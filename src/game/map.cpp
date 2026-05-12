#include "game/map.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxMapDimension = 512;
constexpr int kMaxTileId = 65535;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validMapShape(const TileMap& map)
{
    return map.width > 0 && map.height > 0 &&
        map.width <= kMaxMapDimension && map.height <= kMaxMapDimension &&
        map.tiles.size() == static_cast<std::size_t>(map.width * map.height);
}

} // namespace

bool saveTileMap(const std::filesystem::path& path, const TileMap& map, std::string* errorMessage)
{
    if (!validMapShape(map)) {
        setError(errorMessage, "Map dimensions or tile data are invalid.");
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open map file for writing.");
        return false;
    }

    output << "ADMAP 2\n";
    output << "id " << map.id << "\n";
    if (!map.tilesetId.empty()) {
        output << "tileset " << map.tilesetId << "\n";
    }
    output << "size " << map.width << " " << map.height << "\n";
    output << "spawn " << std::clamp(map.spawnX, 0, map.width - 1) << " " << std::clamp(map.spawnY, 0, map.height - 1) << "\n";
    output << "tiles\n";
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (x > 0) {
                output << ' ';
            }
            output << static_cast<unsigned int>(map.tiles[static_cast<std::size_t>(y) * map.width + x]);
        }
        output << "\n";
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
    if (magic != "ADMAP" || (version != 1 && version != 2)) {
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

    // Optional tileset (v2 only)
    if (key == "tileset") {
        input >> loaded.tilesetId;
        input >> key;
    }

    if (key != "size") {
        setError(errorMessage, "Expected map size.");
        return false;
    }
    input >> loaded.width >> loaded.height;
    if (loaded.width <= 0 || loaded.height <= 0 || loaded.width > kMaxMapDimension || loaded.height > kMaxMapDimension) {
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

    if (key != "tiles") {
        setError(errorMessage, "Expected map tile data.");
        return false;
    }

    loaded.tiles.assign(static_cast<std::size_t>(loaded.width * loaded.height), 0u);

    if (version == 1) {
        // v1: one row per line, each char '0' or '1'
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
                    setError(errorMessage, "Map tile row contains an invalid tile value.");
                    return false;
                }
                loaded.tiles[static_cast<std::size_t>(y) * loaded.width + x] = c == '1' ? 1u : 0u;
            }
        }
    } else {
        // v2: width space-separated integers per row
        for (int y = 0; y < loaded.height; ++y) {
            for (int x = 0; x < loaded.width; ++x) {
                unsigned int tileId = 0;
                input >> tileId;
                if (!input) {
                    setError(errorMessage, "Unexpected end of tile data.");
                    return false;
                }
                if (tileId > static_cast<unsigned int>(kMaxTileId)) {
                    setError(errorMessage, "Tile ID is out of range (max 65535).");
                    return false;
                }
                loaded.tiles[static_cast<std::size_t>(y) * loaded.width + x] = static_cast<uint16_t>(tileId);
            }
        }
    }

    input >> key;
    if (key != "end") {
        setError(errorMessage, "Expected map end marker.");
        return false;
    }

    map = std::move(loaded);
    return true;
}

} // namespace adventure::game
