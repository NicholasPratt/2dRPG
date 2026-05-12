#pragma once

#include "editor/editor_context.hpp"
#include "game/tileset.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

class MapEditorPanel {
public:
    void draw(EditorContext& context);

private:
    int width_ = 24;
    int height_ = 16;
    int tileSize_ = 16;
    int spawnX_ = 1;
    int spawnY_ = 1;
    bool testMode_ = false;
    float playerX_ = 16.0f;
    float playerY_ = 16.0f;
    uint16_t selectedTileId_ = 1;
    std::array<char, 64>  mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};
    std::array<char, 64>  tilesetId_{'\0'};
    std::vector<uint16_t> tiles_ = std::vector<uint16_t>(static_cast<std::size_t>(24 * 16), 0u);
    game::TilesetDef      loadedTileset_;
    bool                  tilesetLoaded_ = false;
    std::string           status_;

    void resizeMap(int width, int height);
    void drawToolbar(EditorContext& context);
    void drawTilesetPalette();
    void drawGrid();
    void drawTestGame();
    void startTestGame();
    void updateTestPlayer();
    [[nodiscard]] bool isSolid(uint16_t tileId) const;
    [[nodiscard]] bool playerCanMoveTo(float x, float y) const;
    [[nodiscard]] bool solidAtPixel(float x, float y) const;
    void saveMap(EditorContext& context);
    void loadMap(EditorContext& context);
    void loadTileset(EditorContext& context);
    [[nodiscard]] uint16_t& tileAt(int x, int y);
    [[nodiscard]] const uint16_t& tileAt(int x, int y) const;
};

} // namespace adventure::editor
