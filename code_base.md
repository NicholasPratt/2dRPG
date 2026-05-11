# Code Base Structure

This project is currently a C++ 2D RPG editor/runtime scaffold. The implemented app is an editor built with Dear ImGui, GLFW, and OpenGL. Runtime game code is only just starting: the first shared game-facing module is a tile map loader/saver for custom `.admap` map files.

The main architectural rule is that the editor creates data the game can load. Runtime code lives outside `src/editor` and must not depend on ImGui.

## Current Layout

```text
2drpg/
  CMakeLists.txt
  code_base.md
  assets/
      raw/
      sprites/                         # PNG exports/source sprite sheets
      character_sprites/               # raw character sprite sheets
      tilesets/                         # raw map tileset images
    game/
      project.json                     # asset root manifest
      sprites/                         # .sprite.json metadata
      character_sprites/                # game-ready character sprite sheet assets
      maps/                            # .admap tile maps
      characters/                      # planned character data
      animations/                      # planned animation data
      palettes/                        # planned palette data
  external/
    imgui/                             # Dear ImGui source and backends
  src/
    app/
      main_editor.cpp                  # windowed editor executable
      main_editor_smoke.cpp            # headless ImGui editor smoke test
      main_game_smoke.cpp              # runtime map loader smoke test
    editor/
      asset_directories.hpp/.cpp       # central asset root paths
      editor_app.hpp/.cpp              # top-level ImGui tabs
      editor_context.hpp               # shared editor context
      panels/
        character_editor_panel.hpp/.cpp
        map_editor_panel.hpp/.cpp
        sprite_editor_panel.hpp/.cpp
    game/
      map.hpp/.cpp                     # runtime .admap map type/load/save
```

## Build Targets

The active CMake targets are:

```text
imgui                    Static Dear ImGui library.
adventure_game           Runtime-facing game/data code. Currently map loading/saving.
adventure_editor         Editor library. Depends on imgui and adventure_game.
adventure_editor_smoke   Headless editor smoke executable.
adventure_game_smoke     Loads an .admap file through runtime code.
adventure_editor_window  GLFW/OpenGL editor window, built when dependencies are found.
```

Useful commands:

```sh
cmake --build build --target adventure_editor_window
cmake --build build --target adventure_editor_smoke
cmake --build build --target adventure_game_smoke
./build/adventure_game_smoke assets/game/maps/new_map.admap
./build/adventure_editor_smoke
```

On macOS, the window target currently emits OpenGL deprecation warnings and a GLFW deployment-version linker warning. These are warnings only.

## Dependency Direction

The intended dependency direction is:

```text
editor -> game/runtime data modules
editor -> imgui
window executable -> editor -> game
game executable/smoke -> game
```

Runtime modules in `src/game` must not include editor headers or ImGui headers. Shared authored file formats should be implemented in runtime-facing code when the game needs to load them.

## Editor App

`EditorApp` owns the top-level ImGui tabs:

- `Characters`
- `Sprites`
- `Maps`
- `Assets`

The editor currently uses simple retained panel state rather than a document system. Undo exists only inside the sprite editor and is snapshot-based.

## Character Editor

Implemented in `src/editor/panels/character_editor_panel.*`.

Current behavior:

- Shows character names in a side list.
- Selecting a character opens a simple sheet.
- A sheet has name, bio, and sprite reference fields.
- Clicking the sprite reference opens the `Sprites` tab.
- If the sprite editor was launched from a character sheet, the selected character’s sprite reference follows the active sprite metadata path, e.g. `assets/game/sprites/new_sprite.sprite.json`.
- Character-launched blank sprites default to a `32x32` canvas.

Current limitation:

- Character data is in-memory only. There is no character save/load format yet.

## Sprite Editor

Implemented in `src/editor/panels/sprite_editor_panel.*`.

Current behavior:

- Pixel editing with frames, layers, palette, preview, and basic animation playback.
- Tools include pen, mirror, bucket, eraser, stroke, line, rect, circle, move, select, picker, and shade.
- Brush sizes: `1x1`, `2x2`, and `4x4`.
- Frame actions: add blank frame, copy frame, delete frame, clear frame.
- Transform actions: horizontal flip, vertical flip, clockwise rotate.
- Selection-aware transforms: active selection transforms only the selected region on the active layer; otherwise transforms apply to every layer in the current frame.
- Clipboard operations for selections: copy and paste.
- OS-aware keyboard shortcuts:
  - macOS: `Cmd+Z`, `Cmd+C`, `Cmd+V`
  - Linux/other: `Ctrl+Z`, `Ctrl+C`, `Ctrl+V`
- Snapshot-based undo for drawing, shapes, bucket/shade, paste, move, frame/layer changes, resize, clear frame, import, and new sprite.

Sprite export/import:

- `Save .sprite.json` writes metadata to `assets/game/sprites/<id>.sprite.json`.
- `Export single frame PNG` writes `assets/raw/sprites/<id>_frame_<n>.png`.
- `Export sprite sheet PNG` writes `assets/raw/sprites/<id>_sheet.png`.
- `Import source PNG` reads the `Source PNG` field.
- PNG import currently supports RGBA PNGs exported by this editor. It is not a general-purpose PNG decoder.

Current limitation:

- Sprite metadata loading is minimal. Opening a sprite reference updates the active sprite id/source path but does not fully deserialize `.sprite.json` into editor state yet.

## Map Editor

Implemented in `src/editor/panels/map_editor_panel.*`.

Current behavior:

- Basic wall/no-wall map painting.
- White tile = wall.
- Black tile = no wall.
- Spawn edit mode places the player start tile.
- Adjustable map width/height.
- Adjustable displayed tile size, defaulting to `16x16` pixels.
- Left-drag paints the selected tile type.
- Right-drag erases to no wall.
- `Fill walls` and `Clear walls`.
- `Save .admap` and `Load .admap` use the runtime map module.
- `Test map` starts an in-editor runtime preview. The 32x32 player spawns at the spawn tile and moves with cursor keys while wall tiles block movement.

The map editor saves to:

```text
assets/game/maps/<map_id>.admap
```

## `.admap` Format

Implemented in `src/game/map.hpp/.cpp`.

This is the first custom game-loadable file type. It is intentionally simple and text-based:

```text
ADMAP 1
id new_map
size 24 16
spawn 1 1
tiles
111111111111111111111111
100000000000000000000001
100000000000000000000001
111111111111111111111111
end
```

Rules:

- `ADMAP 1` is the magic/version header.
- `id` is a single token map id.
- `size <width> <height>` defines dimensions.
- `spawn <x> <y>` defines the player start tile. Older files without this line load with a default spawn.
- `tiles` contains exactly `height` rows.
- Each row must have exactly `width` characters.
- `0` means no wall.
- `1` means wall.
- `end` terminates the file.

Runtime API:

```cpp
namespace adventure::game {

struct TileMap {
    std::string id;
    int width;
    int height;
    std::vector<unsigned char> walls;
};

bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage = nullptr);
bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage = nullptr);

}
```

`adventure_game_smoke` verifies the runtime can load a map without editor code:

```sh
./build/adventure_game_smoke assets/game/maps/new_map.admap
```

Expected output:

```text
Loaded map new_map [24x16]
```

## Asset Directories

`AssetDirectories` centralizes editor asset paths:

```text
rawSprites       assets/raw/sprites
rawCharacterSprites assets/raw/character_sprites
rawTilesets      assets/raw/tilesets
gameSprites      assets/game/sprites
gameCharacterSprites assets/game/character_sprites
gameCharacters   assets/game/characters
gameMaps         assets/game/maps
gameTilesets     assets/game/tilesets
gameAnimations   assets/game/animations
gamePalettes     assets/game/palettes
```

The project manifest at `assets/game/project.json` mirrors these roots.

## Current Data Status

Implemented game-loadable data:

- `.admap` tile maps.

Implemented editor-authored data:

- `.sprite.json` metadata writes.
- Sprite PNG exports.
- In-memory character sheets.
- In-memory sprite pixel documents.
- In-memory map edits with `.admap` save/load.

Not implemented yet:

- General runtime sprite loader.
- Runtime renderer.
- Runtime collision/movement.
- Character save/load format.
- Tile set editor and tile metadata.
- Entity placement.
- Play-in-editor preview.
- Persistent project asset index beyond root paths.

## Near-Term Priorities

1. Add a real game window target that loads `assets/game/maps/new_map.admap` and draws white/black map tiles.
2. Add player position and collision against `.admap` wall tiles.
3. Add full `.sprite.json` loading into both editor and runtime.
4. Save/load character sheets, including sprite metadata references.
5. Expand `.admap` only when needed: spawn point, tile size, named layers, entities, exits, and triggers.
6. Replace the limited PNG importer with a real PNG decoder dependency if arbitrary external PNG import becomes important.

## Engineering Notes

- Keep editor UI state out of `src/game`.
- Keep file format parsing in runtime-facing modules when the game needs to load that format.
- Prefer readable text formats while the project is small.
- Avoid large engine abstractions until there is actual runtime gameplay pressure.
- The current sprite undo stack snapshots whole sprite editor state. This is pragmatic now, but command-based undo will become better once files and documents are larger.
