# High-Level Functionality Audit Plan

This checklist is for catching feature gaps that are easy to miss when testing one screen,
panel, or data type in isolation. Use it whenever a feature feels "implemented" at the
editor level but may not yet be complete across authoring, persistence, runtime, UI, and docs.

## Audit Principle

Every gameplay feature should have a full loop:

1. Author it in the editor.
2. Save it to the right project/chapter/map file.
3. Reload it in the editor without data loss.
4. Load it in the runtime from the selected project, not another project's assets.
5. Interact with it in-game using keyboard and controller input where applicable.
6. Show clear runtime feedback when it succeeds, fails, or is unavailable.
7. Update state, inventory, money, health, position, or persistence consistently.
8. Survive screen transitions, test launches, fresh launches, and continue launches as designed.
9. Be documented in `docs/RPG_Spec.md`, `docs/code_base.md`, and the manual when user-facing.
10. Have a smoke/manual test path that exercises the whole loop.

## Priority Passes

### 1. Editor -> File -> Runtime Contract

Goal: confirm editor-authored data is actually used by the game.

- For each editor panel, list the file(s) it writes and the runtime code that reads them.
- Check version numbers and backwards-compatible load behavior after each format change.
- Verify save/discard/back actions do not silently drop contextual edits.
- Confirm project-root lookup is used everywhere and cannot leak assets between projects.
- Add missing runtime consumption for editor-only data.

Completed pass: doors now have editor support, `.admap` v6 save/load, runtime prompt/transition
behavior, key checks, optional key consumption, blocked-destination feedback, and smoke coverage.

### 2. Inventory, Money, and Item Semantics

Goal: make item behavior predictable from editor fields to runtime UI.

- Distinguish string IDs from numeric values in all item/state/shop fields.
- Verify currency pickup updates the intended money variable, with `Money` as the default.
- Confirm shop purchases update player inventory immediately and limited stock correctly.
- Confirm ammo is only represented in inventory, not split between legacy ammo pools and item stacks.
- Check buy, sell, pickup, use, consume, and quest-action paths all use the same inventory helpers.
- Ensure inventory hides currency as an item row but still shows money in the header.

### 3. Screen Interaction Types

Goal: avoid adding isolated screen tools that do not compose.

- Treat walls, floors, animated tiles, obstacles, doors, NPCs, enemies, and pickups as separate screen feature layers.
- Each layer needs selection, add/edit/delete, save/load, visual canvas feedback, and runtime behavior.
- For trigger-like features, define whether they block movement, trigger on overlap, require interaction, or both.
- For locked/interactable features, define success/failure feedback and required item behavior.

Completed pass: free-use destination doors are walkable interaction triggers; locked and required-item doors are collision blockers. Required-item doors without destinations unlock in place. Locked, missing-key, invalid free-use destination, and blocked-destination cases show runtime feedback.

### 4. Runtime Feedback and Failure States

Goal: players should know why something did or did not happen.

- Add feedback for failed purchases, missing keys, locked doors, unusable items, empty ammo, full health, and blocked transitions.
- Check prompts appear only when the player can act.
- Ensure modal overlays pause only the systems they should pause.
- Verify left/right/up/down navigation works consistently in shops, inventory, and dialogue choices.

### 5. Persistence and Test Launch Modes

Goal: avoid confusing differences between editor testing and real saves.

- Verify every state mutation is correct for fresh launches, selected-screen tests, last-entry tests, and `--continue`.
- Confirm `--fresh` does not overwrite `save.adstate` but can still update `test_checkpoint`.
- Test screen transitions with inventory, money, defeated enemies, dialogue actions, pickups, and shop stock.
- Decide which data is per-session, per-save, per-chapter, per-screen, or project-wide.

### 6. Data Validation and Editor Guidance

Goal: prevent field misunderstandings before runtime.

- Validate missing IDs, duplicate IDs, invalid target IDs, negative prices, empty required items, and broken screen links.
- Use distinct visual treatment for string fields versus numeric fields.
- Prefer pickers/dropdowns for known IDs where practical.
- Add inline warnings where a value is saved but ignored due to another setting.

Example: shop stock on an NPC placement is ignored unless the NPC type interaction is `Shop`.

### 7. Performance and Rendering Budget

Goal: keep editor responsiveness while feature layers grow.

- Avoid per-pixel draw calls in editor canvases when a texture or batched runs will do.
- Clip draw work to visible canvas regions.
- Cache sprite metadata, thumbnails, preview PNG metadata, and decoded images.
- Check large chapter grids and screen-graphics editing after each new overlay layer.
- Keep runtime fixed-step update cost independent of editor-only data.

### 8. Documentation and Test Evidence

Goal: keep human and LLM docs aligned with code.

- Update `docs/RPG_Spec.md` for feature behavior and known planned gaps.
- Update `docs/code_base.md` for file formats, panel ownership, runtime readers, and status matrix.
- Update `docs/manual/index.html` and `docs/manual/llms.txt` for user-facing workflows.
- For each feature, record at least one manual test recipe: editor steps, save/reload, runtime action, expected result.
- Run `adventure_editor_smoke` and a project-specific `adventure_game_smoke` when code changes touch load/save/runtime.

## Suggested Next Tightening Order

1. Screen feature layer audit: walls/floors/animated tiles/obstacles/doors/items/NPCs/enemies all visible and editable without mode confusion.
2. Inventory persistence upgrade: add stack counts to a future `.adstate` version if exact quantities need to survive `--continue`.
3. Validation expansion: add sprite-file existence checks and broken dialogue action target checks.
4. Door polish: play the stored opening animation field before/while transitioning.
5. Manual test checklist: add a small project-level test script/checklist that covers money pickup, shop purchase, ammo use, obstacle respawn, animated tile rendering, and doors.
