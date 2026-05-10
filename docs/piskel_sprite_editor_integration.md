# Piskel-Based Sprite Editor Integration

Piskel is a browser-based pixel art and sprite animation editor. The editor in this project should use Piskel as the product baseline for sprite workflow, but implement the UI natively in Dear ImGui so it stays inside the C++ game editor.

## Piskel Functionality To Mirror

- Pixel drawing tools: pen, mirror pen, eraser, stroke, rectangle, circle, paint bucket, color picker, move, selection, lighten/darken.
- Animation workflow: ordered frames, duplicate/delete frame actions, frame durations, onion-skin preview, and playback preview.
- Layer workflow: named layers with visibility and opacity controls.
- Palette workflow: editable swatches, palette import/export, and reuse across sprites.
- Export workflow: save a still PNG, animated GIF/spritesheet later, and game metadata immediately.

## Local Asset Contract

- Source PNGs belong in `assets/raw/sprites`.
- Runtime sprite metadata belongs in `assets/game/sprites/*.sprite.json`.
- Animation clips derived from sprite frames belong in `assets/game/animations`.
- Shared palettes belong in `assets/game/palettes`.
- Tileset editing should stay separate and use `assets/raw/tilesets` and `assets/game/tilesets`.

The sprite tab should never save game-ready metadata into `assets/raw`. Raw assets are editable source material; `assets/game` is the runtime-facing output tree.

## Current Implementation

The first native integration is in `src/editor/panels/sprite_editor_panel.*` and is hosted from the `Sprites` tab in `src/editor/editor_app.cpp`.

It currently provides the editor shell for:

- Piskel-like tool selection.
- Grid and onion-skin toggles.
- Frame list and timing metadata.
- Layer list with visibility and opacity.
- Palette editing.
- Metadata export to `assets/game/sprites/<id>.sprite.json`.

Pixel painting, PNG loading, spritesheet/GIF export, undo/redo, and animation playback should be implemented on top of this panel once renderer, texture, and command systems exist.
