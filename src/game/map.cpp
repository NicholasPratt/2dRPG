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
    if (map.obstacles.size() > kMaxObstacles) {
        return false;
    }
    for (const MapObstacle& obstacle : map.obstacles) {
        if (obstacle.x < 0 || obstacle.y < 0 || obstacle.width <= 0 || obstacle.height <= 0 ||
            obstacle.x + obstacle.width > map.width || obstacle.y + obstacle.height > map.height ||
            obstacle.activeSeconds <= 0.0f || obstacle.inactiveSeconds < 0.0f) {
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

    output << "ADMAP 4\n";
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
        output << "obstacle " << obstacleTypeToInt(obstacle.type) << ' ' << encodedToken(obstacle.spriteId) << ' '
               << obstacle.x << ' ' << obstacle.y << ' ' << obstacle.width << ' ' << obstacle.height << ' '
               << obstacle.activeSeconds << ' ' << obstacle.inactiveSeconds << ' ' << obstacle.phaseSeconds << "\n";
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
    if (magic != "ADMAP" || version < 1 || version > 4) {
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
                std::string spriteId;
                MapObstacle obstacle;
                input >> typeValue >> spriteId >> obstacle.x >> obstacle.y >> obstacle.width >> obstacle.height
                      >> obstacle.activeSeconds >> obstacle.inactiveSeconds >> obstacle.phaseSeconds;
                if (!input || !obstacleTypeFromInt(typeValue, obstacle.type)) {
                    setError(errorMessage, "Invalid obstacle data.");
                    return false;
                }
                obstacle.spriteId = decodedToken(spriteId);
                loaded.obstacles.push_back(obstacle);
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
