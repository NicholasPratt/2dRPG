# Project Specification: Pixel-Art Action RPG Engine & Editor (C++)

## 1. Project Overview
The goal is to develop a 2D Action RPG engine and integrated development environment (IDE) in the style of classic SNES/GBA titles (e.g., *The Legend of Zelda: A Link to the Past*). The project emphasizes a "hand-drawn" pixel art aesthetic and a robust, built-in editor that allows for real-time asset creation, level design, and entity behavior scripting.

## 2. Technical Stack
- **Language:** C++
- **Graphics/Input:** (To be determined by agent, e.g., SDL2, Raylib, or SFML)
- **Target Style:** 16-bit / 32-bit hand-drawn pixel art.
- **Perspective:** Top-down / Isometric hybrid with pseudo-3D parallax effects.

## 3. Core Engine Architecture

### 3.1. World Structure
- **Chapters:** The game is organized into high-level Chapters.
- **Screens:** Each chapter consists of discrete "Screens." 
- **Scrolling:** Movement between screens uses a "Screen-Flip" transition (classic Zelda style), loading one screen at a time.
- **Pseudo-3D / Parallax:** The engine must support multiple layers. Specifically, the floor and overhead layers should support parallax scrolling to create depth.

### 3.2. Combat & Movement
- **Action-Oriented:** Real-time combat (non-turn-based).
- **Timed Mechanics:** Combat should rely on timing for attacks, blocks, or dodges.
- **Enemy Persistence:** By default, defeated enemies do **not** respawn upon re-entering a screen unless a specific "respawn" flag is set in the editor.

## 4. The Integrated Editor (Critical Component)
The editor is the primary tool for game construction and must include:

### 4.1. Layout Editor
- **Macro View:** Plot courses and connections over the entire level/chapter.
- **Screen Management:** Add, remove, and link individual screens.

### 4.2. Screen/Tile Editor
- **Base Unit:** The Tile.
- **Functionality:** - Copy/Paste tiles for efficiency.
    - **Pixel Painting:** Built-in drawing tools (similar to MS Paint or Piskel) to modify tiles directly or create unique "one-off" features on a per-screen basis.
- **Layers:** Support for floor, player-level, and ceiling/overlay layers.

### 4.3. Sprite & Animation Editor
- **Dynamic Sprite Sheets:** Editor for Player, Enemy, and Item sprites.
- **Painting Tools:** Ability to draw/edit pixel art directly within the engine.
- **Item Animation:** Set frame sequences and animation speeds for world items.
- **Player Progression:** Sprite maps must be saveable/loadable and capable of being updated as the character progresses through chapters.

### 4.4. Enemy AI & Pathing
- **Movement Splines:** The editor must allow users to draw movement paths (splines) for enemies.
- **State Definition:** Define enemy behavior (idle, aggro, patrol) via the editor interface.

## 5. Data & Persistence
- **Save/Load System:** All assets (tilesets, sprite sheets, level layouts) must be stored in a portable format (e.g., JSON, XML, or custom binary).
- **Asset Reuse:** Ensure the player sprite map can be reused across chapters while allowing for chapter-specific enemy and item sheets.

## 6. Key Deliverables for Coding Agents
1. **Core Engine:** Implement the screen-based rendering and C++ game loop.
2. **The Editor UI:** Develop a functional UI for the layout, tile painting, and spline drawing.
3. **Collision & Interaction:** Implement pixel-perfect or tile-based collision.
4. **Pseudo-3D Implementation:** Develop the parallax floor/layer system.
