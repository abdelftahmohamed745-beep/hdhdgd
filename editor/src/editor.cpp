#include "nimbus/editor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "nimbus/asset.hpp"
#include "nimbus/log.hpp"

namespace nimbus::editor {

namespace {
std::string escapeProjectText(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 0x20) return {};
        escaped.push_back(static_cast<char>(c));
    }
  }
  return escaped;
}

std::string unescapeProjectText(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      const char n = text[++i];
      switch (n) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: out.push_back(n); break;
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

bool extractJsonStringField(const std::string& json, const char* key, std::string& out) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  if (pos >= json.size() || json[pos] != '"') return false;
  ++pos;
  std::string raw;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '\\' && pos < json.size()) {
      raw.push_back(c);
      raw.push_back(json[pos++]);
      continue;
    }
    if (c == '"') {
      out = unescapeProjectText(raw);
      return true;
    }
    raw.push_back(c);
  }
  return false;
}

bool writeAtomically(const std::filesystem::path& path, const std::string& contents) {
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.flush();
    if (!file.good()) {
      file.close();
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

bool isSupportedNodeType(const std::string& type) {
  return type == "event" || type == "event_tick" || type == "event_collision" ||
         type == "branch" || type == "for_loop" || type == "add" || type == "sub" ||
         type == "mul" || type == "div" || type == "compare" || type == "get_var" ||
         type == "set_var" || type == "get_position" || type == "set_position" ||
         type == "apply_force" || type == "play_sound" || type == "spawn_particle" ||
         type == "print";
}

bool isPureValueNode(const std::string& type) {
  return type == "add" || type == "sub" || type == "mul" || type == "div" ||
         type == "compare" || type == "get_var" || type == "get_position";
}

}  // namespace

void CommandHistory::execute(UndoCommand cmd) {
  if (cmd.redo) cmd.redo();
  record(std::move(cmd));
}

void CommandHistory::record(UndoCommand cmd) {
  if (cursor_ < commands_.size()) {
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_), commands_.end());
  }
  commands_.push_back(std::move(cmd));
  if (commands_.size() > kMaxHistory) {
    commands_.erase(commands_.begin());
  }
  cursor_ = commands_.size();
}

bool CommandHistory::undo() {
  if (cursor_ == 0) return false;
  --cursor_;
  if (commands_[cursor_].undo) commands_[cursor_].undo();
  return true;
}

bool CommandHistory::redo() {
  if (cursor_ >= commands_.size()) return false;
  if (commands_[cursor_].redo) commands_[cursor_].redo();
  ++cursor_;
  return true;
}

uint32_t VisualScript::addNode(const std::string& type, const std::string& name, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || nextId_ == 0 || !isSupportedNodeType(type))
    return 0;
  VsNode node;
  node.id = nextId_++;
  node.type = type;
  node.name = name;
  node.x = x;
  node.y = y;

  if (type == "event") {
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "event_tick") {
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
    node.outputs.push_back({"dt", VsPin::Type::Float, false});
  } else if (type == "event_collision") {
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
    node.outputs.push_back({"other", VsPin::Type::Entity, false});
  } else if (type == "branch") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"condition", VsPin::Type::Bool, true});
    node.outputs.push_back({"true", VsPin::Type::Exec, false});
    node.outputs.push_back({"false", VsPin::Type::Exec, false});
  } else if (type == "for_loop") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"count", VsPin::Type::Float, true});
    node.outputs.push_back({"body", VsPin::Type::Exec, false});
    node.outputs.push_back({"index", VsPin::Type::Float, false});
    node.outputs.push_back({"completed", VsPin::Type::Exec, false});
  } else if (type == "add" || type == "sub" || type == "mul" || type == "div") {
    node.inputs.push_back({"a", VsPin::Type::Float, true});
    node.inputs.push_back({"b", VsPin::Type::Float, true});
    node.outputs.push_back({"result", VsPin::Type::Float, false});
  } else if (type == "compare") {
    node.inputs.push_back({"a", VsPin::Type::Float, true});
    node.inputs.push_back({"b", VsPin::Type::Float, true});
    node.outputs.push_back({"greater", VsPin::Type::Bool, false});
    node.outputs.push_back({"equal", VsPin::Type::Bool, false});
  } else if (type == "get_var") {
    node.outputs.push_back({"value", VsPin::Type::Float, false});
  } else if (type == "set_var") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"value", VsPin::Type::Float, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "get_position") {
    node.inputs.push_back({"target", VsPin::Type::Entity, true});
    node.outputs.push_back({"value", VsPin::Type::Vector3, false});
  } else if (type == "set_position") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"target", VsPin::Type::Entity, true});
    node.inputs.push_back({"value", VsPin::Type::Vector3, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "apply_force") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"target", VsPin::Type::Entity, true});
    node.inputs.push_back({"force", VsPin::Type::Vector3, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "play_sound") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"frequency", VsPin::Type::Float, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "spawn_particle") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"origin", VsPin::Type::Vector3, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  } else if (type == "print") {
    node.inputs.push_back({"in", VsPin::Type::Exec, true});
    node.inputs.push_back({"message", VsPin::Type::Float, true});
    node.outputs.push_back({"out", VsPin::Type::Exec, false});
  }
  nodes_.push_back(node);
  return node.id;
}

void VisualScript::removeNode(uint32_t id) {
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                              [id](const VsNode& node) { return node.id == id; }),
               nodes_.end());
  edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                              [id](const VsEdge& edge) {
                                return edge.fromNode == id || edge.toNode == id;
                              }),
               edges_.end());
}

void VisualScript::clear() {
  nodes_.clear();
  edges_.clear();
  nextId_ = 1;
}

bool VisualScript::addEdge(const VsEdge& edge) {
  const auto from = std::find_if(nodes_.begin(), nodes_.end(), [&](const VsNode& node) {
    return node.id == edge.fromNode;
  });
  const auto to = std::find_if(nodes_.begin(), nodes_.end(), [&](const VsNode& node) {
    return node.id == edge.toNode;
  });
  if (from == nodes_.end() || to == nodes_.end() || edge.fromPin >= from->outputs.size() ||
      edge.toPin >= to->inputs.size() ||
      from->outputs[edge.fromPin].type != to->inputs[edge.toPin].type) return false;
  const auto occupied = std::find_if(edges_.begin(), edges_.end(), [&](const VsEdge& current) {
    return current.toNode == edge.toNode && current.toPin == edge.toPin;
  });
  if (occupied != edges_.end()) return false;
  const auto duplicate = std::find_if(edges_.begin(), edges_.end(), [&](const VsEdge& current) {
    return current.fromNode == edge.fromNode && current.fromPin == edge.fromPin &&
           current.toNode == edge.toNode && current.toPin == edge.toPin;
  });
  if (duplicate != edges_.end()) return false;
  edges_.push_back(edge);
  return true;
}

void VisualScript::removeEdge(uint32_t fromNode, uint32_t fromPin, uint32_t toNode,
                              uint32_t toPin) {
  edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                              [&](const VsEdge& edge) {
                                return edge.fromNode == fromNode && edge.fromPin == fromPin &&
                                       edge.toNode == toNode && edge.toPin == toPin;
                              }),
               edges_.end());
}

double VisualScript::evaluate(uint32_t entry,
                              const std::unordered_map<std::string, double>& inputs) const {
  const auto findNode = [&](uint32_t id) -> const VsNode* {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
                                    [id](const VsNode& node) { return node.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
  };
  std::unordered_set<uint32_t> evaluating;
  std::function<double(uint32_t)> evaluateNode;
  evaluateNode = [&](uint32_t id) -> double {
    const VsNode* node = findNode(id);
    if (!node || !evaluating.insert(id).second) return 0.0;
    const auto inputValue = [&](uint32_t pinIndex, const std::string& pinName) -> double {
      const auto edge = std::find_if(edges_.begin(), edges_.end(), [&](const VsEdge& candidate) {
        return candidate.toNode == id && candidate.toPin == pinIndex;
      });
      if (edge != edges_.end()) return evaluateNode(edge->fromNode);
      const auto value = inputs.find(pinName);
      return value == inputs.end() ? 0.0 : value->second;
    };
    double result = 0.0;
    if (node->type == "add") result = inputValue(0, "a") + inputValue(1, "b");
    else if (node->type == "sub") result = inputValue(0, "a") - inputValue(1, "b");
    else if (node->type == "mul") result = inputValue(0, "a") * inputValue(1, "b");
    else if (node->type == "div") {
      const double denom = inputValue(1, "b");
      result = std::abs(denom) > 1e-9 ? inputValue(0, "a") / denom : 0.0;
    } else if (node->type == "compare") {
      result = (inputValue(0, "a") > inputValue(1, "b")) ? 1.0 : 0.0;
    } else if (node->type == "get_var") {
      const auto it = inputs.find(node->name);
      result = it != inputs.end() ? it->second : 0.0;
    } else if (node->type == "event_tick") {
      const auto it = inputs.find("dt");
      result = it != inputs.end() ? it->second : 1.0 / 60.0;
    } else if (node->type == "print" || node->type == "play_sound" || node->type == "set_var") {
      result = inputValue(1, node->type == "print" ? "message"
                                 : (node->type == "play_sound" ? "frequency" : "value"));
    }
    evaluating.erase(id);
    return result;
  };
  return evaluateNode(entry);
}

bool VisualScript::evaluateBool(uint32_t entry,
                                const std::unordered_map<std::string, double>& inputs) const {
  const auto findNode = [&](uint32_t id) -> const VsNode* {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
                                    [id](const VsNode& node) { return node.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
  };
  const VsNode* node = findNode(entry);
  if (!node) return false;
  if (node->type == "branch") {
    const auto edge = std::find_if(edges_.begin(), edges_.end(), [&](const VsEdge& candidate) {
      return candidate.toNode == entry && candidate.toPin == 1;
    });
    if (edge != edges_.end()) {
      const VsNode* src = findNode(edge->fromNode);
      if (src && src->type == "compare") {
        // Check which output pin on compare (`0`=greater, `1`=equal).
        const double a = evaluate(edge->fromNode, inputs);  // fallback
        if (edge->fromPin == 1) {
          // Inspect both inputs to compare
          auto getCompIn = [&](uint32_t pinIdx, const char* name) {
            const auto e = std::find_if(edges_.begin(), edges_.end(), [&](const VsEdge& c) {
              return c.toNode == src->id && c.toPin == pinIdx;
            });
            if (e != edges_.end()) return evaluate(e->fromNode, inputs);
            const auto it = inputs.find(name);
            return it != inputs.end() ? it->second : 0.0;
          };
          return std::abs(getCompIn(0, "a") - getCompIn(1, "b")) < 1e-6;
        }
        return a != 0.0;
      }
      return evaluate(edge->fromNode, inputs) != 0.0;
    }
    const auto it = inputs.find("condition");
    return it != inputs.end() && it->second != 0.0;
  }
  return evaluate(entry, inputs) != 0.0;
}

bool VisualScript::execute(uint32_t entry, const VsExecutionContext& context,
                           uint32_t maxExecutedNodes) const {
  if (entry == 0 || maxExecutedNodes == 0) return false;
  const auto findNode = [&](uint32_t id) -> const VsNode* {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
                                    [id](const VsNode& node) { return node.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
  };

  VsExecutionContext mutableCtx = context;
  std::vector<uint32_t> pending{entry};
  std::unordered_set<uint32_t> activeVisit;
  uint32_t executionCount = 0;

  while (!pending.empty()) {
    const uint32_t id = pending.back();
    pending.pop_back();
    if (!activeVisit.insert(id).second) return false;  // Cycle guard.
    if (++executionCount > maxExecutedNodes) return false;
    const VsNode* node = findNode(id);
    if (!node || isPureValueNode(node->type)) return false;
    if (mutableCtx.trace) mutableCtx.trace->push_back(id);

    auto pushExecOut = [&](size_t pinIndex) {
      if (pinIndex >= node->outputs.size() ||
          node->outputs[pinIndex].type != VsPin::Type::Exec) return;
      for (const VsEdge& edge : edges_) {
        if (edge.fromNode == id && edge.fromPin == pinIndex) pending.push_back(edge.toNode);
      }
    };

    if (node->type == "print") {
      if (!mutableCtx.print) return false;
      mutableCtx.print(evaluate(id, mutableCtx.numbers));
      pushExecOut(0);
    } else if (node->type == "set_var") {
      mutableCtx.numbers[node->name] = evaluate(id, mutableCtx.numbers);
      pushExecOut(0);
    } else if (node->type == "set_position") {
      const auto target = mutableCtx.entities.find("target");
      const auto value = mutableCtx.vectors.find("value");
      if (!mutableCtx.setPosition || target == mutableCtx.entities.end() ||
          value == mutableCtx.vectors.end() ||
          !std::isfinite(value->second.x) || !std::isfinite(value->second.y) ||
          !std::isfinite(value->second.z)) return false;
      mutableCtx.setPosition(target->second, value->second);
      pushExecOut(0);
    } else if (node->type == "apply_force") {
      const auto target = mutableCtx.entities.find("target");
      const auto force = mutableCtx.vectors.find("force");
      if (mutableCtx.applyForce && target != mutableCtx.entities.end() &&
          force != mutableCtx.vectors.end()) {
        mutableCtx.applyForce(target->second, force->second);
      }
      pushExecOut(0);
    } else if (node->type == "play_sound") {
      if (mutableCtx.playSound) mutableCtx.playSound(evaluate(id, mutableCtx.numbers));
      pushExecOut(0);
    } else if (node->type == "spawn_particle") {
      const auto origin = mutableCtx.vectors.find("origin");
      if (mutableCtx.spawnParticle && origin != mutableCtx.vectors.end()) {
        mutableCtx.spawnParticle(origin->second);
      }
      pushExecOut(0);
    } else if (node->type == "branch") {
      const bool cond = evaluateBool(id, mutableCtx.numbers);
      pushExecOut(cond ? 0u : 1u);
    } else if (node->type == "for_loop") {
      const auto countIt = mutableCtx.numbers.find("count");
      const int iters =
          countIt != mutableCtx.numbers.end()
              ? std::clamp(static_cast<int>(countIt->second), 0, static_cast<int>(maxExecutedNodes))
              : 0;
      pushExecOut(2);  // Completed runs after loop iterations finish (LIFO stack -> push completed first).
      for (int k = 0; k < iters; ++k) {
        if (++executionCount > maxExecutedNodes) return false;
        pushExecOut(0);
      }
    } else {
      // event / event_tick / event_collision
      pushExecOut(0);
    }
  }
  return executionCount != 0;
}

std::string VisualScript::serializeJson() const {
  std::ostringstream out;
  out << "{\"version\":1,\"nodes\":[";
  for (size_t i = 0; i < nodes_.size(); ++i) {
    if (i) out << ',';
    const VsNode& n = nodes_[i];
    out << "{\"id\":" << n.id << ",\"type\":\"" << escapeProjectText(n.type)
        << "\",\"name\":\"" << escapeProjectText(n.name) << "\",\"x\":" << n.x
        << ",\"y\":" << n.y << "}";
  }
  out << "],\"edges\":[";
  for (size_t i = 0; i < edges_.size(); ++i) {
    if (i) out << ',';
    const VsEdge& e = edges_[i];
    out << "{\"fromNode\":" << e.fromNode << ",\"fromPin\":" << e.fromPin
        << ",\"toNode\":" << e.toNode << ",\"toPin\":" << e.toPin << "}";
  }
  out << "]}";
  return out.str();
}

bool VisualScript::deserializeJson(const std::string& json) {
  if (json.find("\"nodes\":[") == std::string::npos) return false;
  VisualScript parsed;
  std::unordered_map<uint32_t, uint32_t> idMap;

  const size_t nodesStart = json.find("\"nodes\":[");
  const size_t edgesStart = json.find("\"edges\":[");
  const std::string nodesSlice =
      json.substr(nodesStart, (edgesStart != std::string::npos && edgesStart > nodesStart)
                                  ? (edgesStart - nodesStart)
                                  : std::string::npos);
  size_t pos = 0;
  while ((pos = nodesSlice.find('{', pos)) != std::string::npos) {
    const size_t end = nodesSlice.find('}', pos);
    if (end == std::string::npos) return false;
    const std::string obj = nodesSlice.substr(pos, end - pos + 1);
    std::string type, name;
    if (!extractJsonStringField(obj, "type", type)) return false;
    extractJsonStringField(obj, "name", name);
    auto numField = [&](const char* key, double fallback) {
      const std::string needle = std::string("\"") + key + "\":";
      const size_t p = obj.find(needle);
      if (p == std::string::npos) return fallback;
      char* eptr = nullptr;
      const double v = std::strtod(obj.c_str() + p + needle.size(), &eptr);
      return (eptr && std::isfinite(v)) ? v : fallback;
    };
    const uint32_t origId = static_cast<uint32_t>(numField("id", 0.0));
    const float x = static_cast<float>(numField("x", 0.0));
    const float y = static_cast<float>(numField("y", 0.0));
    const uint32_t newId = parsed.addNode(type, name, x, y);
    if (newId == 0) return false;
    if (origId != 0) idMap[origId] = newId;
    pos = end + 1;
  }

  if (edgesStart != std::string::npos) {
    const std::string edgesSlice = json.substr(edgesStart);
    size_t epos = 0;
    while ((epos = edgesSlice.find('{', epos)) != std::string::npos) {
      const size_t end = edgesSlice.find('}', epos);
      if (end == std::string::npos) return false;
      const std::string obj = edgesSlice.substr(epos, end - epos + 1);
      auto numField = [&](const char* key) {
        const std::string needle = std::string("\"") + key + "\":";
        const size_t p = obj.find(needle);
        if (p == std::string::npos) return 0u;
        char* eptr = nullptr;
        const double v = std::strtod(obj.c_str() + p + needle.size(), &eptr);
        return (eptr && std::isfinite(v) && v >= 0.0) ? static_cast<uint32_t>(v) : 0u;
      };
      VsEdge e;
      const uint32_t rawFrom = numField("fromNode");
      const uint32_t rawTo = numField("toNode");
      e.fromNode = idMap.count(rawFrom) ? idMap[rawFrom] : rawFrom;
      e.fromPin = numField("fromPin");
      e.toNode = idMap.count(rawTo) ? idMap[rawTo] : rawTo;
      e.toPin = numField("toPin");
      if (!parsed.addEdge(e)) return false;
      epos = end + 1;
    }
  }

  *this = std::move(parsed);
  return true;
}

std::string VisualScript::toLua() const {
  std::ostringstream out;
  out << "local graph = {}\n";
  for (const VsNode& node : nodes_) {
    out << "-- node " << node.id << " " << node.type << "\n";
    if (node.type == "add") {
      out << "graph[" << node.id << "] = function(a, b) return a + b end\n";
    } else if (node.type == "sub") {
      out << "graph[" << node.id << "] = function(a, b) return a - b end\n";
    } else if (node.type == "mul") {
      out << "graph[" << node.id << "] = function(a, b) return a * b end\n";
    } else if (node.type == "div") {
      out << "graph[" << node.id << "] = function(a, b) if math.abs(b) > 1e-9 then return a / b end return 0 end\n";
    } else if (node.type == "compare") {
      out << "graph[" << node.id << "] = function(a, b) return a > b, math.abs(a - b) < 1e-6 end\n";
    } else if (node.type == "branch") {
      out << "graph[" << node.id << "] = function(cond) return cond and true or false end\n";
    } else if (node.type == "for_loop") {
      out << "graph[" << node.id << "] = function(n, body) for i = 0, (n or 0) - 1 do body(i) end end\n";
    } else if (node.type == "get_var") {
      out << "graph[" << node.id << "] = function(vars) return vars[\""
          << escapeProjectText(node.name) << "\"] or 0 end\n";
    } else if (node.type == "set_var") {
      out << "graph[" << node.id << "] = function(vars, v) vars[\""
          << escapeProjectText(node.name) << "\"] = v end\n";
    } else if (node.type == "get_position") {
      out << "graph[" << node.id
          << "] = function(target) return nimbus.get_position(tostring(target)) end\n";
    } else if (node.type == "print") {
      out << "graph[" << node.id << "] = function(message) print(message) end\n";
    } else if (node.type == "set_position") {
      out << "graph[" << node.id
          << "] = function(target, x, y, z) nimbus.set_position(tostring(target), x, y, z) end\n";
    } else if (node.type == "apply_force") {
      out << "graph[" << node.id
          << "] = function(target, fx, fy, fz) nimbus.emit_event(\"force:\" .. tostring(target)) end\n";
    } else if (node.type == "play_sound" || node.type == "spawn_particle") {
      out << "graph[" << node.id << "] = function(arg) nimbus.emit_event(\""
          << node.type << "\") end\n";
    }
  }
  out << "local edges = {\n";
  for (const VsEdge& edge : edges_)
    out << "  {" << edge.fromNode << "," << edge.fromPin << ","
        << edge.toNode << "," << edge.toPin << "},\n";
  out << "}\n";
  return out.str();
}

bool Editor::newProject(const std::string& path, const std::string& name,
                        const std::string& templateName) {
  if (!engine_ || path.empty() || name.empty()) return false;
  const std::filesystem::path project(path);
  const std::filesystem::path manifest = project / "project.json";
  std::error_code error;
  if (std::filesystem::exists(manifest, error) || error) return false;
  std::filesystem::create_directories(project / "scenes", error);
  std::filesystem::create_directories(project / "scripts", error);
  std::filesystem::create_directories(project / "assets", error);
  if (error) return false;
  const std::string escapedName = escapeProjectText(name);
  if (escapedName.empty() && !name.empty()) return false;

  if (templateName == "3d_action") {
    const ecs::Entity cam = engine_->createEntity("MainCamera");
    engine_->world()->add<render::Camera>(cam, render::Camera{});
    setPosition(cam, {0.0f, 3.0f, 8.0f}, false);
    const ecs::Entity sun = engine_->createEntity("DirectionalLight");
    engine_->world()->add<render::Light>(sun, render::Light{});
    const ecs::Entity player = engine_->createEntity("Player");
    engine_->world()->add<physics::RigidBody>(player, physics::RigidBody{});
    setPosition(player, {0.0f, 1.0f, 0.0f}, false);
  } else if (templateName == "ai_sandbox") {
    const ecs::Entity scout = engine_->createEntity("ScoutAgent");
    engine_->world()->add<ai::Agent>(scout, ai::Agent{});
    setPosition(scout, {2.0f, 0.0f, -4.0f}, false);
  }

  const std::string sceneText = engine_->scene()->serialize();
  const std::string manifestText =
      "{\"version\":1,\"name\":\"" + escapedName + "\",\"template\":\"" +
      escapeProjectText(templateName) + "\",\"entryScene\":\"main.scene.json\"}\n";
  if (!writeAtomically(project / "scenes" / "main.scene.json", sceneText) ||
      !writeAtomically(manifest, manifestText)) return false;

  // Save visual script graph beside project.
  writeAtomically(project / "scripts" / "main.vs.json", visualScript_.serializeJson());
  history_.clear();
  projectPath_ = project.lexically_normal().string();
  projectName_ = name;
  log::write(log::Level::Info, "editor: created project '%s' (%s) at %s", name.c_str(),
             templateName.c_str(), path.c_str());
  return true;
}

bool Editor::openProject(const std::string& path) {
  if (!engine_ || path.empty()) return false;
  const std::filesystem::path project(path);
  std::ifstream manifest(project / "project.json", std::ios::binary);
  if (!manifest) return false;
  const std::string manifestText((std::istreambuf_iterator<char>(manifest)),
                                 std::istreambuf_iterator<char>());
  std::string parsedName;
  if (manifestText.find("\"version\":1") == std::string::npos ||
      !extractJsonStringField(manifestText, "name", parsedName)) return false;

  std::ifstream sceneFile(project / "scenes" / "main.scene.json", std::ios::binary);
  if (!sceneFile) return false;
  const std::string sceneText((std::istreambuf_iterator<char>(sceneFile)),
                              std::istreambuf_iterator<char>());
  if (!engine_->scene()->deserialize(sceneText)) return false;
  if (!engine_->loadProject(project.string())) return false;

  std::ifstream vsFile(project / "scripts" / "main.vs.json", std::ios::binary);
  if (vsFile) {
    const std::string vsText((std::istreambuf_iterator<char>(vsFile)),
                             std::istreambuf_iterator<char>());
    visualScript_.deserializeJson(vsText);
  }

  history_.clear();
  projectPath_ = project.lexically_normal().string();
  projectName_ = parsedName.empty() ? project.filename().string() : parsedName;
  log::write(log::Level::Info, "editor: opened project '%s'", projectName_.c_str());
  return true;
}

bool Editor::saveProject(const std::string& path) {
  if (!engine_) return false;
  const std::filesystem::path project(path.empty() ? projectPath_ : path);
  if (project.empty()) return false;
  std::error_code error;
  if (!std::filesystem::is_regular_file(project / "project.json", error) || error) return false;
  std::filesystem::create_directories(project / "scenes", error);
  std::filesystem::create_directories(project / "scripts", error);
  if (error) return false;
  const std::string sceneText = engine_->scene()->serialize();
  if (!writeAtomically(project / "scenes" / "main.scene.json", sceneText)) return false;
  writeAtomically(project / "scripts" / "main.vs.json", visualScript_.serializeJson());
  projectPath_ = project.lexically_normal().string();
  log::write(log::Level::Info, "editor: saved %zu bytes of scene to %s", sceneText.size(),
             projectPath_.c_str());
  return true;
}

bool Editor::exportProject(const std::string& destPath) {
  if (!engine_ || destPath.empty()) return false;
  const std::filesystem::path dst(destPath);
  std::error_code error;
  std::filesystem::create_directories(dst / "scenes", error);
  std::filesystem::create_directories(dst / "scripts", error);
  if (error) return false;

  const std::string sceneText = engine_->scene()->serialize();
  if (!writeAtomically(dst / "scenes" / "main.scene.json", sceneText)) return false;

  // Export compiled Lua from visual script + copy scripts.
  if (!visualScript_.nodes().empty()) {
    if (!writeAtomically(dst / "scripts" / "visual_script.lua", visualScript_.toLua()))
      return false;
  }
  if (!projectPath_.empty()) {
    const std::filesystem::path srcScripts = std::filesystem::path(projectPath_) / "scripts";
    if (std::filesystem::is_directory(srcScripts, error)) {
      for (const auto& entry : std::filesystem::directory_iterator(srcScripts, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
          std::filesystem::copy_file(entry.path(), dst / "scripts" / entry.path().filename(),
                                     std::filesystem::copy_options::overwrite_existing, error);
        }
      }
    }
  }

  // Build self-contained assets.nbpk bundle.
  asset::PackBuilder builder;
  if (!projectPath_.empty()) {
    const std::filesystem::path srcAssets = std::filesystem::path(projectPath_) / "assets";
    if (std::filesystem::is_directory(srcAssets, error)) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(srcAssets, error)) {
        if (!entry.is_regular_file()) continue;
        const std::string rel =
            std::filesystem::relative(entry.path(), srcAssets, error).generic_string();
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        builder.addData(rel, asset::Kind::Unknown, bytes);
      }
    }
  }
  const std::string mainScene = "scenes/main.scene.json";
  const std::vector<uint8_t> sceneBytes(sceneText.begin(), sceneText.end());
  builder.addData(mainScene, asset::Kind::Scene, sceneBytes);
  if (!builder.write((dst / "assets.nbpk").string())) return false;

  const std::string exportManifest =
      "{\"version\":1,\"name\":\"" +
      escapeProjectText(projectName_.empty() ? "NimbusExport" : projectName_) +
      "\",\"entryScene\":\"scenes/main.scene.json\",\"pack\":\"assets.nbpk\"}\n";
  return writeAtomically(dst / "project.json", exportManifest);
}

std::vector<ecs::Entity> Editor::entities() const {
  std::vector<ecs::Entity> out;
  for (ecs::Entity root : engine_->scene()->roots()) {
    std::vector<ecs::Entity> stack{root};
    while (!stack.empty()) {
      const ecs::Entity e = stack.back();
      stack.pop_back();
      out.push_back(e);
      if (const scene::Node* node = engine_->scene()->node(e))
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
          stack.push_back(*it);
    }
  }
  return out;
}

std::string Editor::entitySummary(ecs::Entity entity) const {
  const scene::Node* node = engine_->scene()->node(entity);
  if (!node) return {};
  std::ostringstream out;
  out << "entity " << entity << " '" << engine_->world()->name(entity) << "'"
      << " children=" << node->children.size()
      << " pos=(" << node->local.position.x << ',' << node->local.position.y << ','
      << node->local.position.z << ')';
  return out.str();
}

void Editor::setPosition(ecs::Entity entity, const math::Vec3& position, bool recordUndo) {
  scene::Transform* transform = engine_->scene()->local(entity);
  if (!transform) return;
  const math::Vec3 oldPos = transform->position;
  transform->position = position;
  engine_->scene()->markDirty(entity);
  if (recordUndo && (oldPos.x != position.x || oldPos.y != position.y || oldPos.z != position.z)) {
    Engine* eng = engine_;
    history_.record({"Set Position",
                     [eng, entity, oldPos]() {
                       if (auto* tf = eng->scene()->local(entity)) {
                         tf->position = oldPos;
                         eng->scene()->markDirty(entity);
                       }
                     },
                     [eng, entity, position]() {
                       if (auto* tf = eng->scene()->local(entity)) {
                         tf->position = position;
                         eng->scene()->markDirty(entity);
                       }
                     }});
  }
}

void Editor::setTransform(ecs::Entity entity, const scene::Transform& transform, bool recordUndo) {
  scene::Transform* existing = engine_->scene()->local(entity);
  if (!existing) return;
  const scene::Transform oldTf = *existing;
  *existing = transform;
  engine_->scene()->markDirty(entity);
  if (recordUndo) {
    Engine* eng = engine_;
    history_.record({"Set Transform",
                     [eng, entity, oldTf]() {
                       if (auto* tf = eng->scene()->local(entity)) {
                         *tf = oldTf;
                         eng->scene()->markDirty(entity);
                       }
                     },
                     [eng, entity, transform]() {
                       if (auto* tf = eng->scene()->local(entity)) {
                         *tf = transform;
                         eng->scene()->markDirty(entity);
                       }
                     }});
  }
}

math::Vec3 Editor::getPosition(ecs::Entity entity) const {
  const scene::Transform* transform = engine_->scene()->local(entity);
  return transform ? transform->position : math::Vec3{0, 0, 0};
}

std::vector<LogEntry> Editor::logs(uint32_t offset, uint32_t limit) const {
  std::vector<LogEntry> out;
  for (const auto& [level, text] : log::ring()->read(offset, limit))
    out.push_back(LogEntry{level, text, 0.0});
  return out;
}

uint32_t Editor::logTotal() const { return log::ring()->total(); }

void Editor::play() { engine_->play(); }
void Editor::pause() { engine_->pause(); }
void Editor::stop() { engine_->stop(); }
void Editor::stepFrame() {
  engine_->step();
  engine_->renderFrame();
}

}  // namespace nimbus::editor
