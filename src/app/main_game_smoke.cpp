#include "game/chapter.hpp"
#include "game/dialogue_graph.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "game/state.hpp"

#include <filesystem>
#include <fstream>
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
    smokeDoor.targetDoorId = "door_back_to_1";
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
    adventure::game::MapChapterExitPlacement smokeExit;
    smokeExit.id = "to_next_chapter";
    smokeExit.x = 8;
    smokeExit.y = 9;
    smokeExit.widthTiles = 3;
    smokeExit.heightTiles = 2;
    smokeExit.activation = adventure::game::ChapterExitActivation::EnterAreaAndCondition;
    smokeExit.condition.type = adventure::game::GameConditionType::IntCompare;
    smokeExit.condition.op = adventure::game::GameCompareOp::GreaterOrEqual;
    smokeExit.condition.variableId = "Crows_killed";
    smokeExit.condition.scope = adventure::game::StateVariableScope::Chapter;
    smokeExit.condition.intValue = 10;
    smokeExit.targetChapterId = "Town";
    smokeExit.targetScreenId = "town_gate";
    smokeExit.targetTileX = 4;
    smokeExit.targetTileY = 6;
    smokeExit.oneShot = true;
    smokeExit.transitionSoundPath = "assets/game/sfx/doors/chapter_exit.wav";
    doorMap.doors = {smokeDoor};
    doorMap.chapterExits = {smokeExit};
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
        loadedDoorMap.doors.front().targetDoorId != "door_back_to_1" ||
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
    if (loadedDoorMap.chapterExits.size() != 1 ||
        loadedDoorMap.chapterExits.front().id != "to_next_chapter" ||
        loadedDoorMap.chapterExits.front().activation != adventure::game::ChapterExitActivation::EnterAreaAndCondition ||
        loadedDoorMap.chapterExits.front().condition.type != adventure::game::GameConditionType::IntCompare ||
        loadedDoorMap.chapterExits.front().condition.op != adventure::game::GameCompareOp::GreaterOrEqual ||
        loadedDoorMap.chapterExits.front().condition.scope != adventure::game::StateVariableScope::Chapter ||
        loadedDoorMap.chapterExits.front().condition.intValue != 10 ||
        loadedDoorMap.chapterExits.front().targetChapterId != "Town" ||
        loadedDoorMap.chapterExits.front().targetScreenId != "town_gate" ||
        !loadedDoorMap.chapterExits.front().oneShot) {
        std::cerr << "Map chapter exit round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped map doors, chapter exits, obstacles, and items (ADMAP v13)\n";

    const std::filesystem::path chapterPath = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/game/chapters/chapter_1.adchapter");
    adventure::game::Chapter chapter;
    if (!adventure::game::loadChapter(chapterPath, chapter, &error)) {
        std::cerr << "Failed to load chapter: " << error << "\n";
        return 1;
    }

    std::cout << "Loaded chapter " << chapter.id << " [" << chapter.screens.size()
              << " screen(s)] start [" << chapter.startScreenId << "]\n";

    adventure::game::Chapter actionChapter = chapter;
    actionChapter.screens.front().walkingSfxPath = "assets/game/sfx/walking/grass.wav";
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
    enemySpeak.actionRepeatLimit = 1;
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

    adventure::game::AnimatedTilePlacement lowerAnimation;
    lowerAnimation.spriteId = "smoke_lower";
    lowerAnimation.cellX = 4;
    lowerAnimation.cellY = 5;
    lowerAnimation.layer = 0;
    lowerAnimation.stack = 0;
    adventure::game::AnimatedTilePlacement upperAnimation = lowerAnimation;
    upperAnimation.spriteId = "smoke_upper";
    upperAnimation.stack = 2;
    actionChapter.screens.front().animatedTiles.push_back(lowerAnimation);
    actionChapter.screens.front().animatedTiles.push_back(upperAnimation);

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
        loadedActionEnemy.waypoints[1].actionRepeatLimit != 1 ||
        loadedActionEnemy.waypoints.back().action != adventure::game::PathWaypointAction::Leave ||
        loadedActionChapter.screens.front().walkingSfxPath != "assets/game/sfx/walking/grass.wav" ||
        loadedActionNpc.curveMode != adventure::game::PathCurveMode::Spline ||
        loadedActionChapter.screens.front().animatedTiles.size() <
            actionChapter.screens.front().animatedTiles.size() ||
        loadedActionChapter.screens.front().animatedTiles[
            loadedActionChapter.screens.front().animatedTiles.size() - 2].stack != 0 ||
        loadedActionChapter.screens.front().animatedTiles.back().stack != 2) {
        std::cerr << "Chapter waypoint action round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped waypoint actions, repeat limits, animation stacks, and walking SFX (ADCHAPTER v15)\n";

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
    path.waypoints[1].actionRepeatLimit = 2;
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
        loadedPath.waypoints[1].speechText != "Legacy path speech" ||
        loadedPath.waypoints[1].actionRepeatLimit != 2) {
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
    state.setLocation("Farm_House", "screen_7", 123.5f, 88.25f);
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
        !loadedState.isEnemyDefeated("screen_1/crow_1") ||
        !loadedState.hasLocation() ||
        loadedState.chapterId() != "Farm_House" ||
        loadedState.screenId() != "screen_7" ||
        loadedState.playerX() != 123.5f ||
        loadedState.playerY() != 88.25f) {
        std::cerr << "State smoke round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped game state Example_Count=" << loadedState.getInt("Example_Count")
              << " defeated=" << loadedState.defeatedEnemies().size() << "\n";

    adventure::game::GameCondition smokeCondition;
    smokeCondition.type = adventure::game::GameConditionType::IntCompare;
    smokeCondition.variableId = "Example_Count";
    smokeCondition.op = adventure::game::GameCompareOp::GreaterOrEqual;
    smokeCondition.intValue = 12;
    if (!adventure::game::gameConditionPasses(smokeCondition, loadedState, "Farm_House")) {
        std::cerr << "Integer game condition did not pass.\n";
        return 1;
    }
    smokeCondition.type = adventure::game::GameConditionType::BoolEquals;
    smokeCondition.variableId = "Example_Complete";
    smokeCondition.boolValue = true;
    if (!adventure::game::gameConditionPasses(smokeCondition, loadedState, "Farm_House")) {
        std::cerr << "Boolean game condition did not pass.\n";
        return 1;
    }
    smokeCondition.type = adventure::game::GameConditionType::HasItem;
    smokeCondition.variableId = "example_reward";
    if (!adventure::game::gameConditionPasses(smokeCondition, loadedState, "Farm_House")) {
        std::cerr << "Item game condition did not pass.\n";
        return 1;
    }
    const std::filesystem::path legacyStatePath = "build/smoke_state_v2.adstate";
    {
        std::ofstream legacy(legacyStatePath);
        legacy << "ADSTATE 2\nints 1\nint Legacy_Count 3\nbools 0\nitems 0\ndefeated 0\nend\n";
    }
    adventure::game::GameState legacyState;
    if (!adventure::game::loadGameState(legacyStatePath, legacyState, &error) ||
        legacyState.getInt("Legacy_Count") != 3 || legacyState.hasLocation()) {
        std::cerr << "Legacy ADSTATE v2 compatibility failed.\n";
        return 1;
    }

    loadedState.setInt("chapter.Farm_House.Crows_killed", 7);
    loadedState.setBool("Quest_Complete", true);
    const std::string interpolated = adventure::game::interpolateGameStateText(
        "You killed $Crows_killed crows. Complete: $Quest_Complete. Reward: $$5.",
        loadedState, "Farm_House");
    if (interpolated != "You killed 7 crows. Complete: true. Reward: $5.") {
        std::cerr << "Game state text interpolation did not match: " << interpolated << "\n";
        return 1;
    }
    std::cout << "Interpolated chapter and universal game variables in dialogue text\n";

    adventure::game::DialogueGraph graph;
    graph.id = "scope_smoke";
    graph.startNodeId = "start";
    adventure::game::DialogueNode startNode;
    startNode.id = "start";
    startNode.type = adventure::game::DialogueNodeType::Condition;
    startNode.nextNodeId = "end";
    startNode.falseNodeId = "end";
    startNode.condition.type = adventure::game::DialogueConditionType::BoolEquals;
    startNode.condition.variableId = "Spoke_To_Grandma";
    startNode.condition.scope = adventure::game::StateVariableScope::Chapter;
    adventure::game::DialogueAction scopedAction;
    scopedAction.type = adventure::game::DialogueActionType::SetBool;
    scopedAction.targetId = "Spoke_To_Grandma";
    scopedAction.scope = adventure::game::StateVariableScope::Chapter;
    startNode.actions.push_back(scopedAction);
    adventure::game::DialogueNode endNode;
    endNode.id = "end";
    endNode.type = adventure::game::DialogueNodeType::End;
    graph.nodes = {startNode, endNode};
    const std::filesystem::path graphSmokePath = "build/scope_smoke.addialogue";
    if (!adventure::game::saveDialogueGraph(graphSmokePath, graph, &error)) {
        std::cerr << "Failed to save dialogue scope smoke file: " << error << "\n";
        return 1;
    }
    adventure::game::DialogueGraph loadedGraph;
    if (!adventure::game::loadDialogueGraph(graphSmokePath, loadedGraph, &error) ||
        loadedGraph.nodes.empty() ||
        loadedGraph.nodes.front().condition.scope != adventure::game::StateVariableScope::Chapter ||
        loadedGraph.nodes.front().actions.empty() ||
        loadedGraph.nodes.front().actions.front().scope != adventure::game::StateVariableScope::Chapter) {
        std::cerr << "Dialogue scoped condition/action round-trip values did not match.\n";
        return 1;
    }

    adventure::game::GameProject project;
    project.id = "smoke_project";
    adventure::game::StateVariableDef stateVariable;
    stateVariable.id = "Example_Count";
    stateVariable.type = adventure::game::StateVariableType::Integer;
    stateVariable.scope = adventure::game::StateVariableScope::Chapter;
    stateVariable.chapterId = "chapter_smoke";
    project.stateVariables.push_back(stateVariable);
    adventure::game::GameEffectDef effectDef;
    effectDef.id = "increment_example";
    effectDef.type = adventure::game::GameEffectType::AddInt;
    effectDef.targetId = "Example_Count";
    effectDef.intValue = 1;
    effectDef.scope = adventure::game::StateVariableScope::Chapter;
    project.effectDefs.push_back(effectDef);
    adventure::game::EnemyType enemyType;
    enemyType.id = "crow";
    enemyType.knockbackResistance = 0.25f;
    enemyType.hitstunSeconds = 0.2f;
    enemyType.aggroRange = 120.0f;
    enemyType.killVariable = "Crows_Killed";
    enemyType.killAmount = 1;
    enemyType.killVariableScope = adventure::game::StateVariableScope::Chapter;
    enemyType.defeatEffectIds = {"increment_example"};
    project.enemyTypes.push_back(enemyType);
    adventure::game::ItemDef itemDef;
    itemDef.id = "crow_feather";
    itemDef.name = "Crow Feather";
    itemDef.type = adventure::game::ItemDefType::Quest;
    itemDef.acquireEffectIds = {"increment_example"};
    project.itemDefs.push_back(itemDef);
    adventure::game::NpcTypeDef npcDef;
    npcDef.id = "grandma";
    npcDef.talkEffectIds = {"increment_example"};
    adventure::game::NpcStateRule npcRule;
    npcRule.condition.type = adventure::game::DialogueConditionType::IntCompare;
    npcRule.condition.variableId = "Example_Count";
    npcRule.condition.scope = adventure::game::StateVariableScope::Chapter;
    npcRule.condition.intValue = 3;
    npcRule.graphId = "quest_complete";
    npcRule.movementOverride = static_cast<int>(adventure::game::NpcMovementMode::Patrol);
    npcRule.animation = "victory";
    npcRule.visibility = 1;
    npcRule.activateEffectIds = {"increment_example"};
    npcDef.stateRules.push_back(npcRule);
    project.npcTypes.push_back(npcDef);
    adventure::game::WeaponDef weaponDef;
    weaponDef.id = "slingshot";
    weaponDef.type = adventure::game::WeaponType::Ranged;
    weaponDef.wallBehavior = adventure::game::ProjectileWallBehavior::Rebound;
    weaponDef.chargeTimeSeconds = 1.2f;
    weaponDef.chargeDamageScaleMin = 0.5f;
    weaponDef.chargeDamageScaleMax = 2.0f;
    weaponDef.overchargeTimeSeconds = 2.0f;
    weaponDef.overchargeEffect = adventure::game::OverchargeEffect::WildShot;
    weaponDef.spreadStartDegrees = 4.0f;
    weaponDef.spreadEndDegrees = 1.0f;
    weaponDef.steadyTimeSeconds = 1.2f;
    weaponDef.pelletCount = 1;
    weaponDef.falloffStartPx = 48.0f;
    weaponDef.falloffEndPx = 160.0f;
    weaponDef.falloffMinDamageScale = 0.25f;
    weaponDef.aimConeDegrees = 40.0f;
    weaponDef.attackAnimState = "attack_20";
    project.weaponDefs.push_back(weaponDef);
    project.chapterIds.push_back("chapter_smoke");
    adventure::game::ChapterSynopsisDef synopsis;
    synopsis.chapterId = "chapter_smoke";
    synopsis.text = "The hero reaches the farm.\nA crow steals the key.";
    project.chapterSynopses.push_back(synopsis);
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
        loadedProject.effectDefs.front().targetId != "Example_Count" ||
        loadedProject.effectDefs.front().scope != adventure::game::StateVariableScope::Chapter ||
        loadedProject.stateVariables.front().scope != adventure::game::StateVariableScope::Chapter ||
        loadedProject.stateVariables.front().chapterId != "chapter_smoke" ||
        loadedProject.chapterSynopses.size() != 1 ||
        loadedProject.chapterSynopses.front().chapterId != "chapter_smoke" ||
        loadedProject.chapterSynopses.front().text != synopsis.text) {
        std::cerr << "Project state/effect definition round-trip values did not match.\n";
        return 1;
    }
    if (loadedProject.enemyTypes.size() != 1 ||
        loadedProject.enemyTypes.front().killVariable != "Crows_Killed" ||
        loadedProject.enemyTypes.front().killVariableScope != adventure::game::StateVariableScope::Chapter ||
        loadedProject.enemyTypes.front().defeatEffectIds != std::vector<std::string>{"increment_example"} ||
        loadedProject.enemyTypes.front().aggroRange != 120.0f ||
        loadedProject.enemyTypes.front().knockbackResistance != 0.25f) {
        std::cerr << "Enemy type combat-feel field round-trip values did not match.\n";
        return 1;
    }
    if (loadedProject.itemDefs.size() != 1 ||
        loadedProject.itemDefs.front().acquireEffectIds != std::vector<std::string>{"increment_example"} ||
        loadedProject.npcTypes.size() != 1 ||
        loadedProject.npcTypes.front().talkEffectIds != std::vector<std::string>{"increment_example"} ||
        loadedProject.npcTypes.front().stateRules.size() != 1 ||
        loadedProject.npcTypes.front().stateRules.front().condition.scope != adventure::game::StateVariableScope::Chapter ||
        loadedProject.npcTypes.front().stateRules.front().graphId != "quest_complete" ||
        loadedProject.npcTypes.front().stateRules.front().animation != "victory") {
        std::cerr << "Item/NPC variable event and rule round-trip values did not match.\n";
        return 1;
    }
    if (loadedProject.weaponDefs.size() != 1 ||
        loadedProject.weaponDefs.front().wallBehavior != adventure::game::ProjectileWallBehavior::Rebound) {
        std::cerr << "Weapon projectile wall-behavior round-trip did not match.\n";
        return 1;
    }
    const adventure::game::WeaponDef& loadedWeapon = loadedProject.weaponDefs.front();
    if (loadedWeapon.chargeTimeSeconds != 1.2f ||
        loadedWeapon.chargeDamageScaleMin != 0.5f ||
        loadedWeapon.chargeDamageScaleMax != 2.0f ||
        loadedWeapon.overchargeTimeSeconds != 2.0f ||
        loadedWeapon.overchargeEffect != adventure::game::OverchargeEffect::WildShot ||
        loadedWeapon.spreadStartDegrees != 4.0f ||
        loadedWeapon.spreadEndDegrees != 1.0f ||
        loadedWeapon.steadyTimeSeconds != 1.2f ||
        loadedWeapon.pelletCount != 1 ||
        loadedWeapon.falloffStartPx != 48.0f ||
        loadedWeapon.falloffEndPx != 160.0f ||
        loadedWeapon.falloffMinDamageScale != 0.25f ||
        loadedWeapon.aimConeDegrees != 40.0f ||
        loadedWeapon.attackAnimState != "attack_20") {
        std::cerr << "Weapon ranged-feel stat round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped scoped state/event/NPC rules and chapter synopses (ADGAME v17)\n";
    return 0;
}
