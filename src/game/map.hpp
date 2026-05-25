#pragma once

#include "game/constants.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

enum class ObstacleType {
    Spike = 0,
    Pit = 1,
    TimedSpike = 2,
};

struct MapObstacle {
    ObstacleType type = ObstacleType::Spike;
    std::string spriteId;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    float activeSeconds = 1.0f;
    float inactiveSeconds = 1.0f;
    float phaseSeconds = 0.0f;
};

struct TileMap {
    std::string id = "new_map";
    std::string tilesetId;
    int width = kScreenTilesW;
    int height = kScreenTilesH;
    int spawnX = 1;
    int spawnY = 1;
    // Layer 0: floor  Layer 1: mid (player-level, used for collision)  Layer 2: ceiling/overlay
    std::array<std::vector<uint16_t>, 3> layers = {
        std::vector<uint16_t>(static_cast<std::size_t>(kScreenTilesW * kScreenTilesH), 0u),
        std::vector<uint16_t>(static_cast<std::size_t>(kScreenTilesW * kScreenTilesH), 0u),
        std::vector<uint16_t>(static_cast<std::size_t>(kScreenTilesW * kScreenTilesH), 0u),
    };
    std::vector<MapObstacle> obstacles;
};

[[nodiscard]] bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage = nullptr);

} // namespace adventure::game
