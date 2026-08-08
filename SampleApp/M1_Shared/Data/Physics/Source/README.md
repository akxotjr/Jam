# M1 Physics composition

`Common.physics_asset.json` owns definitions shared by every M1 world. `Field.physics_asset.json`
owns only Field-local definitions and includes `Common`. Duplicate definition names across included
sources are errors; world-local overrides are intentionally unsupported.

JamPx reads only the flattened files under `SharedData/Generated/M1/Physics`.

```powershell
bin\x64\Debug\JamTools\JamTools_d.exe flatten-physics `
  --common SharedData\M1\Physics\Common.physics_asset.json `
  --out SharedData\Generated\M1\Physics\Common.physics_asset.json

bin\x64\Debug\JamTools\JamTools_d.exe flatten-physics `
  --common SharedData\M1\Physics\Common.physics_asset.json `
  --world SharedData\M1\Physics\Field.physics_asset.json `
  --out SharedData\Generated\M1\Physics\Field.physics_asset.json
```
