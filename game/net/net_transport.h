#pragma once
// A thin layer over ENet (UDP with optional reliability), so nothing else in the game sees it.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/net/net_protocol.h"

namespace fh::net {

struct TransportEvent {
  enum class Type { Connected, Data, Disconnected } type = Type::Data;
  std::uint32_t peer = 0;  // the server's handle for a client; 0 on a client
  Bytes data;
};

enum class Delivery { Reliable, Latest };  // Latest: may be lost, and old ones are dropped

class NetServer {
 public:
  NetServer();
  ~NetServer();
  bool start(std::uint16_t port, int maxClients);
  void stop();
  bool running() const;
  std::vector<TransportEvent> poll();
  void send(std::uint32_t peer, const Bytes& data, Delivery how);
  void kick(std::uint32_t peer);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class NetClient {
 public:
  NetClient();
  ~NetClient();
  bool connect(const std::string& address, std::uint16_t port);
  void disconnect();
  bool connected() const;
  std::vector<TransportEvent> poll();
  void send(const Bytes& data, Delivery how);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fh::net
