// Networking: the message format, and a host and client talking over real UDP on this machine.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/net/dedicated_server.h"
#include "game/net/net_link.h"
#include "game/net/sha256.h"
#include "game/net/net_protocol.h"
#include "game/session/game_session.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;
using namespace fh::net;
namespace net = fh::net;

namespace {

constexpr float kDt = 1.0f / 60.0f;
std::uint16_t nextPort = 37210;  // each test its own, so a slow socket close cannot get in the way

std::unique_ptr<GameSession> shipSession() {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return nullptr;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!scene || !ship || !db) return nullptr;
  auto s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, std::nullopt));
  s->setShipInterior(ship->interior);
  auto def = loadInteriorDef(*m, ship->interior, &errors);
  if (!def) return nullptr;
  s->addInterior(ship->interior, *def);
  s->setEconomy(*db, 500);
  return s;
}

struct Pair {
  std::unique_ptr<GameSession> hostSession, clientSession;
  std::unique_ptr<HostLink> host;
  std::unique_ptr<ClientLink> client;

  void step(int steps = 1) {
    for (int i = 0; i < steps; ++i) {
      host->preStep();
      hostSession->fixedUpdate(kDt);
      host->postStep();
      client->preStep();
      clientSession->fixedUpdate(kDt);
      client->postStep(kDt);
    }
  }
  bool stepUntil(const std::function<bool()>& done, int maxSteps = 600) {
    for (int i = 0; i < maxSteps; ++i) {
      if (done()) return true;
      step();
    }
    return done();
  }
};

std::unique_ptr<Pair> connectedPair(const char* name = "Mara") {
  auto p = std::make_unique<Pair>();
  p->hostSession = shipSession();
  p->clientSession = shipSession();
  if (!p->hostSession || !p->clientSession) return nullptr;
  p->host = std::make_unique<HostLink>(*p->hostSession);
  p->client = std::make_unique<ClientLink>(*p->clientSession);
  const std::uint16_t port = nextPort++;
  if (!p->host->start(port)) return nullptr;
  if (!p->client->start("127.0.0.1", port, name)) return nullptr;
  if (!p->stepUntil([&] { return p->client->joined(); })) return nullptr;
  return p;
}

PlayerId remoteOnHost(const GameSession& s) { return s.crew().back().id; }

}  // namespace

TEST(net_messages_survive_the_trip) {
  ClientInputMsg in;
  in.move = {0.5f, -1.0f};
  in.jump = true;
  in.yaw = 1.25f;
  in.flight.throttle = 0.75f;
  in.flight.mode = SpeedMode::Precision;
  const Bytes a = encode(in);
  const auto out = decodeClientInput(a.data(), a.size());
  CHECK(out.has_value());
  if (out) {
    CHECK(out->move.y == -1.0f);
    CHECK(out->jump);
    CHECK(out->yaw == 1.25f);
    CHECK(out->flight.throttle == 0.75f);
    CHECK(out->flight.mode == SpeedMode::Precision);
  }

  SnapshotMsg s;
  s.tick = 77;
  s.shipPosition = {1.0e10, -5.0, 3.5};
  s.docked = true;
  s.dockProp = 3;
  CrewStateMsg c;
  c.id = 9;
  c.name = "Mara";
  c.interior = "interior.sparrow";
  c.feet = {1, 2, 3};
  s.crew.push_back(c);
  const Bytes b = encode(s);
  const auto snap = decodeSnapshot(b.data(), b.size());
  CHECK(snap.has_value());
  if (snap) {
    CHECK(snap->shipPosition.x == 1.0e10);
    CHECK(snap->crew.size() == 1);
    CHECK(snap->crew[0].name == "Mara");
    CHECK(snap->crew[0].feet.z == 3.0f);
  }
}

TEST(net_nonsense_never_decodes_into_anything) {
  SnapshotMsg s;
  s.crew.resize(2);
  const Bytes good = encode(s);
  for (size_t cut = 0; cut < good.size(); ++cut) CHECK(!decodeSnapshot(good.data(), cut).has_value());
  // Random bytes, including ones that look like the start of a real message.
  unsigned seed = 12345;
  for (int round = 0; round < 2000; ++round) {
    Bytes junk(size_t(1 + round % 300));
    for (auto& b : junk) {
      seed = seed * 1664525u + 1013904223u;
      b = std::uint8_t(seed >> 24);
    }
    junk[0] = std::uint8_t(1 + round % 6);
    (void)decodeHello(junk.data(), junk.size());
    (void)decodeWelcome(junk.data(), junk.size());
    (void)decodeRefused(junk.data(), junk.size());
    (void)decodeClientInput(junk.data(), junk.size());
    (void)decodeCommand(junk.data(), junk.size());
    (void)decodeSnapshot(junk.data(), junk.size());
  }
  // Numbers that would poison the simulation are refused.
  ClientInputMsg in;
  in.yaw = std::nanf("");
  const Bytes bad = encode(in);
  CHECK(!decodeClientInput(bad.data(), bad.size()).has_value());
}

TEST(net_a_client_joins_the_crew_and_leaving_removes_them) {
  auto p = connectedPair();
  CHECK(p != nullptr);
  if (!p) return;
  CHECK(p->hostSession->crew().size() == 2);
  CHECK(p->hostSession->crew()[1].name == "Mara");
  CHECK(!p->hostSession->crew()[1].pilot);  // the host has the helm
  CHECK(p->stepUntil([&] { return p->clientSession->crew().size() == 2; }));
  p->client.reset();
  CHECK([&] {
    for (int i = 0; i < 300; ++i) {
      p->host->preStep();
      p->hostSession->fixedUpdate(kDt);
      p->host->postStep();
      if (p->hostSession->crew().size() == 1) return true;
    }
    return false;
  }());
}

TEST(net_a_client_walks_on_the_host_and_the_host_sees_where) {
  auto p = connectedPair();
  CHECK(p != nullptr);
  if (!p) return;
  const PlayerId mara = remoteOnHost(*p->hostSession);
  // Asking to leave the seat goes through the host.
  p->client->sendCommand("interact");
  CHECK(p->stepUntil([&] { return p->hostSession->player(mara)->location == Location::ShipInterior; }));
  CHECK(p->stepUntil([&] { return p->clientSession->location() == Location::ShipInterior; }));
  p->step(60);
  const Vec3 start = p->hostSession->player(mara)->character.feet;
  CharacterInput& controls = p->clientSession->characterInput();
  p->clientSession->character().yaw = 2.2f;  // past the seat, towards the door
  for (int i = 0; i < 90; ++i) {
    controls.move = {0, 1};
    p->step();
  }
  controls.move = {};
  p->step(30);
  const Vec3 onHost = p->hostSession->player(mara)->character.feet;
  CHECK(length(onHost - start) > 2.0f);
  // Prediction and the host agree, near enough that nothing had to be corrected hard.
  CHECK(length(p->clientSession->character().feet - onHost) < 0.5f);
}

TEST(net_a_client_cannot_steer_from_a_passenger_seat_but_can_from_the_helm) {
  auto p = connectedPair();
  CHECK(p != nullptr);
  if (!p) return;
  GameSession& host = *p->hostSession;
  const PlayerId mara = remoteOnHost(host);
  host.world().player().input = FlightInput{};
  p->clientSession->world().player().input.throttle = 1.0f;
  const Vec3 before = host.world().player().state.position;
  for (int i = 0; i < 60; ++i) {
    host.world().player().input = FlightInput{};  // the host's own helm is idle
    p->clientSession->world().player().input.throttle = 1.0f;
    p->step();
  }
  CHECK(length(host.world().player().state.position - before) < 1.0f);

  // The host gets up and Mara changes seats through the cabin: then the helm is hers.
  CHECK(host.leaveSeat().ok);
  CHECK(host.leaveSeat(mara).ok);
  CHECK(host.perform(mara, "takeSeat").ok);
  CHECK(host.player(mara)->pilot);
  for (int i = 0; i < 120; ++i) {
    p->clientSession->world().player().input.throttle = 1.0f;
    p->step();
  }
  const float moved = length(host.world().player().state.position - before);
  CHECK(moved > 5.0f);
  // What the client shows is where the host has the ship, to within a report or two of travel.
  p->step(20);
  const auto sysHost = host.world().frame().toSystem(host.world().player().state.position).metres;
  const auto sysClient = p->clientSession->world().frame().toSystem(p->clientSession->world().player().state.position).metres;
  CHECK(length(Vec3(sysHost - sysClient)) < 25.0f);
}

TEST(net_a_full_crew_refuses_the_next_person) {
  auto host = shipSession();
  CHECK(host != nullptr);
  if (!host) return;
  HostLink link(*host);
  const std::uint16_t port = nextPort++;
  CHECK(link.start(port));
  std::vector<std::unique_ptr<GameSession>> sessions;
  std::vector<std::unique_ptr<ClientLink>> clients;
  for (int i = 0; i < 3; ++i) {
    sessions.push_back(shipSession());
    clients.push_back(std::make_unique<ClientLink>(*sessions.back()));
    CHECK(clients.back()->start("127.0.0.1", port, std::format("Crew {}", i)));
  }
  auto pump = [&] {
    link.pump();
    for (auto& c : clients) c->pump();
  };
  for (int i = 0; i < 600 && link.clients() < 3; ++i) pump();
  CHECK(link.clients() == 3);
  CHECK(host->crew().size() == 4);
  // A fifth person: the transport has no slot for them, or the host says no. Either way they are not in.
  auto extraSession = shipSession();
  ClientLink extra(*extraSession);
  CHECK(extra.start("127.0.0.1", port, "Fifth"));
  for (int i = 0; i < 300; ++i) {
    pump();
    extra.pump();
  }
  CHECK(!extra.joined());
  CHECK(host->crew().size() == 4);
}

// ---- The password ------------------------------------------------------------------------------------

namespace {
std::string hex(const Digest& d) {
  std::string out;
  for (const std::uint8_t b : d) out += std::format("{:02x}", b);
  return out;
}
}  // namespace

TEST(net_sha256_matches_the_published_values) {
  CHECK(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(net_a_proof_depends_on_the_password_and_the_connection) {
  CHECK(passwordProof(1, "kestrel") == passwordProof(1, "kestrel"));
  CHECK(passwordProof(1, "kestrel") != passwordProof(1, "kestrel2"));
  CHECK(passwordProof(1, "kestrel") != passwordProof(2, "kestrel"));
}

// ---- A server with nobody at its keyboard ---------------------------------------------------------------

namespace {

struct ServerRig {
  std::filesystem::path save;
  std::uint16_t port = nextPort++;
  net::DedicatedServer server{FH_TEST_CONTENT_DIR};
  ServerConfig config;

  ServerRig() {
    save = std::filesystem::temp_directory_path() / std::format("fh_server_test_{}.json", port);
    std::error_code ec;
    std::filesystem::remove(save, ec);
    config.port = port;
    config.password = "kestrel";
  }
  ~ServerRig() {
    server.stop();
    std::error_code ec;
    std::filesystem::remove(save, ec);
  }
  bool start() {
    std::string why;
    const bool ok = server.start(config, save, &why);
    if (!ok) std::fprintf(stderr, "    server: %s\n", why.c_str());
    return ok;
  }
};

struct Visitor {
  std::unique_ptr<GameSession> session = shipSession();
  std::unique_ptr<ClientLink> link = std::make_unique<ClientLink>(*session);
};

bool pumpUntil(ServerRig& rig, Visitor& v, const std::function<bool()>& done, int rounds = 800) {
  for (int i = 0; i < rounds; ++i) {
    if (done()) return true;
    rig.server.update(kDt);
    v.link->preStep();
    v.session->fixedUpdate(kDt);
    v.link->postStep(kDt);
  }
  return done();
}

}  // namespace

TEST(server_will_not_start_without_a_password) {
  ServerRig rig;
  rig.config.password.clear();
  std::string why;
  CHECK(!rig.server.start(rig.config, rig.save, &why));
  CHECK(why.find("password") != std::string::npos);
  CHECK(!rig.server.running());
}

TEST(server_refuses_a_wrong_password_and_says_so) {
  ServerRig rig;
  CHECK(rig.start());
  Visitor v;
  CHECK(v.link->start("127.0.0.1", rig.port, "Mallory", "guess"));
  CHECK(pumpUntil(rig, v, [&] { return v.link->lost(); }));
  CHECK(!v.link->joined());
  CHECK(v.link->failure() == "Wrong password");
  CHECK(rig.server.players().empty());
}

TEST(server_lets_the_right_password_in_and_the_first_aboard_gets_the_helm) {
  ServerRig rig;
  CHECK(rig.start());
  Visitor v;
  CHECK(v.link->start("127.0.0.1", rig.port, "Mara", "kestrel"));
  CHECK(pumpUntil(rig, v, [&] { return v.link->joined(); }));
  CHECK(pumpUntil(rig, v, [&] { return !rig.server.players().empty(); }));
  const auto players = rig.server.players();
  CHECK(players.size() == 1);
  if (players.empty()) return;
  CHECK(players[0].name == "Mara");
  CHECK(players[0].pilot);
  CHECK(players[0].where == "At the helm");
  // The server itself is nobody: the client's crew list has only Mara in it.
  pumpUntil(rig, v, [] { return false; }, 30);
  CHECK(v.session->crew().size() == 1);
  CHECK(v.session->player(v.session->localPlayer())->pilot);

  // She flies from the far end.
  const Vec3 before = rig.server.session()->world().player().state.position;
  for (int i = 0; i < 180; ++i) {
    v.session->world().player().input.throttle = 1.0f;
    rig.server.update(kDt);
    v.link->preStep();
    v.session->fixedUpdate(kDt);
    v.link->postStep(kDt);
  }
  CHECK(length(rig.server.session()->world().player().state.position - before) > 5.0f);
}

TEST(server_world_holds_still_when_nobody_is_there) {
  ServerRig rig;
  CHECK(rig.start());
  const double t0 = rig.server.worldTime();
  for (int i = 0; i < 120; ++i) rig.server.update(kDt);
  CHECK(rig.server.worldTime() == t0);
  CHECK(!rig.server.simulating());
  Visitor v;
  CHECK(v.link->start("127.0.0.1", rig.port, "Mara", "kestrel"));
  CHECK(pumpUntil(rig, v, [&] { return v.link->joined(); }));
  for (int i = 0; i < 120; ++i) pumpUntil(rig, v, [] { return false; }, 1);
  CHECK(rig.server.worldTime() > t0);
}

TEST(server_can_remove_someone_and_they_are_told) {
  ServerRig rig;
  CHECK(rig.start());
  Visitor v;
  CHECK(v.link->start("127.0.0.1", rig.port, "Mara", "kestrel"));
  CHECK(pumpUntil(rig, v, [&] { return !rig.server.players().empty(); }));
  rig.server.kick(rig.server.players().front().id);
  CHECK(pumpUntil(rig, v, [&] { return v.link->lost(); }));
  CHECK(v.link->failure().find("removed") != std::string::npos);
  CHECK(pumpUntil(rig, v, [&] { return rig.server.players().empty(); }));
}

TEST(server_keeps_a_full_crew_to_its_limit) {
  ServerRig rig;
  rig.config.maxPlayers = 1;
  CHECK(rig.start());
  Visitor a, b;
  CHECK(a.link->start("127.0.0.1", rig.port, "First", "kestrel"));
  CHECK(pumpUntil(rig, a, [&] { return a.link->joined(); }));
  CHECK(b.link->start("127.0.0.1", rig.port, "Second", "kestrel"));
  CHECK(pumpUntil(rig, b, [&] { return b.link->lost(); }));
  CHECK(b.link->failure().find("full") != std::string::npos);
  CHECK(rig.server.players().size() == 1);
}

TEST(server_saves_the_world_and_picks_it_up_again) {
  ServerRig rig;
  CHECK(rig.start());
  {
    Visitor v;
    CHECK(v.link->start("127.0.0.1", rig.port, "Mara", "kestrel"));
    CHECK(pumpUntil(rig, v, [&] { return v.link->joined(); }));
    pumpUntil(rig, v, [] { return false; }, 120);
    rig.server.session()->player(rig.server.players().front().id)->credits = 4321;
    pumpUntil(rig, v, [] { return false; }, 10);
    v.link.reset();
  }
  for (int i = 0; i < 200 && !rig.server.players().empty(); ++i) rig.server.update(kDt);
  CHECK(rig.server.players().empty());
  const double time = rig.server.worldTime();
  CHECK(time > 0);
  rig.server.stop();
  CHECK(std::filesystem::exists(rig.save));
  CHECK(rig.start());
  CHECK(std::fabs(rig.server.worldTime() - time) < 0.1);
  // Mara's money waited for her.
  Visitor v;
  CHECK(v.link->start("127.0.0.1", rig.port, "Mara", "kestrel"));
  CHECK(pumpUntil(rig, v, [&] { return !rig.server.players().empty(); }));
  CHECK(rig.server.session()->player(rig.server.players().front().id)->credits == 4321);
}
