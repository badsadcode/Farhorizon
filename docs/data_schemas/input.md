# Input bindings (`content/config/input.json`)

```json
{
  "schemaVersion": 1,
  "mouse": { "sensitivity": 0.0022, "invertY": false, "deadzone": 0.04, "recenterPerSecond": 0.0 },
  "bindings": { "<action>": ["<SDL scancode name>", "..."] }
}
```
* Key names are SDL scancode names (`W`, `Left Shift`, `Space`, `F5`...). Unknown names are reported.
* `mouse.sensitivity` is virtual-stick deflection per pixel. `recenterPerSecond > 0` makes the
  stick drift back to centre.
* The file is re-read on F5, so rebinding takes effect without restarting.

Actions: `dock.request`, `dock.autopilot`, `interact`, `foot.forward/back/left/right/run/jump`, `debug.forceDock`, `ship.throttleUp/Down/Zero`, `ship.strafeLeft/Right/Up/Down`, `ship.rollLeft/Right`,
`ship.pitchUp/Down`, `ship.yawLeft/Right`, `ship.boost`, `ship.toggleAssist`,
`ship.togglePrecision`, `ship.recenterStick`, `target.next`, `target.nearestAsteroid`,
`target.clear`, `camera.cycle`, `camera.dev`, `camera.forward/back/left/right/up/down/boost`,
`camera.autoRotate`, `app.toggleMouse`, `app.quit`, `debug.overlay`, `debug.autofly`,
`visual.cycleLook`, `debug.reloadAssets`, `debug.screenshot`, `window.fullscreen`.
