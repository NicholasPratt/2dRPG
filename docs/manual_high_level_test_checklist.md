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
- Shop purchase: interact with a Shop NPC, buy multiple items with the quantity prompt, and confirm money decreases, limited stock decreases, and the purchased item appears in the player inventory.
- Shop sell: sell an inventory item, confirm money increases and limited shop stock increments when applicable.
- Ammo use: pick up or buy Ammo, confirm it appears in the AMMO section, fire the matching ranged weapon, and confirm the ammo stack decreases.
- Dialogue reward: use a dialogue Action node to give an item/key, then confirm it appears in inventory and can satisfy item ownership checks.
- Door free use: create a free-use door with a valid target screen/tile, interact with it, and confirm the player transitions to the destination.
- Door key use: create a Requires Item door, collect or receive the key, interact, and confirm the door opens. If `Consume Key` is enabled, confirm the key leaves inventory.
- Door failure feedback: test Locked, missing-key, missing-destination, and blocked-target-tile doors and confirm a short feedback notice appears.
- Obstacle hazard: place Spike/Pit/Timed Spike hazards and confirm active hazards respawn the player at the map spawn.
- Animated tile: stamp an animated tile, save, launch, and confirm the animation renders in the correct floor/wall layer.
- Screen transition persistence: cross a screen edge or door, return, and confirm defeated enemies, inventory, money, and quest state behave according to fresh/continue launch rules.

## Editor Validation

- Door editor warns for duplicate/empty IDs, missing required items, missing target screens, blocked target tiles, and unloaded target maps.
- Item placement warns for missing weapon IDs, missing Ammo items, and missing project item IDs.
- NPC shop editors warn when shop rows reference missing item IDs.
- Item definitions warn on empty and duplicate IDs.

## Command Verification

```sh
cmake --build build --parallel
./build/adventure_editor_smoke
./build/adventure_game_smoke projects/Billys_Crow_Hunt/assets/game/maps/screen_1_map.admap projects/Billys_Crow_Hunt/assets/game/chapters/Farm_House.adchapter
```
