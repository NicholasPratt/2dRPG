#include "game/chapter.hpp"
#include "game/map.hpp"

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
    return 0;
}
