#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "nimbus/ai.hpp"
#include "nimbus/engine.hpp"
#include "nimbus/net.hpp"

int main() {
  nimbus::Engine engine;
  if (!engine.initialize("AI Patrol & Net Sync Example", 640, 480, nimbus::rhi::Api::Headless)) {
    std::fprintf(stderr, "ai_patrol_sim: engine init failed\n");
    return 1;
  }

  // Build NavMesh over flat 8x8 heightfield ((width+1) x (depth+1) vertices)
  std::vector<float> heights(9 * 9, 0.0f);
  nimbus::ai::NavMesh nav;
  if (!nav.buildFromGrid(8, 8, 2.0f, heights, 0.7f, {-7.0f, 0.0f, -7.0f})) {
    std::fprintf(stderr, "ai_patrol_sim: NavMesh build failed\n");
    return 1;
  }

  const std::vector<nimbus::math::Vec3> path =
      nav.findPath({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f});
  if (path.size() < 2) {
    std::fprintf(stderr, "ai_patrol_sim: NavMesh pathfinding failed\n");
    return 1;
  }

  // AI Agent with Behavior Tree + Steering + Blackboard
  nimbus::ai::SteeringAgent agent;
  agent.position = path.front();
  agent.maxSpeed = 6.0f;
  size_t waypointIdx = 1;
  uint32_t actionsCompleted = 0;

  nimbus::ai::Blackboard bb;
  bb.setBool("patrol_active", true);

  auto tree = std::make_unique<nimbus::ai::Sequence>();
  tree->add(std::make_unique<nimbus::ai::Condition>(
      [&](nimbus::ecs::Entity) { return bb.getBool("patrol_active"); }));
  auto cooldown = std::make_unique<nimbus::ai::Cooldown>(0.05f);
  cooldown->setChild(std::make_unique<nimbus::ai::Action>(
      [&](nimbus::ecs::Entity, float dt) -> nimbus::ai::Status {
        if (waypointIdx >= path.size()) return nimbus::ai::Status::Success;
        const auto target = path[waypointIdx];
        const auto force = nimbus::ai::steering::arrive(agent, target, 1.5f);
        nimbus::ai::steering::integrate(agent, force, dt);
        if (nimbus::math::length(target - agent.position) < 0.75f) {
          ++waypointIdx;
          ++actionsCompleted;
        }
        return nimbus::ai::Status::Running;
      }));
  tree->add(std::move(cooldown));

  // Authenticated & encrypted UDP client/server snapshot replication
  nimbus::net::Config secCfg;
  secCfg.authKey = "nimbus-patrol-hmac-key";
  secCfg.encryptPayload = true;
  nimbus::net::Server server(secCfg);
  nimbus::net::Client client(secCfg);
  if (!server.start(0) || !client.connect("127.0.0.1", server.port())) {
    std::fprintf(stderr, "ai_patrol_sim: net startup failed\n");
    return 1;
  }

  const nimbus::ecs::Entity npc = engine.createEntity("PatrolBot");
  engine.play();
  for (int i = 0; i < 240; ++i) {
    const float dt = 1.0f / 60.0f;
    tree->tick(npc, dt);
    engine.scene()->setLocal(npc, {agent.position, {0, 0, 0, 1}, {1, 1, 1}});
    nimbus::net::SnapshotBuffer snap;
    snap.capture(npc, agent.position, {0, 0, 0, 1}, agent.velocity, static_cast<uint32_t>(i));
    server.tick(dt);
    client.tick(dt);
    server.broadcastSnapshot(snap);
    client.tick(dt);
    engine.step();
  }

  const bool arrived = nimbus::math::length(path.back() - agent.position) < 1.2f;
  const bool synced = !client.interpolated().snapshots().empty();
  std::printf(
      "ai_patrol_sim: navTris=%u waypoints=%zu reached=%u finalPos=(%.2f,%.2f,%.2f) netSnaps=%zu\n",
      nav.triangleCount(), path.size(), actionsCompleted, agent.position.x, agent.position.y,
      agent.position.z, client.interpolated().snapshots().size());

  client.disconnect();
  server.stop();
  engine.shutdown();
  return (arrived && synced) ? 0 : 1;
}
