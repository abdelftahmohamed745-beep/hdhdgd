// Nimbus engine: top-level coordinator tying every system together.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nimbus/ai.hpp"
#include "nimbus/anim.hpp"
#include "nimbus/asset.hpp"
#include "nimbus/audio.hpp"
#include "nimbus/ecs.hpp"
#include "nimbus/events.hpp"
#include "nimbus/input.hpp"
#include "nimbus/jobs.hpp"
#include "nimbus/math.hpp"
#include "nimbus/net.hpp"
#include "nimbus/particles.hpp"
#include "nimbus/physics.hpp"
#include "nimbus/render.hpp"
#include "nimbus/rhi.hpp"
#include "nimbus/scene.hpp"
#include "nimbus/script.hpp"
#include "nimbus/terrain.hpp"
#include "nimbus/ui.hpp"

namespace nimbus {

enum class State : uint8_t { Stopped, Playing, Paused };

struct EngineStateChangedEvent {
  State previous = State::Stopped;
  State current = State::Stopped;
};

struct EntityLifecycleEvent {
  ecs::Entity entity = ecs::kNullEntity;
  bool created = true;
};

class Engine {
 public:
  Engine();
  ~Engine();

  bool initialize(const std::string& name, uint32_t width, uint32_t height,
                  rhi::Api preferredApi = rhi::Api::None, void* nativeWindow = nullptr);
  void shutdown();

  // Loads a project directory: assets, scene, prefabs, and scripts.
  bool loadProject(const std::string& path);
  void play();
  void pause();
  void stop();

  // Advances the fixed-step simulation from wall-clock time and returns the
  // number of steps executed (capped to avoid an unbounded catch-up spiral).
  uint32_t tick(double frameDeltaSeconds);
  void step();
  void renderFrame();
  void resize(uint32_t width, uint32_t height);

  State state() const { return state_; }
  bool running() const { return state_ != State::Stopped; }

  ecs::World* world() { return world_.get(); }
  ecs::Scheduler* scheduler() { return scheduler_.get(); }
  events::Dispatcher* events() { return events_.get(); }
  input::InputSystem* input() { return input_.get(); }
  scene::Scene* scene() { return scene_.get(); }
  scene::SceneManager* sceneManager() { return sceneManager_.get(); }
  render::Renderer* renderer() { return renderer_.get(); }
  rhi::Device* device() { return device_; }
  physics::World* physics() { return physics_.get(); }
  script::Runtime* scripts() { return scripts_.get(); }
  audio::Mixer* audio() { return audio_.get(); }
  particles::System* particles() { return particles_.get(); }
  terrain::Terrain* terrain() { return terrain_.get(); }
  ui::Root* ui() { return ui_.get(); }
  asset::Pack* pack() { return pack_.get(); }
  jobs::JobSystem* jobs() { return jobs_.get(); }
  net::Server* networkServer() { return networkServer_.get(); }
  net::Client* networkClient() { return networkClient_.get(); }

  const time::FrameStats& stats() const { return stats_; }
  uint32_t frame() const { return frame_; }
  double elapsed() const { return elapsed_; }
  uint32_t viewportWidth() const { return width_; }
  uint32_t viewportHeight() const { return height_; }

  int runTests();

  ecs::Entity createEntity(const std::string& name);
  void destroyEntity(ecs::Entity entity);

 private:
  void registerBuiltinSerializers();
  void registerBuiltinBindings();
  void updateSystems(float deltaSeconds);

  State state_ = State::Stopped;
  std::unique_ptr<events::Dispatcher> events_;
  std::unique_ptr<input::InputSystem> input_;
  std::unique_ptr<jobs::JobSystem> jobs_;
  std::unique_ptr<ecs::Registry> registry_;
  std::unique_ptr<ecs::World> world_;
  std::unique_ptr<ecs::Scheduler> scheduler_;
  std::unique_ptr<scene::SceneManager> sceneManager_;
  std::unique_ptr<scene::Scene> scene_;
  std::unique_ptr<render::Renderer> renderer_;
  std::unique_ptr<physics::World> physics_;
  std::unique_ptr<script::Runtime> scripts_;
  std::unique_ptr<audio::Mixer> audio_;
  std::unique_ptr<particles::System> particles_;
  std::unique_ptr<terrain::Terrain> terrain_;
  std::unique_ptr<ui::Root> ui_;
  std::unique_ptr<asset::Pack> pack_;
  std::unique_ptr<net::Server> networkServer_;
  std::unique_ptr<net::Client> networkClient_;
  rhi::Device* device_ = nullptr;
  time::FixedStep stepper_;
  time::FrameStats stats_;
  uint32_t frame_ = 0;
  double elapsed_ = 0.0;
  uint32_t width_ = 1280;
  uint32_t height_ = 720;
};

}  // namespace nimbus
