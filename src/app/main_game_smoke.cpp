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

    const std::filesystem::path chapterPath = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/game/chapters/chapter_1.adchapter");
    adventure::game::Chapter chapter;
    if (!adventure::game::loadChapter(chapterPath, chapter, &error)) {
        std::cerr << "Failed to load chapter: " << error << "\n";
        return 1;
    }

    std::cout << "Loaded chapter " << chapter.id << " [" << chapter.screens.size()
              << " screen(s)] start [" << chapter.startScreenId << "]\n";

    adventure::game::SpriteMetadata sprite;
    const std::filesystem::path spritePath = "assets/game/sprites/new_sprite.sprite.json";
    if (!adventure::game::loadSpriteMetadata(spritePath, sprite, &error)) {
        std::cerr << "Failed to load sprite metadata: " << error << "\n";
        return 1;
    }
    std::cout << "Loaded sprite metadata " << sprite.id << " [" << sprite.frames.size() << " frame(s)]\n";

    adventure::game::EnemyPath path;
    path.id = "smoke_path";
    path.mapId = map.id;
    path.waypoints = {{16.0f, 16.0f}, {32.0f, 16.0f}};
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
    std::cout << "Round-tripped path " << loadedPath.id << " [" << loadedPath.waypoints.size() << " waypoint(s)]\n";

    adventure::game::GameState state;
    state.setInt("Example_Count", 11);
    state.addInt("Example_Count", 1);
    state.setBool("Example_Complete", true);
    state.giveItem("example_reward");
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
        !loadedState.hasItem("example_reward")) {
        std::cerr << "State smoke round-trip values did not match.\n";
        return 1;
    }
    std::cout << "Round-tripped game state Example_Count=" << loadedState.getInt("Example_Count") << "\n";

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
    std::cout << "Round-tripped project state/effect definitions\n";
    return 0;
}
