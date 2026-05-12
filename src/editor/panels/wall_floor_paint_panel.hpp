#pragma once

#include "editor/editor_context.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

struct PaintLayer {
    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    std::vector<std::uint32_t> pixels;
};

class WallFloorPaintPanel {
public:
    void draw(EditorContext& context);

private:
    enum class ActiveLayer {
        Floor = 0,
        Wall = 1,
    };

    enum class PaintTool {
        Pencil = 0,
        Eraser,
        Fill,
        Line,
        Rect,
    };

    std::array<char, 64> assetId_{'r', 'o', 'o', 'm', '_', 'p', 'a', 'i', 'n', 't', '\0'};
    int width_ = 64;
    int height_ = 36;
    int zoom_ = 10;
    int brushSize_ = 1;
    int resizeWidth_ = 64;
    int resizeHeight_ = 36;
    ActiveLayer activeLayer_ = ActiveLayer::Wall;
    PaintTool tool_ = PaintTool::Pencil;
    bool showGrid_ = true;
    bool animatePreview_ = true;
    float previewScrollX_ = 0.0f;
    float previewScrollY_ = 0.0f;
    float floorParallax_ = 0.45f;
    std::array<int, 2> dragStart_{-1, -1};
    std::array<int, 2> lastPaint_{-1, -1};
    bool strokeCaptured_ = false;
    std::uint32_t activeColor_ = 0xff3b82f6u;
    std::vector<std::uint32_t> palette_{
        0xff000000u, 0xffffffffu, 0xff3b82f6u, 0xff22c55eu,
        0xffef4444u, 0xfff59e0bu, 0xff8b5cf6u, 0xff64748bu,
        0xff7c2d12u, 0xff0f766eu, 0xfff8fafcu, 0xff1f2937u,
    };
    PaintLayer floor_{"Floor", true, 1.0f};
    PaintLayer wall_{"Wall", true, 1.0f};
    std::vector<std::uint32_t> undoFloor_;
    std::vector<std::uint32_t> undoWall_;
    std::string status_;

    void ensureDocument();
    void resizeDocument(int width, int height);
    void drawToolbar(EditorContext& context);
    void drawLayerControls();
    void drawPalette();
    void drawCanvas();
    void drawParallaxPreview();
    void drawToolButton(const char* label, PaintTool tool);
    void drawLayerPixels(ImDrawList* drawList, const PaintLayer& layer, ImVec2 origin, float pixelSize, ImVec2 offset = {0.0f, 0.0f}, bool wrap = false) const;
    void drawCompositePixel(ImDrawList* drawList, ImVec2 min, ImVec2 max, std::uint32_t color, float layerOpacity) const;
    void handleCanvasInput(const ImVec2& origin, float pixelSize);
    bool canvasPixelAt(const ImVec2& origin, float pixelSize, int& x, int& y) const;
    PaintLayer& activeLayer();
    const PaintLayer& activeLayer() const;
    void recordUndo();
    void undo();
    void setPixel(PaintLayer& layer, int x, int y, std::uint32_t color);
    void setBrushPixel(PaintLayer& layer, int x, int y, std::uint32_t color);
    void paintStroke(int x0, int y0, int x1, int y1, std::uint32_t color);
    void drawLine(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color);
    void drawRect(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color);
    void floodFill(PaintLayer& layer, int startX, int startY, std::uint32_t color);
    void clearActiveLayer();
    std::vector<unsigned char> layerRgba(const PaintLayer& layer) const;
    std::vector<unsigned char> compositeRgba(bool parallaxPreview) const;
    void exportPngs(const EditorContext& context);
};

} // namespace adventure::editor
