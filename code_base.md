# Code Base Structure

This project is a C++ 2D RPG engine and integrated editor scaffold targeting a SNES/Zelda pixel-art style (see `RPG_Engine_Specification.md`). The implemented app is an editor built with Dear ImGui, GLFW, and OpenGL. Runtime game code is authored in `src/game` and must remain ImGui-free.

The main architectural rule is that the editor creates data the game can load. Runtime code lives outside `src/editor` and must not depend on ImGui.

The current asset architecture separates reusable game-library assets from chapter usage, and separates projects from each other. New work is stored under `projects/<project>/assets/...`; the repo-root `assets/` tree is retained as a fallback/default asset set. Reusable assets such as characters, enemy types, NPC types, weapon definitions, and project state/effect definitions live in each project's `assets/game/...` library and are indexed by that project's `assets/game/project.adgame`; chapters import/reference those asset ids rather than copying asset data.

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
      project.json                  # legacy/simple asset root manifest
      project.adgame                # game-library manifest: characters, enemies, NPCs, weapons, quest state/effects
      chapters/                     # .adchapter chapter files
      maps/                         # .admap tile maps
      sprites/                      # .sprite.json metadata
      character_sprites/            # game-ready character sprite assets
      characters/                   # .adcharacter reusable character sheets
      animations/                   # planned animation data
      palettes/                     # planned palette data
      paths/                        # .adpath enemy waypoint paths (legacy; placements now in .adchapter)
      tilesets/                     # .tileset.json tileset definitions
  projects/
    <project>/
      assets/
        raw/                        # project-local editable/source PNG exports
        game/                       # project-local runtime assets and project.adgame
          chapters/                 # project .adchapter files
          maps/                     # project .admap files
          sprites/                  # project .sprite.json files
          characters/               # project .adcharacter files
          paths/                    # project .adpath enemy waypoint paths (legacy)
          tilesets/                 # project screen graphics and tileset definitions
  external/
    imgui/                          # Dear ImGui source and backends
    stb/                            # stb_image for PNG loading
  src/
    app/
      main_editor.cpp               # windowed editor executable
      main_game.cpp                 # windowed runtime executable
      main_editor_smoke.cpp         # headless ImGui editor smoke test
      main_game_smoke.cpp           # runtime map + chapter loader smoke test
    editor/
      asset_directories.hpp/.cpp    # central asset root paths
      editor_app.hpp/.cpp           # top-level ImGui tabs
      editor_context.hpp            # shared editor context
      imgui_widgets.hpp             # shared label-left checkbox/slider helpers
      stb_image_impl.cpp            # stb_image implementation unit
      panels/
        character_editor_panel.hpp/.cpp
        enemy_path_editor_panel.hpp/.cpp  # enemy type defs + per-screen enemy placements + spline editor
        item_placement_panel.hpp/.cpp     # place weapon/ammo/health pickups on screens (ScreenEditMode::Items)
        layout_editor_panel.hpp/.cpp
        map_editor_panel.hpp/.cpp         # legacy/detail map panel; not a top-level tab
        npc_editor_panel.hpp/.cpp         # NPC type defs + per-screen NPC placements + patrol path editor
        sprite_editor_panel.hpp/.cpp
        tileset_editor_panel.hpp/.cpp
        wall_floor_paint_panel.hpp/.cpp
        weapon_editor_panel.hpp/.cpp      # create/edit WeaponDef game-library assets (Weapons tab)
    game/
      chapter.hpp/.cpp              # Chapter / ChapterScreen / ScreenLink / EnemyPlacement / NpcPlacement types and .adchapter load/save
      engine.hpp/.cpp               # GLFW/OpenGL runtime loop, screen loading, rendering, collision, combat
      map.hpp/.cpp                  # TileMap type and .admap load/save (v5 adds item placements)
      path.hpp/.cpp                 # EnemyPath type and .adpath load/save (legacy; new enemies use chapter placements)
      project.hpp/.cpp              # GameProject / EnemyType / EnemyAttackDef / NpcTypeDef and .adgame load/save (v8)
      sprite.hpp/.cpp               # Sprite metadata type and .sprite.json load/save
      state.hpp/.cpp                # GameState runtime store and .adstate save/load
      tileset.hpp/.cpp              # TilesetDef / TileDef types and .tileset.json load/save
      weapon.hpp                    # WeaponDef struct (header-only; stored in project.adgame)
```

## Build Targets

```text
imgui                    Static Dear ImGui library.
adventure_game           Runtime-facing game/data code (chapter, project, map, path, sprite metadata, state, tileset).
adventure_editor         Editor library. Depends on imgui and adventure_game.
adventure_editor_smoke   Headless editor smoke executable.
adventure_game_smoke     Loads .admap, .adchapter, .sprite.json, and round-trips .adpath, .adstate, and project state/effect definitions through runtime code.
adventure_game_window    GLFW/OpenGL runtime game window (built when OpenGL + GLFW found).
adventure_editor_window  GLFW/OpenGL editor window (built when OpenGL + GLFW found).
```

Useful commands:

```sh
cmake -B build
cmake --build build --parallel

./build/adventure_game_smoke
./build/adventure_game_smoke assets/game/maps/new_map.admap assets/game/chapters/chapter_1.adchapter
./build/adventure_editor_smoke
./build/adventure_game_window       # direct launch: opens project/chapter picker
./build/adventure_game_window projects/<project>/assets/game/chapters/<chapter>.adchapter
./build/adventure_editor_window   # macOS: emits OpenGL deprecation warnings, harmless
```

From the editor, `Chapter > Save and Play Game` and scoped `Save and Play` buttons save the current project/chapter data and launch `adventure_game_window` as a separate runtime process with an explicit chapter path. The game executable derives its runtime asset root from that chapter path, so editor launches use the selected project folder. Escape closes the game window.

When `adventure_game_window` is launched without arguments it scans `projects/<project>/assets/game/chapters/*.adchapter` and the repo-root fallback assets, then shows a small ImGui picker asking which project/chapter to load.

## Dependency Direction

```text
editor window      →  adventure_editor  →  adventure_game
game window        →  runtime engine    →  adventure_game
editor_smoke       →  adventure_editor  →  adventure_game
game_smoke                             →  adventure_game
```

Runtime modules in `src/game` must not include editor headers or ImGui headers.

## Editor App Tabs

On startup, `EditorApp` opens an `Open Project` modal. The user selects an existing project folder and chapter, or enters a project name and chapter name to create/open `projects/<project>/`. Closing the editor or switching chapters prompts to save or discard unsaved work.

`EditorApp` owns these top-level ImGui tabs (in order):

| Tab | Panel | Purpose |
|-----|-------|---------|
| Characters | `CharacterEditorPanel` | Character sheets with sprite references, Add Character/Delete Character, playable selection |
| Weapons | `WeaponEditorPanel` | Create/edit project-level WeaponDefs (melee + ranged) and set starting weapon |
| Quest State | inline `EditorApp` project-state view | Create/edit designer-defined state variables and reusable effects |
| Screens | `LayoutEditorPanel` | Continuous chapter screen grid, selected-screen tile editing, add/link/delete screens |
| Tilesets | `TilesetEditorPanel` | Generate tileset definitions from source PNG |
| Assets | *(inline)* | Asset directory listing |

`SpriteEditorPanel`, `WallFloorPaintPanel`, `MapEditorPanel`, `EnemyPathEditorPanel`, `NpcEditorPanel`, and `ItemPlacementPanel` are contextual subviews reached from Characters or Screens. `Edit Screen Graphics` opens Wall/Floor Paint, which can switch to map logic for the same screen. `Edit Enemies`, `Edit Enemy Types`, `Edit NPCs`, `Edit NPC Types`, and `Edit Items` open scoped screen editors.

---

## Game Project Library

Implemented in `src/game/project.hpp/.cpp`.

`assets/game/project.adgame` (current version: **8**) is the project-level game-library manifest. It stores:

```cpp
enum class EnemyAttackType { Contact = 0, Melee = 1, Ranged = 2 };

struct EnemyAttackDef {
    EnemyAttackType type = EnemyAttackType::Contact;
    int damage = 1;
    float range = 32.0f;       // px: melee reach or ranged trigger distance
    float cooldown = 1.0f;     // seconds between activations (per-attack timer)
    float projectileSpeed = 120.0f;  // ranged only
    std::string ammoSpriteId;        // ranged only
    std::string animState;           // anim state to play when this attack fires
};

struct EnemyType {
    std::string id = "enemy_1";
    std::string spriteId = "enemy_1";
    int maxHealth = 1;
    int contactDamage = 1;
    float hitboxWidth = 12.0f;
    float hitboxHeight = 12.0f;
    float attackCooldownSeconds = 1.0f;  // legacy contact cooldown
    float speed = 64.0f;
    std::vector<EnemyAttackDef> attacks;
};

enum class NpcMovementMode { Stationary = 0, Patrol = 1, Wander = 2 };
enum class NpcInteractionMode { None = 0, Talk = 1, Shop = 2, Quest = 3 };

struct DialogueLine {
    std::string speaker;
    std::string text;
};

struct NpcTypeDef {
    std::string id = "npc_1";
    std::string spriteId;
    std::string characterId;
    NpcMovementMode defaultMovement = NpcMovementMode::Stationary;
    NpcInteractionMode defaultInteraction = NpcInteractionMode::Talk;
    float defaultSpeed = 32.0f;
    std::string defaultGraphId;
    std::vector<DialogueLine> defaultDialogue;
};

enum class StateVariableType { Integer, Boolean, Item };
struct StateVariableDef { std::string id; StateVariableType type; int defaultInt; bool defaultBool; };

enum class GameEffectType { SetInt, AddInt, SetBool, GiveItem, TakeItem };
struct GameEffectDef { std::string id; GameEffectType type; std::string targetId; int intValue; bool boolValue; };

struct GameProject {
    std::string id;
    std::string playableCharacterId;
    std::string startingWeaponId;
    std::string fontPath;
    std::vector<std::string> characterIds;
    std::vector<std::string> chapterIds;
    std::vector<EnemyType> enemyTypes;
    std::vector<WeaponDef> weaponDefs;
    std::vector<StateVariableDef> stateVariables;
    std::vector<GameEffectDef> effectDefs;
    std::vector<NpcTypeDef> npcTypes;
};
```

Format (ADGAME 8):

```text
ADGAME 8
id game
playable hero
characters 1
character hero
chapters 1
chapter chapter_1
enemy_types 1
enemy_type crow enemy_1 3 1 12.0 12.0 1.0 64.0 1
enemy_attack 2 1 200.0 2.0 120.0 attack_1 ammo_stone
weapon_defs 1
weapon_def slingshot 1 1 96.0 0.35 240.0 ammo_stone ammo_stone 1
starting_weapon slingshot
font "assets/raw/fonts/myfont.ttf"
state_defs 1
state_def Example_Count 0 0 0
effect_defs 1
effect_def increment_example 1 Example_Count 1 1
npc_types 1
npc_type shopkeeper shopkeeper_sprite - 0 1 32.0 - 2
dialogue Hello there!
dialogue Come back soon.
end
```

`enemy_type` line fields: `id spriteId maxHealth contactDamage hitboxW hitboxH contactCooldown speed attackCount`  
`enemy_attack` fields: `type damage range cooldown projectileSpeed animState ammoSpriteId` (type: 0=Contact, 1=Melee, 2=Ranged)

Version history: v1 (id/playable), v2 (chapters + enemy_types), v3 (weapon_defs + starting_weapon), v4 (state_defs + effect_defs), v5 (npc_types), v6 (npc dialogue lines), v7 (font), v8 (enemy attack defs).

Editor behavior:

- `CharacterEditorPanel::saveForChapter` saves reusable character documents and writes `project.adgame`.
- `EnemyPathEditorPanel::saveProjectEnemyTypes` saves reusable enemy type definitions (including attack defs) into `project.adgame`.
- `NpcEditorPanel::saveProjectNpcTypes` saves reusable NPC type definitions into `project.adgame`.
- `WeaponEditorPanel` saves weapon definitions and the project starting weapon into `project.adgame`.
- The Quest State tab in `EditorApp` saves state variable definitions and reusable effect definitions into `project.adgame`.
- All panels that load project data track `lastLoadedProjectRoot_` and reload when the active project folder changes, preventing stale data from a previously opened project.

Runtime behavior:

- `Engine::loadPlayableCharacter` first resolves `Chapter::playableCharacterId`.
- If the chapter has no playable id, it falls back to `project.adgame`.
- A legacy scan of `.adcharacter` files remains as a fallback.
- Weapon pickups call into `GameState::giveItem`, so item ownership and quest state use the same runtime registry.

## Chapter System

Implemented in `src/game/chapter.hpp/.cpp` and `src/editor/panels/layout_editor_panel.*`.

### Data model

```cpp
struct PathWaypoint {
    float x = 0.0f, y = 0.0f;
    float speedOverride = 0.0f;
    float waitSeconds = 0.0f;
    int facing = -1;
    std::string animState;
};

struct EnemyPlacement {
    std::string id = "enemy_1";
    std::string typeId = "enemy_1";      // references EnemyType in project.adgame
    PathBehavior behavior = PathBehavior::Patrol;
    PathCurveMode curveMode = PathCurveMode::Linear;
    float speedOverride = 0.0f;
    bool loop = true;
    bool respawn = false;
    std::vector<PathWaypoint> waypoints;
};

struct NpcPlacement {
    std::string id = "npc_1";
    std::string typeId = "npc_1";        // references NpcTypeDef in project.adgame
    float x = 0.0f, y = 0.0f;
    int facing = 0;                      // 0=S, 1=N, 2=E, 3=W
    float awarenessRadius = 64.0f;
    float interactionRadius = 24.0f;
    NpcMovementMode movementOverride = NpcMovementMode::Stationary;
    std::vector<PathWaypoint> waypoints;
    bool loop = true;
    float speedOverride = 0.0f;
    std::string graphOverride;
    std::vector<DialogueLine> dialogueOverride;
};

struct ScreenLink { std::string north, south, east, west; };

struct ChapterScreen {
    std::string id;
    std::string mapId;        // .admap to load for this screen
    int gridX, gridY;
    ScreenLink links;
    bool respawnEnemies = false;
    std::vector<EnemyPlacement> enemies;   // per-screen enemy instances
    std::vector<NpcPlacement> npcs;        // per-screen NPC instances
};

struct Chapter {
    std::string id;
    std::string startScreenId;
    std::string playableCharacterId;
    std::vector<std::string> importedCharacterIds;
    std::vector<ChapterScreen> screens;
};
```

### `.adchapter` format (v3+)

```text
ADCHAPTER 3
id chapter_1
start screen_1
playable hero
characters 1
character hero
screens 1
screen screen_1 new_map 0 0
links - - - -
respawn 0
enemies 1
enemy enemy_1 crow_1 1 0 0.0 1 0
wp 100.0 80.0
wp 200.0 80.0
npcs 1
npc npc_1 shopkeeper_1 120.0 200.0 2 64.0 24.0 1
wp 120.0 200.0 0.0 2.0
dialogue Merchant Hello there traveler!
end
```

v1 files (no `respawn` per screen) load with `respawnEnemies = false`. v2 files load without character imports. v3 adds character imports, playable character id, per-screen enemy placements, and per-screen NPC placements.

### Layout Editor

- Continuous canvas showing screens as edge-to-edge tile grids based on `gridX/gridY`; the selected screen is centered and highlighted.
- Adjacent/nearby screens render their mid-layer wall layout as context.
- The selected screen's `.admap` can be edited directly in the Screens tab.
- Directional buttons create or select connected screens north/south/east/west and write reciprocal screen links.
- Screen list sidebar and inspector edit id, mapId, grid position, links, respawn flag, and deletion.
- `Edit Screen Graphics` opens the context-aware `WallFloorPaintPanel` subview for the selected screen.
- Screen layout can show scaled graphics previews from `<mapId>_preview.png` behind the structural tile overlay.

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
    std::vector<MapObstacle> obstacles;
    std::vector<MapItemPlacement> items;
};
```

### `.admap` format (v5)

```text
ADMAP 5
id new_map
tileset overworld
size 48 32
spawn 1 1
layer 0
0 0 0 ... (48 values) ...
layer 1
1 1 1 1 1 ... 1
1 0 0 ... 1
...
layer 2
0 0 0 ...
obstacles 2
obstacle 0 spikes 5 5 2 1 1.0 0.0 0.0
obstacle 2 timed_spikes 8 5 2 1 1.0 1.0 0.5
items 1
item item_1 1 ammo_stone 5 384.0 256.0 0 ammo_pickup
end
```

Obstacle fields: `type spriteId x y width height activeSeconds inactiveSeconds phaseSeconds`, where type is `0=Spike`, `1=Pit`, `2=TimedSpike`.
Item fields: `id pickupType targetId quantity x y respawn spriteId`, where pickup type is `0=Weapon`, `1=Ammo`, `2=Health`.
Backward compat: v1–v4 files still load; missing sections default to empty.

### Map Editor

- Layer selector: Floor / Mid / Ceiling radio buttons.
- Copy/paste, tileset palette, obstacle edit mode, save/load.

### Item Placement Editor

Implemented in `src/editor/panels/item_placement_panel.*`.

- Opened from the selected screen inspector via `Edit Items`.
- Places weapon, ammo, and health pickups into the selected screen's `.admap` items section.
- Left column: item list and selected-item properties. Right: top-aligned canvas.
- Canvas shows floor/wall graphics, wall guide, tile grid, and diamond item markers.

---

## Tileset System

Implemented in `src/game/tileset.hpp/.cpp` and `src/editor/panels/tileset_editor_panel.*`.

```cpp
struct TileDef { int id; std::string name; bool solid; };
struct TilesetDef { std::string id; std::string sourcePath; int tileWidth, tileHeight; std::vector<TileDef> tiles; };
```

Format: `assets/game/tilesets/<id>.tileset.json`. Editor generates tile definitions from a source PNG grid.

---

## Runtime Engine

Implemented in `src/game/engine.*` and `src/app/main_game.cpp`. Built as `adventure_game_window`.

### Key runtime structs

```cpp
struct RuntimePathEntity {
    EnemyPath path;             // legacy path data (combat, waypoints, spriteId)
    float x, y;
    float pathDistance;
    int health;
    float contactCooldownSeconds;
    float deathSeconds;         // >= 0 means dying, counts up to visual duration
    std::size_t waypointIndex;
    float waitRemainingSeconds;
    bool atWaypoint;
    float animSeconds;          // animation clock for this entity
    std::string animState;      // current frame-filter state ("idle", "walk", "attack_1", …)
    std::vector<float> attackCooldowns;  // per-attack cooldown timers, indexed by combat.attacks
    float facingX = 1.0f;       // unit vector: direction entity is facing
    float facingY = 0.0f;
};

struct RuntimeNpcEntity {
    NpcPlacement placement;
    std::string spriteId;
    std::vector<DialogueLine> dialogue;
    float x, y;
    float animSeconds;
    std::string actionType;     // "idle" or "walk"
    std::size_t waypointIndex;
    float pathDistance;
    bool playerInAwareness;
    float waitRemainingSeconds;
    bool atWaypoint;
    float facingX = 1.0f;       // unit vector: direction NPC is facing
    float facingY = 0.0f;
};
```

### Direction-aware sprite selection

Three functions implement the same logic at different scopes:

| Function | Subject | State source |
|---|---|---|
| `playerSpriteFrame(bool& flipH)` | Player | `playerActionType_`, `playerFacingX_/Y_`, `playerAnimSeconds_` |
| `spriteFrameForEntity(sprite, entity, bool& flipH)` | Path entity | `entity.animState`, `entity.facingX/Y`, `entity.animSeconds` |
| `spriteFrameForNpc(sprite, npc, bool& flipH)` | NPC | `npc.actionType`, `npc.facingX/Y`, `npc.animSeconds` |

All three: look up frames by action type + facing direction, apply W↔E / NW↔NE / SW↔SE mirroring (sets `flipH = true`), fall back to any frame of that action then to `"idle"`. The render loop applies the flip by swapping `u0`/`u1` in `renderTextureRegion`.

### `facingX/Y` updates

- **Enemies (linear paths):** `updatePaths` writes `entity.facingX = dx/dist; entity.facingY = dy/dist` on every movement step toward the current waypoint.
- **Enemies (spline paths):** `updatePaths` samples the Catmull-Rom tangent (two points `kTangentDelta` px apart along the curve) and normalises it into `entity.facingX/Y`.
- **NPCs:** `updateNpcs` writes `npc.facingX = dx/dist; npc.facingY = dy/dist` when moving toward a waypoint.
- Initial value for both is `(1.0, 0.0)` (facing East) until the entity first moves.

### `updateEnemyCombat`

Runs each frame for all path entities. Advances `entity.animSeconds`. Ticks `entity.attackCooldowns` (one float per `combat.attacks` entry). For each `EnemyAttackDef`:
- **Contact:** handled by the legacy `contactDamage` path (overlap check).
- **Melee:** fires when player is within `atk.range` and cooldown is zero; deals `atk.damage` and resets the per-attack cooldown.
- **Ranged:** fires when player is within `atk.range`; spawns a `RuntimeProjectile` headed toward the player at `atk.projectileSpeed`.
- If `atk.animState` is set and the attack fires, transitions `entity.animState` and resets `entity.animSeconds`.

### Other runtime behavior

- Initializes GLFW/OpenGL window and runs fixed 60 Hz update loop.
- Direct launch opens project/chapter picker; explicit chapter path skips it.
- Loads `.admap`, resolves playable character, loads per-screen enemy and NPC placements from `ChapterScreen`.
- Collision against nonzero cells in `.admap` layer 1.
- Screen-boundary crossings with 30% threshold trigger sliding transition to linked screen.
- Obstacle hazards (spikes, pits, timed spikes) respawn the player at the map spawn.
- Melee attack (Z): hitbox sweep in facing direction, brief yellow flash. Ranged (X): projectile from `WeaponDef` data.
- Item pickups equip weapons, add ammo, or restore HP. All item ownership writes to `GameState`.
- NPC dialogue: E key triggers interaction, subsequent E advances lines; speech bubble / dialogue box rendered above NPC.

Current limitations:

- Rendering uses fixed-pipeline OpenGL; a shader/core-profile renderer is planned.
- Enemy defeat persistence not yet implemented.
- Collision is binary: nonzero mid-layer tile means solid.

---

## Sprite Editor

Implemented in `src/editor/panels/sprite_editor_panel.*`.

- Pixel editing with frames, layers, palette, preview, and animation playback.
- Tools: pen, mirror, bucket, eraser, stroke, line, rect, circle, move, select, picker, shade.
- **Frame metadata section** (right inspector): action type (combo: idle/walk/run/attack/etc.), facing direction (combo: any/E/W/N/S/NE/NW/SE/SW), and duration in ms. The direction system is shared by all sprites: player, enemy, NPC alike.
- Transform actions: flip H/V, rotate CW. Clipboard: copy/paste selections.
- Snapshot-based undo. OS-aware shortcuts (`Cmd` on macOS, `Ctrl` elsewhere).

### Per-sprite dirty buffer system

`SpriteEditorPanel` holds a `std::unordered_map<std::string, SpriteDocumentBuffer> documentBuffers_` keyed by sprite ID. Switching sprites stashes/restores the document. Chapter save flushes all dirty entries. Chapter switch calls `resetDocumentBuffers()`.

Sprite metadata (runtime-facing, `src/game/sprite.hpp/.cpp`):

```cpp
struct SpriteFrameDef {
    int x, y, width, height;
    int durationMs;
    std::string type;       // animation action: "idle", "walk", "attack_1", etc.
    std::string direction;  // "E","W","N","S","NE","NW","SE","SW", or empty = any direction
};
struct SpriteMetadata {
    std::string id;
    std::filesystem::path source;
    std::array<int, 2> canvasSize, gridSize, pivot;
    std::vector<SpriteFrameDef> frames;
    std::vector<std::string> tags;
};
```

---

## Character Editor

Implemented in `src/editor/panels/character_editor_panel.*`.

- Saves reusable character sheets to `assets/game/characters/<id>.adcharacter`.
- Character sheets store name, bio, sprite metadata reference, playable flag, animation slots, and per-frame assignments.
- Add Character and Delete Character controls. Panel prevents deleting the last remaining character.
- Only one character can be marked playable at a time.
- Frame thumbnail PNGs are decoded once and cached for the editing session.

---

## Game State Registry

Implemented in `src/game/state.hpp/.cpp`.

```cpp
class GameState {
public:
    int  getInt(const std::string& id, int fallback = 0) const;
    void setInt(const std::string& id, int value);
    int  addInt(const std::string& id, int delta);
    bool getBool(const std::string& id, bool fallback = false) const;
    void setBool(const std::string& id, bool value);
    bool hasItem(const std::string& id) const;
    void giveItem(const std::string& id);
    void takeItem(const std::string& id);
};
```

`.adstate` stores runtime values. Variable names are designer-authored content, not hard-coded engine behavior.

---

## Wall / Floor Paint

Implemented in `src/editor/panels/wall_floor_paint_panel.*`.

- Two-layer pixel painter (Floor + Wall). Canvas locked to `kScreenTilesW × kTileSize` × `kScreenTilesH × kTileSize` (768 × 512 px).
- Tools: Pencil, Eraser, Fill, Line, Rect, Select, Tile Draw, Tile Select, Tile Paste, Stamp, Tile Fill, Tile Erase.
- Brush shapes: Square, Circle, Spray, Dither. Zoom 1–16.
- Tile palette: `Tile Select` → Add to palette → Stamp / Tile Fill across all screens.
- Undo stack (up to 50 steps).

### Per-screen dirty buffer system

`WallFloorPaintPanel` holds `std::unordered_map<std::string, ScreenGraphicsBuffer> screenBuffers_` keyed by `mapId`. Chapter save flushes dirty entries. Chapter switch calls `resetScreenBuffers()`.

Exported paint files (one set per screen, `<id>` = `mapId`):

| File | Purpose |
|------|---------|
| `assets/raw/tilesets/<id>_floor.png` | Editable/source floor art |
| `assets/raw/tilesets/<id>_wall.png` | Editable/source wall art |
| `assets/raw/tilesets/<id>_preview.png` | Editable/source composite preview |
| `assets/game/tilesets/<id>_floor.png` | Game-ready floor art |
| `assets/game/tilesets/<id>_wall.png` | Game-ready wall/overhead art |
| `assets/game/tilesets/<id>_preview.png` | Game-ready composite preview |

---

## Enemy Editor / Path Editor

Implemented in `src/game/path.*` (legacy `.adpath` format), `src/game/chapter.*` (current `EnemyPlacement` in screens), and `src/editor/panels/enemy_path_editor_panel.*`.

Addresses spec §4.4.

### Data model

Enemy type definitions live in `project.adgame` (`EnemyType` + `EnemyAttackDef`, see Game Project Library section above).

Per-screen enemy instances are stored as `EnemyPlacement` in `ChapterScreen`:

```cpp
struct EnemyPlacement {
    std::string id;         // instance id
    std::string typeId;     // references EnemyType.id in project.adgame
    PathBehavior behavior;
    PathCurveMode curveMode;
    float speedOverride;
    bool loop;
    bool respawn;
    std::vector<PathWaypoint> waypoints;
};
```

Runtime: `Engine::loadPathEntities` builds `RuntimePathEntity` from placements + matching `EnemyType` from the project library. `entity.attackCooldowns` is sized to match `type.attacks.size()`.

### Enemy Type Editor (`Edit Enemy Types` subpanel)

Two-column layout using `BeginGroup`/`EndGroup`:

- **Left column:** `Dummy` reserves space for the sprite preview panel (drawn via `ImDrawList`). Shows the first idle frame of the type's sprite sheet at pixel scale, backed by raw RGBA pixels loaded with `stbi_load`. Below the preview: `Checkbox("Show hitbox")` toggles a red overlay rectangle scaled to `hitboxWidth/Height` relative to the frame.
- **Right column:** Type ID, Sprite ID (with Edit Sprite button), Max HP, Speed, Hitbox W/H, Contact Damage, Contact Cooldown.
- **Attack editor** (below both columns): one `CollapsingHeader` per `EnemyAttackDef`, exposing type combo (Contact/Melee/Ranged), damage, range, cooldown, animation state, and ranged-only fields (projectile speed, ammo sprite ID). Add/Remove attack buttons.

Sprite preview reload guard: reloads when `spritePreview_.loadedId != type.spriteId` OR `spritePreview_.loadedProjectRoot != context.assets.projectRoot.string()` — prevents stale preview after project switch.

### Enemy Placement Editor (`Edit Enemies` / `Edit Splines` subpanels)

- Canvas modes: **Add Enemies** (place/select/delete enemy anchors by typeId) and **Edit Splines** (add/move/delete waypoints for the selected enemy's path).
- Canvas shows the selected screen's map, graphics, grid, enemy markers, and spline for context.
- Behavior (Idle/Patrol/Aggro), speed override, loop, respawn controls.

### `.adpath` format (v3, legacy)

Still used by the smoke test and for backward compatibility. New enemy data lives in `.adchapter`.

```text
ADPATH 3
id path_1
enemy slime_1
sprite slime
map new_map
behavior 1
curve 1
combat 3 1 12.0 12.0 1.0
speed 64.0
loop 1
respawn 0
waypoints 4
wp 48.0 32.0 ...
end
```

---

## NPC Editor

Implemented in `src/game/chapter.*` (`NpcPlacement`, `NpcTypeDef`) and `src/editor/panels/npc_editor_panel.*`.

Addresses spec §4.5.

### NPC Type Editor (`Edit NPC Types` subpanel)

- Lists reusable `NpcTypeDef` records from `project.adgame`.
- Fields: id, sprite ID, character ID, default movement mode, default interaction mode, default speed, graph ID, default dialogue lines.
- `saveProjectNpcTypes` writes updated types back to `project.adgame`.

### NPC Placement Editor (`Edit NPCs` subpanel)

Canvas modes: **Place NPCs** (create/select/delete NPC anchors at world positions) and **Edit Path** (add/move/delete patrol waypoints).

Inspector fields:
- Instance ID, type ID
- Position (x, y), facing direction (N/S/E/W)
- Awareness radius, interaction radius
- Movement override (Stationary / Patrol / Wander), loop, speed override
- Per-waypoint: position, speed override, wait seconds, facing direction, animation state
- Dialogue lines (inline override, one per entry)

Canvas shows selected screen's map, floor/wall graphics, tile grid, NPC markers, and patrol path.

Runtime behavior:

- `updateNpcs` advances NPCs along patrol waypoints; sets `npc.actionType = "walk"` while moving and `"idle"` when at waypoint or in awareness range of player.
- `npc.facingX/Y` updates with the movement direction so `spriteFrameForNpc` can apply directional frame selection and horizontal mirroring.
- `updateNpcAwareness` checks distance to player and sets `npc.playerInAwareness`; when true, the NPC stops patrolling.
- `updateInteraction` (E key): `None → PromptVisible → InDialogue`. Each subsequent E press advances `dialogueLineIndex_`. Leaving interaction radius resets state.
- `renderNpcs` calls `spriteFrameForNpc(sprite, npc, flipH)` and swaps u0/u1 when `flipH` is true.
- `renderInteractionPrompt` shows a pulsing green square above the NPC when in prompt range.
- `renderSpeechBubble` / `renderDialogueBox` shows wrapped dialogue text above the NPC.

---

## Asset Directories

`AssetDirectories` centralises editor asset paths. All paths are relative to `projectRoot`. In normal editor use `projectRoot` is `projects/<project>/`.

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

---

## Implemented vs. Spec

| Spec section | Status |
|---|---|
| §3.1 Chapters / Screens / Screen-Flip links | ✅ Data model + editor + basic runtime transition |
| §3.1 Parallax / pseudo-3D | ✅ Editor preview + runtime floor/wall pre-baked PNG render |
| §3.2 Display & tile dimensions locked to constants | ✅ All editors use `kTileSize`, `kScreenTilesW/H` from `constants.hpp` |
| §3.3 Real-time combat / timed mechanics | ✅ Contact damage, per-attack Melee/Ranged, player health, invulnerability, respawn |
| §3.3 Enemy attack types (Contact/Melee/Ranged) | ✅ `EnemyAttackDef` in project library; per-attack cooldowns in runtime |
| §3.3 Enemy respawn flag | ✅ `respawnEnemies` on `ChapterScreen` |
| §4.1 Layout editor (macro view, screen management) | ✅ |
| §4.1 Project isolation / panel root tracking | ✅ `lastLoadedProjectRoot_` in all data-loading panels |
| §4.2 3-layer tile maps (floor / mid / ceiling) | ✅ |
| §4.2 Copy/paste tiles | ✅ |
| §4.3 Pixel painting — per-screen isolation & dirty buffers | ✅ |
| §4.3 Pixel painting — tile palette & stamp tool | ✅ |
| §4.3 Sprite & animation editor — per-sprite dirty buffers | ✅ |
| §4.3 Sprite metadata round-trip | ✅ |
| §4.3 Sprite frame direction field | ✅ `"E"`/`"W"`/etc. per frame, empty = any direction |
| §4.4 Enemy type definitions (project library) | ✅ `EnemyType` + `EnemyAttackDef` in `project.adgame` v8 |
| §4.4 Enemy type editor: sprite preview + hitbox overlay | ✅ First idle frame shown at pixel scale; red hitbox overlay toggle |
| §4.4 Enemy type editor: attack definitions | ✅ Per-attack collapsing sections with type/damage/range/cooldown/anim/sprite fields |
| §4.4 Enemy type editor: two-column layout (no overlap) | ✅ `BeginGroup`/`EndGroup` on both columns |
| §4.4 Enemy waypoint/spline paths | ✅ Shared `.adpath` v3 format (legacy) + `EnemyPlacement` in chapter screens |
| §4.4 Enemy behavior states (idle/patrol/aggro) | ✅ In path data; runtime moves non-idle paths |
| §4.4 Enemy sprite direction-aware rendering + flip | ✅ `spriteFrameForEntity` with `bool& flipH`; `facingX/Y` from movement direction |
| §4.4 Enemy combat — per-attack Melee/Ranged | ✅ `updateEnemyCombat` fires attacks with per-attack timers |
| §4.5.1 Game state registry | ✅ Generic `GameState`, `.adstate`, `.adgame` v4 state/effect defs, Quest State tab |
| §4.5.2 NPC type definitions (project library) | ✅ `NpcTypeDef` in `project.adgame` v5+ |
| §4.5.2 NPC placements in chapter screens | ✅ `NpcPlacement` in `ChapterScreen` |
| §4.5.3 NPC patrol movement + wait-at-waypoint | ✅ `updateNpcs` with waypoint queue and wait timer |
| §4.5.3 NPC direction-aware rendering + flip | ✅ `spriteFrameForNpc` with `bool& flipH`; `facingX/Y` from movement direction |
| §4.5.3 NPC player awareness | ✅ Awareness radius check; NPC idles when player is near |
| §4.5.4 NPC interaction + sequential dialogue | ✅ E key, prompt, speech bubble / dialogue box, multi-line advance |
| §4.5.5 Flow graph editor | ❌ Planned |
| §5 Save/load (JSON, text formats) | ✅ |
| §6 Runtime game engine (rendering, screen-flip) | ✅ Basic GLFW/OpenGL runtime shell |
| §6 Runtime collision | ✅ Tile collision against `.admap` mid layer |

## Near-Term Priorities

1. Add enemy defeat persistence: registry per chapter/screen, respawn flag respected at screen load.
2. Add branching NPC dialogue (condition/choice nodes) and flow graph executor on top of the generic state/effect system.
3. Add player attack animations and enemy hurt/death state transitions.
4. Replace runtime fixed-pipeline OpenGL with a shader/core-profile renderer.
5. Extend collision beyond binary mid-layer solid tiles only when design needs require transparent/interaction flags.

## Engineering Notes

- Keep editor UI state out of `src/game`.
- File format parsing belongs in runtime-facing modules when the game needs to load that format. Current shared parsers: chapter, map, path, project, sprite metadata, state, tileset.
- Prefer readable text formats while the project is small.
- All editor panels that load project-level data must store `lastLoadedProjectRoot_` and compare it against `context.assets.projectRoot.string()` on each draw call, reloading if it differs. This prevents stale data appearing when the user switches projects.
- The sprite editor undo stack snapshots whole state. Command-based undo should replace it once documents become large.
- Both `WallFloorPaintPanel` and `SpriteEditorPanel` follow the same dirty-buffer pattern: in-memory maps keyed by asset ID hold unsaved state across the session; chapter save flushes dirty entries; chapter switch calls `reset*Buffers()` to clear stale data.
- Direction-aware sprite rendering is implemented in three parallel functions (`playerSpriteFrame`, `spriteFrameForEntity`, `spriteFrameForNpc`) that share the same frame-selection and W↔E mirroring logic. Any changes to the direction/mirror algorithm should be applied to all three.
