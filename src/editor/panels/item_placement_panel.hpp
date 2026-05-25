#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"
#include "game/tileset.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

class ItemPlacementPanel {
public:
    void draw(EditorContext& context);
    void openForScreen(EditorContext& context);
    void saveForScreen(EditorContext& context);

private:
    struct PixelLayer {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
        bool loaded = false;
    };

    std::string loadedMapId_;
    game::TileMap bgMap_;
    bool bgMapLoaded_ = false;
    PixelLayer floorGraphics_;
    PixelLayer wallGraphics_;

    int selectedItem_ = -1;
    float zoom_ = 1.5f;

    // Inspector buffers
    std::array<char, 64> itemId_{'i', 't', 'e', 'm', '_', '1', '\0'};
    int pickupType_ = 1;
    std::array<char, 64> targetId_{'\0'};
    int quantity_ = 5;
    bool respawn_ = false;
    std::array<char, 64> spriteId_{'\0'};

    void drawToolbar(EditorContext& context);
    void drawItemList(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void drawInspector(EditorContext& context);
    void loadBackground(EditorContext& context);
    void syncInspectorFromSelected(const EditorContext& context);
    void writeInspectorToSelected(EditorContext& context);
    void drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const;
    void placeItemAt(EditorContext& context, float worldX, float worldY);
};

} // namespace adventure::editor
