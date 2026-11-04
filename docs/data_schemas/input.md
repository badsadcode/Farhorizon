# Input bindings (`content/config/input.json`)

```json
{ "schemaVersion": 1, "bindings": { "<action>": ["<SDL scancode name>", "..."] } }
```
Key names are SDL scancode names (`SDL_GetScancodeFromName`), for example `W`, `Left Shift`,
`Space`, `F5`. Unknown names are reported and skipped. Several keys may trigger one action.
Gameplay code only queries action names. In-game rebinding UI is planned for Stage 2.
