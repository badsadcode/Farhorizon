# UI theme

`content/ui/theme.json` (asset `ui.theme`, type `ui_theme`) sets the fonts, colours, sizes and panel
style of the menus and terminal, and the fonts, sizes and panel opacity of the HUD. HUD *colours*
come from the active look instead (`hud` in [visuals.md](visuals.md)), so they follow F4.

F5 reloads the theme, except fonts, which are baked once at startup. A theme that fails validation
keeps the previous one.

```json
{
  "schemaVersion": 1,
  "fonts": { "mono": "fonts/ShareTechMono-Regular.ttf", "sans": "fonts/ShareTech-Regular.ttf" },
  "colors": { "text": [226, 172, 98, 255] },
  "sizes": { "small": 16, "body": 20, "heading": 26, "title": 88, "subtitle": 18 },
  "panel": { "chamfer": 14, "border": 1.5, "padding": 22 },
  "hud": { "panelAlpha": 0.8, "speed": 46, "warning": 30 },
  "rowHeight": 40,
  "letterSpacing": 0.32
}
```

## Fields

| Field | Required | Meaning |
|---|---|---|
| `fonts.mono`, `fonts.sans` | yes | TTF paths relative to `content/`; must exist and stay inside the content root |
| `colors.*` | no | sRGB `[r, g, b, a]`, 0-255 |
| `sizes.*` | no | Text sizes in pixels at 1080p, within 6..200 |
| `panel.chamfer` | no | Cut corner size, pixels at 1080p |
| `panel.border` | no | Border width, pixels at 1080p |
| `panel.padding` | no | Inner padding, pixels at 1080p |
| `hud.panelAlpha` | no | Multiplier on the `panel` colour's alpha for HUD panels, 0..1 |
| `hud.speed` | no | Speed readout size, pixels at 1080p, within 6..200 |
| `hud.warning` | no | Warning text size, pixels at 1080p, within 6..200 |
| `rowHeight` | no | Height of a menu row, pixels at 1080p, within 6..200 |
| `letterSpacing` | no | Extra space between title letters, in em |

Omitted optional fields use the defaults in `game/ui/ui_theme.h`.

## Colours

| Key | Used for |
|---|---|
| `text`, `textBright`, `textDim` | Normal text, focused text and titles, secondary text and hints |
| `accent` | Secondary information (held cargo, contract progress) |
| `danger`, `ok` | Errors and binding conflicts, successful results |
| `panel`, `panelBorder` | Panel fill and outline |
| `highlight` | Focused row and selected tab fill |
| `backdrop` | Screen dim behind menus |

Alpha is blended in linear light and looks more transparent than its value suggests: a `panel` alpha
of 246 still shows bright scenery clearly. See [ADR 0010](../decisions/0010-ui-kit-hud-and-testing.md).

## Scaling

Sizes are authored at 1080p and multiplied by window height ÷ 1080 (clamped to 0.6..2.5) and by the
player's Interface scale (`settings.json`, 0.75..1.5).
