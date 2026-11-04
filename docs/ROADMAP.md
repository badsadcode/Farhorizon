# FARHORIZON roadmap

_Agreed with the owner, 2026-11-04. The order matters: each step makes the next one cheaper. Detailed
status lives in `STATUS.md`._

1. **Visuals (in progress).** Shadows, ambient occlusion, reflections, surface detail. ADR 0011.
2. **Blender as the content and point-of-interest editor.** First version done in 0.8.0 (ADR 0012).
   Still to come: docking with a map in the game world, permanent ships from the add-on.
   * Materials and textures from Blender: base colour, normal, metallic-roughness, emissive,
     occlusion. Lights via `KHR_lights_punctual`.
   * Whole maps as `.glb` files, with entities placed through the add-on and solid meshes by default.
   * A scale reference (1.8 m player, doorway) and an artist guide: `docs/BLENDER_GUIDE.md`.
3. **Points of interest and FPS gameplay.** Docking with a derelict and exploring it works (0.8.2). A first
   fight works too (0.9.0): a gun, one enemy, loot, and a simulation that already takes a list of players.
   Still to do: more weapons and enemies, real pathfinding, and loot tables as content files.
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

## Graphics features to add as the game grows
Picked up when a map or scene calls for them, not as one big step. Rough order of value:
* **Screen-space reflections:** sharp reflections of whatever is on screen, including moving things, on
  top of the probes. They cannot show anything off screen, so they need the probes as a fallback.
* **Real planar mirrors:** a surface you mark as a mirror, for when a map needs one.
* **Transparency:** glass, visors, holograms.
* **Depth of field:** best kept for menus, cutscenes and ship views. In first person and in flight it
  tends to hide what you are trying to see, so it would be subtle or off by default, with an option.
* **Motion blur and temporal anti-aliasing:** TAA also cleans up shimmer on thin details.
* **Volumetric light shafts:** for example sun through a hull breach.
* **Ray tracing:** as an option only, for cards that have it.

## Decisions already made
* **Money and cargo:** personal wallets, plus a shared hold on a shared ship.
* **World:** pauses when nobody is connected.
* **Planet size:** much smaller than today's 2,400 km Varga, around 300-600 km.
* **All content stays original.**

**When maps get bigger:** give each part of the screen its own list of lights (clustered shading) and reuse
the shadows of lamps that do not move. The owner agreed this comes after weapons, enemies and loot.

Steps 3 and 4 may swap: step 3 is the faster fun, but FPS combat written before step 4.1 would need
reworking for multiple players. Write it against the multi-player simulation if the two overlap.
