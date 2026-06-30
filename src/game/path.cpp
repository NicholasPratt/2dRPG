#include "game/path.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
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

std::string encodedToken(const std::string& value)
{
    return value.empty() ? "-" : value;
}

std::string decodedToken(const std::string& value)
{
    return value == "-" ? std::string{} : value;
}

} // namespace

bool saveEnemyPath(const std::filesystem::path& path, const EnemyPath& enemyPath, std::string* errorMessage)
{
    if (enemyPath.id.empty() || enemyPath.mapId.empty()) {
        setError(errorMessage, "Path id and map id must not be empty.");
        return false;
    }
    if (enemyPath.speed < 0.0f || static_cast<int>(enemyPath.waypoints.size()) > kMaxWaypoints ||
        enemyPath.combat.maxHealth <= 0 || enemyPath.combat.contactDamage < 0 ||
        enemyPath.combat.hitboxWidth <= 0.0f || enemyPath.combat.hitboxHeight <= 0.0f ||
        enemyPath.combat.attackCooldownSeconds < 0.0f) {
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

    output << "ADPATH 6\n";
    output << "id " << enemyPath.id << "\n";
    output << "enemy " << encodedToken(enemyPath.enemyId) << "\n";
    output << "sprite " << encodedToken(enemyPath.spriteId) << "\n";
    output << "map " << enemyPath.mapId << "\n";
    output << "behavior " << static_cast<int>(enemyPath.behavior) << "\n";
    output << "curve " << static_cast<int>(enemyPath.curveMode) << "\n";
    output << "combat " << enemyPath.combat.maxHealth << ' '
           << enemyPath.combat.contactDamage << ' '
           << enemyPath.combat.hitboxWidth << ' '
           << enemyPath.combat.hitboxHeight << ' '
           << enemyPath.combat.attackCooldownSeconds << "\n";
    output << "speed " << enemyPath.speed << "\n";
    output << "loop " << (enemyPath.loop ? 1 : 0) << "\n";
    output << "respawn " << (enemyPath.respawn ? 1 : 0) << "\n";
    output << "waypoints " << enemyPath.waypoints.size() << "\n";
    for (const PathWaypoint& waypoint : enemyPath.waypoints) {
        output << "wp " << waypoint.x << " " << waypoint.y
               << ' ' << waypoint.speedOverride << ' ' << waypoint.waitSeconds
               << ' ' << waypoint.facing
               << ' ' << (waypoint.animState.empty() ? "-" : waypoint.animState)
               << ' ' << static_cast<int>(waypoint.action)
               << ' ' << waypoint.speechDurationSeconds
               << ' ' << std::quoted(waypoint.speechText)
               << ' ' << waypoint.actionRepeatLimit << "\n";
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
    if (magic != "ADPATH" || version < 1 || version > 6) {
        setError(errorMessage, "Unsupported path file type or version.");
        return false;
    }

    EnemyPath loaded;
    std::string key;

    input >> key;
    if (key != "id") { setError(errorMessage, "Expected id."); return false; }
    input >> loaded.id;

    input >> key;
    if (version >= 2 && key == "enemy") {
        input >> loaded.enemyId;
        loaded.enemyId = decodedToken(loaded.enemyId);
        input >> key;
    }
    if (version >= 2 && key == "sprite") {
        input >> loaded.spriteId;
        loaded.spriteId = decodedToken(loaded.spriteId);
        input >> key;
    }
    if (key != "map") { setError(errorMessage, "Expected map."); return false; }
    input >> loaded.mapId;

    int behavior = 1;
    input >> key;
    if (key != "behavior") { setError(errorMessage, "Expected behavior."); return false; }
    input >> behavior;
    loaded.behavior = static_cast<PathBehavior>(std::clamp(behavior, 0, 2));

    input >> key;
    if (version >= 2 && key == "curve") {
        int curve = 0;
        input >> curve;
        loaded.curveMode = static_cast<PathCurveMode>(std::clamp(curve, 0, 1));
        input >> key;
    }
    if (version >= 3 && key == "combat") {
        input >> loaded.combat.maxHealth >> loaded.combat.contactDamage >>
            loaded.combat.hitboxWidth >> loaded.combat.hitboxHeight >>
            loaded.combat.attackCooldownSeconds;
        if (!input || loaded.combat.maxHealth <= 0 || loaded.combat.contactDamage < 0 ||
            loaded.combat.hitboxWidth <= 0.0f || loaded.combat.hitboxHeight <= 0.0f ||
            loaded.combat.attackCooldownSeconds < 0.0f) {
            setError(errorMessage, "Enemy combat data is invalid.");
            return false;
        }
        input >> key;
    }
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
        if (version >= 4) {
            std::string animToken;
            input >> waypoint.speedOverride >> waypoint.waitSeconds >> waypoint.facing >> animToken;
            waypoint.animState = (animToken == "-") ? std::string{} : animToken;
        }
        if (version >= 5) {
            int action = 0;
            input >> action >> waypoint.speechDurationSeconds >> std::quoted(waypoint.speechText);
            waypoint.action = static_cast<PathWaypointAction>(std::clamp(action, 0, 3));
            waypoint.speechDurationSeconds = std::max(0.0f, waypoint.speechDurationSeconds);
            if (version >= 6) {
                input >> waypoint.actionRepeatLimit;
                waypoint.actionRepeatLimit = std::max(0, waypoint.actionRepeatLimit);
            }
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
