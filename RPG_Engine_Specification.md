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

### 4.1. Project & Chapter Management (ChapterManager)
The `ChapterManager` handles the high-level lifecycle and file I/O for the session.
- **Startup Flow:** Upon opening, the editor must prompt the user to select an existing chapter or create a new one. [cite: 1]
- **Session Safety:** Exiting the editor or switching chapters must trigger a prompt to save or discard changes. [cite: 1]
- **Exporting:** Saving a chapter must automatically generate/update PNG sprite sheets for all edited characters, save dirty screen maps, and export current wall/floor paint graphics into game-ready PNGs. [cite: 1]
- **State Registry:** Maintains a registry of defeated enemies and handles the "Dirty Flag" system for modified screens.

### 4.2. Layout & Map Editor (ScreenEditor)
- **Continuous Screen Grid:** The Screens tab displays the chapter as a continuous grid. The currently selected screen is centered and highlighted. [cite: 1]
- **Adjacent Context:** Adjacent and nearby screens show their wall/mid-layer layout to ensure spatial continuity. [cite: 1]
- **Connected Screens:** North/south/east/west controls manage reciprocal screen links. [cite: 1]
- **Screen Graphics Mode:** Clicking "Edit Screen Graphics" transitions the user directly into Wall/Floor Paint for that specific screen. [cite: 1]

### 4.3. Graphic & Sprite Editing
- **Pixel Painting:** Integrated tools comparable to MS Paint or Piskel for tiles and sprites. [cite: 1]
- **Wall/Floor Paint Layers:** - **Floor Layer:** Base walkable texture (`<mapId>_floor.png`).
    - **Wall/Object Layer:** Includes collision structures AND "Transparent Objects." 
    - **Ceiling Layer:** Parallax-enabled overhead graphics.
- **Object Transparency:** **The editor must allow drawing objects in the paint layer that are flagged as transparent to player movement, enabling the player to move behind or through them.**
- **Visual Aids:** The Wall/Floor Paint view must show the selected screen's wall structure as a toggleable highlight guide. [cite: 1]
- **Dynamic Character Editing:** Clicking "Edit Sprite" while a character is selected opens their specific sprite sheet for modification. [cite: 1]

### 4.4. Enemy AI & Pathing
- **Movement Splines:** Users can draw movement paths (splines) for enemies directly in the editor. [cite: 1]

## 5. Data & Persistence
- **Asset Storage:** Assets are stored in portable formats (JSON/Binary) for engine use. [cite: 1]
- **Screen Tile Maps:** Screen tile data is stored as `.admap` files under `assets/game/maps/`. [cite: 1]
- **Chapter Files:** Chapter layout and metadata stored as `.adchapter` files under `assets/game/chapters/`. [cite: 1]
- **Game-Ready Graphics:** Wall/Floor Paint exports floor, wall, and preview PNGs to `assets/game/tilesets/`. [cite: 1]
