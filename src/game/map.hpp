#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct TileMap {
    std::string id = "new_map";
    int width = 24;
    int height = 16;
    int spawnX = 1;
    int spawnY = 1;
    std::vector<unsigned char> walls = std::vector<unsigned char>(static_cast<std::size_t>(24 * 16), 0u);
};

[[nodiscard]] bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage = nullptr);

} // namespace adventure::game
