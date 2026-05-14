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

### 3.2. Combat & Persistence
- **Action-Oriented:** Real-time, timed combat mechanics. [cite: 1]
- **Enemy Persistence:** Defeated enemies do not respawn by default unless a "respawn" flag is set. [cite: 1]

## 4. The Integrated Editor (Critical Component)
The editor is **state-dependent** and context-aware, tracking exactly what the user is editing at all times.

### 4.1. Project & Chapter Management
- **Startup Flow:** **Upon opening, the editor must prompt the user to select an existing chapter or create a new one.**
- **Session Safety:** **Exiting the editor or switching chapters must trigger a prompt to save or discard changes.**
- **Exporting:** **Saving a chapter must automatically generate/update PNG sprite sheets for all edited characters and entities, save dirty screen maps, and export current wall/floor paint graphics into game-ready PNGs.**

### 4.2. Layout & Map Editor
- **Continuous Screen Grid:** The Screens tab displays the chapter as a continuous grid of screen-sized tile layouts. The currently selected screen is centered and highlighted.
- **Adjacent Context:** Adjacent and nearby screens must show their wall/mid-layer layout so the user can understand spatial continuity without link-line clutter.
- **Screen Editing:** The selected screen's floor, wall/mid, and ceiling tile layers can be painted directly from the Screens tab.
- **Connected Screens:** The editor provides north/south/east/west controls to create or select connected screens and maintain reciprocal screen links.
- **Screen Deletion:** Screens can be deleted from the screen inspector, while preserving at least one screen per chapter and clearing broken links.
- **Screen Graphics Mode:** **Clicking "Edit Screen Graphics" transitions the user directly into Wall/Floor Paint for that specific screen.**
- **Visual Aids:** **The Wall/Floor Paint view must show the selected screen's wall structure as a toggleable highlight guide.**

### 4.3. Graphic & Sprite Editing
- **Pixel Painting:** Integrated tools comparable to MS Paint or Piskel for tiles and sprites. [cite: 1]
- **Wall/Floor Paint:** Screen graphics are painted as floor and wall/overhead PNG layers. When opened from a screen, the paint document uses that screen's map id and wall guide.
- **Tile System:** Support for tile copy-pasting to reuse drawings while allowing for unique, per-tile edits. [cite: 1]
- **Dynamic Character Editing:** **Clicking "Edit Sprite" while a character is selected opens their specific sprite sheet for modification.**
- **Animations:** Items support animated frames with adjustable speeds defined in the editor. [cite: 1]

### 4.4. Enemy AI & Pathing
- **Movement Splines:** Users can draw movement paths (splines) for enemies directly in the editor. [cite: 1]

## 5. Data & Persistence
- **Asset Storage:** Assets are stored in portable formats (JSON/Binary) for engine use. [cite: 1]
- **Reusable Sprite Maps:** Player sprite maps can be reused or updated across chapters. [cite: 1]
- **Screen Tile Maps:** Screen tile data is stored as `.admap` files under `assets/game/maps/`.
- **Chapter Files:** Chapter screen layout, links, start screen, and respawn flags are stored as `.adchapter` files under `assets/game/chapters/`.
- **Game-Ready Screen Graphics:** Wall/Floor Paint exports floor, wall, and preview PNGs to `assets/game/tilesets/` using the selected map id, e.g. `<mapId>_floor.png` and `<mapId>_wall.png`.
