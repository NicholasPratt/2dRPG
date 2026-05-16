#pragma once

#include "editor/editor_context.hpp"
#include "game/constants.hpp"
#include "game/tileset.hpp"

#include <array>
#include <string>

namespace adventure::editor {

class TilesetEditorPanel {
public:
    void draw(EditorContext& context);

private:
    std::array<char, 64>  tilesetId_{'n', 'e', 'w', '_', 't', 'i', 'l', 'e', 's', 'e', 't', '\0'};
    std::array<char, 256> sourcePath_{'\0'};
    int tileWidthPx_ = game::kTileSize;
    int tileHeightPx_ = game::kTileSize;
    int cols_ = 8;
    int rows_ = 8;
    game::TilesetDef tileset_;
    std::string status_;

    void drawToolbar(EditorContext& context);
    void drawTileList();
    void generateTiles();
    void saveTileset(EditorContext& context);
    void loadTileset(EditorContext& context);
};

} // namespace adventure::editor
