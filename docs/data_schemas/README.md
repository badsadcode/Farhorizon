# Data schemas

All content is JSON (plus GLB/PNG assets) under `content/`. Content refers to other content by
**AssetID** strings resolved through `content/asset_manifest.json`, never by file path.
Validate everything with:

```bash
build/content_validator content
```

| Schema | File(s) | Doc |
|---|---|---|
| Asset manifest | `content/asset_manifest.json` | [asset_manifest.md](asset_manifest.md) |
| Ship | `content/ships/*.json` | [ship.md](ship.md) |
| Scene | `content/scenes/*.scene.json` | [scene.md](scene.md) |
| Input bindings | `content/config/input.json` | [input.md](input.md) |
| Visual profiles (looks) | `content/visuals/post_profiles.json` | [visuals.md](visuals.md) |
| Benchmarks | `content/benchmarks/*.json` | [benchmark.md](benchmark.md) |

Conventions: metres, seconds, tons, degrees in data (radians only in code), linear RGB for
colours unless stated, right-handed +Y up with −Z forward.
