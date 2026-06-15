# Manual High-Level Test Checklist

Run this against a project chapter after feature work that touches runtime/editor contracts.
The goal is to test authored data all the way through save, reload, runtime behavior, and
state changes.

## Setup

1. Open the project in the editor.
2. Select the screen being tested.
3. Save the chapter before launching.
4. Use `Play Selected Screen` for isolated checks and `Save and Play Game` for full chapter checks.

## Core Loops

- Money pickup: place a Currency project item pickup, collect it, and confirm the inventory money header increases.
- Item respawn: collect an unchecked coin, leave and return, and confirm it stays gone; collect a checked flower, reload the screen, and confirm it returns. Continue a save and confirm the coin remains collected.
- Shop purchase: interact with a Shop NPC, buy multiple items with the quantity prompt, and confirm money decreases, limited stock decreases, and the purchased item appears in the player inventory.
- Shop sell: sell an inventory item, confirm money increases and limited shop stock increments when applicable.
- Ammo use: pick up or buy Ammo, confirm it appears in the AMMO section, fire the matching ranged weapon, and confirm the ammo stack decreases.
- Dialogue reward: use a dialogue Action node to give an item/key, then confirm it appears in inventory and can satisfy item ownership checks.
- Door destination: create a Free Use door with a valid target screen/tile, interact, and confirm the player transitions to the destination.
- Door destination with key: create a Requires Item door with a destination, collect the matching item ID, interact, and confirm transition. If `Consume Key` is enabled, confirm one key leaves inventory.
- Door failure feedback: test permanent Locked, missing-key, Free Use with missing destination, and blocked-target-tile doors and confirm the correct notice/SFX.
- Door collision: confirm Locked and Requires Item rectangles cannot be walked through, can still be interacted with from beside the rectangle, and Free Use doors retain their walkable trigger behavior.
- Same-screen key door: leave Target Screen empty, unlock with the required item, confirm collision is removed and no destination warning appears, then enter its tiles and confirm an unanimated door disappears and remains unlocked after returning/continuing.
- Door SFX: choose `.ogg`/`.wav` Open, Close, and Locked assets from `assets/game/sfx/doors`; confirm Open plays on successful activation, Close after transition, and Locked for permanent-lock or missing-key failures.
- Path actions: test an off-screen Enter waypoint, timed Speak waypoint, and off-screen Leave waypoint for both an NPC spline and enemy path.
- Sprite tools: draw and close/cancel a polygon, then reorder frames by drag and Earlier/Later; confirm layers, frame metadata, undo, and saved order remain aligned.
- Project folders: create a project and confirm the declared `assets/raw`, `assets/game`, `assets/game/sfx/doors`, and chapter dialogue folders exist.
- Obstacle hazard: place Spike/Pit/Timed Spike hazards and confirm active hazards respawn the player at the map spawn.
- Animated tile: stamp an animated tile, save, launch, and confirm the animation renders in the correct floor/wall layer.
- Screen graphics paste/stamp reference: copy a non-square selection and select a multi-tile palette stamp. For each, press `Ctrl+V` repeatedly and confirm the control label and yellow ghost marker cycle top-left → top-right → bottom-right → bottom-left, then confirm placement matches the preview at every corner.
- Screen graphics Smudge: drag across two contrasting colors at several strengths and confirm the color is carried in the drag direction, undo restores the stroke, transparent source pixels do not erase, and every opaque output color belongs to the 128-color Atari palette. Repeat with Spray and Dither and confirm density feathers at the brush edge.
- Screen graphics animated-tile visibility: place an animated tile over painted floor pixels, clear `Animated tiles`, edit the pixels beneath its footprint, re-enable the overlay, save/reload, and confirm both the underlying art and animated placement remain intact.
- Screen transition persistence: cross a screen edge or door, return, and confirm defeated enemies, inventory, money, and quest state behave according to fresh/continue launch rules.

## Editor Validation

- Door editor warns for duplicate/empty IDs, missing required items, invalid Free Use destinations, blocked target tiles, and unloaded target maps.
- Item placement warns for empty/duplicate placement IDs, missing weapon IDs, missing Ammo items, and missing project item IDs.
- NPC shop editors warn when shop rows reference missing item IDs.
- Item definitions warn on empty and duplicate IDs.

## Command Verification

```sh
cmake --build build --parallel
./build/adventure_editor_smoke
./build/adventure_game_smoke projects/Billys_Crow_Hunt/assets/game/maps/screen_1_map.admap projects/Billys_Crow_Hunt/assets/game/chapters/Farm_House.adchapter
```
