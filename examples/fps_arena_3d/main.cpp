#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "nimbus/audio.hpp"
#include "nimbus/engine.hpp"
#include "nimbus/particles.hpp"
#include "nimbus/physics.hpp"
#include "nimbus/render.hpp"
#include "nimbus/terrain.hpp"
#include "nimbus/ui.hpp"

int main() {
  nimbus::Engine engine;
  if (!engine.initialize("FPS Arena 3D Example", 800, 600, nimbus::rhi::Api::Headless)) {
    std::fprintf(stderr, "fps_arena_3d: engine initialize failed\n");
    return 1;
  }

  // Terrain floor + physics terrain sampler
  nimbus::terrain::Noise noise(7);
  engine.terrain()->generate(noise, 3, 1.5f);
  engine.physics()->setTerrainSampler(
      -32.0f, 32.0f, -32.0f, 32.0f,
      [&](float x, float z) { return engine.terrain()->field().sample(x + 32.0f, z + 32.0f); },
      [&](float x, float z) { return engine.terrain()->field().normal(x + 32.0f, z + 32.0f); });

  // Sun light & camera
  const nimbus::ecs::Entity lightEnt = engine.createEntity("ArenaSun");
  nimbus::render::Light sun;
  sun.type = nimbus::render::Light::Type::Directional;
  sun.intensity = 1.4f;
  engine.world()->add<nimbus::render::Light>(lightEnt, sun);

  nimbus::render::Camera cam;
  cam.position = {0.0f, 2.0f, 8.0f};
  cam.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  engine.renderer()->setCamera(cam);

  // Target crate + dynamic projectile sphere + trigger zone
  const nimbus::ecs::Entity target = engine.createEntity("TargetCrate");
  engine.scene()->setLocal(target, {{0.0f, 2.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}});
  nimbus::physics::Collider tCol;
  tCol.type = nimbus::physics::Collider::Type::Box;
  engine.world()->add<nimbus::physics::Collider>(target, tCol);
  engine.world()->add<nimbus::physics::BoxCollider>(target, {{1.0f, 1.0f, 1.0f}});

  const nimbus::ecs::Entity projectile = engine.createEntity("Projectile");
  engine.scene()->setLocal(projectile, {{0.0f, 2.0f, 4.0f}, {0, 0, 0, 1}, {1, 1, 1}});
  nimbus::physics::RigidBody projBody;
  projBody.useGravity = false;
  projBody.velocity = {0.0f, 0.0f, -24.0f};
  engine.world()->add<nimbus::physics::RigidBody>(projectile, projBody);
  nimbus::physics::Collider pCol;
  pCol.type = nimbus::physics::Collider::Type::Sphere;
  engine.world()->add<nimbus::physics::Collider>(projectile, pCol);
  engine.world()->add<nimbus::physics::SphereCollider>(projectile, {0.35f});

  int hits = 0;
  engine.physics()->setOnCollision([&](const nimbus::physics::Contact& c) {
    if ((c.a == projectile && c.b == target) || (c.b == projectile && c.a == target)) {
      ++hits;
      nimbus::particles::Emitter burst;
      burst.position = c.point;
      burst.rate = 0.0f;
      burst.lifeMin = burst.lifeMax = 1.5f;
      engine.particles()->spawn(burst, 24.0f);
      const auto sfx = nimbus::audio::Mixer::generateTone(660.0f, 0.1f, 22050);
      nimbus::audio::Listener listener;
      listener.position = {0.0f, 2.0f, 8.0f};
      listener.forward = {0.0f, 0.0f, -1.0f};
      engine.audio()->setListener(listener);
      const uint32_t h = engine.audio()->play(sfx, 1.0f, false);
      engine.audio()->setPosition3d(h, c.point);
    }
  });

  // Renderable mesh & UI HUD
  nimbus::render::Mesh boxMesh = nimbus::render::createCube(1.0f);
  nimbus::render::Material mat;
  mat.vertexShader = nimbus::render::defaultLitVertexShader();
  mat.fragmentShader = nimbus::render::defaultLitFragmentShader();
  engine.renderer()->addObject({target, &boxMesh, &mat, false});

  auto hud = std::make_unique<nimbus::ui::Container>();
  auto scoreLabel = std::make_unique<nimbus::ui::Label>();
  scoreLabel->setText("FPS Arena HUD");
  scoreLabel->setRect({12, 12, 220, 28});
  hud->addChild(std::move(scoreLabel));
  engine.ui()->setRoot(std::move(hud));

  // Lua drone script
  std::string luaErr;
  const nimbus::ecs::Entity drone = engine.createEntity("LuaDrone");
  if (!engine.scripts()->run(
          "function move_drone(id, f) nimbus.set_position(id, f * 0.1, 3.0, 0.0) end",
          &luaErr)) {
    std::fprintf(stderr, "fps_arena_3d: Lua run failed: %s\n", luaErr.c_str());
    return 1;
  }

  // Simulate 45 frames
  engine.play();
  for (int f = 1; f <= 45; ++f) {
    double unused = 0.0;
    engine.scripts()->call("move_drone", {static_cast<double>(drone), static_cast<double>(f)},
                           &unused, &luaErr);
    engine.step();
    engine.renderFrame();
  }

  const auto rayHit = engine.physics()->raycast({{0, 2, 6}, {0, 0, -1}}, 20.0f);
  const bool droneMoved = engine.scene()->local(drone)->position.x > 4.0f;
  const bool ok = hits > 0 && engine.particles()->aliveCount() > 0 && rayHit.hit && droneMoved;

  std::printf(
      "fps_arena_3d: frames=%u hits=%d particles=%u rayHit=%d droneX=%.2f sceneBytes=%zu\n",
      engine.frame(), hits, engine.particles()->aliveCount(), rayHit.hit ? 1 : 0,
      engine.scene()->local(drone)->position.x, engine.scene()->serialize().size());

  engine.renderer()->clearObjects();
  engine.renderer()->releaseMesh(boxMesh);
  engine.renderer()->releaseMaterial(mat);
  engine.shutdown();
  return ok ? 0 : 1;
}
