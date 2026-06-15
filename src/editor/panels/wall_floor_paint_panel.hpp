#pragma once

#include "editor/editor_context.hpp"
#include "game/constants.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
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
    void openScreenGraphics(EditorContext& context, const std::string& mapId);
    bool saveForChapter(const EditorContext& context);
    void resetScreenBuffers();
    [[nodiscard]] const std::vector<std::uint32_t>& getPalette() const { return palette_; }
    void setPalette(std::vector<std::uint32_t> palette) { palette_ = std::move(palette); }

private:
    enum class ActiveLayer { Floor = 0, Wall = 1 };
    enum class PaintTool { Pencil = 0, Eraser, Darken, Lighten, Smudge, Fill, Line, Rect, Select, PickColor, TileDraw, TileSelect, TilePaste, TileStamp, TileFill, TileErase, TileRotate };
    enum class BrushShape { Square = 0, Circle, Spray, Dither };
    enum class SnapMode { None = 0, Full, Half, Quarter };
    enum class PasteReference { TopLeft = 0, TopRight, BottomRight, BottomLeft };

    static constexpr int kMaxUndoSteps = 50;

    struct UndoState {
        std::vector<std::uint32_t> floor;
        std::vector<std::uint32_t> wall;
    };

    struct PixelClipboard {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> floor;
        std::vector<std::uint32_t> wall;
    };

    struct ScreenGraphicsBuffer {
        std::vector<std::uint32_t> floor;
        std::vector<std::uint32_t> wall;
        bool dirty = false;
    };

    std::array<char, 64> assetId_{'r', 'o', 'o', 'm', '_', 'p', 'a', 'i', 'n', 't', '\0'};
    int width_ = game::kScreenTilesW * game::kTileSize;
    int height_ = game::kScreenTilesH * game::kTileSize;
    int zoom_ = 10;
    int brushSize_ = 1;
    int adjustmentIntensity_ = 20;
    int adjustmentGraduation_ = 35;
    int pixelsPerTile_ = game::kTileSize;
    ActiveLayer activeLayer_ = ActiveLayer::Wall;
    PaintTool tool_ = PaintTool::Pencil;
    BrushShape brushShape_ = BrushShape::Square;
    SnapMode snapMode_ = SnapMode::None;
    bool showGrid_ = true;
    bool showWallGuide_ = true;
    bool showObstacleOverlay_ = true;
    bool showAnimatedTileOverlay_ = true;
    std::string wallGuideMapId_;
    int wallGuideWidth_ = 0;
    int wallGuideHeight_ = 0;
    std::vector<std::uint8_t> wallGuide_;
    std::vector<game::MapObstacle> obstacleOverlay_;
    bool animatePreview_ = true;
    float previewScrollX_ = 0.0f;
    float previewScrollY_ = 0.0f;
    float previewAnimationX_ = 0.0f;
    float floorParallax_ = 0.45f;
    std::array<int, 2> dragStart_{-1, -1};
    std::array<int, 2> lastPaint_{-1, -1};
    bool strokeCaptured_ = false;
    std::vector<std::uint32_t> adjustmentStrokeBaseline_;
    std::uint32_t activeColor_ = 0xff940000u;
    std::vector<std::uint32_t> palette_{
        0xff000000u, 0xffffffffu, 0xff3b82f6u, 0xff22c55eu,
        0xffef4444u, 0xfff59e0bu, 0xff8b5cf6u, 0xff64748bu,
        0xff7c2d12u, 0xff0f766eu, 0xfff8fafcu, 0xff1f2937u,
    };
    PaintLayer floor_{"Floor", true, 1.0f};
    PaintLayer wall_{"Wall", true, 1.0f};
    std::vector<UndoState> undoStack_;
    bool documentDirty_ = false;
    std::string status_;

    // Selection / copy-paste
    bool selectionActive_ = false;
    bool selectionDragging_ = false;
    int selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;
    PixelClipboard pixelClipboard_;
    bool hasPixelClipboard_ = false;
    bool pasteMode_ = false;
    PasteReference pasteReference_ = PasteReference::TopLeft;

    // Tile stamp
    int stampTileIndex_ = -1;
    int stampFrameIndex_ = 0;
    int animatedTileStack_ = 0;

    // Per-tile sprite info: a palette tile whose linked sprite has >= 2 frames is
    // "animated" and stamps as a runtime animated tile instead of static pixels.
    std::unordered_map<std::string, int> spriteFrameCount_;                    // sprite id -> frame count
    std::unordered_map<std::string, std::array<int, 2>> animTileFootprints_;   // sprite id -> {tilesW, tilesH}

    // Per-screen graphics buffers
    std::string currentScreenId_;
    std::unordered_map<std::string, ScreenGraphicsBuffer> screenBuffers_;

    // LCG seed for spray brush
    std::uint32_t spraySeed_ = 12345u;

    void ensureDocument();
    void resizeDocument(int width, int height);
    void drawScreenNavigator(EditorContext& context);
    void drawToolbar(EditorContext& context);
    void drawLayerControls(EditorContext& context);
    void drawPalette();
    void drawTilePalette(EditorContext& context);
    void refreshTileSpriteInfo(const EditorContext& context);
    void editTileSprite(EditorContext& context, int index);
    [[nodiscard]] bool tileIsAnimated(const TilePaletteEntry& tile) const;
    void addToTilePalette(EditorContext& context);
    void addTileSelectionToPalette(EditorContext& context);
    void stampTile(int x, int y, const TilePaletteEntry& tile);
    void placeAnimatedTile(EditorContext& context, const TilePaletteEntry& tile, int cellX, int cellY);
    void removeAnimatedTileAt(EditorContext& context, int cellX, int cellY, bool allStacks);
    void floodFillTile(int x, int y, const TilePaletteEntry& tile);
    void fillTile(int x, int y, std::uint32_t color);
    void eraseTile(int x, int y);
    void rotateTile(int x, int y, bool clockwise);
    void drawCanvas(EditorContext& context);
    void drawWallGuide(ImDrawList* drawList, ImVec2 origin, float pixelSize) const;
    void drawObstacleOverlay(ImDrawList* drawList, ImVec2 origin, float pixelSize) const;
    void drawParallaxPreview();
    void drawToolButton(const char* label, PaintTool tool);
    void drawBrushShapeButton(const char* label, BrushShape shape);
    void drawLayerPixels(ImDrawList* drawList, const PaintLayer& layer, ImVec2 origin, float pixelSize, ImVec2 offset = {0.0f, 0.0f}, bool wrap = false) const;
    void drawCompositePixel(ImDrawList* drawList, ImVec2 min, ImVec2 max, std::uint32_t color, float layerOpacity) const;
    void handleCanvasInput(EditorContext& context, const ImVec2& origin, float pixelSize);
    bool canvasPixelAt(const ImVec2& origin, float pixelSize, int& x, int& y) const;
    [[nodiscard]] bool sampleVisibleGraphicsColor(int x, int y, std::uint32_t& color, std::string& layerName) const;
    int applySnap(int coord, int gridSize) const;
    int snapCoord(int coord) const;
    PaintLayer& activeLayer();
    const PaintLayer& activeLayer() const;
    void recordUndo();
    void undo();
    void setPixel(PaintLayer& layer, int x, int y, std::uint32_t color);
    void setBrushPixel(PaintLayer& layer, int x, int y, std::uint32_t color);
    void setAdjustmentBrushPixel(PaintLayer& layer, int x, int y, bool lighten);
    void setSmudgeBrushPixel(PaintLayer& layer, int x, int y, int sourceX, int sourceY);
    void paintStroke(int x0, int y0, int x1, int y1, std::uint32_t color);
    void paintAdjustmentStroke(int x0, int y0, int x1, int y1, bool lighten);
    void paintSmudgeStroke(int x0, int y0, int x1, int y1);
    [[nodiscard]] std::uint32_t nearestAtariColor(
        std::uint32_t red, std::uint32_t green, std::uint32_t blue) const;
    void drawLine(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color);
    void drawRect(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color);
    void floodFill(PaintLayer& layer, int startX, int startY, std::uint32_t color);
    void clearActiveLayer();
    void copyPixelSelection();
    void copyTileSelection();
    void pastePixelClipboard(int x, int y);
    void cyclePasteReference();
    [[nodiscard]] std::array<int, 2> placementOrigin(
        int referenceX, int referenceY, int contentWidth, int contentHeight, int referenceSpan = 1) const;
    [[nodiscard]] const char* pasteReferenceName() const;
    std::vector<unsigned char> layerRgba(const PaintLayer& layer) const;
    std::vector<unsigned char> compositeRgba(bool parallaxPreview) const;
    bool exportPngs(const EditorContext& context);
    bool exportScreenPngs(const EditorContext& context, const std::string& id, const ScreenGraphicsBuffer& buf);
};

} // namespace adventure::editor
