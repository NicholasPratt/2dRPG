# Code Base Structure

This project is a C++17 2D action-RPG runtime and integrated editor targeting a SNES/GBA pixel-art style (see `docs/RPG_Spec.md`). The editor uses Dear ImGui, GLFW, and OpenGL. The windowed runtime additionally uses SDL2 and Vorbisfile for music. Runtime-facing game code is authored in `src/game` and must remain ImGui-free.

The main architectural rule is that the editor creates data the game can load. Runtime code lives outside `src/editor` and must not depend on ImGui.

The asset architecture separates reusable game-library assets from chapter usage and isolates projects from each other. Work is stored under `projects/<project>/assets/...`. Reusable assets such as characters, enemy types, NPC types, weapon definitions, item definitions, and project state/effect definitions live in each project's `assets/game/...` library and are indexed by that project's `assets/game/project.adgame`; chapters import/reference those asset ids rather than copying asset data. Dialogue graph trees are chapter-specific content stored under `assets/game/dialogue/<chapterId>/`.

## Current Layout

```text
2drpg/
  CMakeLists.txt
  README.md
  docs/
    RPG_Spec.md                     # game design and feature specification
    code_base.md                    # architecture and data-format guide
    manual/                         # user-facing HTML manual and LLM index
  projects/
    <project>/
      assets/
        raw/                        # project-local editable/source PNG exports
        game/                       # project-local runtime assets and project.adgame
          chapters/                 # project .adchapter files
          maps/                     # project .admap files
          sprites/                  # project .sprite.json files
          characters/               # project .adcharacter files
          dialogue/                 # chapter-specific project .addialogue files
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
        dialogue_graph_editor_panel.hpp/.cpp  # scoped NPC dialogue graph editor
        door_placement_panel.hpp/.cpp    # per-screen door trigger placement/editor (ScreenEditMode::Doors)
        enemy_path_editor_panel.hpp/.cpp  # enemy type defs + per-screen enemy placements + spline editor
        item_placement_panel.hpp/.cpp     # place pickups/project items on screens (ScreenEditMode::Items)
        layout_editor_panel.hpp/.cpp
        map_editor_panel.hpp/.cpp         # legacy/detail map panel; not a top-level tab
        npc_editor_panel.hpp/.cpp         # NPC type defs + per-screen NPC placements + patrol path editor
        sprite_editor_panel.hpp/.cpp
        tileset_editor_panel.hpp/.cpp
        wall_floor_paint_panel.hpp/.cpp
        weapon_editor_panel.hpp/.cpp      # create/edit WeaponDef game-library assets (Weapons tab)
    game/
      chapter.hpp/.cpp              # Chapter / ChapterScreen / ScreenLink / EnemyPlacement / NpcPlacement types and .adchapter load/save
      dialogue_graph.hpp/.cpp       # DialogueGraph type and .addialogue load/save
      engine.hpp/.cpp               # GLFW/OpenGL runtime loop, screen loading, rendering, collision, combat
      map.hpp/.cpp                  # TileMap type and .admap load/save (v10 adds door locked SFX)
      path.hpp/.cpp                 # EnemyPath type and .adpath load/save (legacy; new enemies use chapter placements)
      project.hpp/.cpp              # GameProject / EnemyType / EnemyAttackDef / NpcTypeDef / ItemDef and .adgame load/save (v15)
      sprite.hpp/.cpp               # Sprite metadata type and .sprite.json load/save
      state.hpp/.cpp                # GameState runtime store and .adstate save/load
      tileset.hpp/.cpp              # TilesetDef / TileDef types and .tileset.json load/save
      weapon.hpp                    # WeaponDef struct (header-only; stored in project.adgame)
```

## Build Targets

```text
imgui                    Static Dear ImGui library.
adventure_game           Runtime-facing game/data code (chapter, dialogue graph, project, map, path, sprite metadata, state, tileset).
adventure_editor         Editor library. Depends on imgui and adventure_game.
adventure_editor_smoke   Headless editor smoke executable.
adventure_game_smoke     Loads .admap, .adchapter, optional .sprite.json, and round-trips .adpath, .adstate, scoped dialogue conditions/actions, and project state/event/NPC-rule definitions (ADGAME v16).
adventure_game_window    GLFW/OpenGL runtime game window with SDL2/Vorbis music (built when OpenGL, GLFW, SDL2, and Vorbisfile are found).
adventure_editor_window  GLFW/OpenGL editor window (built when OpenGL + GLFW found).
```

### Prerequisites

- CMake 3.20 or newer
- A C++17 compiler
- `pkg-config`
- OpenGL and GLFW 3 for windowed targets
- SDL2 and Vorbisfile for `adventure_game_window`

Debian/Ubuntu package example:

```sh
sudo apt-get install cmake g++ pkg-config libgl1-mesa-dev libglfw3-dev libsdl2-dev libvorbis-dev
```

Useful commands:

```sh
cmake -S . -B build
cmake --build build --parallel

./build/adventure_editor_smoke
./build/adventure_game_smoke projects/Billys_Crow_Hunt/assets/game/maps/screen_1_map.admap projects/Billys_Crow_Hunt/assets/game/chapters/Farm_House.adchapter
./build/adventure_game_window       # direct launch: opens project/chapter picker
./build/adventure_game_window projects/<project>/assets/game/chapters/<chapter>.adchapter
./build/adventure_editor_window
```

From the editor, `Chapter > Save and Play Game` and scoped `Save and Play` buttons save the current project/chapter data and launch `adventure_game_window` as a separate runtime process with an explicit chapter path. The game executable derives its runtime asset root from that chapter path, so editor launches use the selected project folder. Escape closes the game window.

Two **test launches** sit beside Save and Play (and in the Chapter menu): **Play Selected Screen** (`launchGame(fresh=true, startScreen=selectedScreen)`) and **Play From Last Entry** (`launchGame(fresh=true, fromCheckpoint=true)`, which reads `assets/game/test_checkpoint`). Both pass `--fresh` so authored enemies always appear and the player's real `save.adstate` is left untouched. `launchDetachedProcess` forwards the chapter path plus these extra args via `execv`. **Save and Play itself also starts a new game** (no `--continue`), so prior progress never carries over; the engine only loads `save.adstate` when launched with `--continue`.

The editor reopens the last project+chapter from `projects/.editor_session` on startup (falling back to the Open Project modal), and a **Project** menu → **Project Manager** opens/creates/deletes projects (delete is confirmed and permanent); switching projects respects the unsaved-changes prompt. See `EditorApp::{loadSession,saveSession,drawProjectManagerWindow,deleteProject,requestProjectOpen}`.

When `adventure_game_window` is launched without arguments it scans `projects/<project>/assets/game/chapters/*.adchapter`, then shows a small ImGui picker asking which project/chapter to load. The code can also scan a repository-level `assets/game/chapters/` directory when one exists, but this repository currently keeps authored content in project folders.

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
| Items | inline `EditorApp` project-items view | Create/edit project-level ItemDefs and seed common RPG item defaults |
| Quest State | inline `EditorApp` project-state view | Create/edit designer-defined state variables and reusable effects |
| Screens | `LayoutEditorPanel` | Continuous chapter screen grid, selected-screen tile editing, add/link/delete screens |
| Tilesets | `TilesetEditorPanel` | Generate tileset definitions from source PNG |
| Assets | *(inline)* | Asset directory listing |

`SpriteEditorPanel`, `WallFloorPaintPanel`, `MapEditorPanel`, `EnemyPathEditorPanel`, `NpcEditorPanel`, `DialogueGraphEditorPanel`, `ItemPlacementPanel`, and `DoorPlacementPanel` are contextual subviews reached from Characters or Screens. `Edit Screen Graphics` opens Wall/Floor Paint, which can switch to map logic for the same screen. `Edit Enemies`, `Edit Enemy Types`, `Edit NPCs`, `Edit NPC Types`, `Edit Items`, and `Edit Doors` open scoped screen editors. `Edit Instance Dialogue` opens `DialogueGraphEditorPanel` as a sub-screen of NPC placement so the graph is tied to the selected NPC instance.

---

## Game Project Library

Implemented in `src/game/project.hpp/.cpp`.

`assets/game/project.adgame` (current version: **16**) is the project-level game-library manifest. It stores:

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
    // Action-RPG hit reaction & AI tuning (ADGAME v12+)
    float knockbackResistance = 0.0f;    // 0 = full knockback, 1 = immovable
    float hitstunSeconds = 0.18f;        // stagger window when damaged
    float aggroRange = 0.0f;             // 0 = waypoints only; >0 = chase player within radius (px)
    std::string killVariable;            // GameState int incremented on death (quest hook)
    int killAmount = 1;                  // amount added to killVariable per death
};

enum class NpcMovementMode { Stationary = 0, Patrol = 1, Wander = 2 };
enum class NpcInteractionMode { None = 0, Talk = 1, Shop = 2, Quest = 3 };

struct DialogueLine {
    std::string speaker;
    std::string text;
};

struct ShopItemDef {
    std::string itemId;
    int buyPrice;
    int sellPrice;
    int quantity;
    bool unlimited;
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
    std::vector<ShopItemDef> shopInventory;
};

enum class StateVariableType { Integer, Boolean, Item };
struct StateVariableDef { std::string id; StateVariableType type; int defaultInt; bool defaultBool; };

enum class GameEffectType { SetInt, AddInt, SetBool, GiveItem, TakeItem };
struct GameEffectDef { std::string id; GameEffectType type; std::string targetId; int intValue; bool boolValue; };

enum class ItemDefType {
    Weapon, Ammo, Health, Mana, Currency, Key, Quest, Consumable, Material, Equipment, Custom
};
struct ItemDef {
    std::string id;
    std::string name;
    ItemDefType type;
    std::string spriteId;
    std::string targetId;
    int value;
    bool stackable;
    std::string customType;
};

struct GameProject {
    std::string id;
    std::string playableCharacterId;
    std::string startingWeaponId;
    std::string fontPath;
    std::vector<std::string> characterIds;
    std::vector<std::string> chapterIds;
    std::vector<EnemyType> enemyTypes;
    std::vector<WeaponDef> weaponDefs;
    std::vector<ItemDef> itemDefs;
    std::vector<StateVariableDef> stateVariables;
    std::vector<GameEffectDef> effectDefs;
    std::vector<NpcTypeDef> npcTypes;
};
```

Format (ADGAME 16):

```text
ADGAME 16
id game
playable hero
characters 1
character hero
chapters 1
chapter chapter_1
enemy_types 1
enemy_type crow enemy_1 3 1 12.0 12.0 1.0 64.0 0.25 0.2 120.0 Crows_Killed 1 1
enemy_attack 2 1 200.0 2.0 120.0 attack_1 ammo_stone
weapon_defs 1
weapon_def slingshot 1 1 96.0 0.35 240.0 slingshot ammo_stone ammo_stone 1 1 1.2 0.5 2.0 2.0 1 4.0 1.0 1.2 1 0 0 1.0 40.0 attack_2
item_defs 1
item_def gold "Gold" 4 gold Money 1 1 -
starting_weapon slingshot
font "assets/game/fonts/myfont.ttf"
state_defs 1
state_def Example_Count 0 0 0
effect_defs 1
effect_def increment_example 1 Example_Count 1 1
npc_types 1
npc_type shopkeeper shopkeeper_sprite - 0 2 32.0 - 2 1
dialogue Hello there!
dialogue Come back soon.
shop_item potion 10 5 3 0
end
```

`enemy_type` line fields: `id spriteId maxHealth contactDamage hitboxW hitboxH contactCooldown speed knockbackResistance hitstunSeconds aggroRange killVariable killAmount attackCount` (knockbackResistance..killAmount present only in v12+; `killVariable` is `-` when empty)  
`enemy_attack` fields: `type damage range cooldown projectileSpeed animState ammoSpriteId` (type: 0=Contact, 1=Melee, 2=Ranged)

`weapon_def` line fields: `id type damage range cooldown projSpeed spriteId ammoTypeId [ammoSpriteId(v9+)] ammoPerShot [wallBehavior(v13+)] [chargeTime chargeScaleMin chargeScaleMax overchargeTime overchargeEffect spreadStart spreadEnd steadyTime pellets falloffStart falloffEnd falloffMinScale aimCone (v14+)] [attackAnim(v15+)]` (wallBehavior: 0=Break, 1=Rebound; overchargeEffect: 0=None, 1=WildShot, 2=Misfire, 3=Break; attackAnim: player animation action played on attack, `-` = default `attack_1` melee / `cast` ranged)

Ranged feel stats (`WeaponDef`, ADGAME v14+) let one stat block express different weapon archetypes:

- **Hold-to-draw:** `chargeTimeSeconds > 0` or `steadyTimeSeconds > 0` makes the ranged button a hold/release control. Damage scales from `chargeDamageScaleMin` (tap) to `chargeDamageScaleMax` (full draw) over `chargeTimeSeconds`; shot spread interpolates from `spreadStartDegrees` to `spreadEndDegrees` over `steadyTimeSeconds`. Holding past full draw + `overchargeTimeSeconds` triggers `overchargeEffect`: WildShot (spread keeps growing), Misfire (release fizzles the shot, ammo spent), or Break (the weapon is destroyed and unequipped).
- **Pellets + falloff:** `pelletCount` projectiles per shot, each with an independent spread roll (ammo cost is per shot); impact damage scales from full at `falloffStartPx` down to `falloffMinDamageScale` at `falloffEndPx` (`falloffStartPx <= 0` disables falloff).
- **Auto-aim:** living enemies within weapon range and inside `aimConeDegrees` (full angle, centred on player facing) are selectable targets; `<= 0` disables auto-aim.
- Archetype examples — slingshot: charge 1.2 s, damage ×0.5→×2.0, overhold WildShot or Break, Rebound walls; shotgun: instant, 6 pellets, ~25° spread, falloff 48→160 px to ×0.2; sniper: steady 1.5 s, spread 18°→0°, narrow cone, no falloff.

Version history: v1 (id/playable), v2 (chapters + enemy_types), v3 (weapon_defs + starting_weapon), v4 (state_defs + effect_defs), v5 (npc_types), v6 (npc dialogue lines), v7 (font), v8 (enemy attack defs), v9 (weapon projectile sprites/ammo-per-shot), v10 (item_defs), v11 (NPC shop inventory), v12 (enemy knockback resistance, hitstun, aggro range, kill-counter variable/amount), v13 (weapon projectile wall behavior), v14 (ranged feel stats), v15 (per-weapon attack animation), v16 (scoped variables/effects, event hooks, NPC state rules).

Editor behavior:

- `CharacterEditorPanel::saveForChapter` saves reusable character documents and writes `project.adgame`.
- `EnemyPathEditorPanel::saveProjectEnemyTypes` saves reusable enemy type definitions (including attack defs) into `project.adgame`.
- `NpcEditorPanel::saveProjectNpcTypes` saves reusable NPC type definitions into `project.adgame`.
- NPC type definitions can include shop inventory rows. Each row references a project item ID and stores buy price, sell price, stock count, and unlimited-stock flag.
- `WeaponEditorPanel` saves weapon definitions and the project starting weapon into `project.adgame`. For ranged weapons it exposes an "On wall hit" combo (Break / Rebound) that sets `WeaponDef::wallBehavior`, plus Draw/Charge, Accuracy, Damage falloff, and Auto-aim sections (with tooltips) covering the ADGAME v14 ranged feel stats. An "Attack animation" combo (`attack_1` through `attack_20`, `cast`, or default) sets `WeaponDef::attackAnimState`, the player sprite action the runtime plays when attacking with that weapon.
- The top-level `Items` tab saves project-wide item definitions into `project.adgame`. Item categories cover common RPG types plus a custom category string for user-defined item groups. The tab can add missing common defaults and warns on empty or duplicate item IDs.
- The Quest State tab in `EditorApp` saves state variable definitions and reusable effect definitions into `project.adgame`.
- Quest State uses a master-detail variable list. Variable-reference fields can open a type-filtered picker; Save and Return persists definitions, applies the selected ID/scope, and restores the prior editor screen.
- ADGAME v16 adds Universal/Chapter variable scope, scoped reusable effects, item-acquire and enemy-defeat effect hooks, NPC talk effects, and first-match conditional NPC rules for dialogue graph, movement, visibility, following, animation, and activation effects. ADDIALOGUE v2 adds scope to variable conditions/actions.
- All panels that load project data track `lastLoadedProjectRoot_` and reload when the active project folder changes, preventing stale data from a previously opened project.

Runtime behavior:

- `Engine::loadPlayableCharacter` first resolves `Chapter::playableCharacterId`.
- If the chapter has no playable id, it falls back to `project.adgame`.
- A legacy scan of `.adcharacter` files remains as a fallback.
- Weapon pickups call into `GameState::giveItem`; project item pickups also update the runtime inventory and `GameState`, so item ownership and quest state use the same runtime registry.

---

## Dialogue Graph System

Implemented in `src/game/dialogue_graph.*`, `src/editor/panels/dialogue_graph_editor_panel.*`, and runtime execution helpers in `src/game/engine.*`.

Dialogue graphs are chapter-specific assets stored under:

```text
assets/game/dialogue/<chapterId>/<graphId>.addialogue
```

Characters and reusable NPC type definitions are project-wide. A screen `NpcPlacement` stores a `graphOverride` ID that references one of the active chapter's graph files. `NpcTypeDef::defaultGraphId` can provide a reusable default reference, but authoring instance-specific dialogue is done from NPC placement with `Edit Instance Dialogue`.

### Data model

```cpp
enum class DialogueNodeType { Start, Dialogue, Choice, Condition, Action, End };
enum class DialogueConditionType { Always, IntCompare, BoolEquals, HasItem, HasMoney };
enum class DialogueActionType {
    SetInt, AddInt, SetBool,
    GiveItem, TakeItem,
    GiveMoney, TakeMoney,
    HealPlayer, DamagePlayer,
    MoveNpc, HideNpc, ShowNpc,
    FollowPlayer, StopFollowingPlayer,
    SetNpcAnimation,
    StartQuest, CompleteQuest
};

struct DialogueGraph {
    std::string id;
    std::string startNodeId;
    std::vector<DialogueNode> nodes;
};
```

### Editor behavior

- `DialogueGraphEditorPanel` is opened as a scoped NPC-placement sub-screen, not a top-level tab.
- If a selected NPC instance has no `graphOverride`, `Edit Instance Dialogue` creates one from `<screenId>_<npcId>_dialogue`.
- Canvas nodes are draggable and linked with colored arrows.
- The left panel lists graph files for the active chapter and also lists nodes for navigation.
- Clicking a node on the canvas or in the node navigator selects it, scrolls the canvas toward it, and resets the inspector to the node editor.
- The inspector edits node type, speaker/text, target nodes, choice rows, conditions, and action rows.
- Target fields use node pickers instead of raw text entry.
- `Validate` reports duplicate/missing IDs, broken links, unreachable nodes, empty choice sets, and missing state/item definitions.
- `Simulate` walks the graph using project default state and first available choices, logging dialogue and action flow.

### Runtime behavior

- `Engine::loadNpcEntities` loads a graph from `assets/game/dialogue/<chapterId>/<graphId>.addialogue`, falling back to the old project-level dialogue path for compatibility.
- If an NPC has a graph, interaction starts at the graph's start node. Otherwise the runtime uses the legacy `DialogueLine` list.
- Up/Down or W/S selects player responses. E confirms a choice or advances graph dialogue.
- Conditions evaluate against `GameState` and money stored as integer variable `Money`.
- Action nodes mutate `GameState`, player health, and runtime NPC state.
- Dialogue text interpolation uses `$Variable_Id`. `interpolateGameStateText` resolves a matching
  `chapter.<chapterId>.<Variable_Id>` integer/boolean first, then the universal ID. Unknown placeholders
  remain unchanged, IDs are case-sensitive, `${Variable.With-Dots}` supports IDs containing dots/hyphens,
  and `$$` emits a literal dollar sign. The runtime applies
  interpolation to graph dialogue/speakers/choices, legacy dialogue, and timed NPC/enemy path speech.

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
    PathWaypointAction action = PathWaypointAction::None;
    float speechDurationSeconds = 2.0f;
    std::string speechText;
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
    PathCurveMode curveMode = PathCurveMode::Linear;
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

### `.adchapter` format (v12)

```text
ADCHAPTER 12
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
enemy enemy_1 crow_1 1 1 64.0 0 0 0 3
wp -16.0 80.0 0.0 0.0 -1 - 1 0.0 ""
wp 200.0 80.0 0.0 0.0 -1 idle 2 2.5 "You cannot escape!"
wp 784.0 80.0 0.0 0.0 -1 walk 3 0.0 ""
npcs 1
npc npc_1 shopkeeper_1 120.0 200.0 2 64.0 24.0 1 1 1 32.0 shop_graph 1 1 1
wp 120.0 200.0 0.0 2.0 -1 - 2 2.0 "Welcome!"
dl "Merchant" "Hello there traveler!"
shop_item potion 10 5 3 0
animtiles 1
animtile water_tile 11 14 0
end
```

Older files remain loadable. v1 files (no `respawn` per screen) load with `respawnEnemies = false`. v2 files load without character imports. v3 adds character imports, playable character id, per-screen enemy placements, and per-screen NPC placements. v10 adds per-NPC shop stock overrides. v11 adds a per-screen `animtiles` block. v12 adds NPC curve mode and extends waypoint rows with `action speechDuration "speechText"` (`0=None`, `1=Enter`, `2=Speak`, `3=Leave`).

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
    std::vector<MapDoorPlacement> doors;
};
```

### `.admap` format (v10)

```text
ADMAP 10
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
obstacle obstacle_1 0 spikes 5 5 2 1 1.0 0.0 0.0 1 0.75
obstacle obstacle_2 2 timed_spikes 8 5 2 1 1.0 1.0 0.5 2 0.50
items 1
item item_1 1 ammo_stone 5 384.0 256.0 0 ammo_pickup
doors 1
door door_1 10 12 1 2 2 brass_key 1 screen_2 4 8 door_sprite open assets/game/sfx/doors/open.wav assets/game/sfx/doors/close.wav assets/game/sfx/doors/locked.wav
end
```

Obstacle fields: `id type spriteId x y width height activeSeconds inactiveSeconds phaseSeconds damage damageIntervalSeconds`, where type is `0=Spike`, `1=Pit`, `2=TimedSpike`. `id` is present only in v7+ (v1–v6 obstacles load with a generated `obstacle_<n>` id); `damage`/`damageIntervalSeconds` are present only in v8+ (older obstacles default to `1` damage every `0.75` s).
Item fields: `id pickupType targetId quantity x y respawn spriteId`, where pickup type is `0=Weapon`, `1=Ammo`, `2=Health`, `3=ProjectItem`.
Non-respawning pickups set `item_collected.<chapter>.<screen>.<item>` in boolean game state and remain absent on later screen loads. Respawning pickups return when the screen is loaded again.
Door fields: `id x y widthTiles heightTiles lockMode requiredItemId consumeKey targetScreenId targetTileX targetTileY spriteId openingAnimation openSoundPath closeSoundPath lockedSoundPath`, where lock mode is `0=FreeUse`, `1=Locked`, `2=RequiresItem`.
Backward compat: v1–v9 files still load; missing sections and door SFX default to empty, pre-v7 obstacles get a generated id, and pre-v8 obstacles default to `damage=1`, `damageIntervalSeconds=0.75`.

### Map Editor

- Layer selector: Floor / Mid / Ceiling radio buttons.
- Copy/paste, tileset palette, obstacle edit mode, save/load.
- Supports multiple obstacle types and multiple instances per screen.
- **Per-obstacle editing:** every obstacle carries its own `id`. Obstacle edit mode shows a list of all obstacles (id + type) and an inspector for the selected one that edits its id, type, sprite id (with an Edit Sprite shortcut), tile position, size, **damage** (HP per tick; `0` = harmless), **damage rate** (seconds between damage ticks), and (for Timed Spikes) active/safe/phase timing. An **Add obstacle** button creates one at the spawn tile; clicking an obstacle on the canvas selects it; clicking an empty cell adds a new obstacle (auto-assigned `obstacle_<n>` id) of the chosen "New type" and selects it; right-click deletes the topmost obstacle under the cursor.
- **Shared `.admap` slices:** a screen's `.admap` stores tile layers **and** obstacles, items, and doors, but each is authored in a different panel. Any panel that writes the whole `TileMap` first re-reads the file and preserves the slices it does not own — `MapEditorPanel::saveMap` keeps existing items/doors; `LayoutEditorPanel::saveDirtyMaps` re-reads obstacles/items/doors before writing its tile-layer cache; the item/door panels load-then-save. This prevents one editor from wiping another's data (previously obstacles were always saved as `obstacles 0`).

### Item Placement Editor

Implemented in `src/editor/panels/item_placement_panel.*`.

- Opened from the selected screen inspector via `Edit Items`.
- Places weapon, ammo, health, and project item pickups into the selected screen's `.admap` items section.
- Project item pickups reference project-wide `ItemDef` assets by `targetId`, copying the item sprite ID and default value into the placement.
- Placement defaults and selected-item properties expose `Respawn when screen reloads`. Non-respawning pickups are one-time per playthrough; respawning pickups return when the screen is loaded again.
- Empty or duplicate placement IDs are warned because persistence is keyed by chapter, screen, and placement ID.
- Left column: item list and selected-item properties. Right: top-aligned canvas.
- Canvas shows floor/wall graphics, wall guide, tile grid, and diamond item markers.

### Door Placement Editor

Implemented in `src/editor/panels/door_placement_panel.*`.

- Opened from the selected screen inspector via `Edit Doors`.
- Places per-screen door trigger rectangles into the selected screen's `.admap` doors section.
- Door position and size are stored in tile units (`x`, `y`, `widthTiles`, `heightTiles`).
- Door lock data supports free-use, locked, and specific-key-required doors, plus optional key consumption.
- The inspector places full labels above narrow text fields. `Target screen ID` is directly below lock mode for Free Use/Locked doors; Requires Item additionally shows `Required item ID` and `Consume Key`.
- Door destination data stores target screen ID and target tile X/Y.
- Permanently Locked doors may omit a target screen when they are used only as same-screen barriers.
- Requires Item doors may also omit a target screen. On successful key use, runtime stores `door_unlocked.<chapter>.<screen>.<door>` in boolean game state and treats the door as Free Use for collision. An unanimated unlocked door stores a matching hidden state once the player enters its rectangle, so its sprite stays removed across screen changes and continued saves.
- Door sprite ID is rendered at runtime when a matching sprite exists. Opening animation IDs are stored for future animation playback.
- Open, Close, and Locked SFX use dropdowns populated recursively from project-relative `.ogg` or `.wav` files under `assets/game/sfx/doors`. New and opened projects ensure this folder exists.
- The inspector warns about duplicate/empty door IDs, missing required-item IDs, required items not defined in the Items tab, invalid/missing Free Use destinations, unloadable target maps, out-of-bounds target tiles, and target tiles blocked by the target map wall layer.

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
    std::string graphId;
    DialogueGraph graph;
    bool hasGraph;
    bool hidden;
    bool followingPlayer;
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
- **NPCs:** `updateNpcs` supports linear and spline paths and updates `npc.facingX/Y` from movement direction.
- **Waypoint actions:** `applyEnemyWaypointAction` and `applyNpcWaypointAction` run at control points on both path types. Enter reveals path-hidden actors, Speak pauses and renders a timed bubble, and Leave hides the actor and finishes the path.
- Initial value for both is `(1.0, 0.0)` (facing East) until the entity first moves.

### `updateEnemyCombat`

Runs each frame for all path entities. Advances `entity.animSeconds`. Ticks `entity.attackCooldowns` (one float per `combat.attacks` entry). For each `EnemyAttackDef`:
- **Contact:** handled by the legacy `contactDamage` path (overlap check).
- **Melee:** fires when player is within `atk.range` and cooldown is zero; deals `atk.damage` and resets the per-attack cooldown.
- **Ranged:** fires when player is within `atk.range`; spawns a `RuntimeProjectile` headed toward the player at `atk.projectileSpeed`.
- If `atk.animState` is set and the attack fires, transitions `entity.animState` and resets `entity.animSeconds`.
- Enemies in hitstun (`hitstunSeconds > 0`) skip all attacks until the stagger expires.

### Hit reactions, hitstun, knockback, and aggro

- **Active-frames melee:** `updateAttack` carves `kMeleeAttackSeconds` into windup → active → recovery (`kMeleeWindupSeconds`, `kMeleeActiveWindowSeconds`). `checkMeleeHits` runs every frame inside the active window and dedupes via `meleeHitEnemies_` so each enemy is hit once per swing.
- **`applyEnemyHit(entity, damage, dirX, dirY)`** is the single hit-reaction path for melee and projectiles: subtracts HP, sets `hitFlashSeconds` (white render overlay) and `hitstunSeconds` (from the type), sets a decaying `knockbackVx/Vy = dir * kEnemyKnockbackBasePxPerSecond * (1 - knockbackResistance)`, and switches `animState` to `"hurt"` (or `"dead"` on a lethal hit).
- **Knockback decay** is exponential and collision-checked in `updatePaths` (enemies) and `updatePlayer` (player); applied per-axis so a wall stops only the blocked component.
- **Projectiles (`updateProjectiles`)** carry a `fromEnemy` team flag: player shots damage enemies, enemy shots damage the player (both apply knockback in the projectile's travel direction). Wall impact follows the projectile's `ProjectileWallBehavior`: `Break` despawns on contact (arrow, bullet); `Rebound` reflects per-axis with `kProjectileReboundRestitution`, counts down `bouncesRemaining`, and once spent (or below `kProjectileMinReboundSpeed`, or past max range) "settles" — `settleSeconds` runs an inert grounded rest/fade before despawn (slingshot stone). Player weapon projectiles take their behavior from `WeaponDef::wallBehavior`; enemy projectiles default to `Break`.
- **Player knockback:** `damagePlayer(amount, sourceX, sourceY)` pushes the player away from the damage source; the no-source overload `damagePlayer(amount)` deals damage without knockback (hazards, scripted).
- **Aggro/chase:** when `aggroRange > 0` and the player is within it, `updatePaths` steers the enemy directly toward the player at `path.speed` (overriding waypoints), tracked by `entity.aggroActive`, releasing once the player passes `aggroRange * 1.3` (hysteresis). On release the enemy enters a `returningToPath` phase: it walks from wherever it chased to toward the **nearest point on its path** (`nearestPathDistance`), and on arrival syncs `pathDistance`/`waypointIndex` (`waypointIndexForDistance`) and resumes patrol from there — no snapping back to a stale spline position.

### Other runtime behavior

- Initializes GLFW/OpenGL window and runs fixed 60 Hz update loop.
- Direct launch opens project/chapter picker; explicit chapter path skips it.
- Loads `.admap`, resolves playable character, loads per-screen enemy and NPC placements from `ChapterScreen`.
- Collision against nonzero cells in `.admap` layer 1.
- Screen-boundary crossings with 30% threshold trigger sliding transition to linked screen.
- Obstacle hazards (spikes, pits, timed spikes) deal each obstacle's configured `damage` to the player while overlapping, repeating no faster than that obstacle's `damageIntervalSeconds` (tracked per-obstacle in `hazardCooldowns_`, keyed by id, and cleared on screen load). Damage routes through `damagePlayer`, so the player respawns at the map spawn only when HP reaches 0. Obstacles with `damage <= 0` are harmless.
- Runtime input goes through `Engine::inputDown`, combining keyboard and GLFW gamepad state. Controller mapping is D-pad/left stick for movement and menus, south face button for interact/confirm/use, west face button for melee, east face button for ranged (hold to draw on charge weapons), bumpers (LB/RB) for aim-target cycling, and north face button/Select/Start for inventory.
- Melee attack (Z / west face button): hitbox sweep in facing direction, brief yellow flash. Ranged (X / east face button): projectile(s) from `WeaponDef` data. Attacks play the weapon's `attackAnimState` sprite action when set (`playerAttackAnimOverride_` in `playerActionName`), else `attack_1` (melee) / `cast` (ranged). Instant weapons fire on press; hold-to-draw weapons (`chargeTimeSeconds`/`steadyTimeSeconds > 0`) charge while held — movement slows to the attack speed scale, a charge meter renders above the player (green → gold at full draw → flashing red when overheld) — and fire on release with charge-scaled damage and steady-scaled spread. Overholding applies the weapon's `OverchargeEffect` (WildShot spread growth, Misfire dud shot, or Break: weapon unequipped).
- **Cone auto-aim** (`updateAimTargets`): with a ranged weapon equipped, living enemies within `WeaponDef::range` and inside `aimConeDegrees` (default 45°) of player facing form a candidate list sorted left-to-right; the most central is auto-locked. Every candidate is drawn with a scope-style reticle centred on it (`renderAimTargets` + `renderArc`) — two segmented circles spinning in counter directions with cardinal crosshair ticks; pulsing yellow for the locked target, faint white for the rest. The reticle radius scales with current spread (wide when inaccurate, closing onto the enemy as aim steadies) and the spin rate slows as accuracy sharpens. Tab / Q (or RB / LB) cycle the lock next/prev. **Lock-on facing:** while drawing, the player turns to face the locked target and movement strafes; the lock holds as long as the target is alive and in range. Instant weapons snap player facing to the shot direction on fire. **Target lead:** shots at a locked target solve the ballistic intercept against the enemy's measured velocity (`RuntimePathEntity::velocityX/Y`, bracketed around `updatePaths`) and blend from current position toward the intercept as accuracy sharpens — full anticipation at 0° spread, none at ≥ `kLeadMaxSpreadDegrees`. Without a lock, shots fly straight ahead; each pellet rolls its own spread angle, and projectile impact damage applies the weapon's distance falloff.
- Item pickups equip weapons, add ammo, restore HP, or collect project item definitions. Project item pickups add to the inventory and write ownership to `GameState`; currency items also update the configured money variable. **Ammo lives in the inventory** (there is no separate ammo pool): a ranged weapon fires the matching `Ammo`-type item directly (resolved by `ammoItemIdForWeapon` from the weapon's `ammoTypeId` against item `id`/`targetId`), consumed via `consumeAmmoForWeapon`. The inventory overlay renders a dedicated AMMO section; ammo bought from a shop or picked up is immediately usable.
- Shop NPCs open a two-panel buy/sell overlay. The shop panel spends `Money` and transfers stock into the player inventory; the player panel sells one selected inventory item and adds `Money`. Limited shop rows decrement on buy and increment when sold back. NPC placements can override the reusable NPC type stock, and the runtime scrolls both shop and player inventory lists.
- Door triggers render as sprites or colored rectangles, show the interaction prompt when the player stands on or near their tile rectangle, and activate on E/controller south. Free-use doors remain walkable triggers and transition to the target screen/tile. Locked and required-item rectangles block player movement; interaction still works from beside the rectangle. A key door without a destination unlocks in place, becomes walkable, and stops prompting; without an opening animation it disappears when crossed. Destination transitions mix Open over screen music, then play Close after the slide. Permanently locked and missing-key interactions play Locked.
- Pressing `I`, controller north face button, Select, or Start toggles a simple inventory overlay and pauses player/world updates while it is open. Inventory rows use each item definition's sprite as a pictogram and render stack/value counts as a pictogram/number pair. Up/Down, W/S, D-pad, or left stick changes selection; E, Space, Enter, or controller south face button uses usable item types.
- NPC dialogue: E key or controller south face button triggers interaction. Legacy dialogue advances line-by-line; graph dialogue follows nodes, conditions, choices, and actions. Up/Down, W/S, D-pad, or left stick selects graph responses. Speech bubble / dialogue box renders above NPC and dialogue text is bounded, wrapped, and scrollable.
- **Persistence:** at launch the engine seeds `GameState` from project `StateVariableDef` defaults. **By default that is the whole story — every launch is a new game.** Saved progress in `assets/game/save.adstate` is merged over the defaults only when the runtime is started with `--continue` (gated by `continueSave_`, default false). State is still written on every screen transition and on quit (so a future continue works), and within a session defeated enemies persist in `GameState::defeatedEnemies_` keyed `"<screenId>/<enemyId>"`, filtered out at screen load. A kill increments the enemy type's `killVariable` by `killAmount` on every death (independent of respawn/persistence), wiring the quest counter through the shared registry.
- **Launch flags:** the runtime accepts `--continue` (load `save.adstate` to resume), `--fresh` (test launch: ignore *and* don't overwrite `save.adstate`), `--screen <id>` (start on a specific screen), and `--pos <x> <y>` (start at a position). On each screen entry it writes a lightweight `assets/game/test_checkpoint` (`screen <id>` / `pos <x> <y>`) recording where the player last entered a screen; the editor reads this for "Play From Last Entry". A `--fresh` run never writes `save.adstate` but still updates the checkpoint.
- **Animated tiles:** `Engine::loadAllSprites` also loads sprites referenced by the active screen's `animatedTiles`; `Engine::renderAnimatedTiles(layer)` draws each placement's current frame (via `spriteFrame()` + `renderTextureRegion()` at `cell*kTileSize`) — layer 0 after the floor texture (below the player), layer 1 after the wall texture (player walks behind).

Current limitations:

- Rendering uses fixed-pipeline OpenGL; a shader/core-profile renderer is planned.
- Collision is binary: nonzero mid-layer tile means solid.
- Rebounded/settled projectiles fade out rather than becoming re-collectible ammo (recovery is a possible extension).

---

## Sprite Editor

Implemented in `src/editor/panels/sprite_editor_panel.*`.

- Pixel editing with frames, layers, palette, preview, and animation playback.
- Color authoring is constrained to the shared 128-color Atari 2600 NTSC table in `src/editor/atari_2600_palette.hpp`, derived from Stella's standard NTSC palette. Existing document colors remain displayable; clicking a palette swatch opens the constrained replacement popup, and **Add color** opens the same selector.
- Tools: pen, mirror, bucket, eraser, stroke, line, rect, circle, polygon, move, select, picker, shade.
- **Frame metadata section** (right inspector): action type (combo: idle/walk/run/attack/etc.), facing direction (combo: any/E/W/N/S/NE/NW/SE/SW), and duration in ms. The direction system is shared by all sprites: player, enemy, NPC alike.
- **Polygon tool:** left-click adds vertices; double-click or Enter closes and rasterizes the outline with the current brush/color; right-click or Escape cancels. A live overlay previews committed edges and the closing edge.
- **Frame ordering:** the active thumbnail rail exposes Earlier/Later and drag-and-drop. `reorderFrame` moves the selected `SpriteFrame` and its complete per-layer cel vector together, follows the moved frame, remaps selection ownership, and records one undo state.
- **Resize:** all frames share one canvas size; the `Resize` dialog offers two modes. **Keep pixels (default)** changes only the canvas: existing art keeps its pixel size and stays centred — growing adds equal blank margins (room for sword-swing frames etc.), shrinking crops the edges evenly; `pivot`, `bodyGuide`, and frame rects shift by the margin offset. **Scale pixels** rescales every frame and layer with nearest-neighbour and proportionally rescales `pivot` and `bodyGuide`.
- **Authoring overlays (not rendered in game):** `Pivot` toggle (`showPivot_`) draws a crosshair at `document_.pivot`; `Body` toggle (`showBodyGuide_`) draws the user-defined `bodyGuide` rectangle (`x,y,w,h` canvas px; `= canvas` button fills the frame). Both persist in `.sprite.json`; the runtime never reads `bodyGuide` (`pivot` is also currently unused by rendering — frames draw centred on the entity position).
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
    std::array<int, 4> bodyGuide;  // [x,y,w,h] editor authoring guide; w/h<=0 = unset; ignored by runtime
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
    bool isEnemyDefeated(const std::string& key) const;   // key: "<screenId>/<enemyId>"
    void markEnemyDefeated(const std::string& key);
};

std::string interpolateGameStateText(
    const std::string& text,
    const GameState& state,
    const std::string& chapterId = {});
```

`.adstate` (current version **2**) stores runtime values: named ints, named bools, owned items, and a defeated-enemy key set. v1 files (no `defeated` block) still load. Variable names are designer-authored content, not hard-coded engine behavior. The runtime save file lives at `assets/game/save.adstate`.

`interpolateGameStateText` supports live state in authored text:

```text
You killed $Crows_Killed crows.
Quest complete: $Crow_Quest_Done
Reward: $$5
```

Integer values render as decimal numbers and booleans as `true`/`false`. Resolution prefers
the current chapter's scoped key before the universal key. Plain `$Variable_Id` placeholders
accept letters, digits, and underscores; braced placeholders support the full authored ID.

---

## Wall / Floor Paint

Implemented in `src/editor/panels/wall_floor_paint_panel.*`.

- Two-layer pixel painter (Floor + Wall). Canvas locked to `kScreenTilesW × kTileSize` × `kScreenTilesH × kTileSize` (768 × 512 px).
- The unrestricted RGBA picker is replaced by the shared Atari 2600 NTSC selector (`atari2600::drawNtscPaletteSelector`). It presents all 128 TIA colors plus a transparent swatch used by the layered paint workflow; tooltips show the TIA color code and RGB value.
- Tools: Pencil, Eraser, Fill, Line, Rect, Select, Tile Draw, Tile Select, Tile Paste, Stamp, Tile Fill, Tile Erase.
- Brush shapes: Square, Circle, Spray, Dither. Zoom 1–16.
- Tile palette: `Tile Select` → Add to palette → Stamp / Tile Fill across all screens.
- **Animated tiles (palette-driven):** each `TilePaletteEntry` carries an optional `spriteId`. An **Edit Sprite** button per palette row (`editTileSprite`) seeds a sprite from the tile's pixels (composite wall-over-floor → `PendingSpriteSeed` → `SpriteEditorPanel::createSpriteFromPixels`) and opens the Sprite editor; reopening an existing sprite skips the seed. A tile whose sprite has **≥2 frames** is *animated* (`tileIsAnimated`, cached in `spriteFrameCount_` via `refreshTileSpriteInfo`). Stamping a static tile bakes pixels (`stampTile`); stamping an animated tile calls `placeAnimatedTile`, which upserts a `game::AnimatedTilePlacement` into `context.selectedScreenAnimatedTiles` at the cell (active layer → 0 floor / 1 overlay, replacing any placement there, painted pixels left intact). `Tile Erase`/right-click → `removeAnimatedTileAt`. Placements sync to/from the chapter via `LayoutEditorPanel::applyContextSelectedScreenData` (re-synced on screen switch through `context.requestScreenPlacementSync`). The tile↔sprite link is persisted in `.adeditor` v3.
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
- **Hit Reaction & AI** (below Contact Damage): Knockback resist (0–1), Hitstun (s), Aggro range (px; 0 = waypoints only), and a kill-counter variable name + amount. These write the ADGAME v12 `EnemyType` fields.
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
- Fields: id, project character picker, sprite ID override, default movement mode, default interaction mode, default speed, graph ID, default dialogue lines.
- Characters are project-wide assets from `project.adgame` / `assets/game/characters/*.adcharacter`; NPC types reference character IDs rather than storing character data.
- `saveProjectNpcTypes` writes updated types back to `project.adgame`.

### NPC Placement Editor (`Edit NPCs` subpanel)

Canvas modes: **Place NPCs** (create/select/delete NPC anchors at world positions) and **Edit Path** (add/move/delete patrol waypoints).

Inspector fields:
- Instance ID, type ID
- Position (x, y), facing direction (N/S/E/W)
- Awareness radius, interaction radius
- Movement override (Stationary / Patrol / Wander), linear/spline path, loop, speed override
- Per-waypoint: unrestricted pixel position (including off-screen), speed override, wait seconds, facing direction, animation state, Enter/Speak/Leave action, and timed speech text
- Graph override ID for chapter-specific instance dialogue
- `Edit Instance Dialogue`, which opens `DialogueGraphEditorPanel` for the selected NPC placement and stores graphs under `assets/game/dialogue/<chapterId>/`
- Dialogue lines (inline override, one per entry)

Canvas shows selected screen's map, floor/wall graphics, tile grid, NPC markers, and patrol path.

Runtime behavior:

- `updateNpcs` advances NPCs along linear or spline patrols, stopping at control points to apply waits and actions.
- `npc.facingX/Y` updates with the movement direction so `spriteFrameForNpc` can apply directional frame selection and horizontal mirroring.
- `updateNpcAwareness` checks distance to player and sets `npc.playerInAwareness`; when true, the NPC stops patrolling.
- `updateInteraction` (E key): `None → PromptVisible → InDialogue`. Legacy dialogue advances with E; graph dialogue advances through nodes and uses Up/Down or W/S for choice selection. Leaving interaction radius resets state.
- `renderNpcs` calls `spriteFrameForNpc(sprite, npc, flipH)` and swaps u0/u1 when `flipH` is true.
- `renderInteractionPrompt` shows a pulsing green square above the NPC when in prompt range.
- `renderSpeechBubble` / `renderDialogueBox` shows wrapped dialogue text above the NPC. Dialogue boxes have fixed bounds and support scrolling long text.

---

## Asset Directories

`AssetDirectories` centralises editor asset paths. All paths are relative to `projectRoot`. Its `requiredPaths()` manifest and `ensureRequiredPaths()` create the complete generic `assets/raw` and `assets/game` tree, including `assets/game/sfx/doors`, whenever a project is created or opened. Chapter creation also creates `assets/game/dialogue/<chapter-id>`.

| Field | Default |
|-------|---------|
| `rawSprites` | `assets/raw/sprites` |
| `rawCharacterSprites` | `assets/raw/character_sprites` |
| `rawTilesets` | `assets/raw/tilesets` |
| `gameSprites` | `assets/game/sprites` |
| `gameCharacterSprites` | `assets/game/character_sprites` |
| `gameCharacters` | `assets/game/characters` |
| `gameChapters` | `assets/game/chapters` |
| `gameDialogue` | `assets/game/dialogue` |
| `gameMaps` | `assets/game/maps` |
| `gameTilesets` | `assets/game/tilesets` |
| `gameAnimations` | `assets/game/animations` |
| `gamePalettes` | `assets/game/palettes` |
| `gamePaths` | `assets/game/paths` |
| `gameFonts` | `assets/game/fonts` |
| `gameMusic` | `assets/game/music` |
| `gameSfx` | `assets/game/sfx` |
| Door SFX subfolder | `assets/game/sfx/doors` |

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
| §4.4 Enemy waypoint/spline paths | ✅ `.adpath` v5 (legacy) + `EnemyPlacement` in ADCHAPTER v12, including waypoint actions |
| §4.4 Enemy behavior states (idle/patrol/aggro) | ✅ In path data; runtime moves non-idle paths |
| §4.4 Enemy sprite direction-aware rendering + flip | ✅ `spriteFrameForEntity` with `bool& flipH`; `facingX/Y` from movement direction |
| §4.4 Enemy combat — per-attack Melee/Ranged | ✅ `updateEnemyCombat` fires attacks with per-attack timers |
| §4.5.1 Game state registry | ✅ Generic `GameState`, `.adstate`, `.adgame` v4 state/effect defs, Quest State tab |
| §4.5.2 NPC type definitions (project library) | ✅ `NpcTypeDef` in `project.adgame` v5+ |
| §4.5.2 NPC placements in chapter screens | ✅ `NpcPlacement` in `ChapterScreen` |
| §4.5.2 Shop inventory and buy/sell loop | ✅ Type-level shop inventory, per-placement stock overrides, quantity buying, money transfer, inventory updates |
| §4.5.3 NPC spline movement + waypoint actions | ✅ Linear/spline patrols with wait, Enter, timed Speak, and Leave |
| §4.5.3 NPC direction-aware rendering + flip | ✅ `spriteFrameForNpc` with `bool& flipH`; `facingX/Y` from movement direction |
| §4.5.3 NPC player awareness | ✅ Awareness radius check; NPC idles when player is near |
| §4.5.4 NPC interaction + sequential/graph dialogue | ✅ E key, prompt, speech bubble / dialogue box, multi-line advance, graph execution, choices, conditions, actions |
| §4.5.5 Flow graph editor | ✅ Scoped NPC placement sub-screen with node canvas, arrows, inspector, validation, simulation, and node navigation |
| §5 Save/load (JSON, text formats) | ✅ |
| §5 Item/ammo/currency pickups | ✅ Project item pickups write inventory + `GameState`; ammo lives in inventory; currency updates configured money variable |
| §5 Door placements | ✅ `.admap` v6 save/load, editor support, validation, runtime prompt/transition/key handling |
| §6 Runtime game engine (rendering, screen-flip) | ✅ Basic GLFW/OpenGL runtime shell |
| §6 Runtime collision | ✅ Tile collision against `.admap` mid layer |

## Near-Term Priorities

1. ✅ Enemy defeat persistence: defeats stored in `GameState`/`save.adstate`, respawn flags respected at screen load, kill counters wired to the shared registry.
2. ✅ Action-RPG hit feel: active-frames melee window, knockback (player + enemy), hitstun, hit flash, enemy `hurt`/`dead` state transitions, and basic aggro/chase. Player `attack_1`/`cast`/`hit_react`/`death` action states drive frame selection (animation content authored per character).
3. ✅ Projectile combat: friend/foe targeting (enemy projectiles damage the player), and per-weapon wall behavior (break vs rebound-and-settle).
4. Replace runtime fixed-pipeline OpenGL with a shader/core-profile renderer.
5. Extend collision beyond binary mid-layer solid tiles only when design needs require transparent/interaction flags.
6. Optional: make rebounded/settled projectiles re-collectible as ammo.

## Engineering Notes

- Keep editor UI state out of `src/game`.
- File format parsing belongs in runtime-facing modules when the game needs to load that format. Current shared parsers: chapter, dialogue graph, map, path, project, sprite metadata, state, tileset.
- Prefer readable text formats while the project is small.
- All editor panels that load project-level data must store `lastLoadedProjectRoot_` and compare it against `context.assets.projectRoot.string()` on each draw call, reloading if it differs. This prevents stale data appearing when the user switches projects.
- The sprite editor undo stack snapshots whole state. Command-based undo should replace it once documents become large.
- Both `WallFloorPaintPanel` and `SpriteEditorPanel` follow the same dirty-buffer pattern: in-memory maps keyed by asset ID hold unsaved state across the session; chapter save flushes dirty entries; chapter switch calls `reset*Buffers()` to clear stale data.
- Direction-aware sprite rendering is implemented in three parallel functions (`playerSpriteFrame`, `spriteFrameForEntity`, `spriteFrameForNpc`) that share the same frame-selection and W↔E mirroring logic. Any changes to the direction/mirror algorithm should be applied to all three.
