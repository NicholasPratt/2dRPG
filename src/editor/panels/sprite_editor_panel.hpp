#pragma once

#include "editor/editor_context.hpp"

#include "imgui.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

struct SpriteFrame {
    int x = 0;
    int y = 0;
    int width = 16;
    int height = 16;
    int durationMs = 100;
};

struct SpriteLayer {
    std::string name = "Layer 1";
    bool visible = true;
    float opacity = 1.0f;
};

struct SpriteDocument {
    std::string id = "new_sprite";
    std::filesystem::path sourcePng;
    std::array<int, 2> canvasSize{16, 16};
    std::array<int, 2> gridSize{16, 16};
    std::array<int, 2> pivot{8, 12};
    std::vector<SpriteFrame> frames{{}};
    std::vector<SpriteLayer> layers{{}};
    std::vector<std::string> tags{"idle"};
    std::vector<unsigned int> palette{0xff000000u, 0xffffffffu, 0xff6abe30u, 0xff37946eu};
};

class SpriteEditorPanel {
public:
    void draw(EditorContext& context);

private:
    SpriteDocument document_;
    bool showGrid_ = true;
    bool onionSkin_ = false;
    int zoom_ = 8;
    int selectedFrame_ = 0;
    int selectedLayer_ = 0;
    int selectedTool_ = 0;
    int primaryColor_ = 3;
    int secondaryColor_ = 0;
    int playbackFps_ = 12;

    void drawLeftRail();
    void drawCenterWorkspace();
    void drawRightInspector(EditorContext& context);
    void drawTopBar();
    void drawToolButton(const char* label, const char* tooltip, int toolIndex);
    void drawCanvas(const ImVec2& availableSize);
    void drawPreview(const ImVec2& availableSize);
    void drawSpritePixels(ImDrawList* drawList, ImVec2 origin, float pixelSize) const;
    void drawFrames();
    void drawLayers();
    void drawPalette(EditorContext& context);
    void drawExport(EditorContext& context);
    void saveSpriteMetadata(const EditorContext& context) const;
};

} // namespace adventure::editor
