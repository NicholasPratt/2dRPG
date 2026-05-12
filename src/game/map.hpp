#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct TileMap {
    std::string id = "new_map";
    std::string tilesetId;
    int width = 24;
    int height = 16;
    int spawnX = 1;
    int spawnY = 1;
    // Row-major tile IDs: tiles[y * width + x]. ID 0 = empty/default.
    std::vector<uint16_t> tiles = std::vector<uint16_t>(static_cast<std::size_t>(24 * 16), 0u);
};

[[nodiscard]] bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage = nullptr);

} // namespace adventure::game
