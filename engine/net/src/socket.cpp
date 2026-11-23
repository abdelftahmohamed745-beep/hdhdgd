// POSIX UDP socket used by both desktop and Android builds.
#include "nimbus/net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace nimbus::net {

class UdpSocket final : public Socket {
 public:
  ~UdpSocket() override { close(); }

  bool open(uint16_t port) override {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    return true;
  }

  void close() override {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  bool send(const char* address, uint16_t port, const void* data, uint32_t size) override {
    if (fd_ < 0) return false;
    if (!address || !data || size == 0) return false;
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    if (::inet_pton(AF_INET, address, &destination.sin_addr) != 1) return false;
    const ssize_t sent =
        ::sendto(fd_, data, size, 0, reinterpret_cast<sockaddr*>(&destination),
                 sizeof(destination));
    return sent == static_cast<ssize_t>(size);
  }

  int32_t receive(void* buffer, uint32_t capacity, std::string* fromAddress,
                  uint16_t* fromPort) override {
    if (fd_ < 0) return -1;
    sockaddr_in sender{};
    socklen_t length = sizeof(sender);
    const ssize_t received =
        ::recvfrom(fd_, buffer, capacity, 0, reinterpret_cast<sockaddr*>(&sender), &length);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
      return -1;
    }
    if (fromAddress) {
      char text[INET_ADDRSTRLEN] = {};
      ::inet_ntop(AF_INET, &sender.sin_addr, text, sizeof(text));
      *fromAddress = text;
    }
    if (fromPort) *fromPort = ntohs(sender.sin_port);
    return static_cast<int32_t>(received);
  }

  uint16_t localPort() const override {
    if (fd_ < 0) return 0;
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) return 0;
    return ntohs(address.sin_port);
  }

  bool valid() const override { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

Socket* createUdpSocket() { return new UdpSocket(); }

}  // namespace nimbus::net
