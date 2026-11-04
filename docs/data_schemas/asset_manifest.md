# Asset manifest (`schemaVersion` 1)

```json
{
  "schemaVersion": 1,
  "assets": {
    "<assetId>": { "type": "mesh|ship|scene|config", "source": "relative/path", "license": "optional provenance" }
  }
}
```
* `assetId`: stable dotted name such as `mesh.ship.sparrow`. Renaming a file only changes `source`.
* `source`: relative to `content/`. It must not be absolute or contain `..` (rejected at load).
* `type`: is checked when an ID is resolved. A ship that references a scene ID is an error.
* `license`: required in practice for third-party or placeholder art.
