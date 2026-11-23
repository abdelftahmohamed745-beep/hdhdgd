#include "nimbus/ai.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace nimbus::ai {
namespace {

bool nearlySameVertex(const Vec3& lhs, const Vec3& rhs) {
  return math::lengthSquared(lhs - rhs) <= 1e-5f;
}

float triArea2D(const Vec3& a, const Vec3& b, const Vec3& c) {
  return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

bool pointInTriangleXZ(const Vec3& p, const NavTriangle& t, float& outY) {
  const float x0 = t.b.x - t.a.x;
  const float z0 = t.b.z - t.a.z;
  const float x1 = t.c.x - t.a.x;
  const float z1 = t.c.z - t.a.z;
  const float xp = p.x - t.a.x;
  const float zp = p.z - t.a.z;

  const float denom = x0 * z1 - x1 * z0;
  if (std::abs(denom) < 1e-7f) return false;
  const float invDenom = 1.0f / denom;
  const float u = (xp * z1 - x1 * zp) * invDenom;
  const float v = (x0 * zp - xp * z0) * invDenom;
  const float w = 1.0f - u - v;
  constexpr float kEps = -1e-4f;
  if (u < kEps || v < kEps || w < kEps) return false;
  outY = w * t.a.y + u * t.b.y + v * t.c.y;
  return true;
}

Vec3 closestPointOnTriangle3D(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3 ab = b - a;
  const Vec3 ac = c - a;
  const Vec3 ap = p - a;
  const float d1 = math::dot(ab, ap);
  const float d2 = math::dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return a;

  const Vec3 bp = p - b;
  const float d3 = math::dot(ab, bp);
  const float d4 = math::dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return b;

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return a + ab * v;
  }

  const Vec3 cp = p - c;
  const float d5 = math::dot(ab, cp);
  const float d6 = math::dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return c;

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return a + ac * w;
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + (c - b) * w;
  }

  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return a + ab * v + ac * w;
}

bool findSharedEdge(const NavTriangle& a, const NavTriangle& b, Vec3& outP0, Vec3& outP1) {
  const Vec3 va[3] = {a.a, a.b, a.c};
  const Vec3 vb[3] = {b.a, b.b, b.c};
  Vec3 matches[2]{};
  int found = 0;
  for (const Vec3& pa : va) {
    for (const Vec3& pb : vb) {
      if (nearlySameVertex(pa, pb)) {
        bool already = false;
        for (int k = 0; k < found; ++k) {
          if (nearlySameVertex(matches[k], pa)) {
            already = true;
            break;
          }
        }
        if (!already && found < 2) matches[found++] = pa;
        break;
      }
    }
  }
  if (found < 2) return false;
  outP0 = matches[0];
  outP1 = matches[1];
  return true;
}

Vec3 clampMagnitude(const Vec3& v, float maxMag) {
  if (maxMag <= 0.0f) return {0, 0, 0};
  const float lenSq = math::lengthSquared(v);
  if (lenSq <= maxMag * maxMag) return v;
  return v * (maxMag / std::sqrt(lenSq));
}

}  // namespace

Status Sequence::tick(ecs::Entity entity, float deltaSeconds) {
  while (cursor_ < children_.size()) {
    const Status status = children_[cursor_]->tick(entity, deltaSeconds);
    if (status == Status::Running) return Status::Running;
    if (status == Status::Failure) {
      cursor_ = 0;
      return Status::Failure;
    }
    ++cursor_;
  }
  cursor_ = 0;
  return Status::Success;
}

void Sequence::reset() {
  cursor_ = 0;
  for (NodePtr& child : children_) child->reset();
}

Status Selector::tick(ecs::Entity entity, float deltaSeconds) {
  while (cursor_ < children_.size()) {
    const Status status = children_[cursor_]->tick(entity, deltaSeconds);
    if (status == Status::Running) return Status::Running;
    if (status == Status::Success) {
      cursor_ = 0;
      return Status::Success;
    }
    ++cursor_;
  }
  cursor_ = 0;
  return Status::Failure;
}

void Selector::reset() {
  cursor_ = 0;
  for (NodePtr& child : children_) child->reset();
}

Status Inverter::tick(ecs::Entity entity, float deltaSeconds) {
  if (!child_) return Status::Failure;
  const Status status = child_->tick(entity, deltaSeconds);
  if (status == Status::Success) return Status::Failure;
  if (status == Status::Failure) return Status::Success;
  return Status::Running;
}

Status Cooldown::tick(ecs::Entity entity, float deltaSeconds) {
  if (remaining_ > 0.0f) {
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
      remaining_ = std::max(0.0f, remaining_ - deltaSeconds);
    }
    if (remaining_ > 0.0f) return Status::Failure;
  }
  if (!child_) {
    remaining_ = std::max(0.0f, duration_);
    return Status::Success;
  }
  const Status status = child_->tick(entity, deltaSeconds);
  if (status != Status::Running) {
    remaining_ = std::max(0.0f, duration_);
  }
  return status;
}

void Cooldown::reset() {
  remaining_ = 0.0f;
  if (child_) child_->reset();
}

Status Repeater::tick(ecs::Entity entity, float deltaSeconds) {
  if (!child_) return Status::Failure;
  const Status status = child_->tick(entity, deltaSeconds);
  if (status == Status::Running) return Status::Running;
  if (status == Status::Failure) {
    completed_ = 0;
    return Status::Failure;
  }
  ++completed_;
  if (repeatCount_ > 0 && completed_ >= repeatCount_) {
    completed_ = 0;
    return Status::Success;
  }
  child_->reset();
  return Status::Running;
}

void Repeater::reset() {
  completed_ = 0;
  if (child_) child_->reset();
}

Status Condition::tick(ecs::Entity entity, float deltaSeconds) {
  if (!predicate_ || !predicate_(entity)) return Status::Failure;
  if (child_) return child_->tick(entity, deltaSeconds);
  return Status::Success;
}

namespace steering {

Vec3 seek(const SteeringAgent& agent, const Vec3& target) {
  const Vec3 delta = target - agent.position;
  if (math::lengthSquared(delta) <= 1e-8f) return {0, 0, 0};
  const Vec3 desired = math::normalize(delta) * agent.maxSpeed;
  return clampMagnitude(desired - agent.velocity, agent.maxForce);
}

Vec3 flee(const SteeringAgent& agent, const Vec3& threat) {
  const Vec3 delta = agent.position - threat;
  const Vec3 dir = math::lengthSquared(delta) > 1e-8f ? math::normalize(delta) : Vec3{0, 0, 1};
  const Vec3 desired = dir * agent.maxSpeed;
  return clampMagnitude(desired - agent.velocity, agent.maxForce);
}

Vec3 arrive(const SteeringAgent& agent, const Vec3& target, float slowRadius) {
  const Vec3 delta = target - agent.position;
  const float dist = math::length(delta);
  if (dist <= 1e-5f) return clampMagnitude(-agent.velocity, agent.maxForce);
  float targetSpeed = agent.maxSpeed;
  if (slowRadius > 1e-4f && dist < slowRadius) {
    targetSpeed = agent.maxSpeed * (dist / slowRadius);
  }
  const Vec3 desired = (delta / dist) * targetSpeed;
  return clampMagnitude(desired - agent.velocity, agent.maxForce);
}

Vec3 wander(SteeringAgent& agent, float circleDist, float circleRadius, float jitter,
            float deltaSeconds) {
  if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
    // Deterministic smooth angular sweep mixed with position-derived phase.
    const float phase =
        std::sin(agent.position.x * 0.73f + agent.position.z * 1.19f + agent.wanderAngle * 2.1f);
    agent.wanderAngle += phase * jitter * deltaSeconds;
  }
  const Vec3 forward = math::lengthSquared(agent.velocity) > 1e-5f
                           ? math::normalize(agent.velocity)
                           : Vec3{0, 0, -1};
  const Vec3 circleCenter = forward * circleDist;
  const Vec3 displacement{std::cos(agent.wanderAngle) * circleRadius, 0.0f,
                          std::sin(agent.wanderAngle) * circleRadius};
  return clampMagnitude(circleCenter + displacement, agent.maxForce);
}

Vec3 obstacleAvoid(const SteeringAgent& agent, const std::vector<math::Sphere>& obstacles,
                   float lookAhead) {
  if (lookAhead <= 0.0f || obstacles.empty()) return {0, 0, 0};
  const float speed = math::length(agent.velocity);
  if (speed <= 1e-4f) return {0, 0, 0};
  const Vec3 forward = agent.velocity / speed;
  const float dynamicLookAhead = lookAhead * std::min(1.5f, std::max(0.25f, speed / agent.maxSpeed));

  const math::Sphere* closest = nullptr;
  float closestT = std::numeric_limits<float>::max();
  Vec3 closestProj{};

  for (const math::Sphere& obs : obstacles) {
    const Vec3 toObs = obs.center - agent.position;
    const float t = math::dot(toObs, forward);
    if (t < 0.0f || t > dynamicLookAhead) continue;
    const Vec3 proj = agent.position + forward * t;
    const float lateralDistSq = math::lengthSquared(obs.center - proj);
    const float safeRadius = obs.radius + agent.radius;
    if (lateralDistSq <= safeRadius * safeRadius && t < closestT) {
      closestT = t;
      closest = &obs;
      closestProj = proj;
    }
  }
  if (!closest) return {0, 0, 0};
  Vec3 away = closestProj - closest->center;
  if (math::lengthSquared(away) <= 1e-6f) {
    away = Vec3{-forward.z, 0.0f, forward.x};
  }
  const float urgency = 1.0f - (closestT / std::max(0.01f, dynamicLookAhead));
  return clampMagnitude(math::normalize(away) * agent.maxForce * (0.5f + urgency), agent.maxForce);
}

void integrate(SteeringAgent& agent, const Vec3& steeringForce, float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  const Vec3 clampedForce = clampMagnitude(steeringForce, agent.maxForce);
  agent.velocity = clampMagnitude(agent.velocity + clampedForce * deltaSeconds, agent.maxSpeed);
  agent.position += agent.velocity * deltaSeconds;
}

}  // namespace steering

bool canSee(const Perception& observer, const Vec3& target) {
  const Vec3 toTarget = target - observer.position;
  const float distance = math::length(toTarget);
  if (distance > observer.sightRange) return false;
  if (distance <= 1e-5f) return true;
  const float angle = math::degrees(std::acos(math::clamp(
      math::dot(math::normalize(toTarget), math::normalize(observer.forward)), -1.0f, 1.0f)));
  return angle <= observer.sightAngle * 0.5f;
}

bool NavMesh::buildFromGrid(uint32_t width, uint32_t depth, float cellSize,
                            const std::vector<float>& heights, float maxSlopeCos,
                            const Vec3& origin) {
  triangles_.clear();
  if (width == 0 || depth == 0 || !std::isfinite(cellSize) || cellSize <= 0.0f) return false;
  const uint32_t vxCount = width + 1u;
  const size_t totalVerts = static_cast<size_t>(vxCount) * static_cast<size_t>(depth + 1u);
  if (!heights.empty() && heights.size() < totalVerts) return false;

  auto heightAt = [&](uint32_t gx, uint32_t gz) -> float {
    if (heights.empty()) return origin.y;
    return origin.y + heights[static_cast<size_t>(gz) * vxCount + gx];
  };
  auto vertexAt = [&](uint32_t gx, uint32_t gz) -> Vec3 {
    return {origin.x + static_cast<float>(gx) * cellSize, heightAt(gx, gz),
            origin.z + static_cast<float>(gz) * cellSize};
  };

  struct EdgeKey {
    uint64_t key = 0;
    uint32_t triIndex = 0;
  };
  std::unordered_map<uint64_t, std::vector<int32_t>> edgeToTris;
  auto makeEdgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32u) | static_cast<uint64_t>(b);
  };

  struct TriVerts {
    uint32_t idx[3]{};
  };
  std::vector<TriVerts> triIndices;

  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t i00 = z * vxCount + x;
      const uint32_t i10 = z * vxCount + (x + 1u);
      const uint32_t i01 = (z + 1u) * vxCount + x;
      const uint32_t i11 = (z + 1u) * vxCount + (x + 1u);

      const Vec3 p00 = vertexAt(x, z);
      const Vec3 p10 = vertexAt(x + 1u, z);
      const Vec3 p01 = vertexAt(x, z + 1u);
      const Vec3 p11 = vertexAt(x + 1u, z + 1u);

      auto tryAdd = [&](const Vec3& a, const Vec3& b, const Vec3& c, uint32_t ia, uint32_t ib,
                        uint32_t ic) {
        const Vec3 normal = math::normalize(math::cross(b - a, c - a));
        if (std::abs(normal.y) < maxSlopeCos) return;
        const int32_t ti = static_cast<int32_t>(triangles_.size());
        NavTriangle tri;
        tri.a = a;
        tri.b = b;
        tri.c = c;
        triangles_.push_back(tri);
        triIndices.push_back({{ia, ib, ic}});
        edgeToTris[makeEdgeKey(ia, ib)].push_back(ti);
        edgeToTris[makeEdgeKey(ib, ic)].push_back(ti);
        edgeToTris[makeEdgeKey(ic, ia)].push_back(ti);
      };

      tryAdd(p00, p01, p11, i00, i01, i11);
      tryAdd(p00, p11, p10, i00, i11, i10);
    }
  }

  for (size_t i = 0; i < triangles_.size(); ++i) {
    const TriVerts& tv = triIndices[i];
    const uint64_t edges[3] = {
        makeEdgeKey(tv.idx[0], tv.idx[1]),
        makeEdgeKey(tv.idx[1], tv.idx[2]),
        makeEdgeKey(tv.idx[2], tv.idx[0]),
    };
    for (int e = 0; e < 3; ++e) {
      const auto it = edgeToTris.find(edges[e]);
      if (it == edgeToTris.end()) continue;
      for (int32_t other : it->second) {
        if (other != static_cast<int32_t>(i)) {
          triangles_[i].neighbours[e] = other;
          break;
        }
      }
    }
  }

  return !triangles_.empty();
}

int32_t NavMesh::findContainingTriangle(const Vec3& point) const {
  int32_t best = -1;
  float bestHeightDiff = std::numeric_limits<float>::max();
  for (size_t i = 0; i < triangles_.size(); ++i) {
    float surfaceY = 0.0f;
    if (pointInTriangleXZ(point, triangles_[i], surfaceY)) {
      const float diff = std::abs(surfaceY - point.y);
      if (diff < bestHeightDiff) {
        bestHeightDiff = diff;
        best = static_cast<int32_t>(i);
      }
    }
  }
  return best;
}

Vec3 NavMesh::closestPointOnMesh(const Vec3& point) const {
  const int32_t index = findContainingTriangle(point);
  if (index >= 0) {
    float y = point.y;
    pointInTriangleXZ(point, triangles_[static_cast<size_t>(index)], y);
    return {point.x, y, point.z};
  }
  Vec3 best{0, 0, 0};
  float bestDistance = std::numeric_limits<float>::max();
  for (const NavTriangle& t : triangles_) {
    const Vec3 p = closestPointOnTriangle3D(point, t.a, t.b, t.c);
    const float distance = math::lengthSquared(p - point);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = p;
    }
  }
  return best;
}

std::vector<Vec3> NavMesh::findPath(const Vec3& start, const Vec3& goal) const {
  const int32_t startTriangle = findContainingTriangle(start);
  const int32_t goalTriangle = findContainingTriangle(goal);
  if (startTriangle < 0 || goalTriangle < 0) return {};
  if (startTriangle == goalTriangle) return {start, goal};

  std::unordered_map<int32_t, float> cost;
  std::unordered_map<int32_t, int32_t> cameFrom;
  using Entry = std::pair<float, int32_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
  const auto center = [&](int32_t index) {
    const NavTriangle& t = triangles_[static_cast<size_t>(index)];
    return (t.a + t.b + t.c) * (1.0f / 3.0f);
  };
  const auto heuristic = [&](int32_t index) { return math::distance(center(index), goal); };

  cost[startTriangle] = 0.0f;
  frontier.emplace(heuristic(startTriangle), startTriangle);
  while (!frontier.empty()) {
    const auto [_, current] = frontier.top();
    frontier.pop();
    if (current == goalTriangle) break;
    const NavTriangle& triangle = triangles_[static_cast<size_t>(current)];
    for (int32_t neighbour : triangle.neighbours) {
      if (neighbour < 0 || static_cast<size_t>(neighbour) >= triangles_.size()) continue;
      const float next = cost[current] + math::distance(center(current), center(neighbour));
      if (!cost.count(neighbour) || next < cost[neighbour]) {
        cost[neighbour] = next;
        cameFrom[neighbour] = current;
        frontier.emplace(next + heuristic(neighbour), neighbour);
      }
    }
  }
  if (!cameFrom.count(goalTriangle)) return {};
  std::vector<int32_t> nodes;
  for (int32_t at = goalTriangle; at != startTriangle; at = cameFrom[at]) nodes.push_back(at);
  nodes.push_back(startTriangle);
  std::reverse(nodes.begin(), nodes.end());

  // Build ordered left/right portals for the Simple Stupid Funnel Algorithm.
  struct Portal {
    Vec3 left;
    Vec3 right;
  };
  std::vector<Portal> portals;
  portals.reserve(nodes.size() + 1);
  portals.push_back({start, start});
  for (size_t i = 0; i + 1 < nodes.size(); ++i) {
    const NavTriangle& curr = triangles_[static_cast<size_t>(nodes[i])];
    const NavTriangle& next = triangles_[static_cast<size_t>(nodes[i + 1])];
    Vec3 e0{}, e1{};
    if (!findSharedEdge(curr, next, e0, e1)) {
      const Vec3 mid = (center(nodes[i]) + center(nodes[i + 1])) * 0.5f;
      portals.push_back({mid, mid});
      continue;
    }
    const Vec3 currCenter = center(nodes[i]);
    // Orient portal so that looking from currCenter across the portal,
    // triArea2D(currCenter, left, right) >= 0 matches the funnel inequality checks.
    if (triArea2D(currCenter, e0, e1) >= 0.0f) {
      portals.push_back({e0, e1});
    } else {
      portals.push_back({e1, e0});
    }
  }
  portals.push_back({goal, goal});

  std::vector<Vec3> path;
  path.push_back(start);

  Vec3 portalApex = portals[0].left;
  Vec3 portalLeft = portals[0].left;
  Vec3 portalRight = portals[0].right;
  size_t apexIndex = 0;
  size_t leftIndex = 0;
  size_t rightIndex = 0;

  for (size_t i = 1; i < portals.size(); ++i) {
    const Vec3& left = portals[i].left;
    const Vec3& right = portals[i].right;

    // Update right vertex.
    if (triArea2D(portalApex, portalRight, right) <= 0.0f) {
      if (nearlySameVertex(portalApex, portalRight) ||
          triArea2D(portalApex, portalLeft, right) > 0.0f) {
        portalRight = right;
        rightIndex = i;
      } else {
        path.push_back(portalLeft);
        portalApex = portalLeft;
        apexIndex = leftIndex;
        portalLeft = portalApex;
        portalRight = portalApex;
        leftIndex = apexIndex;
        rightIndex = apexIndex;
        i = apexIndex;
        continue;
      }
    }

    // Update left vertex.
    if (triArea2D(portalApex, portalLeft, left) >= 0.0f) {
      if (nearlySameVertex(portalApex, portalLeft) ||
          triArea2D(portalApex, portalRight, left) < 0.0f) {
        portalLeft = left;
        leftIndex = i;
      } else {
        path.push_back(portalRight);
        portalApex = portalRight;
        apexIndex = rightIndex;
        portalLeft = portalApex;
        portalRight = portalApex;
        leftIndex = apexIndex;
        rightIndex = apexIndex;
        i = apexIndex;
        continue;
      }
    }
  }

  if (path.empty() || !nearlySameVertex(path.back(), goal)) {
    path.push_back(goal);
  }
  return path;
}

bool WorldState::matches(const WorldState& conditions) const {
  for (const auto& [key, value] : conditions.values)
    if (!values.count(key) || values.at(key) != value) return false;
  return true;
}

void WorldState::apply(const WorldState& effects) {
  for (const auto& [key, value] : effects.values) values[key] = value;
}

std::vector<std::string> planGoap(const WorldState& start, const WorldState& goal,
                                  const std::vector<GoapAction>& actions) {
  struct Node {
    WorldState state;
    float cost = 0.0f;
    std::vector<std::string> plan;
  };
  std::vector<Node> frontier;
  frontier.push_back({start, 0.0f, {}});
  std::unordered_set<std::string> visited;
  const auto keyOf = [](const WorldState& state) {
    std::string key;
    for (const auto& [name, value] : state.values) key += name + (value ? "1" : "0");
    return key;
  };
  const size_t kMaxExpansions = 512;
  size_t expansions = 0;
  while (!frontier.empty() && expansions < kMaxExpansions) {
    size_t bestIndex = 0;
    for (size_t i = 1; i < frontier.size(); ++i)
      if (frontier[i].cost < frontier[bestIndex].cost) bestIndex = i;
    Node node = std::move(frontier[bestIndex]);
    frontier.erase(frontier.begin() + static_cast<long>(bestIndex));
    ++expansions;
    if (node.state.matches(goal)) return node.plan;
    const std::string key = keyOf(node.state);
    if (visited.count(key)) continue;
    visited.insert(key);
    for (const GoapAction& action : actions) {
      if (!node.state.matches(action.preconditions)) continue;
      Node next = node;
      next.state.apply(action.effects);
      next.cost += action.cost;
      next.plan.push_back(action.name);
      frontier.push_back(std::move(next));
    }
  }
  return {};
}

}  // namespace nimbus::ai
