#pragma once

#include "game/chapter.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct EnemyCombatStats {
    int maxHealth = 1;
    int contactDamage = 1;
    float hitboxWidth = 12.0f;
    float hitboxHeight = 12.0f;
    float attackCooldownSeconds = 1.0f;
    std::vector<EnemyAttackDef> attacks;
};

struct EnemyPath {
    std::string id = "path_1";
    std::string enemyId = "enemy_1";
    std::string spriteId;
    std::string mapId = "new_map";
    PathBehavior behavior = PathBehavior::Patrol;
    PathCurveMode curveMode = PathCurveMode::Linear;
    EnemyCombatStats combat;
    float speed = 64.0f;
    bool loop = true;
    bool respawn = false;
    std::vector<PathWaypoint> waypoints;
};

[[nodiscard]] bool saveEnemyPath(const std::filesystem::path& path, const EnemyPath& enemyPath, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadEnemyPath(const std::filesystem::path& path, EnemyPath& enemyPath, std::string* errorMessage = nullptr);

} // namespace adventure::game
