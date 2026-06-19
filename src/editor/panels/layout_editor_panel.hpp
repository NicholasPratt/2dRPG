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
    void drawScreenMusicSfx(EditorContext& context);

private:
    enum class LayoutPaintTool { Brush, WallLine, WallRect };

    game::Chapter chapter_;
    int selectedScreen_ = 0;
    int layoutTileSize_ = 10;
    int layoutActiveLayer_ = 1;
    std::uint16_t layoutSelectedTileId_ = 1;
    LayoutPaintTool layoutPaintTool_ = LayoutPaintTool::Brush;
    bool layoutShapeDragging_ = false;
    bool layoutShapeErase_ = false;
    LayoutPaintTool layoutDragTool_ = LayoutPaintTool::Brush;
    int layoutDragLayer_ = 1;
    std::uint16_t layoutDragTileId_ = 1;
    std::string layoutShapeMapId_;
    int layoutShapeX0_ = 0;
    int layoutShapeY0_ = 0;
    int layoutShapeX1_ = 0;
    int layoutShapeY1_ = 0;
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
        double lastCheckedSeconds = 0.0;
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
    void drawLayoutShapePreview(ImDrawList* drawList, ImVec2 min, float tileSize) const;
    void paintLayoutLine(game::TileMap& map, int layer, std::uint16_t tileId);
    void paintLayoutRect(game::TileMap& map, int layer, std::uint16_t tileId);
    void drawScreenInspector(EditorContext& context);
    void drawMusicFilePicker(EditorContext& context, game::ChapterScreen& screen);
    void drawWalkingSfxFilePicker(EditorContext& context, game::ChapterScreen& screen);
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
