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

  // ---- Planets ----
  for (const auto& pl : c.world.planets()) {
    const Vec3 centre = c.world.frame().toLocal(pl.def.center);
    if (auto sp = proj.point(centre); sp && proj.onScreen(*sp)) {
      const double alt = length(c.world.frame().toSystem(fs.position).metres - pl.def.center.metres) - pl.def.radius;
      centeredText(o, sp->x, sp->y - 8, pl.def.name, kDim);
      centeredText(o, sp->x, sp->y + 10, "alt " + formatDistance(alt), kDim);
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

  // ---- Docking ----
  if (c.session) {
    const GameSession& gs = *c.session;
    const Docking& dk = gs.docking();
    const double simT = gs.world().simulationTime();
    if (dk.state() == DockState::ApproachAuthorized || dk.state() == DockState::Capture) {
      const Transform bay = dk.bayTransform(gs.world());
      const Vec3 column = dk.approachPoint(gs.world());
      auto pb = proj.point(bay.position);
      auto pc = proj.point(column);
      if (pb && pc) {  // approach column: dashed line from the pad up to the approach point
        for (int i = 0; i < 10; i += 2) {
          const float t0 = float(i) / 10.0f, t1 = float(i + 1) / 10.0f;
          o.line(pb->x + (pc->x - pb->x) * t0, pb->y + (pc->y - pb->y) * t0, pb->x + (pc->x - pb->x) * t1,
                 pb->y + (pc->y - pb->y) * t1, 2, kNav);
        }
      }
      if (pb && proj.onScreen(*pb)) {  // pad: square marker
        const float r = 12;
        o.line(pb->x - r, pb->y - r, pb->x + r, pb->y - r, 2, kNav);
        o.line(pb->x + r, pb->y - r, pb->x + r, pb->y + r, 2, kNav);
        o.line(pb->x + r, pb->y + r, pb->x - r, pb->y + r, 2, kNav);
        o.line(pb->x - r, pb->y + r, pb->x - r, pb->y - r, 2, kNav);
        shadowText(o, pb->x + 18, pb->y - 7, "PAD", kNav);
      } else {
        edgeArrow(o, c, bay.position, kNav, "PAD " + formatDistance(length(bay.position - fs.position)));
      }
      if (dk.state() == DockState::ApproachAuthorized) {
        const DockApproachInfo& a = dk.approach();
        const auto* dd = gs.world().props()[*dk.station()].def.docking ? &*gs.world().props()[*dk.station()].def.docking
                                                                       : nullptr;
        if (dd) {
          const float x = c.width - 330, y = c.height * 0.35f;
          o.rect(x - 10, y - 8, 320, 92, {10, 8, 16, 150});
          shadowText(o, x, y, dk.autopilot() ? "FINAL APPROACH  [AUTO]" : "FINAL APPROACH", kPrimary);
          shadowText(o, x, y + 22, std::format("DIST  {:6.0f} / {:.0f} m", a.distance, dd->captureDistance),
                     a.distanceOk ? kField : kWarn);
          shadowText(o, x, y + 42, std::format("SPEED {:6.1f} / {:.0f} m/s", a.speed, dd->captureSpeed),
                     a.speedOk ? kField : kWarn);
          shadowText(o, x, y + 62, std::format("ALIGN {:6.1f} / {:.0f} deg", a.angleError, dd->captureAngleDeg),
                     a.angleOk ? kField : kWarn);
        }
      }
    }
    // Docking state + context hints.
    std::string hint;
    switch (dk.state()) {
      case DockState::Undocked: {
        for (const auto& p : gs.world().props())
          if (p.def.docking && length(p.local - fs.position) <= p.def.docking->clearanceRange) {
            hint = "L  request docking at " + p.def.name;
            break;
          }
        break;
      }
      case DockState::RequestingClearance: hint = "Awaiting clearance...   L cancel"; break;
      case DockState::ApproachAuthorized:
        hint = dk.autopilot() ? "Docking autopilot engaged   K manual   L cancel"
                              : "Land on the pad: slow, level, nose forward   K autopilot   L cancel";
        break;
      case DockState::Capture: hint = "Clamps engaging..."; break;
      case DockState::Docked: hint = "DOCKED   M market   F leave seat   L undock   F7 save"; break;
      case DockState::Undocking: hint = "Undocking - clearing the pad"; break;
    }
    // Travel drive and landing take precedence over docking hints.
    const TravelDrive& tr = gs.travel();
    const Landing& ld = gs.landing();
    if (tr.active()) {
      std::string line = std::string("TRAVEL DRIVE  ") + travelStateName(tr.state());
      if (tr.state() == TravelState::Spooling) line += std::format("  {:.0f}%", tr.spoolProgress() * 100.0f);
      if (tr.state() == TravelState::Transit || tr.state() == TravelState::Decelerating)
        line += std::format("  {:.1f} km/s  {}  ETA {:.0f} s", tr.speed() / 1000.0f, formatDistance(tr.remaining()),
                            tr.etaSeconds());
      hint = line + "   J abort";
    } else if (ld.busy()) {
      hint = std::string(landStateName(ld.state())) + "   hold on";
    } else if (dk.state() == DockState::Undocked) {
      const DVec3 me = c.world.frame().toSystem(fs.position).metres;
      for (const auto& pl : c.world.planets())
        for (std::uint32_t si = 0; si < pl.def.sites.size(); ++si)
          if (length(pl.siteOrbit(si) - me) <= Landing::kRequestRange) hint = "L  begin descent to " + pl.def.sites[si].name;
      if (hint.empty()) {
        if (const auto& t = c.world.target(); t && t->kind != TargetRef::Kind::Asteroid) {
          if (auto ti = c.world.targetInfo(); ti && ti->distance > c.session->world().player().def.travel.minDistance)
            hint = "J  travel drive to " + ti->name + "   (N next destination)";
        } else if (hint.empty()) {
          hint = "N  select a destination";
        }
      }
    }
    if (!hint.empty()) centeredText(o, cx, 40, hint, dk.state() == DockState::Docked ? kNav : kPrimary);
    if (simT - tr.messageTime() < 4.0 && !tr.message().empty() && simT - dk.messageTime() >= 6.0)
      centeredText(o, cx, c.height * 0.74f, tr.message(), kPrimary);
    if (simT - ld.messageTime() < 6.0 && !ld.message().empty())
      centeredText(o, cx, c.height * 0.70f, "COMMS  " + ld.message(), kNav);
    // Radio chatter subtitles (accessibility, doc 11).
    if (simT - dk.messageTime() < 6.0 && !dk.message().empty())
      centeredText(o, cx, c.height * 0.78f, "COMMS  " + dk.message(), kNav);
    else if (simT - gs.noticeTime() < 4.0 && !gs.notice().empty())
      centeredText(o, cx, c.height * 0.78f, gs.notice(), kPrimary);
  }

  // ---- Economy + mining ----
  if (c.session) {
    const GameSession& gs = *c.session;
    const float scale = 2.0f;
    std::string cr = std::to_string(gs.credits());
    for (int i = int(cr.size()) - 3; i > 0; i -= 3) cr.insert(size_t(i), ",");
    const std::string line = std::format("CR {}    HOLD {:.0f}/{:.0f} t", cr, gs.cargo().usedTons(),
                                         gs.cargo().capacityTons());
    shadowText(o, c.width - OverlayBuilder::textWidth(line, scale) - 16, 16, line,
               gs.cargo().freeTons() < 1.0f ? kWarn : kPrimary, scale);
    // Target composition (after a scan).
    if (auto t = c.world.target(); t && t->kind == TargetRef::Kind::Asteroid) {
      const Asteroid& a = c.world.asteroids()[t->index];
      std::string info;
      if (gs.mining().isScanned(t->index)) {
        const CommodityDef* cd = gs.commodities().find(a.resource);
        info = a.resource.empty() ? std::string("SCAN: barren rock")
                                  : std::format("SCAN: {}  yield {} t  hardness {:.1f}", cd ? cd->name : a.resource,
                                                a.yield, a.hardness);
      } else if (gs.mining().scanning()) {
        info = std::format("SCANNING {:.0f}%", gs.mining().scanProgress(c.world.simulationTime()) * 100.0f);
      } else {
        info = "R  scan target";
      }
      shadowText(o, c.width - OverlayBuilder::textWidth(info, scale) - 16, 38, info, kField, scale);
    }
    // Mining feedback.
    const Mining& mn = gs.mining();
    if (mn.beamActive()) {
      const float bw = 160, bx = cx - bw * 0.5f, by = c.height * 0.62f;
      centeredText(o, cx, by - 22, "MINING", kPrimary);
      o.rect(bx, by, bw, 6, {20, 16, 10, 160});
      o.rect(bx, by, bw * std::clamp(mn.progress(), 0.0f, 1.0f), 6, kPrimary);
    } else if (!mn.status().empty()) {
      centeredText(o, cx, c.height * 0.62f, mn.status(), kWarn);
    }
    if (gs.lastMiningEvents().cargoFull || (!mn.chunks().empty() && gs.cargo().freeTons() < 1.0f))
      centeredText(o, cx, c.height * 0.66f, "CARGO HOLD FULL - sell at a station (M when docked)", kWarn);
  }

  // ---- Status line ----
  std::string status = cameraModeName(c.cameraMode);
  if (c.controller.autofly()) status += "   AUTOFLY";
  if (!c.mouseCaptured && shipCam) status += "   MOUSE FREE (Esc to steer)";
  centeredText(o, cx, 16, status, kDim);
  if (c.time < c.lookShownUntil) centeredText(o, cx, c.height * 0.30f, "LOOK: " + c.lookName, kPrimary, 3.0f);
}

void drawInteriorHud(OverlayBuilder& o, const InteriorHudContext& c) {
  kPrimary = toColor(c.palette.primary);
  kDim = toColor(c.palette.primary, 0.5f);
  kNav = toColor(c.palette.nav);
  kField = toColor(c.palette.field);
  kWarn = toColor(c.palette.warn);
  const float cx = c.width * 0.5f, cy = c.height * 0.5f;
  const GameSession& s = c.session;
  const InteriorDef* def = s.activeInterior();
  o.circle(cx, cy, 2.5f, 2, kPrimary, 8);  // crosshair dot
  if (const Interactable* it = s.focusedInteractable()) {
    o.circle(cx, cy, 10, 2, kNav, 16);
    centeredText(o, cx, cy + 26, "[F]  " + it->prompt, kNav, 2.5f);
  }
  if (def) {
    const std::string where = def->kind == "surface" ? "SURFACE" : locationName(s.location());
    centeredText(o, cx, 16, where + "   " + def->name, kDim);
  }
  if (!c.mouseCaptured) centeredText(o, cx, 40, "MOUSE FREE (Esc to look around)", kDim);
  if (s.world().simulationTime() - s.noticeTime() < 4.0 && !s.notice().empty())
    centeredText(o, cx, c.height * 0.78f, s.notice(), kPrimary);
  shadowText(o, 16, c.height - 30, "WASD walk   Shift run   Space jump   F use   Esc mouse", kDim, 1.5f);
}

void drawSurfaceHud(OverlayBuilder& o, const InteriorHudContext& c) {
  kPrimary = toColor(c.palette.primary);
  kDim = toColor(c.palette.primary, 0.5f);
  kNav = toColor(c.palette.nav);
  kField = toColor(c.palette.field);
  kWarn = toColor(c.palette.warn);
  const float cx = c.width * 0.5f;
  const GameSession& s = c.session;
  const Landing& ld = s.landing();
  const double t = s.world().simulationTime();
  std::string status = landStateName(ld.state());
  if (ld.site()) status += "   " + s.world().planets()[ld.site()->planet].def.sites[ld.site()->site].name;
  centeredText(o, cx, 16, status, kDim);
  if (ld.state() == LandState::Landed)
    centeredText(o, cx, 40, "LANDED   F leave seat   M market   L launch   F7 save", kNav);
  if (t - ld.messageTime() < 6.0 && !ld.message().empty())
    centeredText(o, cx, c.height * 0.78f, "COMMS  " + ld.message(), kNav);
  else if (t - s.noticeTime() < 4.0 && !s.notice().empty())
    centeredText(o, cx, c.height * 0.78f, s.notice(), kPrimary);
  std::string cr = std::to_string(s.credits());
  for (int i = int(cr.size()) - 3; i > 0; i -= 3) cr.insert(size_t(i), ",");
  const std::string line = std::format("CR {}    HOLD {:.0f}/{:.0f} t", cr, s.cargo().usedTons(), s.cargo().capacityTons());
  shadowText(o, c.width - OverlayBuilder::textWidth(line) - 16, 16, line, kPrimary);
}

}  // namespace fh
