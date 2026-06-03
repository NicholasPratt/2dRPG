#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <string>

namespace adventure::editor {

class DoorPlacementPanel {
public:
    void draw(EditorContext& context);
    void openForScreen(EditorContext& context);
    void saveForScreen(EditorContext& context);

private:
    std::string loadedMapId_;
    game::TileMap bgMap_;
    bool bgMapLoaded_ = false;
    int selectedDoor_ = -1;
    float zoom_ = 1.5f;

    std::array<char, 64> doorId_{'d', 'o', 'o', 'r', '_', '1', '\0'};
    int tileX_ = 0;
    int tileY_ = 0;
    int widthTiles_ = 1;
    int heightTiles_ = 1;
    int lockMode_ = 0;
    std::array<char, 64> requiredItemId_{'\0'};
    bool consumeKey_ = false;
    std::array<char, 64> targetScreenId_{'\0'};
    int targetTileX_ = 1;
    int targetTileY_ = 1;
    std::array<char, 64> spriteId_{'\0'};
    std::array<char, 64> openingAnimation_{'\0'};

    void loadBackground(EditorContext& context);
    void drawToolbar(EditorContext& context);
    void drawDoorList(EditorContext& context);
    void drawInspector(EditorContext& context);
    void drawValidation(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void syncInspectorFromSelected(const EditorContext& context);
    void writeInspectorToSelected(EditorContext& context);
    void placeDoorAt(EditorContext& context, int tileX, int tileY);
};

} // namespace adventure::editor
