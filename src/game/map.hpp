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

enum class ItemPickupType {
    Weapon = 0,
    Ammo = 1,
    Health = 2,
    ProjectItem = 3,
};

enum class DoorLockMode {
    FreeUse = 0,
    Locked = 1,
    RequiresItem = 2,
};

struct MapItemPlacement {
    std::string id;
    ItemPickupType pickupType = ItemPickupType::Ammo;
    std::string targetId;  // Weapon: weapon def id; Ammo: ammoTypeId; Health: unused
    int quantity = 5;      // Ammo: count added; Health: hp restored; Weapon: unused
    float x = 0.0f;
    float y = 0.0f;
    bool respawn = false;
    std::string spriteId;
};

struct MapDoorPlacement {
    std::string id;
    int x = 0;              // tile coordinate
    int y = 0;              // tile coordinate
    int widthTiles = 1;     // size in tiles
    int heightTiles = 1;    // size in tiles
    DoorLockMode lockMode = DoorLockMode::FreeUse;
    std::string requiredItemId;
    bool consumeKey = false;
    std::string targetScreenId;
    int targetTileX = 1;
    int targetTileY = 1;
    // Id of the paired door on the target screen. When set, the player spawns at
    // that door's location (the two doors are created/linked together in the
    // editor). Falls back to targetTileX/targetTileY when empty.
    std::string targetDoorId;
    std::string spriteId;
    std::string openingAnimation;
    std::string openSoundPath;
    std::string closeSoundPath;
    std::string lockedSoundPath;
};

struct MapObstacle {
    std::string id;
    ObstacleType type = ObstacleType::Spike;
    std::string spriteId;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    float activeSeconds = 1.0f;
    float inactiveSeconds = 1.0f;
    float phaseSeconds = 0.0f;
    int damage = 1;                       // HP removed per damage tick
    float damageIntervalSeconds = 0.75f;  // seconds between damage ticks while overlapping
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
    std::vector<MapItemPlacement> items;
    std::vector<MapDoorPlacement> doors;
};

[[nodiscard]] bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage = nullptr);

} // namespace adventure::game
