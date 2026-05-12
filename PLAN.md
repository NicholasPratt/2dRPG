# Rich Per-Tile Map System — Implementation Plan

## Goal

Replace the binary wall/no-wall tile system with a full per-tile ID system backed by
tileset assets, enabling richly detailed maps where every cell carries a specific visual
tile and gameplay properties (solid, water, etc.).

---

## Architecture Overview

```
assets/raw/tilesets/<id>.png          ← source PNG (imported via stb_image)
assets/game/tilesets/<id>.tileset.json ← tileset metadata (tile defs, solid flags)
assets/game/maps/<id>.admap           ← ADMAP 2 format (uint16 tile IDs per cell)
```

Runtime:
- Load `TilesetDef` → look up tile properties by ID
- Load `TileMap` → each cell holds a `uint16_t` tile ID referencing the tileset
- `solid` flag on each tile replaces the old `walls` vector

---

## Phases

### Phase 1 — TilesetDef data model and I/O  ✅ complete
**Files:** `src/game/tileset.hpp`, `src/game/tileset.cpp`

- `TileDef`: per-tile metadata (id, name, solid)
- `TilesetDef`: id, source PNG path, tile dimensions, list of `TileDef`
- `saveTileset` / `loadTileset` — hand-written JSON, matching existing style
- Unit-testable with no ImGui or window dependency

### Phase 2 — ADMAP 2 map format  ✅ complete
**Files:** `src/game/map.hpp`, `src/game/map.cpp`

- Add `tilesetId: string` and `tiles: vector<uint16_t>` to `TileMap`
- Keep `walls: vector<unsigned char>` as a derived/compat view
- `saveTileMap` writes `ADMAP 2` with space-separated uint16 tile ID rows
- `loadTileMap` detects version:
  - v1 → maps `'0'` → tile ID 0, `'1'` → tile ID 1, sets `tilesetId = ""`
  - v2 → reads `tileset` header and uint16 tile rows
- `validMapShape` checks `tiles.size() == width * height`

### Phase 3 — TilesetEditorPanel  ✅ complete
**Files:** `src/editor/panels/tileset_editor_panel.hpp/.cpp`

- Import a PNG from `assets/raw/tilesets/` (via stb_image)
- Display the PNG sliced into a clickable grid at the configured tile dimensions
- Click a tile to edit: set name, solid flag
- Save to `assets/game/tilesets/<id>.tileset.json`
- Wire into `EditorApp` as a new "Tilesets" tab

### Phase 4 — MapEditorPanel tileset picker + per-tile painting  ✅ complete
**Files:** `src/editor/panels/map_editor_panel.hpp/.cpp`

- Add `tilesetId_` and `loadedTileset_` fields to `MapEditorPanel`
- Sidebar: tileset picker — click to load a tileset, then click a tile cell to select it
- Painting: left-click sets `tiles[y * width + x]` to selected tile ID
- Right-click sets tile to 0 (erase)
- Visual: render each cell as a colored rectangle keyed by tile ID (placeholder until
  GL texture rendering lands)
- Spawn, resize, fill operations updated to work with `tiles` instead of `walls`

### Phase 5 — stb_image integration  ✅ complete
**Files:** `external/stb/stb_image.h`, `CMakeLists.txt`

- Add stb_image.h to `external/stb/`
- Create a single `src/editor/stb_image_impl.cpp` with `STB_IMAGE_IMPLEMENTATION`
- Link into `adventure_editor` static lib
- Replace `readEditorPngRgba()` calls with `stbi_load()` in tileset and sprite panels

---

## File Format Reference

### `.tileset.json` (new)

```json
{
  "id": "overworld",
  "source": "assets/raw/tilesets/overworld.png",
  "tileWidth": 16,
  "tileHeight": 16,
  "tiles": [
    { "id": 0, "name": "grass",      "solid": false },
    { "id": 1, "name": "stone_wall", "solid": true  },
    { "id": 2, "name": "water",      "solid": true  }
  ]
}
```

### `.admap` version 2 (new)

```
ADMAP 2
id overworld_cave
tileset overworld
size 24 16
spawn 1 1
tiles
0 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1
0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
...
end
```

Each row is `width` space-separated decimal uint16 values (0–65535).

### `.admap` version 1 (backward compat, read-only)

Existing files continue to load. Tile `'0'` maps to tile ID 0, `'1'` maps to tile ID 1.
`tilesetId` is set to `""` on load.

---

## Build Order

1. `src/game/tileset.hpp` + `src/game/tileset.cpp` (no deps beyond STL)
2. `src/game/map.hpp` + `src/game/map.cpp` (ADMAP 2, keep v1 compat)
3. `external/stb/stb_image.h` + `src/editor/stb_image_impl.cpp` + CMakeLists.txt
4. `src/editor/panels/tileset_editor_panel.hpp/.cpp` + wire into EditorApp
5. `src/editor/panels/map_editor_panel.hpp/.cpp` (tileset picker + tile painting)

---

## Open Questions

- **Tile ID range**: `uint16_t` gives 65536 IDs. Sufficient for foreseeable tilesets.
- **Multiple layers**: out of scope for now; add a `layer` field to ADMAP 3 later.
- **Named regions / trigger zones**: out of scope; planned for a future entity system.
- **Binary map format**: text at 512×512 is ~1.3 MB (acceptable). Revisit if load times matter.
