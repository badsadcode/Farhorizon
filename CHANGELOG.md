# Changelog

## 0.15.0 — Playing across an ocean (2026-11-05)
From the first real game between Poland and Canada.
* **Fixed: rubberbanding when walking.** The game compared your position with the server's as if the network took no time,
  so at transatlantic delay ordinary walking looked like an error and got pulled back. It now compares with where you
  were when you sent those controls, and eases away real differences.
* **The ship and other players move evenly.** They're shown a tenth of a second behind, to hide uneven arrival.
* **Fixed: undocking was refused while anyone was walking the ship.** The clamps now wait only for someone outside it, and
  the message says who.
* **Fixed: a seated passenger could never take an empty helm.** Pressing a helm key or touching the flight controls takes it.
  The cockpit's hint line says who has the helm.
* **Terminals work on a server.** Everyone can buy and sell, with their own money and the ship's shared hold. Repairs,
  upgrades and contracts still aren't available when connected.
* New players on a server start with the same money as a new game. They used to start with nothing.
* **Fixed: a passenger could not leave their seat while the pilot docked.** Only the pilot has to stay.
* Everyone must update: the connection protocol changed. `--net-lag <ms>` pretends the network is that slow, for testing.

## 0.14.0 — A server of its own (2026-11-05)
Roadmap step 4.4: `farhorizon_server.exe`, for leaving running on the machine that hosts the game.
* **A proper window, not a console.** Setup (name, port, password, players), Players (who is on, where they are, and a
  Remove button) and Log. Start, stop and save are buttons. Settings are remembered.
* **It needs no Vulkan and no real graphics card.** It imports no Vulkan, and draws with SDL's 2D renderer.
* **A password is required.** The server won't start without one. It never crosses the network as readable text:
  each connection proves it knows the password using a number picked for that connection. Players use `--password`.
* **The world keeps going between visits.** It holds still while nobody is connected, is saved when the server
  stops, and picks up again next time. Players' money waits for them by name.
* The first person aboard gets the helm. The server itself isn't a player and takes no seat.
* **One zip with the game and the server:** `python tools/package.py` writes it to `dist/`. Unzip and run, with nothing to
  install. A Help tab walks through the router, and the Log tab shows each connection, which tells you if it worked.
* **Join game in the main menu.** Type the address, your name and the password. The address and name are remembered;
  the password isn't. If joining fails, or the connection is lost later, you come back to this screen with the reason.
* The connection protocol changed, so 0.13 games can't join.

## 0.13.0 — Someone else aboard (2026-11-05)
Roadmap step 4.3: the first networked test. One person flies while another walks the ship, on two computers.
* **Host with `--host`, join with `--join <address>`, and pick a name with `--name`.** It uses UDP port 27777,
  or `--port` for another. There is no menu for it yet. See `docs/NETWORK_TEST.md`.
* **The host runs the world.** Clients send their controls and show what the host reports, so they can't cheat
  by changing their copy. Your own legs still respond at once, and a disagreement is corrected.
* **You can see each other,** in a new work suit with a name overhead, including whoever sits at the helm. The
  deck readout says who has it.
* **Anyone can take the helm** if it is free, and steer from the far end of the connection.
* **Not shared yet, so switched off in a networked game:** pirates, mining, landing, terminals for anyone but the
  host, and walking into a place with enemies. Other players' sounds and a password also come later.
* The world no longer pauses for a menu in a networked game.
* New library: ENet (MIT licence).

## 0.12.0 — Leaving the helm (2026-11-05)
Roadmap step 4.2, in single-player: the ship is a place you can walk around while it flies.
* **Press F in flight to leave the seat.** The ship brakes to a stop by itself, and if the travel drive is
  running it holds its course. You can't leave during docking or landing, and the airlock stays shut.
* **The Sparrow has two decks, joined by a stair.** Cockpit, galley and bunks above; the hold, the airlock and the
  engine room below. It replaces the old one-room cabin. All original, built by `tools/blender/build_ship_interior.py`.
* **The windows are real.** Through them you see the actual planets, stations, ships and nebulae, moving as the ship
  moves. Sunlight comes in through them and shifts as the ship turns.
* **The ship doesn't wait for you.** Pirates keep attacking with nobody at the helm, a crash throws you towards the
  bow and shakes the view, and a small readout shows the speed and hull while the helm is empty.
* Saves made while walking aboard in flight load back there.
* Enemy and player route grids are now baked for every map, not only those with enemies.

## 0.11.0 — Room for a crew (2026-11-05)
Groundwork for co-op (roadmap step 4.1). Nothing changes when you play alone, which is the point.
* **The game now tracks up to four players in one session.** Each has their own body, health, guns,
  ammunition and wallet, and can be somewhere different: one in the cockpit, one walking the ship, one
  out in a derelict.
* **Each place runs on its own.** A derelict with someone in it has its own enemies and fight; two
  people in the same place share it. A place nobody is in sleeps, and what was killed there stays dead.
* **Crew rules.** The first to sit down takes the helm; others ride as passengers. The pilot can only
  undock or launch when everyone is seated.
* **Personal wallets, shared hold.** Whoever pays for cargo pays from their own wallet, and it goes into the
  ship's one hold. Loot you pick up is yours. Crew who leave keep their money for when they come back,
  in saves too (save version 6; older saves load as before).
* **Simulated players** drive crew members in tests, with no window or network: four of them walking and
  fighting through a map for 40 seconds must come out identical twice. ADR 0014 has the details.

## 0.10.1 — Low-spec mode (2026-11-05)
* **Low spec mode** (Options > Graphics, or `--low-spec` on the command line) turns off shadows, ambient
  occlusion, reflections and volumetric clouds, shades at most 6 lamps, and renders the 3D scene at half
  resolution while the HUD stays sharp. On the software renderer used for testing it runs about 4 times faster.
* **Render scale** (Options > Graphics): render the 3D scene at 50 to 100% of the window size, then scale it up.
* CI's UI scripts run in low-spec mode, since they test input and logic. The smoke run keeps full lighting.

## 0.10.0 — More guns, more things in the dark (2026-11-05)
* **Two new weapons, found in tool caches.**
  * The **flechette caster** throws a cone of nine darts. It is brutal up close and kicks hard.
  * The **arc cutter** is a short beam that burns for as long as you hold the trigger and drains its cell.
  * The rivet driver is still your starting gun. Keys **1 2 3** or the mouse wheel switch weapons. Each gun has its own magazine and
    its own ammunition (rivets, flechettes, cell charge), shown on the HUD.
* **Two new enemies.**
  * The **spitter** keeps its distance, rears up and lobs a slow glowing glob. Step aside and it misses.
    Get close and it backs away.
  * The **brute** is slow and armoured. It lowers its head, then charges in a straight line and sends you
    flying. Dodge the charge and it stands stunned for a moment, taking double damage.
* **Enemies find their way to you now.** Each map bakes a walkable grid the first time you enter it
  (stairs, ramps and catwalks included), and enemies follow paths around walls, through doors and up to
  where you are instead of scraping along a wall and giving up. They also hear you running.
* **Loot comes from loot tables:** salvage, medical kits, ammunition boxes and tool caches, each rolled once per map and
  marker, so reloading cannot reroll it. Killed enemies sometimes drop ammunition or a medical patch. Walk over a drop
  to take it. Drops stay where they fell, in saves too.
* **Weapons, enemy types and loot are content files** (`content/foot/`), checked by the content validator. In
  your own maps set an Enemy Spawn's `fh_kind` to `crawler`, `spitter` or `brute`, and a Loot entity's to
  `salvage`, `medical`, `ammo`, `cache_flechette` or `cache_arc`.
* **A test range** (`content/maps/test_range.glb`, built by `tools/blender/build_test_range.py`) has all three
  enemy types and both caches. Play it with `--map content/maps/test_range.glb`.
* New models (two guns, spitter, brute, cache, ammunition box) and 13 new sounds, all generated from scratch by
  the scripts in `tools/`.
* Map raycasts are much faster (grid traversal instead of sampling), which the navigation bake needed.
* Saves move to version 5; version 4 saves still load (your rivet driver, magazine and rivets carry over).

## 0.9.0 — Something is still moving back there (2026-11-04)
* **You can fight on foot.** In a map with enemies placed in it you carry a rivet driver: a fitter's tool, 12
  rivets to a magazine. Left mouse fires, R reloads, and an empty gun reloads itself.
* **The first enemy: the crawler.** It sleeps until it sees or hears you, then chases and lunges. Step back
  during its lunge and it misses. Four hits kill one, and the dead stay dead, in saves too.
* **Loot:** walk up and press F. Salvage gives 150 cr and 12 rivets; a medical kit gives back 40 health.
* **If you go down,** you wake at the way in with 60 health and the crawlers settle again. Nothing is taken from you.
  Your ship patches you up and refills the gun.
* **A weak lamp on your suit,** L to switch it off, because you cannot fight what you cannot see.
* New sounds and models, all made from scratch by scripts in `tools/`.
* **Fixed: 0.8.3 sent 16 lights to the shader, but the shader only looped over 8.** With more than 8 lights in a
  map, the ones furthest from where you started stayed dark. All 16 work now, and a test guards it.
* Saves move to version 4; older saves still load.

## 0.8.3 — Lights stay on as you walk (2026-11-04)
* **Fixed: parts of a map lit up suddenly as you walked towards them.** Only the 8 lights nearest the
  player were used, so in a map with more than 8, lights further away switched off and back on.
* **A scene can now use 16 lights at once, and all of them cast shadows.** Before it was 8, with shadows on 4.
* **If a map has more than 16,** the ones that matter most where you stand are used, big lamps before tiny
  nearby lights, and a change fades in or out instead of snapping.
* **An emissive surface next to a real lamp no longer adds a second light,** even if its setting was not
  turned off. A small emissive surface crossing the middle of a map is now one light, not two.
* Tests use their own map, `tests/data/derelict_fixture.glb`, instead of the working map in `content/maps`.
  `content/maps/untitled*.glb` is no longer tracked by git.

## 0.8.2 — The Ash Wren: a derelict you can fly to (2026-11-04)
* **The sample derelict is in the world.** The Ash Wren drifts about 42 km from Kestrel. Select it with N,
  take the travel drive, set down on its hull, walk through your airlock and explore. Its Exit takes you
  back to your ship. Saving and loading inside it work.
* **Any Blender map can be a place in the world:** a manifest entry and a scene object. See the guide.
* **Emission now lights the room.** Emissive surfaces cast real local light in their own colour, with
  strength from Emission Strength and the surface's size. Long strips become several lights. A fixture
  with its own lamp can be set to only glow, in the add-on's Selected panel.
* **Maps no longer get ambient light that follows the camera.** It made rooms change as you walked around.
  Ambient in a map comes from Ambient Light and Reflection Probes.
* Radio chatter fits a wreck: nobody answers. Places with no market or services are not offered as
  contract destinations, and the docked hint offers a terminal only where there is one.
* Invisible Walls are never drawn, even when the map is used as a ship's exterior.
* New `--start-at <object>` debug flag, and UI script checks for location and docking. Tests: 136.

## 0.8.1 — Reflections you can place (2026-11-04)
* **Reflection Probe entity:** place it in a room and scale its box to fit. Shiny surfaces inside reflect
  that room, sharper than before and lined up with the walls. Up to 8 per map, captured once at load.
* **Plain materials work as set:** a Blender material with no textures now keeps your exact roughness,
  so chrome and polished metal need only the sliders. Maps and ships from Blender get no automatic grime.
* The sample derelict has a polished panel, standing water and two probes.
* Near-mirrors no longer show a lamp's highlight on top of its reflection.

## 0.8.0 — Blender is the editor (2026-11-04)
* **FARHORIZON add-on for Blender** (`tools/blender/`, guide in `docs/BLENDER_GUIDE.md`): build maps and
  ships in Blender and press **Play in Game**. Entities with icons in the viewport, a player-sized guide,
  collision choices per object, and a check that catches what would not export.
* **Maps are one standard `.glb`:** geometry, materials, lights and entities together.
* **Textures:** normal, metallic-roughness, emissive and occlusion maps reach the game, with mipmaps and
  anisotropic filtering.
* **Lights from Blender:** point lamps with shadows, flicker, and a Sun with real shadows.
* **Mesh collision:** walls, floors, ramps and stairs work as modelled, with no separate collision.
* **Entities:** Player Start, Exit, Data Log, Terminal, Pilot Seat, Airlock. Enemy Spawn and Loot are
  placed and saved, and show as glows until those systems exist.
* **Ship preview:** `--ship-mesh` flies an exported ship with its own engine and camera points.
* **Sample derelict:** `content_src/maps/derelict_sample.blend`, generated through the add-on with
  original textures. Its cargo bay has a hull breach, sunlight, stairs and a catwalk.
* New flags: `--map`, `--ship-mesh`. `content_validator --map` checks a map. Tests: 134.

## 0.7.3 — Stage 7, part 4: exhaust, speed and the tunnel (2026-11-04)
From the owner's first look at 0.7.2.
* **Exhaust trails are now continuous ribbons of turbulent gas,** with wider billowing edges, so
  there can be no gaps between particles. They fade out when the engine stops.
* **Engine glow:** an irregular, breathing outline with wandering tongues of light and a gentle pulse.
  Each engine has its own rhythm.
* **Speed dust:** motes hang still in space around the ship and streak past as you move, in a mix of
  widths. They show nothing at a standstill and hand over to the tunnel at drive speed.
* **Travel drive tunnel:** streaks and gas on the inside of a slowly twisting tunnel around the flight
  path, with the destination left clear in the middle. Streaks now range from slim to broad.
* Tests: 123.

## 0.7.2 — Stage 7, part 3: trails, drive effects, nebulae, full stop (2026-11-04)
* **Full stop (X):** brakes on every axis and rotation to a standstill and holds there, with flight
  assist on or off, until you touch a flight control. Your assist and precision settings come back
  unchanged. The speed panel shows BRAKING, then HOLDING. Replaces "zero throttle".
* **Engine trails:** textured particle exhaust that follows thrust, stays unbroken at high speed, and
  gains sparks under boost. Other ships have them too. Per-ship `trailRate`, `trailLife`, `trailSize`.
* **Travel drive effects:** sparks gather at the nozzles while spooling, a flash on engaging, streaks
  racing past and long drive plumes in transit, another flash on arrival. Main engines rest while the
  drive holds the ship.
* **Nebulae (look test):** a colourful nebula painted in the distant sky, plus three real volumetric
  clouds you can fly through: around the Cinder Drift, and two large ones nearby. New Options >
  Graphics toggle, Volumetric clouds. Data: `sky.nebulaColorB`, `sky.nebulaColorC`, `clouds`.
* Particle textures are generated at startup, so no art files are needed yet.
* **Testing:** scripted full-stop run, including with assist off. Tests: 113.
* **Dev log:** `docs/devlog/index.html`, the story of the game so far in plain language, with screenshots.

## 0.7.1 — Stage 7, part 2: lighting (2026-11-04)
The first version that lights the world properly. Before and after:
`docs/screenshots/stage7_lighting_before_after.jpg`.
* **Sun shadows:** three cascades out to 1.4 km, stable while the camera moves.
* **Lamp shadows:** the 4 nearest lamps in interiors and on planets cast real shadows, so light no
  longer passes through walls.
* **Ambient occlusion:** contact darkening where surfaces meet, from a new depth prepass.
* **Live reflections:** a probe around the camera supplies reflections and ambient light from the
  real surroundings, including interiors. It updates one cube face per frame.
* **Surface detail in the shader:** grime, panel seams, per-panel tone and fine bump, since the models
  have no textures yet. Strength is per look (`surfaceDetail`).
* **Much calmer picture:** grain, scanlines and chromatic aberration turned well down on the default
  looks. Shadow sides of ships and the hangar lifted so they stay readable.
* **Options > Graphics:** new toggles for Shadows, Ambient occlusion and Reflections.
* **Fixed:** vertical stripes on the Sparrow's cabin wall, where a hatch sat exactly in the wall's plane.
* **Docs:** `docs/ROADMAP.md` records the agreed order: Blender as the content and point-of-interest
  editor, FPS exploration, co-op, then planets. ADR 0011 covers the lighting.
* Requires the `imageCubeArray` Vulkan feature. Tests: 101, plus scripted runs of the new toggles.

## 0.7.0 — Stage 7, part 1: the UI pass (2026-11-04)
First version built and run on a real GPU (RTX 3080). Stage 7 continues with the Blender pipeline
and lighting.
* **Menus:** main menu with the world turning behind it, pause menu (Esc), load and save with
  3 slots + quicksave + autosave, confirmations.
* **Options:** graphics (look, fullscreen, VSync, field of view, interface scale, camera shake),
  audio volumes, gameplay toggles, and controls with **in-game key rebinding**. Conflicts are
  flagged and name the other action, the app's own keys are reserved, and Reset keys restores the
  defaults. Settings live in `settings.json`, beside the game.
* **New flight HUD:** TrueType font, consistent panels, and scaling with Interface scale. A new
  target panel shows range, closing speed, ship shields and hull, or the asteroid scan. Messages
  and warnings stack, so they no longer overlap.
* **Hints show your actual keys** in the HUD and terminal, including after rebinding.
* **Restyled station terminal:** every row has its own buttons (BUY / SELL / ALL, FIT, TAKE, DROP),
  and clicking a row only selects it, so a stray click can no longer spend money or accept a job.
  Long lists scroll.
* TrueType UI font (Share Tech Mono + Share Tech, SIL OFL) and a data-driven UI theme.
* Mouse: Esc opens the menu, backtick frees the mouse, and clicking the game window recaptures it.
* **Fixed:**
  * Validation errors on real GPUs: `shaderDemoteToHelperInvocation` is now enabled. Shaders using
    `discard` were invalid without it. Software rendering in CI had not shown this.
  * The pause menu closed itself on the frame it opened.
  * During key capture, Up, Down and Tab moved around the menu, F10 quit the game, and binding
    Enter re-triggered the prompt.
  * After a fullscreen toggle, a resting mouse stole keyboard focus, so the next Enter changed the
    wrong setting.
  * The mouse wheel could leave focus on a row scrolled out of view.
  * Dragging the Interface scale slider moved the slider under the mouse.
  * Focus could start on a disabled menu item, leaving nothing selected.
* **Testing:** `--ui-script` drives the real menus and terminal with scripted input events and
  checks the results (`tests/ui_scripts/`, also in CI). New `--settings` flag. Tests: 101.

## 0.6.0 — Stage 6: combat, contracts and upgrades (2026-11-04)
* **Combat:** shields and hull with delayed recharge, collision damage, weapon heat and
  overheat lock. Twin **pulse lasers** (simulated bolts, lead pip) and homing **seeker rockets**
  (RMB). Weapon selector: **1** guns / **2** mining laser. LMB fires the selected system.
* **AI pilots** fly the same flight model as you. **Rust Jackal raiders** patrol the deep side
  of the Cinder Drift and intercept, attack, evade and retreat. **Ossa Freight Co-op haulers**
  run a route past Kestrel and flee when shot.
* Explosions with bloom flash, point light and spark debris. Wrecks drop salvage canisters
  the scoop collects. Bounties pay on the kill.
* **Defeat:** the insurer tows you to Kestrel. Cargo and courier jobs are lost, and a 250 cr
  deductible is charged.
* **Contracts:** courier (sealed crates), procurement (ore for the Kestrel refinery, spares for
  Gale Ridge), bounty (Rust Jackals). Boards refresh each world day, you can hold 3 jobs, and
  jobs are paid when you arrive at the right place with the goods.
* **Shipyard:** 6 upgrades (cargo rack, hull plating, shield booster, thruster re-jetting,
  mining laser Mk II, weapon capacitors) and hull repair.
* The terminal now has tabs: **MARKET / SERVICES / CONTRACTS**.
* Combat HUD: shield, hull and heat meters, ship markers (triangle hostile, square civilian),
  target health, lead pip, cyan and red edge flashes, MISSILE INBOUND / HULL CRITICAL /
  OVERHEATED warnings, contract tracker.
* New synthesised sounds: laser shot, slug cannon, rocket launch, shield hit, hull clang,
  explosion, klaxon. Combat camera shake.
* Saves **v3** with **v2 → v3 migration**.
* **Stage 6 exit test in CTest:** take a bounty and a procurement job, fight raiders until the
  bounty is met, dock and get paid, save/reload, mine and deliver the ore, buy an upgrade.
  Tests: 70.
* Benchmark `bench.combat_skirmish`. The validator checks weapons, upgrades, contracts and
  weapon mounts.

## 0.5.0 — Stage 5: travel drive, planet and landing site (2026-11-04)
* In-system **travel drive** (N select, J engage): align, spool, transit, decelerate; exact
  double-precision arrival; mass lock; FOV stretch and engine howl in transit.
* **Varga** (planet) and **Ossa** (moon) rendered as ray-traced impostors: procedural surface,
  craters, terminator, atmospheric limb and halo, scissored to their screen bounds.
* **Landing:** descent corridor with re-entry heat and shake, a cloud-deck fade hiding the scene
  swap, a scripted final approach onto the pad in the surface scene, then Landed. Launch reverses
  it and returns to the site's orbit marker.
* **Gale Ridge Outpost:** procedural terrain with cliff boundaries, dusk atmospheric sky, dust
  fog, low gravity, sodium masts, an outpost corridor with the Supply Shed market, and wind audio.
* **Trade route:** the outpost pays well for supplies and sells ore cheaply; Kestrel the reverse.
* "Parked" generalisation: the seat, airlock and market work at stations and landing sites alike.
* Saves **v2** (landing state) with **v1 → v2 migration**.
* **Stage 5 exit test in CTest:** 3 landing/launch trade cycles, including continuing after a
  reload while landed; return destinations and precision checked. Tests: 59.
* Debug: `--start-landed`, `--start-in surface`; benchmarks for orbit, landing and surface.

## 0.4.0 — Stage 4: mining, cargo, market, save/load + atmosphere pass (2026-11-04)
* Economy: data-driven commodities (3 ores, 4 goods) and the Kestrel Station Exchange with
  bounded scarcity pricing, a spread floor, per-unit slippage and deterministic tick restocking.
  The cargo hold is measured in mass tons.
* Mining: asteroid composition, yield and hardness per field; scan (R); mining laser (LMB/Alt)
  with aim cone and range; ore chunks with scoop field and hard caps; depletion persists.
* Market terminal UI (M when docked, or F at the hangar terminal): CRT-style panel, keyboard and
  mouse, buy/sell/sell-all, ×10.
* Save/load: versioned JSON snapshots, atomic write plus `.bak`, F7/F8, autosave on docking,
  `--load`/`--continue`, saving on foot. Byte-identical save→load→save. Hot reload now uses the
  same snapshot path.
* **Stage 4 exit test in CTest:** profitable mining trip, save/reload, no duplication.
* Atmosphere: 8 point lights per frame, distance fog, flickering failing lamps, the new
  **Used Future** look (default) with an amber/phosphor CRT HUD, darker and colder art direction,
  station work floods, and a mining impact light.
* Audio: software mixer and SDL3 device, a data-driven AudioDirector (ambience beds per
  location, random hull groans, engine rumble following thrust, laser, clamps, airlock, radio,
  UI, scoop). All sounds are original, synthesised placeholders.
* Fixed: a crash on exit (audio stream destroyed after `SDL_Quit`).
* New benchmark: `bench.mining`. Tests: 56.

## 0.3.0 — Stage 3: docking and walkable interiors (2026-11-04)
* Docking state machine: clearance request, approach authorisation, deterministic tolerance-based
  capture with scripted settle, clamps, and undocking. Optional approach autopilot (K).
  Radio-chatter subtitles and approach HUD (pad marker, approach column, distance/speed/alignment
  readout, context hints).
* Station proxy: oriented-box colliders, a roof landing pad with blinking guide lights, and a dish
  collider.
* Walkable interiors from data (piece templates with colliders, spawns, interactables, lights):
  the **Sparrow cabin** (cockpit seat, windows onto space, cargo, airlock) and **Kestrel Hangar 1**
  (open bay to space, your parked ship, services terminal, pillars and props), built from CC0
  Kenney pieces.
* First-person capsule controller (collide-and-slide, gravity, jump, no tunnelling).
* `GameSession` (location state, interaction rules, active interior) and `world_builder`, shared by
  the app and the headless tests.
* **Stage 3 exit test in CTest:** 20 full fly → dock → walk → exit → walk → board → sit → undock
  cycles on real content.
* Debug tools: F6 / `--start-docked` force-dock, `--start-in ship|station`.
* New benchmarks: docking autopilot, ship interior, station hangar.
* Renderer: optional sky and stars, star-glare control, clear colour for enclosed interiors.

## 0.2.0 — Stage 2: ship flight, plus a retro 80s look (2026-11-04)
* 6DOF flight model (assist on/off, precision/cruise/boost, energy-limited boost), driven entirely
  by ship JSON. Deterministic and unit-tested.
* Keyboard and mouse virtual-stick controls. Mouse capture toggles on Esc; quit moved to F10.
* Chase and cockpit cameras; developer orbit/free cameras moved to F2.
* HUD: reticle, prograde/retrograde, speed/set-speed, throttle and boost bars, navigation markers,
  target bracket with range and range rate, off-screen arrows, impact/proximity warnings.
* Targeting (cycle, nearest asteroid, clear).
* Procedural asteroid meshes and deterministic clustered fields (instanced, frustum culled);
  station proxy from CC0 Kenney pieces; sphere collisions.
* The floating origin follows the player ship; static bodies are recomputed from double positions.
* glTF socket import (`socket_*`, `hardpoint_*`) with ship-JSON fallbacks.
* Renderer: HDR target, bloom, final composite with colour grading and lens/film effects;
  instanced mesh pipeline; additive glow sprites (engine exhaust, running lights); data-driven
  sky, fill and rim lighting.
* Data-driven looks (`content/visuals/post_profiles.json`): **Retro 80s** (default) and **Clean**,
  each with its own HUD palette. F4 cycles, `--look` selects.
* Data-driven benchmark paths (`content/benchmarks/`): empty-space cockpit, asteroid field chase,
  station approach.
* New schemas: ship (extended), scene (player/props/asteroidFields/sky), visuals, benchmark, input
  (mouse). `tools/asset_tools/repair_kenney_glb.py` fixes the kit's invalid scene roots.
* CI: the Windows job installs the Vulkan SDK with the official LunarG installer.

## 0.1.0 — Stage 0 + Stage 1 engineering spike (2026-11-04)
* CMake/C++20 project skeleton with the `fh_core` (GPU-free) / `fh_engine` / `farhorizon` split.
  Warnings on, optional sanitizers.
* SDL3 window and action-based input loaded from `content/config/input.json`.
* Vulkan 1.3 renderer (dynamic rendering, sync2): swapchain with resize/out-of-date handling, two
  frames in flight, validation layer + debug messenger, GPU timestamp queries, PNG screenshot
  readback.
* Passes: procedural sky (galactic band, dust, star disc and glare), 9,000-star instanced
  starfield (deterministic seed), PBR (GGX) mesh pass with reversed-Z infinite depth, and a
  developer overlay.
* glTF/GLB importer (cgltf) and data-driven ship and scene definitions with material overrides.
  The asset manifest uses stable AssetIDs.
* Double-precision system coordinates, floating local origin with rebasing, camera-relative
  rendering.
* Fixed-timestep simulation clock with capped catch-up.
* Orbit and free-fly developer camera, F5 hot reload with rollback on invalid content, F11
  fullscreen, F12 screenshots.
* Frame statistics overlay and benchmark mode with CSV/JSON export.
* `content_validator` tool, 21 unit tests, and a CI workflow (Windows MSVC build plus Linux
  sanitizer build with a headless smoke render).
