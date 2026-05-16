#include "game/path.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxWaypoints = 4096;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

bool saveEnemyPath(const std::filesystem::path& path, const EnemyPath& enemyPath, std::string* errorMessage)
{
    if (enemyPath.id.empty() || enemyPath.mapId.empty()) {
        setError(errorMessage, "Path id and map id must not be empty.");
        return false;
    }
    if (enemyPath.speed < 0.0f || static_cast<int>(enemyPath.waypoints.size()) > kMaxWaypoints) {
        setError(errorMessage, "Path speed or waypoint count is invalid.");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open path file for writing.");
        return false;
    }

    output << "ADPATH 1\n";
    output << "id " << enemyPath.id << "\n";
    output << "map " << enemyPath.mapId << "\n";
    output << "behavior " << static_cast<int>(enemyPath.behavior) << "\n";
    output << "speed " << enemyPath.speed << "\n";
    output << "loop " << (enemyPath.loop ? 1 : 0) << "\n";
    output << "respawn " << (enemyPath.respawn ? 1 : 0) << "\n";
    output << "waypoints " << enemyPath.waypoints.size() << "\n";
    for (const PathWaypoint& waypoint : enemyPath.waypoints) {
        output << "wp " << waypoint.x << " " << waypoint.y << "\n";
    }
    output << "end\n";

    if (!output) {
        setError(errorMessage, "Failed while writing path file.");
        return false;
    }
    return true;
}

bool loadEnemyPath(const std::filesystem::path& path, EnemyPath& enemyPath, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open path file for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADPATH" || version != 1) {
        setError(errorMessage, "Unsupported path file type or version.");
        return false;
    }

    EnemyPath loaded;
    std::string key;

    input >> key;
    if (key != "id") { setError(errorMessage, "Expected id."); return false; }
    input >> loaded.id;

    input >> key;
    if (key != "map") { setError(errorMessage, "Expected map."); return false; }
    input >> loaded.mapId;

    int behavior = 1;
    input >> key;
    if (key != "behavior") { setError(errorMessage, "Expected behavior."); return false; }
    input >> behavior;
    loaded.behavior = static_cast<PathBehavior>(std::clamp(behavior, 0, 2));

    input >> key;
    if (key != "speed") { setError(errorMessage, "Expected speed."); return false; }
    input >> loaded.speed;
    if (!input || loaded.speed < 0.0f) {
        setError(errorMessage, "Path speed is invalid.");
        return false;
    }

    int loop = 0;
    input >> key;
    if (key != "loop") { setError(errorMessage, "Expected loop."); return false; }
    input >> loop;
    loaded.loop = (loop != 0);

    int respawn = 0;
    input >> key;
    if (key != "respawn") { setError(errorMessage, "Expected respawn."); return false; }
    input >> respawn;
    loaded.respawn = (respawn != 0);

    int waypointCount = 0;
    input >> key;
    if (key != "waypoints") { setError(errorMessage, "Expected waypoints."); return false; }
    input >> waypointCount;
    if (waypointCount < 0 || waypointCount > kMaxWaypoints) {
        setError(errorMessage, "Waypoint count out of range.");
        return false;
    }

    loaded.waypoints.clear();
    loaded.waypoints.reserve(static_cast<std::size_t>(waypointCount));
    for (int i = 0; i < waypointCount; ++i) {
        PathWaypoint waypoint;
        input >> key;
        if (key != "wp") { setError(errorMessage, "Expected wp entry."); return false; }
        input >> waypoint.x >> waypoint.y;
        if (!input) {
            setError(errorMessage, "Failed to read waypoint.");
            return false;
        }
        loaded.waypoints.push_back(waypoint);
    }

    input >> key;
    if (key != "end") {
        setError(errorMessage, "Expected end marker.");
        return false;
    }

    enemyPath = std::move(loaded);
    return true;
}

} // namespace adventure::game
