#include "game/hud.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

#include "engine/platform/input.h"
#include "game/ui/ui.h"

namespace fh {

namespace {
// Readable colour coding *plus* distinct shapes for every marker type (accessibility, doc 11).
// Colours come from the active visual profile (HudPalette); set at the start of each draw call.
OverlayColor kPrimary, kDim, kNav, kField, kWarn;
constexpr OverlayColor kShadow{0, 0, 0, 170};
constexpr OverlayColor kShield{110, 200, 230, 255};
constexpr OverlayColor kMeterBack{20, 16, 10, 170};

OverlayColor toColor(const Vec4& c, float alphaScale = 1.0f) {
  auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return {b(c.x), b(c.y), b(c.z), b(c.w * alphaScale)};
}

void setPalette(const HudPalette& p) {
  kPrimary = toColor(p.primary);
  kDim = toColor(p.primary, 0.62f);
  kNav = toColor(p.nav);
  kField = toColor(p.field);
  kWarn = toColor(p.warn);
}

std::string groupedCredits(std::int64_t credits) {
  std::string s = std::to_string(credits);
  for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
  return s;
}

std::string keyName(const InputState* in, const char* action, const char* fallback = "--") {
  if (!in) return fallback;
  const auto names = in->bindingNames(action);
  return names.empty() ? std::string(fallback) : names.front();
}

// Sizes are 1080p design pixels scaled by `k`, the same scale the menus use (window + uiScale).
struct Pen {
  OverlayBuilder& o;
  const UiTheme& th;
  float k;

  float s(float v) const { return v * k; }
  float small() const { return s(th.small); }
  float body() const { return s(th.body); }
  float large() const { return s(th.heading); }
  float lineH(float px, FontFace f = FontFace::Mono) const { return OverlayBuilder::lineHeight(px, f); }

  // Text over the world: a soft drop shadow keeps it readable on bright planets and hulls.
  void text(float x, float y, std::string_view t, OverlayColor c, float px, TextAlign a = TextAlign::Left,
            FontFace f = FontFace::Mono) {
    const float off = std::max(1.0f, s(1.0f));
    o.textPx(x + off, y + off, t, OverlayColor{kShadow.r, kShadow.g, kShadow.b, std::min(kShadow.a, c.a)}, px, f, a);
    o.textPx(x, y, t, c, px, f, a);
  }
  void centered(float cx, float y, std::string_view t, OverlayColor c, float px) { text(cx, y, t, c, px, TextAlign::Center); }

  void panel(float x, float y, float w, float h, OverlayColor border = {}) {
    const OverlayColor b = border.a == 255 && border.r == 255 && border.g == 255 && border.b == 255
                               ? kPrimary.withAlpha(0.32f) : border;
    o.panel(x, y, w, h, th.panel.withAlpha(th.hudPanelAlpha), b, s(th.chamfer * 0.7f), std::max(1.0f, s(1.0f)));
  }
  // One line of text on its own small panel, centred (hints, comms).
  void pill(float cx, float y, std::string_view t, OverlayColor c, float px) {
    const float w = OverlayBuilder::measure(t, px) + s(28), h = lineH(px) + s(8);
    panel(cx - w * 0.5f, y, w, h, c.withAlpha(0.3f));
    o.textPx(cx, y + s(4), t, c, px, FontFace::Mono, TextAlign::Center);
  }
  // Segmented bar: the ticks read as a physical gauge and help judge fractions at a glance.
  void meter(float x, float y, float w, float h, float frac, OverlayColor c) {
    o.rect(x, y, w, h, kMeterBack);
    o.rect(x, y, w * std::clamp(frac, 0.0f, 1.0f), h, c);
    for (int i = 1; i < 10; ++i) o.rect(x + w * float(i) * 0.1f, y, std::max(1.0f, s(1.0f)), h, OverlayColor{0, 0, 0, 150});
  }
};

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

void diamond(Pen& p, Vec2 c, float r, OverlayColor col) {
  const float t = std::max(1.5f, p.s(2));
  p.o.line(c.x, c.y - r, c.x + r, c.y, t, col);
  p.o.line(c.x + r, c.y, c.x, c.y + r, t, col);
  p.o.line(c.x, c.y + r, c.x - r, c.y, t, col);
  p.o.line(c.x - r, c.y, c.x, c.y - r, t, col);
}

void bracket(Pen& p, Vec2 c, float r, OverlayColor col) {
  const float l = std::max(p.s(6), r * 0.45f), t = std::max(1.5f, p.s(2.5f));
  for (int sx : {-1, 1})
    for (int sy : {-1, 1}) {
      const float x = c.x + float(sx) * r, y = c.y + float(sy) * r;
      p.o.line(x, y, x - float(sx) * l, y, t, col);
      p.o.line(x, y, x, y - float(sy) * l, t, col);
    }
}

void triangle(Pen& p, Vec2 c, float r, OverlayColor col) {
  const float t = std::max(1.5f, p.s(2));
  const Vec2 a{c.x, c.y - r}, b{c.x + r * 0.9f, c.y + r * 0.6f}, d{c.x - r * 0.9f, c.y + r * 0.6f};
  p.o.line(a.x, a.y, b.x, b.y, t, col);
  p.o.line(b.x, b.y, d.x, d.y, t, col);
  p.o.line(d.x, d.y, a.x, a.y, t, col);
}

void square(Pen& p, Vec2 c, float r, OverlayColor col) {
  const float t = std::max(1.5f, p.s(2));
  p.o.line(c.x - r, c.y - r, c.x + r, c.y - r, t, col);
  p.o.line(c.x + r, c.y - r, c.x + r, c.y + r, t, col);
  p.o.line(c.x + r, c.y + r, c.x - r, c.y + r, t, col);
  p.o.line(c.x - r, c.y + r, c.x - r, c.y - r, t, col);
}

// Arrow at the screen edge pointing toward an off-screen (or behind-camera) world position.
void edgeArrow(Pen& p, const HudContext& c, const Vec3& local, OverlayColor col, const std::string& label) {
  const Vec3 rel = c.camera.rotation.conjugate().rotate(local - c.camera.position);  // camera space
  Vec2 d{rel.x, -rel.y};
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  if (len < 1e-4f) d = {0, 1};
  else d = {d.x / len, d.y / len};
  const float cx = c.width * 0.5f, cy = c.height * 0.5f;
  const float rx = c.width * 0.44f, ry = c.height * 0.40f;
  const Vec2 a{cx + d.x * rx, cy + d.y * ry};
  const Vec2 n{-d.y, d.x};
  p.o.triangle(a.x + d.x * p.s(16), a.y + d.y * p.s(16), a.x + n.x * p.s(9), a.y + n.y * p.s(9), a.x - n.x * p.s(9),
               a.y - n.y * p.s(9), col);
  // Push the label inward by its own half width when the arrow points sideways, so they never touch.
  const float inset = p.s(22) + std::fabs(d.x) * OverlayBuilder::measure(label, p.small()) * 0.5f;
  p.centered(a.x - d.x * inset, a.y - d.y * p.s(22) - p.lineH(p.small()) * 0.5f, label, col, p.small());
}

// ---- Panels shared by the flight, surface and on-foot HUDs ------------------------------------

constexpr float kMargin = 24;  // design px from the screen edge

void economyPanel(Pen& p, const GameSession& gs, float width) {
  const float w = p.s(290), pad = p.s(14), line = p.lineH(p.small()) + p.s(4);
  const float h = line * 2 + p.s(16), x = width - p.s(kMargin) - w, y = p.s(kMargin);
  p.panel(x, y, w, h);
  float ty = y + p.s(8);
  p.o.textPx(x + pad, ty, "CREDITS", kDim, p.small());
  p.o.textPx(x + w - pad, ty, groupedCredits(gs.credits()) + " cr", kPrimary, p.small(), FontFace::Mono, TextAlign::Right);
  ty += line;
  p.o.textPx(x + pad, ty, "HOLD", kDim, p.small());
  p.o.textPx(x + w - pad, ty, std::format("{:.0f} / {:.0f} t", gs.cargo().usedTons(), gs.cargo().capacityTons()),
             gs.cargo().freeTons() < 1.0f ? kWarn : kPrimary, p.small(), FontFace::Mono, TextAlign::Right);
}

void contractsPanel(Pen& p, const GameSession& gs) {
  const auto& active = gs.contracts().active();
  if (active.empty()) return;
  const float w = p.s(470), pad = p.s(14), line = p.lineH(p.small()) + p.s(4);
  const float x = p.s(kMargin), y = p.s(kMargin), h = line * float(active.size() + 1) + p.s(16);
  p.panel(x, y, w, h);
  float ty = y + p.s(8);
  p.o.textPx(x + pad, ty, "CONTRACTS", kDim, p.small());
  const auto& locs = gs.contractLocations();
  for (const auto& k : active) {
    ty += line;
    std::string state;
    switch (k.kind) {
      case ContractKind::Courier: state = locationDisplayName(locs, k.destination); break;
      case ContractKind::Procurement:
        state = std::format("{}/{}  {}", gs.cargo().units(k.commodity), k.units, locationDisplayName(locs, k.issuer));
        break;
      case ContractKind::Bounty:
        state = k.ready() ? "DONE  " + locationDisplayName(locs, k.issuer) : std::format("{}/{}", k.kills, k.killsRequired);
        break;
    }
    const float stateW = OverlayBuilder::measure(state, p.small());
    const OverlayColor col = k.ready() ? kField : kPrimary;
    p.o.textPx(x + pad, ty, fitText(k.title, p.small(), w - pad * 2 - stateW - p.s(14)), col, p.small());
    p.o.textPx(x + w - pad, ty, state, col, p.small(), FontFace::Mono, TextAlign::Right);
  }
}

// ---- Combat -----------------------------------------------------------------------------------

void edgeFlashes(Pen& p, const HudContext& c) {
  auto edges = [&](OverlayColor col, float amount) {
    if (amount <= 0) return;
    const OverlayColor ca = col.withAlpha(std::clamp(amount, 0.0f, 1.0f) * 0.47f);
    const float t = std::min(c.width, c.height) * 0.05f;
    p.o.rect(0, 0, c.width, t, ca);
    p.o.rect(0, c.height - t, c.width, t, ca);
    p.o.rect(0, t, t, c.height - 2 * t, ca);
    p.o.rect(c.width - t, t, t, c.height - 2 * t, ca);
  };
  edges(kShield, c.shieldFlash);
  edges(OverlayColor{220, 40, 25, 255}, c.damageFlash);
}

void shipMarkers(Pen& p, const HudContext& c, const Projector& proj) {
  const Combat& cb = c.session->combat();
  const FlightState& fs = c.world.player().state;
  // Hostiles are triangles, civilians squares.
  for (const auto& n : cb.npcs()) {
    const ShipType* t = cb.shipType(n.shipDef);
    if (!t) continue;
    const bool hostile = n.hostile || t->def.ai.role == "pirate";
    const float d = length(n.state.position - fs.position);
    if (d > 8000.0f) continue;
    const bool isTarget = c.world.target() && c.world.target()->kind == TargetRef::Kind::Ship &&
                          c.world.target()->index == n.id;
    if (isTarget) continue;  // drawn with the target bracket
    const OverlayColor col = hostile ? kWarn : kNav;
    if (auto s = proj.point(n.state.position); s && proj.onScreen(*s)) {
      if (hostile) triangle(p, *s, p.s(9), col);
      else square(p, *s, p.s(7), col);
      p.text(s->x + p.s(13), s->y - p.lineH(p.small()) * 0.5f, formatDistance(d), col, p.small());
    } else if (hostile && d < 3000.0f) {
      edgeArrow(p, c, n.state.position, col, "HOSTILE " + formatDistance(d));
    }
  }
  // Lead pip for the target ship.
  if (const auto& tr = c.world.target(); tr && tr->kind == TargetRef::Kind::Ship && !c.miningMode) {
    if (auto lead = cb.leadPoint(c.world, tr->index))
      if (auto lp = proj.point(*lead); lp && proj.onScreen(*lp)) {
        const float t = std::max(1.5f, p.s(2));
        p.o.circle(lp->x, lp->y, p.s(6), t, kWarn, 12);
        p.o.line(lp->x - p.s(10), lp->y, lp->x - p.s(4), lp->y, t, kWarn);
        p.o.line(lp->x + p.s(4), lp->y, lp->x + p.s(10), lp->y, t, kWarn);
      }
  }
}

// Own systems, bottom left: shields, hull, heat and the selected weapon.
void systemsPanel(Pen& p, const HudContext& c) {
  const Combat& cb = c.session->combat();
  const Durability& d = cb.player();
  const Armament& arm = cb.playerArms();
  const float px = p.small(), line = p.lineH(px) + p.s(5), pad = p.s(14);
  const float w = p.s(320), h = line * 5 + p.s(22);
  const float x = p.s(kMargin), y = c.height - p.s(kMargin) - h;
  p.panel(x, y, w, h);
  const float labelW = p.s(52), mx = x + pad + labelW, mw = w - pad * 2 - labelW, mh = p.s(8);
  float ty = y + p.s(8);
  auto gauge = [&](const char* name, float frac, OverlayColor col, OverlayColor labelCol) {
    p.o.textPx(x + pad, ty, name, labelCol, px);
    p.meter(mx, ty + (p.lineH(px) - mh) * 0.5f, mw, mh, frac, col);
    ty += line;
  };
  const float hull = d.hullFraction();
  gauge("SHD", d.maxShield > 0 ? d.shield / d.maxShield : 0, kShield, kShield);
  gauge("HULL", hull, hull < 0.3f ? kWarn : kPrimary, hull < 0.3f ? kWarn : kPrimary);
  gauge("HEAT", arm.heatFraction(), arm.overheated ? kWarn : kDim, arm.overheated ? kWarn : kDim);
  ty += p.s(2);
  p.o.rect(x + pad, ty, w - pad * 2, 1.0f, kDim.withAlpha(0.4f));
  ty += p.s(3);
  const std::string guns = keyName(c.bindings, "weapon.guns"), mining = keyName(c.bindings, "weapon.mining");
  if (c.miningMode) {
    p.o.textPx(x + pad, ty, "[" + mining + "] MINING LASER", kPrimary, px);
    ty += line;
    p.o.textPx(x + pad, ty, guns + " guns", kDim, px);
  } else {
    const std::string name = arm.primary.empty() ? std::string("NO GUNS")
                                                 : std::format("{} x{}", arm.primary.front().def->name, arm.primary.size());
    p.o.textPx(x + pad, ty, "[" + guns + "] " + name, arm.overheated ? kWarn : kPrimary, px);
    ty += line;
    std::string second = arm.secondary.empty() ? std::string() : "RMB " + arm.secondary.front().def->name + "   ";
    p.o.textPx(x + pad, ty, second + mining + " mining", kDim, px);
  }
}

// Current target, bottom right: name, range, closing speed; ship health or the asteroid scan.
void targetPanel(Pen& p, const HudContext& c, const TargetInfo& t, OverlayColor col) {
  const GameSession* gs = c.session;
  const float px = p.small(), line = p.lineH(px) + p.s(5), pad = p.s(14);
  const NpcShip* ship = nullptr;
  if (const auto& tr = c.world.target(); gs && tr && tr->kind == TargetRef::Kind::Ship) ship = gs->combat().npc(tr->index);
  const bool asteroid = c.world.target() && c.world.target()->kind == TargetRef::Kind::Asteroid;
  const int rows = 2 + (ship ? 2 : 0) + (asteroid && gs ? 1 : 0);
  const float w = p.s(320), h = line * float(rows) + p.s(16);
  const float x = c.width - p.s(kMargin) - w, y = c.height - p.s(kMargin) - h;
  p.panel(x, y, w, h, col.withAlpha(0.4f));
  float ty = y + p.s(8);
  p.o.textPx(x + pad, ty, fitText(t.name, px, w - pad * 2), col, px);
  ty += line;
  const std::string closing = std::fabs(t.closingSpeed) < 0.5f ? std::string("--") : std::format("{:+.0f} m/s", -t.closingSpeed);
  p.o.textPx(x + pad, ty, formatDistance(t.distance), kPrimary, px);
  p.o.textPx(x + w - pad, ty, closing, kPrimary, px, FontFace::Mono, TextAlign::Right);
  ty += line;
  const float labelW = p.s(52), mh = p.s(8);
  if (ship) {
    p.o.textPx(x + pad, ty, "SHD", kShield, px);
    p.meter(x + pad + labelW, ty + (p.lineH(px) - mh) * 0.5f, w - pad * 2 - labelW, mh,
            ship->dur.maxShield > 0 ? ship->dur.shield / ship->dur.maxShield : 0, kShield);
    ty += line;
    p.o.textPx(x + pad, ty, "HULL", col, px);
    p.meter(x + pad + labelW, ty + (p.lineH(px) - mh) * 0.5f, w - pad * 2 - labelW, mh, ship->dur.hullFraction(), col);
  }
  if (asteroid && gs) {
    const std::uint32_t index = c.world.target()->index;
    const Asteroid& a = c.world.asteroids()[index];
    std::string info;
    if (gs->mining().isScanned(index)) {
      const CommodityDef* cd = gs->commodities().find(a.resource);
      info = a.resource.empty() ? std::string("Barren rock")
                                : std::format("{}  {} t  hard {:.1f}", cd ? cd->name : a.resource, a.yield, a.hardness);
    } else if (gs->mining().scanning()) {
      info = std::format("SCANNING {:.0f}%", gs->mining().scanProgress(c.world.simulationTime()) * 100.0f);
    } else {
      info = keyName(c.bindings, "ship.scan") + "  scan";
    }
    p.o.textPx(x + pad, ty, fitText(info, px, w - pad * 2), kField, px);
  }
}

// ---- Flight -----------------------------------------------------------------------------------

constexpr float kClusterH = 118;

// Speed cluster, bottom centre: speed, set-speed, modes, throttle and boost gauges.
void flightCluster(Pen& p, const HudContext& c, const PlayerShip& player) {
  const FlightState& fs = player.state;
  const float speed = length(fs.velocity);
  const float w = p.s(480), h = p.s(kClusterH);
  const float cx = c.width * 0.5f, x = cx - w * 0.5f, y = c.height - p.s(kMargin) - h;
  p.panel(x, y, w, h);
  const float cap = flightSpeedCap(player.def.flight, c.input, fs.boosting);
  // Speed: big number, the unit beside it.
  const float big = p.s(p.th.hudSpeed);
  const std::string num = std::format("{:.0f}", speed);
  const float numW = OverlayBuilder::measure(num, big), unitW = OverlayBuilder::measure(" m/s", p.body());
  const float nx = cx - (numW + unitW) * 0.5f, ny = y + p.s(8);
  p.o.textPx(nx, ny, num, kPrimary, big);
  p.o.textPx(nx + numW, ny + p.lineH(big) - p.lineH(p.body()) - p.s(4), " m/s", kDim, p.body());
  // Modes.
  const char* mode = fs.boosting ? "BOOST" : (c.input.mode == SpeedMode::Precision ? "PRECISION" : "CRUISE");
  if (c.controller.holding()) mode = speed > 0.05f ? "BRAKING" : "HOLDING";
  const std::string set = c.input.assist ? std::format("SET {:.0f}", c.input.throttle * cap) : std::string("SET --");
  p.o.textPx(cx, y + h - p.lineH(p.small()) - p.s(10), std::format("{}   {}   ASSIST {}", set, mode, c.input.assist ? "ON" : "OFF"),
             c.input.assist ? kPrimary : kWarn, p.small(), FontFace::Mono, TextAlign::Center);
  // Throttle (left): fill = set-speed, tick = actual forward speed.
  const float barW = p.s(14), barH = h - p.s(44), by = y + p.s(10);
  const float tx = x + p.s(26), ex = x + w - p.s(26) - barW;
  p.o.rect(tx, by, barW, barH, kMeterBack);
  const float zeroY = by + barH * (1.0f / 1.3f);  // throttle range [-0.3, 1]
  const float t = c.input.throttle;
  if (t >= 0) p.o.rect(tx, zeroY - t * (zeroY - by), barW, t * (zeroY - by), kPrimary);
  else p.o.rect(tx, zeroY, barW, -t / 0.3f * (by + barH - zeroY), kWarn);
  p.o.rect(tx, zeroY, barW, 1.0f, kDim);
  const float vfrac = std::clamp(dot(fs.velocity, c.ship.rotation.forward()) / std::max(1.0f, cap), -0.3f, 1.0f);
  const float vy = zeroY - vfrac * (zeroY - by);
  p.o.line(tx - p.s(6), vy, tx + barW + p.s(6), vy, std::max(1.5f, p.s(2)), kNav);
  p.o.textPx(tx + barW * 0.5f, by + barH + p.s(6), "THR", kDim, p.small(), FontFace::Mono, TextAlign::Center);
  // Boost energy (right).
  const float e = std::clamp(fs.boostEnergy / player.def.flight.boostEnergyCapacity, 0.0f, 1.0f);
  p.o.rect(ex, by, barW, barH, kMeterBack);
  p.o.rect(ex, by + barH * (1.0f - e), barW, barH * e, fs.boostLockout ? kWarn : (fs.boosting ? kNav : kPrimary));
  p.o.textPx(ex + barW * 0.5f, by + barH + p.s(6), "BST", kDim, p.small(), FontFace::Mono, TextAlign::Center);
}

void approachPanel(Pen& p, const HudContext& c, const DockApproachInfo& a, const DockingDef& dd, bool autopilot) {
  const float px = p.small(), line = p.lineH(px) + p.s(5), pad = p.s(14);
  const float w = p.s(330), h = line * 4 + p.s(16);
  const float x = c.width - p.s(kMargin) - w, y = c.height * 0.35f;
  p.panel(x, y, w, h, kNav.withAlpha(0.4f));
  float ty = y + p.s(8);
  p.o.textPx(x + pad, ty, autopilot ? "FINAL APPROACH  [AUTO]" : "FINAL APPROACH", kPrimary, px);
  auto row = [&](const char* name, const std::string& value, bool ok) {
    ty += line;
    p.o.textPx(x + pad, ty, name, kDim, px);
    p.o.textPx(x + w - pad, ty, value, ok ? kField : kWarn, px, FontFace::Mono, TextAlign::Right);
  };
  row("DIST", std::format("{:.0f} / {:.0f} m", a.distance, dd.captureDistance), a.distanceOk);
  row("SPEED", std::format("{:.1f} / {:.0f} m/s", a.speed, dd.captureSpeed), a.speedOk);
  row("ALIGN", std::format("{:.1f} / {:.0f} deg", a.angleError, dd.captureAngleDeg), a.angleOk);
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
  setPalette(c.palette);
  static const UiTheme kFallbackTheme;
  Pen p{o, c.theme ? *c.theme : kFallbackTheme, uiLayoutScale(c.height, c.uiScale)};
  const Projector proj{c};
  const float cx = c.width * 0.5f, cy = c.height * 0.5f;
  const float unit = std::min(c.width, c.height);
  const PlayerShip& player = c.world.player();
  const FlightState& fs = player.state;
  const bool shipCam = c.cameraMode == CameraMode::Chase || c.cameraMode == CameraMode::Cockpit;
  const float lineW = std::max(1.5f, p.s(2));
  auto key = [&](const char* action) { return keyName(c.bindings, action); };

  if (c.session) edgeFlashes(p, c);

  // ---- Navigation markers (stations, fields) ----
  const float small = p.small(), smallH = p.lineH(small);
  auto markerLabels = [&](Vec2 s, const std::string& name, const std::string& range, OverlayColor col) {
    p.text(s.x + p.s(15), s.y - smallH, name, col, small);
    p.text(s.x + p.s(15), s.y, range, col, small);
  };
  for (const auto& pr : c.world.props()) {
    if (pr.def.marker == "none") continue;
    const bool isTarget = c.world.target() && c.world.target()->kind == TargetRef::Kind::Prop &&
                          &c.world.props()[c.world.target()->index] == &pr;
    if (isTarget) continue;
    if (auto s = proj.point(pr.local); s && proj.onScreen(*s)) {
      diamond(p, *s, p.s(9), kNav);
      markerLabels(*s, pr.def.name, formatDistance(length(pr.local - fs.position)), kNav);
    }
  }
  for (std::uint32_t fi = 0; fi < c.world.fields().size(); ++fi) {
    const auto& f = c.world.fields()[fi];
    const Vec3 centre = c.world.frame().toLocal(f.def.center);
    const double d = length(centre - fs.position);
    if (d < f.def.radius * 0.8) continue;  // inside the field: the rocks speak for themselves
    if (auto s = proj.point(centre); s && proj.onScreen(*s)) {
      o.circle(s->x, s->y, p.s(10), lineW, kField, 6);  // hexagon = asteroid field
      markerLabels(*s, f.def.name, formatDistance(d), kField);
    }
  }

  // ---- Planets ----
  for (const auto& pl : c.world.planets()) {
    const Vec3 centre = c.world.frame().toLocal(pl.def.center);
    if (auto sp = proj.point(centre); sp && proj.onScreen(*sp)) {
      const double alt = length(c.world.frame().toSystem(fs.position).metres - pl.def.center.metres) - pl.def.radius;
      p.centered(sp->x, sp->y - smallH, pl.def.name, kDim, small);
      p.centered(sp->x, sp->y, "alt " + formatDistance(alt), kDim, small);
    }
  }

  // ---- Target ----
  auto target = c.world.targetInfo();
  OverlayColor targetCol = kNav;
  if (target) {
    targetCol = target->marker == "asteroid" ? kField : (target->marker == "hostile" ? kWarn : kNav);
    auto s = proj.point(target->local);
    if (s && proj.onScreen(*s, p.s(20))) {
      // Bracket sized to the target's apparent radius.
      const float dist = std::max(1.0f, length(target->local - c.camera.position));
      const float pxPerRad = c.height * 0.5f / std::tan(radians(c.camera.fovDeg) * 0.5f);
      const float r = std::clamp(target->radius / dist * pxPerRad, p.s(14), unit * 0.4f);
      bracket(p, *s, r, targetCol);
      p.centered(s->x, s->y - r - smallH - p.s(6), target->name, targetCol, small);
      p.centered(s->x, s->y + r + p.s(6), formatDistance(target->distance), targetCol, small);
    } else {
      edgeArrow(p, c, target->local, targetCol, formatDistance(target->distance));
    }
  }

  if (shipCam) {
    // ---- Reticle (where the nose points) ----
    const Vec3 nose = c.ship.rotation.forward();
    if (auto s = proj.direction(nose)) {
      o.circle(s->x, s->y, p.s(9), lineW, kPrimary, 20);
      o.line(s->x - p.s(22), s->y, s->x - p.s(13), s->y, lineW, kPrimary);
      o.line(s->x + p.s(13), s->y, s->x + p.s(22), s->y, lineW, kPrimary);
      o.line(s->x, s->y + p.s(13), s->x, s->y + p.s(20), lineW, kPrimary);
    }
    // ---- Velocity vector (prograde) / retrograde ----
    const float speed = length(fs.velocity);
    if (speed > 2.0f) {
      const Vec3 dir = fs.velocity / speed;
      if (auto s = proj.direction(dir); s && proj.onScreen(*s)) {
        o.circle(s->x, s->y, p.s(7), lineW, kNav, 16);
        o.line(s->x, s->y - p.s(7), s->x, s->y - p.s(15), lineW, kNav);
        o.line(s->x - p.s(7), s->y, s->x - p.s(15), s->y, lineW, kNav);
        o.line(s->x + p.s(7), s->y, s->x + p.s(15), s->y, lineW, kNav);
      } else if (auto r = proj.direction(-dir); r && proj.onScreen(*r)) {
        o.line(r->x - p.s(7), r->y - p.s(7), r->x + p.s(7), r->y + p.s(7), lineW, kNav);
        o.line(r->x - p.s(7), r->y + p.s(7), r->x + p.s(7), r->y - p.s(7), lineW, kNav);
      }
    }
    // ---- Virtual stick ----
    const float stickR = unit * 0.16f;
    const Vec2 st = c.controller.stick();
    if (c.mouseCaptured) o.circle(cx, cy, stickR, 1.0f, kDim.withAlpha(0.45f), 48);
    if (std::fabs(st.x) + std::fabs(st.y) > 0.01f) {
      o.line(cx, cy, cx + st.x * stickR, cy + st.y * stickR, 1.5f, kDim);
      o.circle(cx + st.x * stickR, cy + st.y * stickR, p.s(4), lineW, kPrimary, 10);
    }
    flightCluster(p, c, player);
  }

  // ---- Warnings: stacked downward from the upper third, always with text (never colour alone) ----
  float warnY = c.height * 0.2f;
  const float warnPx = p.s(p.th.hudWarning);
  auto warning = [&](const std::string& text, OverlayColor col = {}, float px = 0) {
    const float size = px > 0 ? px : warnPx;
    p.centered(cx, warnY, text, col.a == 255 && col.r == 255 && col.g == 255 ? kWarn : col, size);
    warnY += p.lineH(size) + p.s(6);
  };
  if (c.world.simulationTime() - player.lastImpactTime < 1.5 && std::fmod(c.time, 0.3) < 0.2)
    warning(std::format("IMPACT  {:.0f} m/s", player.lastImpactSpeed));
  else if (auto near = c.world.nearestAsteroidSurface(); near && near->first < 80.0f && near->second > 5.0f)
    warning(std::format("PROXIMITY  {:.0f} m", std::max(0.0f, near->first)));
  if (c.session) {
    const Durability& d = c.session->combat().player();
    const Armament& arm = c.session->combat().playerArms();
    const bool flying = c.session->combatActive(), blink = std::fmod(c.time, 0.5) < 0.3;
    if (flying && c.missileWarning) {
      if (blink) warning("MISSILE INBOUND");
    } else if (flying && d.hullFraction() < 0.25f) {
      if (blink) warning("HULL CRITICAL");
    } else if (flying && arm.overheated) {
      warning("WEAPONS OVERHEATED", kWarn, p.body());
    }
  }
  if (c.time < c.lookShownUntil) warning("LOOK: " + c.lookName, kPrimary);

  // ---- Status line and context hint, top centre ----
  std::string status = cameraModeName(c.cameraMode);
  if (c.controller.autofly()) status += "   AUTOFLY";
  if (!c.mouseCaptured && shipCam) status += "   MOUSE FREE - click to steer";
  p.centered(cx, p.s(12), status, kDim, small);

  // ---- Everything that needs the session ----
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
                 pb->y + (pc->y - pb->y) * t1, lineW, kNav);
        }
      }
      if (pb && proj.onScreen(*pb)) {  // pad: square marker
        square(p, *pb, p.s(12), kNav);
        p.text(pb->x + p.s(18), pb->y - smallH * 0.5f, "PAD", kNav, small);
      } else {
        edgeArrow(p, c, bay.position, kNav, "PAD " + formatDistance(length(bay.position - fs.position)));
      }
      if (dk.state() == DockState::ApproachAuthorized && dk.station() && gs.world().props()[*dk.station()].def.docking)
        approachPanel(p, c, dk.approach(), *gs.world().props()[*dk.station()].def.docking, dk.autopilot());
    }

    // Docking state + context hints. Key names come from the player's bindings.
    const std::string dockKey = key("dock.request");
    std::string hint;
    switch (dk.state()) {
      case DockState::Undocked:
        for (const auto& pr : gs.world().props())
          if (pr.def.docking && length(pr.local - fs.position) <= pr.def.docking->clearanceRange) {
            hint = dockKey + "  request docking at " + pr.def.name;
            break;
          }
        break;
      case DockState::RequestingClearance: hint = "Awaiting clearance...   " + dockKey + " cancel"; break;
      case DockState::ApproachAuthorized:
        hint = dk.autopilot() ? std::format("Docking autopilot engaged   {} manual   {} cancel", key("dock.autopilot"), dockKey)
                              : std::format("Land on the pad: slow, level, nose forward   {} autopilot   {} cancel",
                                            key("dock.autopilot"), dockKey);
        break;
      case DockState::Capture: hint = "Clamps engaging..."; break;
      case DockState::Docked:
        {
          const auto& dockDef = gs.world().props().at(*dk.station()).def.docking;
          const bool hasTerminal = dockDef && (!dockDef->market.empty() || !dockDef->services.empty());
          hint = std::format("DOCKED   {}{} leave seat   {} undock   {} save", hasTerminal ? key("ui.market") + " terminal   " : std::string(),
                             key("interact"), dockKey, key("game.save"));
        }
        break;
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
      hint = line + "   " + key("travel.engage") + " abort";
    } else if (ld.busy()) {
      hint = std::string(landStateName(ld.state())) + "   hold on";
    } else if (dk.state() == DockState::Undocked) {
      const DVec3 me = c.world.frame().toSystem(fs.position).metres;
      for (const auto& pl : c.world.planets())
        for (std::uint32_t si = 0; si < pl.def.sites.size(); ++si)
          if (length(pl.siteOrbit(si) - me) <= Landing::kRequestRange) hint = dockKey + "  begin descent to " + pl.def.sites[si].name;
      if (hint.empty()) {
        if (const auto& t = c.world.target(); t && t->kind != TargetRef::Kind::Asteroid) {
          if (auto ti = c.world.targetInfo(); ti && ti->distance > gs.world().player().def.travel.minDistance)
            hint = std::format("{}  travel drive to {}   ({} next destination)", key("travel.engage"), ti->name, key("nav.next"));
        } else {
          hint = key("nav.next") + "  select a destination";
        }
      }
    }
    if (!hint.empty() && c.showHints)
      p.pill(cx, p.s(12) + smallH + p.s(6), hint, dk.state() == DockState::Docked ? kNav : kPrimary, small);

    economyPanel(p, gs, c.width);
    if (c.showContractTracker) contractsPanel(p, gs);
    systemsPanel(p, c);
    shipMarkers(p, c, proj);

    // ---- Messages: stacked upward from the speed cluster so they can never overlap ----
    float msgBottom = c.height - p.s(kMargin + kClusterH + 12);
    auto message = [&](const std::string& text, OverlayColor col) {
      const float h = p.lineH(small) + p.s(8);
      msgBottom -= h;
      p.pill(cx, msgBottom, text, col, small);
      msgBottom -= p.s(6);
    };
    if (simT - dk.messageTime() < 6.0 && !dk.message().empty()) message("COMMS  " + dk.message(), kNav);
    else if (simT - gs.noticeTime() < 4.0 && !gs.notice().empty()) message(gs.notice(), kPrimary);
    if (simT - ld.messageTime() < 6.0 && !ld.message().empty()) message("COMMS  " + ld.message(), kNav);
    if (simT - tr.messageTime() < 4.0 && !tr.message().empty() && simT - dk.messageTime() >= 6.0)
      message(tr.message(), kPrimary);
    if (gs.lastMiningEvents().cargoFull || (!gs.mining().chunks().empty() && gs.cargo().freeTons() < 1.0f))
      message(std::format("CARGO HOLD FULL - sell at a station ({} when docked)", key("ui.market")), kWarn);
    const Mining& mn = gs.mining();
    if (mn.beamActive()) {
      const float bw = p.s(200), bh = p.s(8);
      msgBottom -= bh;
      p.meter(cx - bw * 0.5f, msgBottom, bw, bh, mn.progress(), kPrimary);
      msgBottom -= smallH + p.s(4);
      p.centered(cx, msgBottom, "MINING", kPrimary, small);
    } else if (!mn.status().empty()) {
      message(mn.status(), kWarn);
    }
  }
  if (target) targetPanel(p, c, *target, targetCol);
}

void drawInteriorHud(OverlayBuilder& o, const InteriorHudContext& c) {
  setPalette(c.palette);
  static const UiTheme kFallbackTheme;
  Pen p{o, c.theme ? *c.theme : kFallbackTheme, uiLayoutScale(c.height, c.uiScale)};
  const float cx = c.width * 0.5f, cy = c.height * 0.5f, small = p.small();
  const GameSession& s = c.session;
  const InteriorDef* def = s.activeInterior();
  const float lineW = std::max(1.5f, p.s(2));
  o.circle(cx, cy, p.s(2.5f), lineW, kPrimary, 8);  // crosshair dot
  if (const Interactable* it = s.focusedInteractable()) {
    o.circle(cx, cy, p.s(10), lineW, kNav, 16);
    p.pill(cx, cy + p.s(24), "[" + keyName(c.bindings, "interact") + "]  " + it->prompt, kNav, p.body());
  }
  if (def) {
    const std::string where = def->kind == "surface" ? "SURFACE" : locationName(s.location());
    p.centered(cx, p.s(12), where + "   " + def->name, kDim, small);
  }
  if (!c.mouseCaptured) p.centered(cx, p.s(14) + p.lineH(small), "MOUSE FREE - click to look around", kDim, small);
  economyPanel(p, s, c.width);
  if (c.showContractTracker) contractsPanel(p, s);
  if (s.world().simulationTime() - s.noticeTime() < 4.0 && !s.notice().empty())
    p.pill(cx, c.height * 0.78f, s.notice(), kPrimary, small);
  if (c.showHints) {
    auto key = [&](const char* action) { return keyName(c.bindings, action); };
    const std::string walk = key("foot.forward") + key("foot.left") + key("foot.back") + key("foot.right");
    p.text(p.s(kMargin), c.height - p.s(kMargin) - p.lineH(small),
           std::format("{} walk   {} run   {} jump   {} use   Esc menu", walk, key("foot.run"), key("foot.jump"), key("interact")),
           kDim, small);
  }
}

void drawSurfaceHud(OverlayBuilder& o, const InteriorHudContext& c) {
  setPalette(c.palette);
  static const UiTheme kFallbackTheme;
  Pen p{o, c.theme ? *c.theme : kFallbackTheme, uiLayoutScale(c.height, c.uiScale)};
  const float cx = c.width * 0.5f, small = p.small();
  const GameSession& s = c.session;
  const Landing& ld = s.landing();
  const double t = s.world().simulationTime();
  std::string status = landStateName(ld.state());
  if (ld.site()) status += "   " + s.world().planets()[ld.site()->planet].def.sites[ld.site()->site].name;
  p.centered(cx, p.s(12), status, kDim, small);
  if (ld.state() == LandState::Landed && c.showHints) {
    auto key = [&](const char* action) { return keyName(c.bindings, action); };
    p.pill(cx, p.s(18) + p.lineH(small),
           std::format("LANDED   {} leave seat   {} market   {} launch   {} save", key("interact"), key("ui.market"),
                       key("dock.request"), key("game.save")),
           kNav, small);
  }
  if (t - ld.messageTime() < 6.0 && !ld.message().empty())
    p.pill(cx, c.height * 0.78f, "COMMS  " + ld.message(), kNav, small);
  else if (t - s.noticeTime() < 4.0 && !s.notice().empty())
    p.pill(cx, c.height * 0.78f, s.notice(), kPrimary, small);
  economyPanel(p, s, c.width);
  if (c.showContractTracker) contractsPanel(p, s);
}

}  // namespace fh
