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

### 3.2. Display & Tile Dimensions
- **Tile Size:** 16 × 16 pixels (`kTileSize = 16`). All sprites and tileset tiles are fixed to this size.
- **Screen Size:** 24 × 16 tiles (`kScreenTilesW = 24`, `kScreenTilesH = 16`), giving a native resolution of 384 × 256 pixels per screen.
- These values are defined in `src/game/constants.hpp` and are the single source of truth for all editors and engine code.

### 3.3. Combat & Persistence
- **Action-Oriented:** Real-time, timed combat mechanics. [cite: 1]
- **Enemy Persistence:** Defeated enemies do not respawn by default unless a "respawn" flag is set. [cite: 1]

### 3.4. Runtime Shell
- **Executable:** `adventure_game_window` is the current runtime executable.
- **Timing:** The runtime uses a fixed 60 Hz update loop for gameplay simulation.
- **Rendering:** The runtime renders pre-baked per-screen floor/wall PNGs and keeps `.admap` data for collision and structure.
- **Screen Transitions:** Crossing a screen edge follows the current `ChapterScreen` north/south/east/west link and performs a simple sliding transition.
- **Collision:** The current runtime collision model treats any nonzero `.admap` mid-layer cell as solid.

## 4. The Integrated Editor (Critical Component)
The editor is **state-dependent** and context-aware, tracking exactly what the user is editing at all times.

### 4.1. Project & Chapter Management
`EditorApp` coordinates the high-level chapter lifecycle and delegates file I/O to runtime-facing game modules and editor panels.
- **Startup Flow:** Upon opening, the editor must prompt the user to select an existing chapter or create a new one. [cite: 1]
- **Session Safety:** Exiting the editor or switching chapters must trigger a prompt to save or discard changes. [cite: 1]
- **Exporting:** Saving a chapter must automatically generate/update PNG sprite sheets for all edited sprites, save dirty screen maps, and export wall/floor paint graphics into game-ready PNGs for every modified screen. [cite: 1]
- **Dirty-Buffer System:** Both the sprite editor and wall/floor paint panel hold unsaved edits in per-asset in-memory buffers (keyed by asset ID) during the session. Disk writes happen only when the chapter is saved. Switching chapters clears all buffers so data from one chapter cannot bleed into another.
- **State Registry:** Maintains a registry of defeated enemies and handles the "Dirty Flag" system for modified screens.

### 4.2. Layout & Map Editor (ScreenEditor)
- **Continuous Screen Grid:** The Screens tab displays the chapter as a continuous grid. The currently selected screen is centered and highlighted. [cite: 1]
- **Adjacent Context:** Adjacent and nearby screens show their wall/mid-layer layout to ensure spatial continuity. [cite: 1]
- **Connected Screens:** North/south/east/west controls manage reciprocal screen links. [cite: 1]
- **Screen Graphics Mode:** Clicking "Edit Screen Graphics" transitions the user directly into Wall/Floor Paint for that specific screen. [cite: 1]

### 4.3. Graphic & Sprite Editing
- **Pixel Painting:** Integrated tools comparable to MS Paint or Piskel for tiles and sprites. [cite: 1]
- **Wall/Floor Paint Layers:**
    - **Floor Layer:** Base walkable texture (`<mapId>_floor.png`).
    - **Wall/Object Layer:** Rendered over actors and guided by the structural `.admap` mid layer.
    - **Ceiling Layer:** Represented structurally in `.admap`; a dedicated runtime paint/export layer is planned.
- **Object Transparency:** Planned collision flags should distinguish solid, visual-overlap, and interaction cells. The current runtime treats nonzero `.admap` mid-layer cells as solid.
- **Visual Aids:** The Wall/Floor Paint view must show the selected screen's wall structure as a toggleable highlight guide. [cite: 1]
- **Dynamic Character Editing:** Clicking "Edit Sprite" while a character is selected opens their specific sprite sheet for modification. [cite: 1]
- **Per-Screen Graphics Isolation:** Each screen has its own set of floor/wall/preview PNGs (named `<mapId>_floor.png` etc.). Edits are held in per-screen in-memory buffers during the session and written to disk only on chapter save.
- **Tile Palette System:** In Select mode, a region on the canvas can be snapped to the tile grid and added to a chapter-wide tile palette. Palette entries store floor and wall pixel data for the selected region. The **TileStamp** tool pastes a palette entry onto any screen; the **TileErase** tool clears entire tile cells (makes them transparent). This accelerates level creation by reusing hand-crafted tile art across multiple screens.
- **Per-Sprite Isolation:** The sprite editor holds each open sprite document in its own in-memory buffer. Switching sprites stashes the current document; returning to a sprite restores it from memory. All dirty sprite documents are flushed to disk (metadata JSON + sprite-sheet PNG) on chapter save.

### 4.4. Enemy AI & Pathing
- **Waypoint Paths:** Users can draw waypoint paths for enemies directly in the editor. [cite: 1]
- **Runtime Movement:** Runtime path entities load `.adpath` files for the active screen `mapId` and advance along waypoints using speed in pixels per second.

## 5. Data & Persistence
- **Asset Storage:** Assets are stored in portable formats (JSON/binary) for engine use. [cite: 1]
- **Screen Tile Maps:** Screen tile data is stored as `.admap` files under `assets/game/maps/`. [cite: 1]
- **Chapter Files:** Chapter layout and metadata stored as `.adchapter` files under `assets/game/chapters/`. [cite: 1]
- **Enemy Paths:** Enemy waypoint data is stored as `.adpath` files under `assets/game/paths/` and is parsed by `src/game/path.*`.
- **Game-Ready Graphics:** Wall/Floor Paint exports floor, wall, and preview PNGs per screen to both `assets/raw/tilesets/` and `assets/game/tilesets/`, named `<mapId>_floor.png`, `<mapId>_wall.png`, `<mapId>_preview.png`. Writes are deferred until chapter save.
- **Sprite Sheets:** The sprite editor exports `<id>_sheet.png` to `assets/raw/sprites/` and metadata to `assets/game/sprites/<id>.sprite.json` on chapter save. Multiple open sprites are buffered in memory and all dirty ones are flushed together.
- **Sprite Metadata:** `.sprite.json` is runtime-facing and parsed by `src/game/sprite.*`; the sprite editor can reopen metadata and import the referenced sheet pixels.

## 6. Implementation Status

Implemented:

- Chapter, map, tileset, enemy path, and sprite metadata load/save modules in `src/game`.
- Integrated editor tabs for chapters/screens, maps, wall/floor paint, sprites, tilesets, characters, and enemy paths.
- Per-screen wall/floor paint buffers and per-sprite document buffers flushed on chapter save.
- Basic runtime shell with fixed-step update, pre-baked PNG rendering, tile collision, screen-link transitions, and path-following entities.

Planned:

- Runtime sprite animation/rendering from loaded sprite metadata.
- Character save/load format and runtime character spawning.
- Defeated-enemy state registry and persistence.
- Shader/core-profile renderer to replace fixed-pipeline OpenGL.
- Explicit collision/interaction flags, only when gameplay needs exceed binary mid-layer collision.
