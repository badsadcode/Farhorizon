#include "game/net/net_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fh::net {

namespace {

constexpr size_t kMaxCrewInSnapshot = 8;

class Writer {
 public:
  explicit Writer(MessageType t) { u8(std::uint8_t(t)); }
  void u8(std::uint8_t v) { out_.push_back(v); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  template <class T>
  void raw(T v) {  // little-endian on every platform we build for
    const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    out_.insert(out_.end(), p, p + sizeof(T));
  }
  void vec3(const Vec3& v) { raw(v.x), raw(v.y), raw(v.z); }
  void text(const std::string& s, size_t limit) {
    const size_t n = std::min(s.size(), limit);
    raw(std::uint16_t(n));
    out_.insert(out_.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
  }
  Bytes take() { return std::move(out_); }

 private:
  Bytes out_;
};

class Reader {
 public:
  Reader(const std::uint8_t* data, size_t size) : p_(data), left_(size) {}
  bool ok() const { return ok_; }
  std::uint8_t u8() { return raw<std::uint8_t>(); }
  bool boolean() { return u8() != 0; }
  template <class T>
  T raw() {
    T v{};
    if (left_ < sizeof(T)) {
      ok_ = false;
      return v;
    }
    std::memcpy(&v, p_, sizeof(T));
    p_ += sizeof(T);
    left_ -= sizeof(T);
    return v;
  }
  // A number that came from outside: NaN and infinity would poison the simulation.
  float number() {
    const float v = raw<float>();
    if (!std::isfinite(v)) ok_ = false;
    return v;
  }
  double numberD() {
    const double v = raw<double>();
    if (!std::isfinite(v)) ok_ = false;
    return v;
  }
  Vec3 vec3() {
    Vec3 v;
    v.x = number();
    v.y = number();
    v.z = number();
    return v;
  }
  std::string text(size_t limit) {
    const auto n = raw<std::uint16_t>();
    if (!ok_ || n > limit || n > left_) {
      ok_ = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    left_ -= n;
    return s;
  }

 private:
  const std::uint8_t* p_;
  size_t left_;
  bool ok_ = true;
};

Reader open(const std::uint8_t* data, size_t size, MessageType want, bool& ok) {
  Reader r(data, size);
  ok = size > 0 && r.u8() == std::uint8_t(want);
  return r;
}

template <class T>
std::optional<T> finish(const Reader& r, T value) {
  if (!r.ok()) return std::nullopt;
  return value;
}

}  // namespace

std::optional<MessageType> peekType(const std::uint8_t* data, size_t size) {
  if (size == 0 || data[0] < std::uint8_t(MessageType::Hello) || data[0] > std::uint8_t(MessageType::Snapshot))
    return std::nullopt;
  return MessageType(data[0]);
}

Bytes encode(const HelloMsg& m) {
  Writer w(MessageType::Hello);
  w.raw(m.protocol);
  w.text(m.name, kMaxNameLength);
  return w.take();
}

std::optional<HelloMsg> decodeHello(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::Hello, ok);
  if (!ok) return std::nullopt;
  HelloMsg m;
  m.protocol = r.raw<std::uint16_t>();
  m.name = r.text(kMaxNameLength);
  return finish(r, m);
}

Bytes encode(const WelcomeMsg& m) {
  Writer w(MessageType::Welcome);
  w.raw(m.yourId);
  return w.take();
}

std::optional<WelcomeMsg> decodeWelcome(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::Welcome, ok);
  if (!ok) return std::nullopt;
  WelcomeMsg m;
  m.yourId = r.raw<std::uint32_t>();
  return finish(r, m);
}

Bytes encode(const RefusedMsg& m) {
  Writer w(MessageType::Refused);
  w.text(m.reason, kMaxTextLength);
  return w.take();
}

std::optional<RefusedMsg> decodeRefused(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::Refused, ok);
  if (!ok) return std::nullopt;
  RefusedMsg m;
  m.reason = r.text(kMaxTextLength);
  return finish(r, m);
}

Bytes encode(const ClientInputMsg& m) {
  Writer w(MessageType::ClientInput);
  w.raw(m.move.x), w.raw(m.move.y);
  w.boolean(m.run), w.boolean(m.jump);
  w.raw(m.yaw), w.raw(m.pitch);
  w.raw(m.flight.throttle);
  w.vec3(m.flight.thrust);
  w.vec3(m.flight.rotation);
  w.boolean(m.flight.boost), w.boolean(m.flight.assist);
  w.u8(std::uint8_t(m.flight.mode));
  return w.take();
}

std::optional<ClientInputMsg> decodeClientInput(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::ClientInput, ok);
  if (!ok) return std::nullopt;
  ClientInputMsg m;
  m.move.x = std::clamp(r.number(), -1.0f, 1.0f);
  m.move.y = std::clamp(r.number(), -1.0f, 1.0f);
  m.run = r.boolean();
  m.jump = r.boolean();
  m.yaw = r.number();
  m.pitch = std::clamp(r.number(), -1.57f, 1.57f);
  m.flight.throttle = std::clamp(r.number(), -1.0f, 1.0f);
  const Vec3 thrust = r.vec3();
  const Vec3 rotation = r.vec3();
  auto unit = [](const Vec3& v) {
    return Vec3{std::clamp(v.x, -1.0f, 1.0f), std::clamp(v.y, -1.0f, 1.0f), std::clamp(v.z, -1.0f, 1.0f)};
  };
  m.flight.thrust = unit(thrust);
  m.flight.rotation = unit(rotation);
  m.flight.boost = r.boolean();
  m.flight.assist = r.boolean();
  const std::uint8_t mode = r.u8();
  if (mode > std::uint8_t(SpeedMode::Cruise)) return std::nullopt;
  m.flight.mode = SpeedMode(mode);
  return finish(r, m);
}

Bytes encode(const CommandMsg& m) {
  Writer w(MessageType::Command);
  w.text(m.name, kMaxCommandLength);
  return w.take();
}

std::optional<CommandMsg> decodeCommand(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::Command, ok);
  if (!ok) return std::nullopt;
  CommandMsg m;
  m.name = r.text(kMaxCommandLength);
  return finish(r, m);
}

Bytes encode(const SnapshotMsg& m) {
  Writer w(MessageType::Snapshot);
  w.raw(m.tick);
  w.raw(m.time);
  w.raw(m.shipPosition.x), w.raw(m.shipPosition.y), w.raw(m.shipPosition.z);
  w.raw(m.shipOrientation.x), w.raw(m.shipOrientation.y), w.raw(m.shipOrientation.z), w.raw(m.shipOrientation.w);
  w.vec3(m.shipVelocity);
  w.boolean(m.docked);
  w.raw(m.dockProp), w.raw(m.dockBay);
  w.raw(m.hull), w.raw(m.shield);
  w.raw(m.targetKind), w.raw(m.targetIndex), w.raw(m.targetSub);
  w.u8(std::uint8_t(std::min(m.crew.size(), kMaxCrewInSnapshot)));
  for (size_t i = 0; i < m.crew.size() && i < kMaxCrewInSnapshot; ++i) {
    const CrewStateMsg& c = m.crew[i];
    w.raw(c.id);
    w.text(c.name, kMaxNameLength);
    w.u8(c.location);
    w.text(c.interior, kMaxTextLength);
    w.boolean(c.pilot);
    w.vec3(c.feet);
    w.raw(c.yaw), w.raw(c.pitch), w.raw(c.health);
    w.raw(c.credits);
    w.text(c.notice, kMaxTextLength);
    w.raw(c.noticeSerial);
  }
  return w.take();
}

std::optional<SnapshotMsg> decodeSnapshot(const std::uint8_t* data, size_t size) {
  bool ok = false;
  Reader r = open(data, size, MessageType::Snapshot, ok);
  if (!ok) return std::nullopt;
  SnapshotMsg m;
  m.tick = r.raw<std::uint32_t>();
  m.time = r.numberD();
  m.shipPosition.x = r.numberD();
  m.shipPosition.y = r.numberD();
  m.shipPosition.z = r.numberD();
  m.shipOrientation.x = r.number();
  m.shipOrientation.y = r.number();
  m.shipOrientation.z = r.number();
  m.shipOrientation.w = r.number();
  m.shipVelocity = r.vec3();
  m.docked = r.boolean();
  m.dockProp = r.raw<std::uint32_t>();
  m.dockBay = r.raw<std::uint32_t>();
  m.hull = r.number();
  m.shield = r.number();
  m.targetKind = r.raw<std::int32_t>();
  m.targetIndex = r.raw<std::uint32_t>();
  m.targetSub = r.raw<std::uint32_t>();
  const size_t crew = r.u8();
  if (!r.ok() || crew > kMaxCrewInSnapshot) return std::nullopt;
  m.crew.resize(crew);
  for (CrewStateMsg& c : m.crew) {
    c.id = r.raw<std::uint32_t>();
    c.name = r.text(kMaxNameLength);
    c.location = r.u8();
    c.interior = r.text(kMaxTextLength);
    c.pilot = r.boolean();
    c.feet = r.vec3();
    c.yaw = r.number();
    c.pitch = r.number();
    c.health = r.number();
    c.credits = r.raw<std::int64_t>();
    c.notice = r.text(kMaxTextLength);
    c.noticeSerial = r.raw<std::uint32_t>();
  }
  return finish(r, m);
}

}  // namespace fh::net
