# Code Base Structure

This project is a C++ 2D RPG engine and integrated editor scaffold targeting a SNES/Zelda pixel-art style (see `RPG_Engine_Specification.md`). The implemented app is an editor built with Dear ImGui, GLFW, and OpenGL. Runtime game code is authored in `src/game` and must remain ImGui-free.

The main architectural rule is that the editor creates data the game can load. Runtime code lives outside `src/editor` and must not depend on ImGui.

## Current Layout

```text
2drpg/
  CMakeLists.txt
  code_base.md
  RPG_Engine_Specification.md       # game design / feature spec
  assets/
    raw/
      sprites/                      # PNG source sprite sheets
      character_sprites/            # raw character sprite PNGs
      tilesets/                     # raw map tileset images
    game/
      project.json                  # asset root manifest
      chapters/                     # .adchapter chapter files
      maps/                         # .admap tile maps
      sprites/                      # .sprite.json metadata
      character_sprites/            # game-ready character sprite assets
      characters/                   # planned character data
      animations/                   # planned animation data
      palettes/                     # planned palette data
      paths/                        # .adpath enemy waypoint paths
      tilesets/                     # .tileset.json tileset definitions
  external/
    imgui/                          # Dear ImGui source and backends
    stb/                            # stb_image for PNG loading
  src/
    app/
      main_editor.cpp               # windowed editor executable
      main_editor_smoke.cpp         # headless ImGui editor smoke test
      main_game_smoke.cpp           # runtime map + chapter loader smoke test
    editor/
      asset_directories.hpp/.cpp    # central asset root paths
      editor_app.hpp/.cpp           # top-level ImGui tabs
      editor_context.hpp            # shared editor context
      stb_image_impl.cpp            # stb_image implementation unit
      panels/
        character_editor_panel.hpp/.cpp
        enemy_path_editor_panel.hpp/.cpp
        layout_editor_panel.hpp/.cpp
        map_editor_panel.hpp/.cpp        # legacy/detail map panel; not a top-level tab
        sprite_editor_panel.hpp/.cpp
        tileset_editor_panel.hpp/.cpp
        wall_floor_paint_panel.hpp/.cpp
    game/
      chapter.hpp/.cpp              # Chapter / ChapterScreen / ScreenLink types and .adchapter load/save
      map.hpp/.cpp                  # TileMap type and .admap load/save
      tileset.hpp/.cpp              # TilesetDef / TileDef types and .tileset.json load/save
```

## Build Targets

```text
imgui                    Static Dear ImGui library.
adventure_game           Runtime-facing game/data code (chapter, map, tileset).
adventure_editor         Editor library. Depends on imgui and adventure_game.
adventure_editor_smoke   Headless editor smoke executable.
adventure_game_smoke     Loads an .admap and .adchapter through runtime code.
adventure_editor_window  GLFW/OpenGL editor window (built when OpenGL + GLFW found).
```

Useful commands:

```sh
cmake -B build
cmake --build build --parallel

./build/adventure_game_smoke
./build/adventure_game_smoke assets/game/maps/new_map.admap assets/game/chapters/chapter_1.adchapter
./build/adventure_editor_smoke
./build/adventure_editor_window   # macOS: emits OpenGL deprecation warnings, harmless
```

## Dependency Direction

```text
window executable  →  adventure_editor  →  adventure_game
editor_smoke       →  adventure_editor  →  adventure_game
game_smoke                             →  adventure_game
```

Runtime modules in `src/game` must not include editor headers or ImGui headers.

## Editor App Tabs

On startup, `EditorApp` opens a chapter selector modal. The user must load an existing chapter or create a new one before the main editor workflow is shown. Closing the editor or switching chapters prompts to save or discard unsaved work.

`EditorApp` owns these top-level ImGui tabs (in order):

| Tab | Panel | Purpose |
|-----|-------|---------|
| Characters | `CharacterEditorPanel` | Character sheets with sprite references |
| Sprites | `SpriteEditorPanel` | Full pixel-art sprite / animation editor |
| Screens | `LayoutEditorPanel` | Continuous chapter screen grid, selected-screen tile editing, add/link/delete screens |
| Tilesets | `TilesetEditorPanel` | Generate tileset definitions from source PNG |
| Wall/Floor Paint | `WallFloorPaintPanel` | Pixel paint tool for room art with selected-screen wall guide and parallax preview |
| Enemy Paths | `EnemyPathEditorPanel` | Waypoint/spline editor for enemy patrol paths |
| Assets | *(inline)* | Asset directory listing |

---

## Chapter System

Implemented in `src/game/chapter.hpp/.cpp` and `src/editor/panels/layout_editor_panel.*`.

### Data model

```cpp
struct ScreenLink { std::string north, south, east, west; };

struct ChapterScreen {
    std::string id;
    std::string mapId;        // .admap to load for this screen
    int gridX, gridY;         // position in macro layout
    ScreenLink links;         // screen-flip neighbours
    bool respawnEnemies;      // false = defeated enemies stay gone (spec default)
};

struct Chapter {
    std::string id;
    std::string startScreenId;
    std::vector<ChapterScreen> screens;
};
```

### `.adchapter` format (v2)

```text
ADCHAPTER 2
id chapter_1
start screen_1
screens 2
screen screen_1 new_map 0 0
links - screen_2 - -
respawn 0
screen screen_2 dungeon_1 0 1
links screen_1 - - -
respawn 1
end
```

v1 files (no `respawn` per screen) load with `respawnEnemies = false`.

### Layout Editor

- Continuous canvas showing screens as edge-to-edge tile grids based on `gridX/gridY`; the selected screen is centered and highlighted.
- Adjacent/nearby screens render their mid-layer wall layout as context.
- The selected screen's `.admap` can be edited directly in the Screens tab. Left-click paints the active tile ID; right-click erases. Layer selector controls Floor / Walls(mid) / Ceiling.
- Directional buttons create or select connected screens north/south/east/west and write reciprocal screen links.
- Screen list sidebar and inspector edit id, mapId, grid position, links, respawn flag, and deletion.
- Save/load from `assets/game/chapters/<id>.adchapter`. Chapter save also saves dirty `.admap` files edited in the Screens tab.
- `Edit Screen Graphics` opens `Wall/Floor Paint` for the selected screen's map id.

---

## Tile Map System

Implemented in `src/game/map.hpp/.cpp` and `src/editor/panels/map_editor_panel.*`.

### Data model

```cpp
struct TileMap {
    std::string id;
    std::string tilesetId;
    int width, height;
    int spawnX, spawnY;
    // Layer 0: floor   Layer 1: mid (player-level, collision)   Layer 2: ceiling
    std::array<std::vector<uint16_t>, 3> layers;
};
```

### `.admap` format (v3)

```text
ADMAP 3
id new_map
tileset overworld
size 24 16
spawn 1 1
layer 0
0 0 0 ... (24 values) ...
layer 1
1 1 1 1 1 ... 1
1 0 0 ... 1
...
layer 2
0 0 0 ...
end
```

Backward compat: v1 (char `0`/`1` rows) and v2 (space-separated integer rows) both load into `layers[1]` (mid layer); floor and ceiling default to zero.

### Map Editor

- Layer selector: Floor / Mid / Ceiling radio buttons.
- Each layer rendered with a distinct tint: floor dim, mid normal, ceiling blue.
- Inactive layers rendered at 75% brightness.
- **Copy/paste**: "Select region" enters rubber-band mode → "Copy" copies selected tiles from active layer → "Paste" enters ghost-preview mode; click to stamp.
- Tileset palette for selecting tile IDs; solid indicator per tile.
- Test-game mode: arrow-key player movement with tile-based collision on the mid layer.
- Save/load: `assets/game/maps/<id>.admap`.

---

## Tileset System

Implemented in `src/game/tileset.hpp/.cpp` and `src/editor/panels/tileset_editor_panel.*`.

### Data model

```cpp
struct TileDef {
    int id;
    std::string name;
    bool solid;
};

struct TilesetDef {
    std::string id;
    std::string sourcePath;
    int tileWidth, tileHeight;
    std::vector<TileDef> tiles;
};
```

Format: `assets/game/tilesets/<id>.tileset.json`.  
Editor generates tile definitions from a source PNG grid.

---

## Sprite Editor

Implemented in `src/editor/panels/sprite_editor_panel.*`.

- Pixel editing with frames, layers, palette, preview, and animation playback.
- Tools: pen, mirror, bucket, eraser, stroke, line, rect, circle, move, select, picker, shade.
- Brush sizes 1×1, 2×2, 4×4.
- Frame actions: add, copy, delete, clear.
- Transform actions: flip H/V, rotate CW — applied to selection or whole frame.
- Clipboard: copy/paste selections.
- OS-aware shortcuts: `Cmd+Z/C/V` (macOS), `Ctrl+Z/C/V` (other).
- Snapshot-based undo for drawing, transforms, paste, resize, frame/layer changes, import.

Export/import paths:

| Operation | Path |
|-----------|------|
| Save metadata | `assets/game/sprites/<id>.sprite.json` |
| Export single frame | `assets/raw/sprites/<id>_frame_<n>.png` |
| Export sprite sheet | `assets/raw/sprites/<id>_sheet.png` |
| Import PNG | per Source PNG field |

---

## Wall / Floor Paint

Implemented in `src/editor/panels/wall_floor_paint_panel.*`.

- Two-layer pixel painter: Floor and Wall.
- Tools: pencil, eraser, fill, line, rect.
- Palette, brush size, layer visibility/opacity controls.
- **Pixel-scale canvas:** when opened from `Edit Screen Graphics`, the canvas is sized at `mapWidth × pixelsPerTile` by `mapHeight × pixelsPerTile` pixels so each canvas pixel is one actual output pixel. `pixelsPerTile` defaults to 16 and is adjustable in the toolbar (range 1–64).
- **Tile boundary grid:** a subtle white overlay marks tile boundaries on the canvas to aid alignment.
- **Scrollable canvas:** the canvas region is a fixed-height (500 px) scrollable child window with a horizontal scrollbar, supporting large canvases comfortably.
- Zoom range 1–16 (opens at zoom 2 when loaded from a screen).
- When opened from `Edit Screen Graphics`, loads the selected screen's `.admap` mid layer as a toggleable yellow `Wall guide` overlay; guide cells are scaled to `pixelsPerTile × pixelsPerTile` canvas pixels each.
- Parallax preview: animated floor scroll at a subtle ±4 px range behind the wall layer.
- Undo with one level of history.
- Export: writes floor, wall, and parallax-preview PNGs to both `assets/raw/tilesets/` and `assets/game/tilesets/`.
- Chapter save calls this export path so the current paint document has game-ready PNGs for runtime use.

Exported paint files:

| File | Purpose |
|------|---------|
| `assets/raw/tilesets/<id>_floor.png` | Editable/source floor art export |
| `assets/raw/tilesets/<id>_wall.png` | Editable/source wall art export |
| `assets/raw/tilesets/<id>_preview.png` | Editable/source composite preview |
| `assets/game/tilesets/<id>_floor.png` | Game-ready floor art |
| `assets/game/tilesets/<id>_wall.png` | Game-ready wall/overhead art |
| `assets/game/tilesets/<id>_preview.png` | Game-ready preview/debug composite |

---

## Enemy Path Editor

Implemented in `src/editor/panels/enemy_path_editor_panel.*`.

Addresses spec §4.4 (movement splines / state definition).

### Data model

```cpp
struct Waypoint { float x, y; };  // world pixels
enum class Behavior { Idle, Patrol, Aggro };
// Fields: id, mapId (ref), behavior, speed (px/s), loop, respawn, waypoints
```

### `.adpath` format (v1)

```text
ADPATH 1
id path_1
map new_map
behavior 1
speed 64.0
loop 1
respawn 0
waypoints 4
wp 48.0 32.0
wp 96.0 32.0
wp 96.0 80.0
wp 48.0 80.0
end
```

`behavior`: 0=Idle, 1=Patrol, 2=Aggro.  
Files saved to `assets/game/paths/<id>.adpath`.

### Editor features

- Canvas shows a 16px world-tile grid (zoom 0.5×–6×).
- Optional map background: load any `.admap` mid layer as a tile reference.
- Click empty area: add waypoint (snaps to grid if enabled).
- Click near existing waypoint: select it.
- Drag selected waypoint: move it.
- Right-click waypoint: delete.
- Delete key: delete selected waypoint.
- Loop line drawn from last to first waypoint when loop is enabled.
- Behavior (Idle/Patrol/Aggro), speed slider, loop checkbox, respawn checkbox.

---

## Asset Directories

`AssetDirectories` centralises editor asset paths. All paths are relative to `projectRoot` (default: current working directory).

| Field | Default |
|-------|---------|
| `rawSprites` | `assets/raw/sprites` |
| `rawCharacterSprites` | `assets/raw/character_sprites` |
| `rawTilesets` | `assets/raw/tilesets` |
| `gameSprites` | `assets/game/sprites` |
| `gameCharacterSprites` | `assets/game/character_sprites` |
| `gameCharacters` | `assets/game/characters` |
| `gameChapters` | `assets/game/chapters` |
| `gameMaps` | `assets/game/maps` |
| `gameTilesets` | `assets/game/tilesets` |
| `gameAnimations` | `assets/game/animations` |
| `gamePalettes` | `assets/game/palettes` |
| `gamePaths` | `assets/game/paths` |

The project manifest at `assets/game/project.json` mirrors these roots.

---

## Implemented vs. Spec

| Spec section | Status |
|---|---|
| §3.1 Chapters / Screens / Screen-Flip links | ✅ Data model + editor (no runtime transition yet) |
| §3.1 Parallax / pseudo-3D | ✅ Editor preview in Wall/Floor Paint (no runtime renderer yet) |
| §3.2 Real-time combat / timed mechanics | ❌ Not yet |
| §3.2 Enemy respawn flag | ✅ `respawnEnemies` on `ChapterScreen` |
| §4.1 Layout editor (macro view, screen management) | ✅ |
| §4.2 3-layer tile maps (floor / mid / ceiling) | ✅ |
| §4.2 Copy/paste tiles | ✅ |
| §4.2 Pixel painting | ✅ Wall/Floor Paint panel with selected-screen wall guide |
| §4.3 Sprite & animation editor | ✅ |
| §4.4 Enemy paths / splines | ✅ `.adpath` format + editor |
| §4.4 Enemy behavior states (idle/patrol/aggro) | ✅ In path data |
| §5 Save/load (JSON, text formats) | ✅ |
| §6 Runtime game engine (rendering, screen-flip) | ❌ Not yet |
| §6 Runtime collision | ✅ Test-game mode in map editor |

## Near-Term Priorities

1. Runtime game window: load a chapter, render the start screen's `.admap` mid layer, basic player movement.
2. Screen-flip transitions: when player reaches a screen edge with a link, load and render the linked screen.
3. Full `.sprite.json` round-trip (currently write-only from editor).
4. Character save/load format.
5. Enemy runtime: load `.adpath` and drive a simple entity along the waypoints.
6. Parallax rendering in the runtime using `assets/game/tilesets/<mapId>_floor.png` and `<mapId>_wall.png`.

## Engineering Notes

- Keep editor UI state out of `src/game`.
- File format parsing belongs in runtime-facing modules when the game needs to load that format.
- Prefer readable text formats while the project is small.
- Collision is currently tile-based on the mid layer only. Pixel-perfect collision is a future concern.
- The sprite editor undo stack snapshots whole state. Command-based undo should replace it once documents become large.
