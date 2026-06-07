#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "game/state.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    const std::filesystem::path mapPath = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/game/maps/new_map.admap");

    adventure::game::TileMap map;
    std::string error;
    if (!adventure::game::loadTileMap(mapPath, map, &error)) {
        std::cerr << "Failed to load map: " << error << "\n";
        return 1;
    }

    std::cout << "Loaded map " << map.id << " [" << map.width << "x" << map.height
              << "] spawn [" << map.spawnX << "," << map.spawnY << "]\n";

    adventure::game::MapDoorPlacement smokeDoor;
    smokeDoor.id = "smoke_door";
    smokeDoor.x = 3;
    smokeDoor.y = 4;
    smokeDoor.widthTiles = 2;
    smokeDoor.heightTiles = 1;
    smokeDoor.lockMode = adventure::game::DoorLockMode::RequiresItem;
    smokeDoor.requiredItemId = "brass_key";
    smokeDoor.consumeKey = true;
    smokeDoor.targetScreenId = "screen_2";
    smokeDoor.targetTileX = 6;
    smokeDoor.targetTileY = 7;
    smokeDoor.spriteId = "door_sprite";
    smokeDoor.openingAnimation = "open";
    smokeDoor.openSoundPath = "assets/game/sfx/doors/door_open.ogg";
    smokeDoor.closeSoundPath = "assets/game/sfx/doors/door_close.wav";
    smokeDoor.lockedSoundPath = "assets/game/sfx/doors/door_locked.wav";
    adventure::game::MapObstacle smokeObstacle;
    smokeObstacle.id = "smoke_spike";
    smokeObstacle.type = adventure::game::ObstacleType::TimedSpike;
    smokeObstacle.spriteId = "timed_spikes";
    smokeObstacle.x = 2;
    smokeObstacle.y = 2;
    smokeObstacle.width = 3;
    smokeObstacle.height = 1;
    smokeObstacle.activeSeconds = 1.5f;
    smokeObstacle.inactiveSeconds = 0.5f;
    smokeObstacle.phaseSeconds = 0.25f;
    smokeObstacle.damage = 2;
    smokeObstacle.damageIntervalSeconds = 0.5f;
    adventure::game::MapItemPlacement oneTimeItem;
    oneTimeItem.id = "coin_once";
    oneTimeItem.pickupType = adventure::game::ItemPickupType::ProjectItem;
    oneTimeItem.targetId = "coins";
    oneTimeItem.quantity = 1;
    oneTimeItem.x = 32.0f;
    oneTimeItem.y = 48.0f;
    oneTimeItem.respawn = false;
    oneTimeItem.spriteId = "coin";
    adventure::game::MapItemPlacement respawningItem = oneTimeItem;
    respawningItem.id = "flower_repeat";
    respawningItem.targetId = "flower";
    respawningItem.respawn = true;
    respawningItem.spriteId = "flower";
    adventure::game::TileMap doorMap = map;
    doorMap.doors = {smokeDoor};
    doorMap.obstacles = {smokeObstacle};
    doorMap.items = {oneTimeItem, respawningItem};
    const std::filesystem::path mapSmokePath = "build/smoke_map.admap";
    if (!adventure::game::saveTileMap(mapSmokePath, doorMap, &error)) {
        std::cerr << "Failed to save map smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::TileMap loadedDoorMap;
    if (!adventure::game::loadTileMap(mapSmokePath, loadedDoorMap, &error)) {
        std::cerr << "Failed to load map smoke file: " << error << "\n";
        return 1;
    }
    if (loadedDoorMap.doors.size() != 1 ||
        loadedDoorMap.doors.front().id != "smoke_door" ||
        loadedDoorMap.doors.front().widthTiles != 2 ||
        loadedDoorMap.doors.front().lockMode != adventure::game::DoorLockMode::RequiresItem ||
        loadedDoorMap.doors.front().requiredItemId != "brass_key" ||
        !loadedDoorMap.doors.front().consumeKey ||
        loadedDoorMap.doors.front().targetScreenId != "screen_2" ||
        loadedDoorMap.doors.front().targetTileX != 6 ||
        loadedDoorMap.doors.front().targetTileY != 7 ||
        loadedDoorMap.doors.front().spriteId != "door_sprite" ||
        loadedDoorMap.doors.front().openingAnimation != "open" ||
        loadedDoorMap.doors.front().openSoundPath != "assets/game/sfx/doors/door_open.ogg" ||
        loadedDoorMap.doors.front().closeSoundPath != "assets/game/sfx/doors/door_close.wav" ||
        loadedDoorMap.doors.front().lockedSoundPath != "assets/game/sfx/doors/door_locked.wav") {
        std::cerr << "Map door round-trip values did not match.\n";
        return 1;
    }
    if (loadedDoorMap.obstacles.size() != 1 ||
        loadedDoorMap.obstacles.front().id != "smoke_spike" ||
        loadedDoorMap.obstacles.front().type != adventure::game::ObstacleType::TimedSpike ||
        loadedDoorMap.obstacles.front().spriteId != "timed_spikes" ||
        loadedDoorMap.obstacles.front().x != 2 ||
        loadedDoorMap.obstacles.front().width != 3 ||
        loadedDoorMap.obstacles.front().damage != 2 ||
        loadedDoorMap.obstacles.front().damageIntervalSeconds != 0.5f) {
        std::cerr << "Map obstacle round-trip values did not match.\n";
        return 1;
    }
    if (loadedDoorMap.items.size() != 2 ||
        loadedDoorMap.items[0].id != "coin_once" ||
        loadedDoorMap.items[0].respawn ||
        loadedDoorMap.items[1].id != "flower_repeat" ||
        !loadedDoorMap.items[1].respawn) {
        std::cerr << "Map item respawn round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped map doors, obstacles, and item respawn flags (ADMAP v10)\n";

    const std::filesystem::path chapterPath = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/game/chapters/chapter_1.adchapter");
    adventure::game::Chapter chapter;
    if (!adventure::game::loadChapter(chapterPath, chapter, &error)) {
        std::cerr << "Failed to load chapter: " << error << "\n";
        return 1;
    }

    std::cout << "Loaded chapter " << chapter.id << " [" << chapter.screens.size()
              << " screen(s)] start [" << chapter.startScreenId << "]\n";

    adventure::game::Chapter actionChapter = chapter;
    adventure::game::EnemyPlacement actionEnemy;
    actionEnemy.id = "action_enemy";
    actionEnemy.typeId = "crow";
    actionEnemy.curveMode = adventure::game::PathCurveMode::Spline;
    adventure::game::PathWaypoint enemyEnter;
    enemyEnter.x = -16.0f;
    enemyEnter.y = 64.0f;
    enemyEnter.action = adventure::game::PathWaypointAction::Enter;
    adventure::game::PathWaypoint enemySpeak;
    enemySpeak.x = 64.0f;
    enemySpeak.y = 64.0f;
    enemySpeak.action = adventure::game::PathWaypointAction::Speak;
    enemySpeak.speechDurationSeconds = 1.5f;
    enemySpeak.speechText = "You cannot escape!";
    adventure::game::PathWaypoint enemyLeave;
    enemyLeave.x = 784.0f;
    enemyLeave.y = 64.0f;
    enemyLeave.action = adventure::game::PathWaypointAction::Leave;
    actionEnemy.waypoints = {enemyEnter, enemySpeak, enemyLeave};
    actionChapter.screens.front().enemies.push_back(actionEnemy);

    adventure::game::NpcPlacement actionNpc;
    actionNpc.id = "action_npc";
    actionNpc.typeId = "guide";
    actionNpc.movementOverride = adventure::game::NpcMovementMode::Patrol;
    actionNpc.curveMode = adventure::game::PathCurveMode::Spline;
    actionNpc.waypoints = {enemyEnter, enemySpeak, enemyLeave};
    actionChapter.screens.front().npcs.push_back(actionNpc);

    const std::filesystem::path chapterSmokePath = "build/smoke_chapter.adchapter";
    if (!adventure::game::saveChapter(chapterSmokePath, actionChapter, &error)) {
        std::cerr << "Failed to save chapter action smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::Chapter loadedActionChapter;
    if (!adventure::game::loadChapter(chapterSmokePath, loadedActionChapter, &error)) {
        std::cerr << "Failed to load chapter action smoke file: " << error << "\n";
        return 1;
    }
    const auto& loadedActionEnemy = loadedActionChapter.screens.front().enemies.back();
    const auto& loadedActionNpc = loadedActionChapter.screens.front().npcs.back();
    if (loadedActionEnemy.curveMode != adventure::game::PathCurveMode::Spline ||
        loadedActionEnemy.waypoints.size() != 3 ||
        loadedActionEnemy.waypoints.front().x != -16.0f ||
        loadedActionEnemy.waypoints[1].action != adventure::game::PathWaypointAction::Speak ||
        loadedActionEnemy.waypoints[1].speechDurationSeconds != 1.5f ||
        loadedActionEnemy.waypoints[1].speechText != "You cannot escape!" ||
        loadedActionEnemy.waypoints.back().action != adventure::game::PathWaypointAction::Leave ||
        loadedActionNpc.curveMode != adventure::game::PathCurveMode::Spline) {
        std::cerr << "Chapter waypoint action round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped spline waypoint enter/speak/leave actions (ADCHAPTER v12)\n";

    adventure::game::SpriteMetadata sprite;
    const std::filesystem::path spritePath = "assets/game/sprites/new_sprite.sprite.json";
    if (!adventure::game::loadSpriteMetadata(spritePath, sprite, &error)) {
        // Non-fatal: the default sprite path may not exist; the round-trip checks below
        // are the point of this smoke test.
        std::cout << "Note: skipped optional sprite metadata (" << error << ")\n";
    } else {
        std::cout << "Loaded sprite metadata " << sprite.id << " [" << sprite.frames.size() << " frame(s)]\n";
    }

    adventure::game::EnemyPath path;
    path.id = "smoke_path";
    path.mapId = map.id;
    path.waypoints = {{16.0f, 16.0f}, {32.0f, 16.0f}};
    path.waypoints[1].action = adventure::game::PathWaypointAction::Speak;
    path.waypoints[1].speechDurationSeconds = 2.5f;
    path.waypoints[1].speechText = "Legacy path speech";
    const std::filesystem::path pathSmokePath = "build/smoke_path.adpath";
    if (!adventure::game::saveEnemyPath(pathSmokePath, path, &error)) {
        std::cerr << "Failed to save path smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::EnemyPath loadedPath;
    if (!adventure::game::loadEnemyPath(pathSmokePath, loadedPath, &error)) {
        std::cerr << "Failed to load path smoke file: " << error << "\n";
        return 1;
    }
    if (loadedPath.waypoints.size() != 2 ||
        loadedPath.waypoints[1].action != adventure::game::PathWaypointAction::Speak ||
        loadedPath.waypoints[1].speechDurationSeconds != 2.5f ||
        loadedPath.waypoints[1].speechText != "Legacy path speech") {
        std::cerr << "Path waypoint action round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped path " << loadedPath.id << " [" << loadedPath.waypoints.size() << " waypoint(s)]\n";

    adventure::game::GameState state;
    state.setInt("Example_Count", 11);
    state.addInt("Example_Count", 1);
    state.setBool("Example_Complete", true);
    state.giveItem("example_reward");
    state.markEnemyDefeated("screen_1/crow_1");
    const std::filesystem::path stateSmokePath = "build/smoke_state.adstate";
    if (!adventure::game::saveGameState(stateSmokePath, state, &error)) {
        std::cerr << "Failed to save state smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::GameState loadedState;
    if (!adventure::game::loadGameState(stateSmokePath, loadedState, &error)) {
        std::cerr << "Failed to load state smoke file: " << error << "\n";
        return 1;
    }
    if (loadedState.getInt("Example_Count") != 12 ||
        !loadedState.getBool("Example_Complete") ||
        !loadedState.hasItem("example_reward") ||
        !loadedState.isEnemyDefeated("screen_1/crow_1")) {
        std::cerr << "State smoke round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped game state Example_Count=" << loadedState.getInt("Example_Count")
              << " defeated=" << loadedState.defeatedEnemies().size() << "\n";

    adventure::game::GameProject project;
    project.id = "smoke_project";
    adventure::game::StateVariableDef stateVariable;
    stateVariable.id = "Example_Count";
    stateVariable.type = adventure::game::StateVariableType::Integer;
    project.stateVariables.push_back(stateVariable);
    adventure::game::GameEffectDef effectDef;
    effectDef.id = "increment_example";
    effectDef.type = adventure::game::GameEffectType::AddInt;
    effectDef.targetId = "Example_Count";
    effectDef.intValue = 1;
    project.effectDefs.push_back(effectDef);
    adventure::game::EnemyType enemyType;
    enemyType.id = "crow";
    enemyType.knockbackResistance = 0.25f;
    enemyType.hitstunSeconds = 0.2f;
    enemyType.aggroRange = 120.0f;
    enemyType.killVariable = "Crows_Killed";
    enemyType.killAmount = 1;
    project.enemyTypes.push_back(enemyType);
    adventure::game::WeaponDef weaponDef;
    weaponDef.id = "slingshot";
    weaponDef.type = adventure::game::WeaponType::Ranged;
    weaponDef.wallBehavior = adventure::game::ProjectileWallBehavior::Rebound;
    project.weaponDefs.push_back(weaponDef);
    const std::filesystem::path projectSmokePath = "build/smoke_project.adgame";
    if (!adventure::game::saveGameProject(projectSmokePath, project, &error)) {
        std::cerr << "Failed to save project smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::GameProject loadedProject;
    if (!adventure::game::loadGameProject(projectSmokePath, loadedProject, &error)) {
        std::cerr << "Failed to load project smoke file: " << error << "\n";
        return 1;
    }
    if (loadedProject.stateVariables.size() != 1 ||
        loadedProject.effectDefs.size() != 1 ||
        loadedProject.effectDefs.front().targetId != "Example_Count") {
        std::cerr << "Project state/effect definition round-trip values did not match.\n";
        return 1;
    }
    if (loadedProject.enemyTypes.size() != 1 ||
        loadedProject.enemyTypes.front().killVariable != "Crows_Killed" ||
        loadedProject.enemyTypes.front().aggroRange != 120.0f ||
        loadedProject.enemyTypes.front().knockbackResistance != 0.25f) {
        std::cerr << "Enemy type combat-feel field round-trip values did not match.\n";
        return 1;
    }
    if (loadedProject.weaponDefs.size() != 1 ||
        loadedProject.weaponDefs.front().wallBehavior != adventure::game::ProjectileWallBehavior::Rebound) {
        std::cerr << "Weapon projectile wall-behavior round-trip did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped project state/effect/enemy-type/weapon definitions (ADGAME v13)\n";
    return 0;
}
