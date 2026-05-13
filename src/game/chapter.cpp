#include "game/chapter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxScreens = 1024;
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

    output << "ADCHAPTER 2\n";
    output << "id " << chapter.id << "\n";
    output << "start " << chapter.startScreenId << "\n";
    output << "screens " << chapter.screens.size() << "\n";
    for (const ChapterScreen& screen : chapter.screens) {
        output << "screen " << screen.id << ' ' << screen.mapId << ' ' << screen.gridX << ' ' << screen.gridY << "\n";
        output << "links "
               << encodedLink(screen.links.north) << ' '
               << encodedLink(screen.links.south) << ' '
               << encodedLink(screen.links.east) << ' '
               << encodedLink(screen.links.west) << "\n";
        output << "respawn " << (screen.respawnEnemies ? 1 : 0) << "\n";
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
    if (magic != "ADCHAPTER" || version < 1 || version > 2) {
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
