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
- Music exclusivity: move repeatedly between screens with different music, the same music, and no music. Confirm the previous track stops before a replacement starts, the same track is not doubled/restarted unnecessarily, silence is respected, and door SFX still mix over the single active track.
- Runtime display scaling: open the Escape display menu and test Windowed 1x, Windowed 2x, and Fullscreen 2x. Confirm the playfield remains crisp and proportional, fullscreen is centred at 1536 x 1080 with black side bars on a 1920 x 1080 monitor, F11 toggles fullscreen, and selecting Quit Game closes cleanly. Then resize the window larger and confirm the playfield grows to the largest integer scale that fits (no fixed 2x cap) and stays centred/letterboxed without stretching.
- Gamepad menu: with a controller connected, press Select/Back and confirm it opens/closes the display menu (same as Esc); confirm the inventory still opens on North (Y)/Start.
- Door over wall art: give a door a sprite, paint Wall pixels across the doorway, and confirm in game the door sprite renders above the wall layer (the opening stays visible) and still slides correctly during a screen transition.
- Linked door pairing: create a door, set its Target screen, and Apply. Confirm a paired door is auto-created at the centre of the target screen, both doors reference each other (targetDoorId), and reopening either screen shows the linked door. Drag the paired door somewhere specific, launch, cross through, and confirm the player spawns at the paired door stepped one tile inward. Confirm a door with no paired door still uses the fallback Target Tile X/Y, and that v10 maps load and upgrade to v11 on save.
- Path actions: test an off-screen Enter waypoint, timed Speak waypoint, and off-screen Leave waypoint for both an NPC spline and enemy path.
- Sprite tools: draw and close/cancel a polygon, then reorder frames by drag and Earlier/Later; confirm layers, frame metadata, undo, and saved order remain aligned.
- Sprite resize and tile guide: open Resize, set a tile count under "Size in tiles" and confirm "Dimensions (px)" updates to tiles x 16 (and vice-versa); resize with Keep pixels and Scale pixels and confirm art is centred/cropped vs rescaled. Toggle the "Tiles" overlay and confirm brighter tile-boundary lines appear every 16px independent of the per-pixel Grid.
- Projectile ammo size: author an ammo sprite larger than one tile (e.g. 32x32), fire the ranged weapon, and confirm the in-flight projectile matches the ammo pickup's on-screen size (not oversized) with aspect ratio preserved.
- Screen graphics shortcuts: with the canvas focused (no text field active), press B/E/D/G/U/F/L/R/S/I and confirm each selects the matching tool, `[`/`]` change the brush size, and the tool buttons show their key on hover. Confirm Ctrl+C/Ctrl+V/Ctrl+Z and Esc still behave and that the single-key shortcuts do not fire while typing in a field.
- Project folders: create a project and confirm the declared `assets/raw`, `assets/game`, `assets/game/sfx/doors`, and chapter dialogue folders exist.
- Obstacle hazard: place Spike/Pit/Timed Spike hazards and confirm active hazards respawn the player at the map spawn.
- Animated tile stacks: stamp three different animations into Stack 1-3 at the same cell in both Floor and Overlay bands. Save/reload and launch; confirm higher stacks render over lower stacks, Floor remains below actors, Overlay remains above walls, right-click removes only the selected slot, and Tile Erase removes all slots at the cell.
- Screen graphics paste/stamp reference: copy a non-square selection and select a multi-tile palette stamp. For each, press `Ctrl+V` repeatedly and confirm the control label and yellow ghost marker cycle top-left → top-right → bottom-right → bottom-left, then confirm placement matches the preview at every corner.
- Screen graphics Smudge: drag across two contrasting colors at several strengths and confirm the color is carried in the drag direction, undo restores the stroke, transparent source pixels do not erase, and every opaque output color belongs to the 128-color Atari palette. Repeat with Spray and Dither and confirm density feathers at the brush edge.
- Screen graphics brush range/runtime guide: confirm Brush accepts values through 32 and large Pencil/Smudge/Spray/Dither strokes remain clipped to the canvas. Launch the game and confirm structural collision tiles no longer show the faint yellow debug overlay.
- Screen graphics Lighten/Darken graduation: compare Graduation at 0%, 50%, and 100% using the same brush size and Intensity. Confirm 0% has a hard circular edge, larger values widen the dithered feather, and changing Graduation does not change the adjustment strength in the solid center.
- Atari color selectors: in Screen Graphics and Sprite color popups, click Transparent and the first black swatch repeatedly. Confirm each selects correctly without a Dear ImGui duplicate-ID/program error.
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
