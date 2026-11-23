#include "nimbus/asset.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <sys/types.h>
#include <unordered_map>

#include "nimbus/log.hpp"

namespace nimbus::asset {

AssetId idFromPath(const std::string& path) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::Mesh: return "mesh";
    case Kind::Texture: return "texture";
    case Kind::Material: return "material";
    case Kind::Clip: return "clip";
    case Kind::Audio: return "audio";
    case Kind::Script: return "script";
    case Kind::Scene: return "scene";
    case Kind::Unknown: return "unknown";
  }
  return "unknown";
}

namespace {

uint32_t calculateCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint16_t readU16Le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

uint32_t readU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8u) |
         (static_cast<uint32_t>(p[2]) << 16u) | (static_cast<uint32_t>(p[3]) << 24u);
}

float readF32Le(const uint8_t* p) {
  const uint32_t u = readU32Le(p);
  float v = 0.0f;
  std::memcpy(&v, &u, sizeof(v));
  return v;
}

// Lightweight safe JSON value parser for glTF 2.0 documents.
struct JsonVal {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolVal = false;
  double numVal = 0.0;
  std::string strVal;
  std::vector<JsonVal> arrVal;
  std::unordered_map<std::string, JsonVal> objVal;

  const JsonVal* get(const char* key) const {
    if (type != Type::Object) return nullptr;
    auto it = objVal.find(key);
    return it == objVal.end() ? nullptr : &it->second;
  }
  double asNumber(double def = 0.0) const { return type == Type::Number ? numVal : def; }
  int asInt(int def = -1) const { return type == Type::Number ? static_cast<int>(numVal) : def; }
  std::string asString() const { return type == Type::String ? strVal : std::string{}; }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& src) : s_(src) {}
  bool parse(JsonVal& out) {
    skipWs();
    if (!parseValue(out, 0)) return false;
    skipWs();
    return pos_ == s_.size();
  }

 private:
  void skipWs() {
    while (pos_ < s_.size() &&
           (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
      ++pos_;
    }
  }
  bool parseValue(JsonVal& out, int depth) {
    if (depth > 64) return false;
    skipWs();
    if (pos_ >= s_.size()) return false;
    const char c = s_[pos_];
    if (c == '{') return parseObject(out, depth);
    if (c == '[') return parseArray(out, depth);
    if (c == '"') {
      out = JsonVal{};
      out.type = JsonVal::Type::String;
      return parseString(out.strVal);
    }
    if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
      out = JsonVal{}; out.type = JsonVal::Type::Bool; out.boolVal = true; pos_ += 4; return true;
    }
    if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
      out = JsonVal{}; out.type = JsonVal::Type::Bool; out.boolVal = false; pos_ += 5; return true;
    }
    if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
      out = JsonVal{}; out.type = JsonVal::Type::Null; pos_ += 4; return true;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      const char* start = s_.c_str() + pos_;
      char* end = nullptr;
      const double v = std::strtod(start, &end);
      if (!end || end == start || !std::isfinite(v)) return false;
      pos_ = static_cast<size_t>(end - s_.c_str());
      out = JsonVal{};
      out.type = JsonVal::Type::Number;
      out.numVal = v;
      return true;
    }
    return false;
  }
  bool parseString(std::string& out) {
    if (pos_ >= s_.size() || s_[pos_] != '"') return false;
    ++pos_;
    out.clear();
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= s_.size()) return false;
        const char e = s_[pos_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > s_.size()) return false;
            pos_ += 4;
            out.push_back('?');
            break;
          }
          default: return false;
        }
      } else {
        out.push_back(c);
      }
    }
    return false;
  }
  bool parseArray(JsonVal& out, int depth) {
    ++pos_;
    out = JsonVal{};
    out.type = JsonVal::Type::Array;
    skipWs();
    if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
    while (pos_ < s_.size()) {
      JsonVal elem;
      if (!parseValue(elem, depth + 1)) return false;
      out.arrVal.push_back(std::move(elem));
      skipWs();
      if (pos_ >= s_.size()) return false;
      if (s_[pos_] == ']') { ++pos_; return true; }
      if (s_[pos_] != ',') return false;
      ++pos_;
    }
    return false;
  }
  bool parseObject(JsonVal& out, int depth) {
    ++pos_;
    out = JsonVal{};
    out.type = JsonVal::Type::Object;
    skipWs();
    if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
    while (pos_ < s_.size()) {
      skipWs();
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (pos_ >= s_.size() || s_[pos_] != ':') return false;
      ++pos_;
      JsonVal val;
      if (!parseValue(val, depth + 1)) return false;
      out.objVal[std::move(key)] = std::move(val);
      skipWs();
      if (pos_ >= s_.size()) return false;
      if (s_[pos_] == '}') { ++pos_; return true; }
      if (s_[pos_] != ',') return false;
      ++pos_;
    }
    return false;
  }
  const std::string& s_;
  size_t pos_ = 0;
};

bool decodeBase64(const std::string& input, std::vector<uint8_t>& out) {
  static constexpr int8_t kTable[256] = {
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
  out.clear();
  uint32_t val = 0;
  int valb = -8;
  for (unsigned char c : input) {
    if (c == '=') break;
    if (std::isspace(c)) continue;
    const int8_t d = kTable[c];
    if (d < 0) return false;
    val = (val << 6u) + static_cast<uint32_t>(d);
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<uint8_t>((val >> valb) & 0xffu));
      valb -= 8;
    }
  }
  return true;
}

bool readAccessorSlice(const JsonVal& root, const std::vector<std::vector<uint8_t>>& buffers,
                       int accessorIndex, int& outComponentType, std::string& outType,
                       size_t& outCount, const uint8_t*& outPtr, size_t& outStride,
                       size_t& outAvailable) {
  const JsonVal* accessors = root.get("accessors");
  const JsonVal* views = root.get("bufferViews");
  if (!accessors || !views || accessorIndex < 0 ||
      static_cast<size_t>(accessorIndex) >= accessors->arrVal.size())
    return false;
  const JsonVal& acc = accessors->arrVal[static_cast<size_t>(accessorIndex)];
  const int viewIdx = acc.get("bufferView") ? acc.get("bufferView")->asInt(-1) : -1;
  if (viewIdx < 0 || static_cast<size_t>(viewIdx) >= views->arrVal.size()) return false;
  const JsonVal& bv = views->arrVal[static_cast<size_t>(viewIdx)];
  const int bufIdx = bv.get("buffer") ? bv.get("buffer")->asInt(0) : 0;
  if (bufIdx < 0 || static_cast<size_t>(bufIdx) >= buffers.size()) return false;
  const std::vector<uint8_t>& buf = buffers[static_cast<size_t>(bufIdx)];

  const size_t bvOffset = bv.get("byteOffset") ? static_cast<size_t>(std::max(0, bv.get("byteOffset")->asInt(0))) : 0;
  const size_t bvLength = bv.get("byteLength") ? static_cast<size_t>(std::max(0, bv.get("byteLength")->asInt(0))) : 0;
  const size_t accOffset = acc.get("byteOffset") ? static_cast<size_t>(std::max(0, acc.get("byteOffset")->asInt(0))) : 0;
  if (bvOffset > buf.size() || bvLength > buf.size() - bvOffset || accOffset > bvLength) return false;

  outComponentType = acc.get("componentType") ? acc.get("componentType")->asInt(0) : 0;
  outType = acc.get("type") ? acc.get("type")->asString() : std::string{};
  outCount = acc.get("count") ? static_cast<size_t>(std::max(0, acc.get("count")->asInt(0))) : 0;
  outStride = bv.get("byteStride") ? static_cast<size_t>(std::max(0, bv.get("byteStride")->asInt(0))) : 0;
  outPtr = buf.data() + bvOffset + accOffset;
  outAvailable = bvLength - accOffset;
  return outCount > 0;
}

}  // namespace

// ---- Pack ----------------------------------------------------------------

bool Pack::open(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    log::write(log::Level::Warning, "asset: pack not found: %s", path.c_str());
    return false;
  }
  const auto fail = [&](const char* reason) {
    log::write(log::Level::Error, "asset: invalid pack '%s': %s", path.c_str(), reason);
    std::fclose(file);
    return false;
  };
  if (::fseeko(file, 0, SEEK_END) != 0) return fail("cannot seek to end");
  const off_t end = ::ftello(file);
  if (end < static_cast<off_t>(sizeof(Header)) || ::fseeko(file, 0, SEEK_SET) != 0)
    return fail("truncated header");
  const uint64_t fileSize = static_cast<uint64_t>(end);
  Header header{};
  if (std::fread(&header, sizeof(header), 1, file) != 1 || std::memcmp(header.magic, "NBPK", 4) != 0)
    return fail("bad header magic or truncated header");
  if (header.version != 1) return fail("unsupported pack version");
  if (header.entryCount > 1000000u) return fail("entry count exceeds safety limit");
  if (header.tableOffset < sizeof(Header) || header.tableOffset > fileSize)
    return fail("table offset outside file");
  const uint64_t tableBytes = static_cast<uint64_t>(header.entryCount) * sizeof(Entry);
  if (tableBytes > fileSize - header.tableOffset) return fail("entry table exceeds file bounds");
  if (::fseeko(file, static_cast<off_t>(header.tableOffset), SEEK_SET) != 0)
    return fail("cannot seek to entry table");

  std::vector<Entry> entries(header.entryCount);
  std::unordered_map<AssetId, uint32_t> index;
  index.reserve(header.entryCount);
  for (uint32_t i = 0; i < header.entryCount; ++i) {
    Entry& entry = entries[i];
    if (std::fread(&entry, sizeof(Entry), 1, file) != 1) return fail("truncated entry table");
    if (entry.id == kNullAsset) return fail("entry has null asset id");
    if (static_cast<uint8_t>(entry.kind) > static_cast<uint8_t>(Kind::Unknown))
      return fail("entry has unknown asset kind");
    if (!std::memchr(entry.name, '\0', sizeof(entry.name))) return fail("entry name is not terminated");
    if (entry.offset < sizeof(Header) || entry.offset > header.tableOffset ||
        entry.size > header.tableOffset - entry.offset)
      return fail("asset payload overlaps header/table or exceeds bounds");
    if (!index.emplace(entry.id, i).second) return fail("duplicate asset id");
  }
  std::fclose(file);
  entries_.swap(entries);
  index_.swap(index);
  path_ = path;
  valid_ = true;
  log::write(log::Level::Info, "asset: opened %s (%u entries)", path.c_str(), header.entryCount);
  return true;
}

void Pack::close() {
  valid_ = false;
  path_.clear();
  entries_.clear();
  index_.clear();
}

const Entry* Pack::find(AssetId id) const {
  auto it = index_.find(id);
  return it == index_.end() ? nullptr : &entries_[it->second];
}

const Entry* Pack::findByName(const std::string& name) const {
  for (const Entry& entry : entries_)
    if (name == entry.name) return &entry;
  return nullptr;
}

uint64_t Pack::read(AssetId id, std::vector<uint8_t>& out) const {
  out.clear();
  const Entry* entry = valid_ ? find(id) : nullptr;
  if (!entry || entry->size > std::numeric_limits<size_t>::max() ||
      entry->size > 256ull * 1024ull * 1024ull) return 0;
  std::FILE* file = std::fopen(path_.c_str(), "rb");
  if (!file) return 0;
  if (::fseeko(file, static_cast<off_t>(entry->offset), SEEK_SET) != 0) {
    std::fclose(file);
    return 0;
  }
  out.resize(static_cast<size_t>(entry->size));
  const size_t readBytes = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), file);
  const bool readOk = readBytes == out.size();
  std::fclose(file);
  if (!readOk || (entry->crc32 != 0 && calculateCrc32(out.data(), out.size()) != entry->crc32)) {
    out.clear();
    return 0;
  }
  return static_cast<uint64_t>(readBytes);
}

// ---- PackBuilder ---------------------------------------------------------

void PackBuilder::addData(const std::string& name, Kind kind, const std::vector<uint8_t>& bytes) {
  for (Item& item : items_) {
    if (item.name == name) {
      item.kind = kind;
      item.data = bytes;
      return;
    }
  }
  items_.push_back({name, kind, bytes});
}

bool PackBuilder::addFile(const std::string& name, Kind kind, const std::string& filePath) {
  std::ifstream input(filePath, std::ios::binary | std::ios::ate);
  if (!input.is_open()) return false;
  const std::streamsize len = input.tellg();
  if (len < 0 || static_cast<uint64_t>(len) > 256ull * 1024ull * 1024ull) return false;
  input.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(len));
  if (len > 0 && !input.read(reinterpret_cast<char*>(bytes.data()), len)) return false;
  addData(name, kind, bytes);
  return true;
}

bool PackBuilder::write(const std::string& outputPath) const {
  if (outputPath.empty()) return false;
  std::filesystem::path p(outputPath);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  std::FILE* file = std::fopen(outputPath.c_str(), "wb");
  if (!file) return false;

  Header header{};
  header.entryCount = static_cast<uint32_t>(items_.size());
  if (std::fwrite(&header, sizeof(header), 1, file) != 1) {
    std::fclose(file);
    return false;
  }

  std::vector<Entry> table;
  table.reserve(items_.size());
  uint64_t cursor = sizeof(Header);
  for (const Item& item : items_) {
    Entry e{};
    e.id = idFromPath(item.name);
    if (e.id == kNullAsset) e.id = 1;
    e.kind = item.kind;
    e.offset = cursor;
    e.size = item.data.size();
    e.uncompressedSize = item.data.size();
    e.crc32 = item.data.empty() ? 0 : calculateCrc32(item.data.data(), item.data.size());
    const size_t copyLen = std::min(item.name.size(), sizeof(e.name) - 1u);
    std::memcpy(e.name, item.name.data(), copyLen);
    e.name[copyLen] = '\0';
    if (!item.data.empty()) {
      if (std::fwrite(item.data.data(), 1, item.data.size(), file) != item.data.size()) {
        std::fclose(file);
        return false;
      }
    }
    cursor += item.data.size();
    table.push_back(e);
  }

  header.tableOffset = cursor;
  if (!table.empty()) {
    if (std::fwrite(table.data(), sizeof(Entry), table.size(), file) != table.size()) {
      std::fclose(file);
      return false;
    }
  }
  if (::fseeko(file, 0, SEEK_SET) != 0 || std::fwrite(&header, sizeof(header), 1, file) != 1) {
    std::fclose(file);
    return false;
  }
  std::fclose(file);
  return true;
}

// ---- Loader --------------------------------------------------------------

Loader::Loader(Pack* pack, uint32_t maxPendingRequests)
    : pack_(pack), maxPendingRequests_(std::max(1u, maxPendingRequests)) {
  if (pack_) worker_ = std::thread(&Loader::workerLoop, this);
}

Loader::~Loader() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    queue_.clear();
  }
  ready_.notify_all();
  if (worker_.joinable()) worker_.join();
  std::lock_guard lock(mutex_);
  completed_.clear();
}

bool Loader::request(AssetId id, Callback callback) {
  if (!pack_ || !callback || id == kNullAsset) return false;
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || queue_.size() + completed_.size() + active_ >=
                       maxPendingRequests_) return false;
    queue_.push_back({id, std::move(callback)});
  }
  ready_.notify_one();
  return true;
}

uint32_t Loader::pending() const {
  std::lock_guard lock(mutex_);
  const size_t count = queue_.size() + completed_.size() + active_;
  return static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX));
}

void Loader::workerLoop() {
  for (;;) {
    Request request;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      request = std::move(queue_.front());
      queue_.pop_front();
      ++active_;
    }

    std::vector<uint8_t> bytes;
    try {
      if (pack_) (void)pack_->read(request.id, bytes);
    } catch (const std::exception& error) {
      log::write(log::Level::Error, "asset: async read failed for %llu: %s",
                 static_cast<unsigned long long>(request.id), error.what());
      bytes.clear();
    } catch (...) {
      log::write(log::Level::Error, "asset: async read failed for %llu",
                 static_cast<unsigned long long>(request.id));
      bytes.clear();
    }

    {
      std::lock_guard lock(mutex_);
      --active_;
      if (!stopping_) {
        completed_.push_back({request.id, std::move(request.callback), std::move(bytes)});
      }
    }
  }
}

void Loader::update() {
  std::deque<Completion> ready;
  {
    std::lock_guard lock(mutex_);
    ready.swap(completed_);
  }
  for (Completion& completion : ready) {
    try {
      if (completion.callback)
        completion.callback(completion.id, std::move(completion.bytes));
    } catch (const std::exception& error) {
      log::write(log::Level::Error, "asset: completion callback failed for %llu: %s",
                 static_cast<unsigned long long>(completion.id), error.what());
    } catch (...) {
      log::write(log::Level::Error, "asset: completion callback failed for %llu",
                 static_cast<unsigned long long>(completion.id));
    }
  }
}

// ---- OBJ Importer --------------------------------------------------------

namespace {
bool parseFiniteFloat(std::istringstream& stream, float& value) {
  if (!(stream >> value)) return false;
  return std::isfinite(value);
}

bool resolveObjIndex(const std::string& token, size_t count, size_t& resolved) {
  if (token.empty()) return false;
  long long value = 0;
  try {
    size_t consumed = 0;
    value = std::stoll(token, &consumed, 10);
    if (consumed != token.size() || value == 0) return false;
  } catch (...) {
    return false;
  }
  if (value > 0) {
    const auto zeroBased = static_cast<unsigned long long>(value - 1);
    if (zeroBased >= count) return false;
    resolved = static_cast<size_t>(zeroBased);
    return true;
  }
  const unsigned long long distance = static_cast<unsigned long long>(-(value + 1)) + 1ull;
  if (distance > count) return false;
  resolved = count - static_cast<size_t>(distance);
  return true;
}

struct ObjFaceVertex {
  size_t position = 0;
  size_t uv = 0;
  size_t normal = 0;
  bool hasUv = false;
  bool hasNormal = false;
};

bool parseFaceVertex(const std::string& token, size_t positionCount, size_t uvCount,
                     size_t normalCount, ObjFaceVertex& result) {
  const size_t first = token.find('/');
  const size_t second = first == std::string::npos ? std::string::npos
                                                    : token.find('/', first + 1);
  if (second != std::string::npos && token.find('/', second + 1) != std::string::npos)
    return false;
  const std::string position = token.substr(0, first);
  if (!resolveObjIndex(position, positionCount, result.position)) return false;
  if (first == std::string::npos) return true;
  const std::string uv = token.substr(first + 1,
      (second == std::string::npos ? token.size() : second) - first - 1);
  if (!uv.empty()) {
    if (!resolveObjIndex(uv, uvCount, result.uv)) return false;
    result.hasUv = true;
  }
  if (second != std::string::npos) {
    const std::string normal = token.substr(second + 1);
    if (!normal.empty()) {
      if (!resolveObjIndex(normal, normalCount, result.normal)) return false;
      result.hasNormal = true;
    }
  }
  return true;
}
}  // namespace

bool importObj(const std::string& text, MeshImport& out) {
  std::istringstream stream(text);
  std::string line;
  std::vector<math::Vec3> positions;
  std::vector<math::Vec3> normals;
  std::vector<math::Vec2> uvs;
  MeshImport parsed;
  while (std::getline(stream, line)) {
    std::istringstream row(line);
    std::string type;
    if (!(row >> type) || type.empty() || type[0] == '#') continue;
    if (type == "v") {
      math::Vec3 value{};
      if (!parseFiniteFloat(row, value.x) || !parseFiniteFloat(row, value.y) ||
          !parseFiniteFloat(row, value.z)) return false;
      positions.push_back(value);
    } else if (type == "vn") {
      math::Vec3 value{};
      if (!parseFiniteFloat(row, value.x) || !parseFiniteFloat(row, value.y) ||
          !parseFiniteFloat(row, value.z)) return false;
      normals.push_back(value);
    } else if (type == "vt") {
      math::Vec2 value{};
      if (!parseFiniteFloat(row, value.x) || !parseFiniteFloat(row, value.y)) return false;
      uvs.push_back(value);
    } else if (type == "f") {
      std::vector<ObjFaceVertex> face;
      std::string reference;
      while (row >> reference) {
        if (!reference.empty() && reference[0] == '#') break;
        ObjFaceVertex vertex;
        if (!parseFaceVertex(reference, positions.size(), uvs.size(), normals.size(), vertex))
          return false;
        face.push_back(vertex);
      }
      if (face.size() < 3) return false;
      if (face.size() > std::numeric_limits<uint32_t>::max() / 3u) return false;
      const size_t addedVertices = (face.size() - 2) * 3;
      if (parsed.indices.size() > std::numeric_limits<uint32_t>::max() - addedVertices)
        return false;
      for (size_t triangle = 1; triangle + 1 < face.size(); ++triangle) {
        const size_t corners[3] = {0, triangle, triangle + 1};
        for (size_t faceIndex : corners) {
          const ObjFaceVertex& index = face[faceIndex];
          const math::Vec3& position = positions[index.position];
          const math::Vec3 normal = index.hasNormal ? normals[index.normal]
                                                     : math::Vec3{0, 1, 0};
          const math::Vec2 uv = index.hasUv ? uvs[index.uv] : math::Vec2{0, 0};
          parsed.vertices.insert(parsed.vertices.end(),
              {position.x, position.y, position.z, normal.x, normal.y, normal.z, uv.x, uv.y});
          math::aabbExpand(parsed.bounds, position);
          parsed.indices.push_back(static_cast<uint32_t>(parsed.indices.size()));
        }
      }
    }
  }
  if (parsed.vertices.empty()) return false;
  out = std::move(parsed);
  return true;
}

// ---- glTF 2.0 & GLB Importer ---------------------------------------------

bool importGltf(const std::string& jsonText, const std::vector<uint8_t>& binChunk,
                const std::string& baseDir, GltfScene& out) {
  JsonVal root;
  if (!JsonParser(jsonText).parse(root) || root.type != JsonVal::Type::Object) return false;

  std::vector<std::vector<uint8_t>> buffers;
  if (const JsonVal* bufArr = root.get("buffers")) {
    for (size_t i = 0; i < bufArr->arrVal.size(); ++i) {
      const JsonVal& b = bufArr->arrVal[i];
      const JsonVal* uriVal = b.get("uri");
      if (!uriVal) {
        if (i == 0 && !binChunk.empty()) {
          buffers.push_back(binChunk);
        } else {
          return false;
        }
      } else {
        const std::string uri = uriVal->asString();
        const std::string prefix = "base64,";
        const size_t b64Pos = uri.find(prefix);
        if (uri.rfind("data:", 0) == 0 && b64Pos != std::string::npos) {
          std::vector<uint8_t> decoded;
          if (!decodeBase64(uri.substr(b64Pos + prefix.size()), decoded)) return false;
          buffers.push_back(std::move(decoded));
        } else if (!baseDir.empty() && uri.find("..") == std::string::npos) {
          const std::filesystem::path full = std::filesystem::path(baseDir) / uri;
          std::ifstream f(full, std::ios::binary | std::ios::ate);
          if (!f.is_open()) return false;
          const std::streamsize len = f.tellg();
          if (len <= 0 || len > 128 * 1024 * 1024) return false;
          f.seekg(0, std::ios::beg);
          std::vector<uint8_t> bytes(static_cast<size_t>(len));
          if (!f.read(reinterpret_cast<char*>(bytes.data()), len)) return false;
          buffers.push_back(std::move(bytes));
        } else {
          return false;
        }
      }
    }
  } else if (!binChunk.empty()) {
    buffers.push_back(binChunk);
  }

  GltfScene scene;
  if (const JsonVal* mats = root.get("materials")) {
    for (const JsonVal& m : mats->arrVal) {
      GltfMaterialImport mat;
      if (const JsonVal* n = m.get("name")) mat.name = n->asString();
      if (const JsonVal* pbr = m.get("pbrMetallicRoughness")) {
        if (const JsonVal* bc = pbr->get("baseColorFactor"); bc && bc->arrVal.size() >= 4) {
          mat.baseColor = {static_cast<float>(bc->arrVal[0].asNumber(1)),
                           static_cast<float>(bc->arrVal[1].asNumber(1)),
                           static_cast<float>(bc->arrVal[2].asNumber(1)),
                           static_cast<float>(bc->arrVal[3].asNumber(1))};
        }
        if (const JsonVal* met = pbr->get("metallicFactor"))
          mat.metallic = static_cast<float>(met->asNumber(0.0));
        if (const JsonVal* rgh = pbr->get("roughnessFactor"))
          mat.roughness = static_cast<float>(rgh->asNumber(0.5));
      }
      scene.materials.push_back(std::move(mat));
    }
  }

  const JsonVal* meshesArr = root.get("meshes");
  if (!meshesArr || meshesArr->arrVal.empty()) return false;

  for (const JsonVal& gm : meshesArr->arrVal) {
    const JsonVal* prims = gm.get("primitives");
    if (!prims || prims->arrVal.empty()) continue;
    const std::string mName = gm.get("name") ? gm.get("name")->asString() : std::string("mesh");
    for (const JsonVal& prim : prims->arrVal) {
      const JsonVal* attrs = prim.get("attributes");
      if (!attrs) continue;
      const JsonVal* posAcc = attrs->get("POSITION");
      if (!posAcc) continue;

      int posComp = 0; std::string posType; size_t posCount = 0, posStride = 0, posAvail = 0;
      const uint8_t* posPtr = nullptr;
      if (!readAccessorSlice(root, buffers, posAcc->asInt(-1), posComp, posType, posCount, posPtr,
                             posStride, posAvail))
        return false;
      if (posComp != 5126 || posType != "VEC3") return false;
      const size_t pStep = posStride == 0 ? 12u : posStride;
      if (pStep < 12u || (posCount - 1u) * pStep + 12u > posAvail) return false;

      const uint8_t* normPtr = nullptr;
      size_t normStride = 12u, normCount = 0, normAvail = 0;
      if (const JsonVal* normAcc = attrs->get("NORMAL")) {
        int nc = 0; std::string nt; size_t ns = 0;
        if (readAccessorSlice(root, buffers, normAcc->asInt(-1), nc, nt, normCount, normPtr, ns,
                              normAvail) &&
            nc == 5126 && nt == "VEC3") {
          normStride = ns == 0 ? 12u : ns;
          if (normCount < posCount || (posCount - 1u) * normStride + 12u > normAvail)
            normPtr = nullptr;
        } else {
          normPtr = nullptr;
        }
      }

      const uint8_t* uvPtr = nullptr;
      size_t uvStride = 8u, uvCount = 0, uvAvail = 0;
      if (const JsonVal* uvAcc = attrs->get("TEXCOORD_0")) {
        int uc = 0; std::string ut; size_t us = 0;
        if (readAccessorSlice(root, buffers, uvAcc->asInt(-1), uc, ut, uvCount, uvPtr, us,
                              uvAvail) &&
            uc == 5126 && ut == "VEC2") {
          uvStride = us == 0 ? 8u : us;
          if (uvCount < posCount || (posCount - 1u) * uvStride + 8u > uvAvail) uvPtr = nullptr;
        } else {
          uvPtr = nullptr;
        }
      }

      MeshImport mesh;
      mesh.vertices.resize(posCount * 8u);
      for (size_t i = 0; i < posCount; ++i) {
        const float px = readF32Le(posPtr + i * pStep + 0);
        const float py = readF32Le(posPtr + i * pStep + 4);
        const float pz = readF32Le(posPtr + i * pStep + 8);
        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
        if (normPtr) {
          nx = readF32Le(normPtr + i * normStride + 0);
          ny = readF32Le(normPtr + i * normStride + 4);
          nz = readF32Le(normPtr + i * normStride + 8);
        }
        float u = 0.0f, v = 0.0f;
        if (uvPtr) {
          u = readF32Le(uvPtr + i * uvStride + 0);
          v = readF32Le(uvPtr + i * uvStride + 4);
        }
        mesh.vertices[i * 8 + 0] = px;
        mesh.vertices[i * 8 + 1] = py;
        mesh.vertices[i * 8 + 2] = pz;
        mesh.vertices[i * 8 + 3] = nx;
        mesh.vertices[i * 8 + 4] = ny;
        mesh.vertices[i * 8 + 5] = nz;
        mesh.vertices[i * 8 + 6] = u;
        mesh.vertices[i * 8 + 7] = v;
        math::aabbExpand(mesh.bounds, {px, py, pz});
      }

      if (const JsonVal* idxAcc = prim.get("indices")) {
        int ic = 0; std::string it; size_t iCount = 0, is = 0, iAvail = 0;
        const uint8_t* iPtr = nullptr;
        if (!readAccessorSlice(root, buffers, idxAcc->asInt(-1), ic, it, iCount, iPtr, is, iAvail))
          return false;
        const size_t elemBytes = (ic == 5121) ? 1u : (ic == 5123 ? 2u : (ic == 5125 ? 4u : 0u));
        if (elemBytes == 0) return false;
        const size_t iStep = is == 0 ? elemBytes : is;
        if ((iCount - 1u) * iStep + elemBytes > iAvail) return false;
        mesh.indices.resize(iCount);
        for (size_t i = 0; i < iCount; ++i) {
          uint32_t idx = 0;
          if (ic == 5121) idx = iPtr[i * iStep];
          else if (ic == 5123) idx = readU16Le(iPtr + i * iStep);
          else idx = readU32Le(iPtr + i * iStep);
          if (idx >= posCount) return false;
          mesh.indices[i] = idx;
        }
      } else {
        mesh.indices.resize(posCount);
        for (size_t i = 0; i < posCount; ++i) mesh.indices[i] = static_cast<uint32_t>(i);
      }

      scene.meshes.push_back(std::move(mesh));
      scene.meshNames.push_back(mName);
    }
  }

  if (scene.meshes.empty()) return false;
  out = std::move(scene);
  return true;
}

bool importGlb(const uint8_t* data, size_t size, GltfScene& out) {
  if (!data || size < 12u) return false;
  if (readU32Le(data) != 0x46546C67u) return false;  // "glTF"
  const uint32_t version = readU32Le(data + 4);
  const uint32_t totalLen = readU32Le(data + 8);
  if (version != 2 || totalLen > size || totalLen < 12u) return false;

  std::string jsonChunk;
  std::vector<uint8_t> binChunk;
  size_t offset = 12u;
  while (offset + 8u <= totalLen) {
    const uint32_t chunkLen = readU32Le(data + offset);
    const uint32_t chunkType = readU32Le(data + offset + 4);
    offset += 8u;
    if (chunkLen > totalLen - offset) return false;
    if (chunkType == 0x4E4F534Au) {  // "JSON"
      jsonChunk.assign(reinterpret_cast<const char*>(data + offset), chunkLen);
    } else if (chunkType == 0x004E4942u) {  // "BIN\0"
      binChunk.assign(data + offset, data + offset + chunkLen);
    }
    offset += chunkLen;
  }
  if (jsonChunk.empty()) return false;
  return importGltf(jsonChunk, binChunk, {}, out);
}

// ---- Texture Importer (PPM P6/P3, TGA, BMP) ------------------------------

bool importTexture(const uint8_t* data, size_t size, TextureImport& out) {
  if (!data || size < 4u) return false;

  // PPM P6 binary or P3 ASCII
  if (data[0] == 'P' && (data[1] == '6' || data[1] == '3')) {
    const bool binary = (data[1] == '6');
    size_t pos = 2;
    auto nextToken = [&](std::string& tok) -> bool {
      tok.clear();
      while (pos < size) {
        if (data[pos] == '#') {
          while (pos < size && data[pos] != '\n') ++pos;
        } else if (std::isspace(data[pos])) {
          ++pos;
        } else {
          break;
        }
      }
      while (pos < size && !std::isspace(data[pos]) && data[pos] != '#') {
        tok.push_back(static_cast<char>(data[pos++]));
      }
      return !tok.empty();
    };
    std::string wStr, hStr, maxStr;
    if (!nextToken(wStr) || !nextToken(hStr) || !nextToken(maxStr)) return false;
    const uint32_t w = static_cast<uint32_t>(std::strtoul(wStr.c_str(), nullptr, 10));
    const uint32_t h = static_cast<uint32_t>(std::strtoul(hStr.c_str(), nullptr, 10));
    const uint32_t maxV = static_cast<uint32_t>(std::strtoul(maxStr.c_str(), nullptr, 10));
    if (w == 0 || h == 0 || w > 16384 || h > 16384 || maxV == 0 || maxV > 255) return false;
    TextureImport tex{w, h, 4, std::vector<uint8_t>(static_cast<size_t>(w) * h * 4u, 255)};
    if (binary) {
      if (pos >= size) return false;
      ++pos;  // single whitespace byte
      const size_t rgbBytes = static_cast<size_t>(w) * h * 3u;
      if (rgbBytes > size - pos) return false;
      for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        tex.rgba[i * 4 + 0] = data[pos + i * 3 + 0];
        tex.rgba[i * 4 + 1] = data[pos + i * 3 + 1];
        tex.rgba[i * 4 + 2] = data[pos + i * 3 + 2];
        tex.rgba[i * 4 + 3] = 255;
      }
    } else {
      for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        std::string r, g, b;
        if (!nextToken(r) || !nextToken(g) || !nextToken(b)) return false;
        tex.rgba[i * 4 + 0] = static_cast<uint8_t>(std::atoi(r.c_str()));
        tex.rgba[i * 4 + 1] = static_cast<uint8_t>(std::atoi(g.c_str()));
        tex.rgba[i * 4 + 2] = static_cast<uint8_t>(std::atoi(b.c_str()));
        tex.rgba[i * 4 + 3] = 255;
      }
    }
    out = std::move(tex);
    return true;
  }

  // Windows BMP (24-bit or 32-bit uncompressed BI_RGB)
  if (size >= 54u && data[0] == 'B' && data[1] == 'M') {
    const uint32_t dataOffset = readU32Le(data + 10);
    const int32_t w = static_cast<int32_t>(readU32Le(data + 18));
    const int32_t rawH = static_cast<int32_t>(readU32Le(data + 22));
    const uint16_t bpp = readU16Le(data + 28);
    const uint32_t compression = readU32Le(data + 30);
    const uint32_t h = static_cast<uint32_t>(std::abs(rawH));
    if (w <= 0 || h == 0 || w > 16384 || h > 16384 || (bpp != 24 && bpp != 32) ||
        compression != 0 || dataOffset >= size)
      return false;
    const size_t uw = static_cast<size_t>(w);
    const size_t bytesPerPx = bpp / 8u;
    const size_t rowStride = ((uw * bytesPerPx + 3u) / 4u) * 4u;
    if (rowStride * h > size - dataOffset) return false;
    TextureImport tex{static_cast<uint32_t>(w), h, 4, std::vector<uint8_t>(uw * h * 4u, 255)};
    const bool topDown = rawH < 0;
    for (uint32_t y = 0; y < h; ++y) {
      const uint32_t srcY = topDown ? y : (h - 1u - y);
      const uint8_t* row = data + dataOffset + static_cast<size_t>(srcY) * rowStride;
      for (size_t x = 0; x < uw; ++x) {
        const size_t dst = (static_cast<size_t>(y) * uw + x) * 4u;
        tex.rgba[dst + 0] = row[x * bytesPerPx + 2];
        tex.rgba[dst + 1] = row[x * bytesPerPx + 1];
        tex.rgba[dst + 2] = row[x * bytesPerPx + 0];
        tex.rgba[dst + 3] = bpp == 32 ? row[x * bytesPerPx + 3] : 255;
      }
    }
    out = std::move(tex);
    return true;
  }

  // Uncompressed TrueColor TGA (type 2, 24 or 32 bpp)
  if (size >= 18u && data[1] == 0 && data[2] == 2) {
    const uint8_t idLen = data[0];
    const uint16_t w = readU16Le(data + 12);
    const uint16_t h = readU16Le(data + 14);
    const uint8_t bpp = data[16];
    const uint8_t desc = data[17];
    if (w == 0 || h == 0 || (bpp != 24 && bpp != 32)) return false;
    const size_t pixelOffset = 18u + idLen;
    const size_t bytesPerPx = bpp / 8u;
    const size_t need = static_cast<size_t>(w) * h * bytesPerPx;
    if (pixelOffset > size || need > size - pixelOffset) return false;
    TextureImport tex{w, h, 4, std::vector<uint8_t>(static_cast<size_t>(w) * h * 4u, 255)};
    const bool topOrigin = (desc & 0x20u) != 0;
    for (uint32_t y = 0; y < h; ++y) {
      const uint32_t srcY = topOrigin ? y : (h - 1u - y);
      const uint8_t* row = data + pixelOffset + static_cast<size_t>(srcY) * w * bytesPerPx;
      for (uint32_t x = 0; x < w; ++x) {
        const size_t dst = (static_cast<size_t>(y) * w + x) * 4u;
        tex.rgba[dst + 0] = row[x * bytesPerPx + 2];
        tex.rgba[dst + 1] = row[x * bytesPerPx + 1];
        tex.rgba[dst + 2] = row[x * bytesPerPx + 0];
        tex.rgba[dst + 3] = bpp == 32 ? row[x * bytesPerPx + 3] : 255;
      }
    }
    out = std::move(tex);
    return true;
  }

  return false;
}

bool importTextureFile(const std::string& path, TextureImport& out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) return false;
  const std::streamsize len = f.tellg();
  if (len <= 0 || len > 128 * 1024 * 1024) return false;
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(len));
  if (!f.read(reinterpret_cast<char*>(bytes.data()), len)) return false;
  return importTexture(bytes.data(), bytes.size(), out);
}

// ---- AssetRegistry & Bounded LRU Cache -----------------------------------

AssetId AssetRegistry::registerAsset(const std::string& path, Kind kind,
                                     const std::vector<AssetId>& dependencies) {
  const AssetId id = idFromPath(path);
  Record rec;
  rec.id = id;
  rec.path = path;
  rec.kind = kind;
  rec.dependencies = dependencies;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    const auto ft = std::filesystem::last_write_time(path, ec);
    if (!ec) {
      rec.lastModifiedNs =
          std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
    }
  }
  records_[id] = std::move(rec);
  pathIndex_[path] = id;
  return id;
}

const AssetRegistry::Record* AssetRegistry::find(AssetId id) const {
  auto it = records_.find(id);
  return it == records_.end() ? nullptr : &it->second;
}

const AssetRegistry::Record* AssetRegistry::findByPath(const std::string& path) const {
  auto pit = pathIndex_.find(path);
  if (pit == pathIndex_.end()) return nullptr;
  return find(pit->second);
}

std::vector<AssetId> AssetRegistry::dependentsOf(AssetId dependency) const {
  std::vector<AssetId> out;
  for (const auto& [id, rec] : records_) {
    if (std::find(rec.dependencies.begin(), rec.dependencies.end(), dependency) !=
        rec.dependencies.end()) {
      out.push_back(id);
    }
  }
  return out;
}

void AssetRegistry::setCacheBudget(size_t maxBytes) {
  maxCacheBytes_ = maxBytes;
  while (currentCacheBytes_ > maxCacheBytes_ && !lruList_.empty()) {
    const CacheEntry& victim = lruList_.back();
    currentCacheBytes_ -= victim.bytes.size();
    lruMap_.erase(victim.id);
    lruList_.pop_back();
  }
}

void AssetRegistry::cachePut(AssetId id, std::vector<uint8_t> data) {
  auto existing = lruMap_.find(id);
  if (existing != lruMap_.end()) {
    currentCacheBytes_ -= existing->second->bytes.size();
    lruList_.erase(existing->second);
    lruMap_.erase(existing);
  }
  if (data.size() > maxCacheBytes_) return;
  while (currentCacheBytes_ + data.size() > maxCacheBytes_ && !lruList_.empty()) {
    const CacheEntry& victim = lruList_.back();
    currentCacheBytes_ -= victim.bytes.size();
    lruMap_.erase(victim.id);
    lruList_.pop_back();
  }
  currentCacheBytes_ += data.size();
  lruList_.push_front({id, std::move(data)});
  lruMap_[id] = lruList_.begin();
}

bool AssetRegistry::cacheGet(AssetId id, std::vector<uint8_t>& out) {
  auto it = lruMap_.find(id);
  if (it == lruMap_.end()) return false;
  lruList_.splice(lruList_.begin(), lruList_, it->second);
  out = it->second->bytes;
  return true;
}

void AssetRegistry::cacheClear() {
  lruList_.clear();
  lruMap_.clear();
  currentCacheBytes_ = 0;
}

std::vector<AssetId> AssetRegistry::pollModified() {
  std::vector<AssetId> modified;
  std::error_code ec;
  for (auto& [id, rec] : records_) {
    if (rec.path.empty() || !std::filesystem::exists(rec.path, ec)) continue;
    const auto ft = std::filesystem::last_write_time(rec.path, ec);
    if (ec) continue;
    const int64_t ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
    if (ns != rec.lastModifiedNs) {
      rec.lastModifiedNs = ns;
      modified.push_back(id);
    }
  }
  return modified;
}

}  // namespace nimbus::asset
