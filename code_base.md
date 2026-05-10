# Code Base Structure

This project is a C++ 2D action RPG built with raylib, with an editor developed alongside the game. The editor uses Dear ImGui for dockable tools, inspectors, asset workflows, and editing panels. The game style target is classic SNES/GBA action RPGs: tile maps, sprite animation, room/world transitions, real-time combat, dialogue, inventory, triggers, and authored encounters.

The main architectural goal is to keep runtime game code, editor-only code, data formats, and low-level platform code separated. The editor should create the same data that the game loads. The game should not depend on ImGui.

## Core Principles

- The game runtime must be able to run without editor code.
- Editor tools should operate on shared asset/data types, not duplicate game-only formats.
- Prefer data-driven content for maps, entities, sprites, animations, items, dialogue, and combat definitions.
- Keep raylib usage behind thin systems where practical, but do not over-abstract simple drawing/input calls too early.
- Build content tools early because action RPGs are content-heavy.
- Use stable file formats from the beginning: JSON/TOML/YAML for authored data, PNG for images, WAV/OGG for audio, and a project manifest for asset indexing.
- Keep hot reload and quick iteration in mind, even if implemented later.

## Current Directory Layout

The project scaffold has been created with the directories below. Source files are listed as the planned ownership points for each module; add them as the implementation grows. The empty directories are intentional so assets, dependencies, runtime code, editor tools, and tests have stable homes from the start.

```text
Adventure/
  code_base.md
  CMakeLists.txt                  # planned build entry point
  assets/
    raw/                          # source assets before game-ready processing
      sprites/
      tilesets/
      audio/
      fonts/
    game/                         # editor-authored assets loaded by the runtime
      sprites/
      tilesets/
      maps/
      animations/
      entities/
      items/
      dialogue/
      palettes/
      project.json                # planned asset manifest
  external/                       # third-party dependencies, preferably pinned
    raylib/
    imgui/
    rlImGui/
  src/                            # project source
    app/                          # executable entry points
      main_game.cpp
      main_editor.cpp
    core/                         # dependency-free utilities and primitives
      assert.hpp
      filesystem.hpp
      log.hpp
      math.hpp
      result.hpp
      time.hpp
      types.hpp
    platform/                     # raylib-backed platform services
      window.hpp
      window_raylib.cpp
      input.hpp
      input_raylib.cpp
      audio.hpp
      audio_raylib.cpp
    render/                       # 2D renderer and texture wrappers
      camera2d.hpp
      color.hpp
      renderer2d.hpp
      renderer2d_raylib.cpp
      texture.hpp
      texture_raylib.cpp
      draw_batch.hpp
    assets/                       # asset ids, database, loaders, validation
      asset_id.hpp
      asset_database.hpp
      asset_loader.hpp
      image_asset.hpp
      sprite_asset.hpp
      tileset_asset.hpp
      map_asset.hpp
      animation_asset.hpp
      audio_asset.hpp
    data/                         # serializable authored data structures
      serialization.hpp
      project_file.hpp
      sprite_data.hpp
      tileset_data.hpp
      map_data.hpp
      entity_data.hpp
      item_data.hpp
      dialogue_data.hpp
    game/                         # runtime simulation and game UI
      game.hpp
      game_state.hpp
      world/                      # maps, rooms, layers, triggers
        world.hpp
        map.hpp
        room.hpp
        tile_layer.hpp
        collision_layer.hpp
        trigger.hpp
      entities/                   # entity model and gameplay actors
        entity.hpp
        entity_id.hpp
        entity_registry.hpp
        components.hpp
        player.hpp
        enemy.hpp
        npc.hpp
        projectile.hpp
      systems/                    # runtime update/render systems
        animation_system.hpp
        collision_system.hpp
        combat_system.hpp
        enemy_ai_system.hpp
        interaction_system.hpp
        movement_system.hpp
        render_system.hpp
        script_system.hpp
      ui/                         # runtime HUD and menus
        hud.hpp
        dialogue_box.hpp
        inventory_menu.hpp
      save/                       # save file format and persistence
        save_file.hpp
        save_system.hpp
    editor/                       # ImGui editor application
      editor_app.hpp
      editor_context.hpp
      editor_document.hpp
      editor_history.hpp
      editor_selection.hpp
      commands/                   # undoable editor mutations
        editor_command.hpp
        map_commands.hpp
        sprite_commands.hpp
        tileset_commands.hpp
      panels/                     # dockable ImGui panels
        dockspace_panel.hpp
        asset_browser_panel.hpp
        inspector_panel.hpp
        viewport_panel.hpp
        console_panel.hpp
        sprite_editor_panel.hpp
        tile_editor_panel.hpp
        map_editor_panel.hpp
      tools/                      # viewport/editing tools
        editor_tool.hpp
        select_tool.hpp
        paint_tile_tool.hpp
        erase_tile_tool.hpp
        fill_tool.hpp
        collision_tool.hpp
        entity_place_tool.hpp
        trigger_tool.hpp
      widgets/                    # reusable ImGui widgets
        property_grid.hpp
        file_picker.hpp
        texture_preview.hpp
        timeline_widget.hpp
        palette_widget.hpp
  tests/                          # unit and data-validation tests
    asset_tests.cpp
    map_tests.cpp
    serialization_tests.cpp
```

Created directories:

```text
assets/raw/sprites
assets/raw/tilesets
assets/raw/audio
assets/raw/fonts
assets/game/sprites
assets/game/tilesets
assets/game/maps
assets/game/animations
assets/game/entities
assets/game/items
assets/game/dialogue
assets/game/palettes
external/raylib
external/imgui
external/rlImGui
src/app
src/core
src/platform
src/render
src/assets
src/data
src/game/world
src/game/entities
src/game/systems
src/game/ui
src/game/save
src/editor/commands
src/editor/panels
src/editor/tools
src/editor/widgets
tests
```

## Build Targets

Use separate executables that share libraries:

```text
adventure_core      Shared utilities, math, logging, filesystem, serialization.
adventure_platform  raylib-backed window, input, audio, texture loading.
adventure_assets    Asset database, loading, validation, import/export.
adventure_runtime   Game simulation, world, entities, systems, UI.
adventure_editor    ImGui editor application and editor-only tools.
adventure_game      Shipping game executable.
```

The important dependency direction:

```text
editor -> runtime -> assets -> platform -> core
game   -> runtime -> assets -> platform -> core
```

The runtime must not include editor headers. Editor-only features such as selection, undo history, dock layouts, gizmos, and ImGui widgets should stay in `src/editor`.

## Runtime Loop

The game executable should be small:

```cpp
int main()
{
    Platform platform;
    AssetDatabase assets;
    Game game;

    platform.init();
    assets.loadProject("assets/game/project.json");
    game.init(&platform, &assets);

    while (!platform.shouldClose()) {
        float dt = platform.beginFrame();
        game.update(dt);
        game.render();
        platform.endFrame();
    }

    game.shutdown();
    platform.shutdown();
}
```

Keep `Game::update()` deterministic where possible. Input is gathered at the platform layer, translated into gameplay actions, then consumed by movement, combat, interaction, and menu systems.

## Editor Loop

The editor owns ImGui state, active documents, selection, undo/redo, open panels, and tool modes:

```cpp
int main()
{
    Platform platform;
    AssetDatabase assets;
    EditorApp editor;

    platform.init();
    assets.loadProject("assets/game/project.json");
    editor.init(&platform, &assets);

    while (!platform.shouldClose()) {
        float dt = platform.beginFrame();
        editor.update(dt);
        editor.render();
        platform.endFrame();
    }

    editor.shutdown();
    platform.shutdown();
}
```

The editor can embed a live game preview by owning a `Game` instance inside a viewport panel, but the dependency still points from editor to runtime.

## Data Model

Use plain data structures for authored content. Runtime systems can build optimized working state from these structures.

Recommended asset types:

- `SpriteAsset`: source image, frame rectangles, pivots, tags, collision boxes.
- `AnimationAsset`: named clips, frame timing, loop mode, events.
- `TilesetAsset`: tile size, atlas texture, tile metadata, autotile rules, collision flags.
- `MapAsset`: dimensions, tile layers, collision layer, entity placements, triggers, exits, music.
- `EntityData`: archetype, sprite/animation references, stats, collision body, behavior id.
- `ItemData`: name, icon, stack rules, use behavior, equipment slot if needed.
- `DialogueData`: dialogue graph or simple script references.

Use stable asset ids instead of raw file paths in saved data:

```cpp
struct AssetId {
    uint64_t value;
};
```

The asset database maps ids to paths and metadata. This lets files move without breaking every map.

## Sprite Editor

The sprite editor should focus on turning source images into game-ready sprite data.

Core features:

- Import PNG.
- Define frame grid or manually draw frame rectangles.
- Set origin/pivot per sprite or per animation.
- Define hurtboxes, hitboxes, interact boxes, and shadow anchors.
- Preview animations.
- Assign tags such as `idle`, `walk_down`, `attack_left`, `hurt`, `death`.
- Save to `assets/game/sprites/*.sprite.json`.

Keep image editing minimal at first. Full pixel-art editing can become a large project by itself. Start with metadata editing over external PNGs, then add pixel editing only after the game needs it.

## Tile Editor

The tile editor defines how tiles behave.

Core features:

- Import tileset PNG.
- Configure tile width/height.
- Select tiles and assign collision flags.
- Define terrain type such as grass, water, wall, ledge, stairs, pit, bridge.
- Define tile animation frames for water, torches, grass, etc.
- Define autotile rules later, after manual tile painting works.
- Save to `assets/game/tilesets/*.tileset.json`.

Tile collision should be metadata on the tileset, with optional map-level collision overrides.

## Map Editor

The map editor is the most important production tool.

Core features:

- Paint tile layers.
- Edit collision overlay.
- Place entities from archetypes.
- Place triggers, exits, doors, save points, signs, and cutscene markers.
- Preview map using the runtime renderer.
- Toggle gameplay preview.
- Validate missing assets, invalid exits, overlapping blockers, and broken entity references.
- Save to `assets/game/maps/*.map.json`.

Suggested map layers:

```text
ground
detail_low
collision
objects
detail_high
foreground
triggers
entities
```

For SNES/GBA-style action RPGs, support draw sorting by Y position for entities and object layers. This lets characters walk in front of or behind trees, pillars, counters, and NPCs.

## Entity Architecture

Start with a simple component-based model, not a complex ECS framework.

```cpp
struct Entity {
    EntityId id;
    TransformComponent transform;
    SpriteComponent sprite;
    CollisionComponent collision;
    CombatComponent combat;
    BrainComponent brain;
};
```

Not every entity needs every component. A simple registry with vectors and ids is enough at first. Move to a stricter ECS only when the game has enough entities or systems to justify it.

Important early components:

- `TransformComponent`: position, facing, sort offset.
- `SpriteComponent`: sprite id, animation state.
- `CollisionComponent`: body size, solid flag, trigger flag.
- `MovementComponent`: velocity, acceleration, movement mode.
- `CombatComponent`: health, attack definitions, invulnerability timer.
- `InteractionComponent`: prompt, interaction script/dialogue.
- `BrainComponent`: behavior id and local AI state.

## Combat Model

For classic action RPG combat, use authored attack definitions:

```cpp
struct AttackData {
    AssetId animation;
    int damage;
    float startupTime;
    float activeTime;
    float recoveryTime;
    Rect hitbox;
    Vec2 knockback;
    bool canMoveDuringAttack;
};
```

Combat should be based on animation events and temporary hitboxes. Avoid hardcoding sword swings directly in player code. The player, enemies, and bosses should all be able to use the same attack pipeline.

## Editor Commands and Undo

Use a command pattern for editor mutations:

```cpp
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual void execute(EditorContext& ctx) = 0;
    virtual void undo(EditorContext& ctx) = 0;
};
```

Examples:

- `PaintTileCommand`
- `EraseTileCommand`
- `MoveEntityCommand`
- `PlaceTriggerCommand`
- `ChangeSpritePivotCommand`
- `SetTileCollisionCommand`

This keeps undo/redo consistent across sprite, tile, and map editing.

## File Formats

Use readable text formats while the project is young. JSON is a practical default because many libraries support it and diffs are readable enough.

Example map file:

```json
{
  "id": "map_forest_entrance",
  "name": "Forest Entrance",
  "size": [64, 48],
  "tileSize": [16, 16],
  "tilesets": ["tileset_forest"],
  "layers": [],
  "entities": [],
  "triggers": [],
  "exits": []
}
```

Later, if load times or file sizes become a problem, add a packed binary build step for shipping. Keep the editor source files readable.

## Milestone Order

1. Create a window, fixed timestep update, basic renderer, and input wrapper.
2. Load a texture and draw a player sprite.
3. Implement map data, tileset data, and a simple tilemap renderer.
4. Build the map editor viewport with pan/zoom and tile painting.
5. Add collision metadata to tiles and player collision.
6. Add sprite animation data and animation preview.
7. Add entity placement in the map editor.
8. Add player sword attack with hitbox data.
9. Add one enemy with movement, health, and knockback.
10. Add map exits and room transitions.
11. Add dialogue and simple interaction triggers.
12. Add save/load.

Do not start with a full engine framework. Start with the smallest runtime/editor loop that can load data, edit a map, save it, and immediately play it.

## Early Technical Choices

Recommended libraries:

- raylib: windowing, graphics, audio, input.
- Dear ImGui: editor UI.
- rlImGui or equivalent integration layer: ImGui with raylib.
- nlohmann/json or yyjson: readable asset serialization.
- glm or custom small math types: optional, because raylib already has `Vector2`, `Rectangle`, and `Color`.
- Catch2 or doctest: lightweight tests for serialization and game logic.

Recommended rendering setup:

- Internal game resolution: `320x180`, `384x216`, or `400x240`.
- Scale to window using integer scaling where possible.
- Draw world to a render texture.
- Draw UI separately.
- Keep pixel art filtering set to nearest.

For a SNES/GBA-like RPG, `16x16` tiles are a strong default. Use larger sprites such as `16x24`, `24x32`, or `32x32` depending on the character style.

## Things To Avoid Early

- Building a full pixel-art editor before map editing works.
- Adding scripting before the core interaction model is clear.
- Implementing a complex ECS before there is enough gameplay to need it.
- Creating binary-only asset formats too early.
- Mixing ImGui/editor state into game runtime classes.
- Hardcoding player-only combat rules.
- Letting maps reference raw image paths directly.

## Definition Of A Good First Vertical Slice

A good first slice should prove that the architecture works:

- Open the editor.
- Import a tileset.
- Mark wall tiles as solid.
- Paint a small map.
- Place the player spawn.
- Place one enemy.
- Press play in the editor.
- Walk around with collision.
- Attack the enemy.
- Transition to another map.
- Save the project and reload it.

Once that works, the codebase has enough shape to grow into a real action RPG.
