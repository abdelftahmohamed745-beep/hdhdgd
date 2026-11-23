// Nimbus MCP server implementation: JSON-RPC 2.0 over stdio, strict path confinement,
// 35+ schema-defined engine & editor tools.
#include "nimbus/mcp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

#include "nimbus/asset.hpp"
#include "nimbus/log.hpp"

namespace nimbus::mcp {

// ---- Minimal JSON --------------------------------------------------------
namespace json {

struct Value {
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  const Value* find(const std::string& key) const {
    for (const auto& [name, value] : object)
      if (name == key) return &value;
    return nullptr;
  }
  double asNumber(double fallback = 0.0) const {
    return type == Type::Number ? number : fallback;
  }
  std::string asString(const std::string& fallback = {}) const {
    return type == Type::String ? string : fallback;
  }
  bool asBool(bool fallback = false) const { return type == Type::Bool ? boolean : fallback; }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  bool parse(Value& out) {
    if (text_.size() > kMaxDocumentBytes) return false;
    skip();
    if (!parseValue(out, 0)) return false;
    skip();
    return position_ == text_.size();
  }

 private:
  static constexpr size_t kMaxDocumentBytes = 1024 * 1024;
  static constexpr size_t kMaxDepth = 64;
  static constexpr size_t kMaxValues = 100000;

  void skip() {
    while (position_ < text_.size() &&
           (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' ||
            text_[position_] == '\t'))
      ++position_;
  }

  bool consume(char expected) {
    skip();
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  bool parseValue(Value& out, size_t depth) {
    skip();
    if (depth > kMaxDepth || ++values_ > kMaxValues || position_ >= text_.size()) return false;
    const char c = text_[position_];
    if (c == '{') return parseObject(out, depth + 1);
    if (c == '[') return parseArray(out, depth + 1);
    if (c == '"') {
      out.type = Value::Type::String;
      return parseString(out.string);
    }
    if (c == 't' || c == 'f') return parseBool(out);
    if (c == 'n') {
      if (text_.compare(position_, 4, "null") != 0) return false;
      position_ += 4;
      out.type = Value::Type::Null;
      return true;
    }
    return parseNumber(out);
  }

  bool parseObject(Value& out, size_t depth) {
    out.type = Value::Type::Object;
    if (!consume('{')) return false;
    skip();
    if (consume('}')) return true;
    for (;;) {
      skip();
      if (position_ >= text_.size() || text_[position_] != '"') return false;
      std::string key;
      if (!parseString(key) || !consume(':')) return false;
      if (out.object.find(key) != out.object.end()) return false;
      Value value;
      if (!parseValue(value, depth)) return false;
      out.object.emplace(std::move(key), std::move(value));
      skip();
      if (consume(',')) continue;
      return consume('}');
    }
  }

  bool parseArray(Value& out, size_t depth) {
    out.type = Value::Type::Array;
    if (!consume('[')) return false;
    skip();
    if (consume(']')) return true;
    for (;;) {
      Value value;
      if (!parseValue(value, depth)) return false;
      out.array.push_back(std::move(value));
      skip();
      if (consume(',')) continue;
      return consume(']');
    }
  }

  bool parseHex4(uint32_t& codepoint) {
    if (position_ + 4 > text_.size()) return false;
    codepoint = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[position_++];
      codepoint <<= 4;
      if (c >= '0' && c <= '9') codepoint |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') codepoint |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') codepoint |= static_cast<uint32_t>(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  static void appendUtf8(std::string& output, uint32_t cp) {
    if (cp <= 0x7f) output.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }

  bool parseString(std::string& out) {
    if (!consume('"')) return false;
    out.clear();
    while (position_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[position_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= text_.size()) return false;
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!parseHex4(cp)) return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') return false;
            position_ += 2;
            uint32_t low = 0;
            if (!parseHex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return false;
          }
          appendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool parseBool(Value& out) {
    if (text_.compare(position_, 4, "true") == 0) {
      position_ += 4;
      out.type = Value::Type::Bool;
      out.boolean = true;
      return true;
    }
    if (text_.compare(position_, 5, "false") == 0) {
      position_ += 5;
      out.type = Value::Type::Bool;
      out.boolean = false;
      return true;
    }
    return false;
  }

  bool parseNumber(Value& out) {
    const size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-') ++position_;
    if (position_ >= text_.size()) return false;
    if (text_[position_] == '0') {
      ++position_;
      if (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_])))
        return false;
    } else {
      if (text_[position_] < '1' || text_[position_] > '9') return false;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      const size_t fraction = position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
      if (fraction == position_) return false;
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-'))
        ++position_;
      const size_t exponent = position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
      if (exponent == position_) return false;
    }
    const std::string token = text_.substr(start, position_ - start);
    char* end = nullptr;
    out.number = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(out.number)) return false;
    out.type = Value::Type::Number;
    return true;
  }

  const std::string& text_;
  size_t position_ = 0;
  size_t values_ = 0;
};

std::string escape(const std::string& text) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(hex[c >> 4]);
          out.push_back(hex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string quote(const std::string& text) { return "\"" + escape(text) + "\""; }

std::string stringify(const Value& value) {
  switch (value.type) {
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return value.boolean ? "true" : "false";
    case Value::Type::Number: {
      if (!std::isfinite(value.number)) return "null";
      std::ostringstream out;
      out << std::setprecision(std::numeric_limits<double>::max_digits10) << value.number;
      return out.str();
    }
    case Value::Type::String: return quote(value.string);
    case Value::Type::Array: {
      std::ostringstream out;
      out << '[';
      for (size_t i = 0; i < value.array.size(); ++i) {
        if (i) out << ',';
        out << stringify(value.array[i]);
      }
      out << ']';
      return out.str();
    }
    case Value::Type::Object: {
      std::ostringstream out;
      out << '{';
      bool first = true;
      for (const auto& [key, member] : value.object) {
        if (!first) out << ',';
        first = false;
        out << quote(key) << ':' << stringify(member);
      }
      out << '}';
      return out.str();
    }
  }
  return "null";
}

std::string number(double value) {
  Value jsonNumber;
  jsonNumber.type = Value::Type::Number;
  jsonNumber.number = value;
  return stringify(jsonNumber);
}

}  // namespace json

// ---- Tool helpers --------------------------------------------------------

namespace {

std::string levelName(log::Level level) {
  switch (level) {
    case log::Level::Debug: return "debug";
    case log::Level::Info: return "info";
    case log::Level::Warning: return "warning";
    case log::Level::Error: return "error";
    case log::Level::Fatal: return "fatal";
  }
  return "info";
}

std::string resultOk(const std::string& payload) {
  std::ostringstream out;
  out << "{\"ok\":true,\"result\":" << payload << "}";
  return out.str();
}

std::string stringField(const json::Value& args, const std::string& key,
                        const std::string& fallback = {}) {
  const json::Value* value = args.find(key);
  return value ? value->asString(fallback) : fallback;
}

double numberField(const json::Value& args, const std::string& key, double fallback = 0.0) {
  const json::Value* value = args.find(key);
  return value ? value->asNumber(fallback) : fallback;
}

math::Vec3 vec3Field(const json::Value& args, const std::string& key,
                     math::Vec3 fallback = {0, 0, 0}) {
  const json::Value* value = args.find(key);
  if (!value || value->type != json::Value::Type::Array || value->array.size() < 3) return fallback;
  return {static_cast<float>(value->array[0].asNumber(fallback.x)),
          static_cast<float>(value->array[1].asNumber(fallback.y)),
          static_cast<float>(value->array[2].asNumber(fallback.z))};
}

math::Quat quatField(const json::Value& args, const std::string& key,
                     math::Quat fallback = {0, 0, 0, 1}) {
  const json::Value* value = args.find(key);
  if (!value || value->type != json::Value::Type::Array || value->array.size() < 4) return fallback;
  return math::quatNormalize({static_cast<float>(value->array[0].asNumber(fallback.x)),
                              static_cast<float>(value->array[1].asNumber(fallback.y)),
                              static_cast<float>(value->array[2].asNumber(fallback.z)),
                              static_cast<float>(value->array[3].asNumber(fallback.w))});
}

}  // namespace

void Server::setWorkspaceRoot(const std::string& rootPath) {
  if (rootPath.empty()) {
    workspaceRoot_.clear();
    return;
  }
  std::error_code ec;
  const std::filesystem::path abs = std::filesystem::weakly_canonical(rootPath, ec);
  workspaceRoot_ = ec ? std::filesystem::path(rootPath).lexically_normal().string() : abs.string();
}

bool Server::validateAndResolvePath(const std::string& rawPath, std::string& outResolved,
                                    std::string& outError) const {
  if (rawPath.empty()) {
    outError = "path is required";
    return false;
  }
  for (unsigned char c : rawPath) {
    if (c < 0x20) {
      outError = "invalid control character in path";
      return false;
    }
  }
  const std::filesystem::path p(rawPath);
  for (const auto& part : p) {
    if (part == "..") {
      outError = "path traversal ('..') is forbidden";
      return false;
    }
  }
  std::error_code ec;
  if (!workspaceRoot_.empty()) {
    const std::filesystem::path root = std::filesystem::weakly_canonical(workspaceRoot_, ec);
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(p.is_absolute() ? p : (root / p), ec);
    auto rootStr = root.generic_string();
    auto candStr = candidate.generic_string();
    if (!rootStr.empty() && rootStr.back() != '/') rootStr.push_back('/');
    if (candStr != root.generic_string() && candStr.compare(0, rootStr.size(), rootStr) != 0) {
      outError = "path escapes confined workspace root";
      return false;
    }
    outResolved = candidate.string();
    return true;
  }
  outResolved = p.lexically_normal().string();
  return true;
}

void Server::addTool(const std::string& name, const std::string& description,
                     const std::string& inputSchemaJson, Handler handler) {
  names_.push_back(name);
  descriptions_.push_back(description);
  schemas_.push_back(inputSchemaJson.empty() ? "{\"type\":\"object\"}" : inputSchemaJson);
  tools_[name] = std::move(handler);
}

void Server::registerTools() {
  names_.clear();
  descriptions_.clear();
  schemas_.clear();
  tools_.clear();

  addTool("engine_status", "Report engine state, frame, device, script backend and stats.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            std::ostringstream out;
            nimbus::Engine* engine = editor_->engine();
            const char* state = engine->state() == nimbus::State::Playing    ? "playing"
                                : engine->state() == nimbus::State::Paused  ? "paused"
                                                                             : "stopped";
            out << "{\"state\":" << json::quote(state) << ",\"frame\":" << engine->frame()
                << ",\"elapsed\":" << json::number(engine->elapsed())
                << ",\"entities\":" << engine->world()->entityCount()
                << ",\"nodes\":" << engine->scene()->nodeCount()
                << ",\"width\":" << engine->viewportWidth()
                << ",\"height\":" << engine->viewportHeight()
                << ",\"lua\":" << json::quote(engine->scripts() ? engine->scripts()->backendName() : "none")
                << ",\"undoCount\":" << editor_->history()->undoCount()
                << ",\"redoCount\":" << editor_->history()->redoCount()
                << ",\"device\":" << json::quote(engine->device() ? engine->device()->info().deviceName : "headless")
                << ",\"api\":" << json::quote(engine->device()
                                                  ? (engine->device()->info().api == rhi::Api::Vulkan
                                                         ? "vulkan"
                                                         : (engine->device()->info().api == rhi::Api::OpenGlEs
                                                                ? "opengl-es"
                                                                : "headless"))
                                                  : "none")
                << ",\"drawCalls\":" << engine->stats().drawCalls
                << ",\"triangles\":" << engine->stats().triangles
                << ",\"fps\":" << json::number(engine->stats().fps) << "}";
            return Result{true, resultOk(out.str())};
          });

  addTool("engine_resize", "Resize the engine viewport and UI root.",
          "{\"type\":\"object\",\"required\":[\"width\",\"height\"],\"properties\":{"
          "\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const uint32_t w = static_cast<uint32_t>(numberField(parsed, "width"));
            const uint32_t h = static_cast<uint32_t>(numberField(parsed, "height"));
            if (w == 0 || h == 0) return Result{false, "", "width and height must be > 0"};
            editor_->engine()->resize(w, h);
            return Result{true, resultOk("{\"width\":" + std::to_string(w) + ",\"height\":" +
                                         std::to_string(h) + "}")};
          });

  addTool("project_new", "Create a new project directory with an optional template.",
          "{\"type\":\"object\",\"required\":[\"path\"],\"properties\":{"
          "\"path\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},"
          "\"template\":{\"type\":\"string\",\"enum\":[\"empty\",\"3d_action\",\"ai_sandbox\"]}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            std::string safePath, err;
            if (!validateAndResolvePath(stringField(parsed, "path"), safePath, err))
              return Result{false, "", err};
            const std::string name = stringField(parsed, "name", "project");
            const std::string tmpl = stringField(parsed, "template", "empty");
            const bool ok = editor_->newProject(safePath, name, tmpl);
            return Result{ok, resultOk("{\"path\":" + json::quote(safePath) + "}"),
                          ok ? "" : "newProject failed"};
          });

  addTool("project_open", "Open an existing project directory.",
          "{\"type\":\"object\",\"required\":[\"path\"],\"properties\":{\"path\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            std::string safePath, err;
            if (!validateAndResolvePath(stringField(parsed, "path"), safePath, err))
              return Result{false, "", err};
            const bool ok = editor_->openProject(safePath);
            return Result{ok, resultOk("{\"path\":" + json::quote(safePath) + ",\"name\":" +
                                       json::quote(editor_->projectName()) + "}"),
                          ok ? "" : "openProject failed"};
          });

  addTool("project_save", "Save the current scene and visual script to the project.",
          "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const std::string raw = stringField(parsed, "path", editor_->projectPath());
            std::string safePath, err;
            if (!validateAndResolvePath(raw, safePath, err)) return Result{false, "", err};
            const bool ok = editor_->saveProject(safePath);
            return Result{ok, resultOk("{\"path\":" + json::quote(safePath) + "}"),
                          ok ? "" : "saveProject failed"};
          });

  addTool("project_export", "Export a self-contained runnable game bundle (.nbpk + scenes + scripts).",
          "{\"type\":\"object\",\"required\":[\"dest\"],\"properties\":{\"dest\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            std::string safeDest, err;
            if (!validateAndResolvePath(stringField(parsed, "dest"), safeDest, err))
              return Result{false, "", err};
            const bool ok = editor_->exportProject(safeDest);
            return Result{ok, resultOk("{\"dest\":" + json::quote(safeDest) + "}"),
                          ok ? "" : "exportProject failed"};
          });

  addTool("scene_list_entities", "List every entity with its name, parent, transform and components.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            std::ostringstream out;
            out << "[";
            const std::vector<ecs::Entity> entities = editor_->entities();
            for (size_t i = 0; i < entities.size(); ++i) {
              if (i) out << ',';
              const ecs::Entity e = entities[i];
              const scene::Node* node = editor_->engine()->scene()->node(e);
              const auto comps = editor_->engine()->world()->componentNames(e);
              out << "{\"entity\":" << e
                  << ",\"name\":" << json::quote(editor_->engine()->world()->name(e))
                  << ",\"parent\":"
                  << (node && node->parent != ecs::kNullEntity ? std::to_string(node->parent)
                                                               : std::string("-1"))
                  << ",\"position\":[" << node->local.position.x << ',' << node->local.position.y
                  << ',' << node->local.position.z << "]"
                  << ",\"rotation\":[" << node->local.rotation.x << ',' << node->local.rotation.y
                  << ',' << node->local.rotation.z << ',' << node->local.rotation.w << "]"
                  << ",\"scale\":[" << node->local.scale.x << ',' << node->local.scale.y << ','
                  << node->local.scale.z << "],\"components\":[";
              for (size_t c = 0; c < comps.size(); ++c) {
                if (c) out << ',';
                out << json::quote(comps[c]);
              }
              out << "]}";
            }
            out << "]";
            return Result{true, resultOk(out.str())};
          });

  addTool("scene_list_scenes", "List scenes managed by SceneManager.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const auto names = editor_->engine()->sceneManager()->sceneNames();
            std::ostringstream out;
            out << "{\"active\":" << json::quote(editor_->engine()->scene()->name())
                << ",\"scenes\":[";
            for (size_t i = 0; i < names.size(); ++i) {
              if (i) out << ',';
              out << json::quote(names[i]);
            }
            out << "]}";
            return Result{true, resultOk(out.str())};
          });

  addTool("scene_save_prefab", "Register an entity subtree as a reusable prefab.",
          "{\"type\":\"object\",\"required\":[\"entity\",\"prefabId\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"prefabId\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            const std::string prefabId = stringField(parsed, "prefabId");
            if (prefabId.empty()) return Result{false, "", "prefabId is required"};
            const bool ok = editor_->engine()->scene()->savePrefabFromNode(entity, prefabId);
            return Result{ok, resultOk("{\"prefabId\":" + json::quote(prefabId) + "}"),
                          ok ? "" : "savePrefabFromNode failed"};
          });

  addTool("scene_instantiate_prefab", "Instantiate a registered prefab into the active scene.",
          "{\"type\":\"object\",\"required\":[\"prefabId\"],\"properties\":{"
          "\"prefabId\":{\"type\":\"string\"},\"parent\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const std::string prefabId = stringField(parsed, "prefabId");
            const json::Value* pVal = parsed.find("parent");
            const ecs::Entity parent =
                pVal && pVal->type == json::Value::Type::Number
                    ? static_cast<ecs::Entity>(pVal->asNumber())
                    : ecs::kNullEntity;
            const ecs::Entity root = editor_->engine()->scene()->instantiatePrefab(prefabId, parent);
            const bool ok = root != ecs::kNullEntity;
            return Result{ok, resultOk("{\"entity\":" + std::to_string(root) + "}"),
                          ok ? "" : "unknown prefab"};
          });

  addTool("entity_create", "Create an entity under an optional parent.",
          "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
          "\"parent\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const std::string name = stringField(parsed, "name", "entity");
            const json::Value* parentValue = parsed.find("parent");
            const ecs::Entity parent =
                parentValue && parentValue->type == json::Value::Type::Number
                    ? static_cast<ecs::Entity>(parentValue->asNumber())
                    : ecs::kNullEntity;
            const ecs::Entity entity = editor_->engine()->createEntity(name);
            if (parent != ecs::kNullEntity) editor_->engine()->scene()->setParent(entity, parent);
            std::ostringstream out;
            out << "{\"entity\":" << entity << ",\"name\":" << json::quote(name) << "}";
            return Result{true, resultOk(out.str())};
          });

  addTool("entity_destroy", "Destroy an entity and its children.",
          "{\"type\":\"object\",\"required\":[\"entity\"],\"properties\":{\"entity\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            editor_->engine()->destroyEntity(entity);
            return Result{true, resultOk("{\"destroyed\":true}")};
          });

  addTool("entity_set_position", "Set an entity's local position (undoable).",
          "{\"type\":\"object\",\"required\":[\"entity\",\"position\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"position\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            editor_->setPosition(entity, vec3Field(parsed, "position"));
            return Result{true, resultOk("{\"entity\":" + std::to_string(entity) + "}")};
          });

  addTool("entity_set_transform", "Set an entity's full position/rotation/scale transform (undoable).",
          "{\"type\":\"object\",\"required\":[\"entity\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"position\":{\"type\":\"array\"},"
          "\"rotation\":{\"type\":\"array\"},\"scale\":{\"type\":\"array\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            const scene::Transform* cur = editor_->engine()->scene()->local(entity);
            if (!cur) return Result{false, "", "entity not found"};
            scene::Transform tf = *cur;
            if (parsed.find("position")) tf.position = vec3Field(parsed, "position", tf.position);
            if (parsed.find("rotation")) tf.rotation = quatField(parsed, "rotation", tf.rotation);
            if (parsed.find("scale")) tf.scale = vec3Field(parsed, "scale", tf.scale);
            editor_->setTransform(entity, tf);
            return Result{true, resultOk("{\"entity\":" + std::to_string(entity) + "}")};
          });

  addTool("entity_get", "Read an entity's transform and component summary.",
          "{\"type\":\"object\",\"required\":[\"entity\"],\"properties\":{\"entity\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            return Result{true, resultOk(json::quote(editor_->entitySummary(entity)))};
          });

  addTool("component_list", "List serialized components on an entity with their JSON values.",
          "{\"type\":\"object\",\"required\":[\"entity\"],\"properties\":{\"entity\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            ecs::World* world = editor_->engine()->world();
            if (!world->alive(entity)) return Result{false, "", "entity not alive"};
            std::ostringstream out;
            out << "{";
            bool first = true;
            for (const auto& ser : world->serializers()) {
              if (ser.has && ser.has(*world, entity)) {
                if (!first) out << ',';
                first = false;
                out << json::quote(ser.name) << ':' << (ser.serializeJson ? ser.serializeJson(*world, entity) : "{}");
              }
            }
            out << "}";
            return Result{true, resultOk(out.str())};
          });

  addTool("component_add", "Add or configure a serialized component on an entity.",
          "{\"type\":\"object\",\"required\":[\"entity\",\"component\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"component\":{\"type\":\"string\"},\"data\":{\"type\":\"object\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            const std::string comp = stringField(parsed, "component");
            const json::Value* data = parsed.find("data");
            const std::string dataJson = data ? json::stringify(*data) : "{}";
            ecs::World* world = editor_->engine()->world();
            if (!world->alive(entity)) return Result{false, "", "entity not alive"};
            for (const auto& ser : world->serializers()) {
              if (ser.name == comp && ser.deserializeJson) {
                const bool ok = ser.deserializeJson(*world, entity, dataJson);
                return Result{ok, resultOk("{\"added\":" + json::quote(comp) + "}"),
                              ok ? "" : "component deserialize failed"};
              }
            }
            return Result{false, "", "unknown component: " + comp};
          });

  addTool("component_remove", "Remove a named component from an entity.",
          "{\"type\":\"object\",\"required\":[\"entity\",\"component\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"component\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            const std::string comp = stringField(parsed, "component");
            ecs::World* world = editor_->engine()->world();
            if (!world->alive(entity)) return Result{false, "", "entity not alive"};
            if (comp == "RigidBody") world->remove<physics::RigidBody>(entity);
            else if (comp == "Collider") world->remove<physics::Collider>(entity);
            else if (comp == "BoxCollider") world->remove<physics::BoxCollider>(entity);
            else if (comp == "SphereCollider") world->remove<physics::SphereCollider>(entity);
            else if (comp == "CapsuleCollider") world->remove<physics::CapsuleCollider>(entity);
            else if (comp == "Camera") world->remove<render::Camera>(entity);
            else if (comp == "Light") world->remove<render::Light>(entity);
            else return Result{false, "", "unsupported component remove: " + comp};
            return Result{true, resultOk("{\"removed\":" + json::quote(comp) + "}")};
          });

  addTool("editor_undo", "Undo the last recorded editor command.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const bool ok = editor_->undo();
            return Result{ok, resultOk("{\"undone\":" + std::string(ok ? "true" : "false") + "}")};
          });

  addTool("editor_redo", "Redo the previously undone editor command.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const bool ok = editor_->redo();
            return Result{ok, resultOk("{\"redone\":" + std::string(ok ? "true" : "false") + "}")};
          });

  addTool("script_run", "Run a Lua script in the engine's sandboxed Lua runtime.",
          "{\"type\":\"object\",\"required\":[\"source\"],\"properties\":{\"source\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            if (!editor_->engine()->scripts())
              return Result{false, "", "script runtime unavailable"};
            const std::string source = stringField(parsed, "source");
            std::string error;
            const bool ok = editor_->engine()->scripts()->run(source, &error);
            return Result{ok, resultOk("{\"ran\":" + std::string(ok ? "true" : "false") + "}"),
                          ok ? "" : error};
          });

  addTool("script_attach", "Attach a Lua script to an entity.",
          "{\"type\":\"object\",\"required\":[\"entity\",\"source\"],\"properties\":{"
          "\"entity\":{\"type\":\"integer\"},\"source\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            if (!editor_->engine()->scripts())
              return Result{false, "", "script runtime unavailable"};
            const ecs::Entity entity = static_cast<ecs::Entity>(numberField(parsed, "entity"));
            const std::string source = stringField(parsed, "source");
            editor_->engine()->scripts()->attach(entity, source);
            return Result{true, resultOk("{\"attached\":" + std::to_string(entity) + "}")};
          });

  addTool("play", "Start playback.", "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            editor_->play();
            return Result{true, resultOk("{\"state\":\"playing\"}")};
          });

  addTool("pause", "Pause playback.", "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            editor_->pause();
            return Result{true, resultOk("{\"state\":\"paused\"}")};
          });

  addTool("stop", "Stop playback.", "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            editor_->stop();
            return Result{true, resultOk("{\"state\":\"stopped\"}")};
          });

  addTool("step", "Advance one fixed simulation step and render a frame.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            editor_->stepFrame();
            return Result{true, resultOk("{\"frame\":" + std::to_string(editor_->engine()->frame()) + "}")};
          });

  addTool("logs", "Read engine log lines with an offset and limit.",
          "{\"type\":\"object\",\"properties\":{\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const uint32_t offset = static_cast<uint32_t>(numberField(parsed, "offset"));
            const uint32_t limit = static_cast<uint32_t>(numberField(parsed, "limit", 50.0));
            std::ostringstream out;
            out << "[";
            const std::vector<editor::LogEntry> entries = editor_->logs(offset, limit);
            for (size_t i = 0; i < entries.size(); ++i) {
              if (i) out << ',';
              out << "{\"level\":" << json::quote(levelName(entries[i].level))
                  << ",\"text\":" << json::quote(entries[i].text) << "}";
            }
            out << "]";
            return Result{true, resultOk(out.str())};
          });

  addTool("visual_script_list", "List visual scripting nodes, pins and edges.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            return Result{true, resultOk(editor_->visualScript()->serializeJson())};
          });

  addTool("visual_script_add_node", "Add a visual scripting node.",
          "{\"type\":\"object\",\"required\":[\"type\"],\"properties\":{"
          "\"type\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},"
          "\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const uint32_t id = editor_->visualScript()->addNode(
                stringField(parsed, "type", "add"), stringField(parsed, "name", "node"),
                static_cast<float>(numberField(parsed, "x")),
                static_cast<float>(numberField(parsed, "y")));
            return Result{id != 0, resultOk("{\"id\":" + std::to_string(id) + "}"),
                          id != 0 ? "" : "unsupported node type"};
          });

  addTool("visual_script_remove_node", "Remove a visual scripting node and its connected edges.",
          "{\"type\":\"object\",\"required\":[\"id\"],\"properties\":{\"id\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const uint32_t id = static_cast<uint32_t>(numberField(parsed, "id"));
            editor_->visualScript()->removeNode(id);
            return Result{true, resultOk("{\"removed\":" + std::to_string(id) + "}")};
          });

  addTool("visual_script_add_edge", "Connect two compatible pins in the visual script graph.",
          "{\"type\":\"object\",\"required\":[\"fromNode\",\"fromPin\",\"toNode\",\"toPin\"],\"properties\":{"
          "\"fromNode\":{\"type\":\"integer\"},\"fromPin\":{\"type\":\"integer\"},"
          "\"toNode\":{\"type\":\"integer\"},\"toPin\":{\"type\":\"integer\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            editor::VsEdge edge;
            edge.fromNode = static_cast<uint32_t>(numberField(parsed, "fromNode"));
            edge.fromPin = static_cast<uint32_t>(numberField(parsed, "fromPin"));
            edge.toNode = static_cast<uint32_t>(numberField(parsed, "toNode"));
            edge.toPin = static_cast<uint32_t>(numberField(parsed, "toPin"));
            const bool ok = editor_->visualScript()->addEdge(edge);
            return Result{ok, resultOk("{\"connected\":" + std::string(ok ? "true" : "false") + "}"),
                          ok ? "" : "incompatible pins or duplicate edge"};
          });

  addTool("visual_script_execute", "Execute the visual script graph from an entry node with trace.",
          "{\"type\":\"object\",\"required\":[\"entry\"],\"properties\":{"
          "\"entry\":{\"type\":\"integer\"},\"a\":{\"type\":\"number\"},\"b\":{\"type\":\"number\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            const uint32_t entry = static_cast<uint32_t>(numberField(parsed, "entry"));
            std::vector<uint32_t> trace;
            std::vector<double> printed;
            editor::VsExecutionContext ctx;
            ctx.numbers["a"] = numberField(parsed, "a");
            ctx.numbers["b"] = numberField(parsed, "b");
            ctx.numbers["count"] = numberField(parsed, "count", 3.0);
            ctx.trace = &trace;
            ctx.print = [&](double v) { printed.push_back(v); };
            ctx.setPosition = [this](ecs::Entity e, const math::Vec3& p) { editor_->setPosition(e, p); };
            const bool ok = editor_->visualScript()->execute(entry, ctx);
            std::ostringstream out;
            out << "{\"executed\":" << (ok ? "true" : "false") << ",\"trace\":[";
            for (size_t i = 0; i < trace.size(); ++i) {
              if (i) out << ',';
              out << trace[i];
            }
            out << "],\"printed\":[";
            for (size_t i = 0; i < printed.size(); ++i) {
              if (i) out << ',';
              out << json::number(printed[i]);
            }
            out << "]}";
            return Result{ok, resultOk(out.str()), ok ? "" : "visual script execution failed"};
          });

  addTool("visual_script_save", "Serialize the current visual script graph to JSON.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            return Result{true, resultOk(editor_->visualScript()->serializeJson())};
          });

  addTool("visual_script_to_lua", "Export the visual script graph as executable Lua.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            return Result{true,
                          resultOk("\"" + json::escape(editor_->visualScript()->toLua()) + "\"")};
          });

  addTool("test_run", "Run the engine's internal test suite and report pass/fail.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const int failures = editor_->engine()->runTests();
            return Result{failures == 0,
                          resultOk("{\"failures\":" + std::to_string(failures) + "}"),
                          failures == 0 ? "" : "tests failed"};
          });

  addTool("asset_list", "List assets in the loaded pack.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const asset::Pack* pack = editor_->engine()->pack();
            std::ostringstream out;
            out << "[";
            const std::vector<asset::Entry>& entries = pack->entries();
            for (size_t i = 0; i < entries.size(); ++i) {
              if (i) out << ',';
              out << "{\"id\":" << entries[i].id
                  << ",\"name\":" << json::quote(entries[i].name)
                  << ",\"kind\":" << json::quote(asset::kindName(entries[i].kind))
                  << ",\"size\":" << entries[i].size << "}";
            }
            out << "]";
            return Result{true, resultOk(out.str())};
          });

  addTool("asset_import_obj", "Import an OBJ mesh from text into a MeshImport.",
          "{\"type\":\"object\",\"required\":[\"text\"],\"properties\":{\"text\":{\"type\":\"string\"}}}",
          [](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            asset::MeshImport mesh;
            const bool ok = asset::importObj(stringField(parsed, "text"), mesh);
            std::ostringstream out;
            out << "{\"ok\":" << (ok ? "true" : "false")
                << ",\"vertices\":" << mesh.vertices.size()
                << ",\"indices\":" << mesh.indices.size() << "}";
            return Result{ok, resultOk(out.str()), ok ? "" : "import failed"};
          });

  addTool("asset_build_pack", "Package a directory of assets into an .nbpk CRC-verified bundle.",
          "{\"type\":\"object\",\"required\":[\"sourceDir\",\"outputFile\"],\"properties\":{"
          "\"sourceDir\":{\"type\":\"string\"},\"outputFile\":{\"type\":\"string\"}}}",
          [this](const std::string& args) {
            json::Value parsed;
            if (!json::Parser(args).parse(parsed)) return Result{false, "", "invalid json"};
            std::string srcDir, dstFile, err;
            if (!validateAndResolvePath(stringField(parsed, "sourceDir"), srcDir, err) ||
                !validateAndResolvePath(stringField(parsed, "outputFile"), dstFile, err)) {
              return Result{false, "", err};
            }
            asset::PackBuilder builder;
            std::error_code ec;
            if (!std::filesystem::is_directory(srcDir, ec))
              return Result{false, "", "sourceDir not found"};
            uint32_t count = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(srcDir, ec)) {
              if (!entry.is_regular_file()) continue;
              const std::string rel =
                  std::filesystem::relative(entry.path(), srcDir, ec).generic_string();
              std::ifstream f(entry.path(), std::ios::binary);
              if (!f) continue;
              std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
              builder.addData(rel, asset::Kind::Unknown, bytes);
              ++count;
            }
            const bool ok = builder.write(dstFile);
            return Result{ok, resultOk("{\"entries\":" + std::to_string(count) + ",\"output\":" +
                                       json::quote(dstFile) + "}"),
                          ok ? "" : "pack write failed"};
          });

  addTool("build_status", "Report host & Android toolchain capability status honestly.",
          "{\"type\":\"object\",\"properties\":{}}",
          [this](const std::string&) {
            const bool hasLua = editor_->engine()->scripts() && editor_->engine()->scripts()->valid();
            std::ostringstream out;
            out << "{\"host\":{\"compiler\":\"g++ c++20\",\"lua\":"
                << (hasLua ? "true" : "false")
                << ",\"luaBackend\":"
                << json::quote(hasLua ? editor_->engine()->scripts()->backendName() : "none")
                << "},\"android\":{\"gradleProjectConfigured\":true,"
                << "\"localSdkPresent\":false,\"apkStatus\":\"NOT_VERIFIED_LOCALLY\"}}";
            return Result{true, resultOk(out.str())};
          });
}

Result Server::invoke(const std::string& name, const std::string& arguments) {
  const auto it = tools_.find(name);
  if (it == tools_.end()) return Result{false, "", "unknown tool: " + name};
  return it->second(arguments);
}

std::string Server::handle(const std::string& request) {
  json::Value message;
  if (!json::Parser(request).parse(message)) {
    return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"parse error\"}}";
  }
  if (message.type != json::Value::Type::Object) {
    return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"invalid request\"}}";
  }
  const json::Value* id = message.find("id");
  const json::Value* version = message.find("jsonrpc");
  const json::Value* methodValue = message.find("method");
  const json::Value* params = message.find("params");

  auto response = [&](const std::string& result, bool error, const std::string& msg,
                      int errorCode = -32601) {
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"id\":";
    if (id && id->type == json::Value::Type::Number)
      out << json::number(id->number);
    else if (id && id->type == json::Value::Type::String)
      out << json::quote(id->string);
    else
      out << "null";
    if (error) {
      out << ",\"error\":{\"code\":" << errorCode << ",\"message\":" << json::quote(msg) << "}";
    } else {
      out << ",\"result\":" << result;
    }
    out << "}";
    return out.str();
  };

  if (!version || version->type != json::Value::Type::String || version->string != "2.0" ||
      !methodValue || methodValue->type != json::Value::Type::String ||
      methodValue->string.empty()) {
    return response("", true, "invalid JSON-RPC request", -32600);
  }
  const std::string& method = methodValue->string;

  if (method == "initialize") {
    std::ostringstream out;
    out << "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{}},"
           "\"serverInfo\":{\"name\":\"nimbus-mcp\",\"version\":\"0.2.0\"}}";
    return response(out.str(), false, "");
  }
  if (method == "notifications/initialized") return "";
  if (method == "tools/list") {
    std::ostringstream out;
    out << "{\"tools\":[";
    for (size_t i = 0; i < names_.size(); ++i) {
      if (i) out << ',';
      out << "{\"name\":" << json::quote(names_[i])
          << ",\"description\":" << json::quote(descriptions_[i])
          << ",\"inputSchema\":" << schemas_[i] << "}";
    }
    out << "]}";
    return response(out.str(), false, "");
  }
  if (method == "tools/call") {
    const std::string name = params ? stringField(*params, "name") : std::string();
    std::string arguments = "{}";
    if (!params || params->type != json::Value::Type::Object) {
      return response("", true, "tools/call params must be an object");
    }
    const json::Value* raw = params->find("arguments");
    if (raw) {
      if (raw->type != json::Value::Type::Object)
        return response("", true, "tool arguments must be an object", -32602);
      arguments = json::stringify(*raw);
    }
    const Result result = invoke(name, arguments);
    std::ostringstream out;
    out << "{\"content\":[{\"type\":\"text\",\"text\":"
        << json::quote(result.ok ? result.json : result.error) << "}],\"isError\":"
        << (result.ok ? "false" : "true") << "}";
    return response(out.str(), false, "");
  }
  return response("", true, "method not found: " + method);
}

void Server::runStdio() {
  constexpr size_t kMaxLineBytes = 1024 * 1024;
  for (;;) {
    std::string line;
    bool oversized = false;
    bool receivedAny = false;
    char character = 0;
    while (std::cin.get(character)) {
      receivedAny = true;
      if (character == '\n') break;
      if (line.size() < kMaxLineBytes) line.push_back(character);
      else oversized = true;
    }
    if (!receivedAny) break;
    if (oversized) {
      std::cout << "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{"
                   "\"code\":-32700,\"message\":\"request too large\"}}\n";
      std::cout.flush();
      continue;
    }
    if (line.empty()) {
      if (!std::cin) break;
      continue;
    }
    const std::string response = handle(line);
    if (!response.empty()) {
      std::cout << response << "\n";
      std::cout.flush();
    }
    if (!std::cin) break;
  }
}

}  // namespace nimbus::mcp
