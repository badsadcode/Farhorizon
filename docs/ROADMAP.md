# FARHORIZON roadmap

_Agreed with the owner, 2026-11-04. The order matters: each step makes the next one cheaper. Detailed
status lives in `STATUS.md`._

1. **Visuals (in progress).** Shadows, ambient occlusion, reflections, surface detail. ADR 0011.
2. **Blender as the content and point-of-interest editor.**
   * Materials and textures from Blender: base colour, normal, metallic-roughness, emissive,
     occlusion. Lights via `KHR_lights_punctual`.
   * Whole maps as `.glb` files with naming conventions: `COL_` colliders, `SPAWN_`, `USE_`, later
     `ENEMY_` and `LOOT_` empties.
   * A scale-reference scene (1.8 m player, doorways, ship bounds) and an artist guide.
3. **Points of interest and FPS gameplay.** Dock with or land near a site, explore it on foot, fight
   and loot. For example a derelict in space. Needs first-person weapons, a damage model on foot,
   enemy AI with navigation, and loot tables.
4. **Co-op, 1-4 players.**
   1. Multi-player simulation: separate players from the world; separate ship reference frames;
      tested headless with simulated players.
   2. Walkable moving ship, multi-deck, in single-player. Reverses "no walking inside a moving ship"
      in ADR 0005: characters are stored relative to their ship.
   3. Network test on a LAN: one flies, another walks the ship. Server-authoritative, ENet.
   4. `farhorizon_server.exe` with a real GUI (setup, players, logs tab) and no Vulkan requirement.
      Clients are untrusted; password required. Friend's PC hosts; one forwarded UDP port.
   5. Crew stations (helm, guns, engineering, mining) and ship ownership. Solo must always work.
5. **Explorable planets.** Cube-sphere, streamed chunks, real landing, generated places to discover.
   Port suitable algorithms from the owner's Thornwake project (noise, mountains, canyons, biomes;
   C#/Godot, so ported rather than linked). Start with dry worlds; skip rivers and lakes at first.

## Decisions already made
* **Money and cargo:** personal wallets, plus a shared hold on a shared ship.
* **World:** pauses when nobody is connected.
* **Planet size:** much smaller than today's 2,400 km Varga, around 300-600 km.
* **All content stays original.**

Steps 3 and 4 may swap: step 3 is the faster fun, but FPS combat written before step 4.1 would need
reworking for multiple players. Write it against the multi-player simulation if the two overlap.
