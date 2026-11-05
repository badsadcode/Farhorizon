# ADR 0014 — Crew and places: several players in one session

_Status: accepted (0.11.0, roadmap step 4.1)._

## Context
Co-op for 1–4 players is roadmap step 4. Before any networking, the simulation has to stop assuming
one player: until 0.10 `GameSession` held one character, one wallet, one "where am I" and one fight on
foot. Agreed rules from the roadmap: personal wallets and a shared hold on a shared ship; the world
pauses when nobody is connected; solo must always work.

## Decision
* **Players are separate from the world.** `GameSession` keeps a list of `CrewMember`s (id, name,
  location, pilot flag, place, body, input, wallet, notices, their own on-foot events) and an
  index-aligned list of `FootPlayer`s (health, loadout). Slot 0 is this machine's player and is never
  removed. Every single-player accessor (`location()`, `character()`, `credits()`, `perform()` and so on)
  is a thin wrapper over slot 0, so the app and the old tests did not change.
* **Places are instances.** A walkable place (ship interior, station, map, surface) exists as an
  `Instance` only while someone is in it: its colliders, terrain, gravity, its own `FootCombat` (enemies,
  projectiles) and drops. It is created on first entry (and its nav grid baked once per map) and put
  away when the last person leaves. What was killed or taken stays in `clearedMarkers_`, so leaving and
  coming back does not respawn anything. Two players in two different places fight two different fights;
  two players in the same place share one. Characters are stored in the place's own coordinates, which
  is the reference frame the walkable moving ship (4.2) will build on.
* **Events are told from each player's point of view.** The fight numbers its players 0..n-1; the session
  re-numbers each event per crew member, so `player == 0` always means "you" and `1 + slot` someone else.
  The HUD and audio did not change.
* **Crew rules.** Taking a seat makes you the pilot if nobody is, otherwise a passenger. Only the pilot
  undocks or launches, and only when everyone is seated (`crewAboard()`). Leaving the seat stays
  limited to a parked ship.
* **Money.** Each crew member has a wallet; trades charge whoever made them, and the cargo goes into the
  one shared hold. Loot on foot (credits in caches and drops) goes to whoever picked it up. Contract
  rewards, bounties, upgrades, repairs and the insurance deductible stay with the host (slot 0) for now;
  shared ship expenses get their own rules with crew stations (4.5).
* **Away crew keep their money.** When someone leaves, their wallet is kept by name and given back when
  they rejoin. Saves (v6) store these wallets under `crew.wallets`; nobody is "connected" after a load.
* **Simulated players.** `SimPlayer` (`game/session/sim_player.*`) drives one crew member only through
  their input, like a person at a keyboard: walk to a point along the nav grid (giving up on unreachable
  goals), turn to and shoot the nearest enemy. Tests use it headless, including a soak with four of them
  that must give identical results twice, which is what lockstep checks or replays will need later.

## Consequences
* No networking yet: crew are added in-process. The "world pauses when nobody is connected" rule belongs
  to the server (4.3/4.4), where "connected" exists.
* Ship flight is still one ship and one pilot; gunner and engineer seats come with crew stations (4.5).
* `fixedUpdate` now steps every instance; a place with nobody in it costs nothing.
