# ADR 0016 — Networking: the host decides

_Status: accepted (0.13.0, roadmap step 4.3)._

## Decision
* **ENet over UDP**, hidden in `game/net/net_transport`. Join, leave and commands are reliable; controls and world
  reports go on a channel where only the latest matters.
* **A hand-written binary format** (`net_protocol`) with bounds checks and a refusal of NaN and infinity, because
  anything can arrive from a network. Tests feed it every truncation of a real packet and random bytes.
* **Server-authoritative.** The host runs the one real `GameSession`. A client sends its controls, an absolute look
  direction and named commands, and the host decides. `interact` is checked against where the host says that
  player stands, and helm commands are refused from anyone but the pilot.
* **A client keeps a local `GameSession` in `netClient` mode:** it steps only walking, and everything else is overwritten
  by reports, 20 a second. Its own body is predicted and corrected only beyond 0.5 m. Others and the ship glide
  between reports. The ship's position is sent in the system frame, because each machine has its own floating origin.
* **Listen server first.** The host plays too. Step 4.4 moves this to `farhorizon_server.exe`; `HostLink` needs a
  session, not a window, so that should carry over.
* **What isn't shared is switched off** by `setNetworked`, rather than left to drift apart silently: space combat,
  mining, landing, and entry to places with enemies. Terminals work only for the host.

## Added in 0.14.0 (step 4.4)
* **`DedicatedServer` runs a session with no window** (`game/net/dedicated_server`), and `farhorizon_server.exe` is a thin
  window over it, drawn by the same overlay geometry through SDL_Renderer, so no Vulkan is imported. Tests drive
  `DedicatedServer` directly.
* **The session's first crew slot stays, as nobody** (`setDedicated`). Everything that means "this machine's player"
  kept working, at the cost of shared ship expenses still being charged to that slot.
* **Passwords use a challenge:** the server sends a one-time number, and the client sends SHA-256 of it mixed with the
  password. This stops reading the password from the traffic or replaying it. It doesn't stop someone guessing
  offline from captured traffic, and the game traffic itself isn't encrypted, so a password should be unique.
* **Helm commands moved into `GameSession`,** since a server has no app to run them.

## Added in 0.15.0, after the first game across the Atlantic
* **Reconciling our own body.** Each input carries a number, and each report says which number it includes. The client
  compares that report with where it was at that input, keeps the difference, and eases it away. The old check compared
  against the present, so any walking at more than about 90 ms of round trip counted as an error.
* **The world is shown 7 steps behind** (about 120 ms), between reports, with the clock steered gently. Past the last report
  it carries on by velocity for a third of a second.
* **Undocking waits only for people outside the ship.** ADR 0014's "everyone seated" predates walking in flight.
* **An empty helm belongs to whoever is seated and reaches for it.**
* `ClientLink::setSimulatedLag` makes the tests and `--net-lag` see a slow network.

## Consequences
* Solo is unaffected: none of this runs without `--host` or `--join`.
* Client's copy of NPCs and world details is not authoritative. Sharing combat comes with crew stations.
* No password or encryption yet, so this isn't for the open internet.
