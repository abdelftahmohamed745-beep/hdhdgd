#include "nimbus/engine.hpp"

#include "nimbus/log.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace nimbus {

namespace {
bool extractJsonNumberField(const std::string& json, const char* key, double& out) {
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  const char* start = json.c_str() + pos + needle.size();
  while (*start == ' ' || *start == '\t') ++start;
  char* end = nullptr;
  const double v = std::strtod(start, &end);
  if (!end || end == start || !std::isfinite(v)) return false;
  out = v;
  return true;
}

bool extractJsonBoolField(const std::string& json, const char* key, bool& out) {
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t valPos = pos + needle.size();
  while (valPos < json.size() && (json[valPos] == ' ' || json[valPos] == '\t')) ++valPos;
  if (json.compare(valPos, 4, "true") == 0) { out = true; return true; }
  if (json.compare(valPos, 5, "false") == 0) { out = false; return true; }
  return false;
}

bool extractJsonVec3Field(const std::string& json, const char* key, math::Vec3& out) {
  const std::string needle = std::string("\"") + key + "\":[";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  const char* p = json.c_str() + pos + needle.size();
  float vals[3]{};
  for (int i = 0; i < 3; ++i) {
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (!end || end == p || !std::isfinite(v)) return false;
    vals[i] = static_cast<float>(v);
    p = end;
    while (*p == ' ' || *p == ',') ++p;
  }
  out = {vals[0], vals[1], vals[2]};
  return true;
}
}  // namespace

Engine::Engine() = default;
Engine::~Engine() { shutdown(); }

void Engine::registerBuiltinSerializers() {
  if (!world_) return;
  world_->registerSerializer({
      "RigidBody",
      [](const ecs::World& w, ecs::Entity e) { return w.has<physics::RigidBody>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* b = w.get<physics::RigidBody>(e);
        if (!b) return {};
        std::ostringstream o;
        o << "{\"mass\":" << b->mass << ",\"restitution\":" << b->restitution
          << ",\"friction\":" << b->friction
          << ",\"kinematic\":" << (b->kinematic ? "true" : "false")
          << ",\"useGravity\":" << (b->useGravity ? "true" : "false") << '}';
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        physics::RigidBody b{};
        double m = 1.0, r = 0.2, f = 0.5;
        if (extractJsonNumberField(json, "mass", m)) b.mass = static_cast<float>(m);
        if (extractJsonNumberField(json, "restitution", r)) b.restitution = static_cast<float>(r);
        if (extractJsonNumberField(json, "friction", f)) b.friction = static_cast<float>(f);
        extractJsonBoolField(json, "kinematic", b.kinematic);
        extractJsonBoolField(json, "useGravity", b.useGravity);
        b.inverseMass = (!b.kinematic && b.mass > 0.0f) ? (1.0f / b.mass) : 0.0f;
        return w.add<physics::RigidBody>(e, b) != nullptr;
      },
  });

  world_->registerSerializer({
      "Collider",
      [](const ecs::World& w, ecs::Entity e) { return w.has<physics::Collider>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* c = w.get<physics::Collider>(e);
        if (!c) return {};
        std::ostringstream o;
        o << "{\"type\":" << static_cast<int>(c->type)
          << ",\"trigger\":" << (c->trigger ? "true" : "false")
          << ",\"offset\":[" << c->offset.x << ',' << c->offset.y << ',' << c->offset.z << "]}";
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        physics::Collider c{};
        double t = 0.0;
        if (extractJsonNumberField(json, "type", t))
          c.type = static_cast<physics::Collider::Type>(static_cast<int>(t));
        extractJsonBoolField(json, "trigger", c.trigger);
        extractJsonVec3Field(json, "offset", c.offset);
        return w.add<physics::Collider>(e, c) != nullptr;
      },
  });

  world_->registerSerializer({
      "BoxCollider",
      [](const ecs::World& w, ecs::Entity e) { return w.has<physics::BoxCollider>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* c = w.get<physics::BoxCollider>(e);
        if (!c) return {};
        std::ostringstream o;
        o << "{\"halfExtents\":[" << c->halfExtents.x << ',' << c->halfExtents.y << ','
          << c->halfExtents.z << "]}";
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        physics::BoxCollider c{};
        extractJsonVec3Field(json, "halfExtents", c.halfExtents);
        return w.add<physics::BoxCollider>(e, c) != nullptr;
      },
  });

  world_->registerSerializer({
      "SphereCollider",
      [](const ecs::World& w, ecs::Entity e) { return w.has<physics::SphereCollider>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* c = w.get<physics::SphereCollider>(e);
        if (!c) return {};
        std::ostringstream o;
        o << "{\"radius\":" << c->radius << '}';
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        physics::SphereCollider c{};
        double r = 0.5;
        if (extractJsonNumberField(json, "radius", r)) c.radius = static_cast<float>(r);
        return w.add<physics::SphereCollider>(e, c) != nullptr;
      },
  });

  world_->registerSerializer({
      "CapsuleCollider",
      [](const ecs::World& w, ecs::Entity e) { return w.has<physics::CapsuleCollider>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* c = w.get<physics::CapsuleCollider>(e);
        if (!c) return {};
        std::ostringstream o;
        o << "{\"radius\":" << c->radius << ",\"height\":" << c->height << '}';
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        physics::CapsuleCollider c{};
        double r = 0.25, h = 1.0;
        if (extractJsonNumberField(json, "radius", r)) c.radius = static_cast<float>(r);
        if (extractJsonNumberField(json, "height", h)) c.height = static_cast<float>(h);
        return w.add<physics::CapsuleCollider>(e, c) != nullptr;
      },
  });

  world_->registerSerializer({
      "Camera",
      [](const ecs::World& w, ecs::Entity e) { return w.has<render::Camera>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* c = w.get<render::Camera>(e);
        if (!c) return {};
        std::ostringstream o;
        o << "{\"fov\":" << c->fov << ",\"near\":" << c->nearPlane
          << ",\"far\":" << c->farPlane << ",\"aspect\":" << c->aspect << '}';
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        render::Camera c{};
        double v = 0.0;
        if (extractJsonNumberField(json, "fov", v)) c.fov = static_cast<float>(v);
        if (extractJsonNumberField(json, "near", v)) c.nearPlane = static_cast<float>(v);
        if (extractJsonNumberField(json, "far", v)) c.farPlane = static_cast<float>(v);
        if (extractJsonNumberField(json, "aspect", v)) c.aspect = static_cast<float>(v);
        return w.add<render::Camera>(e, c) != nullptr;
      },
  });

  world_->registerSerializer({
      "Light",
      [](const ecs::World& w, ecs::Entity e) { return w.has<render::Light>(e); },
      [](const ecs::World& w, ecs::Entity e) -> std::string {
        const auto* l = w.get<render::Light>(e);
        if (!l) return {};
        std::ostringstream o;
        o << "{\"type\":" << static_cast<int>(l->type)
          << ",\"intensity\":" << l->intensity << ",\"range\":" << l->range
          << ",\"color\":[" << l->color.x << ',' << l->color.y << ',' << l->color.z << "]}";
        return o.str();
      },
      [](ecs::World& w, ecs::Entity e, const std::string& json) {
        render::Light l{};
        double v = 0.0;
        if (extractJsonNumberField(json, "type", v))
          l.type = static_cast<render::Light::Type>(static_cast<int>(v));
        if (extractJsonNumberField(json, "intensity", v)) l.intensity = static_cast<float>(v);
        if (extractJsonNumberField(json, "range", v)) l.range = static_cast<float>(v);
        extractJsonVec3Field(json, "color", l.color);
        return w.add<render::Light>(e, l) != nullptr;
      },
  });
}

void Engine::registerBuiltinBindings() {
  if (!scripts_) return;
  scripts_->registerFunction("set_position",
      [this](const std::string& entityText, double x, double y, double z) {
        if (!scene_) return;
        try {
          size_t parsed = 0;
          const auto raw = std::stoull(entityText, &parsed, 10);
          if (parsed != entityText.size()) return;
          const ecs::Entity entity = static_cast<ecs::Entity>(raw);
          if (scene::Transform* transform = scene_->local(entity)) {
            transform->position = {static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(z)};
            scene_->markDirty(entity);
          }
        } catch (...) {
          log::write(log::Level::Warning,
                     "script: nimbus.set_position received invalid entity id");
        }
      });

  scripts_->registerFunction("create_entity",
      [this](const std::string& entityName) -> std::string {
        const ecs::Entity e = createEntity(entityName.empty() ? "ScriptEntity" : entityName);
        return e == ecs::kNullEntity ? std::string{} : std::to_string(e);
      });

  scripts_->registerFunction("destroy_entity",
      [this](const std::string& entityText) -> double {
        if (!scene_) return 0.0;
        try {
          size_t parsed = 0;
          const auto raw = std::stoull(entityText, &parsed, 10);
          if (parsed != entityText.size()) return 0.0;
          const ecs::Entity e = static_cast<ecs::Entity>(raw);
          if (!world_ || !world_->alive(e)) return 0.0;
          destroyEntity(e);
          return 1.0;
        } catch (...) {
          return 0.0;
        }
      });

  scripts_->registerFunction("get_position",
      [this](const std::string& entityText) -> std::string {
        if (!scene_) return {};
        try {
          size_t parsed = 0;
          const auto raw = std::stoull(entityText, &parsed, 10);
          if (parsed != entityText.size()) return {};
          const ecs::Entity e = static_cast<ecs::Entity>(raw);
          const scene::Transform* transform = scene_->local(e);
          if (!transform) return {};
          std::ostringstream o;
          o << transform->position.x << ',' << transform->position.y << ','
            << transform->position.z;
          return o.str();
        } catch (...) {
          return {};
        }
      });

  scripts_->registerFunction("action_down",
      [this](const std::string& actionName) -> double {
        if (!input_) return 0.0;
        return input_->actionDown(actionName) ? 1.0 : 0.0;
      });

  scripts_->registerFunction("axis",
      [this](const std::string& axisName) -> double {
        if (!input_) return 0.0;
        return static_cast<double>(input_->axisValue(axisName));
      });

  scripts_->registerFunction("emit_event",
      [this](const std::string& eventName) -> double {
        if (!events_ || eventName.empty()) return 0.0;
        events_->queue(eventName);
        return 1.0;
      });

  scripts_->registerFunction("entity_count",
      [this](const std::string&) -> double {
        return world_ ? static_cast<double>(world_->entityCount()) : 0.0;
      });
}

bool Engine::initialize(const std::string& name, uint32_t width, uint32_t height,
                        rhi::Api preferredApi, void* nativeWindow) {
  shutdown();
  stepper_.reset();
  stats_ = {};
  if (width == 0 || height == 0) {
    log::write(log::Level::Error, "engine: render dimensions must be non-zero");
    return false;
  }
  width_ = width;
  height_ = height;
  events_ = std::make_unique<events::Dispatcher>();
  input_ = std::make_unique<input::InputSystem>();
  jobs_ = std::make_unique<jobs::JobSystem>();
  networkServer_ = std::make_unique<net::Server>();
  networkClient_ = std::make_unique<net::Client>();
  registry_ = std::make_unique<ecs::Registry>();
  world_ = std::make_unique<ecs::World>(registry_.get());
  scheduler_ = std::make_unique<ecs::Scheduler>();
  sceneManager_ = std::make_unique<scene::SceneManager>();
  scene_ = std::make_unique<scene::Scene>(world_.get(), "main");
  physics_ = std::make_unique<physics::World>(world_.get(), scene_.get());
  scripts_ = std::make_unique<script::Runtime>();
  audio_ = std::make_unique<audio::Mixer>();
  particles_ = std::make_unique<particles::System>();
  terrain_ = std::make_unique<terrain::Terrain>(64, 64, 1.0f, 32);
  ui_ = std::make_unique<ui::Root>();
  pack_ = std::make_unique<asset::Pack>();

  registerBuiltinSerializers();

  if (!scripts_->initialize()) {
    log::write(log::Level::Warning,
               "engine: Lua unavailable; continuing with native gameplay only");
    scripts_.reset();
  }
  registerBuiltinBindings();
  if (!audio_->initialize()) {
    log::write(log::Level::Warning, "engine: audio unavailable, continuing without sound");
  }
  device_ = rhi::createDevice(preferredApi, nativeWindow, width, height);
  if (!device_) {
    log::write(log::Level::Warning, "engine: no render device, running headless");
  } else {
    renderer_ = std::make_unique<render::Renderer>(device_, scene_.get());
    renderer_->setScreenSize(width, height);
  }
  log::write(log::Level::Info, "engine: '%s' initialized (%ux%u, %u workers)", name.c_str(),
             width, height, jobs_->workerCount());
  return true;
}

void Engine::shutdown() {
  const bool hasResources = events_ || input_ || jobs_ || registry_ || world_ || scheduler_ ||
      sceneManager_ || scene_ || renderer_ || physics_ || scripts_ || audio_ || particles_ ||
      terrain_ || ui_ || pack_ || networkServer_ || networkClient_ || device_;
  if (!hasResources) {
    state_ = State::Stopped;
    frame_ = 0;
    elapsed_ = 0.0;
    stepper_.reset();
    stats_ = {};
    return;
  }
  renderer_.reset();
  if (device_) {
    delete device_;
    device_ = nullptr;
  }
  ui_.reset();
  particles_.reset();
  terrain_.reset();
  audio_.reset();
  scripts_.reset();
  physics_.reset();
  scene_.reset();
  sceneManager_.reset();
  scheduler_.reset();
  world_.reset();
  registry_.reset();
  pack_.reset();
  networkClient_.reset();
  networkServer_.reset();
  jobs_.reset();
  input_.reset();
  events_.reset();
  state_ = State::Stopped;
  frame_ = 0;
  elapsed_ = 0.0;
  stepper_.reset();
  stats_ = {};
  log::write(log::Level::Info, "engine: shutdown");
}

bool Engine::loadProject(const std::string& path) {
  if (!pack_ || path.empty()) return false;
  const std::filesystem::path root(path);
  const std::filesystem::path packPath = root / "assets.nbpk";
  std::error_code error;
  const bool exists = std::filesystem::exists(packPath, error);
  if (error) return false;
  if (exists) {
    if (!pack_->open(packPath.string())) return false;
    log::write(log::Level::Info, "engine: project pack loaded from %s", path.c_str());
  } else {
    pack_->close();
    log::write(log::Level::Info, "engine: project '%s' loaded without a pack", path.c_str());
  }

  // Also load scripts/*.lua if scripting runtime is active.
  const std::filesystem::path scriptsDir = root / "scripts";
  if (scripts_ && std::filesystem::is_directory(scriptsDir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, error)) {
      if (entry.is_regular_file() && entry.path().extension() == ".lua") {
        std::string scriptErr;
        if (!scripts_->runFile(entry.path().string(), &scriptErr)) {
          log::write(log::Level::Warning, "engine: project script '%s' error: %s",
                     entry.path().filename().string().c_str(), scriptErr.c_str());
        }
      }
    }
  }
  return true;
}

ecs::Entity Engine::createEntity(const std::string& name) {
  const ecs::Entity e = scene_ ? scene_->createNode(name) : ecs::kNullEntity;
  if (e != ecs::kNullEntity && events_) events_->publish(EntityLifecycleEvent{e, true});
  return e;
}

void Engine::destroyEntity(ecs::Entity entity) {
  if (!scene_) return;
  if (events_) events_->publish(EntityLifecycleEvent{entity, false});
  if (scripts_) scripts_->detach(entity);
  scene_->destroy(entity);
}

void Engine::play() {
  if (!world_ || !scene_ || !physics_ || !particles_) return;
  const State previous = state_;
  if (state_ == State::Stopped) stepper_.reset();
  state_ = State::Playing;
  if (events_ && previous != state_)
    events_->publish(EngineStateChangedEvent{previous, state_});
  log::write(log::Level::Info, "engine: play");
}

void Engine::pause() {
  if (state_ == State::Playing) {
    const State previous = state_;
    state_ = State::Paused;
    if (events_) events_->publish(EngineStateChangedEvent{previous, state_});
  }
}

void Engine::stop() {
  const State previous = state_;
  state_ = State::Stopped;
  stepper_.reset();
  frame_ = 0;
  elapsed_ = 0.0;
  if (input_) input_->reset();
  if (events_ && previous != state_)
    events_->publish(EngineStateChangedEvent{previous, state_});
}

void Engine::resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) return;
  width_ = width;
  height_ = height;
  if (device_) device_->resize(width, height);
  if (renderer_) renderer_->setScreenSize(width, height);
  if (ui_) ui_->layout(static_cast<float>(width), static_cast<float>(height));
}

void Engine::updateSystems(float deltaSeconds) {
  if (input_) input_->beginFrame(elapsed_);
  if (scheduler_ && world_) scheduler_->run(*world_, deltaSeconds);
  if (physics_) physics_->step(deltaSeconds);
  if (particles_) particles_->update(deltaSeconds);
  if (scripts_) scripts_->update(deltaSeconds);

  if (world_) {
    const std::vector<uint32_t> animatorArchetypes = world_->query<anim::Animator>();
    for (uint32_t archetypeId : animatorArchetypes) {
      const std::vector<ecs::Entity> entities = world_->archetype(archetypeId).entities;
      for (ecs::Entity entity : entities)
        if (anim::Animator* animator = world_->get<anim::Animator>(entity))
          animator->update(deltaSeconds);
    }
    const std::vector<uint32_t> agentArchetypes = world_->query<ai::Agent>();
    for (uint32_t archetypeId : agentArchetypes) {
      const std::vector<ecs::Entity> entities = world_->archetype(archetypeId).entities;
      for (ecs::Entity entity : entities)
        if (ai::Agent* agent = world_->get<ai::Agent>(entity))
          agent->update(entity, deltaSeconds);
    }
  }
  if (networkServer_) networkServer_->tick(deltaSeconds);
  if (networkClient_) networkClient_->tick(deltaSeconds);
  if (scene_) scene_->updateTransforms();
  if (audio_) audio_->mix(static_cast<uint32_t>(deltaSeconds * audio_->sampleRate()));
  if (events_) events_->dispatchQueued();
  if (input_) input_->endFrame();
}

uint32_t Engine::tick(double frameDeltaSeconds) {
  if (state_ != State::Playing) return 0;

  const double safeDelta = std::isfinite(frameDeltaSeconds)
                               ? std::max(0.0, frameDeltaSeconds) : 0.0;
  const auto start = std::chrono::steady_clock::now();
  stats_.frameTimeMs = static_cast<float>(safeDelta * 1000.0);
  stats_.framesPerSecond = safeDelta > 0.0 ? static_cast<float>(1.0 / safeDelta) : 0.0f;
  stats_.fps = stats_.framesPerSecond;

  uint32_t steps = 0;
  stepper_.advance(safeDelta);
  while (stepper_.consumeStep()) {
    const float fixedDelta = static_cast<float>(stepper_.stepSeconds());
    updateSystems(fixedDelta);
    elapsed_ += stepper_.stepSeconds();
    ++frame_;
    ++steps;
  }

  const auto finish = std::chrono::steady_clock::now();
  stats_.cpuTimeMs = std::chrono::duration<float, std::milli>(finish - start).count();
  stats_.memoryBytes = world_ ? world_->estimatedMemoryBytes() : 0;
  return steps;
}

void Engine::step() {
  if (state_ == State::Stopped || !world_ || !scene_ || !physics_ || !particles_) return;
  const auto start = std::chrono::steady_clock::now();
  const float fixedDelta = static_cast<float>(stepper_.stepSeconds());
  updateSystems(fixedDelta);
  elapsed_ += stepper_.stepSeconds();
  ++frame_;
  stepper_.reset();
  const auto finish = std::chrono::steady_clock::now();
  stats_.cpuTimeMs = std::chrono::duration<float, std::milli>(finish - start).count();
  stats_.memoryBytes = world_ ? world_->estimatedMemoryBytes() : 0;
}

void Engine::renderFrame() {
  if (!renderer_ || !device_) return;
  if (device_->isContextLost()) {
    if (!device_->recoverContext()) return;
  }

  // Sync ECS MeshComponent / Light entities when present.
  if (world_) {
    const auto meshArchetypes = world_->query<render::MeshComponent>();
    if (!meshArchetypes.empty()) {
      renderer_->clearObjects();
      for (uint32_t aid : meshArchetypes) {
        const auto& arch = world_->archetype(aid);
        for (ecs::Entity e : arch.entities) {
          const auto* mc = world_->get<render::MeshComponent>(e);
          if (mc && mc->visible && mc->mesh && mc->material) {
            renderer_->addObject({e, mc->mesh, mc->material, mc->castShadow});
          }
        }
      }
    }
    const auto lightArchetypes = world_->query<render::Light>();
    if (!lightArchetypes.empty()) {
      renderer_->clearLights();
      for (uint32_t aid : lightArchetypes) {
        const auto& arch = world_->archetype(aid);
        for (ecs::Entity e : arch.entities) {
          const auto* lightComp = world_->get<render::Light>(e);
          if (!lightComp) continue;
          render::Light l = *lightComp;
          if (scene_) {
            if (const auto* tf = scene_->local(e)) l.position = tf->position;
          }
          renderer_->addLight(l);
        }
      }
    }
  }

  renderer_->clearOverlays();
  if (particles_ && particles_->aliveCount() > 0) {
    const uint32_t count = particles_->aliveCount();
    std::vector<float> instData(static_cast<size_t>(count) * 8u);
    particles_->writeInstanceData(instData.data(), count);
    renderer_->submitParticles(instData.data(), count);
  }
  if (ui_) {
    std::vector<ui::DrawCommand> cmds;
    ui_->draw(cmds);
    for (const ui::DrawCommand& cmd : cmds) {
      render::UiQuad q;
      q.x = cmd.bounds.x;
      q.y = cmd.bounds.y;
      q.width = cmd.bounds.width;
      q.height = cmd.bounds.height;
      q.color = {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a};
      q.texture = cmd.texture;
      renderer_->submitUiQuad(q);
    }
  }

  render::FrameUniforms uniforms = renderer_->buildUniforms();
  uniforms.time = static_cast<float>(elapsed_);
  renderer_->render(uniforms);
  const time::FrameStats& rStats = renderer_->stats();
  stats_.drawCalls = rStats.drawCalls;
  stats_.triangles = rStats.triangles;
  stats_.gpuTimeMs = rStats.gpuTimeMs;
}

}  // namespace nimbus
