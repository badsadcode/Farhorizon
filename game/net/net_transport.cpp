#include "game/net/net_transport.h"

#include <enet/enet.h>

#include <map>
#include <mutex>

#include "engine/core/log.h"

namespace fh::net {

namespace {

constexpr int kChannels = 2;  // 0 reliable, 1 latest-only

// ENet's global set-up, shared by however many servers and clients exist (a test has both).
std::mutex initMutex;
int initCount = 0;

bool acquireEnet() {
  std::lock_guard<std::mutex> lock(initMutex);
  if (initCount == 0 && enet_initialize() != 0) return false;
  ++initCount;
  return true;
}

void releaseEnet() {
  std::lock_guard<std::mutex> lock(initMutex);
  if (--initCount == 0) enet_deinitialize();
}

ENetPacket* makePacket(const Bytes& data, Delivery how) {
  return enet_packet_create(data.data(), data.size(), how == Delivery::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

int channelOf(Delivery how) { return how == Delivery::Reliable ? 0 : 1; }

}  // namespace

// ---- Server ------------------------------------------------------------------------------------------

struct NetServer::Impl {
  ENetHost* host = nullptr;
  bool haveEnet = false;
  std::uint32_t nextPeer = 1;
  std::map<std::uint32_t, ENetPeer*> peers;
};

NetServer::NetServer() : impl_(std::make_unique<Impl>()) {}
NetServer::~NetServer() { stop(); }

bool NetServer::start(std::uint16_t port, int maxClients) {
  stop();
  impl_->haveEnet = acquireEnet();
  if (!impl_->haveEnet) return false;
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  impl_->host = enet_host_create(&address, size_t(maxClients), kChannels, 0, 0);
  if (!impl_->host) log::error("net", "could not open UDP port {}", port);
  return impl_->host != nullptr;
}

void NetServer::stop() {
  if (impl_->host) {
    for (auto& [id, peer] : impl_->peers) enet_peer_disconnect_now(peer, 0);
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
    impl_->host = nullptr;
  }
  impl_->peers.clear();
  if (impl_->haveEnet) releaseEnet();
  impl_->haveEnet = false;
}

bool NetServer::running() const { return impl_->host != nullptr; }

std::vector<TransportEvent> NetServer::poll() {
  std::vector<TransportEvent> out;
  if (!impl_->host) return out;
  ENetEvent e;
  while (enet_host_service(impl_->host, &e, 0) > 0) {
    switch (e.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        const std::uint32_t id = impl_->nextPeer++;
        impl_->peers[id] = e.peer;
        e.peer->data = reinterpret_cast<void*>(std::uintptr_t(id));
        out.push_back({TransportEvent::Type::Connected, id, {}});
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        const auto id = std::uint32_t(reinterpret_cast<std::uintptr_t>(e.peer->data));
        out.push_back({TransportEvent::Type::Data, id, Bytes(e.packet->data, e.packet->data + e.packet->dataLength)});
        enet_packet_destroy(e.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        const auto id = std::uint32_t(reinterpret_cast<std::uintptr_t>(e.peer->data));
        impl_->peers.erase(id);
        out.push_back({TransportEvent::Type::Disconnected, id, {}});
        break;
      }
      default:
        break;
    }
  }
  return out;
}

void NetServer::send(std::uint32_t peer, const Bytes& data, Delivery how) {
  auto it = impl_->peers.find(peer);
  if (it == impl_->peers.end()) return;
  enet_peer_send(it->second, enet_uint8(channelOf(how)), makePacket(data, how));
}

void NetServer::kick(std::uint32_t peer) {
  auto it = impl_->peers.find(peer);
  if (it != impl_->peers.end()) enet_peer_disconnect_later(it->second, 0);
}

// ---- Client ------------------------------------------------------------------------------------------

struct NetClient::Impl {
  ENetHost* host = nullptr;
  ENetPeer* peer = nullptr;
  bool haveEnet = false;
  bool connected = false;
};

NetClient::NetClient() : impl_(std::make_unique<Impl>()) {}
NetClient::~NetClient() { disconnect(); }

bool NetClient::connect(const std::string& address, std::uint16_t port) {
  disconnect();
  impl_->haveEnet = acquireEnet();
  if (!impl_->haveEnet) return false;
  impl_->host = enet_host_create(nullptr, 1, kChannels, 0, 0);
  if (!impl_->host) return false;
  ENetAddress a{};
  if (enet_address_set_host(&a, address.c_str()) != 0) {
    log::error("net", "could not find host '{}'", address);
    return false;
  }
  a.port = port;
  impl_->peer = enet_host_connect(impl_->host, &a, kChannels, 0);
  return impl_->peer != nullptr;
}

void NetClient::disconnect() {
  if (impl_->host) {
    if (impl_->peer) enet_peer_disconnect_now(impl_->peer, 0);
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
    impl_->host = nullptr;
    impl_->peer = nullptr;
  }
  impl_->connected = false;
  if (impl_->haveEnet) releaseEnet();
  impl_->haveEnet = false;
}

bool NetClient::connected() const { return impl_->connected; }

std::vector<TransportEvent> NetClient::poll() {
  std::vector<TransportEvent> out;
  if (!impl_->host) return out;
  ENetEvent e;
  while (enet_host_service(impl_->host, &e, 0) > 0) {
    switch (e.type) {
      case ENET_EVENT_TYPE_CONNECT:
        impl_->connected = true;
        out.push_back({TransportEvent::Type::Connected, 0, {}});
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        out.push_back({TransportEvent::Type::Data, 0, Bytes(e.packet->data, e.packet->data + e.packet->dataLength)});
        enet_packet_destroy(e.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        impl_->connected = false;
        impl_->peer = nullptr;
        out.push_back({TransportEvent::Type::Disconnected, 0, {}});
        break;
      default:
        break;
    }
  }
  return out;
}

void NetClient::send(const Bytes& data, Delivery how) {
  if (!impl_->peer || !impl_->connected) return;
  enet_peer_send(impl_->peer, enet_uint8(channelOf(how)), makePacket(data, how));
}

}  // namespace fh::net
