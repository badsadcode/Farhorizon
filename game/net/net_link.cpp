#include "game/net/net_link.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <random>

#include "engine/core/log.h"
#include "game/net/sha256.h"

namespace fh::net {

namespace {
constexpr int kStepsPerSnapshot = 3;           // 20 reports a second at 60 steps a second
constexpr float kSecondsPerSnapshot = 3.0f / 60.0f;
constexpr float kOwnBodyTolerance = 0.5f;      // walking ahead of the host by more than this gets corrected
}  // namespace

// ---- Host --------------------------------------------------------------------------------------------

bool HostLink::start(std::uint16_t port) {
  // One more than the limit, so that the person over it can be told why rather than left timing out.
  if (!server_.start(port, maxPlayers_ + 1)) return false;
  session_->setNetworked(true);
  log::info("net", "hosting on UDP port {}", port);
  return true;
}

void HostLink::kick(PlayerId who, const std::string& reason) {
  for (const auto& [peer, id] : players_)
    if (id == who) {
      server_.send(peer, encode(RefusedMsg{reason}), Delivery::Reliable);
      server_.kick(peer);
      return;
    }
}

void HostLink::handle(const TransportEvent& e) {
  if (e.type == TransportEvent::Type::Connected) {
    static std::random_device entropy;
    const std::uint64_t nonce = (std::uint64_t(entropy()) << 32) | entropy();
    nonces_[e.peer] = nonce;
    server_.send(e.peer, encode(ChallengeMsg{nonce}), Delivery::Reliable);
    return;
  }
  if (e.type == TransportEvent::Type::Disconnected) {
    nonces_.erase(e.peer);
    auto it = players_.find(e.peer);
    if (it == players_.end()) return;
    const CrewMember* m = session_->player(it->second);
    const std::string name = m ? m->name : std::string("Someone");
    inputs_.erase(it->second);
    noticeSerial_.erase(it->second);
    noticeSeen_.erase(it->second);
    session_->removePlayer(it->second);
    players_.erase(it);
    session_->notify(name + " left the ship");
    return;
  }
  if (e.type != TransportEvent::Type::Data) return;
  const auto type = peekType(e.data.data(), e.data.size());
  if (!type) return;  // not ours: ignored, like everything else malformed below
  auto known = players_.find(e.peer);
  if (*type == MessageType::Hello) {
    if (known != players_.end()) return;
    const auto hello = decodeHello(e.data.data(), e.data.size());
    if (!hello) return;
    auto refuse = [&](const std::string& why) {
      server_.send(e.peer, encode(RefusedMsg{why}), Delivery::Reliable);
      server_.kick(e.peer);
    };
    if (hello->protocol != kProtocolVersion) return refuse("Your version of the game does not match the host's");
    auto nonce = nonces_.find(e.peer);
    if (nonce == nonces_.end()) return;
    const Digest expected = passwordProof(nonce->second, password_);
    nonces_.erase(nonce);  // one try per connection
    if (!password_.empty() && hello->proof != expected) {
      log::warn("net", "someone tried to join as '{}' with the wrong password", hello->name);
      return refuse("Wrong password");
    }
    if (clients() >= maxPlayers_) return refuse("The ship's crew is full");
    const PlayerId id = session_->addPlayer(hello->name);
    if (id == 0) return refuse("The ship's crew is full");
    log::info("net", "{} joined", session_->player(id)->name);
    players_[e.peer] = id;
    inputs_[id] = ClientInputMsg{};
    server_.send(e.peer, encode(WelcomeMsg{id}), Delivery::Reliable);
    session_->notify(session_->player(id)->name + " came aboard");
    return;
  }
  if (known == players_.end()) return;  // nothing else counts until they have said hello
  const PlayerId who = known->second;
  if (*type == MessageType::ClientInput) {
    if (const auto in = decodeClientInput(e.data.data(), e.data.size())) {
      const bool jump = inputs_[who].jump || in->jump;  // kept until a step has used it
      inputs_[who] = *in;
      inputs_[who].jump = jump;
    }
  } else if (*type == MessageType::Command) {
    const auto cmd = decodeCommand(e.data.data(), e.data.size());
    if (!cmd) return;
    if (cmd->name == "interact") {
      session_->interact(who);  // checked against where the host says they are standing
      if (CrewMember* m = session_->player(who); m && m->marketRequested) {
        m->marketRequested = false;
        session_->notifyPlayer(who, "Terminals only work for the host so far");
      }
    } else if (const CrewMember* m = session_->player(who); m && m->location == Location::Cockpit && m->pilot) {
      helmCommands_.emplace_back(who, cmd->name);
    } else {
      session_->notifyPlayer(who, "Only the pilot can do that");
    }
  }
}

void HostLink::pump() {
  for (const TransportEvent& e : server_.poll()) handle(e);
}

void HostLink::preStep() {
  pump();
  for (auto& [who, in] : inputs_) {
    CrewMember* m = session_->player(who);
    if (!m) continue;
    if (m->location == Location::Cockpit) {
      if (m->pilot) session_->world().player().input = in.flight;
    } else {
      m->input.move = in.move;
      m->input.run = in.run;
      m->input.jump = in.jump;
      m->character.yaw = in.yaw;
      m->character.pitch = in.pitch;
    }
    in.jump = false;
  }
}

SnapshotMsg HostLink::buildSnapshot() {
  SpaceWorld& w = session_->world();
  SnapshotMsg s;
  s.tick = tick_;
  s.time = w.simulationTime();
  s.shipPosition = w.frame().toSystem(w.player().state.position).metres;
  s.shipOrientation = w.player().state.orientation;
  s.shipVelocity = w.player().state.velocity;
  const Docking& d = session_->docking();
  s.docked = d.state() == DockState::Docked && d.station().has_value();
  if (s.docked) {
    s.dockProp = *d.station();
    s.dockBay = d.bay();
  }
  s.hull = session_->combat().player().hull;
  s.shield = session_->combat().player().shield;
  if (const auto& t = w.target()) {
    s.targetKind = std::int32_t(t->kind);
    s.targetIndex = t->index;
    s.targetSub = t->sub;
  }
  for (const CrewMember& m : session_->crew()) {
    if (session_->dedicated() && m.id == session_->localPlayer()) continue;  // a server is nobody
    CrewStateMsg c;
    c.id = m.id;
    c.name = m.name;
    c.location = std::uint8_t(m.location);
    c.interior = m.interior;
    c.pilot = m.pilot;
    c.feet = m.character.feet;
    c.yaw = m.character.yaw;
    c.pitch = m.character.pitch;
    if (const FootPlayer* f = session_->footPlayer(m.id)) c.health = f->health;
    c.credits = m.credits;
    if (m.noticeTime != noticeSeen_[m.id]) {
      noticeSeen_[m.id] = m.noticeTime;
      ++noticeSerial_[m.id];
    }
    c.notice = m.notice;
    c.noticeSerial = noticeSerial_[m.id];
    s.crew.push_back(std::move(c));
  }
  return s;
}

void HostLink::postStep() {
  if (++tick_ % kStepsPerSnapshot != 0 || players_.empty()) return;
  const Bytes bytes = encode(buildSnapshot());
  for (const auto& [peer, who] : players_) server_.send(peer, bytes, Delivery::Latest);
}

// ---- Client ------------------------------------------------------------------------------------------

bool ClientLink::start(const std::string& address, std::uint16_t port, const std::string& name, const std::string& password) {
  name_ = name;
  password_ = password;
  session_->setNetworked(true);
  session_->setNetClient(true);
  if (!client_.connect(address, port)) {
    failure_ = "Could not reach " + address;
    lost_ = true;
    return false;
  }
  log::info("net", "connecting to {}:{}", address, port);
  return true;
}

void ClientLink::handle(const TransportEvent& e) {
  switch (e.type) {
    case TransportEvent::Type::Connected:
      break;  // the server speaks first
    case TransportEvent::Type::Disconnected:
      lost_ = true;
      if (failure_.empty()) failure_ = "Lost the connection to the host";
      break;
    case TransportEvent::Type::Data: {
      const auto type = peekType(e.data.data(), e.data.size());
      if (!type) return;
      if (*type == MessageType::Challenge) {
        if (const auto c = decodeChallenge(e.data.data(), e.data.size())) {
          HelloMsg hello;
          hello.name = name_;
          hello.proof = passwordProof(c->nonce, password_);
          client_.send(encode(hello), Delivery::Reliable);
        }
      } else if (*type == MessageType::Welcome) {
        if (const auto w = decodeWelcome(e.data.data(), e.data.size())) {
          yourId_ = w->yourId;
          locals_[yourId_] = session_->localPlayer();
          session_->notify("Aboard. Connected to the host.");
          log::info("net", "joined as player {}", yourId_);
        }
      } else if (*type == MessageType::Refused) {
        if (const auto r = decodeRefused(e.data.data(), e.data.size())) failure_ = r->reason;
        lost_ = true;
      } else if (*type == MessageType::Snapshot) {
        if (const auto s = decodeSnapshot(e.data.data(), e.data.size())) apply(*s);
      }
      break;
    }
  }
}

void ClientLink::pump() {
  for (const TransportEvent& e : client_.poll()) handle(e);
}

void ClientLink::preStep() {
  pump();
  if (!joined()) return;
  const CrewMember* me = session_->player(session_->localPlayer());
  if (!me) return;
  ClientInputMsg in;
  in.move = me->input.move;
  in.run = me->input.run;
  in.jump = me->input.jump;
  in.yaw = me->character.yaw + me->input.yawDelta;
  in.pitch = std::clamp(me->character.pitch + me->input.pitchDelta, -1.55f, 1.55f);
  in.flight = session_->world().player().input;
  client_.send(encode(in), Delivery::Latest);
}

void ClientLink::sendCommand(const std::string& name) {
  client_.send(encode(CommandMsg{name}), Delivery::Reliable);
}

void ClientLink::apply(const SnapshotMsg& s) {
  if (s.tick <= lastTick_ && lastTick_ != 0) return;  // an old one that arrived late
  lastTick_ = s.tick;
  SpaceWorld& w = session_->world();

  // The ship: glide from where we show it now to where the host says it is.
  if (!haveShip_) {
    shipFrom_ = s.shipPosition;
    rotFrom_ = s.shipOrientation;
    haveShip_ = true;
  } else {
    shipFrom_ = w.frame().toSystem(w.player().state.position).metres;
    rotFrom_ = w.player().state.orientation;
  }
  shipTo_ = s.shipPosition;
  rotTo_ = s.shipOrientation;
  shipVelocity_ = s.shipVelocity;
  shipAlpha_ = 0;

  session_->netApplyShipStatus(s.docked, s.dockProp, s.dockBay, s.hull, s.shield);
  if (s.targetKind < 0) {
    w.clearTarget();
  } else {
    w.setTarget({TargetRef::Kind(s.targetKind), s.targetIndex, s.targetSub});
  }

  // People: whoever is not in this report has left, and anyone new gets a place here.
  std::vector<std::uint32_t> gone;
  for (const auto& [hostId, local] : locals_) {
    const bool present = std::any_of(s.crew.begin(), s.crew.end(), [&](const CrewStateMsg& c) { return c.id == hostId; });
    if (!present && hostId != yourId_) gone.push_back(hostId);
  }
  for (std::uint32_t hostId : gone) {
    session_->removePlayer(locals_[hostId]);
    remotes_.erase(locals_[hostId]);
    locals_.erase(hostId);
  }
  for (const CrewStateMsg& c : s.crew) {
    if (!locals_.count(c.id)) {
      if (c.id == yourId_) continue;
      const PlayerId local = session_->addPlayer(c.name);
      if (local == 0) continue;
      locals_[c.id] = local;
      remotes_[local] = {c.feet, c.yaw, c.pitch};
    }
    const PlayerId local = locals_[c.id];
    const bool self = c.id == yourId_;
    CrewMember* m = session_->player(local);
    if (!m) continue;
    const Location loc = Location(std::min<std::uint8_t>(c.location, std::uint8_t(Location::StationInterior)));
    const bool moved = m->location != loc || (loc != Location::Cockpit && m->interior != c.interior);
    session_->netApplyPlace(local, loc, c.interior, c.pilot);
    m = session_->player(local);
    if (moved) {
      m->character.feet = c.feet;
      m->character.velocity = {};
      m->character.yaw = c.yaw;
      m->character.pitch = c.pitch;
    }
    if (FootPlayer* f = session_->footPlayer(local)) f->health = c.health;
    m->credits = c.credits;
    if (self) {
      // Our own legs run ahead of the host. Only a real disagreement is corrected.
      if (loc != Location::Cockpit && length(m->character.feet - c.feet) > kOwnBodyTolerance) {
        m->character.feet = c.feet;
        m->character.velocity = {};
      }
      if (c.noticeSerial != noticeSerial_) {
        noticeSerial_ = c.noticeSerial;
        if (!c.notice.empty()) session_->notify(c.notice);
      }
    } else {
      remotes_[local] = {c.feet, c.yaw, c.pitch};
    }
  }
}

void ClientLink::postStep(float dt) {
  SpaceWorld& w = session_->world();
  if (haveShip_) {
    shipAlpha_ = std::min(1.0f, shipAlpha_ + dt / kSecondsPerSnapshot);
    const DVec3 at = shipFrom_ + (shipTo_ - shipFrom_) * double(shipAlpha_);
    PlayerShip& ship = w.player();
    ship.state.position = w.frame().toLocal(SystemPosition{at});
    ship.state.orientation = nlerp(rotFrom_, rotTo_, shipAlpha_);
    ship.state.velocity = shipVelocity_;  // for the speed readouts; the next report sets the position again
  }
  const float follow = std::min(1.0f, dt * 15.0f);
  for (auto& [local, r] : remotes_) {
    CrewMember* m = session_->player(local);
    if (!m) continue;
    m->character.velocity = {};
    m->character.feet = m->character.feet + (r.feet - m->character.feet) * follow;
    m->character.yaw = r.yaw;
    m->character.pitch = r.pitch;
    m->input = CharacterInput{};
  }
}

}  // namespace fh::net
