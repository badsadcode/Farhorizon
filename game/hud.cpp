#include "game/hud.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace fh {

namespace {
// Readable colour coding *plus* distinct shapes for every marker type (accessibility, doc 11).
// Colours come from the active visual profile (HudPalette); set at the start of drawHud.
OverlayColor kPrimary, kDim, kNav, kField, kWarn;
constexpr OverlayColor kShadow{0, 0, 0, 140};

OverlayColor toColor(const Vec4& c, float alphaScale = 1.0f) {
  auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return {b(c.x), b(c.y), b(c.z), b(c.w * alphaScale)};
}

struct Projector {
  const HudContext& c;
  std::optional<Vec2> point(const Vec3& local) const {
    const Vec3 rel = local - c.camera.position;
    const Vec4 clip = c.viewProj * Vec4(rel, 1.0f);
    if (clip.w <= 1e-3f) return std::nullopt;
    const float x = clip.x / clip.w, y = clip.y / clip.w;
    return Vec2{(x * 0.5f + 0.5f) * c.width, (y * 0.5f + 0.5f) * c.height};
  }
  std::optional<Vec2> direction(const Vec3& dir) const { return point(c.camera.position + dir * 10000.0f); }
  bool onScreen(const Vec2& p, float margin = 0) const {
    return p.x >= margin && p.y >= margin && p.x <= c.width - margin && p.y <= c.height - margin;
  }
};

void shadowText(OverlayBuilder& o, float x, float y, const std::string& s, OverlayColor col, float scale = 2.0f) {
  o.text(x + 1, y + 1, s, kShadow, scale);
  o.text(x, y, s, col, scale);
}
void centeredText(OverlayBuilder& o, float cx, float y, const std::string& s, OverlayColor col, float scale = 2.0f) {
  shadowText(o, cx - OverlayBuilder::textWidth(s, scale) * 0.5f, y, s, col, scale);
}

void diamond(OverlayBuilder& o, Vec2 p, float r, OverlayColor col) {
  o.line(p.x, p.y - r, p.x + r, p.y, 2, col);
  o.line(p.x + r, p.y, p.x, p.y + r, 2, col);
  o.line(p.x, p.y + r, p.x - r, p.y, 2, col);
  o.line(p.x - r, p.y, p.x, p.y - r, 2, col);
}

void bracket(OverlayBuilder& o, Vec2 p, float r, OverlayColor col) {
  const float l = std::max(6.0f, r * 0.45f);
  for (int sx : {-1, 1})
    for (int sy : {-1, 1}) {
      const float x = p.x + float(sx) * r, y = p.y + float(sy) * r;
      o.line(x, y, x - float(sx) * l, y, 2.5f, col);
      o.line(x, y, x, y - float(sy) * l, 2.5f, col);
    }
}

// Arrow at the screen edge pointing toward an off-screen (or behind-camera) world position.
void edgeArrow(OverlayBuilder& o, const HudContext& c, const Vec3& local, OverlayColor col, const std::string& label) {
  const Vec3 rel = c.camera.rotation.conjugate().rotate(local - c.camera.position);  // camera space
  Vec2 d{rel.x, -rel.y};
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  if (len < 1e-4f) d = {0, 1};
  else d = {d.x / len, d.y / len};
  const float cx = c.width * 0.5f, cy = c.height * 0.5f;
  const float rx = c.width * 0.44f, ry = c.height * 0.40f;
  const Vec2 p{cx + d.x * rx, cy + d.y * ry};
  const Vec2 n{-d.y, d.x};
  o.triangle(p.x + d.x * 16, p.y + d.y * 16, p.x + n.x * 9, p.y + n.y * 9, p.x - n.x * 9, p.y - n.y * 9, col);
  centeredText(o, p.x - d.x * 18, p.y - d.y * 18 - 7, label, col);
}
}  // namespace

std::string formatDistance(double metres) {
  const double au = 149597870700.0;
  if (std::fabs(metres) >= 0.01 * au) return std::format("{:.3f} AU", metres / au);
  if (std::fabs(metres) >= 10000.0) return std::format("{:.1f} km", metres / 1000.0);
  if (std::fabs(metres) >= 1000.0) return std::format("{:.2f} km", metres / 1000.0);
  return std::format("{:.0f} m", metres);
}

void drawHud(OverlayBuilder& o, const HudContext& c) {
  kPrimary = toColor(c.palette.primary);
  kDim = toColor(c.palette.primary, 0.5f);
  kNav = toColor(c.palette.nav);
  kField = toColor(c.palette.field);
  kWarn = toColor(c.palette.warn);
  const Projector proj{c};
  const float cx = c.width * 0.5f, cy = c.height * 0.5f;
  const float unit = std::min(c.width, c.height);
  const PlayerShip& player = c.world.player();
  const FlightState& fs = player.state;
  const bool shipCam = c.cameraMode == CameraMode::Chase || c.cameraMode == CameraMode::Cockpit;

  // ---- Navigation markers (stations, fields) ----
  for (const auto& p : c.world.props()) {
    if (p.def.marker == "none") continue;
    const bool isTarget = c.world.target() && c.world.target()->kind == TargetRef::Kind::Prop &&
                          &c.world.props()[c.world.target()->index] == &p;
    if (isTarget) continue;
    if (auto s = proj.point(p.local); s && proj.onScreen(*s)) {
      diamond(o, *s, 9, kNav);
      shadowText(o, s->x + 14, s->y - 14, p.def.name, kNav);
      shadowText(o, s->x + 14, s->y + 4, formatDistance(length(p.local - fs.position)), kNav);
    }
  }
  for (std::uint32_t fi = 0; fi < c.world.fields().size(); ++fi) {
    const auto& f = c.world.fields()[fi];
    const Vec3 centre = c.world.frame().toLocal(f.def.center);
    const double d = length(centre - fs.position);
    if (d < f.def.radius * 0.8) continue;  // inside the field: the rocks speak for themselves
    if (auto s = proj.point(centre); s && proj.onScreen(*s)) {
      o.circle(s->x, s->y, 10, 2, kField, 6);  // hexagon = asteroid field
      shadowText(o, s->x + 14, s->y - 14, f.def.name, kField);
      shadowText(o, s->x + 14, s->y + 4, formatDistance(d), kField);
    }
  }

  // ---- Target ----
  if (auto t = c.world.targetInfo()) {
    const OverlayColor col = t->marker == "asteroid" ? kField : kNav;
    auto s = proj.point(t->local);
    if (s && proj.onScreen(*s, 20)) {
      // Bracket sized to the target's apparent radius.
      const float dist = std::max(1.0f, length(t->local - c.camera.position));
      const float pxPerRad = c.height * 0.5f / std::tan(radians(c.camera.fovDeg) * 0.5f);
      const float r = std::clamp(t->radius / dist * pxPerRad, 14.0f, unit * 0.4f);
      bracket(o, *s, r, col);
      centeredText(o, s->x, s->y - r - 24, t->name, col);
      const std::string closing = std::fabs(t->closingSpeed) < 0.5f ? std::string("--")
                                 : std::format("{:+.0f} m/s", -t->closingSpeed);
      centeredText(o, s->x, s->y + r + 8, formatDistance(t->distance) + "   " + closing, col);
    } else {
      edgeArrow(o, c, t->local, col, formatDistance(t->distance));
    }
  }

  if (shipCam) {
    // ---- Reticle (where the nose points) ----
    const Vec3 nose = c.ship.rotation.forward();
    if (auto s = proj.direction(nose)) {
      o.circle(s->x, s->y, 9, 2, kPrimary, 20);
      o.line(s->x - 22, s->y, s->x - 13, s->y, 2, kPrimary);
      o.line(s->x + 13, s->y, s->x + 22, s->y, 2, kPrimary);
      o.line(s->x, s->y + 13, s->x, s->y + 20, 2, kPrimary);
    }
    // ---- Velocity vector (prograde) / retrograde ----
    const float speed = length(fs.velocity);
    if (speed > 2.0f) {
      const Vec3 dir = fs.velocity / speed;
      if (auto s = proj.direction(dir); s && proj.onScreen(*s)) {
        o.circle(s->x, s->y, 7, 2, kNav, 16);
        o.line(s->x, s->y - 7, s->x, s->y - 15, 2, kNav);
        o.line(s->x - 7, s->y, s->x - 15, s->y, 2, kNav);
        o.line(s->x + 7, s->y, s->x + 15, s->y, 2, kNav);
      } else if (auto r = proj.direction(-dir); r && proj.onScreen(*r)) {
        o.line(r->x - 7, r->y - 7, r->x + 7, r->y + 7, 2, kNav);
        o.line(r->x - 7, r->y + 7, r->x + 7, r->y - 7, 2, kNav);
      }
    }
    // ---- Virtual stick ----
    const float stickR = unit * 0.16f;
    const Vec2 st = c.controller.stick();
    if (c.mouseCaptured) o.circle(cx, cy, stickR, 1.0f, kDim, 48);
    if (std::fabs(st.x) + std::fabs(st.y) > 0.01f) {
      o.line(cx, cy, cx + st.x * stickR, cy + st.y * stickR, 1.5f, kDim);
      o.circle(cx + st.x * stickR, cy + st.y * stickR, 4, 2, kPrimary, 10);
    }

    // ---- Speed / throttle panel ----
    const float cap = flightSpeedCap(player.def.flight, c.input, fs.boosting);
    const float baseY = c.height - 150.0f;
    centeredText(o, cx, baseY, std::format("{:.0f} m/s", speed), kPrimary, 4.0f);
    const char* mode = fs.boosting ? "BOOST" : (c.input.mode == SpeedMode::Precision ? "PRECISION" : "CRUISE");
    const std::string set = c.input.assist ? std::format("SET {:.0f}", c.input.throttle * cap) : std::string("SET --");
    centeredText(o, cx, baseY + 46,
                 std::format("{}   {}   ASSIST {}", set, mode, c.input.assist ? "ON" : "OFF"),
                 c.input.assist ? kPrimary : kWarn);
    // Throttle bar (left of panel): fill = set-speed, tick = actual speed fraction.
    const float bx = cx - 270, by = baseY - 30, bh = 110, bw = 14;
    o.rect(bx, by, bw, bh, {20, 16, 10, 140});
    const float zeroY = by + bh * (1.0f / 1.3f);  // throttle range [-0.3, 1]
    const float t = c.input.throttle;
    if (t >= 0) o.rect(bx, zeroY - t * (zeroY - by), bw, t * (zeroY - by), kPrimary);
    else o.rect(bx, zeroY, bw, -t / 0.3f * (by + bh - zeroY), kWarn);
    const float vfrac = std::clamp(dot(fs.velocity, c.ship.rotation.forward()) / std::max(1.0f, cap), -0.3f, 1.0f);
    const float vy = zeroY - vfrac * (zeroY - by);
    o.line(bx - 6, vy, bx + bw + 6, vy, 2, kNav);
    shadowText(o, bx - 4, by + bh + 6, "THR", kDim);
    // Boost energy (right of panel).
    const float ex = cx + 256;
    const float e = fs.boostEnergy / player.def.flight.boostEnergyCapacity;
    o.rect(ex, by, bw, bh, {20, 16, 10, 140});
    const OverlayColor eCol = fs.boostLockout ? kWarn : (fs.boosting ? kNav : kPrimary);
    o.rect(ex, by + bh * (1.0f - e), bw, bh * e, eCol);
    shadowText(o, ex - 10, by + bh + 6, "BST", kDim);
  }

  // ---- Warnings ----
  if (c.world.simulationTime() - player.lastImpactTime < 1.5 && std::fmod(c.time, 0.3) < 0.2)
    centeredText(o, cx, c.height * 0.22f, std::format("IMPACT  {:.0f} m/s", player.lastImpactSpeed), kWarn, 3.0f);
  else if (auto near = c.world.nearestAsteroidSurface(); near && near->first < 80.0f && near->second > 5.0f)
    centeredText(o, cx, c.height * 0.22f, std::format("PROXIMITY  {:.0f} m", std::max(0.0f, near->first)), kWarn, 3.0f);

  // ---- Status line ----
  std::string status = cameraModeName(c.cameraMode);
  if (c.controller.autofly()) status += "   AUTOFLY";
  if (!c.mouseCaptured && shipCam) status += "   MOUSE FREE (Esc to steer)";
  centeredText(o, cx, 16, status, kDim);
  if (c.time < c.lookShownUntil) centeredText(o, cx, c.height * 0.30f, "LOOK: " + c.lookName, kPrimary, 3.0f);
}

}  // namespace fh
