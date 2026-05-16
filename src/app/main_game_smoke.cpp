#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/sprite.hpp"

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
    return 0;
}
