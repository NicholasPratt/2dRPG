#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct ScreenLink {
    std::string north;
    std::string south;
    std::string east;
    std::string west;
};

struct ChapterScreen {
    std::string id = "screen_1";
    std::string mapId = "new_map";
    int gridX = 0;
    int gridY = 0;
    ScreenLink links;
    bool respawnEnemies = false;
};

struct Chapter {
    std::string id = "chapter_1";
    std::string startScreenId = "screen_1";
    std::vector<ChapterScreen> screens{{}};
};

[[nodiscard]] const ChapterScreen* findScreen(const Chapter& chapter, const std::string& screenId);
[[nodiscard]] bool saveChapter(const std::filesystem::path& path, const Chapter& chapter, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadChapter(const std::filesystem::path& path, Chapter& chapter, std::string* errorMessage = nullptr);

} // namespace adventure::game
