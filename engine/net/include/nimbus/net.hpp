// Nimbus networking: reliable UDP, authenticated/encrypted wire protocol, snapshot replication & interpolation, client prediction.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::net {

constexpr uint32_t kMaxClients = 32;
constexpr uint16_t kDefaultPort = 7777;
constexpr uint32_t kMaxPacketBytes = 1200;

enum class Channel : uint8_t { ReliableOrdered, ReliableSequenced, Unreliable };

struct Packet {
  uint32_t sequence = 0;
  uint32_t ack = 0;
  uint32_t ackBits = 0;
  Channel channel = Channel::Unreliable;
  std::vector<uint8_t> payload;
};

struct Config {
  uint32_t tickRate = 30;
  uint32_t maxPacketBytes = kMaxPacketBytes;
  float timeoutSeconds = 5.0f;
  float heartbeatSeconds = 1.0f;
  bool clientSidePrediction = true;
  bool entityInterpolation = true;
  std::string authKey;        // Non-empty enables keyed MAC authentication + 64-pkt replay protection.
  bool encryptPayload = false;  // Encrypts wire payloads with keyed stream cipher when authKey is set.
};

// A minimal UDP socket abstraction so the engine runs on Android and desktop.
class Socket {
 public:
  virtual ~Socket() = default;
  virtual bool open(uint16_t port) = 0;
  virtual void close() = 0;
  virtual bool send(const char* address, uint16_t port, const void* data, uint32_t size) = 0;
  virtual int32_t receive(void* buffer, uint32_t capacity, std::string* fromAddress,
                          uint16_t* fromPort) = 0;
  virtual uint16_t localPort() const = 0;
  virtual bool valid() const = 0;
};

Socket* createUdpSocket();

// Reliability layer: sequence numbers, acks, and resend for reliable channels.
class Reliability {
 public:
  void send(const Packet& packet, std::function<void(const Packet&)> transmit);
  void update(float deltaSeconds, std::function<void(const Packet&)> transmit);
  bool receive(const Packet& packet, Packet* out);
  uint32_t pendingReliable() const { return static_cast<uint32_t>(pending_.size()); }

 private:
  struct PendingPacket {
    Packet packet;
    float elapsed = 0.0f;
    uint32_t transmissions = 1;
  };
  void acknowledge(uint32_t ack, uint32_t ackBits);
  uint32_t localSequence_ = 1;
  uint32_t remoteSequence_ = 0;
  uint32_t ackBits_ = 0;
  bool hasRemoteSequence_ = false;
  std::vector<PendingPacket> pending_;
};

// Wire security & replay protection helpers (usable directly or by Server/Client).
struct ReplayWindow {
  uint32_t highestSeq = 0;
  uint64_t mask = 0;
  bool initialized = false;

  bool accept(uint32_t sequence);
  void reset() {
    highestSeq = 0;
    mask = 0;
    initialized = false;
  }
};

// Snapshot: a flat state blob per entity, replicated to clients.
struct EntitySnapshot {
  uint32_t entityId = 0;
  math::Vec3 position{0, 0, 0};
  math::Quat rotation{0, 0, 0, 1};
  math::Vec3 velocity{0, 0, 0};
  uint32_t stateFlags = 0;
};

class SnapshotBuffer {
 public:
  void capture(uint32_t entityId, const math::Vec3& position, const math::Quat& rotation,
               const math::Vec3& velocity, uint32_t flags);
  const std::vector<EntitySnapshot>& snapshots() const { return snapshots_; }
  void clear() { snapshots_.clear(); }
  // Delta-encodes against a baseline; returns bytes written.
  uint32_t encodeDelta(const SnapshotBuffer& baseline, uint8_t* out, uint32_t capacity) const;
  bool decodeDelta(const uint8_t* data, uint32_t size);

  static SnapshotBuffer interpolate(const SnapshotBuffer& from, const SnapshotBuffer& to,
                                    float alpha);

 private:
  std::vector<EntitySnapshot> snapshots_;
};

// Client prediction: applies local input immediately and reconciles against
// the authoritative server snapshot.
class Prediction {
 public:
  void applyInput(uint32_t tick, const math::Vec3& move);
  // Returns the correction to apply, if the server state disagrees.
  bool reconcile(uint32_t serverTick, const math::Vec3& serverPosition, math::Vec3* correction);
  const math::Vec3& predicted() const { return predicted_; }
  uint32_t lastProcessedTick() const { return lastProcessedTick_; }

 private:
  math::Vec3 predicted_{0, 0, 0};
  std::vector<std::pair<uint32_t, math::Vec3>> pendingInputs_;
  uint32_t lastProcessedTick_ = 0;
};

// Server: accepts clients, ticks the simulation, broadcasts snapshots.
class Server {
 public:
  explicit Server(const Config& config = Config{}) : config_(config) {}
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  bool start(uint16_t port);
  void stop();
  void tick(float deltaSeconds);
  uint32_t clientCount() const { return clientCount_; }
  uint16_t port() const { return port_; }
  uint32_t rejectedPackets() const { return rejectedPackets_; }
  void broadcastSnapshot(const SnapshotBuffer& snapshot);

 private:
  Config config_;
  Socket* socket_ = nullptr;
  struct Peer {
    std::string address;
    uint16_t port = 0;
    float lastSeen = 0.0f;
    uint32_t txSeq = 1;
    ReplayWindow rxReplay;
    Reliability reliability;
  };
  std::vector<Peer> peers_;
  float clock_ = 0.0f;
  float accumulator_ = 0.0f;
  uint32_t clientCount_ = 0;
  uint32_t rejectedPackets_ = 0;
  uint16_t port_ = 0;
  bool running_ = false;
};

// Client: connects, predicts, interpolates remote entities.
class Client {
 public:
  explicit Client(const Config& config = Config{}) : config_(config) {}
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  bool connect(const std::string& address, uint16_t port);
  void disconnect();
  void tick(float deltaSeconds);
  bool connected() const { return connected_; }
  uint32_t rejectedPackets() const { return rejectedPackets_; }
  const SnapshotBuffer& interpolated() const { return interpolated_; }

 private:
  Config config_;
  Socket* socket_ = nullptr;
  std::string address_;
  uint16_t port_ = 0;
  bool connected_ = false;
  bool handshaking_ = false;
  float timeSinceLastPacket_ = 0.0f;
  float timeSinceLastSend_ = 0.0f;
  uint32_t txSeq_ = 1;
  uint32_t rejectedPackets_ = 0;
  ReplayWindow rxReplay_;
  Reliability reliability_;
  SnapshotBuffer prevSnapshot_;
  SnapshotBuffer interpolated_;
  Prediction prediction_;
};

}  // namespace nimbus::net
