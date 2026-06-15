# Adventure 2D RPG Engine

A C++17 action-RPG runtime and integrated Dear ImGui editor for building
tile-based, screen-to-screen games. The repository includes the
`Billys_Crow_Hunt` sample project.

## Features

- Project and chapter management
- 48 x 32 tile screens using 16 px tiles
- Floor/wall painting, collision, hazards, destination/key doors with SFX, pickups, and animated tiles
- Character, sprite, weapon, enemy, NPC, shop, quest-state, and dialogue editors
- Linear/spline actor paths with enter, timed speech, and leave waypoint actions
- Sprite polygon drawing, frame drag-reordering, and repeatable/one-time pickup placement
- Melee and ranged combat, enemy paths and aggro, inventory, shops, and saves
- Ranged weapon feel stats: hold-to-draw charge, overcharge penalties, spread/steady
  accuracy, pellet spreads, distance damage falloff, and cone auto-aim with
  target cycling (keyboard + gamepad)
- Project-local assets under `projects/<project>/assets/`

## Build

Requirements:

- CMake 3.20+
- A C++17 compiler
- OpenGL and GLFW 3 for the windowed editor
- SDL2 and Vorbisfile in addition to OpenGL/GLFW for the windowed game
- `pkg-config` for dependency discovery

On Debian or Ubuntu:

```sh
sudo apt-get install cmake g++ pkg-config libgl1-mesa-dev libglfw3-dev libsdl2-dev libvorbis-dev
```

Configure and build:

```sh
cmake -S . -B build
cmake --build build --parallel
```

CMake always builds the data libraries and smoke executables. Windowed targets
are added only when their native dependencies are found.

## Run

```sh
./build/adventure_editor_window
./build/adventure_game_window
```

Launching the game without arguments opens a project/chapter picker. To launch a
chapter directly:

```sh
./build/adventure_game_window \
  projects/Billys_Crow_Hunt/assets/game/chapters/Farm_House.adchapter
```

Runtime options:

```text
--continue        Load assets/game/save.adstate for the selected project
--fresh           Ignore and do not overwrite save.adstate
--screen <id>     Start on a specific chapter screen
--pos <x> <y>     Override the initial pixel position
```

Normal launches start a new game. Use `--continue` to resume saved state.

Controls:

- Move: arrow keys / WASD / left stick
- Interact: E / Enter / Space / gamepad A
- Melee attack: Z / gamepad X
- Ranged attack: X / gamepad B
- Inventory and weapon selector: I / gamepad Y, Start, or Back
- In the inventory, highlight a weapon and press its attack button to assign it

The runtime keeps the complete 48 x 32 tile room visible and renders health,
weapon slots, and ammo in a dedicated black bar below the playfield. At startup
it queries the primary monitor's usable work area, chooses the largest integer
window scale that fits with room for window decorations, and centers the window.

Dialogue text can show live game-state values with `$Variable_Id`. For example,
`You killed $Crows_killed crows.` resolves chapter-scoped variables in the
current chapter before checking universal variables. Use `${Variable.With-Dots}`
for IDs containing dots or hyphens, and `$$` for a literal `$`.

## Verify

```sh
./build/adventure_editor_smoke
./build/adventure_game_smoke \
  projects/Billys_Crow_Hunt/assets/game/maps/screen_1_map.admap \
  projects/Billys_Crow_Hunt/assets/game/chapters/Farm_House.adchapter
```

## Documentation

- [Project specification](docs/RPG_Spec.md)
- [Codebase and file-format guide](docs/code_base.md)
- [Editor manual](docs/manual/README.md)
- [Manual test checklist](docs/manual_high_level_test_checklist.md)
- [Functionality audit checklist](docs/high_level_functionality_audit.md)
