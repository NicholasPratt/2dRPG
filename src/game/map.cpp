#include "game/map.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxMapDimension = 512;
constexpr int kMaxTileId = 65535;
constexpr int kLayerCount = 3;
constexpr int kMaxObstacles = 2048;
constexpr int kMaxItems = 1024;
constexpr int kMaxDoors = 1024;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validMapShape(const TileMap& map)
{
    if (map.width <= 0 || map.height <= 0 ||
        map.width > kMaxMapDimension || map.height > kMaxMapDimension) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(map.width * map.height);
    for (const auto& layer : map.layers) {
        if (layer.size() != expected) {
            return false;
        }
    }
    if (map.obstacles.size() > kMaxObstacles || map.items.size() > kMaxItems || map.doors.size() > kMaxDoors) {
        return false;
    }
    for (const MapObstacle& obstacle : map.obstacles) {
        if (obstacle.x < 0 || obstacle.y < 0 || obstacle.width <= 0 || obstacle.height <= 0 ||
            obstacle.x + obstacle.width > map.width || obstacle.y + obstacle.height > map.height ||
            obstacle.activeSeconds <= 0.0f || obstacle.inactiveSeconds < 0.0f) {
            return false;
        }
    }
    for (const MapDoorPlacement& door : map.doors) {
        if (door.x < 0 || door.y < 0 || door.widthTiles <= 0 || door.heightTiles <= 0 ||
            door.x + door.widthTiles > map.width || door.y + door.heightTiles > map.height ||
            door.targetTileX < 0 || door.targetTileY < 0) {
            return false;
        }
    }
    return true;
}

int obstacleTypeToInt(ObstacleType type)
{
    return static_cast<int>(type);
}

bool obstacleTypeFromInt(int value, ObstacleType& type)
{
    switch (value) {
        case 0: type = ObstacleType::Spike; return true;
        case 1: type = ObstacleType::Pit; return true;
        case 2: type = ObstacleType::TimedSpike; return true;
        default: return false;
    }
}

DoorLockMode doorLockModeFromInt(int value)
{
    switch (value) {
        case 1: return DoorLockMode::Locked;
        case 2: return DoorLockMode::RequiresItem;
        default: return DoorLockMode::FreeUse;
    }
}

void writeTileLayer(std::ofstream& output, const std::vector<uint16_t>& layer, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x > 0) {
                output << ' ';
            }
            output << static_cast<unsigned int>(layer[static_cast<std::size_t>(y) * width + x]);
        }
        output << '\n';
    }
}

std::string encodedToken(const std::string& value)
{
    return value.empty() ? "-" : value;
}

std::string decodedToken(const std::string& value)
{
    return value == "-" ? std::string{} : value;
}

bool readTileLayer(std::ifstream& input, std::vector<uint16_t>& layer, int width, int height,
    std::string* errorMessage)
{
    const std::size_t sz = static_cast<std::size_t>(width * height);
    layer.assign(sz, 0u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned int tileId = 0;
            input >> tileId;
            if (!input) {
                setError(errorMessage, "Unexpected end of tile data.");
                return false;
            }
            if (tileId > static_cast<unsigned int>(kMaxTileId)) {
                setError(errorMessage, "Tile ID out of range (max 65535).");
                return false;
            }
            layer[static_cast<std::size_t>(y) * width + x] = static_cast<uint16_t>(tileId);
        }
    }
    return true;
}

} // namespace

bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage)
{
    if (!validMapShape(map)) {
        setError(errorMessage, "Map dimensions or layer data are invalid.");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open map file for writing.");
        return false;
    }

    output << "ADMAP 10\n";
    output << "id " << map.id << "\n";
    if (!map.tilesetId.empty()) {
        output << "tileset " << map.tilesetId << "\n";
    }
    output << "size " << map.width << " " << map.height << "\n";
    output << "spawn " << std::clamp(map.spawnX, 0, map.width - 1)
           << " " << std::clamp(map.spawnY, 0, map.height - 1) << "\n";

    for (int l = 0; l < kLayerCount; ++l) {
        output << "layer " << l << "\n";
        writeTileLayer(output, map.layers[l], map.width, map.height);
    }
    output << "obstacles " << map.obstacles.size() << "\n";
    for (const MapObstacle& obstacle : map.obstacles) {
        output << "obstacle " << encodedToken(obstacle.id) << ' ' << obstacleTypeToInt(obstacle.type) << ' '
               << encodedToken(obstacle.spriteId) << ' '
               << obstacle.x << ' ' << obstacle.y << ' ' << obstacle.width << ' ' << obstacle.height << ' '
               << obstacle.activeSeconds << ' ' << obstacle.inactiveSeconds << ' ' << obstacle.phaseSeconds << ' '
               << obstacle.damage << ' ' << obstacle.damageIntervalSeconds << "\n";
    }
    output << "items " << map.items.size() << "\n";
    for (const MapItemPlacement& item : map.items) {
        output << "item " << encodedToken(item.id)
               << ' ' << static_cast<int>(item.pickupType)
               << ' ' << encodedToken(item.targetId)
               << ' ' << item.quantity
               << ' ' << item.x
               << ' ' << item.y
               << ' ' << (item.respawn ? 1 : 0)
               << ' ' << encodedToken(item.spriteId) << "\n";
    }
    output << "doors " << map.doors.size() << "\n";
    for (const MapDoorPlacement& door : map.doors) {
        output << "door " << encodedToken(door.id)
               << ' ' << door.x
               << ' ' << door.y
               << ' ' << door.widthTiles
               << ' ' << door.heightTiles
               << ' ' << static_cast<int>(door.lockMode)
               << ' ' << encodedToken(door.requiredItemId)
               << ' ' << (door.consumeKey ? 1 : 0)
               << ' ' << encodedToken(door.targetScreenId)
               << ' ' << door.targetTileX
               << ' ' << door.targetTileY
               << ' ' << encodedToken(door.spriteId)
               << ' ' << encodedToken(door.openingAnimation)
               << ' ' << encodedToken(door.openSoundPath)
               << ' ' << encodedToken(door.closeSoundPath)
               << ' ' << encodedToken(door.lockedSoundPath) << "\n";
    }
    output << "end\n";

    if (!output) {
        setError(errorMessage, "Failed while writing map file.");
        return false;
    }
    return true;
}

bool loadTileMap(const std::filesystem::path& path, TileMap& map, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open map file for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADMAP" || version < 1 || version > 10) {
        setError(errorMessage, "Unsupported map file type or version.");
        return false;
    }

    TileMap loaded;
    std::string key;

    input >> key;
    if (key != "id") {
        setError(errorMessage, "Expected map id.");
        return false;
    }
    input >> loaded.id;

    input >> key;
    if (key == "tileset") {
        input >> loaded.tilesetId;
        input >> key;
    }

    if (key != "size") {
        setError(errorMessage, "Expected map size.");
        return false;
    }
    input >> loaded.width >> loaded.height;
    if (loaded.width <= 0 || loaded.height <= 0 ||
        loaded.width > kMaxMapDimension || loaded.height > kMaxMapDimension) {
        setError(errorMessage, "Map size is outside supported limits.");
        return false;
    }

    input >> key;
    if (key == "spawn") {
        input >> loaded.spawnX >> loaded.spawnY;
        loaded.spawnX = std::clamp(loaded.spawnX, 0, loaded.width - 1);
        loaded.spawnY = std::clamp(loaded.spawnY, 0, loaded.height - 1);
        input >> key;
    } else {
        loaded.spawnX = 1 < loaded.width ? 1 : 0;
        loaded.spawnY = 1 < loaded.height ? 1 : 0;
    }

    // Allocate all layers; key holds the next section keyword
    const std::size_t sz = static_cast<std::size_t>(loaded.width * loaded.height);
    for (auto& layer : loaded.layers) {
        layer.assign(sz, 0u);
    }

    if (version <= 2) {
        // v1/v2: single tile section → load into mid layer (index 1)
        if (key != "tiles") {
            setError(errorMessage, "Expected map tile data.");
            return false;
        }
        if (version == 1) {
            for (int y = 0; y < loaded.height; ++y) {
                std::string row;
                input >> row;
                if (static_cast<int>(row.size()) != loaded.width) {
                    setError(errorMessage, "Map tile row has the wrong width.");
                    return false;
                }
                for (int x = 0; x < loaded.width; ++x) {
                    const char c = row[static_cast<std::size_t>(x)];
                    if (c != '0' && c != '1') {
                        setError(errorMessage, "Map tile row contains an invalid value.");
                        return false;
                    }
                    loaded.layers[1][static_cast<std::size_t>(y) * loaded.width + x] = (c == '1') ? 1u : 0u;
                }
            }
        } else {
            if (!readTileLayer(input, loaded.layers[1], loaded.width, loaded.height, errorMessage)) {
                return false;
            }
        }
    } else {
        // v3: three labeled layer sections; key is "layer" for the first one
        for (int l = 0; l < kLayerCount; ++l) {
            if (key != "layer") {
                setError(errorMessage, "Expected 'layer' keyword.");
                return false;
            }
            int layerIdx = -1;
            input >> layerIdx;
            if (layerIdx != l) {
                setError(errorMessage, "Layer index mismatch in map file.");
                return false;
            }
            if (!readTileLayer(input, loaded.layers[l], loaded.width, loaded.height, errorMessage)) {
                return false;
            }
            input >> key;
        }
        if (version >= 4 && key == "obstacles") {
            int obstacleCount = 0;
            input >> obstacleCount;
            if (!input || obstacleCount < 0 || obstacleCount > kMaxObstacles) {
                setError(errorMessage, "Invalid obstacle count.");
                return false;
            }
            loaded.obstacles.reserve(static_cast<std::size_t>(obstacleCount));
            for (int i = 0; i < obstacleCount; ++i) {
                if (!(input >> key) || key != "obstacle") {
                    setError(errorMessage, "Expected obstacle entry.");
                    return false;
                }
                int typeValue = 0;
                std::string idToken;
                std::string spriteId;
                MapObstacle obstacle;
                if (version >= 7) {
                    input >> idToken;
                }
                input >> typeValue >> spriteId >> obstacle.x >> obstacle.y >> obstacle.width >> obstacle.height
                      >> obstacle.activeSeconds >> obstacle.inactiveSeconds >> obstacle.phaseSeconds;
                if (version >= 8) {
                    input >> obstacle.damage >> obstacle.damageIntervalSeconds;
                }
                if (!input || !obstacleTypeFromInt(typeValue, obstacle.type)) {
                    setError(errorMessage, "Invalid obstacle data.");
                    return false;
                }
                obstacle.id = decodedToken(idToken);
                if (obstacle.id.empty()) {
                    obstacle.id = "obstacle_" + std::to_string(i + 1);
                }
                obstacle.spriteId = decodedToken(spriteId);
                loaded.obstacles.push_back(obstacle);
            }
            input >> key;
        }
        if (version >= 5 && key == "items") {
            int itemCount = 0;
            input >> itemCount;
            if (!input || itemCount < 0 || itemCount > kMaxItems) {
                setError(errorMessage, "Invalid item count.");
                return false;
            }
            loaded.items.reserve(static_cast<std::size_t>(itemCount));
            for (int i = 0; i < itemCount; ++i) {
                if (!(input >> key) || key != "item") {
                    setError(errorMessage, "Expected item entry.");
                    return false;
                }
                MapItemPlacement item;
                std::string idToken;
                std::string targetIdToken;
                std::string spriteIdToken;
                int pickupTypeValue = 0;
                int respawnValue = 0;
                input >> idToken >> pickupTypeValue >> targetIdToken >> item.quantity
                      >> item.x >> item.y >> respawnValue >> spriteIdToken;
                if (!input) {
                    setError(errorMessage, "Invalid item data.");
                    return false;
                }
                item.id = decodedToken(idToken);
                item.targetId = decodedToken(targetIdToken);
                item.spriteId = decodedToken(spriteIdToken);
                item.respawn = (respawnValue != 0);
                switch (pickupTypeValue) {
                    case 1: item.pickupType = ItemPickupType::Ammo; break;
                    case 2: item.pickupType = ItemPickupType::Health; break;
                    case 3: item.pickupType = ItemPickupType::ProjectItem; break;
                    default: item.pickupType = ItemPickupType::Weapon; break;
                }
                loaded.items.push_back(item);
            }
            input >> key;
        }
        if (version >= 6 && key == "doors") {
            int doorCount = 0;
            input >> doorCount;
            if (!input || doorCount < 0 || doorCount > kMaxDoors) {
                setError(errorMessage, "Invalid door count.");
                return false;
            }
            loaded.doors.reserve(static_cast<std::size_t>(doorCount));
            for (int i = 0; i < doorCount; ++i) {
                if (!(input >> key) || key != "door") {
                    setError(errorMessage, "Expected door entry.");
                    return false;
                }
                MapDoorPlacement door;
                std::string idToken;
                std::string requiredItemToken;
                std::string targetScreenToken;
                std::string spriteIdToken;
                std::string openingAnimationToken;
                std::string openSoundToken;
                std::string closeSoundToken;
                std::string lockedSoundToken;
                int lockModeValue = 0;
                int consumeKeyValue = 0;
                input >> idToken >> door.x >> door.y >> door.widthTiles >> door.heightTiles
                      >> lockModeValue >> requiredItemToken >> consumeKeyValue >> targetScreenToken
                      >> door.targetTileX >> door.targetTileY >> spriteIdToken >> openingAnimationToken;
                if (version >= 9) {
                    input >> openSoundToken >> closeSoundToken;
                }
                if (version >= 10) {
                    input >> lockedSoundToken;
                }
                if (!input) {
                    setError(errorMessage, "Invalid door data.");
                    return false;
                }
                door.id = decodedToken(idToken);
                door.lockMode = doorLockModeFromInt(lockModeValue);
                door.requiredItemId = decodedToken(requiredItemToken);
                door.consumeKey = consumeKeyValue != 0;
                door.targetScreenId = decodedToken(targetScreenToken);
                door.spriteId = decodedToken(spriteIdToken);
                door.openingAnimation = decodedToken(openingAnimationToken);
                door.openSoundPath = decodedToken(openSoundToken);
                door.closeSoundPath = decodedToken(closeSoundToken);
                door.lockedSoundPath = decodedToken(lockedSoundToken);
                loaded.doors.push_back(std::move(door));
            }
            input >> key;
        }
        if (key != "end") {
            setError(errorMessage, "Expected map end marker.");
            return false;
        }
        if (!validMapShape(loaded)) {
            setError(errorMessage, "Loaded map data is invalid.");
            return false;
        }
        map = std::move(loaded);
        return true;
    }

    input >> key;
    if (key != "end") {
        setError(errorMessage, "Expected map end marker.");
        return false;
    }

    if (!validMapShape(loaded)) {
        setError(errorMessage, "Loaded map data is invalid.");
        return false;
    }
    map = std::move(loaded);
    return true;
}

} // namespace adventure::game
