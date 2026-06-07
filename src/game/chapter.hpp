#pragma once

#include "game/project.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

enum class PathWaypointAction {
    None = 0,
    Enter = 1,
    Speak = 2,
    Leave = 3,
};

struct PathWaypoint {
    float x = 0.0f;
    float y = 0.0f;
    float speedOverride = 0.0f;
    float waitSeconds = 0.0f;
    int facing = -1;
    std::string animState;
    PathWaypointAction action = PathWaypointAction::None;
    float speechDurationSeconds = 2.0f;
    std::string speechText;
};

enum class PathBehavior {
    Idle = 0,
    Patrol = 1,
    Aggro = 2,
};

enum class PathCurveMode {
    Linear = 0,
    Spline = 1,
};

struct EnemyPlacement {
    std::string id = "enemy_1";
    std::string typeId = "enemy_1";
    PathBehavior behavior = PathBehavior::Patrol;
    PathCurveMode curveMode = PathCurveMode::Linear;
    float speedOverride = 0.0f;
    bool loop = true;
    bool respawn = false;
    bool renderAboveWalls = false;
    std::vector<PathWaypoint> waypoints;
};

struct NpcPlacement {
    std::string id = "npc_1";
    std::string typeId = "npc_1";
    float x = 0.0f;
    float y = 0.0f;
    int facing = 0; // 0=S, 1=N, 2=E, 3=W
    float awarenessRadius = 64.0f;
    float interactionRadius = 24.0f;
    NpcMovementMode movementOverride = NpcMovementMode::Stationary;
    PathCurveMode curveMode = PathCurveMode::Linear;
    std::vector<PathWaypoint> waypoints;
    bool loop = true;
    float speedOverride = 0.0f;
    std::string graphOverride;
    std::vector<DialogueLine> dialogueOverride;
    std::vector<ShopItemDef> shopInventoryOverride;
};

struct ScreenLink {
    std::string north;
    std::string south;
    std::string east;
    std::string west;
};

// An animated tile is a sprite asset placed on a map cell. The runtime cycles the
// sprite's frames over the static screen graphics. `layer` controls draw order
// relative to the wall texture: 0 = above floor (player walks over it),
// 1 = above walls / overlay (player walks behind it).
struct AnimatedTilePlacement {
    std::string spriteId;
    int cellX = 0;
    int cellY = 0;
    int layer = 0;
};

struct ChapterScreen {
    std::string id = "screen_1";
    std::string mapId = "new_map";
    int gridX = 0;
    int gridY = 0;
    ScreenLink links;
    bool respawnEnemies = false;
    std::string musicPath;
    bool musicLoop = true;
    std::vector<EnemyPlacement> enemies;
    std::vector<NpcPlacement> npcs;
    std::vector<AnimatedTilePlacement> animatedTiles;
};

struct Chapter {
    std::string id = "chapter_1";
    std::string startScreenId = "screen_1";
    std::string playableCharacterId;
    std::vector<std::string> importedCharacterIds;
    std::vector<ChapterScreen> screens{{}};
};

[[nodiscard]] const ChapterScreen* findScreen(const Chapter& chapter, const std::string& screenId);
[[nodiscard]] bool saveChapter(const std::filesystem::path& path, const Chapter& chapter, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadChapter(const std::filesystem::path& path, Chapter& chapter, std::string* errorMessage = nullptr);

} // namespace adventure::game
