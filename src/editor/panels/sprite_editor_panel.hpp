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

struct SpriteCel {
    std::vector<unsigned int> pixels;
};

struct SpriteClipboard {
    int width = 0;
    int height = 0;
    std::vector<unsigned int> pixels;
};

struct SpriteDocument {
    std::string id = "new_sprite";
    std::filesystem::path sourcePng;
    std::array<int, 2> canvasSize{16, 16};
    std::array<int, 2> gridSize{16, 16};
    std::array<int, 2> pivot{8, 8};
    std::vector<SpriteFrame> frames{{}};
    std::vector<SpriteLayer> layers{{}};
    std::vector<std::string> tags{"idle"};
    std::vector<unsigned int> palette{0xff000000u, 0xffffffffu, 0xff6abe30u, 0xff37946eu};
    std::vector<std::vector<SpriteCel>> cels;
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
    int primaryColor_ = 1;
    int secondaryColor_ = 0;
    int playbackFps_ = 12;
    bool runAnimationPreview_ = false;
    float previewTimeSeconds_ = 0.0f;
    std::array<int, 2> trackedCanvasSize_{16, 16};
    std::array<int, 2> newSpriteSize_{16, 16};
    std::array<int, 2> resizeSpriteSize_{16, 16};
    std::array<int, 2> resizeSelectionSize_{1, 1};
    std::array<int, 2> dragStartPixel_{0, 0};
    std::array<int, 2> lastPaintPixel_{-1, -1};
    std::array<int, 4> selectionBounds_{0, 0, 0, 0};
    std::array<int, 2> selectionDragCurrent_{0, 0};
    std::array<int, 2> moveDragOffset_{0, 0};
    int selectionFrame_ = 0;
    int selectionLayer_ = 0;
    bool openNewSpritePopup_ = false;
    bool openResizePopup_ = false;
    bool openResizeSelectionPopup_ = false;
    bool shapeDragActive_ = false;
    bool dragUsesSecondaryColor_ = false;
    bool hasSelection_ = false;
    bool selectionDragActive_ = false;
    bool moveDragActive_ = false;
    std::vector<unsigned int> moveSourcePixels_;
    SpriteClipboard clipboard_;

    void drawLeftRail();
    void drawCenterWorkspace();
    void drawRightInspector(EditorContext& context);
    void drawTopBar();
    void drawToolButton(const char* label, const char* tooltip, int toolIndex);
    void drawCanvas(const ImVec2& availableSize);
    void drawPreview(const ImVec2& availableSize);
    void drawSpritePixels(ImDrawList* drawList, ImVec2 origin, float pixelSize, int frameIndex) const;
    void drawFrames();
    void drawLayers();
    void drawPalette(EditorContext& context);
    void drawExport(EditorContext& context);
    void saveSpriteMetadata(const EditorContext& context) const;
    void exportSpriteSheetPng(const EditorContext& context);
    [[nodiscard]] int previewFrameIndex() const;
    void handleShortcuts();

    void ensureDocumentState();
    void createBlankSprite(int width, int height);
    void resizeSprite(int newWidth, int newHeight);
    void resizeCanvasStorage(int oldWidth, int oldHeight, int newWidth, int newHeight);
    void handleCanvasInput(const ImVec2& canvasOrigin, float pixelSize);
    bool canvasPixelAt(const ImVec2& canvasOrigin, float pixelSize, int& x, int& y) const;
    SpriteCel& activeCel();
    SpriteCel& celAt(int frameIndex, int layerIndex);
    const SpriteCel& celAt(int frameIndex, int layerIndex) const;
    void setPixel(SpriteCel& cel, int x, int y, unsigned int color);
    unsigned int currentPaletteColor(bool useSecondaryColor) const;
    unsigned int sampledVisibleColor(int x, int y) const;
    void paintStroke(int x0, int y0, int x1, int y1, bool useSecondaryColor);
    void applyClickTool(int x, int y, bool useSecondaryColor);
    void drawLine(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color, bool mirror);
    void drawRectangle(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color);
    void drawCircle(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color);
    void floodFill(SpriteCel& cel, int startX, int startY, unsigned int replacementColor);
    void shadePixel(int x, int y, bool useSecondaryColor);
    void drawSelectionOverlay(ImDrawList* drawList, ImVec2 canvasOrigin, float pixelSize) const;
    void clearSelection();
    bool selectionAppliesToActiveCel() const;
    bool pixelInSelection(int x, int y) const;
    void setSelectionFromDrag(int x0, int y0, int x1, int y1);
    void beginMoveDrag();
    void previewMoveSelection(int offsetX, int offsetY);
    void finalizeMoveSelection();
    void copySelection();
    void pasteSelection();
    void resizeSelection(int newWidth, int newHeight);
};

} // namespace adventure::editor
