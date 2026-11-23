// Nimbus test suite: one assertion macro, per-system coverage.
#include "nimbus/engine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>

#include "nimbus/log.hpp"

namespace nimbus {

namespace {
int g_failures = 0;
int g_checks = 0;
std::string g_current = "suite";

struct LifetimeProbe {
  static int live;
  std::string value;
  LifetimeProbe() { ++live; }
  explicit LifetimeProbe(std::string text) : value(std::move(text)) { ++live; }
  LifetimeProbe(const LifetimeProbe& other) : value(other.value) { ++live; }
  LifetimeProbe(LifetimeProbe&& other) noexcept : value(std::move(other.value)) { ++live; }
  LifetimeProbe& operator=(const LifetimeProbe&) = default;
  LifetimeProbe& operator=(LifetimeProbe&&) noexcept = default;
  ~LifetimeProbe() { --live; }
};
int LifetimeProbe::live = 0;

struct alignas(64) OverAlignedComponent {
  uint64_t value = 0xC0FFEEu;
};
struct ExtraComponent { int value = 7; };

void check(bool condition, const std::string& what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    log::write(log::Level::Error, "test [%s] FAILED: %s", g_current.c_str(), what.c_str());
  }
}

void near(float actual, float expected, float tolerance, const std::string& what) {
  ++g_checks;
  if (std::fabs(actual - expected) > tolerance) {
    ++g_failures;
    log::write(log::Level::Error, "test [%s] FAILED: %s (got %f, want %f)", g_current.c_str(),
               what.c_str(), actual, expected);
  }
}
}  // namespace

int Engine::runTests() {
  g_failures = 0;
  g_checks = 0;

  log::write(log::Level::Info, "tests: begin");
  g_current = "math";
  {
    using namespace math;
    near(length(normalize(Vec3{3, 4, 0})), 1.0f, 1e-5f, "normalize unit length");
    near(dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}), 0.0f, 1e-6f, "orthogonal dot");
    const Quat q = quatFromAxisAngle({0, 1, 0}, kPi / 2.0f);
    const Vec3 rotated = rotate(q, {1, 0, 0});
    near(rotated.z, -1.0f, 1e-4f, "quat rotates X to -Z");
    const Mat4 m = mat4FromTrs({1, 2, 3}, {0, 0, 0, 1}, {1, 1, 1});
    const Vec4 t = mat4Transform(m, {0, 0, 0, 1});
    near(t.x, 1.0f, 1e-5f, "translation applies");
    near(t.y, 2.0f, 1e-5f, "translation applies y");
    log::write(log::Level::Info, "tests: math A");
    const Mat4 inv = mat4Inverse(m);
    const Mat4 identity = mat4Multiply(m, inv);
    near(identity[0][0], 1.0f, 1e-4f, "inverse roundtrip");
    log::write(log::Level::Info, "tests: math B");
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    check(aabbContains(box, {0, 0, 0}), "aabb contains origin");
    check(!aabbContains(box, {2, 0, 0}), "aabb rejects outside");
    log::write(log::Level::Info, "tests: math C");
    const Mat4 vp = mat4Multiply(mat4Perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f),
                                 mat4LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0}));
    const Frustum frustum = frustumFromViewProj(vp);
    check(frustumContainsPoint(frustum, {0, 0, 0}), "frustum contains origin");
    check(!frustumContainsPoint(frustum, {0, 0, 10}), "frustum rejects behind camera");
  }

  g_current = "ecs";
  log::write(log::Level::Info, "tests: math done, %d checks, %d failures", g_checks, g_failures);
  {
    ecs::Registry registry;
    ecs::World world(&registry);
    struct Position {
      float x = 0.0f, y = 0.0f;
    };
    struct Velocity {
      float vx = 0.0f;
    };
    log::write(log::Level::Info, "tests: ecs create a");
    const ecs::Entity a = world.create("a");
    log::write(log::Level::Info, "tests: ecs create b");
    const ecs::Entity b = world.create("b");
    log::write(log::Level::Info, "tests: ecs add Position a");
    world.add<Position>(a, Position{1.0f, 2.0f});
    log::write(log::Level::Info, "tests: ecs add Velocity a");
    world.add<Velocity>(a, Velocity{3.0f});
    log::write(log::Level::Info, "tests: ecs add Position b");
    world.add<Position>(b, Position{4.0f, 5.0f});
    check(world.get<Position>(a) != nullptr, "component present");
    check(world.get<Velocity>(b) == nullptr, "component absent");
    near(world.get<Position>(a)->x, 1.0f, 1e-6f, "component value");
    const auto& matched = world.query<Position>();
    check(matched.size() == 2, "query matches both archetypes");
    const auto& withVelocity = world.query<Position, Velocity>();
    check(withVelocity.size() == 1, "query with two components");
    world.remove<Position>(a);
    check(world.get<Position>(a) == nullptr, "component removed");
    world.destroy(b);
    check(!world.alive(b), "entity destroyed");
    check(world.entityCount() == 1, "entity count after destroy");
  }
  {
    ecs::Registry registry;
    ecs::World world(&registry);
    const ecs::Entity original = world.create("recycled");
    check(world.alive(original), "empty-independent entity liveness");
    world.destroy(original);
    world.destroy(original);  // A repeated destroy must not duplicate the free index.
    const ecs::Entity replacement = world.create("replacement");
    const ecs::Entity another = world.create("another");
    check(replacement != original, "recycled entity receives a new generation handle");
    check(!world.alive(original), "stale generation handle is rejected");
    check(world.get<LifetimeProbe>(original) == nullptr, "stale handle cannot access components");
    check(another != replacement, "double destroy does not duplicate free-list entries");

    std::vector<ecs::Entity> many;
    many.reserve(48);
    for (int i = 0; i < 48; ++i) {
      const ecs::Entity entity = world.create("pool-growth");
      world.add<LifetimeProbe>(entity, LifetimeProbe("component-" + std::to_string(i)));
      many.push_back(entity);
    }
    check(world.get<LifetimeProbe>(many[37])->value == "component-37",
          "non-trivial component survives pool growth");
    world.add<ExtraComponent>(many[37], ExtraComponent{});
    check(world.get<LifetimeProbe>(many[37])->value == "component-37",
          "non-trivial component survives archetype migration");
    world.remove<ExtraComponent>(many[37]);
    check(world.get<LifetimeProbe>(many[37])->value == "component-37",
          "non-trivial component survives component removal migration");

    const ecs::Entity alignedEntity = world.create("aligned");
    world.add<OverAlignedComponent>(alignedEntity, OverAlignedComponent{});
    const auto* aligned = world.get<OverAlignedComponent>(alignedEntity);
    check(aligned && reinterpret_cast<uintptr_t>(aligned) % alignof(OverAlignedComponent) == 0,
          "over-aligned component storage is correctly aligned");
    for (ecs::Entity entity : many) world.destroy(entity);
    world.destroy(alignedEntity);
    world.destroy(replacement);
    world.destroy(another);
  }
  check(LifetimeProbe::live == 0, "component destructors run exactly once after world teardown");

  g_current = "scene";
  log::write(log::Level::Info, "tests: ecs done, %d checks, %d failures", g_checks, g_failures);
  {
    ecs::Registry registry;
    ecs::World world(&registry);
    scene::Scene scene(&world);
    const ecs::Entity root = scene.createNode("root");
    const ecs::Entity child = scene.createNode("child", root);
    scene.setLocal(child, scene::Transform{{1.2345678f, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    scene.setLocal(root, scene::Transform{{1, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    const math::Mat4 worldMatrix = scene.world(child);
    near(worldMatrix[3][0], 2.2345678f, 1e-5f, "hierarchy transform composes");
    check(scene.nodeCount() == 2, "node count");
    const std::string serialized = scene.serialize();
    check(serialized.find("\"version\":1") != std::string::npos, "scene serializes version");
    check(serialized.find("\"name\":\"root\"") != std::string::npos, "scene serializes name");
    world.setName(root, "root\"quoted\\path\nline");
    const std::string escaped = scene.serialize();
    check(escaped.find("root\\\"quoted\\\\path\\nline") != std::string::npos,
          "scene JSON escapes quotes, slashes and control characters");
    check(escaped.find(std::to_string(root)) != std::string::npos,
          "scene serialization preserves 64-bit entity identifiers");
    check(escaped.find(std::to_string(child)) != std::string::npos,
          "scene serialization preserves parent and child handles");
    scene::Scene restored(&world);
    check(restored.deserialize(escaped), "scene deserialization accepts valid saved scene");
    check(restored.nodeCount() == 2, "scene deserialization restores hierarchy nodes");
    const ecs::Entity restoredRoot = restored.roots().empty() ? ecs::kNullEntity : restored.roots()[0];
    check(restoredRoot != ecs::kNullEntity &&
              world.name(restoredRoot) == "root\"quoted\\path\nline",
          "scene deserialization restores escaped UTF-8 text");
    const scene::Node* restoredRootNode =
        restored.nodeCount() == 2 && !restored.roots().empty()
            ? restored.node(restored.roots()[0]) : nullptr;
    const auto restoredNode = restoredRootNode && !restoredRootNode->children.empty()
                                  ? restoredRootNode->children[0] : ecs::kNullEntity;
    check(restoredNode != ecs::kNullEntity &&
              std::fabs(restored.local(restoredNode)->position.x - 1.2345678f) < 1e-6f,
          "scene serialization preserves float round-trip precision");
    check(!restored.deserialize("{invalid"), "scene deserialization rejects malformed JSON");
    check(restored.nodeCount() == 2, "failed scene load preserves current scene");
    if (restoredRoot != ecs::kNullEntity) restored.destroy(restoredRoot);
    scene.destroy(root);
    check(scene.nodeCount() == 0, "destroy subtree");
  }

  g_current = "physics";
  log::write(log::Level::Info, "tests: scene done, %d checks, %d failures", g_checks, g_failures);
  {
    ecs::Registry registry;
    ecs::World world(&registry);
    physics::World physics(&world);
    physics::Contact contact{};
    check(physics::intersectSphereSphere({{0, 0, 0}, 1.0f}, {{1.5f, 0, 0}, 1.0f}, contact),
          "sphere/sphere hit");
    near(contact.penetration, 0.5f, 1e-5f, "penetration depth");
    check(!physics::intersectSphereSphere({{0, 0, 0}, 1.0f}, {{3.0f, 0, 0}, 1.0f}, contact),
          "sphere/sphere miss");
    check(physics::intersectBoxBox({{-1, -1, -1}, {1, 1, 1}}, {{0, 0, 0}, {2, 2, 2}}, contact),
          "box/box hit");
    physics::RayHit rayHit{};
    check(physics::raycastAabb({{0, 0, 5}, {0, 0, -1}}, {{-1, -1, -1}, {1, 1, 1}}, rayHit),
          "ray hits box");
    near(rayHit.distance, 4.0f, 1e-4f, "ray distance");
    check(physics::raycastSphere({{0, 0, 5}, {0, 0, -1}}, {{0, 0, 0}, 1.0f}, rayHit),
          "ray hits sphere");
    physics.step(1.0f / 60.0f);
    check(physics.contactCount() == 0, "no contacts without colliders");
  }
  {
    ecs::Registry registry;
    ecs::World world(&registry);
    scene::Scene scene(&world);
    physics::World physics(&world, &scene);
    physics.setGravity({0, 0, 0});
    const ecs::Entity floor = scene.createNode("floor");
    scene.setLocal(floor, scene::Transform{{0, -1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    world.add<physics::Collider>(floor, physics::Collider{physics::Collider::Type::Box});
    world.add<physics::BoxCollider>(floor, physics::BoxCollider{{5, 0.5f, 5}});
    const ecs::Entity ball = scene.createNode("ball");
    physics::RigidBody body;
    body.useGravity = false;
    body.velocity = {0, -1, 0};
    world.add<physics::RigidBody>(ball, body);
    world.add<physics::Collider>(ball, physics::Collider{physics::Collider::Type::Sphere});
    world.add<physics::SphereCollider>(ball, physics::SphereCollider{0.5f});
    physics.step(0.1f);
    check(physics.contactCount() == 1, "world detects collider contact");
    near(world.get<physics::RigidBody>(ball)->velocity.y, 0.0f, 1e-4f,
         "contact impulse stops downward sphere");
    check(scene.local(ball)->position.y > -0.1f, "penetration correction moves dynamic body out");
    const physics::RayHit hit = physics.raycast({{0, 3, 0}, {0, -1, 0}}, 10.0f);
    check(hit.hit && hit.entity == ball, "world raycast returns nearest entity");
  }

  g_current = "anim";
  log::write(log::Level::Info, "tests: physics done, %d checks, %d failures", g_checks, g_failures);
  {
    anim::Skeleton skeleton;
    skeleton.joints.push_back({"root", -1});
    skeleton.joints.push_back({"child", 0});
    skeleton.inverseBindMatrices.resize(2, math::mat4Identity());
    anim::Clip clip;
    clip.name = "idle";
    clip.duration = 1.0f;
    clip.framesPerSecond = 30.0f;
    anim::Track track;
    track.joint = 1;
    track.keys.push_back({0.0f, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    track.keys.push_back({1.0f, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    clip.tracks.push_back(track);
    anim::Animator animator;
    animator.setSkeleton(skeleton);
    animator.states.push_back(anim::AnimationState{&clip, 0.0f, 1.0f, 1.0f, true});
    animator.play(0);
    animator.update(0.5f);
    check(animator.skinMatrices.size() == 2, "skin matrices sized");
    near(animator.skinMatrices[1][3][1], 0.5f, 1e-4f,
         "animation samples and interpolates keyframe time instead of selecting a frame index");
    anim::Skeleton invalidSkeleton;
    invalidSkeleton.joints.push_back({"a", 1});
    invalidSkeleton.joints.push_back({"b", 0});
    anim::Animator invalidAnimator;
    check(!invalidAnimator.setSkeleton(invalidSkeleton),
          "animation rejects cyclic joint hierarchies");
    math::Vec3 mid{}, end{};
    check(anim::solveTwoBoneIK({0, 0, 0}, {1, 1, 0}, 1.0f, 1.0f, mid, end), "two-bone IK solves");
  }

  g_current = "terrain";
  log::write(log::Level::Info, "tests: anim done, %d checks, %d failures", g_checks, g_failures);
  {
    terrain::Noise noise(42);
    terrain::Terrain terrain(64, 64, 1.0f, 32);
    terrain.generate(noise, 4, 8.0f);
    near(terrain.field().at(0, 0), noise.fbm(0.0f, 0.0f, 4) * 8.0f, 1e-4f, "terrain generation");
    check(terrain.chunkCountX() == 2 && terrain.chunkCountZ() == 2, "chunk counts");
    render::Mesh& mesh = terrain.chunkMesh(0, 0, 0);
    check(!mesh.vertices.empty(), "chunk mesh built");
    check(mesh.indices.size() % 3 == 0, "index buffer is triangles");
    check(terrain.lodForDistance(1.0f) == 0, "near LOD");
check(terrain.lodForDistance(200.0f * 32.0f) == 3, "far LOD");
    const math::Vec3 normal = terrain.field().normal(10.0f, 10.0f);
    near(math::length(normal), 1.0f, 1e-3f, "terrain normal normalized");
  }

  g_current = "particles";
  log::write(log::Level::Info, "tests: terrain done, %d checks, %d failures", g_checks, g_failures);
  {
    particles::System system(64);
    particles::Emitter emitter;
    emitter.rate = 10.0f;
    emitter.maxParticles = 64;
    emitter.lifeMin = 10.0f;
    emitter.lifeMax = 10.0f;
    system.addEmitter(emitter);
    system.update(1.0f);
    check(system.aliveCount() > 0, "particles spawned");
    check(system.aliveCount() <= 64, "capacity respected");
    std::vector<float> instanceData(system.aliveCount() * 8);
    system.writeInstanceData(instanceData.data(), system.aliveCount());
    check(instanceData.size() == system.aliveCount() * 8, "instance data size");
    particles::System isolated(8);
    particles::Emitter redEmitter;
    redEmitter.rate = 1.0f;
    redEmitter.maxParticles = 1;
    redEmitter.lifeMin = redEmitter.lifeMax = 10.0f;
    redEmitter.speedMin = redEmitter.speedMax = 0.0f;
    redEmitter.gravity = {0, 0, 0};
    redEmitter.colorStart = redEmitter.colorEnd = {1, 0, 0};
    particles::Emitter blueEmitter = redEmitter;
    blueEmitter.gravity = {0, -10, 0};
    blueEmitter.colorStart = blueEmitter.colorEnd = {0, 0, 1};
    const uint64_t redId = isolated.addEmitter(redEmitter);
    const uint64_t blueId = isolated.addEmitter(blueEmitter);
    isolated.update(1.0f);
    isolated.update(1.0f);
    const auto red = std::find_if(isolated.particles().begin(), isolated.particles().end(),
        [redId](const particles::Particle& particle) { return particle.emitterId == redId; });
    const auto blue = std::find_if(isolated.particles().begin(), isolated.particles().end(),
        [blueId](const particles::Particle& particle) { return particle.emitterId == blueId; });
    check(isolated.aliveCount() == 2, "particle emitters enforce independent per-emitter limits");
    check(red != isolated.particles().end() && red->position.y == 0.0f &&
              red->color.x > red->color.z,
          "particle attributes retain the red emitter's gravity and color settings");
    check(blue != isolated.particles().end() && blue->position.y < -1.0f &&
              blue->color.z > blue->color.x,
          "particle attributes retain the blue emitter's gravity and color settings");
    isolated.writeInstanceData(nullptr, 1);
    particles::System fractional(4);
    particles::Emitter slowEmitter;
    slowEmitter.rate = 0.5f;
    slowEmitter.lifeMin = slowEmitter.lifeMax = 10.0f;
    fractional.addEmitter(slowEmitter);
    fractional.update(1.0f);
    check(fractional.aliveCount() == 0, "fractional particle emission accumulates without rounding up");
    fractional.update(1.0f);
    check(fractional.aliveCount() == 1, "fractional emission remainder spawns at the correct rate");
  }

  g_current = "audio";
  log::write(log::Level::Info, "tests: particles done, %d checks, %d failures", g_checks, g_failures);
  log::write(log::Level::Info, "tests: audio begin");
  {
    audio::Mixer mixer(8000);
    const audio::Clip clip = audio::Mixer::generateTone(440.0f, 0.1f, 8000);
    log::write(log::Level::Info, "tests: audio tone generated, samples=%u", clip.sampleCount());
    check(clip.sampleCount() == 800, "tone sample count");
    log::write(log::Level::Info, "tests: audio playing");
    const uint32_t handle = mixer.play(clip, 1.0f, false);
    check(mixer.activeVoices() == 1, "voice started");
    log::write(log::Level::Info, "tests: audio mixing");
    mixer.mix(400);
    check(mixer.activeVoices() <= 1, "voice state after mix");
    mixer.stop(handle);
    check(mixer.activeVoices() == 0, "voice stopped");
  }

  g_current = "ai";
  log::write(log::Level::Info, "tests: audio done, %d checks, %d failures", g_checks, g_failures);
  {
    ai::Sequence sequence;
    sequence.add(std::make_unique<ai::Action>([](ecs::Entity, float) {
      return ai::Status::Success;
    }));
    check(sequence.tick(ecs::kNullEntity, 0.0f) == ai::Status::Success, "sequence success");
    ai::Selector selector;
    selector.add(std::make_unique<ai::Action>([](ecs::Entity, float) {
      return ai::Status::Failure;
    }));
    selector.add(std::make_unique<ai::Action>([](ecs::Entity, float) {
      return ai::Status::Success;
    }));
    check(selector.tick(ecs::kNullEntity, 0.0f) == ai::Status::Success, "selector fallback");
    ai::Perception perception;
    perception.position = {0, 0, 0};
    perception.forward = {0, 0, -1};
    perception.sightRange = 10.0f;
    perception.sightAngle = 90.0f;
    check(ai::canSee(perception, {0, 0, -5}), "perception sees ahead");
    check(!ai::canSee(perception, {0, 0, 5}), "perception blind behind");
    ai::NavMesh nav;
    ai::NavTriangle a{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    ai::NavTriangle b{{1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    a.neighbours[0] = 1;
    b.neighbours[0] = 0;
    nav.addTriangle(a);
    nav.addTriangle(b);
    const std::vector<math::Vec3> path = nav.findPath({0.2f, 0, 0.2f}, {0.8f, 0, 0.8f});
    check(path.size() >= 2, "navmesh path found");
    ai::WorldState start;
    start.values["has_key"] = false;
    ai::WorldState goal;
    goal.values["has_key"] = true;
    ai::GoapAction action;
    action.name = "pick_key";
    action.effects.values["has_key"] = true;
    const std::vector<std::string> plan = ai::planGoap(start, goal, {action});
    check(plan.size() == 1 && plan[0] == "pick_key", "goap plan");
  }

  g_current = "net";
  log::write(log::Level::Info, "tests: ai done, %d checks, %d failures", g_checks, g_failures);
  {
    net::SnapshotBuffer emptyBaseline;
    net::SnapshotBuffer buffer;
    buffer.capture(1, {0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0}, 0);
    buffer.capture(2, {1, 2, 3}, {0, 0, 0, 1}, {0, 0, 0}, 1);
    uint8_t bytes[256]{};
    const uint32_t size = buffer.encodeDelta(emptyBaseline, bytes, sizeof(bytes));
    check(size > 0, "snapshot encodes full baseline");
    net::SnapshotBuffer decoded;
    check(decoded.decodeDelta(bytes, size), "snapshot decodes");
    check(decoded.snapshots().size() == 2, "snapshot count preserved");
    check(decoded.snapshots()[1].entityId == 2, "snapshot entity identity preserved");
    uint8_t tooSmall[8]{};
    check(buffer.encodeDelta(emptyBaseline, tooSmall, sizeof(tooSmall)) == 0,
          "snapshot rejects insufficient output capacity atomically");
    net::SnapshotBuffer changed;
    changed.capture(1, {5, 0, 0}, {0, 0, 0, 1}, {1, 0, 0}, 0);
    changed.capture(2, {1, 2, 3}, {0, 0, 0, 1}, {0, 0, 0}, 1);
    uint8_t deltaBytes[128]{};
    const uint32_t deltaSize = changed.encodeDelta(buffer, deltaBytes, sizeof(deltaBytes));
    net::SnapshotBuffer deltaDecoded;
    check(deltaSize > 0 && deltaDecoded.decodeDelta(deltaBytes, deltaSize),
          "snapshot delta contains changed records only");
    check(deltaDecoded.snapshots().size() == 1 && deltaDecoded.snapshots()[0].entityId == 1,
          "snapshot delta omits unchanged records");
    const auto priorDecodedCount = deltaDecoded.snapshots().size();
    check(!deltaDecoded.decodeDelta(deltaBytes, deltaSize - 1), "snapshot rejects truncated payload");
    check(deltaDecoded.snapshots().size() == priorDecodedCount,
          "invalid snapshot does not partially mutate decoded state");

    net::Reliability sender;
    net::Reliability receiver;
    net::Packet reliablePacket;
    reliablePacket.channel = net::Channel::ReliableOrdered;
    reliablePacket.payload = {1, 2, 3};
    net::Packet transmitted;
    uint32_t sends = 0;
    sender.send(reliablePacket, [&](const net::Packet& packet) { transmitted = packet; ++sends; });
    check(sender.pendingReliable() == 1 && sends == 1, "reliable send queues packet");
    sender.update(0.3f, [&](const net::Packet& packet) { transmitted = packet; ++sends; });
    check(sends == 2, "reliable packet retransmits after timeout");
    net::Packet received;
    check(receiver.receive(transmitted, &received), "reliable receiver accepts packet");
    check(!receiver.receive(transmitted, nullptr), "reliable receiver rejects duplicate packet");
    net::Packet acknowledgement;
    acknowledgement.channel = net::Channel::Unreliable;
    receiver.send(acknowledgement, [&](const net::Packet& packet) { acknowledgement = packet; });
    check(sender.receive(acknowledgement, nullptr), "acknowledgement packet is accepted");
    check(sender.pendingReliable() == 0, "acknowledgement clears reliable retransmission queue");

    net::Server server;
    check(server.start(0) && server.port() != 0, "UDP server binds an ephemeral local port");
    net::Client client;
    check(client.connect("127.0.0.1", server.port()), "UDP client sends connection request");
    server.tick(0.01f);
    client.tick(0.01f);
    check(client.connected(), "UDP handshake establishes client connection");
    check(server.clientCount() == 1, "server tracks unique connected clients");
    for (uint32_t tick = 0; tick < 60; ++tick) {
      client.tick(0.1f);
      server.tick(0.1f);
      client.tick(0.0f);
    }
    check(client.connected() && server.clientCount() == 1,
          "UDP client heartbeat keeps the peer alive beyond the idle timeout");
    server.broadcastSnapshot(buffer);
    client.tick(0.01f);
    check(client.interpolated().snapshots().size() == 2, "server broadcasts a decodable snapshot");
    client.disconnect();
    server.tick(0.01f);
    check(server.clientCount() == 0, "client disconnect removes server peer");
    server.stop();

    net::Prediction prediction;
    prediction.applyInput(1, {1, 0, 0});
    near(prediction.predicted().x, 1.0f, 1e-5f, "prediction applies input");
    math::Vec3 correction{};
    check(!prediction.reconcile(1, {1, 0, 0}, &correction), "prediction matches server");
    prediction.applyInput(2, {1, 0, 0});
    check(prediction.reconcile(1, {2, 0, 0}, &correction), "prediction corrects divergent server state");
    near(correction.x, 1.0f, 1e-5f, "correction magnitude");
  }

  g_current = "ui";
  log::write(log::Level::Info, "tests: net done, %d checks, %d failures", g_checks, g_failures);
  {
    auto container = std::make_unique<ui::Container>();
    container->setAxis(ui::Axis::Vertical);
    container->setPadding(4.0f);
    auto label = std::make_unique<ui::Label>();
    label->setText("hello");
    label->setRect({0, 0, 100, 20});
    container->addChild(std::move(label));
    ui::Root root;
    root.setRoot(std::move(container));
    root.layout(800, 600);
    std::vector<ui::DrawCommand> commands;
    root.draw(commands);
    check(commands.size() == 1, "label draws one command");
    check(commands[0].text == "hello", "label text");
    auto button = std::make_unique<ui::Button>();
    button->setLabel("ok");
    button->setRect({0, 0, 50, 20});
    bool clicked = false;
    button->onClick([&] { clicked = true; });
    check(button->handlePointer({10, 10}, true), "button press");
    check(button->handlePointer({10, 10}, false), "button release");
    check(clicked, "button callback fired");
  }

  g_current = "asset";
  log::write(log::Level::Info, "tests: ui done, %d checks, %d failures", g_checks, g_failures);
  {
    check(asset::idFromPath("a.obj") != asset::idFromPath("b.obj"), "asset ids differ");
    check(asset::idFromPath("same") == asset::idFromPath("same"), "asset ids stable");
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    asset::MeshImport mesh;
    check(asset::importObj(obj, mesh), "obj import");
    check(mesh.vertices.size() == 24, "obj vertex count");
    check(mesh.indices.size() == 3, "obj index count");
    near(mesh.bounds.max.x, 1.0f, 1e-5f, "obj bounds");

    const std::string relativeObj =
        "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nf -4 -3 -2 -1\n";
    asset::MeshImport quad;
    check(asset::importObj(relativeObj, quad), "obj import resolves negative relative indices");
    check(quad.indices.size() == 6, "obj import triangulates a quad");
    check(quad.vertices.size() == 48, "obj quad emits six complete interleaved vertices");
    near(quad.bounds.max.y, 2.0f, 1e-5f, "obj quad bounds");

    const std::string attributesObj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
    asset::MeshImport attributes;
    check(asset::importObj(attributesObj, attributes), "obj import supports v/vt/vn references");
    near(attributes.vertices[5], 1.0f, 1e-5f, "obj importer preserves referenced normal");
    near(attributes.vertices[14], 1.0f, 1e-5f, "obj importer preserves referenced uv");

    asset::MeshImport unchanged;
    unchanged.vertices = {42.0f};
    check(!asset::importObj("v 0 0 0\nv 1 0 0\nf 0 1 2\n", unchanged),
          "obj importer rejects zero vertex indices");
    check(unchanged.vertices.size() == 1 && unchanged.vertices[0] == 42.0f,
          "obj parse failure leaves previous output unchanged");
    check(!asset::importObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n", unchanged),
          "obj importer rejects out-of-range vertex indices");
    check(!asset::importObj("v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", unchanged),
          "obj importer rejects non-finite vertex data");
    check(!asset::importObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/7 2/1 3/1\n", unchanged),
          "obj importer rejects out-of-range texture coordinates");
  }

  g_current = "script";
  log::write(log::Level::Info, "tests: asset done, %d checks, %d failures", g_checks, g_failures);
  {
    script::Runtime runtime;
    check(runtime.initialize(), "lua initializes");
    std::string error;
    check(runtime.run("x = 1 + 2", &error), "lua runs");
    double result = 0.0;
    check(runtime.run("function add(a,b) return a+b end", &error), "lua defines function");
    check(runtime.call("add", {2.0, 3.0}, &result, &error), "lua calls function");
    near(static_cast<float>(result), 5.0f, 1e-6f, "lua return value");
    runtime.shutdown();
  }

  g_current = "engine";
  log::write(log::Level::Info, "tests: script done, %d checks, %d failures", g_checks, g_failures);
  {
    Engine engine;
    check(engine.initialize("test", 320, 240, rhi::Api::None), "engine initializes headless");
    const ecs::Entity entity = engine.createEntity("player");
    check(engine.world()->alive(entity), "entity created");
    engine.scene()->setLocal(entity, scene::Transform{{5, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}});
    near(engine.scene()->world(entity)[3][0], 5.0f, 1e-5f, "engine scene transform");
    engine.play();
    for (int i = 0; i < 10; ++i) engine.step();
    check(engine.frame() == 10, "engine steps");
    engine.pause();
    engine.step();
    check(engine.frame() == 11, "paused engine supports an explicit single-step");
    engine.stop();
    check(engine.state() == State::Stopped, "engine stops");
    engine.shutdown();
  }

  log::write(log::Level::Info, "tests: %d checks, %d failures", g_checks, g_failures);
  return g_failures;
}

}  // namespace nimbus
