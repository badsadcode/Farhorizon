#include "game/ui/bindings.h"

#include <algorithm>
#include <iterator>

namespace fh {

namespace {
using C = BindContext;
constexpr Bindable kBindables[] = {
    {"ship.throttleUp", "Throttle up", C::Flight},
    {"ship.throttleDown", "Throttle down", C::Flight},
    {"ship.throttleZero", "Zero throttle", C::Flight},
    {"ship.strafeLeft", "Strafe left", C::Flight},
    {"ship.strafeRight", "Strafe right", C::Flight},
    {"ship.strafeUp", "Strafe up", C::Flight},
    {"ship.strafeDown", "Strafe down", C::Flight},
    {"ship.rollLeft", "Roll left", C::Flight},
    {"ship.rollRight", "Roll right", C::Flight},
    {"ship.pitchUp", "Pitch up", C::Flight},
    {"ship.pitchDown", "Pitch down", C::Flight},
    {"ship.yawLeft", "Yaw left", C::Flight},
    {"ship.yawRight", "Yaw right", C::Flight},
    {"ship.recenterStick", "Recentre stick", C::Flight},
    {"ship.boost", "Boost", C::Flight},
    {"ship.toggleAssist", "Flight assist", C::Flight},
    {"ship.togglePrecision", "Precision mode", C::Flight},
    {"weapon.guns", "Select guns", C::Flight},
    {"weapon.mining", "Select mining laser", C::Flight},
    {"ship.firePrimary", "Fire (keyboard)", C::Flight},
    {"ship.fireSecondary", "Rocket (keyboard)", C::Flight},
    {"ship.mine", "Mining laser (hold)", C::Flight},
    {"ship.scan", "Scan asteroid", C::Flight},
    {"target.next", "Next target", C::Flight},
    {"target.nearestHostile", "Nearest hostile", C::Flight},
    {"target.nearestAsteroid", "Nearest asteroid", C::Flight},
    {"target.clear", "Clear target", C::Flight},
    {"nav.next", "Next destination", C::Flight},
    {"travel.engage", "Travel drive", C::Flight},
    {"dock.request", "Dock / land / launch", C::Flight},
    {"dock.autopilot", "Docking autopilot", C::Flight},
    {"camera.cycle", "Chase / cockpit camera", C::Flight},
    {"foot.forward", "Walk forward", C::Foot},
    {"foot.back", "Walk back", C::Foot},
    {"foot.left", "Walk left", C::Foot},
    {"foot.right", "Walk right", C::Foot},
    {"foot.run", "Run", C::Foot},
    {"foot.jump", "Jump", C::Foot},
    {"interact", "Interact / leave seat", C::Always},
    {"ui.market", "Terminal", C::Always},
    {"game.save", "Quicksave", C::Always},
    {"game.load", "Quickload", C::Always},
    {"app.toggleMouse", "Free / capture mouse", C::Always},
};

constexpr const char* kSystemActions[] = {"app.menu",         "app.quit",      "window.fullscreen", "debug.screenshot",
                                          "debug.overlay",    "debug.autofly", "debug.forceDock",   "debug.reloadAssets",
                                          "visual.cycleLook", "camera.dev"};

bool hasKey(const std::vector<std::string>& keys, const std::string& key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}
}  // namespace

std::span<const Bindable> bindables() { return kBindables; }

bool isReservedKey(const std::string& key, const BindingLookup& lookup) {
  return std::any_of(std::begin(kSystemActions), std::end(kSystemActions),
                     [&](const char* a) { return hasKey(lookup(a), key); });
}

std::string bindingConflict(const std::string& action, const std::string& key, const BindingLookup& lookup) {
  const auto self = std::find_if(std::begin(kBindables), std::end(kBindables),
                                 [&](const Bindable& b) { return action == b.action; });
  const C context = self != std::end(kBindables) ? self->context : C::Always;
  for (const Bindable& b : kBindables) {
    if (action == b.action) continue;
    const bool overlap = context == C::Always || b.context == C::Always || context == b.context;
    if (overlap && hasKey(lookup(b.action), key)) return b.label;
  }
  return {};
}

}  // namespace fh
