#pragma once
// The player-rebindable actions (Options > Controls) and the rules for rebinding them. GPU-free
// and SDL-free so the rules are unit-tested: key names are plain strings here.
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fh {

// When an action is live. Flight and on-foot actions may share keys (W flies and walks); an
// `Always` action must not share a key with anything, or one press would do two things.
enum class BindContext { Flight, Foot, Always };

struct Bindable {
  const char* action;
  const char* label;
  BindContext context;
};

std::span<const Bindable> bindables();

// Returns the key names currently bound to an action.
using BindingLookup = std::function<std::vector<std::string>(const std::string& action)>;

// Keys the app handles itself in every mode (menu, quit, fullscreen, screenshots, developer
// tools) cannot be given to a game action.
bool isReservedKey(const std::string& key, const BindingLookup& lookup);

// Label of a bindable action that would fire together with `action` on `key`; empty if none.
std::string bindingConflict(const std::string& action, const std::string& key, const BindingLookup& lookup);

}  // namespace fh
