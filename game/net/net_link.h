#pragma once
// Joins a GameSession to the network. The host runs the real world and decides everything; a client sends
// its controls, walks ahead of the replies so its own legs feel immediate, and is corrected if it disagrees.
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/net/net_protocol.h"
#include "game/net/net_transport.h"
#include "game/session/game_session.h"

namespace fh::net {

class HostLink {
 public:
  explicit HostLink(GameSession& session) : session_(&session) {}
  bool start(std::uint16_t port);
  // Each simulation step: first takes what clients sent, then, after the step, tells them what happened.
  void preStep();
  void postStep();
  // Also once per frame, so joining and leaving work while the simulation holds still.
  void pump();
  // Helm commands from remote players, for the app to run, since it owns that logic.
  std::vector<std::pair<PlayerId, std::string>> takeHelmCommands() { return std::exchange(helmCommands_, {}); }
  int clients() const { return int(players_.size()); }

 private:
  void handle(const TransportEvent& e);
  SnapshotMsg buildSnapshot();

  GameSession* session_;
  NetServer server_;
  std::map<std::uint32_t, PlayerId> players_;  // by transport peer
  std::map<PlayerId, ClientInputMsg> inputs_;
  std::map<PlayerId, std::uint32_t> noticeSerial_;
  std::map<PlayerId, double> noticeSeen_;
  std::vector<std::pair<PlayerId, std::string>> helmCommands_;
  std::uint32_t tick_ = 0;
};

class ClientLink {
 public:
  explicit ClientLink(GameSession& session) : session_(&session) {}
  bool start(const std::string& address, std::uint16_t port, const std::string& name);
  void preStep();             // sends our controls
  void postStep(float dt);    // moves everything towards what the host last said
  void pump();
  void sendCommand(const std::string& name);

  bool joined() const { return yourId_ != 0; }
  bool lost() const { return lost_; }
  const std::string& failure() const { return failure_; }

 private:
  void handle(const TransportEvent& e);
  void apply(const SnapshotMsg& s);

  struct Remote {
    Vec3 feet{};
    float yaw = 0, pitch = 0;
  };

  GameSession* session_;
  NetClient client_;
  std::string name_;
  std::uint32_t yourId_ = 0;
  bool lost_ = false;
  std::string failure_;
  std::uint32_t lastTick_ = 0;
  std::map<std::uint32_t, PlayerId> locals_;   // host's id -> this machine's id for the same person
  std::map<PlayerId, Remote> remotes_;
  std::uint32_t noticeSerial_ = 0;
  // The ship, between the host's reports.
  bool haveShip_ = false;
  DVec3 shipFrom_{}, shipTo_{};
  Quat rotFrom_{}, rotTo_{};
  Vec3 shipVelocity_{};
  float shipAlpha_ = 1;
};

}  // namespace fh::net
