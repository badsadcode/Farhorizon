#pragma once
// Joins a GameSession to the network. The host runs the real world and decides everything; a client sends
// its controls, walks ahead of the replies so its own legs feel immediate, and is corrected if it disagrees.
#include <cstdint>
#include <deque>
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
  void setPassword(const std::string& password) { password_ = password; }  // empty: anyone may join
  void setMaxPlayers(int n) { maxPlayers_ = n; }                           // not counting a host at the keyboard
  void kick(PlayerId who, const std::string& reason);
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
  std::map<PlayerId, std::uint32_t> acknowledged_;
  double shipMessageTime_ = -1;
  std::string shipMessage_;
  std::uint32_t shipMessageSerial_ = 0;
  std::vector<std::pair<PlayerId, std::string>> helmCommands_;
  std::uint32_t tick_ = 0;
  std::string password_;
  int maxPlayers_ = GameSession::kMaxPlayers - 1;
  std::map<std::uint32_t, std::uint64_t> nonces_;  // by transport peer, until they have said hello
};

class ClientLink {
 public:
  explicit ClientLink(GameSession& session) : session_(&session) {}
  bool start(const std::string& address, std::uint16_t port, const std::string& name, const std::string& password = {});
  void preStep();             // sends our controls
  void postStep(float dt);    // moves everything towards what the host last said
  void pump();
  void sendCommand(const std::string& name);
  // For tests and trying things out: pretend the network is this slow, in simulation steps each way.
  void setSimulatedLag(int stepsEachWay, int jitterSteps);

  bool joined() const { return yourId_ != 0; }
  bool lost() const { return lost_; }
  const std::string& failure() const { return failure_; }

 private:
  void handle(const TransportEvent& e);
  void apply(const SnapshotMsg& s);
  void send(const Bytes& data, Delivery how);
  void showWorldAsOf(double tick, float dt);

  struct Remote {
    Vec3 feet{};
    float yaw = 0, pitch = 0;
  };
  // One of the host's reports, kept so the world can be shown a moment behind, moving evenly.
  struct Report {
    std::uint32_t tick = 0;
    DVec3 ship{};
    Quat rotation{};
    Vec3 velocity{};
    std::map<PlayerId, Remote> people;
  };
  struct Past {  // where our own legs had us when we had sent a given input
    std::uint32_t sequence = 0;
    Vec3 feet{};
  };
  struct Delayed {
    int due = 0;
    TransportEvent event;
  };
  struct DelayedSend {
    int due = 0;
    Bytes data;
    Delivery how = Delivery::Reliable;
  };

  GameSession* session_;
  NetClient client_;
  std::string name_, password_;
  int stepsWaiting_ = 0;
  std::uint32_t yourId_ = 0;
  bool lost_ = false;
  std::string failure_;
  std::uint32_t lastTick_ = 0;
  std::map<std::uint32_t, PlayerId> locals_;   // host's id -> this machine's id for the same person
  std::map<PlayerId, Remote> remotes_;
  std::uint32_t noticeSerial_ = 0;
  std::uint32_t shipMessageSerial_ = 0;
  FlightInput pilotControls_;
  std::deque<Report> reports_;
  double showTick_ = -1;
  std::uint32_t inputSequence_ = 0;
  std::deque<Past> past_;
  Vec3 correction_{};  // how far our legs are from where the host says, being eased away
  std::vector<std::pair<std::string, int>> lastCargo_;
  int lagSteps_ = 0, lagJitter_ = 0, stepCount_ = 0, lastDueIn_ = 0, lastDueOut_ = 0;
  std::uint32_t lagSeed_ = 12345;
  std::deque<Delayed> delayedIn_;
  std::deque<DelayedSend> delayedOut_;
};

}  // namespace fh::net
