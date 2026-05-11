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
    return 0;
}
