// Nimbus asset system: content-addressed packs, pack builder, async streaming,
// glTF 2.0 / GLB & OBJ & Texture (PPM/TGA/BMP) importers, GUID registry & LRU cache.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::asset {

using AssetId = uint64_t;

constexpr AssetId kNullAsset = 0;

enum class Kind : uint8_t { Mesh, Texture, Material, Clip, Audio, Script, Scene, Unknown };

struct Header {
  char magic[4] = {'N', 'B', 'P', 'K'};
  uint32_t version = 1;
  uint32_t entryCount = 0;
  uint64_t tableOffset = 0;
};

struct Entry {
  AssetId id = kNullAsset;
  Kind kind = Kind::Unknown;
  uint64_t offset = 0;
  uint64_t size = 0;
  uint64_t uncompressedSize = 0;
  uint32_t crc32 = 0;
  char name[64] = {};
};

AssetId idFromPath(const std::string& path);
const char* kindName(Kind kind);

class Pack {
 public:
  bool open(const std::string& path);
  void close();
  bool valid() const { return valid_; }
  const std::vector<Entry>& entries() const { return entries_; }
  const Entry* find(AssetId id) const;
  const Entry* findByName(const std::string& name) const;
  uint64_t read(AssetId id, std::vector<uint8_t>& out) const;
  uint32_t entryCount() const { return static_cast<uint32_t>(entries_.size()); }

 private:
  bool valid_ = false;
  std::string path_;
  std::vector<Entry> entries_;
  std::unordered_map<AssetId, uint32_t> index_;
};

// Writes spec-compliant NBPK v1 asset packs with CRC-32 checksums.
class PackBuilder {
 public:
  void addData(const std::string& name, Kind kind, const std::vector<uint8_t>& bytes);
  bool addFile(const std::string& name, Kind kind, const std::string& filePath);
  bool write(const std::string& outputPath) const;
  uint32_t itemCount() const { return static_cast<uint32_t>(items_.size()); }
  void clear() { items_.clear(); }

 private:
  struct Item {
    std::string name;
    Kind kind = Kind::Unknown;
    std::vector<uint8_t> data;
  };
  std::vector<Item> items_;
};

class Loader {
 public:
  using Callback = std::function<void(AssetId, std::vector<uint8_t>)>;

  explicit Loader(Pack* pack, uint32_t maxPendingRequests = 256);
  ~Loader();
  Loader(const Loader&) = delete;
  Loader& operator=(const Loader&) = delete;

  bool request(AssetId id, Callback callback);
  void update();
  uint32_t pending() const;

 private:
  struct Request {
    AssetId id = kNullAsset;
    Callback callback;
  };
  struct Completion {
    AssetId id = kNullAsset;
    Callback callback;
    std::vector<uint8_t> bytes;
  };

  void workerLoop();

  Pack* pack_ = nullptr;
  const uint32_t maxPendingRequests_ = 256;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Request> queue_;
  std::deque<Completion> completed_;
  std::thread worker_;
  uint32_t active_ = 0;
  bool stopping_ = false;
};

struct MeshImport {
  std::vector<float> vertices;  // pos3, normal3, uv2
  std::vector<uint32_t> indices;
  math::Aabb bounds{};
};

struct TextureImport {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 4;  // normalized to RGBA8
  std::vector<uint8_t> rgba;
};

struct GltfMaterialImport {
  std::string name;
  math::Vec4 baseColor{1, 1, 1, 1};
  float metallic = 0.0f;
  float roughness = 0.5f;
};

struct GltfScene {
  std::vector<MeshImport> meshes;
  std::vector<std::string> meshNames;
  std::vector<GltfMaterialImport> materials;
};

bool importObj(const std::string& text, MeshImport& out);
bool importGltf(const std::string& jsonText, const std::vector<uint8_t>& binChunk,
                const std::string& baseDir, GltfScene& out);
bool importGlb(const uint8_t* data, size_t size, GltfScene& out);
bool importTexture(const uint8_t* data, size_t size, TextureImport& out);
bool importTextureFile(const std::string& path, TextureImport& out);

// Content registry: GUID mapping, dependencies, bounded LRU byte cache, hot-reload watcher.
class AssetRegistry {
 public:
  struct Record {
    AssetId id = kNullAsset;
    std::string path;
    Kind kind = Kind::Unknown;
    std::vector<AssetId> dependencies;
    int64_t lastModifiedNs = 0;
  };

  AssetId registerAsset(const std::string& path, Kind kind,
                        const std::vector<AssetId>& dependencies = {});
  const Record* find(AssetId id) const;
  const Record* findByPath(const std::string& path) const;
  std::vector<AssetId> dependentsOf(AssetId dependency) const;

  void setCacheBudget(size_t maxBytes);
  size_t cacheBudget() const { return maxCacheBytes_; }
  size_t cachedBytes() const { return currentCacheBytes_; }
  void cachePut(AssetId id, std::vector<uint8_t> data);
  bool cacheGet(AssetId id, std::vector<uint8_t>& out);
  void cacheClear();

  // Scans registered filesystem paths and returns IDs whose mtime changed.
  std::vector<AssetId> pollModified();

 private:
  std::unordered_map<AssetId, Record> records_;
  std::unordered_map<std::string, AssetId> pathIndex_;

  struct CacheEntry {
    AssetId id = kNullAsset;
    std::vector<uint8_t> bytes;
  };
  size_t maxCacheBytes_ = 64ull * 1024ull * 1024ull;
  size_t currentCacheBytes_ = 0;
  std::list<CacheEntry> lruList_;
  std::unordered_map<AssetId, std::list<CacheEntry>::iterator> lruMap_;
};

}  // namespace nimbus::asset
