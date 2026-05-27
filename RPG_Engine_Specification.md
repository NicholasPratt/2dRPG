# Project Specification: Pixel-Art Action RPG Engine & Editor (C++)

## 1. Project Overview
The goal is to develop a 2D Action RPG engine and integrated development environment (IDE) in the style of classic SNES/GBA titles. The project emphasizes a "hand-drawn" pixel art aesthetic and a robust, built-in editor that allows for real-time asset creation and level design. [cite: 1]

## 2. Technical Stack
- **Language:** C++ [cite: 1]
- **Target Style:** 16-bit / 32-bit hand-drawn pixel art. [cite: 1]
- **Perspective:** Top-down / Isometric hybrid with pseudo-3D parallax effects. [cite: 1]

## 3. Core Engine Architecture

### 3.1. World Structure
- **Chapters:** The game is organized into high-level Chapters. [cite: 1]
- **Screens:** Each chapter consists of discrete "Screens." [cite: 1]
- **Scrolling:** Movement between screens uses a "Screen-Flip" transition, loading one screen at a time. [cite: 1]
- **Pseudo-3D / Parallax:** The engine supports multiple layers, specifically for floor and overhead parallax depth. [cite: 1]
- **Game Library vs Chapter Usage:** Reusable assets live in a project-level game library. Chapters import and reference those assets instead of owning private copies.

### 3.2. Display & Tile Dimensions
- **Tile Size:** 16 × 16 pixels (`kTileSize = 16`). All sprites and tileset tiles are fixed to this size.
- **Screen Size:** 48 × 32 tiles (`kScreenTilesW = 48`, `kScreenTilesH = 32`), giving a native resolution of 768 × 512 pixels per screen.
- **Pixel Scale:** The screen is larger in tile count for modern displays, but the tile size and pixel-art unit size remain unchanged at 16 px.
- These values are defined in `src/game/constants.hpp` and are the single source of truth for all editors and engine code.

### 3.3. Combat & Persistence
- **Action-Oriented:** Real-time, timed combat mechanics. [cite: 1]
- **Enemy Persistence:** Defeated enemies do not respawn by default unless a "respawn" flag is set. [cite: 1]
- **Enemy Attack Types:** Each enemy type can define multiple attacks, each with an independently configured attack type: Contact (overlap-based), Melee (range-triggered swing), or Ranged (fires a projectile toward the player). Each attack has its own damage, range, cooldown, projectile speed, ammo sprite ID, and optional animation state.
- **Player Attacks:** Melee (Z key) sweeps a hitbox in the facing direction with a brief flash. Ranged (X key) fires a projectile using `WeaponDef` data. Both respect per-weapon cooldowns.

### 3.4. Runtime Shell
- **Executable:** `adventure_game_window` is the current runtime executable.
- **Timing:** The runtime uses a fixed 60 Hz update loop for gameplay simulation.
- **Rendering:** The runtime renders pre-baked per-screen floor/wall PNGs and keeps `.admap` data for collision and structure.
- **Screen Transitions:** Crossing a screen edge follows the current `ChapterScreen` north/south/east/west link and performs a sliding transition. The transition triggers when 30% of the player sprite has crossed the edge, only if the destination screen exists and the destination entry point is not blocked.
- **Collision:** The current runtime collision model treats any nonzero `.admap` mid-layer cell as solid.
- **Playable Character:** The runtime resolves the playable character through the active chapter, then the project library, then a legacy fallback scan of character files. The selected character's `sprite` field references a `.sprite.json` file; the engine loads that sprite metadata and its exported sheet PNG. The player is rendered using the direction-aware sprite animation system (see §4.3 and §4.4).
- **Obstacles:** `.admap` files can store spike, pit, and timed-spike obstacle rectangles. Obstacles may reference sprite-editor sprites by ID and active hazards respawn the player at the map spawn.

## 4. The Integrated Editor (Critical Component)
The editor is **state-dependent** and context-aware, tracking exactly what the user is editing at all times.

### 4.1. Project & Chapter Management
`EditorApp` coordinates the high-level chapter lifecycle and delegates file I/O to runtime-facing game modules and editor panels.
- **Startup Flow:** Upon opening, the editor must prompt the user for a project and chapter. Existing projects are loaded from `projects/<project>/`; new projects create their own project folder with `assets/game/...` and `assets/raw/...` subfolders.
- **Project Isolation:** Every project owns its own assets, chapters, maps, sprites, characters, screen graphics, enemy types, NPC types, and project manifest. Editor and runtime asset lookup must use the selected project folder, not stale repo-root assets. All editor panels that load assets track `lastLoadedProjectRoot_` and reload when the project changes.
- **Session Safety:** Exiting the editor or switching chapters must trigger a prompt to save or discard changes. [cite: 1]
- **Exporting:** Saving a chapter must automatically generate/update PNG sprite sheets for all edited sprites, save dirty screen maps, and export wall/floor paint graphics into game-ready PNGs for every modified screen. [cite: 1]
- **Dirty-Buffer System:** Both the sprite editor and wall/floor paint panel hold unsaved edits in per-asset in-memory buffers (keyed by asset ID) during the session. Disk writes happen only when the chapter is saved. Switching chapters clears all buffers so data from one chapter cannot bleed into another.
- **Game Library Save:** Character sheets, enemy types, NPC type definitions, weapon definitions, state variable definitions, and reusable effect definitions are saved as reusable game-library assets. `assets/game/project.adgame` records available character IDs, chapter IDs, enemy type definitions, NPC type definitions, weapon definitions, project state/effect definitions, and the project default playable character. `.adchapter` files record imported character IDs and chapter playable character ID.
- **Character Management:** The Characters tab exposes Add Character and Delete Character controls in the main character workflow. Deleting a character removes it from the in-memory project list immediately and removes the `.adcharacter` document on save; at least one character is always retained.
- **Save and Play:** `Chapter > Save and Play Game` and scoped editor `Save and Play` controls save active editor data, then launch the separate `adventure_game_window` runtime executable with the selected chapter path. Runtime asset lookup is derived from that chapter's project folder. Escape closes the game window.
- **Direct Runtime Launch:** Opening `adventure_game_window` directly scans `projects/<project>/assets/game/chapters/*.adchapter` plus repository fallback assets and asks the user which project/chapter to load.
- **State Registry:** Maintains a registry of defeated enemies and handles the "Dirty Flag" system for modified screens.

### 4.2. Layout & Map Editor (ScreenEditor)
- **Continuous Screen Grid:** The Screens tab displays the chapter as a continuous grid. The currently selected screen is centered and highlighted. [cite: 1]
- **Adjacent Context:** Adjacent and nearby screens show their wall/mid-layer layout to ensure spatial continuity. [cite: 1]
- **Connected Screens:** North/south/east/west controls manage reciprocal screen links. [cite: 1]
- **Screen Graphics Mode:** Clicking "Edit Screen Graphics" transitions the user directly into Wall/Floor Paint for that specific screen. [cite: 1]
- **Context-Aware Graphics:** Wall/Floor Paint is accessed from the Screens tab as a context-specific subview. It includes a Back to Screens button and screen selector so graphics edits stay tied to chapter screens.
- **Graphics Preview:** The Screens tab can display scaled-down previews from each screen's exported `<mapId>_preview.png` behind the structural tile/wall overlay.
- **Screens Page Layout:** Main Screens page controls must be arranged in rows that reserve enough space for labels and controls. Toolbar/help text should wrap rather than overlap, and canvas-only overlays must not be drawn over inspector labels.
- **Screens Page Performance:** Screen preview PNG metadata checks are throttled and off-canvas screens are skipped during rendering so large chapter layouts remain responsive.
- **Map Logic Mode:** From Screen Graphics, the editor can switch to map logic editing for the same screen. This exposes tile layers, spawn placement, obstacles, and hazard sprite references without leaving screen context.

### 4.3. Graphic & Sprite Editing
- **Pixel Painting:** Integrated tools comparable to MS Paint or Piskel for tiles and sprites. [cite: 1]
- **Screen Graphics Workspace:** The screen graphics editor may use the full available editor workspace; it is not confined to a half-height preview area.
- **Wall/Floor Paint Layers:**
    - **Floor Layer:** Base walkable texture (`<mapId>_floor.png`).
    - **Wall/Object Layer:** Rendered over actors and guided by the structural `.admap` mid layer.
    - **Ceiling Layer:** Represented structurally in `.admap`; a dedicated runtime paint/export layer is planned.
- **Object Transparency:** Planned collision flags should distinguish solid, visual-overlap, and interaction cells. The current runtime treats nonzero `.admap` mid-layer cells as solid.
- **Visual Aids:** The Wall/Floor Paint view must show the selected screen's wall structure as a toggleable highlight guide. [cite: 1]
- **Dynamic Character Editing:** Clicking "Edit Sprite" while a character is selected opens their specific sprite sheet for modification. [cite: 1]
- **Return-to-Character Workflow:** Sprite editing launched from a character shows a Return to Character action. Returning saves/exports the sprite and refreshes the character's per-frame assignments.
- **Playable Character Selection:** Character sheets include a Playable Character checkbox. Only one character is playable at a time.
- **Frame Assignment:** In the sprite editor, each frame has an action type (e.g. `idle`, `walk`, `attack_1`), a facing direction (`E`, `W`, `N`, `S`, `NE`, `NW`, `SE`, `SW`, or empty for any direction), and a duration in ms. Multiple frames may share the same action + direction to form a looped animation clip. The runtime cycles through matching frames and auto-mirrors: if no west-facing frames exist, east-facing frames are flipped horizontally. The same mirroring applies for NW↔NE and SW↔SE. This direction system applies to the player, enemies, and NPCs alike.
- **Per-Screen Graphics Isolation:** Each screen has its own set of floor/wall/preview PNGs (named `<mapId>_floor.png` etc.). Edits are held in per-screen in-memory buffers during the session and written to disk only on chapter save.
- **Tile-Based Screen Graphics Tools:** The screen graphics editor includes `Tile Draw`, `Tile Select`, `Tile Paste`, `Stamp`, `Tile Fill`, and `Tile Erase`. `Tile Draw` fills a full 16×16 tile cell with the selected color and supports drag painting. `Tile Select` selects exactly one tile cell for copy/paste or palette capture. `Stamp` places the selected palette tile and supports drag painting across tile cells. `Tile Fill` flood-fills contiguous matching tiles with copies of the selected palette tile. `Tile Erase` clears full tile cells.
- **Tile Palette System:** Tile selections can be added to a chapter-wide tile palette. Palette entries store floor and wall pixel data for the selected tile. Tile palette entries are static in the screen graphics editor; animation is authored through the Sprite editor rather than an in-panel animation preview.
- **Per-Sprite Isolation:** The sprite editor holds each open sprite document in its own in-memory buffer. Switching sprites stashes the current document; returning to a sprite restores it from memory. All dirty sprite documents are flushed to disk (metadata JSON + sprite-sheet PNG) on chapter save.
- **Character Sprite Thumbnail Performance:** Character frame thumbnails and sprite frame assignments are cached while editing. The character panel must not reload sprite metadata or decode PNG thumbnails every frame.

### 4.4. Enemy AI, Pathing & Attack System
- **Enemy Types (Project Library):** Reusable enemy type definitions live in `project.adgame`. Each `EnemyType` records: id, sprite ID, max health, contact damage, hitbox size, contact-hit cooldown, movement speed, and a list of `EnemyAttackDef` entries.
- **Enemy Attacks:** Each `EnemyAttackDef` specifies:
  - `type`: Contact (legacy overlap damage), Melee (sweep within range), or Ranged (projectile toward player)
  - `damage`, `range`, `cooldown` (per-attack timer)
  - `projectileSpeed`, `ammoSpriteId` (Ranged only)
  - `animState` — animation state name to trigger when this attack fires (e.g. `"attack_1"`)
- **Enemy Placements:** Chapters reference enemy types by `typeId`. Placements store path behavior, curve mode, waypoints, respawn flag, speed override, and loop setting.
- **Enemy Type Editor:** The `Edit Enemy Types` sub-panel shows:
  - **Left column:** Live sprite preview showing the first idle frame of the type's sprite sheet at pixel scale, with a toggleable red hitbox overlay.
  - **Right column:** Type ID, sprite ID (with Edit Sprite link), max HP, movement speed, hitbox dimensions, and contact damage fields. Both columns use `BeginGroup`/`EndGroup` so text and controls do not overlap.
  - **Attack editor:** Collapsing sections (one per defined attack) expose attack type, damage, range, cooldown, animation state, and ranged-specific fields (projectile speed, ammo sprite ID).
- **Enemy Screen Context:** When editing enemies for a screen, the editor must show the screen wall map, graphics, features, and grid as context behind the enemy markers and paths.
- **Enemy Editing Modes:** Adding enemies and editing enemy splines are separate canvas states. Add Enemies mode places/selects/deletes enemy anchors. Edit Splines mode edits the selected enemy's waypoint/spline path.
- **Waypoint and Spline Paths:** Users can draw waypoint paths for enemies directly in the editor. Paths may be linear polylines or smoothed Catmull-Rom splines. [cite: 1]
- **Sprite Editing Link:** Enemy sprite IDs open directly in the sprite editor. Sprite frame `type` labels define animation states such as `idle`, `walk`, `attack_1`, `hurt`, and `dead`.
- **Runtime Movement:** Runtime path entities advance along linear or spline waypoints using speed in pixels per second. The entity's `facingX/facingY` vector is updated from the movement direction on every step (tangent-sampled for splines).
- **Runtime Enemy Sprites:** Enemies use a direction-aware sprite selector (`spriteFrameForEntity`) that matches frames by action type and 8-directional facing, auto-mirrors W→E / NW→NE / SW→SE, and falls back gracefully. The horizontal flip is applied in the render loop.
- **Runtime Combat:** `updateEnemyCombat` fires each `EnemyAttackDef` independently with its own per-attack cooldown. Melee attacks deal damage when the player is within range; Ranged attacks spawn a projectile. The legacy contact-damage path remains alongside the new attack system.

### 4.5. NPC Interaction, Movement, and Quest Flow System
NPC interaction is a major engine/editor subsystem and must be designed deliberately rather than bolted onto enemy pathing. NPCs may share low-level spline movement helpers with enemies, but they are semantically different runtime entities with conversation, state, inventory, and quest behavior.

#### 4.5.1. Game State Registry
- **Shared State Store:** The runtime exposes a central `GameState` registry for quest, dialogue, item, and enemy-death effects. The current implementation supports named integers, booleans, and item ownership.
- **User-Labelled Variables:** Designers define variable and item IDs. Example names like `Crows_Killed` or `Grandma_Crow_Quest_Complete` are only sample content, not engine-level concepts.
- **Project Definitions:** `assets/game/project.adgame` v4+ stores project-level state variable definitions and reusable effect definitions. Definitions are generic and can represent any designer-authored counter, flag, or item, including but not limited to crow-kill style quests.
- **Runtime APIs:** `src/game/state.*` provides helpers such as `getInt`, `setInt`, `addInt`, `getBool`, `setBool`, `hasItem`, `giveItem`, and `takeItem`.
- **Unified Effects:** Enemy kills, item pickups, NPC conversations, and scripted events must all write to the same registry so quests are not implemented as one-off special cases.
- **Persistence:** Runtime state can be round-tripped through the ADSTATE format. Editor-authored defaults live in project/chapter data; runtime progress belongs in save-game data.

#### 4.5.2. NPC Data Model
- **NPC Type Definitions:** Reusable `NpcTypeDef` records live in the project game library (`project.adgame`). Each definition stores: id, sprite ID, optional character ID, default movement mode, default interaction mode, default speed, graph ID, and default dialogue lines.
- **NPC Placements:** Screens can place NPC instances with ID, type ID, map/screen position, facing direction, awareness radius, interaction radius, movement path (inline waypoints), loop flag, speed override, graph override, and dialogue override.
- **NPC vs Enemy:** NPCs are not enemy placements. Enemies prioritize combat/pathing. NPCs prioritize dialogue, scripted movement, player awareness, and stateful interaction.
- **Player Awareness:** NPCs detect player proximity. When the player enters the awareness radius, the NPC stops patrolling and idles. When the player enters the interaction radius and presses E, dialogue begins.

#### 4.5.3. Complex Movement Splines
- **Spline Movement:** NPCs support linear waypoint paths with speed in pixels per second. NPCs update `facingX/facingY` during movement so their sprite flips direction correctly.
- **Path Modes:** Paths support loop (wraps back to first waypoint) and once (stops at last waypoint).
- **Waypoint Actions:** Waypoints may include wait duration, facing direction override, and animation state override.
- **Animation Between Movement:** The NPC `actionType` toggles between `"walk"` and `"idle"` based on current movement state. The direction-aware `spriteFrameForNpc` function selects and optionally mirrors frames the same way as players and enemies.
- **Runtime Control:** Flow graph nodes can start, stop, pause, resume, or redirect NPC movement.

#### 4.5.4. Conversation and Interaction Runtime
- **Player Engagement:** When the player enters an NPC interaction radius and presses E, the NPC starts/resumes its dialogue.
- **Dialogue Presentation:** Dialogue lines display speaker and text in a speech bubble or dialogue box above the NPC.
- **Branching Dialogue:** Choices and condition nodes will route the graph based on game state, inventory, quest progress, or prior dialogue (planned).
- **Effects:** Interaction graphs can give/take items, change integers/booleans, start/complete quests, equip or unlock weapons, change NPC movement, and play animations (planned).
- **Example Quest:** Grandma asks Billy to shoot 12 crows with a slingshot. Each crow death increments a designer-authored counter by 1. Approaching Grandma with that counter below the configured threshold plays reminder dialogue. Approaching with the counter at or above the threshold plays completion dialogue, sets a configured completion flag, and gives the configured reward item.
- **Example Scope Note:** The Grandma/Billy/crows quest is an example content scenario. The editor must allow the same flow to be built with any designer-defined variable name, threshold, NPC, enemy, item, or reward.

#### 4.5.5. Flow Graph Editor
- **Graphical Editor:** NPC interaction authoring should use a node/flow diagram editor rather than only text fields.
- **Node Types:** Planned nodes include `Start`, `Dialogue`, `Choice`, `Condition`, `SetInt`, `AddInt`, `SetBool`, `GiveItem`, `TakeItem`, `StartQuest`, `CompleteQuest`, `PlayAnimation`, `MoveNpc`, `Wait`, and `End`.
- **Condition Types:** Conditions include integer comparisons using designer-defined variable IDs, boolean checks, item ownership, quest state, weapon ownership/equipped state, and screen/chapter state.
- **Inspector:** Selecting a node opens an inspector for speaker, dialogue text, variable names, comparison operators, item IDs, movement targets, animation names, and outgoing edge labels.
- **Validation:** The editor must warn about missing target nodes, missing item IDs, missing variable definitions, unreachable nodes, duplicate start nodes, and graph cycles that cannot exit.
- **Simulation:** The editor should include a lightweight test/simulate panel where designers can set temporary game state values and preview which dialogue/effects would run.

#### 4.5.6. Implementation Phases
1. Add the game state registry and runtime helpers first. **Implemented:** `GameState`, ADSTATE round-trip, ADGAME v4 state/effect definitions, and editor context persistence.
2. Add NPC definitions, NPC placements, and runtime rendering/placement. **Implemented:** `NpcTypeDef`, `NpcPlacement` in chapter screens, `RuntimeNpcEntity`, `NpcEditorPanel`, runtime movement + awareness + dialogue.
3. Add interact-near-NPC behavior and simple dialogue script execution. **Implemented:** E key interaction, speech bubble / dialogue box rendering, multi-line sequential dialogue.
4. Add spline movement upgrades for NPCs, including speed, wait, and animation-per-segment data. **Implemented:** patrol waypoints, wait-at-waypoint, speed override, facing-direction-aware sprite rendering.
5. Add graph data files and a non-graph runtime executor. *(Planned)*
6. Build the graphical flow diagram editor on top of the graph data model. *(Planned)*
7. Integrate quest examples such as Grandma/Billy/crows and validate that enemy death, item rewards, and dialogue branching all use the shared state registry without hard-coded example variable names. *(Planned)*

## 5. Data & Persistence
- **Asset Storage:** Assets are stored in portable formats (JSON/binary) for engine use. [cite: 1]
- **Project Folders:** Active projects are stored under `projects/<project>/`. Each project has its own `assets/game/...` and `assets/raw/...` tree. The repo-root `assets/` tree is retained as a fallback/default asset set, but new editor work should live in project folders.
- **Game Project Library:** `assets/game/project.adgame` stores game-level reusable asset references, currently character IDs, chapter IDs, enemy type definitions (with attack defs), NPC type definitions, weapon definitions, default playable character ID, project state variable definitions, reusable effect definitions, and an optional font path.
- **Project State and Effects:** `.adgame` v4+ stores designer-authored state variable definitions and reusable effect definitions. Supported variable types are integer, boolean, and item. Supported effect types are set integer, add integer, set boolean, give item, and take item.
- **Enemy Attack Definitions:** `.adgame` v8 adds per-enemy-type attack definition sub-records (`enemy_attack`), each storing attack type, damage, range, cooldown, projectile speed, animation state, and ammo sprite ID.
- **Runtime Game State:** `.adstate` stores per-playthrough runtime state values: named integer values, named boolean values, and owned item IDs.
- **Screen Tile Maps:** Screen tile data is stored as `.admap` files under `assets/game/maps/`. [cite: 1]
- **Map Obstacles and Items:** `.admap` v4 stores obstacle rectangles with type, sprite ID, size, and timing data. `.admap` v5 adds weapon/ammo/health item placements. Older `.admap` versions remain loadable.
- **Chapter Files:** Chapter layout and metadata stored as `.adchapter` files under `assets/game/chapters/`. Current chapter files store screen layout, per-screen enemy placements, per-screen NPC placements, imported character IDs, and chapter playable character ID. [cite: 1]
- **Character Files:** Reusable character sheets are stored under `assets/game/characters/*.adcharacter`. Character documents store name, bio, base sprite metadata path, playable flag, animation slots, and per-frame animation assignments.
- **Enemy Placements:** Enemy instance data (typeId, path behavior, waypoints, speed, loop, respawn) is stored per-screen inside `.adchapter` files rather than in separate `.adpath` files for the new enemy type system.
- **Game-Ready Graphics:** Wall/Floor Paint exports floor, wall, and preview PNGs per screen to both `assets/raw/tilesets/` and `assets/game/tilesets/`, named `<mapId>_floor.png`, `<mapId>_wall.png`, `<mapId>_preview.png`. Writes are deferred until chapter save.
- **Sprite Sheets:** The sprite editor exports `<id>_sheet.png` and individual `<id>_frame_<n>.png` files to `assets/raw/sprites/`, and metadata to `assets/game/sprites/<id>.sprite.json` on chapter save. Multiple open sprites are buffered in memory and all dirty ones are flushed together.
- **Sprite Metadata:** `.sprite.json` is runtime-facing and parsed by `src/game/sprite.*`; the sprite editor can reopen metadata and import the referenced sheet pixels. Each frame stores an action type label and an optional facing direction (`"E"`, `"W"`, `"N"`, `"S"`, `"NE"`, `"NW"`, `"SE"`, `"SW"`); omitting direction means the frame applies to any facing. The direction field is omitted from the JSON when empty for backward compatibility. This direction system is shared by players, enemies, and NPCs.

## 6. Implementation Status

Implemented:

- Chapter, map, tileset, enemy path, and sprite metadata load/save modules in `src/game`.
- Project-level game library load/save module in `src/game/project.*`.
- Generic game state registry in `src/game/state.*`, including ADSTATE save/load for named ints, bools, and items.
- `.adgame` v8 project-level data including state variable definitions, reusable effect definitions, enemy type definitions with per-type attack definitions, NPC type definitions, weapon definitions, and font path.
- Project-folder startup flow in the editor and project/chapter picker in the direct runtime executable.
- Integrated editor tabs for chapters/screens, maps, wall/floor paint, sprites, tilesets, characters, enemy paths, **weapons**, **NPC types**, and **Quest State** definitions.
- Per-screen wall/floor paint buffers and per-sprite document buffers flushed on chapter save.
- Character document save/load, playable character selection, sprite frame assignment, and runtime playable-character texture loading.
- Basic runtime shell with fixed-step update, direct-launch project selection, pre-baked PNG rendering, tile collision, screen-link transitions, playable character rendering, sprite-backed hazards, linear/spline path-following enemies, and contact-damage combat.
- **Weapon system:** `WeaponDef` data model (melee + ranged) stored in `.adgame` project data. Editor `Weapons` tab to create/edit weapon definitions and set the project starting weapon.
- **Item placement system:** `MapItemPlacement` (weapon/ammo/health pickups) stored in `.admap` v5 items section. Editor `Edit Items` per-screen canvas to place/delete/configure pickups.
- **Runtime attack:** Z key for melee (hitbox sweep in facing direction, brief flash), X key for ranged (projectile with configurable speed/range). Both keys respect per-weapon cooldowns. Melee shows a direction-matched yellow flash; projectiles render as sprites or yellow rectangles.
- **Runtime item collection:** player walks over item pickup to collect it. Weapon pickup equips the weapon to the melee/ranged slot. Ammo pickup adds to the ammo pool. Health pickup restores HP.
- **HUD additions:** melee-weapon indicator bar and ammo pool bar rendered above the health hearts.
- **Enemy type system:** `EnemyType` definitions stored project-wide in `.adgame`. Each type has id, sprite, health, hitbox, speed, contact damage, and any number of `EnemyAttackDef` entries (Contact / Melee / Ranged). The runtime fires each attack independently with per-attack cooldowns. Ranged attacks spawn projectiles toward the player; Melee attacks deal damage within range.
- **Enemy type editor:** Sprite preview panel shows the first idle frame of the enemy sprite with a toggleable hitbox overlay. Two-column layout (preview left, stats right) uses `BeginGroup`/`EndGroup` so text and controls are properly separated. Attack editor shows per-attack collapsing sections.
- **Direction-aware player animation:** player sprite loaded from the character's `.sprite.json`. Action state (`idle`/`walk`/attack states) drives frame selection; 8-directional facing (`E/W/N/S/NE/NW/SE/SW`) selects directional frame subsets. W/NW/SW auto-mirror from E/NE/SE via horizontal texture flip. Animation timer resets on action-state change.
- **Direction-aware enemy animation:** `RuntimePathEntity` tracks `facingX/facingY` updated each movement step (tangent-sampled for spline paths). `spriteFrameForEntity` mirrors `playerSpriteFrame` logic with direction filtering and `bool& flipHorizontal` output. The enemy render loop applies the flip.
- **Direction-aware NPC animation:** `RuntimeNpcEntity` tracks `facingX/facingY` updated while patrolling. `spriteFrameForNpc` applies the same direction + mirror logic using `npc.actionType` and `npc.animSeconds`. The NPC render loop applies the flip.
- **NPC runtime:** `RuntimeNpcEntity` with patrol movement (waypoints, wait, speed override), player awareness radius, interaction radius, E key dialogue trigger, speech bubble / dialogue box rendering, and sequential multi-line dialogue. NPCs pause patrol and idle when the player is within awareness radius.
- **Project root tracking:** all editor panels that load assets (`EnemyPathEditorPanel`, `NpcEditorPanel`, `WeaponEditorPanel`) store `lastLoadedProjectRoot_` and reload when the active project changes, preventing stale data from a previously opened project.

Planned:

- Enemy defeat persistence (respawn flags respected, registry per chapter/screen).
- Enemy and item game-library documents plus chapter import/placement UX beyond the current path-backed enemy instances.
- Branching NPC dialogue, flow graph execution, graphical flow diagram editor.
- Player attack animations; enemy hurt/death state transitions.
- Shader/core-profile renderer to replace fixed-pipeline OpenGL.
- Explicit collision/interaction flags, only when gameplay needs exceed binary mid-layer collision.
