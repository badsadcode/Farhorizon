# ADR 0010 — UI kit, theme, HUD and scripted UI tests (Stage 7)

**Status:** accepted · **Date:** 2026-11-04

## One overlay, one font atlas
All 2D (HUD, terminal, menus, developer text) goes through `OverlayBuilder`: one vertex buffer and
one draw call sampling one RGBA atlas. `stb_truetype` bakes Share Tech Mono and Share Tech (SIL OFL,
`content/fonts/`) at a set of pixel sizes; solid shapes use the atlas's white texel. Text picks the
closest baked size and scales the quads, so any size works and common sizes stay pixel-exact.
Without a font (headless tests, tools) text falls back to `stb_easy_font`, which lets the Ui logic
be unit-tested without a GPU.

## Immediate-mode kit instead of a retained widget tree
`game/ui/ui.*` is a small immediate-mode kit. Screens call widgets every frame in a fixed order and
each focusable gets the next focus index. It is roughly 400 lines, needs no widget state to keep in
sync with game state, and fits the single overlay buffer. Rejected: Dear ImGui (hard to give it the
game's look, and it would be a second text and input path).

Rules the kit enforces, each from a bug found while verifying it:
* **The mouse takes focus only when it moves or clicks.** A resting cursor must not fight the
  keyboard. Window resizes move the cursor without the player touching it, so the app ignores mouse
  motion for a few frames after one.
* **Keyboard focus passes over disabled widgets**, in the direction of travel, so a screen never
  opens with nothing selected.
* **Scrolled-out rows keep their focus index** (`skipFocusable`), and the wheel pulls focus along
  with the list so Enter always acts on a visible row.

## Input ownership on the frame a screen opens
`handleAppInput` runs before the overlay is built, in the same frame, with the same key state. So
the Esc that opens the pause menu, or the M that opens the terminal, is still "pressed" when that
screen first runs and would close it again. The app gives a just-opened menu a neutral `UiInput`,
and the terminal ignores its close keys on its first frame.

While a key is being captured for rebinding, the Ui gets a neutral input (keys and clicks belong to
the capture, not to navigation) and the app skips its own hotkeys, so F10 at the prompt does not quit.

## Rebinding rules (`game/ui/bindings.*`, SDL-free and unit-tested)
Every bindable action has a context: Flight, Foot or Always.
* Flight and Foot may share a key, since only one is live at a time (W flies and walks).
* Same-context, or anything against an Always action, is a **conflict**. It is **allowed but
  flagged** in red on both rows with a notice naming the other action. Refusing would make swapping
  two keys a three-step puzzle.
* Keys the app itself handles in every mode (menu, quit, fullscreen, screenshot, developer tools)
  are **reserved and refused**.

Hints name the player's actual keys: the HUD and terminal read them from `InputState`. Core gameplay
messages (mining, landing, session) no longer contain key letters, since core code cannot know them.

## HUD layout
Fixed anchors: systems bottom left, flight cluster bottom centre, target bottom right, economy top
right, contracts top left, status and hint top centre, approach readout right of centre. Transient
**messages stack upward from the flight cluster and warnings stack downward from the upper third**,
instead of sitting at fixed heights, so they can no longer overlap each other.

The HUD uses the same scale as the menus (`uiLayoutScale`: window height × Interface scale). Colours
stay in `HudPalette` because they belong to the visual look (F4); fonts, sizes and panel opacity
come from the theme. The HUD is hidden while a menu is open: the world is paused, and the HUD
collided with the hint bar.

Interface scale applies when the slider is released. The slider sits in the layout it scales, so
applying it live moves the bar under the mouse and feeds back on itself.

## Scripted UI tests (`--ui-script`, `game/ui/ui_script.*`, `tests/ui_scripts/`)
A small script pushes real SDL events into the normal event queue, so menus, rebinding and the
terminal run exactly as they do for a player, then asserts on bindings and screens and takes
screenshots. A failed expectation exits with code 3. An event filter drops real devices' events
during a run, so touching the mouse cannot change the result.

**Why:** the unit tests cover the Ui logic but not the app wiring. The same-frame close of the pause
menu, the focus jump after a fullscreen toggle and the capture bugs all lived in that wiring, and
all of them were found by these scripts rather than by unit tests. They also run in CI (Linux,
software Vulkan). Scripted runs do not open the audio device. Scripts use window coordinates of the
default 1600×900 window for mouse actions.

## Colours and blending
The overlay blends in linear light, so alpha looks far more transparent than its value suggests:
4% of a bright background reads as about 20%. Theme panels are therefore nearly opaque (alpha 252 of
255). Treat alpha values in `theme.json` accordingly.
