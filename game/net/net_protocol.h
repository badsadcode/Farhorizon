#pragma once
// What goes over the wire. Plain byte packing with bounds checks: anything can arrive from a network, so a
// short or nonsense packet must only ever make decoding fail. GPU-free and unit-tested.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/math/math.h"
#include "game/ships/flight_model.h"

namespace fh::net {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 27777;
inline constexpr size_t kMaxNameLength = 24;
inline constexpr size_t kMaxCommandLength = 32;
inline constexpr size_t kMaxTextLength = 200;

enum class MessageType : std::uint8_t { Hello = 1, Welcome, Refused, ClientInput, Command, Snapshot };

struct HelloMsg {
  std::uint16_t protocol = kProtocolVersion;
  std::string name;
};

struct WelcomeMsg {
  std::uint32_t yourId = 0;
};

struct RefusedMsg {
  std::string reason;
};

// A player's controls, about once per simulation step. The look direction is absolute: it is theirs to
// choose, and nothing they send can move them other than by walking.
struct ClientInputMsg {
  Vec2 move{};
  bool run = false;
  bool jump = false;
  float yaw = 0, pitch = 0;
  FlightInput flight;
};

struct CommandMsg {
  std::string name;  // "interact", or a helm command such as "dock.request"
};

struct CrewStateMsg {
  std::uint32_t id = 0;
  std::string name;
  std::uint8_t location = 0;  // Location enum
  std::string interior;
  bool pilot = false;
  Vec3 feet{};
  float yaw = 0, pitch = 0;
  float health = 100;
  std::int64_t credits = 0;
  std::string notice;
  std::uint32_t noticeSerial = 0;  // goes up each time the notice changes
};

struct SnapshotMsg {
  std::uint32_t tick = 0;
  double time = 0;
  DVec3 shipPosition{};       // system frame: the two machines keep different local origins
  Quat shipOrientation{};
  Vec3 shipVelocity{};
  bool docked = false;
  std::uint32_t dockProp = 0, dockBay = 0;
  float hull = 100, shield = 60;
  std::int32_t targetKind = -1;  // -1: no target
  std::uint32_t targetIndex = 0, targetSub = 0;
  std::vector<CrewStateMsg> crew;
};

using Bytes = std::vector<std::uint8_t>;

Bytes encode(const HelloMsg& m);
Bytes encode(const WelcomeMsg& m);
Bytes encode(const RefusedMsg& m);
Bytes encode(const ClientInputMsg& m);
Bytes encode(const CommandMsg& m);
Bytes encode(const SnapshotMsg& m);

std::optional<MessageType> peekType(const std::uint8_t* data, size_t size);
std::optional<HelloMsg> decodeHello(const std::uint8_t* data, size_t size);
std::optional<WelcomeMsg> decodeWelcome(const std::uint8_t* data, size_t size);
std::optional<RefusedMsg> decodeRefused(const std::uint8_t* data, size_t size);
std::optional<ClientInputMsg> decodeClientInput(const std::uint8_t* data, size_t size);
std::optional<CommandMsg> decodeCommand(const std::uint8_t* data, size_t size);
std::optional<SnapshotMsg> decodeSnapshot(const std::uint8_t* data, size_t size);

}  // namespace fh::net
