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

### 3.4. Runtime Shell
- **Executable:** `adventure_game_window` is the current runtime executable.
- **Timing:** The runtime uses a fixed 60 Hz update loop for gameplay simulation.
- **Rendering:** The runtime renders pre-baked per-screen floor/wall PNGs and keeps `.admap` data for collision and structure.
- **Screen Transitions:** Crossing a screen edge follows the current `ChapterScreen` north/south/east/west link and performs a sliding transition. The transition triggers when 30% of the player sprite has crossed the edge, only if the destination screen exists and the destination entry point is not blocked.
- **Collision:** The current runtime collision model treats any nonzero `.admap` mid-layer cell as solid.
- **Playable Character:** The runtime resolves the playable character through the active chapter, then the project library, then a legacy fallback scan of character files. The selected character's idle frame PNG is rendered as the player.
- **Obstacles:** `.admap` files can store spike, pit, and timed-spike obstacle rectangles. Obstacles may reference sprite-editor sprites by ID and active hazards respawn the player at the map spawn.

## 4. The Integrated Editor (Critical Component)
The editor is **state-dependent** and context-aware, tracking exactly what the user is editing at all times.

### 4.1. Project & Chapter Management
`EditorApp` coordinates the high-level chapter lifecycle and delegates file I/O to runtime-facing game modules and editor panels.
- **Startup Flow:** Upon opening, the editor must prompt the user for a project and chapter. Existing projects are loaded from `projects/<project>/`; new projects create their own project folder with `assets/game/...` and `assets/raw/...` subfolders.
- **Project Isolation:** Every project owns its own assets, chapters, maps, sprites, characters, screen graphics, enemy types, and project manifest. Editor and runtime asset lookup must use the selected project folder, not stale repo-root assets.
- **Session Safety:** Exiting the editor or switching chapters must trigger a prompt to save or discard changes. [cite: 1]
- **Exporting:** Saving a chapter must automatically generate/update PNG sprite sheets for all edited sprites, save dirty screen maps, and export wall/floor paint graphics into game-ready PNGs for every modified screen. [cite: 1]
- **Dirty-Buffer System:** Both the sprite editor and wall/floor paint panel hold unsaved edits in per-asset in-memory buffers (keyed by asset ID) during the session. Disk writes happen only when the chapter is saved. Switching chapters clears all buffers so data from one chapter cannot bleed into another.
- **Game Library Save:** Character sheets and enemy types are saved as reusable game-library assets. `assets/game/project.adgame` records available character IDs, chapter IDs, enemy type definitions, and the project default playable character. `.adchapter` files record imported character IDs and chapter playable character ID.
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
- **Frame Assignment:** Character sheets show exported sprite frame PNGs as individual thumbnails. Each frame can be assigned to an animation state via dropdown or custom text. Multiple frames may share a state such as `walk`.
- **Per-Screen Graphics Isolation:** Each screen has its own set of floor/wall/preview PNGs (named `<mapId>_floor.png` etc.). Edits are held in per-screen in-memory buffers during the session and written to disk only on chapter save.
- **Tile-Based Screen Graphics Tools:** The screen graphics editor includes `Tile Draw`, `Tile Select`, `Tile Paste`, `Stamp`, `Tile Fill`, and `Tile Erase`. `Tile Draw` fills a full 16×16 tile cell with the selected color and supports drag painting. `Tile Select` selects exactly one tile cell for copy/paste or palette capture. `Stamp` places the selected palette tile and supports drag painting across tile cells. `Tile Fill` flood-fills contiguous matching tiles with copies of the selected palette tile. `Tile Erase` clears full tile cells.
- **Tile Palette System:** Tile selections can be added to a chapter-wide tile palette. Palette entries store floor and wall pixel data for the selected tile. Tile palette entries are static in the screen graphics editor; animation is authored through the Sprite editor rather than an in-panel animation preview.
- **Per-Sprite Isolation:** The sprite editor holds each open sprite document in its own in-memory buffer. Switching sprites stashes the current document; returning to a sprite restores it from memory. All dirty sprite documents are flushed to disk (metadata JSON + sprite-sheet PNG) on chapter save.

### 4.4. Enemy AI & Pathing
- **Enemy Editor:** Users can define reusable enemy types and per-screen enemy placements with enemy ID, sprite ID, behavior state, movement curve mode, speed, respawn flag, and map reference.
- **Enemy Screen Context:** When editing enemies for a screen, the editor must show the screen wall map, graphics, features, and grid as context behind the enemy markers and paths.
- **Enemy Editing Modes:** Adding enemies and editing enemy splines are separate canvas states. Add Enemies mode places/selects/deletes enemy anchors. Edit Splines mode edits the selected enemy's waypoint/spline path.
- **Combat Data:** Enemy instances store max health, contact damage, hitbox size, and hit cooldown data. Runtime contact damage uses those values and gives the player a short invulnerability window.
- **Waypoint and Spline Paths:** Users can draw waypoint paths for enemies directly in the editor. Paths may be linear polylines or smoothed Catmull-Rom splines. [cite: 1]
- **Sprite Editing Link:** Enemy sprite IDs open directly in the sprite editor. Sprite frame `type` labels define animation states such as `idle`, `walk`, `attack`, `hurt`, and `dead`.
- **Runtime Movement:** Runtime path entities load `.adpath` files for the active screen `mapId` and advance along waypoints using speed in pixels per second. Linear and spline paths are both supported.
- **Runtime Enemy Sprites:** Runtime path entities render the sprite sheet referenced by the enemy path's sprite ID when available, falling back to debug rectangles when missing.

## 5. Data & Persistence
- **Asset Storage:** Assets are stored in portable formats (JSON/binary) for engine use. [cite: 1]
- **Project Folders:** Active projects are stored under `projects/<project>/`. Each project has its own `assets/game/...` and `assets/raw/...` tree. The repo-root `assets/` tree is retained as a fallback/default asset set, but new editor work should live in project folders.
- **Game Project Library:** `assets/game/project.adgame` stores game-level reusable asset references, currently character IDs, chapter IDs, enemy type definitions, and default playable character ID.
- **Screen Tile Maps:** Screen tile data is stored as `.admap` files under `assets/game/maps/`. [cite: 1]
- **Map Obstacles:** `.admap` v4 stores obstacle rectangles with type, sprite ID, size, and timing data. Older `.admap` versions remain loadable.
- **Chapter Files:** Chapter layout and metadata stored as `.adchapter` files under `assets/game/chapters/`. Current chapter files store screen layout, imported character IDs, and chapter playable character ID. [cite: 1]
- **Character Files:** Reusable character sheets are stored under `assets/game/characters/*.adcharacter`. Character documents store name, bio, base sprite metadata path, playable flag, animation slots, and per-frame animation assignments.
- **Enemy Paths:** Enemy waypoint/spline data is stored as `.adpath` files under `assets/game/paths/` and is parsed by `src/game/path.*`. Current path files include enemy ID, sprite ID, behavior, curve mode, combat data, speed, loop, respawn, and waypoint data.
- **Game-Ready Graphics:** Wall/Floor Paint exports floor, wall, and preview PNGs per screen to both `assets/raw/tilesets/` and `assets/game/tilesets/`, named `<mapId>_floor.png`, `<mapId>_wall.png`, `<mapId>_preview.png`. Writes are deferred until chapter save.
- **Sprite Sheets:** The sprite editor exports `<id>_sheet.png` and individual `<id>_frame_<n>.png` files to `assets/raw/sprites/`, and metadata to `assets/game/sprites/<id>.sprite.json` on chapter save. Multiple open sprites are buffered in memory and all dirty ones are flushed together.
- **Sprite Metadata:** `.sprite.json` is runtime-facing and parsed by `src/game/sprite.*`; the sprite editor can reopen metadata and import the referenced sheet pixels. Sprite frames include animation type labels.

## 6. Implementation Status

Implemented:

- Chapter, map, tileset, enemy path, and sprite metadata load/save modules in `src/game`.
- Project-level game library load/save module in `src/game/project.*`.
- Project-folder startup flow in the editor and project/chapter picker in the direct runtime executable.
- Integrated editor tabs for chapters/screens, maps, wall/floor paint, sprites, tilesets, characters, enemy paths, and **weapons**.
- Per-screen wall/floor paint buffers and per-sprite document buffers flushed on chapter save.
- Character document save/load, playable character selection, sprite frame assignment, and runtime playable-character texture loading.
- Basic runtime shell with fixed-step update, direct-launch project selection, pre-baked PNG rendering, tile collision, screen-link transitions, playable character rendering, sprite-backed hazards, linear/spline path-following enemies, and contact-damage combat.
- **Weapon system:** `WeaponDef` data model (melee + ranged) stored in `.adgame` v3. Editor `Weapons` tab to create/edit weapon definitions and set the project starting weapon.
- **Item placement system:** `MapItemPlacement` (weapon/ammo/health pickups) stored in `.admap` v5 items section. Editor `Edit Items` per-screen canvas to place/delete/configure pickups.
- **Runtime attack:** Z key for melee (hitbox sweep in facing direction, brief flash), X key for ranged (projectile with configurable speed/range). Both keys respect per-weapon cooldowns. Melee shows a direction-matched yellow flash; projectiles render as sprites or yellow rectangles.
- **Runtime item collection:** player walks over item pickup to collect it. Weapon pickup equips the weapon to the melee/ranged slot. Ammo pickup adds to the ammo pool. Health pickup restores HP.
- **HUD additions:** melee-weapon indicator bar and ammo pool bar rendered above the health hearts.

Planned:

- Enemy defeat persistence (respawn flags respected, registry per chapter/screen).
- Enemy and item game-library documents plus chapter import/placement UX beyond the current path-backed enemy instances.
- Player attack animations; enemy hurt/death state transitions.
- Shader/core-profile renderer to replace fixed-pipeline OpenGL.
- Explicit collision/interaction flags, only when gameplay needs exceed binary mid-layer collision.
