#pragma once

#include "editor/editor_context.hpp"
#include "game/tileset.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

class MapEditorPanel {
public:
    void draw(EditorContext& context);
    void openMapId(EditorContext& context, const std::string& mapId);

private:
    enum class EditMode { Paint, Select, Paste };

    int width_ = 24;
    int height_ = 16;
    int tileSize_ = 16;
    int spawnX_ = 1;
    int spawnY_ = 1;
    int activeLayer_ = 1; // 0=floor, 1=mid, 2=ceiling
    bool testMode_ = false;
    float playerX_ = 16.0f;
    float playerY_ = 16.0f;
    uint16_t selectedTileId_ = 1;
    std::array<char, 64> mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};
    std::array<char, 64> tilesetId_{'\0'};
    std::array<std::vector<uint16_t>, 3> layers_ = {
        std::vector<uint16_t>(static_cast<std::size_t>(24 * 16), 0u),
        std::vector<uint16_t>(static_cast<std::size_t>(24 * 16), 0u),
        std::vector<uint16_t>(static_cast<std::size_t>(24 * 16), 0u),
    };
    game::TilesetDef loadedTileset_;
    bool tilesetLoaded_ = false;
    std::string status_;

    // Copy / paste state
    EditMode editMode_ = EditMode::Paint;
    bool selectionDragging_ = false;
    int selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;
    bool hasSelection_ = false;
    std::vector<uint16_t> clipboard_;
    int clipboardW_ = 0, clipboardH_ = 0;

    // Undo stack
    static constexpr int kMaxUndoSteps = 50;
    struct MapUndoState {
        std::array<std::vector<uint16_t>, 3> layers;
    };
    std::vector<MapUndoState> undoStack_;

    // Test-game state
    float testPlayerX_ = 16.0f;
    float testPlayerY_ = 16.0f;

    void resizeMap(int width, int height);
    void recordUndo();
    void undoMap();
    void drawToolbar(EditorContext& context);
    void drawTilesetPalette();
    void drawGrid(EditorContext& context);
    void drawWallOutlines(ImDrawList* drawList, ImVec2 origin) const;
    void drawTestGame();
    void startTestGame();
    void updateTestPlayer();
    void copySelection();
    [[nodiscard]] bool isSolid(uint16_t tileId) const;
    [[nodiscard]] bool playerCanMoveTo(float x, float y) const;
    [[nodiscard]] bool solidAtPixel(float x, float y) const;
    void saveMap(EditorContext& context);
    void loadMap(EditorContext& context);
    void loadTileset(EditorContext& context);
    [[nodiscard]] uint16_t& tileAt(int x, int y, int layer);
    [[nodiscard]] const uint16_t& tileAt(int x, int y, int layer) const;
};

} // namespace adventure::editor
