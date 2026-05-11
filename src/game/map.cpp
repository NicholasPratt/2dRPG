#include "game/map.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxMapDimension = 512;

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
        map.walls.size() == static_cast<std::size_t>(map.width * map.height);
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

    output << "ADMAP 1\n";
    output << "id " << map.id << "\n";
    output << "size " << map.width << " " << map.height << "\n";
    output << "spawn " << std::clamp(map.spawnX, 0, map.width - 1) << " " << std::clamp(map.spawnY, 0, map.height - 1) << "\n";
    output << "tiles\n";
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            output << (map.walls[static_cast<std::size_t>(y) * map.width + x] != 0u ? '1' : '0');
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
    if (magic != "ADMAP" || version != 1) {
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

    loaded.walls.assign(static_cast<std::size_t>(loaded.width * loaded.height), 0u);
    for (int y = 0; y < loaded.height; ++y) {
        std::string row;
        input >> row;
        if (static_cast<int>(row.size()) != loaded.width) {
            setError(errorMessage, "Map tile row has the wrong width.");
            return false;
        }
        for (int x = 0; x < loaded.width; ++x) {
            if (row[static_cast<std::size_t>(x)] != '0' && row[static_cast<std::size_t>(x)] != '1') {
                setError(errorMessage, "Map tile row contains an invalid tile value.");
                return false;
            }
            loaded.walls[static_cast<std::size_t>(y) * loaded.width + x] = row[static_cast<std::size_t>(x)] == '1' ? 1u : 0u;
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
