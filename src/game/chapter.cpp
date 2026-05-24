#include "game/chapter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxScreens = 1024;
constexpr int kMaxScreenEnemies = 2048;
constexpr int kMaxEnemyWaypoints = 4096;
constexpr int kMinGridCoord = -512;
constexpr int kMaxGridCoord = 512;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validToken(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    });
}

std::string encodedLink(const std::string& value)
{
    return value.empty() ? "-" : value;
}

std::string decodedLink(const std::string& value)
{
    return value == "-" ? std::string{} : value;
}

bool validChapterShape(const Chapter& chapter)
{
    if (!validToken(chapter.id) || !validToken(chapter.startScreenId) ||
        chapter.screens.empty() || static_cast<int>(chapter.screens.size()) > kMaxScreens) {
        return false;
    }

    for (const ChapterScreen& screen : chapter.screens) {
        if (!validToken(screen.id) || !validToken(screen.mapId)) {
            return false;
        }
        if (screen.gridX < kMinGridCoord || screen.gridX > kMaxGridCoord ||
            screen.gridY < kMinGridCoord || screen.gridY > kMaxGridCoord) {
            return false;
        }
        if (screen.enemies.size() > kMaxScreenEnemies) {
            return false;
        }
        for (const EnemyPlacement& enemy : screen.enemies) {
            if (!validToken(enemy.id) || !validToken(enemy.typeId) ||
                enemy.speedOverride < 0.0f || enemy.waypoints.size() > kMaxEnemyWaypoints) {
                return false;
            }
        }
    }

    return findScreen(chapter, chapter.startScreenId) != nullptr;
}

} // namespace

const ChapterScreen* findScreen(const Chapter& chapter, const std::string& screenId)
{
    for (const ChapterScreen& screen : chapter.screens) {
        if (screen.id == screenId) {
            return &screen;
        }
    }
    return nullptr;
}

bool saveChapter(const std::filesystem::path& path, const Chapter& chapter, std::string* errorMessage)
{
    if (!validChapterShape(chapter)) {
        setError(errorMessage, "Chapter id, start screen, or screen data is invalid.");
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open chapter file for writing.");
        return false;
    }

    output << "ADCHAPTER 4\n";
    output << "id " << chapter.id << "\n";
    output << "start " << chapter.startScreenId << "\n";
    output << "playable " << (chapter.playableCharacterId.empty() ? "-" : chapter.playableCharacterId) << "\n";
    output << "characters " << chapter.importedCharacterIds.size() << "\n";
    for (const std::string& id : chapter.importedCharacterIds) {
        output << "character " << id << "\n";
    }
    output << "screens " << chapter.screens.size() << "\n";
    for (const ChapterScreen& screen : chapter.screens) {
        output << "screen " << screen.id << ' ' << screen.mapId << ' ' << screen.gridX << ' ' << screen.gridY << "\n";
        output << "links "
               << encodedLink(screen.links.north) << ' '
               << encodedLink(screen.links.south) << ' '
               << encodedLink(screen.links.east) << ' '
               << encodedLink(screen.links.west) << "\n";
        output << "respawn " << (screen.respawnEnemies ? 1 : 0) << "\n";
        output << "enemies " << screen.enemies.size() << "\n";
        for (const EnemyPlacement& enemy : screen.enemies) {
            output << "enemy " << enemy.id << ' ' << enemy.typeId << ' '
                   << static_cast<int>(enemy.behavior) << ' ' << static_cast<int>(enemy.curveMode) << ' '
                   << enemy.speedOverride << ' ' << (enemy.loop ? 1 : 0) << ' ' << (enemy.respawn ? 1 : 0) << ' '
                   << enemy.waypoints.size() << "\n";
            for (const PathWaypoint& waypoint : enemy.waypoints) {
                output << "wp " << waypoint.x << ' ' << waypoint.y << "\n";
            }
        }
    }
    output << "end\n";

    if (!output) {
        setError(errorMessage, "Failed while writing chapter file.");
        return false;
    }

    return true;
}

bool loadChapter(const std::filesystem::path& path, Chapter& chapter, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open chapter file for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADCHAPTER" || version < 1 || version > 4) {
        setError(errorMessage, "Unsupported chapter file type or version.");
        return false;
    }

    Chapter loaded;
    std::string key;

    input >> key;
    if (key != "id") {
        setError(errorMessage, "Expected chapter id.");
        return false;
    }
    input >> loaded.id;

    input >> key;
    if (key != "start") {
        setError(errorMessage, "Expected chapter start screen.");
        return false;
    }
    input >> loaded.startScreenId;

    if (version >= 3) {
        input >> key >> loaded.playableCharacterId;
        if (key != "playable") {
            setError(errorMessage, "Expected playable character.");
            return false;
        }
        if (loaded.playableCharacterId == "-") {
            loaded.playableCharacterId.clear();
        }

        int characterCount = 0;
        input >> key >> characterCount;
        if (key != "characters" || characterCount < 0) {
            setError(errorMessage, "Expected character import count.");
            return false;
        }
        loaded.importedCharacterIds.clear();
        loaded.importedCharacterIds.reserve(static_cast<std::size_t>(characterCount));
        for (int i = 0; i < characterCount; ++i) {
            std::string characterId;
            input >> key >> characterId;
            if (key != "character" || characterId.empty()) {
                setError(errorMessage, "Expected character import.");
                return false;
            }
            loaded.importedCharacterIds.push_back(characterId);
        }
    }

    int screenCount = 0;
    input >> key >> screenCount;
    if (key != "screens" || screenCount <= 0 || screenCount > kMaxScreens) {
        setError(errorMessage, "Expected a valid screen count.");
        return false;
    }

    loaded.screens.clear();
    loaded.screens.reserve(static_cast<std::size_t>(screenCount));
    for (int i = 0; i < screenCount; ++i) {
        ChapterScreen screen;
        input >> key;
        if (key != "screen") {
            setError(errorMessage, "Expected screen entry.");
            return false;
        }
        input >> screen.id >> screen.mapId >> screen.gridX >> screen.gridY;
        if (!input) {
            setError(errorMessage, "Failed to read screen entry.");
            return false;
        }

        std::string north;
        std::string south;
        std::string east;
        std::string west;
        input >> key >> north >> south >> east >> west;
        if (key != "links" || !input) {
            setError(errorMessage, "Expected screen links.");
            return false;
        }
        screen.links.north = decodedLink(north);
        screen.links.south = decodedLink(south);
        screen.links.east = decodedLink(east);
        screen.links.west = decodedLink(west);

        if (version >= 2) {
            std::string respawnKey;
            int respawnVal = 0;
            input >> respawnKey >> respawnVal;
            if (respawnKey != "respawn" || !input) {
                setError(errorMessage, "Expected respawn flag.");
                return false;
            }
            screen.respawnEnemies = (respawnVal != 0);
        }

        if (version >= 4) {
            int enemyCount = 0;
            input >> key >> enemyCount;
            if (key != "enemies" || !input || enemyCount < 0 || enemyCount > kMaxScreenEnemies) {
                setError(errorMessage, "Expected valid enemy count.");
                return false;
            }
            screen.enemies.reserve(static_cast<std::size_t>(enemyCount));
            for (int enemyIndex = 0; enemyIndex < enemyCount; ++enemyIndex) {
                EnemyPlacement enemy;
                int behavior = 1;
                int curve = 0;
                int loopVal = 1;
                int respawnVal = 0;
                int waypointCount = 0;
                input >> key;
                if (key != "enemy") {
                    setError(errorMessage, "Expected enemy entry.");
                    return false;
                }
                input >> enemy.id >> enemy.typeId >> behavior >> curve >> enemy.speedOverride
                      >> loopVal >> respawnVal >> waypointCount;
                if (!input || waypointCount < 0 || waypointCount > kMaxEnemyWaypoints) {
                    setError(errorMessage, "Invalid enemy entry.");
                    return false;
                }
                enemy.behavior = static_cast<PathBehavior>(std::clamp(behavior, 0, 2));
                enemy.curveMode = static_cast<PathCurveMode>(std::clamp(curve, 0, 1));
                enemy.loop = loopVal != 0;
                enemy.respawn = respawnVal != 0;
                enemy.waypoints.reserve(static_cast<std::size_t>(waypointCount));
                for (int waypointIndex = 0; waypointIndex < waypointCount; ++waypointIndex) {
                    PathWaypoint waypoint;
                    input >> key >> waypoint.x >> waypoint.y;
                    if (key != "wp" || !input) {
                        setError(errorMessage, "Expected enemy waypoint.");
                        return false;
                    }
                    enemy.waypoints.push_back(waypoint);
                }
                screen.enemies.push_back(std::move(enemy));
            }
        }

        loaded.screens.push_back(std::move(screen));
    }

    input >> key;
    if (key != "end") {
        setError(errorMessage, "Expected chapter end marker.");
        return false;
    }

    if (!validChapterShape(loaded)) {
        setError(errorMessage, "Loaded chapter data is invalid.");
        return false;
    }

    chapter = std::move(loaded);
    return true;
}

} // namespace adventure::game
