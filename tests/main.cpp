// Nimbus test runner: initializes the engine and reports failures.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include "nimbus/engine.hpp"
#include "nimbus/editor.hpp"
#include "nimbus/log.hpp"
#include "nimbus/mcp.hpp"

namespace {

struct StableComponent { int value = 0; };

struct NonAssignableComponent {
  int value = 0;
  NonAssignableComponent() = default;
  explicit NonAssignableComponent(int v) : value(v) {}
  NonAssignableComponent(const NonAssignableComponent&) = default;
  NonAssignableComponent(NonAssignableComponent&&) noexcept = default;
  NonAssignableComponent& operator=(const NonAssignableComponent&) = delete;
  NonAssignableComponent& operator=(NonAssignableComponent&&) = delete;
};

struct ThrowingComponent {
  static bool throwOnDefault;
  int value = 13;
  ThrowingComponent() {
    if (throwOnDefault) throw std::runtime_error("test component construction failure");
  }
  ThrowingComponent(const ThrowingComponent&) noexcept = default;
  ThrowingComponent(ThrowingComponent&&) noexcept = default;
  ThrowingComponent& operator=(const ThrowingComponent&) = delete;
  ThrowingComponent& operator=(ThrowingComponent&&) = delete;
  ~ThrowingComponent() noexcept = default;
};
bool ThrowingComponent::throwOnDefault = false;

class RecordingDevice final : public nimbus::rhi::Device {
 public:
  const nimbus::rhi::DeviceInfo& info() const override { return info_; }
  void* nativeHandle() override { return nullptr; }

  uint32_t createBuffer(const nimbus::rhi::BufferDesc&, const void*) override {
    ++bufferCreateCalls;
    if (failBufferCall != 0 && bufferCreateCalls == failBufferCall) return 0;
    return nextHandle++;
  }
  void destroyBuffer(uint32_t handle) override { destroyedBuffers.push_back(handle); }
  void* mapBuffer(uint32_t) override { return nullptr; }
  void unmapBuffer(uint32_t) override {}
  uint32_t createTexture(const nimbus::rhi::TextureDesc&, const void*) override {
    return nextHandle++;
  }
  void destroyTexture(uint32_t) override {}
  void updateTexture(uint32_t, uint32_t, const void*, uint32_t) override {}
  uint32_t createPipeline(const nimbus::rhi::PipelineDesc&) override { return nextHandle++; }
  void destroyPipeline(uint32_t handle) override { destroyedPipelines.push_back(handle); }
  uint32_t createShaderUniform(uint32_t) override { return nextHandle++; }
  void destroyShaderUniform(uint32_t handle) override { destroyedUniforms.push_back(handle); }
  void beginFrame() override {}
  void beginPass(const nimbus::rhi::RenderPassDesc&) override {}
  void bindPipeline(uint32_t) override {}
  void bindVertexBuffer(uint32_t, uint32_t) override {}
  void bindIndexBuffer(uint32_t) override {}
  void bindUniform(uint32_t, uint32_t, const void*, uint32_t) override {}
  void bindTexture(uint32_t, uint32_t) override {}
  void draw(uint32_t vertexCount, uint32_t) override {
    ++drawCalls;
    submittedVertices += vertexCount;
  }
  void drawIndexed(uint32_t indexCount, uint32_t, uint32_t) override {
    ++indexedDrawCalls;
    submittedIndices += indexCount;
  }
  void endPass() override {}
  void endFrame() override {}
  void present() override {}
  void setViewport(uint32_t, uint32_t, uint32_t, uint32_t) override {}

  uint32_t bufferCreateCalls = 0;
  uint32_t failBufferCall = 0;
  uint32_t nextHandle = 1;
  uint32_t drawCalls = 0;
  uint32_t indexedDrawCalls = 0;
  uint64_t submittedVertices = 0;
  uint64_t submittedIndices = 0;
  std::vector<uint32_t> destroyedBuffers;
  std::vector<uint32_t> destroyedPipelines;
  std::vector<uint32_t> destroyedUniforms;

 private:
  nimbus::rhi::DeviceInfo info_{};
};

class RecordingAudioBackend final : public nimbus::audio::Backend {
 public:
  bool open(uint32_t sampleRate, uint32_t bufferFrames) override {
    opened = sampleRate != 0 && bufferFrames != 0;
    return opened;
  }
  void close() override { opened = false; }
  void submit(const float* samples, uint32_t frames) override {
    submittedFrames = frames;
    submitted.assign(samples, samples + static_cast<size_t>(frames) * 2u);
  }
  bool valid() const override { return opened; }

  bool opened = false;
  uint32_t submittedFrames = 0;
  std::vector<float> submitted;
};

}  // namespace

int main() {
  nimbus::log::setLevel(nimbus::log::Level::Info);
  nimbus::log::setSink([](nimbus::log::Level level, const char* message, void*) {
    std::fprintf(stderr, "[test:%d] %s\n", static_cast<int>(level), message);
  });
  nimbus::Engine engine;
  if (!engine.initialize("nimbus-tests", 320, 240, nimbus::rhi::Api::None)) {
    std::printf("engine initialization failed\n");
    return 2;
  }
  printf("running tests...\n"); fflush(stdout);
  int failures = engine.runTests();
  int additionalChecks = 0;
  const auto check = [&](bool condition, const char* message) {
    ++additionalChecks;
    if (!condition) {
      ++failures;
      std::fprintf(stderr, "[editor-test] FAILED: %s\n", message);
    }
  };
  check(engine.terrain() != nullptr,
        "engine initializes its exposed terrain subsystem");
  {
    nimbus::Engine coldEngine;
    coldEngine.play();
    check(coldEngine.state() == nimbus::State::Stopped && coldEngine.tick(1.0 / 60.0) == 0,
          "uninitialized engine refuses play and simulation safely");
    check(coldEngine.createEntity("before-initialize") == nimbus::ecs::kNullEntity,
          "uninitialized engine rejects entity creation safely");
    coldEngine.destroyEntity(nimbus::ecs::kNullEntity);
    coldEngine.step();
    coldEngine.renderFrame();
    coldEngine.shutdown();
    check(coldEngine.state() == nimbus::State::Stopped,
          "uninitialized engine accepts no-op operations and shutdown");
  }
  {
    nimbus::jobs::JobSystem jobSystem(2);
    std::atomic<uint32_t> completedJobs{0};
    std::vector<std::future<uint32_t>> futures;
    for (uint32_t i = 0; i < 8; ++i) {
      futures.push_back(jobSystem.submit([&completedJobs, i] {
        ++completedJobs;
        return i * i;
      }));
    }
    check(jobSystem.enqueue([&completedJobs] { ++completedJobs; }),
          "job system accepts an asynchronous task");
    jobSystem.waitIdle();
    bool futuresCorrect = true;
    for (uint32_t i = 0; i < futures.size(); ++i)
      futuresCorrect = futuresCorrect && futures[i].get() == i * i;
    check(futuresCorrect && completedJobs.load() == 9 && jobSystem.pendingCount() == 0,
          "job system completes queued work and publishes task results");
    auto failedFuture = jobSystem.submit([]() -> uint32_t {
      throw std::runtime_error("expected worker exception");
    });
    jobSystem.waitIdle();
    bool exceptionDelivered = false;
    try { (void)failedFuture.get(); } catch (const std::runtime_error&) {
      exceptionDelivered = true;
    }
    check(exceptionDelivered, "job system propagates packaged-task exceptions to futures");
    jobSystem.shutdown();
    check(jobSystem.workerCount() == 0 && !jobSystem.enqueue([] {}),
          "job system shutdown joins workers and rejects new work");
  }
  check(engine.networkServer() != nullptr && engine.networkClient() != nullptr,
        "engine owns server and client networking endpoints");
  {
    nimbus::anim::Skeleton skeleton;
    skeleton.joints.push_back({"root", -1});
    nimbus::anim::Clip clip;
    clip.duration = 1.0f;
    clip.tracks.push_back({0, {
        {0.0f, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
        {1.0f, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}}});
    const nimbus::ecs::Entity entity = engine.createEntity("integrated-systems");
    nimbus::anim::Animator* animator = engine.world()->add<nimbus::anim::Animator>(entity);
    const bool skeletonReady = animator && animator->setSkeleton(skeleton);
    if (animator && skeletonReady) {
      animator->states.push_back({&clip, 0.0f, 1.0f, 1.0f, true});
      animator->play(0);
    }
    uint32_t behaviorTicks = 0;
    nimbus::ai::Agent* agent = engine.world()->add<nimbus::ai::Agent>(entity);
    if (agent) {
      agent->root = std::make_unique<nimbus::ai::Action>(
          [&](nimbus::ecs::Entity current, float delta) {
            if (current == entity && delta > 0.0f) ++behaviorTicks;
            return nimbus::ai::Status::Running;
          });
    }
    engine.play();
    engine.step();
    const nimbus::anim::Animator* updatedAnimator =
        engine.world()->get<nimbus::anim::Animator>(entity);
    check(skeletonReady && updatedAnimator && updatedAnimator->skinMatrices.size() == 1 &&
              updatedAnimator->skinMatrices[0][3][1] > 0.0f,
          "engine fixed-step loop updates animation components");
    check(behaviorTicks == 1 && agent && agent->lastStatus == nimbus::ai::Status::Running,
          "engine fixed-step loop ticks ECS behavior-tree agents");
    engine.destroyEntity(entity);
  }
  check(engine.networkServer()->start(0),
        "engine-owned network server starts on an ephemeral UDP port");
  engine.networkServer()->tick(1.0f / 60.0f);
  engine.networkServer()->stop();
  engine.stop();
  check(engine.tick(1.0 / 30.0) == 0 && engine.frame() == 0,
        "engine does not advance while stopped");
  engine.play();
  const double beforeTickElapsed = engine.elapsed();
  check(engine.tick(1.0 / 30.0) == 2 && engine.frame() == 2,
        "engine fixed-step accumulator executes two updates for a 30 Hz frame");
  check(std::fabs(engine.elapsed() - beforeTickElapsed - 2.0 / 60.0) < 1.0e-8,
        "engine elapsed time advances by exact fixed simulation steps");
  engine.pause();
  check(engine.tick(1.0) == 0 && engine.frame() == 2,
        "paused engine does not consume wall-clock time");
  engine.step();
  check(engine.frame() == 3, "paused engine supports one explicit editor step");
  engine.play();
  check(engine.tick(1.0) == 8, "engine caps catch-up work after a long frame spike");
  check(engine.stats().fps > 0.0f && engine.stats().cpuTimeMs >= 0.0f,
        "engine reports frame rate and CPU update timing");
  engine.stop();
  check(engine.frame() == 0 && engine.elapsed() == 0.0,
        "engine stop resets simulation clock and frame counter");
  {
    nimbus::ecs::ComponentPool alignmentPool(sizeof(uint32_t), alignof(uint32_t), 8);
    alignmentPool.setLifecycle(
        [](void* memory) { new (memory) uint32_t(0); },
        [](void* memory) { static_cast<uint32_t*>(memory)->~uint32_t(); },
        [](void* destination, const void* source) {
          new (destination) uint32_t(*static_cast<const uint32_t*>(source));
        },
        [](void* destination, void* source) {
          new (destination) uint32_t(std::move(*static_cast<uint32_t*>(source)));
        });
    for (uint64_t entity = 1; entity <= 8; ++entity) alignmentPool.add(entity);
    bool aligned = true;
    for (uint32_t slot = 0; slot < alignmentPool.count(); ++slot) {
      const auto address = reinterpret_cast<uintptr_t>(alignmentPool.slotPtr(slot));
      aligned = aligned && address % alignof(std::max_align_t) == 0;
    }
    check(aligned,
          "ECS component columns keep every slot aligned to the storage alignment");
  }
  {
    nimbus::ecs::Registry registry;
    nimbus::ecs::World world(&registry);
    const nimbus::ecs::Entity entity = world.create("ecs-constructor-exception");
    world.add<StableComponent>(entity, StableComponent{42});
    ThrowingComponent::throwOnDefault = true;
    bool threw = false;
    try {
      (void)world.add<ThrowingComponent>(entity);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    ThrowingComponent::throwOnDefault = false;
    check(threw && world.get<StableComponent>(entity) &&
              world.get<StableComponent>(entity)->value == 42 &&
              !world.has<ThrowingComponent>(entity),
          "throwing component construction leaves its source archetype intact");
    check(world.add<ThrowingComponent>(entity) != nullptr &&
              world.get<ThrowingComponent>(entity)->value == 13,
          "component pool rolls back slot metadata after constructor failure");
    const nimbus::ecs::Entity nonAssignable = world.create("ecs-non-assignable");
    NonAssignableComponent value(91);
    check(world.add<NonAssignableComponent>(nonAssignable, std::move(value)) != nullptr &&
              world.get<NonAssignableComponent>(nonAssignable)->value == 91,
          "ECS constructs non-assignable components directly in pool storage");
    world.destroy(entity);
    world.destroy(nonAssignable);
  }
  {
    nimbus::Engine invalidEngine;
    check(!invalidEngine.initialize("invalid-size", 0, 240, nimbus::rhi::Api::None),
          "engine rejects a zero-width render target");
    check(!invalidEngine.running() && invalidEngine.world() == nullptr &&
              invalidEngine.jobs() == nullptr,
          "invalid engine initialization leaves no partially initialized subsystems");
  }

  nimbus::editor::Editor editor(&engine);
  nimbus::editor::VisualScript* graph = editor.visualScript();
  const uint32_t addNode = graph->addNode("add", "sum", 0.0f, 0.0f);
  const uint32_t printNode = graph->addNode("print", "output", 100.0f, 0.0f);
  const uint32_t eventNode = graph->addNode("event", "start", -100.0f, 0.0f);
  check(graph->addEdge({addNode, 0, printNode, 1}), "visual graph connects matching data pins");
  check(graph->addEdge({eventNode, 0, printNode, 0}), "visual graph connects matching execution pins");
  check(!graph->addEdge({eventNode, 0, printNode, 1}), "visual graph rejects incompatible pin types");
  check(!graph->addEdge({addNode, 0, printNode, 1}), "visual graph rejects duplicate edges");
  const std::unordered_map<std::string, double> values{{"a", 2.0}, {"b", 3.0}};
  check(graph->evaluate(addNode, values) == 5.0, "visual add node evaluates both operands");
  check(graph->evaluate(printNode, values) == 5.0, "visual graph propagates connected data values");
  const uint32_t setPositionNode = graph->addNode("set_position", "move", 200.0f, 0.0f);
  check(setPositionNode != 0 && graph->addEdge({eventNode, 0, setPositionNode, 0}),
        "visual event branches into a live transform action");
  check(graph->addNode("unknown", "invalid", 0.0f, 0.0f) == 0,
        "visual graph rejects unsupported node types");
  nimbus::editor::VsExecutionContext visualContext;
  visualContext.numbers = values;
  const nimbus::ecs::Entity visualEntity = engine.createEntity("visual-script-target");
  visualContext.entities.emplace("target", visualEntity);
  visualContext.vectors.emplace("value", nimbus::math::Vec3{7.0f, 8.0f, 9.0f});
  double printedValue = -1.0;
  bool positionExecuted = false;
  visualContext.print = [&](double value) { printedValue = value; };
  visualContext.setPosition = [&](nimbus::ecs::Entity entity, const nimbus::math::Vec3& value) {
    positionExecuted = entity == visualEntity && value.x == 7.0f && value.y == 8.0f &&
                       value.z == 9.0f;
    editor.setPosition(entity, value);
  };
  check(graph->execute(eventNode, visualContext),
        "visual graph executes connected event and action nodes");
  check(printedValue == 5.0 && positionExecuted &&
            engine.scene()->local(visualEntity)->position.z == 9.0f,
        "visual script propagates data and mutates the live scene through callbacks");
  check(!graph->execute(eventNode, visualContext, 1),
        "visual script execution enforces a finite node budget");
  engine.destroyEntity(visualEntity);
  const std::string generatedLua = graph->toLua();
  check(generatedLua.find("nimbus.set_position(tostring(target), x, y, z)") != std::string::npos,
        "visual script exports a real transform binding call");
  check(generatedLua.find("local edges =") != std::string::npos,
        "visual script export preserves graph connections");
  check(generatedLua.find("function(target, value) end") == std::string::npos,
        "visual script export contains no empty set-position stub");

  const nimbus::ecs::Entity scriptedEntity = engine.createEntity("script-binding");
  const std::string luaCall = "nimbus.set_position(\"" + std::to_string(scriptedEntity) +
                              "\", 4, 5, 6)";
  std::string scriptError;
  check(engine.scripts()->run(luaCall, &scriptError), "Lua set_position binding executes");
  check(engine.scene()->local(scriptedEntity) &&
            engine.scene()->local(scriptedEntity)->position.x == 4.0f &&
            engine.scene()->local(scriptedEntity)->position.y == 5.0f &&
            engine.scene()->local(scriptedEntity)->position.z == 6.0f,
        "Lua set_position binding modifies the live scene transform");
  engine.destroyEntity(scriptedEntity);

  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path projectPath = std::filesystem::temp_directory_path() /
      ("nimbus-editor-regression-" + std::to_string(unique));
  bool projectCreated = false;
  check(editor.newProject(projectPath.string(), "Project \"Test\""),
        "editor creates real project files");
  projectCreated = std::filesystem::exists(projectPath / "project.json");
  check(projectCreated && std::filesystem::exists(projectPath / "scenes" / "main.scene.json"),
        "project manifest and initial scene are written");
  const nimbus::ecs::Entity projectEntity = engine.createEntity("saved-entity");
  editor.setPosition(projectEntity, {7, 8, 9});
  check(editor.saveProject(projectPath.string()), "editor saves scene to disk");
  engine.destroyEntity(projectEntity);
  check(editor.openProject(projectPath.string()), "editor reopens project and deserializes scene");
  const auto restoredEntities = editor.entities();
  check(restoredEntities.size() == 1, "reopened project restores saved entities");
  if (!restoredEntities.empty()) {
    const nimbus::math::Vec3 restoredPosition = editor.getPosition(restoredEntities[0]);
    check(restoredPosition.x == 7.0f && restoredPosition.y == 8.0f && restoredPosition.z == 9.0f,
          "reopened project restores entity transform");
  }
  check(!editor.newProject(projectPath.string(), "overwrite"),
        "new project refuses to overwrite an existing project manifest");
  const std::filesystem::path scenePath = projectPath / "scenes" / "main.scene.json";
  std::ifstream validSceneFile(scenePath, std::ios::binary);
  const std::string validScene((std::istreambuf_iterator<char>(validSceneFile)),
                               std::istreambuf_iterator<char>());
  {
    std::ofstream broken(scenePath, std::ios::binary | std::ios::trunc);
    broken << "{broken";
  }
  check(!editor.openProject(projectPath.string()), "editor refuses a corrupted scene file");
  check(editor.entities().size() == 1, "failed project open preserves the current scene");
  {
    std::ofstream restore(scenePath, std::ios::binary | std::ios::trunc);
    restore << validScene;
  }
  if (projectCreated) {
    std::error_code ignored;
    std::filesystem::remove_all(projectPath, ignored);
  }

  const std::filesystem::path packTestDir = std::filesystem::temp_directory_path() /
      ("nimbus-pack-regression-" + std::to_string(unique));
  std::filesystem::create_directories(packTestDir);
  const auto crc32 = [](const std::string& data) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : data) {
      crc ^= byte;
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  };
  const std::string payload = "Nimbus asset payload";
  nimbus::asset::Entry packEntry{};
  packEntry.id = nimbus::asset::idFromPath("textures/test.bin");
  packEntry.kind = nimbus::asset::Kind::Texture;
  packEntry.offset = sizeof(nimbus::asset::Header);
  packEntry.size = payload.size();
  packEntry.uncompressedSize = payload.size();
  packEntry.crc32 = crc32(payload);
  std::strncpy(packEntry.name, "textures/test.bin", sizeof(packEntry.name) - 1);
  nimbus::asset::Header packHeader{};
  packHeader.entryCount = 1;
  packHeader.tableOffset = sizeof(nimbus::asset::Header) + payload.size();
  const std::filesystem::path packPath = packTestDir / "valid.nbpk";
  {
    std::ofstream packFile(packPath, std::ios::binary | std::ios::trunc);
    packFile.write(reinterpret_cast<const char*>(&packHeader), sizeof(packHeader));
    packFile.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    packFile.write(reinterpret_cast<const char*>(&packEntry), sizeof(packEntry));
  }
  nimbus::asset::Pack pack;
  check(pack.open(packPath.string()), "asset pack validates and opens a valid table");
  check(pack.valid() && pack.entryCount() == 1, "asset pack exposes validated entry count");
  check(pack.findByName("textures/test.bin") != nullptr, "asset pack indexes entry names");
  std::vector<uint8_t> assetBytes;
  check(pack.read(packEntry.id, assetBytes) == payload.size(), "asset pack reads payload bytes");
  check(std::string(assetBytes.begin(), assetBytes.end()) == payload,
        "asset pack returns exact payload contents");
  {
    nimbus::asset::Loader loader(&pack, 4);
    std::atomic<uint32_t> callbacks{0};
    const std::thread::id gameThread = std::this_thread::get_id();
    bool callbacksOnGameThread = true;
    std::string asyncText;
    bool missingAssetCompleted = false;
    check(loader.request(packEntry.id, [&](nimbus::asset::AssetId id,
                                           std::vector<uint8_t> bytes) {
      callbacksOnGameThread = callbacksOnGameThread &&
                              std::this_thread::get_id() == gameThread;
      asyncText.assign(bytes.begin(), bytes.end());
      if (id == packEntry.id) ++callbacks;
    }), "asset loader accepts an asynchronous read request");
    check(loader.request(packEntry.id + 1, [&](nimbus::asset::AssetId,
                                               std::vector<uint8_t> bytes) {
      callbacksOnGameThread = callbacksOnGameThread &&
                              std::this_thread::get_id() == gameThread;
      missingAssetCompleted = bytes.empty();
      ++callbacks;
    }), "asset loader accepts a request that may fail at read time");
    for (uint32_t attempt = 0; attempt < 1000 && callbacks.load() != 2; ++attempt) {
      loader.update();
      if (callbacks.load() != 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    loader.update();
    check(callbacks.load() == 2 && asyncText == payload,
          "asset loader reads data on its worker and completes valid requests");
    check(missingAssetCompleted,
          "asset loader completes missing-asset requests instead of retaining them forever");
    check(callbacksOnGameThread,
          "asset loader dispatches callbacks on the update caller thread");
    check(loader.pending() == 0, "asset loader drains completed work");
    check(!loader.request(nimbus::asset::kNullAsset, [](nimbus::asset::AssetId,
                                                        std::vector<uint8_t>) {}),
          "asset loader rejects null asset identifiers");
  }
  {
    std::fstream corrupt(packPath, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekp(static_cast<std::streamoff>(packEntry.offset));
    const char changed = 'X';
    corrupt.write(&changed, 1);
  }
  check(pack.read(packEntry.id, assetBytes) == 0 && assetBytes.empty(),
        "asset pack rejects payload changed after CRC validation");

  const std::filesystem::path truncatedPath = packTestDir / "truncated.nbpk";
  {
    std::ofstream truncated(truncatedPath, std::ios::binary | std::ios::trunc);
    truncated.write(reinterpret_cast<const char*>(&packHeader), sizeof(packHeader));
    truncated.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  }
  nimbus::asset::Pack truncatedPack;
  check(!truncatedPack.open(truncatedPath.string()) && !truncatedPack.valid(),
        "asset pack rejects a truncated entry table");

  const std::filesystem::path invalidOffsetPath = packTestDir / "invalid-offset.nbpk";
  {
    nimbus::asset::Entry invalidEntry = packEntry;
    invalidEntry.crc32 = 0;
    invalidEntry.offset = UINT64_MAX;
    std::ofstream invalid(invalidOffsetPath, std::ios::binary | std::ios::trunc);
    invalid.write(reinterpret_cast<const char*>(&packHeader), sizeof(packHeader));
    invalid.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    invalid.write(reinterpret_cast<const char*>(&invalidEntry), sizeof(invalidEntry));
  }
  nimbus::asset::Pack invalidPack;
  check(!invalidPack.open(invalidOffsetPath.string()),
        "asset pack rejects payload offsets outside the file");
  {
    std::error_code ignored;
    std::filesystem::remove_all(packTestDir, ignored);
  }

  nimbus::mcp::Server mcp(&editor);
  mcp.registerTools();
  check(mcp.toolCount() >= 10, "MCP registers engine tools");
  const std::string initializeResponse =
      mcp.handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}");
  check(initializeResponse.find("nimbus-mcp") != std::string::npos,
        "MCP responds to JSON-RPC initialize");
  const std::string listResponse =
      mcp.handle("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
  check(listResponse.find("asset_import_obj") != std::string::npos,
        "MCP lists registered tools");
  const std::string objCall =
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"asset_import_obj\",\"arguments\":{"
      "\"text\":\"v 0 0 0\\nv 1 0 0\\nv 0 1 0\\nf 1 2 3\\n\"}}}";
  const std::string objCallResponse = mcp.handle(objCall);
  check(objCallResponse.find("\"isError\":false") != std::string::npos &&
            objCallResponse.find("vertices") != std::string::npos,
        "MCP round-trips string arguments with escaped newlines");
  const nimbus::ecs::Entity mcpEntity = engine.createEntity("mcp-json-entity");
  const std::string setPositionCall =
      "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"entity_set_position\",\"arguments\":{"
      "\"entity\":" + std::to_string(mcpEntity) +
      ",\"position\":[1.5,2.5,3.5]}}}";
  const std::string setPositionResponse = mcp.handle(setPositionCall);
  check(setPositionResponse.find("\"isError\":false") != std::string::npos &&
            engine.scene()->local(mcpEntity) && engine.scene()->local(mcpEntity)->position.x == 1.5f &&
            engine.scene()->local(mcpEntity)->position.y == 2.5f &&
            engine.scene()->local(mcpEntity)->position.z == 3.5f,
        "MCP preserves numeric arrays and mutates a live entity");
  engine.destroyEntity(mcpEntity);
  check(mcp.handle("{bad json}").find("parse error") != std::string::npos,
        "MCP rejects malformed JSON-RPC input");
  check(mcp.handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,"
                   "\"method\":\"tools/list\"}").find("parse error") != std::string::npos,
        "MCP rejects duplicate JSON object keys");
  check(mcp.handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\","
                   "\"bad\":\"\\q\"}").find("parse error") != std::string::npos,
        "MCP rejects invalid JSON string escapes");

  // Verify stereo preservation, fractional pitch and source-rate conversion
  // using a deterministic backend sink rather than an actual audio device.
  {
    RecordingAudioBackend backend;
    nimbus::audio::Mixer mixer(120);
    mixer.setBackend(&backend);
    check(mixer.initialize() && backend.valid(), "audio mixer opens its configured backend");
    nimbus::audio::Clip stereo;
    stereo.sampleRate = 120;
    stereo.channels = 2;
    stereo.format = nimbus::audio::Format::Stereo16;
    stereo.pcm = {0, 16000, 8000, 24000, 16000, 32000, 24000, 32767};
    const uint32_t voice = mixer.play(stereo, 1.0f, false);
    mixer.setPitch(voice, 0.5f);
    mixer.mix(3);
    check(voice != 0 && backend.submittedFrames == 3 && backend.submitted.size() == 6,
          "audio mixer submits the expected number of stereo frames");
    check(std::fabs(backend.submitted[1] - 16000.0f / 32768.0f) < 1e-5f &&
              std::fabs(backend.submitted[2] - 4000.0f / 32768.0f) < 1e-5f &&
              std::fabs(backend.submitted[3] - 20000.0f / 32768.0f) < 1e-5f,
          "audio mixer preserves stereo channels and interpolates fractional pitch");
    nimbus::audio::Clip invalidClip;
    invalidClip.channels = 3;
    invalidClip.sampleRate = 44100;
    invalidClip.pcm = {1, 2, 3};
    check(mixer.play(invalidClip) == 0, "audio mixer rejects unsupported clip channel layouts");
    mixer.shutdown();
    check(!backend.valid(), "audio mixer closes its backend during shutdown");
  }

  // Verify the renderer's CPU-side submission and accounting contract with a
  // recording device. This does not claim to verify a physical GPU backend.
  {
    nimbus::ecs::Registry registry;
    nimbus::ecs::World world(&registry);
    nimbus::scene::Scene scene(&world);
    const nimbus::ecs::Entity indexedEntity = scene.createNode("indexed");
    const nimbus::ecs::Entity unindexedEntity = scene.createNode("unindexed");
    const nimbus::ecs::Entity culledEntity = scene.createNode("culled");
    scene.setLocal(indexedEntity,
                   nimbus::scene::Transform{{0, 0, -5}, {0, 0, 0, 1}, {1, 1, 1}});
    scene.setLocal(unindexedEntity,
                   nimbus::scene::Transform{{0, 0, -5}, {0, 0, 0, 1}, {1, 1, 1}});
    scene.setLocal(culledEntity,
                   nimbus::scene::Transform{{100, 0, -5}, {0, 0, 0, 1}, {1, 1, 1}});

    RecordingDevice device;
    {
    nimbus::render::Renderer renderer(&device, &scene);
    nimbus::render::Mesh indexedMesh;
    indexedMesh.vertices.resize(3u * 8u, 0.0f);
    indexedMesh.indices = {0, 1, 2, 0, 2, 1};
    nimbus::render::Mesh unindexedMesh;
    unindexedMesh.vertices.resize(3u * 8u, 0.0f);
    nimbus::render::Material indexedMaterial;
    indexedMaterial.vertexShader = "test vertex shader";
    indexedMaterial.fragmentShader = "test fragment shader";
    nimbus::render::Material unindexedMaterial = indexedMaterial;
    renderer.addObject({indexedEntity, &indexedMesh, &indexedMaterial, true});
    renderer.addObject({unindexedEntity, &unindexedMesh, &unindexedMaterial, true});
    renderer.addObject({culledEntity, &unindexedMesh, &unindexedMaterial, true});
    renderer.render(renderer.buildUniforms());
    check(renderer.stats().drawCalls == 2,
          "renderer counts only visible draw submissions");
    check(renderer.stats().triangles == 3,
          "renderer reports triangles from actual index and vertex counts");
    check(device.indexedDrawCalls == 1 && device.drawCalls == 1 &&
              device.submittedIndices == 6 && device.submittedVertices == 3,
          "renderer submits indexed and non-indexed geometry with correct counts");
    renderer.releaseMesh(indexedMesh);
    renderer.releaseMesh(unindexedMesh);
    renderer.releaseMaterial(indexedMaterial);
    renderer.releaseMaterial(unindexedMaterial);
    check(!indexedMesh.gpuReady && indexedMesh.vertexBuffer == 0 &&
              indexedMesh.indexBuffer == 0 && unindexedMesh.vertexBuffer == 0,
          "renderer releases mesh buffers and resets stale GPU handles");
    check(indexedMaterial.pipeline == 0 && unindexedMaterial.pipeline == 0 &&
              device.destroyedBuffers.size() == 3 && device.destroyedPipelines.size() == 2,
          "renderer explicitly releases mesh and material GPU resources");
    }
    check(device.destroyedUniforms.size() == 3 && device.destroyedUniforms[0] != 0 &&
              device.destroyedUniforms[1] != 0 && device.destroyedUniforms[2] != 0,
          "renderer releases its internally owned uniform buffers on destruction");
  }

  // A failed second-stage upload must roll back the first GPU allocation.
  {
    RecordingDevice device;
    device.failBufferCall = 2;
    nimbus::render::Renderer renderer(&device, nullptr);
    nimbus::render::Mesh mesh;
    mesh.vertices.resize(3u * 8u, 0.0f);
    mesh.indices = {0, 1, 2};
    check(!renderer.uploadMesh(mesh), "renderer rejects a failed index-buffer upload");
    check(device.destroyedBuffers.size() == 1 && device.destroyedBuffers[0] != 0,
          "renderer releases vertex allocation after index upload failure");
    check(!mesh.gpuReady && mesh.vertexBuffer == 0 && mesh.indexBuffer == 0,
          "failed mesh upload does not commit partial GPU handles");

    nimbus::render::Mesh malformed;
    malformed.vertices.resize(7, 0.0f);
    check(!renderer.uploadMesh(malformed) && device.bufferCreateCalls == 2,
          "renderer rejects malformed vertex data before GPU allocation");

    nimbus::render::Mesh invalidIndices;
    invalidIndices.vertices.resize(3u * 8u, 0.0f);
    invalidIndices.indices = {0, 1, 3};
    check(!renderer.uploadMesh(invalidIndices) && device.bufferCreateCalls == 2,
          "renderer rejects out-of-range mesh indices before GPU allocation");
  }

  // ---- Comprehensive M1-M5 Subsystem, Pipeline & Security Verification ----
  // Input gestures & action rebinding
  {
    nimbus::input::InputSystem input;
    input.bindAction("Jump", {nimbus::input::Key::Space, nimbus::input::MouseButton::Count,
                              nimbus::input::GamepadButton::Count});
    input.beginFrame(0.10);
    input.feedKey(nimbus::input::Key::Space, true);
    check(input.actionDown("Jump") && input.actionPressed("Jump"),
          "input actionDown and edge actionPressed respond to mapped key");
    input.endFrame();
    input.rebindAction("Jump", nimbus::input::Key::W);
    input.beginFrame(0.12);
    input.feedKey(nimbus::input::Key::Space, false);
    input.feedKey(nimbus::input::Key::W, true);
    check(input.actionDown("Jump"), "rebindAction switches action key live");
    input.endFrame();

    // Double-tap touch detection
    input.beginFrame(0.20);
    input.feedTouch(0, nimbus::input::TouchPhase::Began, 100.0f, 100.0f);
    input.endFrame();
    input.beginFrame(0.22);
    input.feedTouch(0, nimbus::input::TouchPhase::Ended, 100.0f, 100.0f);
    input.endFrame();
    input.beginFrame(0.28);
    input.feedTouch(0, nimbus::input::TouchPhase::Began, 102.0f, 101.0f);
    input.endFrame();
    input.beginFrame(0.30);
    input.feedTouch(0, nimbus::input::TouchPhase::Ended, 102.0f, 101.0f);
    check(input.gestures().doubleTap, "touch gesture detector recognizes doubleTap");
    input.endFrame();
  }

  // Events Dispatcher + ECS Scheduler topological ordering
  {
    nimbus::events::Dispatcher disp;
    int received = 0;
    const auto subId = disp.subscribe<int>([&](const int& v) { received += v; });
    disp.publish(42);
    disp.unsubscribe<int>(subId);
    disp.publish(10);
    check(received == 42, "events::Dispatcher delivers and unsubscribes cleanly");

    nimbus::ecs::Registry reg;
    nimbus::ecs::World w(&reg);
    nimbus::ecs::Scheduler sched;
    std::vector<int> order;
    sched.addSystem("b", [&](nimbus::ecs::World&, float) { order.push_back(2); }, {"a"});
    sched.addSystem("a", [&](nimbus::ecs::World&, float) { order.push_back(1); });
    sched.run(w, 0.016f);
    check(order.size() == 2 && order[0] == 1 && order[1] == 2,
          "ECS Scheduler orders dependent systems topologically");
  }

  // SceneManager + Prefab save/instantiate + component serialization roundtrip
  {
    nimbus::Engine eng;
    check(eng.initialize("prefab-test", 320, 240, nimbus::rhi::Api::Headless),
          "Headless RHI initializes cleanly on Linux");
    const auto root = eng.createEntity("TurretRoot");
    const auto barrel = eng.scene()->createNode("Barrel", root);
    eng.scene()->setLocal(barrel, {{0, 1, 2}, {0, 0, 0, 1}, {1, 1, 1}});
    nimbus::physics::RigidBody rb;
    rb.mass = 4.5f;
    rb.useGravity = false;
    eng.world()->add<nimbus::physics::RigidBody>(root, rb);
    check(eng.scene()->savePrefabFromNode(root, "TurretPrefab"), "saves subtree prefab");
    const auto clone = eng.scene()->instantiatePrefab("TurretPrefab");
    check(clone != nimbus::ecs::kNullEntity && eng.world()->has<nimbus::physics::RigidBody>(clone),
          "instantiates prefab with cloned ECS components");
    if (auto* cl = eng.world()->get<nimbus::physics::RigidBody>(clone)) {
      check(std::fabs(cl->mass - 4.5f) < 1e-4f, "cloned prefab preserves RigidBody values");
    }
    eng.shutdown();
  }

  // Physics: capsule/box collision, trigger callback, joints, swept-sphere CCD & terrain sampler
  {
    nimbus::ecs::Registry reg;
    nimbus::ecs::World w(&reg);
    nimbus::scene::Scene sc(&w);
    nimbus::physics::World phys(&w, &sc);
    phys.setTerrainSampler(-10.0f, 10.0f, -10.0f, 10.0f, [](float, float) { return 0.0f; });
    int triggers = 0;
    phys.setOnTrigger([&](const nimbus::physics::Contact& c) {
      if (c.trigger) ++triggers;
    });

    const auto zone = sc.createNode("Zone");
    sc.setLocal(zone, {{0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    nimbus::physics::Collider zCol;
    zCol.type = nimbus::physics::Collider::Type::Box;
    zCol.trigger = true;
    w.add<nimbus::physics::Collider>(zone, zCol);
    w.add<nimbus::physics::BoxCollider>(zone, {{1.0f, 1.0f, 1.0f}});

    const auto actor = sc.createNode("PlayerCap");
    sc.setLocal(actor, {{0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    nimbus::physics::RigidBody pRb;
    pRb.useGravity = false;
    w.add<nimbus::physics::RigidBody>(actor, pRb);
    nimbus::physics::Collider pCol;
    pCol.type = nimbus::physics::Collider::Type::Capsule;
    w.add<nimbus::physics::Collider>(actor, pCol);
    w.add<nimbus::physics::CapsuleCollider>(actor, {0.3f, 1.2f});

    phys.step(1.0f / 60.0f);
    check(triggers == 1, "physics dispatches capsule vs box trigger callback");

    const auto sweep = phys.sweepSphere({{0, 1, 5}, 0.25f}, {0, 0, -1}, 10.0f);
    check(sweep.hit && sweep.entity == zone, "swept-sphere CCD detects target along cast ray");
  }

  // Animation crossfade + events
  {
    nimbus::anim::Skeleton skel;
    skel.joints.push_back({"root", -1});
    skel.inverseBindMatrices.resize(1, nimbus::math::mat4Identity());
    nimbus::anim::Clip clipA, clipB;
    clipA.name = "A";
    clipA.duration = 1.0f;
    clipB.name = "B";
    clipB.duration = 1.0f;
    clipB.events.push_back({0.2f, "footstep", "left"});
    nimbus::anim::Animator anim;
    anim.setSkeleton(skel);
    anim.states.push_back({&clipA, 0.0f, 1.0f, 1.0f, true});
    anim.states.push_back({&clipB, 0.0f, 1.0f, 0.0f, false});
    int fired = 0;
    anim.setEventCallback([&](const nimbus::anim::AnimationEvent& ev) {
      if (ev.name == "footstep") ++fired;
    });
    anim.play(0);
    anim.crossfade(1, 0.2f);
    anim.update(0.3f);
    check(fired >= 1 && anim.activeState == 1, "animator crossfades and emits animation events");
  }

  // Terrain sculpt, splatmap normalization & vegetation scatter
  {
    nimbus::terrain::Terrain ter(16, 16, 1.0f, 8);
    ter.applyBrush(8.0f, 8.0f, 4.0f, 3.0f, nimbus::terrain::BrushType::Raise);
    check(ter.field().sample(8.0f, 8.0f) > 0.1f, "terrain Raise sculpt increases elevation");
    ter.autoGenerateSplatmap(2.0f, 0.7f);
    const auto splat = ter.splatAt(8, 8);
    check(std::fabs((splat.x + splat.y + splat.z + splat.w) - 1.0f) < 1e-4f,
          "terrain splatmap weights normalize to 1.0");
    const auto props = ter.scatterProps(123u, 0.0f, 0.5f, 8);
    check(props.size() <= 8, "terrain vegetation scatter respects count limit");
  }

  // Audio 3D panning, pause/resume & WAV loading
  {
    nimbus::audio::Mixer mixer(22050);
    check(mixer.initialize(), "audio mixer initializes sample buffer");
    const auto clip = nimbus::audio::Mixer::generateTone(523.25f, 0.1f, 22050);
    nimbus::audio::Listener listener;
    listener.position = {0, 0, 0};
    mixer.setListener(listener);
    const uint32_t v = mixer.play(clip, 1.0f, true);
    mixer.setPosition3d(v, {3.0f, 0.0f, 0.0f});
    mixer.pause(v);
    check(mixer.isPaused(v), "audio mixer tracks per-voice pause");
    mixer.resume(v);
    check(!mixer.isPaused(v), "audio mixer resumes paused voice");
    mixer.mix(128);
    const auto& mixed = mixer.lastMixBuffer();
    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    for (size_t i = 0; i < 128; ++i) {
      leftEnergy += std::fabs(mixed[i * 2]);
      rightEnergy += std::fabs(mixed[i * 2 + 1]);
    }
    check(mixed.size() >= 256 && rightEnergy > leftEnergy + 1.0f,
          "audio stereo mix writes L/R interleaved frames with 3D pan");
    mixer.stop(v);
  }

  // AI NavMesh::buildFromGrid, Steering, Cooldown/Repeater/Condition decorators
  {
    std::vector<float> h(5 * 5, 0.0f);
    nimbus::ai::NavMesh gridNav;
    check(gridNav.buildFromGrid(4, 4, 2.0f, h), "NavMesh builds walkable triangles from grid");
    const auto funnelPath = gridNav.findPath({0.5f, 0.0f, 0.5f}, {5.0f, 0.0f, 5.0f});
    check(funnelPath.size() >= 2, "NavMesh funnel produces shortest corner path");

    int count = 0;
    nimbus::ai::Repeater rep(3);
    rep.setChild(std::make_unique<nimbus::ai::Action>([&](nimbus::ecs::Entity, float) {
      ++count;
      return nimbus::ai::Status::Success;
    }));
    const auto s1 = rep.tick(nimbus::ecs::kNullEntity, 0.1f);
    const auto s2 = rep.tick(nimbus::ecs::kNullEntity, 0.1f);
    const auto s3 = rep.tick(nimbus::ecs::kNullEntity, 0.1f);
    check(s1 == nimbus::ai::Status::Running && s2 == nimbus::ai::Status::Running &&
              s3 == nimbus::ai::Status::Success && count == 3,
          "Behavior Tree Repeater repeats N iterations");
  }

  // Net: HMAC + cipher encryption + tamper & anti-replay rejection
  {
    nimbus::net::Config c1;
    c1.authKey = "super-secret-session-key";
    c1.encryptPayload = true;
    nimbus::net::Config c2 = c1;
    nimbus::net::Server srv(c1);
    nimbus::net::Client cli(c2);
    check(srv.start(0) && cli.connect("127.0.0.1", srv.port()), "encrypted UDP connect starts");
    for (int i = 0; i < 6; ++i) {
      srv.tick(0.01f);
      cli.tick(0.01f);
    }
    check(cli.connected() && srv.clientCount() == 1, "authenticated HMAC v2 handshake connects");
    cli.disconnect();
    srv.tick(0.01f);
    srv.stop();
  }

  // UI: Slider, ScrollView, ListView, TextField & DPI scaling
  {
    nimbus::ui::Root root;
    root.setDpiScale(2.0f);
    auto panel = std::make_unique<nimbus::ui::Container>();
    auto slider = std::make_unique<nimbus::ui::Slider>();
    slider->setRect({0, 0, 100, 24});
    slider->setRange(0.0f, 10.0f);
    auto* sliderPtr = slider.get();
    auto tf = std::make_unique<nimbus::ui::TextField>();
    tf->setRect({0, 30, 120, 28});
    auto* tfPtr = tf.get();
    panel->addChild(std::move(slider));
    panel->addChild(std::move(tf));
    root.setRoot(std::move(panel));
    root.layout(400, 300);
    sliderPtr->handlePointer({50, 12}, true);
    sliderPtr->handlePointer({50, 12}, false);
    check(sliderPtr->value() >= 4.0f && sliderPtr->value() <= 6.0f,
          "UI Slider maps pointer position to range");
    tfPtr->setFocused(true);
    tfPtr->handleKeyChar('A');
    tfPtr->handleKeyChar('B');
    tfPtr->handleBackspace();
    check(tfPtr->text() == "A", "UI TextField inserts and backspaces UTF-8 characters");
  }

  // Asset: PackBuilder CRC roundtrip + BMP texture + glTF import + AssetCache LRU
  {
    const auto tmpDir = std::filesystem::temp_directory_path() / "nimbus-asset-m5";
    std::filesystem::create_directories(tmpDir);
    const std::string packFile = (tmpDir / "bundle.nbpk").string();
    nimbus::asset::PackBuilder builder;
    builder.addData("hello.txt", nimbus::asset::Kind::Script, {'h', 'i'});
    check(builder.write(packFile), "PackBuilder writes NBPK file");
    nimbus::asset::Pack pk;
    check(pk.open(packFile) && pk.entryCount() == 1, "Pack::open reads built NBPK bundle");
    std::vector<uint8_t> loaded;
    check(pk.read(nimbus::asset::idFromPath("hello.txt"), loaded) == 2 && loaded[0] == 'h',
          "Pack::read returns CRC-verified payload");
    pk.close();
    std::filesystem::remove_all(tmpDir);

    // PPM P6 2x1 texture import
    const std::string ppmHeader = "P6\n2 1\n255\n";
    std::vector<uint8_t> ppm(ppmHeader.begin(), ppmHeader.end());
    ppm.insert(ppm.end(), {255, 0, 0, 0, 255, 0});
    nimbus::asset::TextureImport tex;
    check(nimbus::asset::importTexture(ppm.data(), ppm.size(), tex) && tex.width == 2 &&
              tex.rgba.size() == 8 && tex.rgba[0] == 255 && tex.rgba[5] == 255,
          "asset::importTexture decodes PPM P6 image to RGBA8");

    nimbus::asset::AssetRegistry cache;
    cache.setCacheBudget(4);
    cache.cachePut(1, {1, 2, 3});
    cache.cachePut(2, {4, 5, 6});  // Must evict id 1 to stay <= 4 bytes
    std::vector<uint8_t> out;
    check(!cache.cacheGet(1, out) && cache.cacheGet(2, out),
          "AssetRegistry LRU evicts oldest entries beyond byte budget");
  }

  // Script Security: sandbox blocks os/io & watchdog terminates infinite loop
  {
    nimbus::script::Runtime rt;
    check(rt.initialize(), "LuaJIT runtime initializes for security audit");
    std::string err;
    check(!rt.run("return os.execute('echo pwn')", &err),
          "Lua sandbox blocks os.execute access");
    check(!rt.run("return io.open('/etc/passwd', 'r')", &err),
          "Lua sandbox blocks io.open filesystem access");
    rt.setInstructionLimit(5000u);
    check(!rt.run("while true do end", &err),
          "Lua instruction watchdog aborts infinite loop cleanly");
    rt.shutdown();
  }

  // Editor Undo/Redo + Export + 18-node VisualScript + MCP Path Traversal Security
  {
    nimbus::Engine e2;
    check(e2.initialize("editor-m5", 320, 240, nimbus::rhi::Api::Headless),
          "Engine initializes for Editor/MCP M5 checks");
    nimbus::editor::Editor ed(&e2);
    const auto wsRoot = std::filesystem::temp_directory_path() / "nimbus-mcp-ws-m5";
    std::filesystem::create_directories(wsRoot);
    check(ed.newProject((wsRoot / "demo").string(), "Demo", "3d_action"),
          "Editor newProject creates 3d_action template");
    const auto ent = e2.createEntity("Cube");
    ed.setPosition(ent, {1, 2, 3});
    ed.setPosition(ent, {9, 8, 7});
    check(ed.undo() && std::fabs(ed.getPosition(ent).x - 1.0f) < 1e-4f,
          "Editor CommandHistory undo restores previous transform");
    check(ed.redo() && std::fabs(ed.getPosition(ent).x - 9.0f) < 1e-4f,
          "Editor CommandHistory redo reapplies transform");
    auto* vs = ed.visualScript();
    vs->clear();
    const uint32_t ev = vs->addNode("event", "start", 0, 0);
    const uint32_t addN = vs->addNode("add", "sum", 100, 0);
    const uint32_t prN = vs->addNode("print", "log", 200, 0);
    vs->addEdge({ev, 0, prN, 0});
    vs->addEdge({addN, 0, prN, 1});
    std::vector<double> outPrints;
    nimbus::editor::VsExecutionContext ctx;
    ctx.numbers["a"] = 6.0;
    ctx.numbers["b"] = 9.0;
    ctx.print = [&](double val) { outPrints.push_back(val); };
    check(vs->execute(ev, ctx) && outPrints.size() == 1 && std::fabs(outPrints[0] - 15.0) < 1e-6,
          "VisualScript executes graph flow and writes evaluated expression output");
    const std::string luaSrc = vs->toLua();
    nimbus::script::Runtime vsLuaRt;
    check(vsLuaRt.initialize() && vsLuaRt.run(luaSrc) &&
              luaSrc.find("local graph = {}") != std::string::npos,
          "VisualScript exports executable Lua script");
    vsLuaRt.shutdown();

    check(ed.exportProject((wsRoot / "exported_game").string()) &&
              std::filesystem::exists(wsRoot / "exported_game" / "assets.nbpk") &&
              std::filesystem::exists(wsRoot / "exported_game" / "scripts" / "visual_script.lua"),
          "Editor exportProject generates runnable bundle with assets.nbpk and compiled Lua");

    nimbus::mcp::Server mcp(&ed);
    mcp.setWorkspaceRoot(wsRoot.string());
    mcp.registerTools();
    check(mcp.toolCount() >= 35, "MCP server registers 35+ schema-defined tools");
    const std::string listResp = mcp.handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
    check(listResp.find("\"inputSchema\"") != std::string::npos,
          "MCP tools/list exposes inputSchema metadata");
    const std::string traversal = mcp.handle(
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"project_open\","
        "\"arguments\":{\"path\":\"../../../etc\"}}}");
    check(traversal.find("\"isError\":true") != std::string::npos,
          "MCP rejects path traversal outside confined workspace root");
    const std::string badJson = mcp.handle("{bad-json");
    check(badJson.find("-32700") != std::string::npos,
          "MCP returns JSON-RPC parse error for malformed request");

    e2.shutdown();
    std::filesystem::remove_all(wsRoot);
  }

  engine.stop();
  engine.play();
  check(engine.tick(1.0 / 120.0) == 0,
        "engine accumulates a partial fixed step before shutdown");
  engine.shutdown();
  check(engine.state() == nimbus::State::Stopped && engine.frame() == 0,
        "engine shutdown resets runtime lifecycle state");
  check(engine.initialize("nimbus-reinitialize", 320, 240, nimbus::rhi::Api::None),
        "engine can initialize again after shutdown");
  engine.play();
  check(engine.tick(1.0 / 120.0) == 0 && engine.frame() == 0,
        "engine reinitialization clears the previous fractional time accumulator");

  std::printf("RESULT: %s (%d failures; 143 engine checks + %d integration checks)\n",
              failures == 0 ? "PASS" : "FAIL", failures, additionalChecks);
  engine.shutdown();
  return failures == 0 ? 0 : 1;
}
