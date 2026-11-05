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
constexpr float kSnapDistance = 2.5f;          // further off than this and easing would only look worse
constexpr float kIgnoreDistance = 0.04f;       // too small to matter
constexpr double kShowBehindTicks = 7;         // the world is shown this far in the past, to hide uneven arrival
constexpr double kMaxExtrapolateTicks = 20;
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
    log::info("net", "connection from {}", e.address);
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
        server_.send(e.peer, encode(CommandMsg{"open.terminal"}), Delivery::Reliable);
      }
    } else if (cmd->name.rfind("trade:", 0) == 0) {
      // trade:<commodity>:<units>. Whoever asks pays, and the market has the final say.
      const size_t colon = cmd->name.rfind(':');
      if (colon <= 6) return;
      const std::string commodity = cmd->name.substr(6, colon - 6);
      const int units = std::clamp(std::atoi(cmd->name.c_str() + colon + 1), -1000, 1000);
      if (units == 0) return;
      const TradeResult r = session_->trade(who, commodity, units);
      const CommodityDef* def = session_->commodities().find(commodity);
      const std::string goods = def ? def->name : commodity;
      session_->notifyPlayer(who, r.units > 0 ? std::format("{} {} x {} for {} cr", units > 0 ? "Bought" : "Sold", r.units, goods, r.credits)
                                              : (r.message.empty() ? std::string("No trade") : r.message));
    } else if (session_->tryTakeHelm(who)) {
      helmCommands_.emplace_back(who, cmd->name);
    } else {
      const CrewMember* m = session_->player(who);
      log::info("net", "{} asked for '{}' without the helm", m ? m->name : std::string("?"), cmd->name);
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
    acknowledged_[who] = in.sequence;
    if (m->location == Location::Cockpit) {
      const FlightInput& f = in.flight;
      // An empty helm is anyone's: touching the controls from a seat takes it.
      if (!m->pilot && (f.throttle != 0 || length(f.thrust) > 0 || length(f.rotation) > 0.05f)) session_->tryTakeHelm(who);
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
    c.inputSequence = acknowledged_[m.id];
    s.crew.push_back(std::move(c));
  }
  for (const auto& [id, units] : session_->cargo().contents()) s.cargo.emplace_back(id, units);
  if (Market* market = session_->dockedMarket())
    for (const int stock : market->stocks()) s.marketStock.push_back(stock);
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
      if (failure_.empty())
        failure_ = joined() ? "The connection to the server was lost."
                            : "Could not reach the server.\n\nCheck the address and the port, that the server is running, "
                              "and that its router forwards that UDP port to it.";
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
      } else if (*type == MessageType::Command) {
        if (const auto c = decodeCommand(e.data.data(), e.data.size()); c && c->name == "open.terminal")
          if (CrewMember* me = session_->player(session_->localPlayer())) me->marketRequested = true;
      }
      break;
    }
  }
}

void ClientLink::setSimulatedLag(int stepsEachWay, int jitterSteps) {
  lagSteps_ = std::max(0, stepsEachWay);
  lagJitter_ = std::max(0, jitterSteps);
}

void ClientLink::send(const Bytes& data, Delivery how) {
  if (lagSteps_ == 0 && lagJitter_ == 0) {
    client_.send(data, how);
    return;
  }
  lagSeed_ = lagSeed_ * 1664525u + 1013904223u;
  const int extra = lagJitter_ ? int((lagSeed_ >> 16) % std::uint32_t(lagJitter_ + 1)) : 0;
  lastDueOut_ = std::max(lastDueOut_, stepCount_ + lagSteps_ + extra);  // a network may delay, but ENet keeps order
  delayedOut_.push_back({lastDueOut_, data, how});
}

void ClientLink::pump() {
  for (TransportEvent& e : client_.poll()) {
    if (lagSteps_ == 0 && lagJitter_ == 0) {
      handle(e);
      continue;
    }
    lagSeed_ = lagSeed_ * 1664525u + 1013904223u;
    const int extra = lagJitter_ ? int((lagSeed_ >> 16) % std::uint32_t(lagJitter_ + 1)) : 0;
    lastDueIn_ = std::max(lastDueIn_, stepCount_ + lagSteps_ + extra);
    delayedIn_.push_back({lastDueIn_, std::move(e)});
  }
  while (!delayedIn_.empty() && delayedIn_.front().due <= stepCount_) {
    const Delayed d = std::move(delayedIn_.front());
    delayedIn_.pop_front();
    handle(d.event);
  }
  while (!delayedOut_.empty() && delayedOut_.front().due <= stepCount_) {
    client_.send(delayedOut_.front().data, delayedOut_.front().how);
    delayedOut_.pop_front();
  }
}

void ClientLink::preStep() {
  ++stepCount_;
  pump();
  if (!joined()) {
    if (!lost_ && ++stepsWaiting_ > 60 * 12) {  // the network library alone would wait half a minute
      lost_ = true;
      failure_ = "Could not reach the server.\n\nCheck the address and the port, that the server is running, "
                 "and that its router forwards that UDP port to it.";
    }
    return;
  }
  for (const std::string& c : session_->takeNetCommands()) sendCommand(c);
  const CrewMember* me = session_->player(session_->localPlayer());
  if (!me) return;
  ClientInputMsg in;
  in.sequence = ++inputSequence_;
  in.move = me->input.move;
  in.run = me->input.run;
  in.jump = me->input.jump;
  in.yaw = me->character.yaw + me->input.yawDelta;
  in.pitch = std::clamp(me->character.pitch + me->input.pitchDelta, -1.55f, 1.55f);
  in.flight = session_->world().player().input;
  send(encode(in), Delivery::Latest);
}

void ClientLink::sendCommand(const std::string& name) {
  send(encode(CommandMsg{name}), Delivery::Reliable);
}

void ClientLink::apply(const SnapshotMsg& s) {
  if (s.tick <= lastTick_ && lastTick_ != 0) return;  // an old one that arrived late
  lastTick_ = s.tick;
  SpaceWorld& w = session_->world();

  Report report;
  report.tick = s.tick;
  report.ship = s.shipPosition;
  report.rotation = s.shipOrientation;
  report.velocity = s.shipVelocity;

  session_->netApplyShipStatus(s.docked, s.dockProp, s.dockBay, s.hull, s.shield);
  {
    std::map<std::string, int> cargo;
    for (const auto& [id, units] : s.cargo) cargo[id] = units;
    session_->netApplyEconomy(std::move(cargo), s.marketStock);
  }
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
      // Our own legs run ahead of the host. So compare its answer with where we were when we sent the input it
      // has got to, not with where we are now: otherwise simple walking looks like an error at any real distance.
      if (moved) {
        past_.clear();
        correction_ = {};
      } else if (loc != Location::Cockpit) {
        while (!past_.empty() && past_.front().sequence < c.inputSequence) past_.pop_front();
        if (!past_.empty() && past_.front().sequence == c.inputSequence) {
          const Vec3 error = c.feet - past_.front().feet;
          const float size = length(error);
          if (size > kSnapDistance) {
            m->character.feet = c.feet;
            m->character.velocity = {};
            past_.clear();
            correction_ = {};
          } else {
            correction_ = size > kIgnoreDistance ? error : Vec3{};
          }
        }
      }
      if (c.noticeSerial != noticeSerial_) {
        noticeSerial_ = c.noticeSerial;
        if (!c.notice.empty()) session_->notify(c.notice);
      }
    } else {
      report.people[local] = {c.feet, c.yaw, c.pitch};
      if (moved) remotes_[local] = report.people[local];
    }
  }

  // Keep it for showing a moment from now. The clock is steered gently, so it never visibly jumps.
  reports_.push_back(std::move(report));
  const double wanted = double(s.tick) - kShowBehindTicks;
  if (showTick_ < 0 || std::fabs(showTick_ - wanted) > 60) showTick_ = wanted;
  else showTick_ += (wanted - showTick_) * 0.05;
}

void ClientLink::showWorldAsOf(double tick, float dt) {
  while (reports_.size() > 2 && double(reports_[1].tick) < tick - 1) reports_.pop_front();
  if (reports_.empty()) return;
  size_t from = 0;
  while (from + 2 < reports_.size() && double(reports_[from + 1].tick) <= tick) ++from;
  const Report& a = reports_[from];
  const Report& b = reports_[std::min(from + 1, reports_.size() - 1)];
  const double span = double(b.tick) - double(a.tick);
  const double t = span > 0 ? std::clamp((tick - double(a.tick)) / span, 0.0, 1.0) : 0.0;

  SpaceWorld& w = session_->world();
  PlayerShip& ship = w.player();
  DVec3 at = a.ship + (b.ship - a.ship) * t;
  if (tick > double(b.tick)) {  // the reports ran out: carry on as it was going, for a moment
    const double ahead = std::min(tick - double(b.tick), kMaxExtrapolateTicks) / 60.0;
    at = b.ship + DVec3(b.velocity) * ahead;
  }
  ship.state.position = w.frame().toLocal(SystemPosition{at});
  ship.state.orientation = nlerp(a.rotation, b.rotation, float(t));
  ship.state.velocity = b.velocity;  // for the speed readouts; the next step sets the position again

  const float follow = std::min(1.0f, dt * 20.0f);
  for (auto& [local, shown] : remotes_) {
    auto pa = a.people.find(local), pb = b.people.find(local);
    if (pa == a.people.end() || pb == b.people.end()) continue;
    shown.feet = pa->second.feet + (pb->second.feet - pa->second.feet) * float(t);
    shown.yaw = pb->second.yaw;
    shown.pitch = pb->second.pitch;
  }
  for (auto& [local, shown] : remotes_) {
    CrewMember* m = session_->player(local);
    if (!m) continue;
    m->character.velocity = {};
    m->character.feet = m->character.feet + (shown.feet - m->character.feet) * follow;
    m->character.yaw = shown.yaw;
    m->character.pitch = shown.pitch;
    m->input = CharacterInput{};
  }
}

void ClientLink::postStep(float dt) {
  if (showTick_ >= 0) showTick_ += double(dt) * 60.0;
  if (!reports_.empty() && showTick_ >= 0) {
    for (const Report& r : reports_)
      for (const auto& [local, person] : r.people)
        if (!remotes_.count(local)) remotes_[local] = person;
    showWorldAsOf(showTick_, dt);
  }
  for (auto it = remotes_.begin(); it != remotes_.end();)
    it = session_->player(it->first) ? std::next(it) : remotes_.erase(it);

  // Our own body: remember where each input took us, and ease away any disagreement with the host.
  CrewMember* me = session_->player(session_->localPlayer());
  if (me && me->location != Location::Cockpit) {
    const float ease = std::min(1.0f, dt * 6.0f);
    const Vec3 step = correction_ * ease;
    correction_ = correction_ - step;
    me->character.feet = me->character.feet + step;
    for (Past& p : past_) p.feet = p.feet + step;
    past_.push_back({inputSequence_, me->character.feet});
    if (past_.size() > 300) past_.pop_front();
  } else {
    past_.clear();
    correction_ = {};
  }
}

}  // namespace fh::net
