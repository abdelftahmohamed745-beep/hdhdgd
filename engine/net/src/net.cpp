#include "nimbus/net.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

#include "nimbus/log.hpp"

namespace nimbus::net {
namespace {

constexpr uint8_t kSnapshotVersion = 1;
constexpr uint32_t kWireHeaderBytes = 14;  // magic(4) + ver(1) + msg(1) + seq(4) + nonce(4)
constexpr uint32_t kWireTagBytes = 8;      // keyed 64-bit authentication tag
constexpr uint8_t kWireVersion = 2;
enum class WireMessage : uint8_t { Connect = 1, Welcome = 2, Disconnect = 3, Snapshot = 4 };
constexpr std::array<uint8_t, 4> kWireMagic{{'N', 'M', 'B', 'E'}};
constexpr float kRetransmitInterval = 0.25f;

void writeU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xffu);
  out[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
}
uint16_t readU16(const uint8_t* in) {
  return static_cast<uint16_t>(in[0] | (static_cast<uint16_t>(in[1]) << 8u));
}
void writeU32(uint8_t* out, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}
uint32_t readU32(const uint8_t* in) {
  uint32_t value = 0;
  for (uint32_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (i * 8u);
  return value;
}
void writeU64(uint8_t* out, uint64_t value) {
  for (uint32_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}
uint64_t readU64(const uint8_t* in) {
  uint64_t value = 0;
  for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[i]) << (i * 8u);
  return value;
}
void writeFloat(uint8_t* out, float value) { writeU32(out, std::bit_cast<uint32_t>(value)); }
float readFloat(const uint8_t* in) { return std::bit_cast<float>(readU32(in)); }

uint64_t mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

uint64_t deriveKeySeed(const std::string& key, uint32_t nonce, uint32_t seq) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (uint8_t c : key) {
    h ^= c;
    h *= 0x100000001b3ULL;
    h = mix64(h);
  }
  h ^= (static_cast<uint64_t>(nonce) << 32u) | static_cast<uint64_t>(seq);
  return mix64(h);
}

void xorCipher(const std::string& key, uint32_t nonce, uint32_t seq, uint8_t* data, uint32_t size) {
  if (key.empty() || !data || size == 0) return;
  uint64_t state = deriveKeySeed(key, nonce, seq);
  uint32_t i = 0;
  while (i < size) {
    state = mix64(state + i);
    for (uint32_t b = 0; b < 8 && i < size; ++b, ++i) {
      data[i] ^= static_cast<uint8_t>((state >> (b * 8u)) & 0xffu);
    }
  }
}

uint64_t computeMac(const std::string& key, const uint8_t* data, uint32_t size) {
  uint64_t s0 = 0x736f6d6570736575ULL;
  uint64_t s1 = 0x646f72616e646f6dULL;
  for (size_t i = 0; i < key.size(); ++i) {
    s0 = mix64(s0 ^ (static_cast<uint8_t>(key[i]) * 0x9e3779b97f4a7c15ULL + i));
    s1 = mix64(s1 ^ (s0 + i));
  }
  for (uint32_t i = 0; i < size; ++i) {
    s0 = mix64(s0 ^ (static_cast<uint64_t>(data[i]) << ((i & 7u) * 8u)));
    s1 = mix64(s1 + s0);
  }
  return mix64(s0 ^ s1);
}

bool constantTimeEq64(uint64_t a, uint64_t b) {
  const uint64_t diff = a ^ b;
  return diff == 0;
}

bool sameSnapshot(const EntitySnapshot& a, const EntitySnapshot& b) {
  return a.position.x == b.position.x && a.position.y == b.position.y &&
         a.position.z == b.position.z && a.rotation.x == b.rotation.x &&
         a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
         a.rotation.w == b.rotation.w && a.velocity.x == b.velocity.x &&
         a.velocity.y == b.velocity.y && a.velocity.z == b.velocity.z &&
         a.stateFlags == b.stateFlags;
}

bool finiteSnapshot(const EntitySnapshot& snapshot) {
  const float values[] = {snapshot.position.x, snapshot.position.y, snapshot.position.z,
                          snapshot.rotation.x, snapshot.rotation.y, snapshot.rotation.z,
                          snapshot.rotation.w, snapshot.velocity.x, snapshot.velocity.y,
                          snapshot.velocity.z};
  for (float value : values) if (!std::isfinite(value)) return false;
  return true;
}

bool sendWire(Socket* socket, const std::string& address, uint16_t port, WireMessage message,
              uint32_t seq, const Config& config, const uint8_t* payload = nullptr,
              uint32_t payloadSize = 0) {
  const bool useAuth = !config.authKey.empty();
  const uint32_t tagBytes = useAuth ? kWireTagBytes : 0u;
  if (!socket || payloadSize + kWireHeaderBytes + tagBytes > kMaxPacketBytes) return false;
  std::array<uint8_t, kMaxPacketBytes> packet{};
  std::copy(kWireMagic.begin(), kWireMagic.end(), packet.data());
  packet[4] = kWireVersion;
  packet[5] = static_cast<uint8_t>(message);
  writeU32(packet.data() + 6, seq);
  const uint32_t nonce = static_cast<uint32_t>(mix64(seq ^ (port * 0x9e3779b1u)));
  writeU32(packet.data() + 10, nonce);
  if (payloadSize && payload) {
    std::memcpy(packet.data() + kWireHeaderBytes, payload, payloadSize);
    if (useAuth && config.encryptPayload) {
      xorCipher(config.authKey, nonce, seq, packet.data() + kWireHeaderBytes, payloadSize);
    }
  }
  const uint32_t frameBytes = kWireHeaderBytes + payloadSize;
  if (useAuth) {
    const uint64_t tag = computeMac(config.authKey, packet.data(), frameBytes);
    writeU64(packet.data() + frameBytes, tag);
  }
  return socket->send(address.c_str(), port, packet.data(), frameBytes + tagBytes);
}

bool decodeWire(const Config& config, uint8_t* buffer, uint32_t received, WireMessage& outMsg,
                uint32_t& outSeq, uint32_t& outPayloadSize) {
  const bool useAuth = !config.authKey.empty();
  const uint32_t tagBytes = useAuth ? kWireTagBytes : 0u;
  if (!buffer || received < kWireHeaderBytes + tagBytes) return false;
  if (!std::equal(kWireMagic.begin(), kWireMagic.end(), buffer) || buffer[4] != kWireVersion) {
    return false;
  }
  if (buffer[5] < static_cast<uint8_t>(WireMessage::Connect) ||
      buffer[5] > static_cast<uint8_t>(WireMessage::Snapshot)) {
    return false;
  }
  const uint32_t frameBytes = received - tagBytes;
  if (useAuth) {
    const uint64_t expectedTag = computeMac(config.authKey, buffer, frameBytes);
    const uint64_t packetTag = readU64(buffer + frameBytes);
    if (!constantTimeEq64(expectedTag, packetTag)) return false;
  }
  outMsg = static_cast<WireMessage>(buffer[5]);
  outSeq = readU32(buffer + 6);
  const uint32_t nonce = readU32(buffer + 10);
  outPayloadSize = frameBytes - kWireHeaderBytes;
  if (useAuth && config.encryptPayload && outPayloadSize > 0) {
    xorCipher(config.authKey, nonce, outSeq, buffer + kWireHeaderBytes, outPayloadSize);
  }
  return true;
}

}  // namespace

bool ReplayWindow::accept(uint32_t sequence) {
  if (sequence == 0) return false;
  if (!initialized) {
    highestSeq = sequence;
    mask = 1ULL;
    initialized = true;
    return true;
  }
  if (sequence > highestSeq) {
    const uint32_t diff = sequence - highestSeq;
    if (diff >= 64) {
      mask = 1ULL;
    } else {
      mask = (mask << diff) | 1ULL;
    }
    highestSeq = sequence;
    return true;
  }
  const uint32_t behind = highestSeq - sequence;
  if (behind >= 64) return false;
  const uint64_t bit = 1ULL << behind;
  if ((mask & bit) != 0) return false;
  mask |= bit;
  return true;
}

void Reliability::send(const Packet& packet, std::function<void(const Packet&)> transmit) {
  if (!transmit) return;
  Packet outgoing = packet;
  outgoing.sequence = localSequence_++;
  if (localSequence_ == 0) localSequence_ = 1;
  outgoing.ack = remoteSequence_;
  outgoing.ackBits = ackBits_;
  if (packet.channel != Channel::Unreliable) pending_.push_back({outgoing, 0.0f, 1});
  transmit(outgoing);
}

void Reliability::acknowledge(uint32_t ack, uint32_t ackBits) {
  if (ack == 0 || pending_.empty()) return;
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const PendingPacket& pending) {
    const uint32_t sequence = pending.packet.sequence;
    if (sequence == ack) return true;
    const uint32_t behind = ack - sequence;
    return behind >= 1 && behind <= 32 && (ackBits & (1u << (behind - 1u))) != 0;
  }), pending_.end());
}

void Reliability::update(float deltaSeconds, std::function<void(const Packet&)> transmit) {
  if (!transmit || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  for (PendingPacket& pending : pending_) {
    pending.elapsed += deltaSeconds;
    if (pending.elapsed < kRetransmitInterval) continue;
    pending.elapsed = 0.0f;
    ++pending.transmissions;
    pending.packet.ack = remoteSequence_;
    pending.packet.ackBits = ackBits_;
    transmit(pending.packet);
  }
}

bool Reliability::receive(const Packet& packet, Packet* out) {
  acknowledge(packet.ack, packet.ackBits);
  if (packet.sequence == 0) return false;
  if (!hasRemoteSequence_) {
    remoteSequence_ = packet.sequence;
    ackBits_ = 0;
    hasRemoteSequence_ = true;
  } else {
    const uint32_t forward = packet.sequence - remoteSequence_;
    if (forward == 0) return false;  // duplicate newest packet
    if (forward < 0x80000000u) {
      ackBits_ = forward >= 32 ? 0u : (ackBits_ << forward);
      if (forward <= 32) ackBits_ |= (1u << (forward - 1u));
      remoteSequence_ = packet.sequence;
    } else {
      const uint32_t behind = remoteSequence_ - packet.sequence;
      if (behind == 0 || behind > 32) return false;
      const uint32_t mask = 1u << (behind - 1u);
      if ((ackBits_ & mask) != 0) return false;
      ackBits_ |= mask;
    }
  }
  if (out) *out = packet;
  return true;
}

void SnapshotBuffer::capture(uint32_t entityId, const math::Vec3& position,
                             const math::Quat& rotation, const math::Vec3& velocity,
                             uint32_t flags) {
  EntitySnapshot snapshot;
  snapshot.entityId = entityId;
  snapshot.position = position;
  snapshot.rotation = rotation;
  snapshot.velocity = velocity;
  snapshot.stateFlags = flags;
  const auto found = std::find_if(snapshots_.begin(), snapshots_.end(),
                                  [entityId](const EntitySnapshot& item) {
                                    return item.entityId == entityId;
                                  });
  if (found == snapshots_.end()) snapshots_.push_back(snapshot);
  else *found = snapshot;
}

uint32_t SnapshotBuffer::encodeDelta(const SnapshotBuffer& baseline, uint8_t* out,
                                     uint32_t capacity) const {
  if (!out || snapshots_.size() > std::numeric_limits<uint16_t>::max()) return 0;
  std::vector<const EntitySnapshot*> changed;
  changed.reserve(snapshots_.size());
  for (const EntitySnapshot& snapshot : snapshots_) {
    if (!finiteSnapshot(snapshot)) return 0;
    const auto previous = std::find_if(baseline.snapshots_.begin(), baseline.snapshots_.end(),
                                       [&](const EntitySnapshot& item) {
                                         return item.entityId == snapshot.entityId;
                                       });
    if (previous == baseline.snapshots_.end() || !sameSnapshot(snapshot, *previous))
      changed.push_back(&snapshot);
  }
  constexpr uint32_t kRecordBytes = 4u + 3u * 4u + 4u * 4u + 3u * 4u + 4u;
  const uint64_t required = 3ull + static_cast<uint64_t>(changed.size()) * kRecordBytes;
  if (required > capacity || required > kMaxPacketBytes) return 0;
  out[0] = kSnapshotVersion;
  writeU16(out + 1, static_cast<uint16_t>(changed.size()));
  uint32_t offset = 3;
  for (const EntitySnapshot* value : changed) {
    writeU32(out + offset, value->entityId); offset += 4;
    const float values[] = {value->position.x, value->position.y, value->position.z,
                            value->rotation.x, value->rotation.y, value->rotation.z,
                            value->rotation.w, value->velocity.x, value->velocity.y,
                            value->velocity.z};
    for (float component : values) { writeFloat(out + offset, component); offset += 4; }
    writeU32(out + offset, value->stateFlags); offset += 4;
  }
  return offset;
}

bool SnapshotBuffer::decodeDelta(const uint8_t* data, uint32_t size) {
  constexpr uint32_t kRecordBytes = 4u + 3u * 4u + 4u * 4u + 3u * 4u + 4u;
  if (!data || size < 3 || data[0] != kSnapshotVersion) return false;
  const uint16_t count = readU16(data + 1);
  const uint64_t expected = 3ull + static_cast<uint64_t>(count) * kRecordBytes;
  if (expected != size) return false;
  std::vector<EntitySnapshot> decoded;
  decoded.reserve(count);
  std::unordered_set<uint32_t> ids;
  uint32_t offset = 3;
  for (uint16_t i = 0; i < count; ++i) {
    EntitySnapshot snapshot;
    snapshot.entityId = readU32(data + offset); offset += 4;
    float values[10]{};
    for (float& component : values) { component = readFloat(data + offset); offset += 4; }
    snapshot.position = {values[0], values[1], values[2]};
    snapshot.rotation = {values[3], values[4], values[5], values[6]};
    snapshot.velocity = {values[7], values[8], values[9]};
    snapshot.stateFlags = readU32(data + offset); offset += 4;
    if (!ids.insert(snapshot.entityId).second || !finiteSnapshot(snapshot)) return false;
    decoded.push_back(snapshot);
  }
  snapshots_ = std::move(decoded);
  return true;
}

SnapshotBuffer SnapshotBuffer::interpolate(const SnapshotBuffer& from, const SnapshotBuffer& to,
                                           float alpha) {
  const float t = math::clamp(std::isfinite(alpha) ? alpha : 0.0f, 0.0f, 1.0f);
  SnapshotBuffer result;
  for (const EntitySnapshot& dst : to.snapshots_) {
    const auto it = std::find_if(from.snapshots_.begin(), from.snapshots_.end(),
                                 [&](const EntitySnapshot& s) { return s.entityId == dst.entityId; });
    if (it == from.snapshots_.end()) {
      if (t >= 0.5f) result.snapshots_.push_back(dst);
      continue;
    }
    EntitySnapshot blended;
    blended.entityId = dst.entityId;
    blended.position = math::lerp(it->position, dst.position, t);
    blended.rotation = math::quatSlerp(it->rotation, dst.rotation, t);
    blended.velocity = math::lerp(it->velocity, dst.velocity, t);
    blended.stateFlags = t < 0.5f ? it->stateFlags : dst.stateFlags;
    result.snapshots_.push_back(blended);
  }
  return result;
}

void Prediction::applyInput(uint32_t tick, const math::Vec3& move) {
  if (tick <= lastProcessedTick_ && !pendingInputs_.empty()) return;
  if (!std::isfinite(move.x) || !std::isfinite(move.y) || !std::isfinite(move.z)) return;
  predicted_ += move;
  pendingInputs_.emplace_back(tick, move);
  lastProcessedTick_ = std::max(lastProcessedTick_, tick);
}

bool Prediction::reconcile(uint32_t serverTick, const math::Vec3& serverPosition,
                           math::Vec3* correction) {
  if (!std::isfinite(serverPosition.x) || !std::isfinite(serverPosition.y) ||
      !std::isfinite(serverPosition.z)) return false;
  math::Vec3 replayed{0, 0, 0};
  for (const auto& [tick, move] : pendingInputs_)
    if (tick > serverTick) replayed += move;
  const math::Vec3 expected = serverPosition + replayed;
  const math::Vec3 delta = expected - predicted_;
  if (correction) *correction = delta;
  predicted_ = expected;
  pendingInputs_.erase(
      std::remove_if(pendingInputs_.begin(), pendingInputs_.end(),
                     [serverTick](const auto& entry) { return entry.first <= serverTick; }),
      pendingInputs_.end());
  return math::length(delta) > 1e-4f;
}

Server::~Server() { stop(); }

bool Server::start(uint16_t port) {
  stop();
  socket_ = createUdpSocket();
  if (!socket_ || !socket_->open(port)) {
    log::write(log::Level::Error, "net: server socket open failed on port %u", port);
    if (socket_) { delete socket_; socket_ = nullptr; }
    return false;
  }
  port_ = socket_->localPort();
  if (port_ == 0) { stop(); return false; }
  running_ = true;
  clock_ = accumulator_ = 0.0f;
  rejectedPackets_ = 0;
  log::write(log::Level::Info, "net: server listening on %u", port);
  return true;
}

void Server::stop() {
  running_ = false;
  peers_.clear();
  clientCount_ = 0;
  port_ = 0;
  if (socket_) { socket_->close(); delete socket_; socket_ = nullptr; }
}

void Server::tick(float deltaSeconds) {
  if (!running_ || !socket_) return;
  if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
    accumulator_ += deltaSeconds;
    clock_ += deltaSeconds;
  }
  std::array<uint8_t, kMaxPacketBytes> buffer{};
  for (;;) {
    std::string from;
    uint16_t fromPort = 0;
    const int32_t received = socket_->receive(buffer.data(), static_cast<uint32_t>(buffer.size()),
                                              &from, &fromPort);
    if (received <= 0) break;
    WireMessage message{};
    uint32_t seq = 0;
    uint32_t payloadSize = 0;
    if (!decodeWire(config_, buffer.data(), static_cast<uint32_t>(received), message, seq,
                    payloadSize)) {
      ++rejectedPackets_;
      continue;
    }
    auto peer = std::find_if(peers_.begin(), peers_.end(), [&](const Peer& item) {
      return item.address == from && item.port == fromPort;
    });
    if (message == WireMessage::Connect) {
      if (peer == peers_.end()) {
        if (peers_.size() >= kMaxClients) continue;
        Peer np{from, fromPort, clock_, 1, {}, {}};
        if (!np.rxReplay.accept(seq)) {
          ++rejectedPackets_;
          continue;
        }
        peers_.push_back(std::move(np));
        peer = std::prev(peers_.end());
      } else {
        if (!peer->rxReplay.accept(seq)) {
          ++rejectedPackets_;
          continue;
        }
        peer->lastSeen = clock_;
      }
      sendWire(socket_, from, fromPort, WireMessage::Welcome, peer->txSeq++, config_);
    } else if (message == WireMessage::Disconnect) {
      if (peer != peers_.end()) {
        if (!peer->rxReplay.accept(seq)) {
          ++rejectedPackets_;
          continue;
        }
        peers_.erase(peer);
      }
    } else if (peer != peers_.end()) {
      if (!peer->rxReplay.accept(seq)) {
        ++rejectedPackets_;
        continue;
      }
      peer->lastSeen = clock_;
    }
  }
  peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [&](const Peer& peer) {
    return config_.timeoutSeconds > 0.0f && clock_ - peer.lastSeen > config_.timeoutSeconds;
  }), peers_.end());
  clientCount_ = static_cast<uint32_t>(peers_.size());
}

void Server::broadcastSnapshot(const SnapshotBuffer& snapshot) {
  if (!socket_ || peers_.empty()) return;
  SnapshotBuffer emptyBaseline;
  const uint32_t maxPayload =
      kMaxPacketBytes - kWireHeaderBytes - (!config_.authKey.empty() ? kWireTagBytes : 0u);
  std::array<uint8_t, kMaxPacketBytes> payload{};
  const uint32_t size = snapshot.encodeDelta(emptyBaseline, payload.data(), maxPayload);
  if (size == 0 && !snapshot.snapshots().empty()) return;
  for (Peer& peer : peers_)
    sendWire(socket_, peer.address, peer.port, WireMessage::Snapshot, peer.txSeq++, config_,
             payload.data(), size);
}

Client::~Client() { disconnect(); }

bool Client::connect(const std::string& address, uint16_t port) {
  disconnect();
  if (address.empty() || port == 0) return false;
  socket_ = createUdpSocket();
  if (!socket_ || !socket_->open(0)) {
    if (socket_) { delete socket_; socket_ = nullptr; }
    return false;
  }
  address_ = address;
  port_ = port;
  timeSinceLastPacket_ = 0.0f;
  timeSinceLastSend_ = 0.0f;
  txSeq_ = 1;
  rejectedPackets_ = 0;
  rxReplay_.reset();
  if (!sendWire(socket_, address_, port_, WireMessage::Connect, txSeq_++, config_)) {
    disconnect();
    return false;
  }
  handshaking_ = true;
  connected_ = false;
  return true;
}

void Client::disconnect() {
  if (socket_ && (connected_ || handshaking_))
    sendWire(socket_, address_, port_, WireMessage::Disconnect, txSeq_++, config_);
  connected_ = false;
  handshaking_ = false;
  if (socket_) { socket_->close(); delete socket_; socket_ = nullptr; }
  prevSnapshot_.clear();
  interpolated_.clear();
  rxReplay_.reset();
  timeSinceLastPacket_ = 0.0f;
  timeSinceLastSend_ = 0.0f;
}

void Client::tick(float deltaSeconds) {
  if ((!connected_ && !handshaking_) || !socket_) return;
  if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
    timeSinceLastPacket_ += deltaSeconds;
    timeSinceLastSend_ += deltaSeconds;
  }
  const float heartbeatInterval = std::isfinite(config_.heartbeatSeconds) &&
                                          config_.heartbeatSeconds > 0.05f
                                      ? config_.heartbeatSeconds : 1.0f;
  if (timeSinceLastSend_ >= heartbeatInterval) {
    sendWire(socket_, address_, port_, WireMessage::Connect, txSeq_++, config_);
    timeSinceLastSend_ = 0.0f;
  }
  std::array<uint8_t, kMaxPacketBytes> buffer{};
  for (;;) {
    std::string from;
    uint16_t fromPort = 0;
    const int32_t received = socket_->receive(buffer.data(), static_cast<uint32_t>(buffer.size()),
                                              &from, &fromPort);
    if (received <= 0) break;
    if (from != address_ || fromPort != port_) continue;
    WireMessage message{};
    uint32_t seq = 0;
    uint32_t payloadSize = 0;
    if (!decodeWire(config_, buffer.data(), static_cast<uint32_t>(received), message, seq,
                    payloadSize) ||
        !rxReplay_.accept(seq)) {
      ++rejectedPackets_;
      continue;
    }
    timeSinceLastPacket_ = 0.0f;
    if (message == WireMessage::Welcome) {
      connected_ = true;
      handshaking_ = false;
    } else if (message == WireMessage::Snapshot && connected_) {
      SnapshotBuffer update;
      if (update.decodeDelta(buffer.data() + kWireHeaderBytes, payloadSize)) {
        prevSnapshot_ = std::move(interpolated_);
        interpolated_ = config_.entityInterpolation && !prevSnapshot_.snapshots().empty()
                            ? SnapshotBuffer::interpolate(prevSnapshot_, update, 0.5f)
                            : std::move(update);
      }
    }
  }
  if (config_.timeoutSeconds > 0.0f && timeSinceLastPacket_ > config_.timeoutSeconds) {
    connected_ = false;
    handshaking_ = false;
    if (socket_) { socket_->close(); delete socket_; socket_ = nullptr; }
  }
}

}  // namespace nimbus::net
