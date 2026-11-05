#include "game/foot/foot_defs.h"

#include <algorithm>

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

int FootContent::ammoIndex(const std::string& id) const {
  for (size_t i = 0; i < ammo.size(); ++i)
    if (ammo[i].id == id) return int(i);
  return -1;
}

int FootContent::weaponIndex(const std::string& id) const {
  for (size_t i = 0; i < weapons.size(); ++i)
    if (weapons[i].id == id) return int(i);
  return -1;
}

int FootContent::enemyIndex(const std::string& id) const {
  for (size_t i = 0; i < enemies.size(); ++i)
    if (enemies[i].id == id || enemies[i].id == "enemy." + id) return int(i);
  return -1;
}

const LootTable* FootContent::lootTable(const std::string& id) const {
  for (const auto& t : loot)
    if (t.id == id || t.id == "loot." + id) return &t;
  return nullptr;
}

FootContent builtinFootContent() {
  FootContent c;
  c.ammo.push_back({"ammo.rivets", "RIVETS", 200});
  FootWeaponDef w;
  w.id = "weapon.rivet_driver";
  w.name = "RIVET DRIVER";
  w.ammo = "ammo.rivets";
  w.mesh = "mesh.foot.rivet_driver";
  c.weapons.push_back(w);
  FootEnemyDef e;
  e.id = "enemy.crawler";
  e.name = "Crawler";
  c.enemies.push_back(e);
  LootTable salvage;
  salvage.id = "loot.salvage";
  salvage.prompt = "Take the salvage";
  LootEntry se;
  se.creditsMin = se.creditsMax = 150;
  se.ammo["ammo.rivets"] = {12, 12};
  salvage.entries.push_back(se);
  c.loot.push_back(salvage);
  LootTable medical;
  medical.id = "loot.medical";
  medical.mesh = "medical";
  medical.prompt = "Take the medical kit";
  LootEntry me;
  me.heal = 40;
  medical.entries.push_back(me);
  c.loot.push_back(medical);
  return c;
}

namespace {

std::optional<nlohmann::json> readJson(const AssetManifest& m, const AssetID& id, const char* type, content::Reader& r,
                                        const char* arrayKey) {
  auto path = m.resolve(id, type);
  auto text = path ? readTextFile(*path) : std::nullopt;
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains(arrayKey) || !j[arrayKey].is_array()) {
    r.error(std::string("invalid JSON or missing '") + arrayKey + "' array");
    return std::nullopt;
  }
  return j;
}

Vec3 vec(content::Reader& r, const nlohmann::json& j, const char* key, Vec3 def) { return r.vec3(j, key, false, def); }

}  // namespace

std::optional<FootContent> loadFootContent(const AssetManifest& manifest, std::vector<std::string>* errors) {
  FootContent c;
  c.startWeapons.clear();
  c.startAmmo.clear();
  bool ok = true;

  {  // ---- weapons + ammo ----
    content::Reader r("foot weapons", errors);
    auto j = readJson(manifest, "foot.weapons", "foot_weapons", r, "weapons");
    if (!j) return std::nullopt;
    if (j->contains("ammo") && (*j)["ammo"].is_array())
      for (const auto& a : (*j)["ammo"]) {
        FootAmmoDef d;
        d.id = r.str(a, "id");
        d.name = r.str(a, "name");
        d.maxReserve = int(r.num(a, "maxReserve", false, d.maxReserve));
        c.ammo.push_back(d);
      }
    for (const auto& wj : (*j)["weapons"]) {
      FootWeaponDef w;
      w.id = r.str(wj, "id");
      w.name = r.str(wj, "name");
      const std::string mode = r.str(wj, "mode", false, "auto");
      if (mode == "auto") w.mode = FootFireMode::Auto;
      else if (mode == "pellets") w.mode = FootFireMode::Pellets;
      else if (mode == "beam") w.mode = FootFireMode::Beam;
      else r.error("weapon '" + w.id + "': mode must be auto, pellets or beam");
      w.ammo = r.str(wj, "ammo");
      w.damage = float(r.num(wj, "damage"));
      w.pellets = int(r.num(wj, "pellets", false, 1));
      w.spreadDeg = float(r.num(wj, "spreadDeg", false, 0));
      w.fireInterval = float(r.num(wj, "fireInterval", false, w.fireInterval));
      w.ammoPerSecond = float(r.num(wj, "ammoPerSecond", false, 0));
      w.reloadSeconds = float(r.num(wj, "reloadSeconds", false, w.reloadSeconds));
      w.magazine = int(r.num(wj, "magazine"));
      w.range = float(r.num(wj, "range"));
      w.noiseRadius = float(r.num(wj, "noiseRadius", false, w.noiseRadius));
      w.knockback = float(r.num(wj, "knockback", false, 0));
      w.mesh = r.str(wj, "mesh");
      w.muzzle = vec(r, wj, "muzzle", w.muzzle);
      w.tracerColor = vec(r, wj, "tracerColor", w.tracerColor);
      if (wj.contains("sounds") && wj["sounds"].is_object()) {
        const auto& s = wj["sounds"];
        w.soundFire = r.str(s, "fire", false, w.soundFire);
        w.soundReload = r.str(s, "reload", false, w.soundReload);
        w.soundDry = r.str(s, "dry", false, w.soundDry);
        w.soundHit = r.str(s, "hit", false, w.soundHit);
      }
      if (w.damage <= 0 || w.magazine <= 0 || w.range <= 0) r.error("weapon '" + w.id + "': damage, magazine and range must be > 0");
      if (w.mode == FootFireMode::Beam && w.ammoPerSecond <= 0) r.error("weapon '" + w.id + "': a beam needs ammoPerSecond > 0");
      if (w.mode != FootFireMode::Beam && w.fireInterval <= 0) r.error("weapon '" + w.id + "': fireInterval must be > 0");
      if (w.pellets < 1 || w.pellets > 32) r.error("weapon '" + w.id + "': pellets must be 1..32");
      if (!manifest.find(w.mesh)) r.error("weapon '" + w.id + "': unknown mesh '" + w.mesh + "'");
      c.weapons.push_back(w);
    }
    if (j->contains("start") && (*j)["start"].is_object()) {
      const auto& s = (*j)["start"];
      if (s.contains("weapons") && s["weapons"].is_array())
        for (const auto& wid : s["weapons"])
          if (wid.is_string()) c.startWeapons.push_back(wid.get<std::string>());
      if (s.contains("ammo") && s["ammo"].is_object())
        for (const auto& [aid, n] : s["ammo"].items()) c.startAmmo[aid] = n.is_number() ? n.get<int>() : 0;
    }
    for (const auto& w : c.weapons)
      if (c.ammoIndex(w.ammo) < 0) r.error("weapon '" + w.id + "': unknown ammo '" + w.ammo + "'");
    for (const auto& id : c.startWeapons)
      if (c.weaponIndex(id) < 0) r.error("start: unknown weapon '" + id + "'");
    for (const auto& [id, n] : c.startAmmo)
      if (c.ammoIndex(id) < 0) r.error("start: unknown ammo '" + id + "'");
    if (c.startWeapons.empty()) r.error("start.weapons must name at least one weapon");
    ok &= r.ok();
  }

  {  // ---- loot ----
    content::Reader r("foot loot", errors);
    auto j = readJson(manifest, "foot.loot", "foot_loot", r, "tables");
    if (!j) return std::nullopt;
    for (const auto& tj : (*j)["tables"]) {
      LootTable t;
      t.id = r.str(tj, "id");
      t.mesh = r.str(tj, "mesh", false, "salvage");
      t.prompt = r.str(tj, "prompt", false, "Take it");
      if (t.mesh != "salvage" && t.mesh != "medical" && t.mesh != "cache" && t.mesh != "ammo")
        r.error("loot '" + t.id + "': mesh must be salvage, medical, cache or ammo");
      if (tj.contains("entries") && tj["entries"].is_array())
        for (const auto& ej : tj["entries"]) {
          LootEntry e;
          e.weight = float(r.num(ej, "weight", false, 1));
          if (ej.contains("credits") && ej["credits"].is_array() && ej["credits"].size() == 2) {
            e.creditsMin = ej["credits"][0].get<int>();
            e.creditsMax = ej["credits"][1].get<int>();
          }
          if (ej.contains("ammo") && ej["ammo"].is_object())
            for (const auto& [aid, range] : ej["ammo"].items()) {
              if (!range.is_array() || range.size() != 2) {
                r.error("loot '" + t.id + "': ammo amounts are [min, max]");
                continue;
              }
              e.ammo[aid] = {range[0].get<int>(), range[1].get<int>()};
              if (c.ammoIndex(aid) < 0) r.error("loot '" + t.id + "': unknown ammo '" + aid + "'");
            }
          e.heal = float(r.num(ej, "heal", false, 0));
          e.weapon = r.str(ej, "weapon", false);
          if (!e.weapon.empty() && c.weaponIndex(e.weapon) < 0) r.error("loot '" + t.id + "': unknown weapon '" + e.weapon + "'");
          if (e.weight <= 0 || e.creditsMax < e.creditsMin) r.error("loot '" + t.id + "': bad weight or credit range");
          t.entries.push_back(e);
        }
      if (t.entries.empty()) r.error("loot '" + t.id + "': needs entries");
      c.loot.push_back(t);
    }
    ok &= r.ok();
  }

  {  // ---- enemies ----
    content::Reader r("foot enemies", errors);
    auto j = readJson(manifest, "foot.enemies", "foot_enemies", r, "enemies");
    if (!j) return std::nullopt;
    for (const auto& ej : (*j)["enemies"]) {
      FootEnemyDef e;
      e.id = r.str(ej, "id");
      e.name = r.str(ej, "name");
      const std::string b = r.str(ej, "behaviour", false, "melee");
      if (b == "melee") e.behaviour = FootBehaviour::Melee;
      else if (b == "ranged") e.behaviour = FootBehaviour::Ranged;
      else if (b == "charger") e.behaviour = FootBehaviour::Charger;
      else r.error("enemy '" + e.id + "': behaviour must be melee, ranged or charger");
      auto f = [&](const char* k, float& v) { v = float(r.num(ej, k, false, v)); };
      f("health", e.health);
      f("speed", e.speed);
      f("bodyRadius", e.bodyRadius);
      f("bodyHeight", e.bodyHeight);
      f("hitRadius", e.hitRadius);
      f("hitCentreHeight", e.hitCentreHeight);
      f("sightRange", e.sightRange);
      f("closeSenseRange", e.closeSenseRange);
      f("hearRunRange", e.hearRunRange);
      f("forgetSeconds", e.forgetSeconds);
      f("attackRange", e.attackRange);
      f("attackReach", e.attackReach);
      f("windupSeconds", e.windupSeconds);
      f("attackCooldown", e.attackCooldown);
      f("attackDamage", e.attackDamage);
      f("preferredRange", e.preferredRange);
      f("retreatRange", e.retreatRange);
      f("projectileSpeed", e.projectileSpeed);
      f("projectileDamage", e.projectileDamage);
      f("projectileRadius", e.projectileRadius);
      f("projectileGravity", e.projectileGravity);
      f("aimSeconds", e.aimSeconds);
      f("fireCooldown", e.fireCooldown);
      f("chargeMinRange", e.chargeMinRange);
      f("chargeMaxRange", e.chargeMaxRange);
      f("chargeWindup", e.chargeWindup);
      f("chargeSpeed", e.chargeSpeed);
      f("chargeDistance", e.chargeDistance);
      f("chargeDamage", e.chargeDamage);
      f("chargeKnockback", e.chargeKnockback);
      f("recoverSeconds", e.recoverSeconds);
      f("recoverDamageScale", e.recoverDamageScale);
      f("knockbackResistance", e.knockbackResistance);
      f("dropChance", e.dropChance);
      f("visualScale", e.visualScale);
      e.dropTable = r.str(ej, "dropTable", false);
      e.visual = r.str(ej, "visual", false, "crawler");
      if (e.visual != "crawler" && e.visual != "spitter" && e.visual != "brute")
        r.error("enemy '" + e.id + "': visual must be crawler, spitter or brute");
      if (ej.contains("sounds") && ej["sounds"].is_object()) {
        const auto& s = ej["sounds"];
        e.soundWake = r.str(s, "wake", false, e.soundWake);
        e.soundHit = r.str(s, "hit", false, e.soundHit);
        e.soundDie = r.str(s, "die", false, e.soundDie);
        e.soundAttack = r.str(s, "attack", false, e.soundAttack);
      }
      if (e.health <= 0 || e.speed <= 0 || e.hitRadius <= 0) r.error("enemy '" + e.id + "': health, speed and hitRadius must be > 0");
      if (!e.dropTable.empty() && !c.lootTable(e.dropTable)) r.error("enemy '" + e.id + "': unknown dropTable '" + e.dropTable + "'");
      if (e.dropChance < 0 || e.dropChance > 1) r.error("enemy '" + e.id + "': dropChance must be 0..1");
      c.enemies.push_back(e);
    }
    if (c.enemies.empty()) r.error("no enemies");
    ok &= r.ok();
  }
  if (!ok) return std::nullopt;
  return c;
}

std::uint64_t lootSeed(const std::string& key) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char ch : key) h = (h ^ ch) * 1099511628211ull;
  return h;
}

LootRoll rollLoot(const LootTable& table, std::uint64_t seed) {
  auto next = [&]() {
    std::uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  };
  auto uniform = [&]() { return float(next() >> 40) / float(1u << 24); };
  auto range = [&](int lo, int hi) { return hi <= lo ? lo : lo + int(next() % std::uint64_t(hi - lo + 1)); };
  LootRoll out;
  if (table.entries.empty()) return out;
  float total = 0;
  for (const auto& e : table.entries) total += e.weight;
  float pick = uniform() * total;
  const LootEntry* chosen = &table.entries.back();
  for (const auto& e : table.entries) {
    pick -= e.weight;
    if (pick <= 0) {
      chosen = &e;
      break;
    }
  }
  out.credits = range(chosen->creditsMin, chosen->creditsMax);
  for (const auto& [id, mm] : chosen->ammo)
    if (const int n = range(mm[0], mm[1]); n > 0) out.ammo[id] = n;
  out.heal = chosen->heal;
  out.weapon = chosen->weapon;
  return out;
}

}  // namespace fh
