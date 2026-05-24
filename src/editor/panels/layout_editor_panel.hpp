#pragma once

#include "editor/editor_context.hpp"
#include "game/chapter.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace adventure::editor {

class LayoutEditorPanel {
public:
    void draw(EditorContext& context);
    [[nodiscard]] bool loadChapterById(EditorContext& context, const std::string& chapterId);
    void createChapter(EditorContext& context, const std::string& chapterId);
    [[nodiscard]] bool saveCurrentChapter(EditorContext& context);
    [[nodiscard]] const game::Chapter& chapter() const { return chapter_; }
    void saveDirtyMaps(EditorContext& context);
    void applyContextSelectedScreenData(EditorContext& context);
    bool selectScreenById(EditorContext& context, const std::string& screenId);

private:
    game::Chapter chapter_;
    int selectedScreen_ = 0;
    int layoutTileSize_ = 10;
    int layoutActiveLayer_ = 1;
    std::uint16_t layoutSelectedTileId_ = 1;
    bool showGraphicsPreview_ = true;
    float graphicsPreviewOpacity_ = 0.85f;
    std::array<char, 64> chapterId_{'c', 'h', 'a', 'p', 't', 'e', 'r', '_', '1', '\0'};
    std::unordered_map<std::string, game::TileMap> loadedMaps_;
    std::unordered_set<std::string> dirtyMapIds_;
    std::string status_;
    struct GraphicsPreview {
        std::filesystem::path path;
        std::filesystem::file_time_type lastWrite{};
        int mapWidth = 0;
        int mapHeight = 0;
        bool loaded = false;
        std::vector<std::uint32_t> tileColors;
    };
    std::unordered_map<std::string, GraphicsPreview> graphicsPreviews_;

    void drawToolbar(EditorContext& context);
    void drawScreenList(EditorContext& context);
    void drawMacroView(EditorContext& context);
    void drawScreenTileLayout(EditorContext& context, ImDrawList* drawList, const game::ChapterScreen& screen, ImVec2 min, float tileSize, bool selected);
    void drawGraphicsPreview(EditorContext& context, ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize);
    void drawMapTiles(ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize, bool selected) const;
    void drawWallOutlines(ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize, ImU32 color) const;
    void drawScreenInspector(EditorContext& context);
    void addScreen();
    void addConnectedScreen(EditorContext& context, const char* direction, int dx, int dy);
    void deleteSelectedScreen();
    [[nodiscard]] game::TileMap& ensureMapLoaded(EditorContext& context, const std::string& mapId);
    [[nodiscard]] game::ChapterScreen* screenAt(int gridX, int gridY);
    [[nodiscard]] game::ChapterScreen* screenById(const std::string& screenId);
    [[nodiscard]] std::string nextScreenId() const;
    void syncContextScreens(EditorContext& context) const;
    void syncSelectedScreenToContext(EditorContext& context) const;
    void syncChapterIdBuffer();
    [[nodiscard]] bool selectedScreenValid() const;
};

} // namespace adventure::editor
