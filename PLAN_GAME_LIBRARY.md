# Game Library And Chapter Usage Plan

## Goal

Separate reusable game assets from chapter-specific usage.

## Model

- Project/game library owns canonical reusable assets:
  - characters
  - enemies
  - items
  - sprites and animation frame assignments
  - maps and graphical assets
- Chapters own usage:
  - imported character ids
  - playable character id for the chapter
  - screen layout and map references
  - future enemy/item placements and chapter-specific overrides

## Initial Implementation Slice

1. Add `assets/game/project.adgame`.
   - Stores available character ids.
   - Stores the project default playable character id.
2. Extend `.adchapter`.
   - Stores imported character ids.
   - Stores playable character id used by the chapter.
3. Keep `.adcharacter` files as reusable library assets.
   - Character sheet edits update the library asset.
   - Chapters reference character ids rather than copying character data.
4. Runtime loading.
   - Load chapter.
   - Resolve playable character id from chapter.
   - Load that character document from the game library.
   - Load the selected frame image for the playable character.

## Next Slices

- Add enemy and item library documents.
- Add chapter placement records for enemies/items.
- Add explicit import/remove controls in chapter UI.
- Add per-chapter overrides only where needed.
- Add validation for missing imported assets before launch.
