#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct PathWaypoint {
    float x = 0.0f;
    float y = 0.0f;
};

enum class PathBehavior {
    Idle = 0,
    Patrol = 1,
    Aggro = 2,
};

struct EnemyPath {
    std::string id = "path_1";
    std::string mapId = "new_map";
    PathBehavior behavior = PathBehavior::Patrol;
    float speed = 64.0f;
    bool loop = true;
    bool respawn = false;
    std::vector<PathWaypoint> waypoints;
};

[[nodiscard]] bool saveEnemyPath(const std::filesystem::path& path, const EnemyPath& enemyPath, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadEnemyPath(const std::filesystem::path& path, EnemyPath& enemyPath, std::string* errorMessage = nullptr);

} // namespace adventure::game
