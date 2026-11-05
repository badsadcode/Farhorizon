#pragma once
// A game with nobody sitting at the host's keyboard: loads the content with no graphics, runs the world while
// somebody is connected, and saves it when it stops. The server program's window is one way to drive this; the
// tests are another.
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/net/net_link.h"
#include "game/net/net_protocol.h"
#include "game/session/game_session.h"

namespace fh {
struct ProgressionContent;
}

namespace fh::net {

struct ServerConfig {
  std::string name = "Farhorizon server";
  std::uint16_t port = kDefaultPort;
  std::string password;  // required
  int maxPlayers = 4;
};

struct ServerPlayer {
  PlayerId id = 0;
  std::string name;
  std::string where;
  bool pilot = false;
};

class DedicatedServer {
 public:
  explicit DedicatedServer(std::filesystem::path contentDir);
  ~DedicatedServer();

  // Returns false and says why. `savePath` is loaded if it exists, and written on stop().
  bool start(const ServerConfig& config, const std::filesystem::path& savePath, std::string* error);
  void stop();
  bool running() const { return session_ != nullptr; }
  void update(double seconds);  // call as often as you like: the world advances in fixed steps

  bool saveNow(std::string* error);
  std::vector<ServerPlayer> players() const;
  void kick(PlayerId id);
  bool simulating() const { return link_ && link_->clients() > 0; }  // the world pauses when nobody is here
  double worldTime() const;
  GameSession* session() { return session_.get(); }

 private:
  std::filesystem::path contentDir_, savePath_;
  std::string sceneId_ = "scene.stage2_flight";
  std::unique_ptr<ProgressionContent> progression_;
  std::unique_ptr<GameSession> session_;
  std::unique_ptr<HostLink> link_;
  double accumulator_ = 0;
};

}  // namespace fh::net
