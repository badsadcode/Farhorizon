#pragma once
// Combat and AI tuning from ship data (kept separate to avoid include cycles with ship_def.h).
#include <string>
#include <vector>

namespace fh {

struct MountDef {
  std::string mount;    // ship socket name
  std::string weapon;   // weapon id
};

struct CombatParams {
  float shieldRegen = 8.0f;       // shield points per second once recharging
  float shieldDelay = 4.0f;       // seconds after the last hit before recharging
  float heatCapacity = 100.0f;
  float heatDissipation = 22.0f;  // per second
  float collisionDamagePerMs = 1.5f;  // hull damage per m/s of impact above 10 m/s
  std::vector<MountDef> primary;
  std::vector<MountDef> secondary;
};

// NPC behaviour knobs (design doc 8: readable finite state machine).
struct AiParams {
  std::string faction = "neutral";  // pirates, traders, ...
  std::string role = "trader";      // pirate | trader
  float detectionRange = 2500.0f;
  float attackRange = 900.0f;
  float preferredRange = 450.0f;
  float fleeHullFraction = 0.3f;
  float aimToleranceDeg = 4.0f;
  std::string lootCommodity = "goods.parts";
  int lootMin = 1, lootMax = 3;
  int bounty = 0;                   // informational (contracts pay bounties)
};

}  // namespace fh
