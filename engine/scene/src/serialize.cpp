// Scene serialization: full hierarchy, registered ECS components, prefabs,
// version migration (v1 <-> v2 compatible), and subtree instantiation.
#include "nimbus/scene.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace nimbus::scene {

namespace {

std::string escapeJson(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
      case '\"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          const char hex[] = "0123456789abcdef";
          result += "\\u00";
          result += hex[c >> 4];
          result += hex[c & 0x0F];
        } else result += static_cast<char>(c);
    }
  }
  return result;
}

void writeTransform(std::ostringstream& out, const Transform& t) {
  out << "\"position\":[" << t.position.x << ',' << t.position.y << ',' << t.position.z << ']';
  out << ",\"rotation\":[" << t.rotation.x << ',' << t.rotation.y << ',' << t.rotation.z << ','
      << t.rotation.w << ']';
  out << ",\"scale\":[" << t.scale.x << ',' << t.scale.y << ',' << t.scale.z << ']';
}

struct JsonValue {
  enum class Type { Null, Boolean, Number, String, Array, Object } type = Type::Null;
  bool boolean = false;
  std::string text;
  std::string rawSlice;
  std::vector<JsonValue> array;
  std::unordered_map<std::string, JsonValue> object;
};

void appendUtf8(std::string& output, uint32_t codepoint) {
  if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
  else if (codepoint <= 0x7ff) {
    output.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint <= 0xffff) {
    output.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else {
    output.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& source) : source_(source) {}
  bool parse(JsonValue& value) {
    if (source_.size() > 16u * 1024u * 1024u) return false;
    if (!parseValue(value, 0)) return false;
    skipWhitespace();
    return position_ == source_.size();
  }

 private:
  void skipWhitespace() {
    while (position_ < source_.size() &&
           (source_[position_] == ' ' || source_[position_] == '\n' ||
            source_[position_] == '\r' || source_[position_] == '\t')) ++position_;
  }
  bool consume(char c) {
    skipWhitespace();
    if (position_ >= source_.size() || source_[position_] != c) return false;
    ++position_;
    return true;
  }
  bool parseHex4(uint32_t& value) {
    if (position_ + 4 > source_.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = source_[position_++];
      value <<= 4u;
      if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool parseString(std::string& output) {
    if (position_ >= source_.size() || source_[position_++] != '"') return false;
    output.clear();
    while (position_ < source_.size()) {
      const unsigned char c = static_cast<unsigned char>(source_[position_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') { output.push_back(static_cast<char>(c)); continue; }
      if (position_ >= source_.size()) return false;
      const char escaped = source_[position_++];
      switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          uint32_t codepoint = 0;
          if (!parseHex4(codepoint)) return false;
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (position_ + 2 > source_.size() || source_[position_] != '\\' ||
                source_[position_ + 1] != 'u') return false;
            position_ += 2;
            uint32_t low = 0;
            if (!parseHex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return false;
          appendUtf8(output, codepoint);
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool parseNumber(JsonValue& out) {
    const size_t start = position_;
    if (position_ < source_.size() && source_[position_] == '-') ++position_;
    if (position_ >= source_.size()) return false;
    if (source_[position_] == '0') ++position_;
    else {
      if (source_[position_] < '1' || source_[position_] > '9') return false;
      while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
        ++position_;
    }
    if (position_ < source_.size() && source_[position_] == '.') {
      ++position_;
      const size_t fraction = position_;
      while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
        ++position_;
      if (fraction == position_) return false;
    }
    if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
      ++position_;
      if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-'))
        ++position_;
      const size_t exponent = position_;
      while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
        ++position_;
      if (exponent == position_) return false;
    }
    out.type = JsonValue::Type::Number;
    out.text = source_.substr(start, position_ - start);
    out.rawSlice = out.text;
    char* end = nullptr;
    const double value = std::strtod(out.text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value);
  }
  bool parseValue(JsonValue& out, uint32_t depth) {
    if (depth > 64) return false;
    skipWhitespace();
    if (position_ >= source_.size()) return false;
    const size_t valueStart = position_;
    const char c = source_[position_];
    if (c == '"') {
      out.type = JsonValue::Type::String;
      if (!parseString(out.text)) return false;
      out.rawSlice = source_.substr(valueStart, position_ - valueStart);
      return true;
    }
    if (c == '{') {
      ++position_;
      out.type = JsonValue::Type::Object;
      skipWhitespace();
      if (consume('}')) {
        out.rawSlice = source_.substr(valueStart, position_ - valueStart);
        return true;
      }
      for (;;) {
        skipWhitespace();
        std::string key;
        if (!parseString(key) || !consume(':')) return false;
        JsonValue member;
        if (!parseValue(member, depth + 1) || !out.object.emplace(std::move(key), std::move(member)).second)
          return false;
        if (consume('}')) {
          out.rawSlice = source_.substr(valueStart, position_ - valueStart);
          return true;
        }
        if (!consume(',')) return false;
      }
    }
    if (c == '[') {
      ++position_;
      out.type = JsonValue::Type::Array;
      skipWhitespace();
      if (consume(']')) {
        out.rawSlice = source_.substr(valueStart, position_ - valueStart);
        return true;
      }
      for (;;) {
        JsonValue item;
        if (!parseValue(item, depth + 1)) return false;
        out.array.push_back(std::move(item));
        if (out.array.size() > 1000000) return false;
        if (consume(']')) {
          out.rawSlice = source_.substr(valueStart, position_ - valueStart);
          return true;
        }
        if (!consume(',')) return false;
      }
    }
    if (source_.compare(position_, 4, "true") == 0) {
      position_ += 4; out.type = JsonValue::Type::Boolean; out.boolean = true;
      out.rawSlice = "true"; return true;
    }
    if (source_.compare(position_, 5, "false") == 0) {
      position_ += 5; out.type = JsonValue::Type::Boolean; out.boolean = false;
      out.rawSlice = "false"; return true;
    }
    if (source_.compare(position_, 4, "null") == 0) {
      position_ += 4; out.type = JsonValue::Type::Null; out.rawSlice = "null"; return true;
    }
    return parseNumber(out);
  }

  const std::string& source_;
  size_t position_ = 0;
};

const JsonValue* member(const JsonValue& object, const char* key) {
  if (object.type != JsonValue::Type::Object) return nullptr;
  const auto found = object.object.find(key);
  return found == object.object.end() ? nullptr : &found->second;
}

bool parseUnsigned(const JsonValue* value, uint64_t& output) {
  if (!value || value->type != JsonValue::Type::Number || value->text.empty() ||
      value->text.front() == '-' || value->text.find_first_of(".eE+") != std::string::npos) return false;
  const auto result = std::from_chars(value->text.data(), value->text.data() + value->text.size(), output);
  return result.ec == std::errc{} && result.ptr == value->text.data() + value->text.size();
}

bool parseFloat(const JsonValue* value, float& output) {
  if (!value || value->type != JsonValue::Type::Number) return false;
  char* end = nullptr;
  const double parsed = std::strtod(value->text.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(parsed) ||
      parsed > std::numeric_limits<float>::max() || parsed < -std::numeric_limits<float>::max()) return false;
  output = static_cast<float>(parsed);
  return std::isfinite(output);
}

bool parseFloatArray(const JsonValue* value, float* destination, size_t count) {
  if (!value || value->type != JsonValue::Type::Array || value->array.size() != count) return false;
  for (size_t i = 0; i < count; ++i)
    if (!parseFloat(&value->array[i], destination[i])) return false;
  return true;
}

struct SerializedNode {
  uint64_t oldEntity = 0;
  uint64_t parent = 0;
  bool hasParent = false;
  std::string name;
  Transform transform{};
  std::unordered_map<std::string, std::string> components;
  bool hasPrefab = false;
  PrefabInstance prefab;
};

void appendNodeJson(std::ostringstream& out, const Scene& scene, const ecs::World* world,
                    ecs::Entity e, ecs::Entity overrideParent, bool first) {
  if (!first) out << ',';
  const Node* node = scene.node(e);
  out << '{';
  out << "\"entity\":" << e;
  out << ",\"name\":\"" << escapeJson(world->name(e)) << '"';
  out << ",\"parent\":";
  if (overrideParent == ecs::kNullEntity) out << -1;
  else out << overrideParent;
  out << ',';
  writeTransform(out, node->local);

  std::vector<std::pair<std::string, std::string>> serializedComponents;
  for (const ecs::ComponentSerializer& ser : world->serializers()) {
    if (ser.has && ser.serializeJson && ser.has(*world, e)) {
      const std::string payload = ser.serializeJson(*world, e);
      if (!payload.empty()) serializedComponents.emplace_back(ser.name, payload);
    }
  }
  if (!serializedComponents.empty()) {
    out << ",\"components\":{";
    for (size_t i = 0; i < serializedComponents.size(); ++i) {
      if (i) out << ',';
      out << '"' << escapeJson(serializedComponents[i].first) << "\":"
          << serializedComponents[i].second;
    }
    out << '}';
  }
  if (const PrefabInstance* pi = world->get<PrefabInstance>(e)) {
    out << ",\"prefab\":{\"id\":\"" << escapeJson(pi->prefabId) << "\",\"overrides\":[";
    for (size_t i = 0; i < pi->overrides.size(); ++i) {
      if (i) out << ',';
      out << '"' << escapeJson(pi->overrides[i]) << '"';
    }
    out << "]}";
  }
  out << '}';
}

}  // namespace

std::string Scene::serializeSubtree(ecs::Entity rootEntity) const {
  if (!world_ || !node(rootEntity)) return "{}";
  std::ostringstream out;
  out << std::setprecision(std::numeric_limits<float>::max_digits10);
  out << "{\"version\":1,\"name\":\"" << escapeJson(name_) << "\",\"nodes\":[";
  std::vector<std::pair<ecs::Entity, ecs::Entity>> stack{{rootEntity, ecs::kNullEntity}};
  bool first = true;
  while (!stack.empty()) {
    const auto [e, parent] = stack.back();
    stack.pop_back();
    const Node* n = node(e);
    if (!n) continue;
    appendNodeJson(out, *this, world_, e, parent, first);
    first = false;
    for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
      stack.emplace_back(*it, e);
  }
  out << "]}";
  return out.str();
}

std::string Scene::serialize() const {
  std::ostringstream out;
  out << std::setprecision(std::numeric_limits<float>::max_digits10);
  out << '{';
  out << "\"version\":1";
  out << ",\"name\":\"" << escapeJson(name_) << '"';
  out << ",\"nodes\":[";
  bool first = true;
  for (const ecs::Entity root : roots_) {
    std::vector<ecs::Entity> stack{root};
    while (!stack.empty()) {
      const ecs::Entity e = stack.back();
      stack.pop_back();
      const Node* n = this->node(e);
      if (!n) continue;
      appendNodeJson(out, *this, world_, e, n->parent, first);
      first = false;
      for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
        stack.push_back(*it);
    }
  }
  out << "]";
  if (!prefabs_.empty()) {
    out << ",\"prefabs\":{";
    bool pFirst = true;
    for (const auto& [pid, pjson] : prefabs_) {
      if (!pFirst) out << ',';
      pFirst = false;
      out << '"' << escapeJson(pid) << "\":\"" << escapeJson(pjson) << '"';
    }
    out << '}';
  }
  out << '}';
  return out.str();
}

static bool parseScenePayload(const std::string& json, std::string& outName,
                              std::vector<SerializedNode>& parsedNodes,
                              std::unordered_map<std::string, std::string>& parsedPrefabs) {
  JsonValue root;
  JsonParser parser(json);
  if (!parser.parse(root)) return false;
  const JsonValue* version = member(root, "version");
  const JsonValue* nodes = member(root, "nodes");
  uint64_t versionNumber = 0;
  if (!parseUnsigned(version, versionNumber) || (versionNumber != 1 && versionNumber != 2) ||
      !nodes || nodes->type != JsonValue::Type::Array) return false;

  if (const JsonValue* nameVal = member(root, "name");
      nameVal && nameVal->type == JsonValue::Type::String) {
    outName = nameVal->text;
  }
  if (const JsonValue* prefabsVal = member(root, "prefabs");
      prefabsVal && prefabsVal->type == JsonValue::Type::Object) {
    for (const auto& [pid, val] : prefabsVal->object) {
      if (val.type == JsonValue::Type::String) parsedPrefabs[pid] = val.text;
    }
  }

  parsedNodes.reserve(nodes->array.size());
  std::unordered_set<uint64_t> seen;
  for (const JsonValue& value : nodes->array) {
    SerializedNode parsed;
    const JsonValue* entity = member(value, "entity");
    const JsonValue* name = member(value, "name");
    const JsonValue* parent = member(value, "parent");
    uint64_t parentId = 0;
    if (!parseUnsigned(entity, parsed.oldEntity) || !name || name->type != JsonValue::Type::String ||
        !parent || parent->type != JsonValue::Type::Number || !seen.insert(parsed.oldEntity).second)
      return false;
    if (parent->text != "-1") {
      if (!parseUnsigned(parent, parentId)) return false;
      parsed.parent = parentId;
      parsed.hasParent = true;
      if (seen.find(parentId) == seen.end()) return false;
    }
    parsed.name = name->text;
    float position[3]{}, rotation[4]{}, scale[3]{};
    if (!parseFloatArray(member(value, "position"), position, 3) ||
        !parseFloatArray(member(value, "rotation"), rotation, 4) ||
        !parseFloatArray(member(value, "scale"), scale, 3)) return false;
    parsed.transform.position = {position[0], position[1], position[2]};
    parsed.transform.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
    parsed.transform.scale = {scale[0], scale[1], scale[2]};

    if (const JsonValue* comps = member(value, "components");
        comps && comps->type == JsonValue::Type::Object) {
      for (const auto& [cname, cval] : comps->object) {
        if (!cval.rawSlice.empty()) parsed.components.emplace(cname, cval.rawSlice);
      }
    }
    if (const JsonValue* pref = member(value, "prefab");
        pref && pref->type == JsonValue::Type::Object) {
      if (const JsonValue* pid = member(*pref, "id"); pid && pid->type == JsonValue::Type::String) {
        parsed.hasPrefab = true;
        parsed.prefab.prefabId = pid->text;
        if (const JsonValue* ovs = member(*pref, "overrides");
            ovs && ovs->type == JsonValue::Type::Array) {
          for (const JsonValue& o : ovs->array)
            if (o.type == JsonValue::Type::String) parsed.prefab.overrides.push_back(o.text);
        }
      }
    }
    parsedNodes.push_back(std::move(parsed));
  }
  return true;
}

static bool instantiateNodes(ecs::World* world, Scene& target,
                             const std::vector<SerializedNode>& parsedNodes,
                             ecs::Entity rootParent,
                             std::vector<ecs::Entity>* createdRoots) {
  std::unordered_map<uint64_t, ecs::Entity> remapped;
  std::vector<ecs::Entity> localRoots;
  const auto rollback = [&]() {
    for (ecs::Entity r : localRoots) target.destroy(r);
  };
  try {
    for (const SerializedNode& source : parsedNodes) {
      ecs::Entity parent = rootParent;
      if (source.hasParent) {
        const auto found = remapped.find(source.parent);
        if (found == remapped.end()) { rollback(); return false; }
        parent = found->second;
      }
      const ecs::Entity created = target.createNode(source.name, parent);
      if (created == ecs::kNullEntity) { rollback(); return false; }
      if (!source.hasParent) localRoots.push_back(created);
      target.setLocal(created, source.transform);
      for (const auto& [cname, cjson] : source.components) {
        for (const ecs::ComponentSerializer& ser : world->serializers()) {
          if (ser.name == cname && ser.deserializeJson) {
            ser.deserializeJson(*world, created, cjson);
            break;
          }
        }
      }
      if (source.hasPrefab) world->add<PrefabInstance>(created, source.prefab);
      remapped.emplace(source.oldEntity, created);
    }
  } catch (...) {
    rollback();
    return false;
  }
  if (createdRoots) *createdRoots = std::move(localRoots);
  return true;
}

bool Scene::deserialize(const std::string& json) {
  if (!world_) return false;
  std::string parsedName = name_;
  std::vector<SerializedNode> parsedNodes;
  std::unordered_map<std::string, std::string> parsedPrefabs;
  if (!parseScenePayload(json, parsedName, parsedNodes, parsedPrefabs)) return false;

  Scene staging(world_, parsedName);
  if (!instantiateNodes(world_, staging, parsedNodes, ecs::kNullEntity, nullptr)) return false;

  std::swap(nodes_, staging.nodes_);
  std::swap(roots_, staging.roots_);
  name_ = std::move(parsedName);
  for (const auto& [pid, pjson] : parsedPrefabs) prefabs_[pid] = pjson;
  const auto previousRoots = staging.roots_;
  for (ecs::Entity rootEntity : previousRoots) staging.destroy(rootEntity);
  return true;
}

bool Scene::deserializeAppend(const std::string& json, ecs::Entity parent,
                              std::vector<ecs::Entity>* createdRoots) {
  if (!world_) return false;
  std::string ignoredName;
  std::vector<SerializedNode> parsedNodes;
  std::unordered_map<std::string, std::string> parsedPrefabs;
  if (!parseScenePayload(json, ignoredName, parsedNodes, parsedPrefabs)) return false;
  for (const auto& [pid, pjson] : parsedPrefabs) prefabs_[pid] = pjson;
  return instantiateNodes(world_, *this, parsedNodes, parent, createdRoots);
}

bool Scene::registerPrefab(const std::string& id, const std::string& serializedJson) {
  if (id.empty() || serializedJson.empty()) return false;
  prefabs_[id] = serializedJson;
  return true;
}

bool Scene::savePrefabFromNode(ecs::Entity rootEntity, const std::string& prefabId) {
  if (prefabId.empty() || !node(rootEntity)) return false;
  prefabs_[prefabId] = serializeSubtree(rootEntity);
  if (world_ && !world_->has<PrefabInstance>(rootEntity)) {
    world_->add<PrefabInstance>(rootEntity, PrefabInstance{prefabId, {}});
  }
  return true;
}

ecs::Entity Scene::instantiatePrefab(const std::string& prefabId, ecs::Entity parent) {
  const auto it = prefabs_.find(prefabId);
  if (it == prefabs_.end() || !world_) return ecs::kNullEntity;
  std::vector<ecs::Entity> created;
  if (!deserializeAppend(it->second, parent, &created) || created.empty()) return ecs::kNullEntity;
  world_->add<PrefabInstance>(created[0], PrefabInstance{prefabId, {}});
  return created[0];
}

void Scene::markPrefabOverride(ecs::Entity entity, const std::string& componentName) {
  if (!world_ || componentName.empty()) return;
  if (PrefabInstance* pi = world_->get<PrefabInstance>(entity)) {
    if (std::find(pi->overrides.begin(), pi->overrides.end(), componentName) == pi->overrides.end())
      pi->overrides.push_back(componentName);
  }
}

bool Scene::revertPrefabInstance(ecs::Entity entity) {
  if (!world_ || !node(entity)) return false;
  const PrefabInstance* pi = world_->get<PrefabInstance>(entity);
  if (!pi) return false;
  const auto it = prefabs_.find(pi->prefabId);
  if (it == prefabs_.end()) return false;
  std::string ignoredName;
  std::vector<SerializedNode> parsedNodes;
  std::unordered_map<std::string, std::string> ignoredPrefabs;
  if (!parseScenePayload(it->second, ignoredName, parsedNodes, ignoredPrefabs) || parsedNodes.empty())
    return false;
  setLocal(entity, parsedNodes[0].transform);
  for (const auto& [cname, cjson] : parsedNodes[0].components) {
    for (const ecs::ComponentSerializer& ser : world_->serializers()) {
      if (ser.name == cname && ser.deserializeJson) {
        ser.deserializeJson(*world_, entity, cjson);
        break;
      }
    }
  }
  if (PrefabInstance* mutablePi = world_->get<PrefabInstance>(entity)) mutablePi->overrides.clear();
  return true;
}

}  // namespace nimbus::scene
