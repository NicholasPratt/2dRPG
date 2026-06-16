#include "editor/panels/wall_floor_paint_panel.hpp"

#include "editor/atari_2600_palette.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/sprite.hpp"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace adventure::editor {
namespace {

constexpr int kMinCanvasSize = 4;
constexpr int kMaxCanvasSize = 1024;
constexpr int kMaxChapterTiles = 32;
constexpr int kMaxTileFrames = 16;
constexpr unsigned char kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
constexpr int kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

ImU32 packedColor(std::uint32_t color)
{
    return IM_COL32((color >> 0) & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, (color >> 24) & 0xff);
}

std::uint8_t alphaOf(std::uint32_t color)
{
    return static_cast<std::uint8_t>((color >> 24) & 0xff);
}

float radialBrushCoverage(int offsetX, int offsetY, int radius)
{
    if (radius <= 0) {
        return 1.0f;
    }
    const float outerRadius = static_cast<float>(radius) + 0.5f;
    const float distance = std::sqrt(
        static_cast<float>(offsetX * offsetX + offsetY * offsetY));
    return std::clamp((outerRadius - distance) / std::max(1.0f, outerRadius * 0.45f), 0.0f, 1.0f);
}

TilePaletteFrame* frameAt(TilePaletteEntry& tile, int frameIndex)
{
    if (tile.frames.empty()) {
        return nullptr;
    }
    frameIndex = std::clamp(frameIndex, 0, static_cast<int>(tile.frames.size()) - 1);
    return &tile.frames[static_cast<std::size_t>(frameIndex)];
}

const TilePaletteFrame* frameAt(const TilePaletteEntry& tile, int frameIndex)
{
    if (tile.frames.empty()) {
        return nullptr;
    }
    frameIndex = std::clamp(frameIndex, 0, static_cast<int>(tile.frames.size()) - 1);
    return &tile.frames[static_cast<std::size_t>(frameIndex)];
}

int animatedFrameIndex(const TilePaletteEntry& tile)
{
    if (tile.frames.empty()) {
        return 0;
    }
    const int durationMs = std::max(40, tile.frameDurationMs);
    const double seconds = ImGui::GetTime();
    return static_cast<int>((seconds * 1000.0) / static_cast<double>(durationMs)) %
        static_cast<int>(tile.frames.size());
}

void appendBigEndian32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<unsigned char>(value & 0xffu));
}

std::uint32_t crc32(const unsigned char* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1u));
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::uint32_t adler32(const unsigned char* data, std::size_t size)
{
    constexpr std::uint32_t kMod = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

void appendChunk(std::vector<unsigned char>& png, const char* type, const std::vector<unsigned char>& data)
{
    appendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = png.size();
    png.push_back(static_cast<unsigned char>(type[0]));
    png.push_back(static_cast<unsigned char>(type[1]));
    png.push_back(static_cast<unsigned char>(type[2]));
    png.push_back(static_cast<unsigned char>(type[3]));
    png.insert(png.end(), data.begin(), data.end());
    appendBigEndian32(png, crc32(png.data() + typeOffset, 4u + data.size()));
}

bool writePngRgba(const std::filesystem::path& path, int width, int height, const std::vector<unsigned char>& rgba)
{
    if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width * height * 4)) {
        return false;
    }

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0u);
        const auto rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(rowOffset),
            rgba.begin() + static_cast<std::ptrdiff_t>(rowOffset + static_cast<std::size_t>(width) * 4u));
    }

    std::vector<unsigned char> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535u * 5u + 16u);
    zlib.push_back(0x78u);
    zlib.push_back(0x01u);

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t remaining = raw.size() - offset;
        const std::uint16_t blockSize = static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535u));
        const bool finalBlock = offset + blockSize == raw.size();
        zlib.push_back(finalBlock ? 0x01u : 0x00u);
        zlib.push_back(static_cast<unsigned char>(blockSize & 0xffu));
        zlib.push_back(static_cast<unsigned char>((blockSize >> 8) & 0xffu));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~blockSize);
        zlib.push_back(static_cast<unsigned char>(nlen & 0xffu));
        zlib.push_back(static_cast<unsigned char>((nlen >> 8) & 0xffu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }
    appendBigEndian32(zlib, adler32(raw.data(), raw.size()));

    std::vector<unsigned char> ihdr;
    appendBigEndian32(ihdr, static_cast<std::uint32_t>(width));
    appendBigEndian32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8u);
    ihdr.push_back(6u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);

    std::vector<unsigned char> png(std::begin(kPngSignature), std::end(kPngSignature));
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(output);
}

std::uint32_t blendOver(std::uint32_t dst, std::uint32_t src, float opacity)
{
    const float srcA = (static_cast<float>(alphaOf(src)) / 255.0f) * std::clamp(opacity, 0.0f, 1.0f);
    const float dstA = static_cast<float>(alphaOf(dst)) / 255.0f;
    const float outA = srcA + dstA * (1.0f - srcA);
    if (outA <= 0.0f) {
        return 0u;
    }

    const auto channel = [src, dst, srcA, dstA, outA](int shift) {
        const float s = static_cast<float>((src >> shift) & 0xffu) / 255.0f;
        const float d = static_cast<float>((dst >> shift) & 0xffu) / 255.0f;
        return static_cast<std::uint32_t>(std::round(((s * srcA + d * dstA * (1.0f - srcA)) / outA) * 255.0f));
    };

    return (static_cast<std::uint32_t>(std::round(outA * 255.0f)) << 24u) |
        (channel(16) << 16u) | (channel(8) << 8u) | channel(0);
}

void checkerboard(ImDrawList* drawList, ImVec2 min, ImVec2 max, float cellSize)
{
    cellSize = std::max(8.0f, cellSize);
    const ImVec2 clipMin = drawList->GetClipRectMin();
    const ImVec2 clipMax = drawList->GetClipRectMax();
    const float startX = std::max(min.x, clipMin.x);
    const float startY = std::max(min.y, clipMin.y);
    const float endX = std::min(max.x, clipMax.x);
    const float endY = std::min(max.y, clipMax.y);
    if (startX >= endX || startY >= endY) {
        return;
    }

    const float firstX = min.x + std::floor((startX - min.x) / cellSize) * cellSize;
    const float firstY = min.y + std::floor((startY - min.y) / cellSize) * cellSize;
    for (float y = firstY; y < endY; y += cellSize) {
        for (float x = firstX; x < endX; x += cellSize) {
            const bool even = (static_cast<int>((x - min.x) / cellSize) + static_cast<int>((y - min.y) / cellSize)) % 2 == 0;
            drawList->AddRectFilled({std::max(x, startX), std::max(y, startY)},
                {std::min(x + cellSize, endX), std::min(y + cellSize, endY)},
                even ? IM_COL32(58, 62, 66, 255) : IM_COL32(74, 79, 84, 255));
        }
    }
}

std::vector<std::uint32_t> rgbaToPixels(const unsigned char* rgba, int width, int height)
{
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int i = 0; i < width * height; ++i) {
        const int j = i * 4;
        pixels[static_cast<std::size_t>(i)] =
            (static_cast<std::uint32_t>(rgba[j + 3]) << 24) |
            (static_cast<std::uint32_t>(rgba[j + 2]) << 16) |
            (static_cast<std::uint32_t>(rgba[j + 1]) << 8) |
             static_cast<std::uint32_t>(rgba[j + 0]);
    }
    return pixels;
}

} // namespace

void WallFloorPaintPanel::draw(EditorContext& context)
{
    ensureDocument();
    if (!context.selectedScreenMapId.empty() && currentScreenId_ != context.selectedScreenMapId) {
        openScreenGraphics(context, context.selectedScreenMapId);
    }

    // Global keyboard shortcuts
    if (!ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEqual, false)) {
            zoom_ = std::min(16, zoom_ + 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
            zoom_ = std::max(1, zoom_ - 1);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            undo();
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            if (tool_ == PaintTool::Select && selectionActive_) {
                copyPixelSelection();
            } else if (tool_ == PaintTool::TileSelect && selectionActive_) {
                copyTileSelection();
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            if (tool_ == PaintTool::TileStamp && stampTileIndex_ >= 0) {
                cyclePasteReference();
            } else if (hasPixelClipboard_) {
                const bool wasPasting = pasteMode_ &&
                    (tool_ == PaintTool::Select || tool_ == PaintTool::TilePaste);
                pasteMode_ = true;
                tool_ = pixelClipboard_.width == game::kTileSize && pixelClipboard_.height == game::kTileSize
                    ? PaintTool::TilePaste
                    : PaintTool::Select;
                if (wasPasting) {
                    cyclePasteReference();
                } else {
                    status_ = std::string("Paste reference: ") + pasteReferenceName() +
                        ". Press Ctrl+V again to change corner.";
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pasteMode_ = false;
            if (tool_ == PaintTool::Select || tool_ == PaintTool::TileSelect) {
                selectionActive_ = false;
                selectionDragging_ = false;
            }
        }

        // Single-key shortcuts (no modifier) for tool selection and brush size.
        // Guarded against Ctrl/Cmd so they never collide with the copy/paste/undo
        // combos handled above.
        if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper) {
            auto selectTool = [&](PaintTool tool) {
                if (tool_ != tool) {
                    pasteMode_ = false;
                    adjustmentStrokeBaseline_.clear();
                    strokeCaptured_ = false;
                    tool_ = tool;
                }
            };
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) { selectTool(PaintTool::Pencil); }
            else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { selectTool(PaintTool::Eraser); }
            else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) { selectTool(PaintTool::Darken); }
            else if (ImGui::IsKeyPressed(ImGuiKey_G, false)) { selectTool(PaintTool::Lighten); }
            else if (ImGui::IsKeyPressed(ImGuiKey_U, false)) { selectTool(PaintTool::Smudge); }
            else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) { selectTool(PaintTool::Fill); }
            else if (ImGui::IsKeyPressed(ImGuiKey_L, false)) { selectTool(PaintTool::Line); }
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { selectTool(PaintTool::Rect); }
            else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { selectTool(PaintTool::Select); }
            else if (ImGui::IsKeyPressed(ImGuiKey_I, false)) { selectTool(PaintTool::PickColor); }

            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) {
                brushSize_ = std::max(1, brushSize_ - 1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) {
                brushSize_ = std::min(32, brushSize_ + 1);
            }
        }
    }

    drawToolbar(context);
    drawScreenNavigator(context);
    ImGui::Separator();

    const float availH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("WFPLeftPanel", ImVec2(288.0f, availH), false);
    drawLayerControls(context);
    drawPalette();
    drawTilePalette(context);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 6.0f);

    ImGui::BeginChild("WFPRightPanel", ImVec2(0.0f, availH), false);
    drawCanvas(context);
    ImGui::EndChild();

    if (documentDirty_) {
        context.markDirty();
    }
}

void WallFloorPaintPanel::openScreenGraphics(EditorContext& context, const std::string& mapId)
{
    if (mapId.empty()) {
        return;
    }

    // Stash current canvas before switching to a different screen
    if (!currentScreenId_.empty() && currentScreenId_ != mapId) {
        auto& buf = screenBuffers_[currentScreenId_];
        buf.floor = floor_.pixels;
        buf.wall = wall_.pixels;
        buf.dirty = buf.dirty || documentDirty_;
        undoStack_.clear();
        documentDirty_ = false;
    }

    std::memset(assetId_.data(), 0, assetId_.size());
    const std::size_t copyLen = std::min(mapId.size(), assetId_.size() - 1);
    std::memcpy(assetId_.data(), mapId.data(), copyLen);

    // Load wall guide (best-effort; missing map just clears the guide)
    game::TileMap map;
    std::string error;
    const std::filesystem::path mapPath = context.assets.gameMapPath() / (mapId + ".admap");
    if (game::loadTileMap(mapPath, map, &error)) {
        wallGuideMapId_ = map.id;
        wallGuideWidth_ = map.width;
        wallGuideHeight_ = map.height;
        obstacleOverlay_ = map.obstacles;
        wallGuide_.assign(static_cast<std::size_t>(wallGuideWidth_ * wallGuideHeight_), 0u);
        for (int y = 0; y < wallGuideHeight_; ++y) {
            for (int x = 0; x < wallGuideWidth_; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(wallGuideWidth_) + static_cast<std::size_t>(x);
                wallGuide_[idx] = map.layers[1][idx] == 0u ? 0u : 1u;
            }
        }
    } else {
        wallGuide_.clear();
        wallGuideWidth_ = 0;
        wallGuideHeight_ = 0;
        wallGuideMapId_.clear();
        obstacleOverlay_.clear();
    }

    const int pw = game::kScreenTilesW * game::kTileSize;
    const int ph = game::kScreenTilesH * game::kTileSize;
    if (width_ != pw || height_ != ph) {
        resizeDocument(pw, ph);
    }

    currentScreenId_ = mapId;
    context.selectedScreenMapId = mapId;
    for (const ChapterScreenEntry& screen : context.chapterScreens) {
        if (screen.mapId == mapId) {
            context.selectedScreenId = screen.id;
            break;
        }
    }
    // Ask the host to re-sync per-screen placements (animated tiles) for this screen.
    context.requestScreenPlacementSync = true;
    refreshTileSpriteInfo(context);
    selectionActive_ = false;
    selectionDragging_ = false;
    pasteMode_ = false;
    adjustmentStrokeBaseline_.clear();
    strokeCaptured_ = false;

    // Restore from in-memory buffer (highest priority)
    const auto expected = static_cast<std::size_t>(pw * ph);
    auto it = screenBuffers_.find(mapId);
    if (it != screenBuffers_.end() && it->second.floor.size() == expected) {
        floor_.pixels = it->second.floor;
        wall_.pixels  = it->second.wall;
        documentDirty_ = it->second.dirty;
        zoom_ = 2;
        showWallGuide_ = true;
        activeLayer_ = ActiveLayer::Wall;
        status_ = "Restored " + mapId + " from session buffer.";
        return;
    }

    // Fall back to loading from disk (previously exported PNGs)
    const std::filesystem::path floorPath = context.assets.rawTilesetPath() / (mapId + "_floor.png");
    const std::filesystem::path wallPath  = context.assets.rawTilesetPath() / (mapId + "_wall.png");
    bool loadedFloor = false;
    bool loadedWall  = false;
    {
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(floorPath.string().c_str(), &w, &h, &ch, 4);
        if (data && w == pw && h == ph) {
            floor_.pixels = rgbaToPixels(data, w, h);
            loadedFloor = true;
        } else {
            floor_.pixels.assign(expected, 0xff243447u);
        }
        stbi_image_free(data);
    }
    {
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(wallPath.string().c_str(), &w, &h, &ch, 4);
        if (data && w == pw && h == ph) {
            wall_.pixels = rgbaToPixels(data, w, h);
            loadedWall = true;
        } else {
            wall_.pixels.assign(expected, 0u);
        }
        stbi_image_free(data);
    }

    documentDirty_ = false;
    zoom_ = 2;
    showWallGuide_ = true;
    activeLayer_ = ActiveLayer::Wall;
    status_ = (loadedFloor || loadedWall) ? ("Loaded " + mapId + " from disk.") : ("New canvas for " + mapId + ".");
}

bool WallFloorPaintPanel::saveForChapter(const EditorContext& context)
{
    // Flush current canvas into the buffer map
    if (!currentScreenId_.empty()) {
        auto& buf = screenBuffers_[currentScreenId_];
        buf.floor = floor_.pixels;
        buf.wall  = wall_.pixels;
        buf.dirty = buf.dirty || documentDirty_;
    }

    bool allOk = true;
    for (auto& [id, buf] : screenBuffers_) {
        if (!buf.dirty) {
            continue;
        }
        if (exportScreenPngs(context, id, buf)) {
            buf.dirty = false;
        } else {
            allOk = false;
        }
    }

    if (allOk) {
        documentDirty_ = false;
    }
    return allOk;
}

void WallFloorPaintPanel::resetScreenBuffers()
{
    if (!currentScreenId_.empty()) {
        auto& buf = screenBuffers_[currentScreenId_];
        buf.floor = floor_.pixels;
        buf.wall  = wall_.pixels;
    }
    screenBuffers_.clear();
    currentScreenId_.clear();
    documentDirty_ = false;
    undoStack_.clear();
    adjustmentStrokeBaseline_.clear();
    strokeCaptured_ = false;
}

void WallFloorPaintPanel::ensureDocument()
{
    const auto expected = static_cast<std::size_t>(width_ * height_);
    if (floor_.pixels.size() != expected) {
        floor_.pixels.assign(expected, 0xff243447u);
    }
    if (wall_.pixels.size() != expected) {
        wall_.pixels.assign(expected, 0u);
    }
}

void WallFloorPaintPanel::resizeDocument(int width, int height)
{
    width = std::clamp(width, kMinCanvasSize, kMaxCanvasSize);
    height = std::clamp(height, kMinCanvasSize, kMaxCanvasSize);
    if (width == width_ && height == height_) {
        return;
    }

    auto resizeLayer = [this, width, height](PaintLayer& layer) {
        std::vector<std::uint32_t> resized(static_cast<std::size_t>(width * height), 0u);
        const int copyWidth = std::min(width_, width);
        const int copyHeight = std::min(height_, height);
        const std::size_t expectedOldSize = static_cast<std::size_t>(width_ * height_);
        if (layer.pixels.size() == expectedOldSize) {
            for (int y = 0; y < copyHeight; ++y) {
                for (int x = 0; x < copyWidth; ++x) {
                    resized[static_cast<std::size_t>(y * width + x)] = layer.pixels[static_cast<std::size_t>(y * width_ + x)];
                }
            }
        }
        layer.pixels = std::move(resized);
    };

    resizeLayer(floor_);
    resizeLayer(wall_);
    width_ = width;
    height_ = height;
}

void WallFloorPaintPanel::drawScreenNavigator(EditorContext& context)
{
    if (context.chapterScreens.empty()) {
        ImGui::TextDisabled("No chapter screens loaded.");
        return;
    }

    int selectedIndex = -1;
    for (int i = 0; i < static_cast<int>(context.chapterScreens.size()); ++i) {
        if (context.chapterScreens[static_cast<std::size_t>(i)].mapId == currentScreenId_ ||
            context.chapterScreens[static_cast<std::size_t>(i)].id == context.selectedScreenId) {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex < 0) {
        selectedIndex = 0;
    }

    auto openIndex = [&](int index) {
        if (index < 0 || index >= static_cast<int>(context.chapterScreens.size())) {
            return;
        }
        const ChapterScreenEntry& screen = context.chapterScreens[static_cast<std::size_t>(index)];
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        openScreenGraphics(context, screen.mapId);
    };

    ImGui::TextUnformatted("Screen");
    ImGui::SameLine();
    if (ImGui::Button("<") && selectedIndex > 0) {
        openIndex(selectedIndex - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button(">") && selectedIndex + 1 < static_cast<int>(context.chapterScreens.size())) {
        openIndex(selectedIndex + 1);
    }
    ImGui::SameLine();

    const ChapterScreenEntry& selected = context.chapterScreens[static_cast<std::size_t>(selectedIndex)];
    const std::string preview = selected.id + " -> " + selected.mapId;
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo("##ScreenGraphicsSelector", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(context.chapterScreens.size()); ++i) {
            const ChapterScreenEntry& screen = context.chapterScreens[static_cast<std::size_t>(i)];
            const bool selectedItem = i == selectedIndex;
            const std::string label = screen.id + " [" + std::to_string(screen.gridX) + "," +
                std::to_string(screen.gridY) + "] -> " + screen.mapId;
            if (ImGui::Selectable(label.c_str(), selectedItem)) {
                openIndex(i);
            }
            if (selectedItem) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void WallFloorPaintPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(180.0f);
    ui::inputTextString("Asset id", assetId_.data(), assetId_.size());
    ImGui::SameLine();
    ImGui::Text("%dx%d px", width_, height_);

    ui::sliderInt("Zoom =/-", "##ScreenGraphicsZoom", &zoom_, 1, 16, 80.0f);
    ImGui::SameLine(220.0f);
    ui::sliderInt("Brush", "##ScreenGraphicsBrush", &brushSize_, 1, 32, 80.0f);
    ui::checkbox("Grid", "##ScreenGraphicsGrid", &showGrid_);
    if (!wallGuide_.empty()) {
        ImGui::SameLine(220.0f);
        ui::checkbox("Wall guide", "##ScreenGraphicsWallGuide", &showWallGuide_);
    }
    ImGui::SameLine();
    ui::checkbox("Trap hints", "##ScreenGraphicsObstacles", &showObstacleOverlay_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%d pit/spike trap%s on this screen",
            static_cast<int>(obstacleOverlay_.size()), obstacleOverlay_.size() == 1 ? "" : "s");
    }
    ImGui::SameLine();
    ui::checkbox("Animated tiles", "##ScreenGraphicsAnimatedTiles", &showAnimatedTileOverlay_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show or hide placed animated-tile overlays. Hiding does not delete placements.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo")) {
        undo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", static_cast<int>(undoStack_.size()));
    ImGui::SameLine();
    if (ImGui::Button("Export PNGs")) {
        exportPngs(context);
    }

    ImGui::Text("Exports: %s", context.assets.rawTilesetPath().string().c_str());
    if (!wallGuideMapId_.empty()) {
        ImGui::Text("Wall guide: %s", wallGuideMapId_.c_str());
    }
    ImGui::TextDisabled("Tile Draw fills one 16x16 tile. Tile Select copies/pastes exact tiles. Ctrl+C copies; Ctrl+V pastes/cycles its corner.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void WallFloorPaintPanel::drawLayerControls(EditorContext& context)
{
    ImGui::TextUnformatted("Layers");
    int layerIndex = static_cast<int>(activeLayer_);
    ImGui::RadioButton("Wall", &layerIndex, static_cast<int>(ActiveLayer::Wall));
    ui::checkbox("Wall visible", "##WallLayerVisible", &wall_.visible);
    ui::sliderFloat("Wall opacity", "##WallLayerOpacity", &wall_.opacity, 0.0f, 1.0f, "%.2f", 120.0f);

    ImGui::RadioButton("Floor", &layerIndex, static_cast<int>(ActiveLayer::Floor));
    ui::checkbox("Floor visible", "##FloorLayerVisible", &floor_.visible);
    ui::sliderFloat("Floor opacity", "##FloorLayerOpacity", &floor_.opacity, 0.0f, 1.0f, "%.2f", 120.0f);
    activeLayer_ = static_cast<ActiveLayer>(layerIndex);

    ImGui::Spacing();
    ImGui::TextUnformatted("Tools");
    drawToolButton("Pencil", PaintTool::Pencil, "B");
    ImGui::SameLine();
    drawToolButton("Eraser", PaintTool::Eraser, "E");
    drawToolButton("Darken", PaintTool::Darken, "D");
    ImGui::SameLine();
    drawToolButton("Lighten", PaintTool::Lighten, "G");
    drawToolButton("Smudge", PaintTool::Smudge, "U");
    if (tool_ == PaintTool::Darken || tool_ == PaintTool::Lighten || tool_ == PaintTool::Smudge) {
        ui::sliderInt(tool_ == PaintTool::Smudge ? "Strength" : "Intensity",
            "##AdjustmentBrushIntensity", &adjustmentIntensity_,
            1, 100, 120.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("%d%%", adjustmentIntensity_);
        if (tool_ == PaintTool::Smudge) {
            ImGui::TextDisabled("Drag to blend; output snaps to Atari colors.");
        } else {
            ui::sliderInt("Graduation", "##AdjustmentBrushGraduation", &adjustmentGraduation_,
                0, 100, 120.0f);
            ImGui::SameLine();
            ImGui::TextDisabled("%d%%", adjustmentGraduation_);
            ImGui::TextDisabled("Graduation controls the feathered outer edge.");
        }
    }
    drawToolButton("Fill", PaintTool::Fill, "F");
    ImGui::SameLine();
    drawToolButton("Line", PaintTool::Line, "L");
    drawToolButton("Rect", PaintTool::Rect, "R");
    ImGui::SameLine();
    drawToolButton("Select", PaintTool::Select, "S");
    ImGui::SameLine();
    drawToolButton("Pick", PaintTool::PickColor, "I");
    if (tool_ == PaintTool::Select) {
        if (selectionActive_) {
            if (ImGui::Button("Copy", ImVec2(132.0f, 0.0f))) {
                copyPixelSelection();
            }
        }
        if (hasPixelClipboard_) {
            if (selectionActive_) {
                ImGui::SameLine();
            }
            if (ImGui::Button(pasteMode_ ? "Paste On" : "Paste", ImVec2(132.0f, 0.0f))) {
                pasteMode_ = !pasteMode_;
            }
            if (pasteMode_) {
                ImGui::TextDisabled("Reference: %s (Ctrl+V cycles)", pasteReferenceName());
            }
        }
        if (tool_ == PaintTool::Select && selectionActive_) {
            if (ImGui::Button("Add Palette", ImVec2(132.0f, 0.0f))) {
                addToTilePalette(context);
            }
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Tile tools");
    drawToolButton("Tile Draw", PaintTool::TileDraw);
    ImGui::SameLine();
    drawToolButton("Tile Select", PaintTool::TileSelect);
    if (tool_ == PaintTool::TileSelect) {
        if (selectionActive_) {
            if (ImGui::Button("Tile Copy", ImVec2(132.0f, 0.0f))) {
                copyTileSelection();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Palette", ImVec2(132.0f, 0.0f))) {
                addTileSelectionToPalette(context);
            }
        }
        if (hasPixelClipboard_ && pixelClipboard_.width == game::kTileSize && pixelClipboard_.height == game::kTileSize) {
            if (ImGui::Button("Tile Paste", ImVec2(132.0f, 0.0f))) {
                tool_ = PaintTool::TilePaste;
                pasteMode_ = true;
            }
        }
    }
    drawToolButton("Tile Paste", PaintTool::TilePaste);
    ImGui::SameLine();
    drawToolButton("Stamp", PaintTool::TileStamp);
    if ((tool_ == PaintTool::TilePaste && hasPixelClipboard_) ||
        (tool_ == PaintTool::TileStamp && stampTileIndex_ >= 0)) {
        ImGui::TextDisabled("Reference: %s", pasteReferenceName());
        ImGui::TextDisabled("Press Ctrl+V to use another corner.");
    }
    if (tool_ == PaintTool::TileStamp && stampTileIndex_ >= 0 &&
        stampTileIndex_ < static_cast<int>(context.tilePalette.size()) &&
        tileIsAnimated(context.tilePalette[static_cast<std::size_t>(stampTileIndex_)])) {
        ImGui::Text("Animation band: %s", activeLayer_ == ActiveLayer::Floor ? "Floor" : "Overlay");
        ImGui::RadioButton("Stack 1", &animatedTileStack_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Stack 2", &animatedTileStack_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Stack 3", &animatedTileStack_, 2);
        ImGui::TextDisabled("Higher stacks draw over lower stacks in the same band.");
    }
    drawToolButton("Tile Fill", PaintTool::TileFill);
    ImGui::SameLine();
    drawToolButton("Tile Erase", PaintTool::TileErase);
    drawToolButton("Tile Rotate", PaintTool::TileRotate);

    ImGui::Spacing();
    ImGui::TextUnformatted("Brush shape");
    drawBrushShapeButton("Square", BrushShape::Square);
    ImGui::SameLine();
    drawBrushShapeButton("Circle", BrushShape::Circle);
    drawBrushShapeButton("Spray", BrushShape::Spray);
    ImGui::SameLine();
    drawBrushShapeButton("Dither", BrushShape::Dither);

    ImGui::Spacing();
    ImGui::TextUnformatted("Snap");
    int snapIndex = static_cast<int>(snapMode_);
    ImGui::RadioButton("None##snap", &snapIndex, static_cast<int>(SnapMode::None));
    ImGui::SameLine();
    ImGui::RadioButton("Tile##snap", &snapIndex, static_cast<int>(SnapMode::Full));
    ImGui::SameLine();
    ImGui::RadioButton("1/2##snap", &snapIndex, static_cast<int>(SnapMode::Half));
    ImGui::SameLine();
    ImGui::RadioButton("1/4##snap", &snapIndex, static_cast<int>(SnapMode::Quarter));
    snapMode_ = static_cast<SnapMode>(snapIndex);

    ImGui::Spacing();
    if (ImGui::Button("Clear active layer")) {
        recordUndo();
        clearActiveLayer();
    }
}

void WallFloorPaintPanel::drawPalette()
{
    ImGui::Spacing();
    ImGui::TextUnformatted("Atari 2600 NTSC colors");
    atari2600::drawNtscPaletteSelector("ScreenGraphics", activeColor_, true);
}

void WallFloorPaintPanel::addToTilePalette(EditorContext& context)
{
    if (context.tilePalette.size() >= static_cast<std::size_t>(kMaxChapterTiles)) {
        status_ = "Tile palette is full. Each chapter can store 32 tiles.";
        return;
    }

    const int ts = game::kTileSize;
    const int sx0 = (std::min(selX0_, selX1_) / ts) * ts;
    const int sy0 = (std::min(selY0_, selY1_) / ts) * ts;
    const int sx1 = ((std::max(selX0_, selX1_) / ts) + 1) * ts;
    const int sy1 = ((std::max(selY0_, selY1_) / ts) + 1) * ts;
    const int w = std::min(sx1, width_) - sx0;
    const int h = std::min(sy1, height_) - sy0;
    if (w <= 0 || h <= 0) { return; }

    TilePaletteEntry entry;
    entry.name = "tile_" + std::to_string(context.tilePalette.size() + 1);
    entry.widthPx = w;
    entry.heightPx = h;
    entry.frameDurationMs = 250;
    entry.frames.resize(1);
    TilePaletteFrame& frame = entry.frames.front();
    frame.floor.resize(static_cast<std::size_t>(w * h), 0u);
    frame.wall.resize(static_cast<std::size_t>(w * h), 0u);
    // Capture only the active layer; the other layer stays fully transparent so
    // the active layer's own transparency is preserved in the stamp.
    const PaintLayer& sourceLayer = (activeLayer_ == ActiveLayer::Wall) ? wall_ : floor_;
    std::vector<std::uint32_t>& destPixels = (activeLayer_ == ActiveLayer::Wall) ? frame.wall : frame.floor;
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const int sx = sx0 + px;
            const int sy = sy0 + py;
            if (sx < width_ && sy < height_) {
                const auto src = static_cast<std::size_t>(sy * width_ + sx);
                const auto dst = static_cast<std::size_t>(py * w + px);
                destPixels[dst] = sourceLayer.pixels[src];
            }
        }
    }
    context.tilePalette.push_back(std::move(entry));
    stampTileIndex_ = static_cast<int>(context.tilePalette.size()) - 1;
    stampFrameIndex_ = 0;
    status_ = "Added to tile palette (" + std::to_string(context.tilePalette.size()) + "/32 tiles).";
}

void WallFloorPaintPanel::addTileSelectionToPalette(EditorContext& context)
{
    if (!selectionActive_) {
        return;
    }
    const int ts = game::kTileSize;
    const int tx = (std::min(selX0_, selX1_) / ts) * ts;
    const int ty = (std::min(selY0_, selY1_) / ts) * ts;
    selX0_ = tx;
    selY0_ = ty;
    selX1_ = std::min(tx + ts - 1, width_ - 1);
    selY1_ = std::min(ty + ts - 1, height_ - 1);
    addToTilePalette(context);
    status_ += " Use 'Edit Sprite' on the tile to add animation frames.";
}

void WallFloorPaintPanel::drawTilePalette(EditorContext& context)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Tile Palette (%d/%d)", static_cast<int>(context.tilePalette.size()), kMaxChapterTiles);
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##tilesprites")) {
        refreshTileSpriteInfo(context);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Re-check each tile's sprite for added frames (after editing in the Sprite editor).");
    }
    if (context.tilePalette.empty()) {
        ImGui::TextDisabled("Select a region, then\nclick \"-> Tile Palette\".");
        return;
    }

    ImGui::BeginChild("TilePaletteScroll", ImVec2(0.0f, 300.0f), false);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float kThumbPx = 2.0f;

    for (int i = 0; i < static_cast<int>(context.tilePalette.size()); ++i) {
        auto& tile = context.tilePalette[static_cast<std::size_t>(i)];
        if (tile.frames.empty()) {
            tile.frames.push_back({});
            tile.frames.back().floor.assign(static_cast<std::size_t>(tile.widthPx * tile.heightPx), 0u);
            tile.frames.back().wall.assign(static_cast<std::size_t>(tile.widthPx * tile.heightPx), 0u);
        }
        const float thumbW = static_cast<float>(tile.widthPx) * kThumbPx;
        const float thumbH = static_cast<float>(tile.heightPx) * kThumbPx;
        const int previewFrameIndex = std::clamp(stampFrameIndex_, 0, static_cast<int>(tile.frames.size()) - 1);
        const TilePaletteFrame* previewFrame = frameAt(tile, previewFrameIndex);

        ImGui::PushID(i);

        const ImVec2 thumbMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("thumb", {thumbW, thumbH});

        // Draw floor then wall pixels
        if (previewFrame != nullptr) {
            for (int py = 0; py < tile.heightPx; ++py) {
                for (int px = 0; px < tile.widthPx; ++px) {
                    const auto idx = static_cast<std::size_t>(py * tile.widthPx + px);
                    if (idx >= previewFrame->floor.size() || idx >= previewFrame->wall.size()) {
                        continue;
                    }
                    const ImVec2 pMin{thumbMin.x + static_cast<float>(px) * kThumbPx, thumbMin.y + static_cast<float>(py) * kThumbPx};
                    const ImVec2 pMax{pMin.x + kThumbPx, pMin.y + kThumbPx};
                    const std::uint32_t fc = previewFrame->floor[idx];
                    const std::uint32_t wc = previewFrame->wall[idx];
                    if ((fc >> 24) > 0u) {
                        drawList->AddRectFilled(pMin, pMax, packedColor(fc));
                    }
                    if ((wc >> 24) > 0u) {
                        drawList->AddRectFilled(pMin, pMax, packedColor(wc));
                    }
                }
            }
        }

        const bool selected = (tool_ == PaintTool::TileStamp && stampTileIndex_ == i);
        drawList->AddRect(thumbMin, {thumbMin.x + thumbW, thumbMin.y + thumbH},
            selected ? IM_COL32(255, 216, 64, 255) : IM_COL32(180, 180, 180, 200),
            0.0f, 0, selected ? 2.0f : 1.0f);

        if (ImGui::IsItemClicked()) {
            stampTileIndex_ = i;
            stampFrameIndex_ = std::clamp(stampFrameIndex_, 0, static_cast<int>(tile.frames.size()) - 1);
            tool_ = PaintTool::TileStamp;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s  %dx%d px  %d frame(s)", tile.name.c_str(), tile.widthPx, tile.heightPx,
                static_cast<int>(tile.frames.size()));
        }

        ImGui::SameLine();
        // Editable name
        char nameBuf[32]{};
        const std::size_t nameLen = std::min(tile.name.size(), sizeof(nameBuf) - 1);
        std::memcpy(nameBuf, tile.name.data(), nameLen);
        ImGui::SetNextItemWidth(80.0f);
        if (ui::inputTextString("##name", nameBuf, sizeof(nameBuf))) {
            tile.name = nameBuf;
            context.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            context.tilePalette.erase(context.tilePalette.begin() + i);
            if (stampTileIndex_ >= static_cast<int>(context.tilePalette.size())) {
                stampTileIndex_ = static_cast<int>(context.tilePalette.size()) - 1;
            }
            stampFrameIndex_ = 0;
            if (context.tilePalette.empty()) {
                tool_ = PaintTool::Select;
            }
            context.markDirty();
            ImGui::PopID();
            break;
        }

        // Edit Sprite: author animation frames for this tile in the Sprite editor.
        if (ImGui::SmallButton("Edit Sprite")) {
            editTileSprite(context, i);
        }
        ImGui::SameLine();
        if (tileIsAnimated(tile)) {
            ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "\xE2\x97\x8F anim (%d)",
                spriteFrameCount_.count(tile.spriteId) ? spriteFrameCount_.at(tile.spriteId) : 0);
        } else if (!tile.spriteId.empty()) {
            ImGui::TextDisabled("sprite: 1 frame (static)");
        } else {
            ImGui::TextDisabled("static tile");
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void WallFloorPaintPanel::refreshTileSpriteInfo(const EditorContext& context)
{
    spriteFrameCount_.clear();
    animTileFootprints_.clear();
    const int ts = game::kTileSize;
    for (const TilePaletteEntry& tile : context.tilePalette) {
        if (tile.spriteId.empty()) {
            continue;
        }
        const std::filesystem::path metaPath = context.assets.gameSpritePath() / (tile.spriteId + ".sprite.json");
        game::SpriteMetadata meta;
        if (game::loadSpriteMetadata(metaPath, meta, nullptr)) {
            spriteFrameCount_[tile.spriteId] = static_cast<int>(meta.frames.size());
            animTileFootprints_[tile.spriteId] = {
                std::max(1, (meta.canvasSize[0] + ts - 1) / ts),
                std::max(1, (meta.canvasSize[1] + ts - 1) / ts),
            };
        }
    }
}

bool WallFloorPaintPanel::tileIsAnimated(const TilePaletteEntry& tile) const
{
    if (tile.spriteId.empty()) {
        return false;
    }
    const auto it = spriteFrameCount_.find(tile.spriteId);
    return it != spriteFrameCount_.end() && it->second >= 2;
}

void WallFloorPaintPanel::editTileSprite(EditorContext& context, int index)
{
    if (index < 0 || index >= static_cast<int>(context.tilePalette.size())) {
        return;
    }
    TilePaletteEntry& tile = context.tilePalette[static_cast<std::size_t>(index)];

    // Assign a stable, unique sprite id for the tile on first edit.
    if (tile.spriteId.empty()) {
        const std::string base = "tile_" + (currentScreenId_.empty() ? std::string("screen") : currentScreenId_);
        std::string id = base + "_" + std::to_string(index + 1);
        int suffix = index + 1;
        std::error_code ec;
        while (std::filesystem::exists(context.assets.gameSpritePath() / (id + ".sprite.json"), ec)) {
            id = base + "_" + std::to_string(++suffix);
        }
        tile.spriteId = id;
        context.markDirty();
    }

    const std::filesystem::path metaPath = context.assets.gameSpritePath() / (tile.spriteId + ".sprite.json");
    context.requestedSpriteReference = metaPath.generic_string();

    std::error_code ec;
    if (!std::filesystem::exists(metaPath, ec)) {
        // Seed a new sprite from the tile's frame-0 pixels (composite wall over floor).
        const TilePaletteFrame* frame = frameAt(tile, 0);
        PendingSpriteSeed seed;
        seed.id = tile.spriteId;
        seed.width = tile.widthPx;
        seed.height = tile.heightPx;
        seed.pixels.assign(static_cast<std::size_t>(tile.widthPx * tile.heightPx), 0u);
        if (frame != nullptr) {
            for (std::size_t i = 0; i < seed.pixels.size(); ++i) {
                const std::uint32_t fc = i < frame->floor.size() ? frame->floor[i] : 0u;
                const std::uint32_t wc = i < frame->wall.size() ? frame->wall[i] : 0u;
                seed.pixels[i] = (wc >> 24) > 0u ? wc : fc;
            }
        }
        context.pendingSpriteSeed = std::move(seed);
    }
    context.requestEditSprite = true;
    status_ = "Editing sprite '" + tile.spriteId + "'. Add frames to make it animate.";
}

void WallFloorPaintPanel::placeAnimatedTile(EditorContext& context, const TilePaletteEntry& tile, int cellX, int cellY)
{
    if (tile.spriteId.empty()) {
        return;
    }
    auto& placements = context.selectedScreenAnimatedTiles;
    // Stamping replaces only the selected animation band/stack at this cell.
    removeAnimatedTileAt(context, cellX, cellY, false);
    game::AnimatedTilePlacement placement;
    placement.spriteId = tile.spriteId;
    placement.cellX = cellX;
    placement.cellY = cellY;
    placement.layer = (activeLayer_ == ActiveLayer::Wall) ? 1 : 0;
    placement.stack = std::clamp(animatedTileStack_, 0, game::kAnimatedTileStackCount - 1);
    placements.push_back(placement);
    context.markDirty();
}

void WallFloorPaintPanel::removeAnimatedTileAt(
    EditorContext& context, int cellX, int cellY, bool allStacks)
{
    auto& placements = context.selectedScreenAnimatedTiles;
    const int selectedLayer = activeLayer_ == ActiveLayer::Wall ? 1 : 0;
    for (int i = static_cast<int>(placements.size()) - 1; i >= 0; --i) {
        const game::AnimatedTilePlacement& placement = placements[static_cast<std::size_t>(i)];
        if (placement.cellX == cellX && placement.cellY == cellY &&
            (allStacks || (placement.layer == selectedLayer && placement.stack == animatedTileStack_))) {
            placements.erase(placements.begin() + i);
            context.markDirty();
        }
    }
}

void WallFloorPaintPanel::stampTile(int x, int y, const TilePaletteEntry& tile)
{
    const int frameIndex = stampFrameIndex_;
    const TilePaletteFrame* frame = frameAt(tile, frameIndex);
    if (frame == nullptr) {
        return;
    }
    for (int py = 0; py < tile.heightPx; ++py) {
        for (int px = 0; px < tile.widthPx; ++px) {
            const auto src = static_cast<std::size_t>(py * tile.widthPx + px);
            if (src >= frame->floor.size() || src >= frame->wall.size()) {
                continue;
            }
            const std::uint32_t fc = frame->floor[src];
            const std::uint32_t wc = frame->wall[src];
            if ((fc >> 24) > 0u) { setPixel(floor_, x + px, y + py, fc); }
            if ((wc >> 24) > 0u) { setPixel(wall_, x + px, y + py, wc); }
        }
    }
    documentDirty_ = true;
}

void WallFloorPaintPanel::floodFillTile(int x, int y, const TilePaletteEntry& tile)
{
    const int ts = game::kTileSize;
    if (tile.widthPx != ts || tile.heightPx != ts) {
        status_ = "Tile Fill needs a single 16x16 tile palette entry.";
        return;
    }

    const int startTileX = std::clamp(x / ts, 0, width_ / ts - 1);
    const int startTileY = std::clamp(y / ts, 0, height_ / ts - 1);
    const int tilesW = width_ / ts;
    const int tilesH = height_ / ts;
    const int frameIndex = stampFrameIndex_;
    const TilePaletteFrame* fillFrame = frameAt(tile, frameIndex);
    if (fillFrame == nullptr || fillFrame->floor.size() < static_cast<std::size_t>(ts * ts) ||
        fillFrame->wall.size() < static_cast<std::size_t>(ts * ts)) {
        return;
    }

    PixelClipboard target;
    target.width = ts;
    target.height = ts;
    target.floor.assign(static_cast<std::size_t>(ts * ts), 0u);
    target.wall.assign(static_cast<std::size_t>(ts * ts), 0u);
    const int startX = startTileX * ts;
    const int startY = startTileY * ts;
    for (int py = 0; py < ts; ++py) {
        for (int px = 0; px < ts; ++px) {
            const std::size_t dst = static_cast<std::size_t>(py * ts + px);
            const std::size_t src = static_cast<std::size_t>((startY + py) * width_ + startX + px);
            target.floor[dst] = floor_.pixels[src];
            target.wall[dst] = wall_.pixels[src];
        }
    }

    const auto tileMatches = [&](int tileX, int tileY) {
        const int ox = tileX * ts;
        const int oy = tileY * ts;
        for (int py = 0; py < ts; ++py) {
            for (int px = 0; px < ts; ++px) {
                const std::size_t local = static_cast<std::size_t>(py * ts + px);
                const std::size_t src = static_cast<std::size_t>((oy + py) * width_ + ox + px);
                if (floor_.pixels[src] != target.floor[local] || wall_.pixels[src] != target.wall[local]) {
                    return false;
                }
            }
        }
        return true;
    };

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(tilesW * tilesH), 0u);
    std::vector<std::array<int, 2>> stack;
    stack.push_back({startTileX, startTileY});
    int filled = 0;
    while (!stack.empty()) {
        const auto [tx, ty] = stack.back();
        stack.pop_back();
        if (tx < 0 || ty < 0 || tx >= tilesW || ty >= tilesH) {
            continue;
        }
        const std::size_t visitedIndex = static_cast<std::size_t>(ty * tilesW + tx);
        if (visited[visitedIndex] != 0u) {
            continue;
        }
        visited[visitedIndex] = 1u;
        if (!tileMatches(tx, ty)) {
            continue;
        }
        stampTile(tx * ts, ty * ts, tile);
        ++filled;
        stack.push_back({tx + 1, ty});
        stack.push_back({tx - 1, ty});
        stack.push_back({tx, ty + 1});
        stack.push_back({tx, ty - 1});
    }
    status_ = "Tile-filled " + std::to_string(filled) + " tile(s).";
}

void WallFloorPaintPanel::fillTile(int x, int y, std::uint32_t color)
{
    const int ts = game::kTileSize;
    const int tx = (x / ts) * ts;
    const int ty = (y / ts) * ts;
    for (int py = 0; py < ts; ++py) {
        for (int px = 0; px < ts; ++px) {
            setPixel(activeLayer(), tx + px, ty + py, color);
        }
    }
    documentDirty_ = true;
}

void WallFloorPaintPanel::eraseTile(int x, int y)
{
    const int ts = game::kTileSize;
    for (int py = 0; py < ts; ++py) {
        for (int px = 0; px < ts; ++px) {
            setPixel(activeLayer(), x + px, y + py, 0u);
        }
    }
    documentDirty_ = true;
}

void WallFloorPaintPanel::rotateTile(int x, int y, bool clockwise)
{
    const int ts = game::kTileSize;
    const int tx = (x / ts) * ts;
    const int ty = (y / ts) * ts;
    PaintLayer& layer = activeLayer();
    std::array<std::uint32_t, game::kTileSize * game::kTileSize> source{};
    for (int py = 0; py < ts; ++py) {
        for (int px = 0; px < ts; ++px) {
            const int sx = tx + px;
            const int sy = ty + py;
            const std::size_t dst = static_cast<std::size_t>(py * ts + px);
            if (sx >= 0 && sy >= 0 && sx < width_ && sy < height_) {
                source[dst] = layer.pixels[static_cast<std::size_t>(sy * width_ + sx)];
            }
        }
    }

    for (int py = 0; py < ts; ++py) {
        for (int px = 0; px < ts; ++px) {
            const int srcX = clockwise ? py : (ts - 1 - py);
            const int srcY = clockwise ? (ts - 1 - px) : px;
            const std::size_t src = static_cast<std::size_t>(srcY * ts + srcX);
            setPixel(layer, tx + px, ty + py, source[src]);
        }
    }
    documentDirty_ = true;
}

void WallFloorPaintPanel::drawCanvas(EditorContext& context)
{
    const float pixelSize = static_cast<float>(zoom_);
    const ImVec2 canvasSize{static_cast<float>(width_) * pixelSize, static_cast<float>(height_) * pixelSize};

    ImGui::BeginChild("WallFloorCanvasScroll", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("WallFloorCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    handleCanvasInput(context, origin, pixelSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    checkerboard(drawList, origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, pixelSize);
    drawList->PushClipRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, true);
    if (floor_.visible) {
        drawLayerPixels(drawList, floor_, origin, pixelSize);
    }
    if (wall_.visible) {
        drawLayerPixels(drawList, wall_, origin, pixelSize);
    }
    if (showWallGuide_) {
        drawWallGuide(drawList, origin, pixelSize);
    }
    if (showObstacleOverlay_) {
        drawObstacleOverlay(drawList, origin, pixelSize);
    }

    if ((tool_ == PaintTool::TileDraw || tool_ == PaintTool::TileSelect || tool_ == PaintTool::TilePaste ||
            tool_ == PaintTool::TileFill || tool_ == PaintTool::TileRotate) && ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int ts = game::kTileSize;
        const int mx = (static_cast<int>((mouse.x - origin.x) / pixelSize) / ts) * ts;
        const int my = (static_cast<int>((mouse.y - origin.y) / pixelSize) / ts) * ts;
        if (mx >= 0 && my >= 0 && mx < width_ && my < height_) {
            const ImVec2 tMin{origin.x + static_cast<float>(mx) * pixelSize, origin.y + static_cast<float>(my) * pixelSize};
            const ImVec2 tMax{tMin.x + static_cast<float>(ts) * pixelSize, tMin.y + static_cast<float>(ts) * pixelSize};
            const ImU32 col = tool_ == PaintTool::TileDraw ? IM_COL32(90, 180, 255, 180) :
                (tool_ == PaintTool::TileFill ? IM_COL32(120, 100, 255, 190) :
                    (tool_ == PaintTool::TilePaste ? IM_COL32(150, 220, 120, 180) :
                        (tool_ == PaintTool::TileRotate ? IM_COL32(255, 150, 70, 220) : IM_COL32(255, 216, 64, 220))));
            drawList->AddRectFilled(tMin, tMax, IM_COL32(255, 255, 255, 20));
            drawList->AddRect(tMin, tMax, col, 0.0f, 0, 2.0f);
        }
    }

    // Paste ghost preview
    if ((tool_ == PaintTool::Select || tool_ == PaintTool::TilePaste) && (pasteMode_ || tool_ == PaintTool::TilePaste) && hasPixelClipboard_ && ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int mx = static_cast<int>((mouse.x - origin.x) / pixelSize);
        int my = static_cast<int>((mouse.y - origin.y) / pixelSize);
        if (tool_ == PaintTool::TilePaste) {
            const int ts = game::kTileSize;
            mx = (mx / ts) * ts;
            my = (my / ts) * ts;
        } else if (snapMode_ != SnapMode::None) {
            mx = snapCoord(mx);
            my = snapCoord(my);
        }
        const int referenceSpan = tool_ == PaintTool::TilePaste ? game::kTileSize : 1;
        const auto pastePosition = placementOrigin(mx, my,
            pixelClipboard_.width, pixelClipboard_.height, referenceSpan);
        mx = pastePosition[0];
        my = pastePosition[1];
        for (int cy = 0; cy < pixelClipboard_.height; ++cy) {
            for (int cx = 0; cx < pixelClipboard_.width; ++cx) {
                const int tx = mx + cx;
                const int ty = my + cy;
                if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) {
                    continue;
                }
                const auto idx = static_cast<std::size_t>(cy * pixelClipboard_.width + cx);
                const std::uint32_t c = activeLayer_ == ActiveLayer::Floor
                    ? pixelClipboard_.floor[idx]
                    : pixelClipboard_.wall[idx];
                if (alphaOf(c) == 0u) {
                    continue;
                }
                const ImVec2 pMin{origin.x + static_cast<float>(tx) * pixelSize, origin.y + static_cast<float>(ty) * pixelSize};
                const ImVec2 pMax{pMin.x + pixelSize, pMin.y + pixelSize};
                drawList->AddRectFilled(pMin, pMax,
                    IM_COL32((c >> 0) & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 120));
                drawList->AddRect(pMin, pMax, IM_COL32(200, 200, 255, 100));
            }
        }
        const ImVec2 ghostMin{origin.x + static_cast<float>(mx) * pixelSize,
            origin.y + static_cast<float>(my) * pixelSize};
        const ImVec2 ghostMax{ghostMin.x + static_cast<float>(pixelClipboard_.width) * pixelSize,
            ghostMin.y + static_cast<float>(pixelClipboard_.height) * pixelSize};
        ImVec2 marker = ghostMin;
        if (pasteReference_ == PasteReference::TopRight || pasteReference_ == PasteReference::BottomRight) {
            marker.x = ghostMax.x;
        }
        if (pasteReference_ == PasteReference::BottomLeft || pasteReference_ == PasteReference::BottomRight) {
            marker.y = ghostMax.y;
        }
        drawList->AddCircleFilled(marker, 5.0f, IM_COL32(255, 216, 64, 255));
        drawList->AddCircle(marker, 8.0f, IM_COL32(30, 30, 30, 255), 0, 2.0f);
        drawList->AddText({marker.x + 10.0f, marker.y + 5.0f},
            IM_COL32(255, 216, 64, 255), "Ctrl+V: corner");
    }

    // Tile stamp ghost preview
    if ((tool_ == PaintTool::TileStamp || tool_ == PaintTool::TileFill) && stampTileIndex_ >= 0 &&
        stampTileIndex_ < static_cast<int>(context.tilePalette.size()) &&
        ImGui::IsItemHovered()) {
        const auto& tile = context.tilePalette[static_cast<std::size_t>(stampTileIndex_)];
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int ts = game::kTileSize;
        const int referenceX = (static_cast<int>((mouse.x - origin.x) / pixelSize) / ts) * ts;
        const int referenceY = (static_cast<int>((mouse.y - origin.y) / pixelSize) / ts) * ts;
        const auto footprintIt = animTileFootprints_.find(tile.spriteId);
        const bool animated = tileIsAnimated(tile);
        const int placementWidth = animated && footprintIt != animTileFootprints_.end()
            ? footprintIt->second[0] * ts : tile.widthPx;
        const int placementHeight = animated && footprintIt != animTileFootprints_.end()
            ? footprintIt->second[1] * ts : tile.heightPx;
        const auto stampPosition = placementOrigin(referenceX, referenceY,
            placementWidth, placementHeight, ts);
        const int mx = stampPosition[0];
        const int my = stampPosition[1];
        const int frameIndex = stampFrameIndex_;
        const TilePaletteFrame* frame = frameAt(tile, frameIndex);
        if (frame != nullptr) {
            for (int cy = 0; cy < tile.heightPx; ++cy) {
                for (int cx = 0; cx < tile.widthPx; ++cx) {
                    const int tx = mx + cx;
                    const int ty = my + cy;
                    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) { continue; }
                    const auto idx = static_cast<std::size_t>(cy * tile.widthPx + cx);
                    if (idx >= frame->floor.size() || idx >= frame->wall.size()) { continue; }
                    const std::uint32_t fc = frame->floor[idx];
                    const std::uint32_t wc = frame->wall[idx];
                    const ImVec2 pMin{origin.x + static_cast<float>(tx) * pixelSize, origin.y + static_cast<float>(ty) * pixelSize};
                    const ImVec2 pMax{pMin.x + pixelSize, pMin.y + pixelSize};
                    if ((fc >> 24) > 0u) {
                        drawList->AddRectFilled(pMin, pMax, IM_COL32((fc>>0)&0xff, (fc>>8)&0xff, (fc>>16)&0xff, 140));
                    }
                    if ((wc >> 24) > 0u) {
                        drawList->AddRectFilled(pMin, pMax, IM_COL32((wc>>0)&0xff, (wc>>8)&0xff, (wc>>16)&0xff, 140));
                    }
                }
            }
            if (tool_ == PaintTool::TileStamp) {
                const ImVec2 ghostMin{origin.x + static_cast<float>(mx) * pixelSize,
                    origin.y + static_cast<float>(my) * pixelSize};
                const ImVec2 ghostMax{ghostMin.x + static_cast<float>(tile.widthPx) * pixelSize,
                    ghostMin.y + static_cast<float>(tile.heightPx) * pixelSize};
                ImVec2 marker = ghostMin;
                if (pasteReference_ == PasteReference::TopRight || pasteReference_ == PasteReference::BottomRight) {
                    marker.x = ghostMax.x;
                }
                if (pasteReference_ == PasteReference::BottomLeft || pasteReference_ == PasteReference::BottomRight) {
                    marker.y = ghostMax.y;
                }
                drawList->AddRect(ghostMin, ghostMax, IM_COL32(255, 216, 64, 220), 0.0f, 0, 2.0f);
                drawList->AddCircleFilled(marker, 5.0f, IM_COL32(255, 216, 64, 255));
                drawList->AddCircle(marker, 8.0f, IM_COL32(30, 30, 30, 255), 0, 2.0f);
                drawList->AddText({marker.x + 10.0f, marker.y + 5.0f},
                    IM_COL32(255, 216, 64, 255), "Ctrl+V: corner");
            }
        }
    }

    // Tile erase ghost preview
    if (tool_ == PaintTool::TileErase && ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int ts = game::kTileSize;
        const int mx = (static_cast<int>((mouse.x - origin.x) / pixelSize) / ts) * ts;
        const int my = (static_cast<int>((mouse.y - origin.y) / pixelSize) / ts) * ts;
        if (mx >= 0 && my >= 0 && mx < width_ && my < height_) {
            const ImVec2 tMin{origin.x + static_cast<float>(mx) * pixelSize, origin.y + static_cast<float>(my) * pixelSize};
            const ImVec2 tMax{tMin.x + static_cast<float>(ts) * pixelSize, tMin.y + static_cast<float>(ts) * pixelSize};
            drawList->AddRectFilled(tMin, tMax, IM_COL32(255, 60, 60, 60));
            drawList->AddRect(tMin, tMax, IM_COL32(255, 60, 60, 220), 0.0f, 0, 2.0f);
        }
    }

    // Placed animated tiles: markers over their footprints (the selected stamp
    // tile's placements are highlighted). Plus a footprint ghost when the selected
    // stamp tile is animated.
    {
        const int ts = game::kTileSize;
        auto footprintOf = [&](const std::string& spriteId) -> std::array<int, 2> {
            auto it = animTileFootprints_.find(spriteId);
            return it != animTileFootprints_.end() ? it->second : std::array<int, 2>{1, 1};
        };
        const TilePaletteEntry* stampTile = (stampTileIndex_ >= 0 && stampTileIndex_ < static_cast<int>(context.tilePalette.size()))
            ? &context.tilePalette[static_cast<std::size_t>(stampTileIndex_)] : nullptr;
        const std::string selectedSpriteId = stampTile != nullptr ? stampTile->spriteId : std::string{};
        if (showAnimatedTileOverlay_) {
            for (const game::AnimatedTilePlacement& tile : context.selectedScreenAnimatedTiles) {
                const std::array<int, 2> fp = footprintOf(tile.spriteId);
                const ImVec2 mMin{origin.x + static_cast<float>(tile.cellX * ts) * pixelSize,
                    origin.y + static_cast<float>(tile.cellY * ts) * pixelSize};
                const ImVec2 mMax{mMin.x + static_cast<float>(fp[0] * ts) * pixelSize,
                    mMin.y + static_cast<float>(fp[1] * ts) * pixelSize};
                const bool selectedSlot = tile.layer == (activeLayer_ == ActiveLayer::Wall ? 1 : 0) &&
                    tile.stack == animatedTileStack_;
                const bool highlighted = selectedSlot && !selectedSpriteId.empty() && tile.spriteId == selectedSpriteId;
                const ImU32 fill = highlighted ? IM_COL32(255, 216, 64, 70)
                    : (tile.layer == 1 ? IM_COL32(120, 200, 255, 40) : IM_COL32(120, 255, 160, 40));
                const ImU32 border = highlighted ? IM_COL32(255, 216, 64, 255)
                    : (tile.layer == 1 ? IM_COL32(120, 200, 255, 220) : IM_COL32(120, 255, 160, 220));
                drawList->AddRectFilled(mMin, mMax, fill);
                drawList->AddRect(mMin, mMax, border, 0.0f, 0, highlighted ? 2.5f : 1.5f);
                const std::string marker = "A" + std::to_string(tile.stack + 1);
                drawList->AddText({mMin.x + 2.0f, mMin.y + 2.0f}, border, marker.c_str());
            }
        }
        if (tool_ == PaintTool::TileStamp && stampTile != nullptr && tileIsAnimated(*stampTile) &&
            ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int cx = static_cast<int>((mouse.x - origin.x) / pixelSize) / ts;
            const int cy = static_cast<int>((mouse.y - origin.y) / pixelSize) / ts;
            const std::array<int, 2> fp = footprintOf(selectedSpriteId);
            const auto ghostPosition = placementOrigin(cx * ts, cy * ts, fp[0] * ts, fp[1] * ts, ts);
            const ImVec2 gMin{origin.x + static_cast<float>(ghostPosition[0]) * pixelSize,
                origin.y + static_cast<float>(ghostPosition[1]) * pixelSize};
            const ImVec2 gMax{gMin.x + static_cast<float>(fp[0] * ts) * pixelSize,
                gMin.y + static_cast<float>(fp[1] * ts) * pixelSize};
            drawList->AddRectFilled(gMin, gMax, IM_COL32(255, 255, 255, 30));
            drawList->AddRect(gMin, gMax, IM_COL32(120, 255, 160, 230), 0.0f, 0, 2.0f);
        }
    }

    // Selection overlay
    if ((tool_ == PaintTool::Select || tool_ == PaintTool::TileSelect) && (selectionActive_ || selectionDragging_)) {
        const int sx0 = std::min(selX0_, selX1_);
        const int sy0 = std::min(selY0_, selY1_);
        const int sx1 = std::max(selX0_, selX1_);
        const int sy1 = std::max(selY0_, selY1_);
        const ImVec2 sMin{origin.x + static_cast<float>(sx0) * pixelSize, origin.y + static_cast<float>(sy0) * pixelSize};
        const ImVec2 sMax{origin.x + static_cast<float>(sx1 + 1) * pixelSize, origin.y + static_cast<float>(sy1 + 1) * pixelSize};
        drawList->AddRectFilled(sMin, sMax, IM_COL32(255, 216, 64, 28));
        drawList->AddRect(sMin, sMax, IM_COL32(255, 216, 64, 220), 0.0f, 0, 1.5f);
    }

    drawList->PopClipRect();

    if (showGrid_ && pixelSize >= 4.0f) {
        for (int x = 0; x <= width_; ++x) {
            const float px = origin.x + static_cast<float>(x) * pixelSize;
            drawList->AddLine({px, origin.y}, {px, origin.y + canvasSize.y}, IM_COL32(0, 0, 0, 60));
        }
        for (int y = 0; y <= height_; ++y) {
            const float py = origin.y + static_cast<float>(y) * pixelSize;
            drawList->AddLine({origin.x, py}, {origin.x + canvasSize.x, py}, IM_COL32(0, 0, 0, 60));
        }
    }

    // Tile boundary grid
    if (pixelsPerTile_ > 1) {
        const float tileScreen = static_cast<float>(pixelsPerTile_) * pixelSize;
        const int tileCountX = width_ / pixelsPerTile_;
        const int tileCountY = height_ / pixelsPerTile_;
        for (int x = 0; x <= tileCountX; ++x) {
            const float px = origin.x + static_cast<float>(x) * tileScreen;
            drawList->AddLine({px, origin.y}, {px, origin.y + canvasSize.y}, IM_COL32(255, 255, 255, 55));
        }
        for (int y = 0; y <= tileCountY; ++y) {
            const float py = origin.y + static_cast<float>(y) * tileScreen;
            drawList->AddLine({origin.x, py}, {origin.x + canvasSize.x, py}, IM_COL32(255, 255, 255, 55));
        }

        // Half/quarter snap grid markers
        if (snapMode_ == SnapMode::Half || snapMode_ == SnapMode::Quarter) {
            const float halfScreen = tileScreen * 0.5f;
            const int halfCountX = width_ * 2 / pixelsPerTile_;
            const int halfCountY = height_ * 2 / pixelsPerTile_;
            for (int x = 0; x <= halfCountX; ++x) {
                if (x % 2 == 0) { continue; }
                const float px = origin.x + static_cast<float>(x) * halfScreen;
                drawList->AddLine({px, origin.y}, {px, origin.y + canvasSize.y}, IM_COL32(255, 255, 255, 28));
            }
            for (int y = 0; y <= halfCountY; ++y) {
                if (y % 2 == 0) { continue; }
                const float py = origin.y + static_cast<float>(y) * halfScreen;
                drawList->AddLine({origin.x, py}, {origin.x + canvasSize.x, py}, IM_COL32(255, 255, 255, 28));
            }
        }
        if (snapMode_ == SnapMode::Quarter) {
            const float quarterScreen = tileScreen * 0.25f;
            const int qCountX = width_ * 4 / pixelsPerTile_;
            const int qCountY = height_ * 4 / pixelsPerTile_;
            for (int x = 0; x <= qCountX; ++x) {
                if (x % 4 == 0 || x % 2 == 0) { continue; }
                const float px = origin.x + static_cast<float>(x) * quarterScreen;
                drawList->AddLine({px, origin.y}, {px, origin.y + canvasSize.y}, IM_COL32(255, 255, 255, 14));
            }
            for (int y = 0; y <= qCountY; ++y) {
                if (y % 4 == 0 || y % 2 == 0) { continue; }
                const float py = origin.y + static_cast<float>(y) * quarterScreen;
                drawList->AddLine({origin.x, py}, {origin.x + canvasSize.x, py}, IM_COL32(255, 255, 255, 14));
            }
        }
    }

    drawList->AddRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, IM_COL32(220, 220, 220, 255));
    ImGui::EndChild();
}

void WallFloorPaintPanel::drawWallGuide(ImDrawList* drawList, ImVec2 origin, float pixelSize) const
{
    if (wallGuide_.empty() || wallGuideWidth_ <= 0 || wallGuideHeight_ <= 0) {
        return;
    }

    const int drawWidth = std::min(width_ / pixelsPerTile_, wallGuideWidth_);
    const int drawHeight = std::min(height_ / pixelsPerTile_, wallGuideHeight_);
    const float tileScreen = static_cast<float>(pixelsPerTile_) * pixelSize;
    for (int y = 0; y < drawHeight; ++y) {
        for (int x = 0; x < drawWidth; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(wallGuideWidth_) + static_cast<std::size_t>(x);
            if (wallGuide_[index] == 0u) {
                continue;
            }

            const ImVec2 min{origin.x + static_cast<float>(x) * tileScreen, origin.y + static_cast<float>(y) * tileScreen};
            const ImVec2 max{min.x + tileScreen, min.y + tileScreen};
            drawList->AddRectFilled(min, max, IM_COL32(255, 216, 64, 38));
            drawList->AddRect(min, max, IM_COL32(255, 216, 64, 230), 0.0f, 0, 2.0f);
        }
    }
}

void WallFloorPaintPanel::drawObstacleOverlay(ImDrawList* drawList, ImVec2 origin, float pixelSize) const
{
    const float tileScreen = static_cast<float>(pixelsPerTile_) * pixelSize;
    const float now = static_cast<float>(ImGui::GetTime());
    for (const game::MapObstacle& obstacle : obstacleOverlay_) {
        const bool active = obstacle.type != game::ObstacleType::TimedSpike ||
            std::fmod(now + obstacle.phaseSeconds,
                std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds)) < obstacle.activeSeconds;

        ImU32 fill = IM_COL32(230, 60, 70, 82);
        ImU32 border = IM_COL32(255, 105, 115, 235);
        const char* typeLabel = "S";
        if (obstacle.type == game::ObstacleType::Pit) {
            fill = IM_COL32(20, 20, 28, 150);
            border = IM_COL32(150, 155, 175, 235);
            typeLabel = "P";
        } else if (obstacle.type == game::ObstacleType::TimedSpike) {
            fill = active ? IM_COL32(245, 160, 45, 92) : IM_COL32(80, 150, 210, 64);
            border = active ? IM_COL32(255, 190, 70, 240) : IM_COL32(115, 185, 240, 220);
            typeLabel = "T";
        }

        const ImVec2 min{
            origin.x + static_cast<float>(obstacle.x) * tileScreen,
            origin.y + static_cast<float>(obstacle.y) * tileScreen,
        };
        const ImVec2 max{
            min.x + static_cast<float>(std::max(1, obstacle.width)) * tileScreen,
            min.y + static_cast<float>(std::max(1, obstacle.height)) * tileScreen,
        };
        drawList->AddRectFilled(min, max, fill);
        drawList->AddRect(min, max, border, 0.0f, 0, 2.0f);
        drawList->AddText({min.x + 3.0f, min.y + 2.0f}, border, typeLabel);
        if (!obstacle.id.empty() && tileScreen >= 20.0f) {
            drawList->AddText({min.x + 3.0f, min.y + 16.0f}, border, obstacle.id.c_str());
        }
    }
}

void WallFloorPaintPanel::drawParallaxPreview()
{
    if (animatePreview_) {
        previewAnimationX_ = std::fmod(previewAnimationX_ + ImGui::GetIO().DeltaTime * 36.0f, static_cast<float>(width_));
    }
    const float effectiveScrollX = previewScrollX_ + previewAnimationX_;

    ImGui::TextUnformatted("Parallax preview");
    ui::checkbox("Animate floor", "##AnimateFloorPreview", &animatePreview_);
    ui::sliderFloat("Floor factor", "##FloorParallaxFactor", &floorParallax_, 0.0f, 1.0f, "%.3f", 150.0f);
    ui::sliderFloat("Scroll X", "##PreviewScrollX", &previewScrollX_, -4.0f, 4.0f, "%.3f", 220.0f);
    ImGui::SameLine();
    ui::sliderFloat("Scroll Y", "##PreviewScrollY", &previewScrollY_, -4.0f, 4.0f, "%.3f", 220.0f);

    const float previewScale = std::max(1.0f, std::min(8.0f, 520.0f / static_cast<float>(std::max(width_, height_))));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{static_cast<float>(width_) * previewScale, static_cast<float>(height_) * previewScale};
    ImGui::InvisibleButton("WallFloorParallaxPreview", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(14, 18, 24, 255));
    drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    if (floor_.visible) {
        drawLayerPixels(drawList, floor_, origin, previewScale, {effectiveScrollX * floorParallax_, previewScrollY_ * floorParallax_}, true);
    }
    if (wall_.visible) {
        drawLayerPixels(drawList, wall_, origin, previewScale, {previewScrollX_, previewScrollY_}, false);
    }
    drawList->PopClipRect();
    drawList->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(220, 220, 220, 255));
}

void WallFloorPaintPanel::drawToolButton(const char* label, PaintTool tool, const char* shortcut)
{
    const bool selected = tool_ == tool;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
    }
    if (ImGui::Button(label, {132.0f, 0.0f})) {
        if (tool_ != tool) {
            pasteMode_ = false;
            adjustmentStrokeBaseline_.clear();
            strokeCaptured_ = false;
        }
        tool_ = tool;
    }
    if (shortcut != nullptr && shortcut[0] != '\0' && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s (%s)", label, shortcut);
    }
    if (selected) {
        ImGui::PopStyleColor();
    }
}

void WallFloorPaintPanel::drawBrushShapeButton(const char* label, BrushShape shape)
{
    const bool selected = brushShape_ == shape;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
    }
    if (ImGui::Button(label, {76.0f, 0.0f})) {
        brushShape_ = shape;
    }
    if (selected) {
        ImGui::PopStyleColor();
    }
}

void WallFloorPaintPanel::drawLayerPixels(ImDrawList* drawList, const PaintLayer& layer, ImVec2 origin, float pixelSize, ImVec2 offset, bool wrap) const
{
    const std::size_t expected = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    if (width_ <= 0 || height_ <= 0 || pixelSize <= 0.0f || layer.pixels.size() < expected) {
        return;
    }

    const int repeatX = wrap ? 2 : 1;
    const int repeatY = wrap ? 2 : 1;
    const float layerWidth = static_cast<float>(width_) * pixelSize;
    const float layerHeight = static_cast<float>(height_) * pixelSize;
    const ImVec2 clipMin = drawList->GetClipRectMin();
    const ImVec2 clipMax = drawList->GetClipRectMax();
    float baseX = wrap ? -std::fmod(offset.x * pixelSize, layerWidth) : -offset.x * pixelSize;
    float baseY = wrap ? -std::fmod(offset.y * pixelSize, layerHeight) : -offset.y * pixelSize;
    if (wrap && baseX > 0.0f) {
        baseX -= layerWidth;
    }
    if (wrap && baseY > 0.0f) {
        baseY -= layerHeight;
    }

    for (int ry = 0; ry < repeatY; ++ry) {
        for (int rx = 0; rx < repeatX; ++rx) {
            const ImVec2 layerOrigin{origin.x + baseX + static_cast<float>(rx) * layerWidth, origin.y + baseY + static_cast<float>(ry) * layerHeight};
            const float layerMinX = layerOrigin.x;
            const float layerMinY = layerOrigin.y;
            const float layerMaxX = layerOrigin.x + layerWidth;
            const float layerMaxY = layerOrigin.y + layerHeight;
            if (layerMaxX <= clipMin.x || layerMaxY <= clipMin.y || layerMinX >= clipMax.x || layerMinY >= clipMax.y) {
                continue;
            }

            const int xBegin = std::clamp(static_cast<int>(std::floor((clipMin.x - layerOrigin.x) / pixelSize)), 0, width_);
            const int xEnd = std::clamp(static_cast<int>(std::ceil((clipMax.x - layerOrigin.x) / pixelSize)), 0, width_);
            const int yBegin = std::clamp(static_cast<int>(std::floor((clipMin.y - layerOrigin.y) / pixelSize)), 0, height_);
            const int yEnd = std::clamp(static_cast<int>(std::ceil((clipMax.y - layerOrigin.y) / pixelSize)), 0, height_);
            if (xBegin >= xEnd || yBegin >= yEnd) {
                continue;
            }

            for (int y = yBegin; y < yEnd; ++y) {
                int x = xBegin;
                while (x < xEnd) {
                    const std::uint32_t color = layer.pixels[static_cast<std::size_t>(y * width_ + x)];
                    if (alphaOf(color) == 0u) {
                        ++x;
                        continue;
                    }
                    int runEnd = x + 1;
                    while (runEnd < xEnd &&
                        layer.pixels[static_cast<std::size_t>(y * width_ + runEnd)] == color) {
                        ++runEnd;
                    }
                    const ImVec2 min{layerOrigin.x + static_cast<float>(x) * pixelSize,
                        layerOrigin.y + static_cast<float>(y) * pixelSize};
                    const ImVec2 max{layerOrigin.x + static_cast<float>(runEnd) * pixelSize,
                        min.y + pixelSize};
                    drawCompositePixel(drawList, min, max, color, layer.opacity);
                    x = runEnd;
                }
            }
        }
    }
}

void WallFloorPaintPanel::drawCompositePixel(ImDrawList* drawList, ImVec2 min, ImVec2 max, std::uint32_t color, float layerOpacity) const
{
    const auto alpha = static_cast<int>(std::round(static_cast<float>(alphaOf(color)) * std::clamp(layerOpacity, 0.0f, 1.0f)));
    drawList->AddRectFilled(min, max, IM_COL32((color >> 0) & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, alpha));
}

void WallFloorPaintPanel::handleCanvasInput(EditorContext& context, const ImVec2& origin, float pixelSize)
{
    int x = 0;
    int y = 0;
    if (!canvasPixelAt(origin, pixelSize, x, y)) {
        lastPaint_ = {-1, -1};
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            strokeCaptured_ = false;
            adjustmentStrokeBaseline_.clear();
        }
        return;
    }

    if (tool_ == PaintTool::PickColor) {
        ImGui::SetTooltip("Pick color [%d,%d]", x, y);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && x >= 0 && y >= 0 && x < width_ && y < height_) {
            std::uint32_t color = 0u;
            std::string layerName;
            if (sampleVisibleGraphicsColor(x, y, color, layerName)) {
                activeColor_ = color;
                status_ = "Picked color from existing " + layerName + " graphics.";
            } else {
                status_ = "No visible graphics color at that pixel; active color unchanged.";
            }
        }
        return;
    }

    if (snapMode_ != SnapMode::None) {
        x = snapCoord(x);
        y = snapCoord(y);
    }

    // Tile draw fills exactly one tile cell on the active layer.
    if (tool_ == PaintTool::TileDraw) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Fill tile [%d,%d]", tx / ts, ty / ts);
        const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (clicked && !strokeCaptured_) {
            recordUndo();
            strokeCaptured_ = true;
            lastPaint_ = {-1, -1};
        }
        if ((leftDown || rightDown) && strokeCaptured_ && (lastPaint_[0] != tx || lastPaint_[1] != ty)) {
            fillTile(tx, ty, rightDown ? 0u : activeColor_);
            lastPaint_ = {tx, ty};
            status_ = std::string(rightDown ? "Cleared" : "Filled") + " tile [" +
                std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
        }
        if (!leftDown && !rightDown) {
            strokeCaptured_ = false;
            lastPaint_ = {-1, -1};
        }
        return;
    }

    // Tile select always selects one exact 16x16 cell.
    if (tool_ == PaintTool::TileSelect) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Select tile [%d,%d]", tx / ts, ty / ts);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selX0_ = tx;
            selY0_ = ty;
            selX1_ = std::min(tx + ts - 1, width_ - 1);
            selY1_ = std::min(ty + ts - 1, height_ - 1);
            selectionDragging_ = false;
            selectionActive_ = true;

            // If an animated tile is placed on this cell, auto-select the matching
            // palette tile (and switch to the Stamp tool) so it can be re-stamped.
            const int ccx = tx / ts;
            const int ccy = ty / ts;
            const game::AnimatedTilePlacement* selectedPlacement = nullptr;
            for (const game::AnimatedTilePlacement& p : context.selectedScreenAnimatedTiles) {
                const auto fpIt = animTileFootprints_.find(p.spriteId);
                const int fpW = fpIt != animTileFootprints_.end() ? fpIt->second[0] : 1;
                const int fpH = fpIt != animTileFootprints_.end() ? fpIt->second[1] : 1;
                if (ccx >= p.cellX && ccx < p.cellX + fpW && ccy >= p.cellY && ccy < p.cellY + fpH) {
                    const int priority = p.layer * 3 + p.stack;
                    const int selectedPriority = selectedPlacement != nullptr
                        ? selectedPlacement->layer * 3 + selectedPlacement->stack : -1;
                    if (priority >= selectedPriority) {
                        selectedPlacement = &p;
                    }
                }
            }
            if (selectedPlacement != nullptr) {
                for (int ti = 0; ti < static_cast<int>(context.tilePalette.size()); ++ti) {
                    if (context.tilePalette[static_cast<std::size_t>(ti)].spriteId == selectedPlacement->spriteId) {
                        stampTileIndex_ = ti;
                        activeLayer_ = selectedPlacement->layer == 1 ? ActiveLayer::Wall : ActiveLayer::Floor;
                        animatedTileStack_ = std::clamp(
                            selectedPlacement->stack, 0, game::kAnimatedTileStackCount - 1);
                        tool_ = PaintTool::TileStamp;
                        status_ = "Selected animated tile '" + selectedPlacement->spriteId + "' stack " +
                            std::to_string(animatedTileStack_ + 1) + " for stamping.";
                        break;
                    }
                }
            }
        }
        return;
    }

    if (tool_ == PaintTool::TilePaste) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        const auto pastePosition = placementOrigin(tx, ty,
            pixelClipboard_.width, pixelClipboard_.height, ts);
        ImGui::SetTooltip("Paste tile from %s reference", pasteReferenceName());
        if (hasPixelClipboard_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            recordUndo();
            pastePixelClipboard(pastePosition[0], pastePosition[1]);
            status_ = std::string("Pasted tile using ") + pasteReferenceName() + " reference.";
        }
        return;
    }

    if (tool_ == PaintTool::TileFill) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Flood fill tiles from [%d,%d]", tx / ts, ty / ts);
        if (stampTileIndex_ >= 0 && stampTileIndex_ < static_cast<int>(context.tilePalette.size()) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            recordUndo();
            floodFillTile(tx, ty, context.tilePalette[static_cast<std::size_t>(stampTileIndex_)]);
        }
        return;
    }

    if (tool_ == PaintTool::TileRotate) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Rotate tile [%d,%d]. Left: clockwise, right: counter-clockwise.", tx / ts, ty / ts);
        const bool clickedLeft = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool clickedRight = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (clickedLeft || clickedRight) {
            recordUndo();
            rotateTile(tx, ty, clickedLeft);
            status_ = std::string("Rotated tile ") + (clickedLeft ? "clockwise" : "counter-clockwise") +
                " [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
        }
        return;
    }

    // Select tool handling
    if (tool_ == PaintTool::Select) {
        if (pasteMode_ && hasPixelClipboard_) {
            const auto pastePosition = placementOrigin(x, y,
                pixelClipboard_.width, pixelClipboard_.height);
            ImGui::SetTooltip("Paste %dx%d from %s reference", pixelClipboard_.width,
                pixelClipboard_.height, pasteReferenceName());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                recordUndo();
                pastePixelClipboard(pastePosition[0], pastePosition[1]);
                status_ = "Pasted at [" + std::to_string(pastePosition[0]) + "," +
                    std::to_string(pastePosition[1]) + "] using " + pasteReferenceName() + " reference.";
            }
        } else {
            ImGui::SetTooltip("[%d,%d]", x, y);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selX0_ = x; selY0_ = y;
                selX1_ = x; selY1_ = y;
                selectionDragging_ = true;
                selectionActive_ = false;
            }
            if (selectionDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selX1_ = x; selY1_ = y;
            }
            if (selectionDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                selectionDragging_ = false;
                selectionActive_ = true;
            }
        }
        return;
    }

    // Tile erase tool handling (also removes any animated tile placed on the cell)
    if (tool_ == PaintTool::TileErase) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Erase tile [%d,%d]", tx / ts, ty / ts);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !strokeCaptured_) {
            recordUndo();
            strokeCaptured_ = true;
            lastPaint_ = {tx, ty};
            eraseTile(tx, ty);
            removeAnimatedTileAt(context, tx / ts, ty / ts, true);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && strokeCaptured_) {
            if (lastPaint_[0] != tx || lastPaint_[1] != ty) {
                lastPaint_ = {tx, ty};
                eraseTile(tx, ty);
                removeAnimatedTileAt(context, tx / ts, ty / ts, true);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            strokeCaptured_ = false;
            lastPaint_ = {-1, -1};
        }
        return;
    }

    // Tile stamp tool handling. An animated tile (its sprite has >= 2 frames) stamps
    // a runtime animated-tile placement; a static tile bakes pixels as before.
    if (tool_ == PaintTool::TileStamp) {
        if (stampTileIndex_ >= 0 && stampTileIndex_ < static_cast<int>(context.tilePalette.size())) {
            const TilePaletteEntry& tile = context.tilePalette[static_cast<std::size_t>(stampTileIndex_)];
            const bool animated = tileIsAnimated(tile);
            const int ts = game::kTileSize;
            const int referenceX = (x / ts) * ts;
            const int referenceY = (y / ts) * ts;
            const auto footprintIt = animTileFootprints_.find(tile.spriteId);
            const int placementWidth = animated && footprintIt != animTileFootprints_.end()
                ? footprintIt->second[0] * ts : tile.widthPx;
            const int placementHeight = animated && footprintIt != animTileFootprints_.end()
                ? footprintIt->second[1] * ts : tile.heightPx;
            const auto stampPosition = placementOrigin(referenceX, referenceY,
                placementWidth, placementHeight, ts);
            const int tx = stampPosition[0];
            const int ty = stampPosition[1];
            ImGui::SetTooltip(animated ? "Stamp animated tile from %s reference" :
                "Stamp tile from %s reference", pasteReferenceName());

            // Right-click removes an animated placement under the cursor.
            if (animated && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                removeAnimatedTileAt(context, tx / ts, ty / ts, false);
                status_ = "Removed animated tile stack " + std::to_string(animatedTileStack_ + 1) +
                    " at [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
            }

            const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !strokeCaptured_) {
                if (!animated) {
                    recordUndo();  // pixel bake is undoable; placements are not part of the pixel undo
                }
                strokeCaptured_ = true;
                lastPaint_ = {-1, -1};
            }
            if (leftDown && strokeCaptured_ && (lastPaint_[0] != tx || lastPaint_[1] != ty)) {
                if (animated) {
                    placeAnimatedTile(context, tile, tx / ts, ty / ts);
                    status_ = "Stamped animated tile on stack " + std::to_string(animatedTileStack_ + 1) +
                        " at [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
                } else {
                    stampTile(tx, ty, tile);
                    status_ = "Stamped at [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
                }
                lastPaint_ = {tx, ty};
            }
            if (!leftDown) {
                strokeCaptured_ = false;
                lastPaint_ = {-1, -1};
            }
        } else {
            strokeCaptured_ = false;
            lastPaint_ = {-1, -1};
        }
        return;
    }

    const bool adjustmentTool = tool_ == PaintTool::Darken || tool_ == PaintTool::Lighten;
    const bool smudgeTool = tool_ == PaintTool::Smudge;
    if (adjustmentTool) {
        ImGui::SetTooltip("%s %s %d%%, graduation %d%% [%d,%d]", activeLayer().name.c_str(),
            tool_ == PaintTool::Lighten ? "lighten" : "darken",
            adjustmentIntensity_, adjustmentGraduation_, x, y);
    } else if (smudgeTool) {
        ImGui::SetTooltip("%s smudge %.0f%% [%d,%d] (Atari colors)",
            activeLayer().name.c_str(), static_cast<float>(adjustmentIntensity_), x, y);
    } else {
        ImGui::SetTooltip("%s [%d,%d]", activeLayer().name.c_str(), x, y);
    }
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool clicked = adjustmentTool || smudgeTool
        ? ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        : ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (clicked && !strokeCaptured_) {
        recordUndo();
        if (adjustmentTool) {
            adjustmentStrokeBaseline_ = activeLayer().pixels;
        }
        strokeCaptured_ = true;
        dragStart_ = {x, y};
        lastPaint_ = {x, y};
        if (tool_ == PaintTool::Fill) {
            floodFill(activeLayer(), x, y, ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? 0u : activeColor_);
        }
    }

    const std::uint32_t paintColor = rightDown || tool_ == PaintTool::Eraser ? 0u : activeColor_;
    if ((leftDown || rightDown) && (tool_ == PaintTool::Pencil || tool_ == PaintTool::Eraser)) {
        paintStroke(lastPaint_[0] < 0 ? x : lastPaint_[0], lastPaint_[1] < 0 ? y : lastPaint_[1], x, y, paintColor);
        lastPaint_ = {x, y};
    }
    if (leftDown && adjustmentTool && strokeCaptured_) {
        paintAdjustmentStroke(lastPaint_[0] < 0 ? x : lastPaint_[0],
            lastPaint_[1] < 0 ? y : lastPaint_[1], x, y, tool_ == PaintTool::Lighten);
        lastPaint_ = {x, y};
    }
    if (leftDown && smudgeTool && strokeCaptured_ &&
        (lastPaint_[0] != x || lastPaint_[1] != y)) {
        paintSmudgeStroke(lastPaint_[0], lastPaint_[1], x, y);
        lastPaint_ = {x, y};
    }

    if (!leftDown && !rightDown && strokeCaptured_) {
        if (tool_ == PaintTool::Line) {
            drawLine(activeLayer(), dragStart_[0], dragStart_[1], x, y, activeColor_);
        } else if (tool_ == PaintTool::Rect) {
            drawRect(activeLayer(), dragStart_[0], dragStart_[1], x, y, activeColor_);
        }
        strokeCaptured_ = false;
        adjustmentStrokeBaseline_.clear();
        lastPaint_ = {-1, -1};
    }
}

bool WallFloorPaintPanel::canvasPixelAt(const ImVec2& origin, float pixelSize, int& x, int& y) const
{
    if (!ImGui::IsItemHovered()) {
        return false;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    x = static_cast<int>((mouse.x - origin.x) / pixelSize);
    y = static_cast<int>((mouse.y - origin.y) / pixelSize);
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

bool WallFloorPaintPanel::sampleVisibleGraphicsColor(int x, int y, std::uint32_t& color, std::string& layerName) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(y * width_ + x);
    if (wall_.visible && index < wall_.pixels.size() && alphaOf(wall_.pixels[index]) > 0u) {
        color = wall_.pixels[index];
        layerName = wall_.name;
        return true;
    }
    if (floor_.visible && index < floor_.pixels.size() && alphaOf(floor_.pixels[index]) > 0u) {
        color = floor_.pixels[index];
        layerName = floor_.name;
        return true;
    }
    return false;
}

int WallFloorPaintPanel::applySnap(int coord, int gridSize) const
{
    if (gridSize <= 1) {
        return coord;
    }
    return (coord / gridSize) * gridSize;
}

int WallFloorPaintPanel::snapCoord(int coord) const
{
    switch (snapMode_) {
        case SnapMode::Full:    return applySnap(coord, pixelsPerTile_);
        case SnapMode::Half:    return applySnap(coord, std::max(1, pixelsPerTile_ / 2));
        case SnapMode::Quarter: return applySnap(coord, std::max(1, pixelsPerTile_ / 4));
        default:                return coord;
    }
}

PaintLayer& WallFloorPaintPanel::activeLayer()
{
    return activeLayer_ == ActiveLayer::Floor ? floor_ : wall_;
}

const PaintLayer& WallFloorPaintPanel::activeLayer() const
{
    return activeLayer_ == ActiveLayer::Floor ? floor_ : wall_;
}

void WallFloorPaintPanel::recordUndo()
{
    documentDirty_ = true;
    undoStack_.push_back({floor_.pixels, wall_.pixels});
    if (undoStack_.size() > static_cast<std::size_t>(kMaxUndoSteps)) {
        undoStack_.erase(undoStack_.begin());
    }
}

void WallFloorPaintPanel::undo()
{
    if (undoStack_.empty()) {
        status_ = "Nothing to undo.";
        return;
    }
    auto& state = undoStack_.back();
    floor_.pixels = std::move(state.floor);
    wall_.pixels = std::move(state.wall);
    undoStack_.pop_back();
    status_ = "Undone. (" + std::to_string(undoStack_.size()) + " steps left)";
}

void WallFloorPaintPanel::setPixel(PaintLayer& layer, int x, int y, std::uint32_t color)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    layer.pixels[static_cast<std::size_t>(y * width_ + x)] = color;
}

void WallFloorPaintPanel::setBrushPixel(PaintLayer& layer, int x, int y, std::uint32_t color)
{
    const int radius = std::max(0, brushSize_ - 1);
    switch (brushShape_) {
        case BrushShape::Square:
            for (int py = y - radius; py <= y + radius; ++py) {
                for (int px = x - radius; px <= x + radius; ++px) {
                    setPixel(layer, px, py, color);
                }
            }
            break;
        case BrushShape::Circle:
            for (int py = y - radius; py <= y + radius; ++py) {
                for (int px = x - radius; px <= x + radius; ++px) {
                    if ((px - x) * (px - x) + (py - y) * (py - y) <= radius * radius + radius) {
                        setPixel(layer, px, py, color);
                    }
                }
            }
            break;
        case BrushShape::Spray: {
            const int diameter = 2 * radius + 1;
            const int count = std::max(1, diameter * diameter);
            for (int i = 0; i < count; ++i) {
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const int rx = radius > 0 ? static_cast<int>(spraySeed_ % static_cast<std::uint32_t>(diameter)) - radius : 0;
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const int ry = radius > 0 ? static_cast<int>(spraySeed_ % static_cast<std::uint32_t>(diameter)) - radius : 0;
                const float coverage = radialBrushCoverage(rx, ry, radius);
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const float sample = static_cast<float>(spraySeed_ & 0xffffu) / 65535.0f;
                if (sample < coverage * 0.45f) {
                    setPixel(layer, x + rx, y + ry, color);
                }
            }
            break;
        }
        case BrushShape::Dither:
            for (int py = y - radius; py <= y + radius; ++py) {
                for (int px = x - radius; px <= x + radius; ++px) {
                    const float coverage = radialBrushCoverage(px - x, py - y, radius);
                    const float edgeThreshold =
                        (static_cast<float>(kBayer4x4[py & 3][px & 3]) + 0.5f) / 16.0f;
                    if (((px + py) & 1) == 0 && coverage > edgeThreshold) {
                        setPixel(layer, px, py, color);
                    }
                }
            }
            break;
    }
}

void WallFloorPaintPanel::setAdjustmentBrushPixel(PaintLayer& layer, int x, int y, bool lighten)
{
    const std::size_t expected = static_cast<std::size_t>(width_ * height_);
    if (adjustmentStrokeBaseline_.size() != expected) {
        return;
    }

    const int radius = std::max(0, brushSize_ - 1);
    const float outerRadius = static_cast<float>(radius) + 0.5f;
    const float graduation =
        static_cast<float>(std::clamp(adjustmentGraduation_, 0, 100)) / 100.0f;
    const float featherWidth = radius > 0 ? outerRadius * graduation : 0.0f;
    const float innerRadius = std::max(0.0f, outerRadius - featherWidth);
    const int extent = std::max(0, static_cast<int>(std::ceil(outerRadius)));
    const float intensity = static_cast<float>(std::clamp(adjustmentIntensity_, 0, 100)) / 100.0f;

    for (int py = y - extent; py <= y + extent; ++py) {
        for (int px = x - extent; px <= x + extent; ++px) {
            if (px < 0 || py < 0 || px >= width_ || py >= height_) {
                continue;
            }

            const float dx = static_cast<float>(px - x);
            const float dy = static_cast<float>(py - y);
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > outerRadius) {
                continue;
            }
            if (featherWidth > 0.0f && distance > innerRadius) {
                const float coverage = (outerRadius - distance) / featherWidth;
                const float threshold = (static_cast<float>(kBayer4x4[py & 3][px & 3]) + 0.5f) / 16.0f;
                if (coverage <= threshold) {
                    continue;
                }
            }

            const std::size_t index = static_cast<std::size_t>(py * width_ + px);
            const std::uint32_t source = adjustmentStrokeBaseline_[index];
            if (alphaOf(source) == 0u) {
                continue;
            }
            const auto adjustChannel = [lighten, intensity](std::uint32_t channel) {
                const float target = lighten ? 255.0f : 0.0f;
                return static_cast<std::uint32_t>(std::round(
                    static_cast<float>(channel) + (target - static_cast<float>(channel)) * intensity));
            };
            const std::uint32_t red = adjustChannel((source >> 0u) & 0xffu);
            const std::uint32_t green = adjustChannel((source >> 8u) & 0xffu);
            const std::uint32_t blue = adjustChannel((source >> 16u) & 0xffu);
            layer.pixels[index] = (source & 0xff000000u) | (blue << 16u) | (green << 8u) | red;
        }
    }
}

void WallFloorPaintPanel::setSmudgeBrushPixel(
    PaintLayer& layer, int x, int y, int sourceX, int sourceY)
{
    const int radius = std::max(0, brushSize_ - 1);
    const float strength = static_cast<float>(std::clamp(adjustmentIntensity_, 1, 100)) / 100.0f;
    struct SmudgePixel {
        std::size_t destinationIndex = 0;
        std::uint32_t source = 0;
        std::uint32_t destination = 0;
    };
    std::vector<SmudgePixel> pending;
    pending.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));

    for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
        for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
            if (brushShape_ == BrushShape::Circle &&
                offsetX * offsetX + offsetY * offsetY > radius * radius + radius) {
                continue;
            }
            const float coverage = radialBrushCoverage(offsetX, offsetY, radius);
            const int dstX = x + offsetX;
            const int dstY = y + offsetY;
            if (brushShape_ == BrushShape::Spray) {
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const float sample = static_cast<float>(spraySeed_ & 0xffffu) / 65535.0f;
                if (sample >= coverage * 0.45f) {
                    continue;
                }
            }
            if (brushShape_ == BrushShape::Dither) {
                const float edgeThreshold =
                    (static_cast<float>(kBayer4x4[dstY & 3][dstX & 3]) + 0.5f) / 16.0f;
                if (((dstX + dstY) & 1) != 0 || coverage <= edgeThreshold) {
                    continue;
                }
            }

            const int srcX = sourceX + offsetX;
            const int srcY = sourceY + offsetY;
            if (dstX < 0 || dstY < 0 || dstX >= width_ || dstY >= height_ ||
                srcX < 0 || srcY < 0 || srcX >= width_ || srcY >= height_) {
                continue;
            }

            const std::uint32_t source =
                layer.pixels[static_cast<std::size_t>(srcY * width_ + srcX)];
            if (alphaOf(source) == 0u) {
                continue;
            }
            const std::size_t destinationIndex = static_cast<std::size_t>(dstY * width_ + dstX);
            pending.push_back({destinationIndex, source, layer.pixels[destinationIndex]});
        }
    }

    for (const SmudgePixel& pixel : pending) {
            const std::uint32_t source = pixel.source;
            const std::uint32_t destination = pixel.destination;
            const float destinationWeight = alphaOf(destination) == 0u ? 0.0f : 1.0f - strength;
            const float sourceWeight = alphaOf(destination) == 0u ? 1.0f : strength;
            const float totalWeight = destinationWeight + sourceWeight;
            const auto blendChannel = [&](unsigned int shift) {
                const float sourceChannel = static_cast<float>((source >> shift) & 0xffu);
                const float destinationChannel = static_cast<float>((destination >> shift) & 0xffu);
                return static_cast<std::uint32_t>(std::round(
                    (sourceChannel * sourceWeight + destinationChannel * destinationWeight) / totalWeight));
            };
            layer.pixels[pixel.destinationIndex] = nearestAtariColor(
                blendChannel(0u), blendChannel(8u), blendChannel(16u));
    }
}

void WallFloorPaintPanel::paintStroke(int x0, int y0, int x1, int y1, std::uint32_t color)
{
    drawLine(activeLayer(), x0, y0, x1, y1, color);
}

void WallFloorPaintPanel::paintAdjustmentStroke(int x0, int y0, int x1, int y1, bool lighten)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        setAdjustmentBrushPixel(activeLayer(), x0, y0, lighten);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void WallFloorPaintPanel::paintSmudgeStroke(int x0, int y0, int x1, int y1)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int previousX = x0;
    int previousY = y0;

    while (x0 != x1 || y0 != y1) {
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
        setSmudgeBrushPixel(activeLayer(), x0, y0, previousX, previousY);
        previousX = x0;
        previousY = y0;
    }
}

std::uint32_t WallFloorPaintPanel::nearestAtariColor(
    std::uint32_t red, std::uint32_t green, std::uint32_t blue) const
{
    std::uint32_t nearest = atari2600::kNtscPalette.front();
    std::uint32_t nearestDistance = std::numeric_limits<std::uint32_t>::max();
    for (const std::uint32_t candidate : atari2600::kNtscPalette) {
        const int deltaRed = static_cast<int>(red) - static_cast<int>((candidate >> 0u) & 0xffu);
        const int deltaGreen = static_cast<int>(green) - static_cast<int>((candidate >> 8u) & 0xffu);
        const int deltaBlue = static_cast<int>(blue) - static_cast<int>((candidate >> 16u) & 0xffu);
        const std::uint32_t distance = static_cast<std::uint32_t>(
            deltaRed * deltaRed + deltaGreen * deltaGreen + deltaBlue * deltaBlue);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = candidate;
        }
    }
    return nearest;
}

void WallFloorPaintPanel::drawLine(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        setBrushPixel(layer, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void WallFloorPaintPanel::drawRect(PaintLayer& layer, int x0, int y0, int x1, int y1, std::uint32_t color)
{
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int top = std::min(y0, y1);
    const int bottom = std::max(y0, y1);
    for (int x = left; x <= right; ++x) {
        setBrushPixel(layer, x, top, color);
        setBrushPixel(layer, x, bottom, color);
    }
    for (int y = top; y <= bottom; ++y) {
        setBrushPixel(layer, left, y, color);
        setBrushPixel(layer, right, y, color);
    }
}

void WallFloorPaintPanel::floodFill(PaintLayer& layer, int startX, int startY, std::uint32_t color)
{
    if (startX < 0 || startY < 0 || startX >= width_ || startY >= height_) {
        return;
    }
    const std::uint32_t target = layer.pixels[static_cast<std::size_t>(startY * width_ + startX)];
    if (target == color) {
        return;
    }

    std::vector<std::array<int, 2>> stack{{startX, startY}};
    while (!stack.empty()) {
        const auto point = stack.back();
        stack.pop_back();
        const int x = point[0];
        const int y = point[1];
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            continue;
        }
        const auto index = static_cast<std::size_t>(y * width_ + x);
        if (layer.pixels[index] != target) {
            continue;
        }
        layer.pixels[index] = color;
        stack.push_back({x + 1, y});
        stack.push_back({x - 1, y});
        stack.push_back({x, y + 1});
        stack.push_back({x, y - 1});
    }
}

void WallFloorPaintPanel::clearActiveLayer()
{
    std::fill(activeLayer().pixels.begin(), activeLayer().pixels.end(), activeLayer_ == ActiveLayer::Floor ? 0xff243447u : 0u);
}

void WallFloorPaintPanel::copyPixelSelection()
{
    if (!selectionActive_) {
        return;
    }
    const int x0 = std::min(selX0_, selX1_);
    const int y0 = std::min(selY0_, selY1_);
    const int x1 = std::max(selX0_, selX1_);
    const int y1 = std::max(selY0_, selY1_);
    pixelClipboard_.width = x1 - x0 + 1;
    pixelClipboard_.height = y1 - y0 + 1;
    pixelClipboard_.floor.resize(static_cast<std::size_t>(pixelClipboard_.width * pixelClipboard_.height));
    pixelClipboard_.wall.resize(static_cast<std::size_t>(pixelClipboard_.width * pixelClipboard_.height));
    for (int cy = 0; cy < pixelClipboard_.height; ++cy) {
        for (int cx = 0; cx < pixelClipboard_.width; ++cx) {
            const std::size_t dst = static_cast<std::size_t>(cy * pixelClipboard_.width + cx);
            const std::size_t src = static_cast<std::size_t>((y0 + cy) * width_ + (x0 + cx));
            pixelClipboard_.floor[dst] = floor_.pixels[src];
            pixelClipboard_.wall[dst] = wall_.pixels[src];
        }
    }
    hasPixelClipboard_ = true;
    status_ = "Copied " + std::to_string(pixelClipboard_.width) + "x" + std::to_string(pixelClipboard_.height) + " pixels.";
}

void WallFloorPaintPanel::copyTileSelection()
{
    if (!selectionActive_) {
        return;
    }
    const int ts = game::kTileSize;
    const int x0 = (std::min(selX0_, selX1_) / ts) * ts;
    const int y0 = (std::min(selY0_, selY1_) / ts) * ts;
    pixelClipboard_.width = ts;
    pixelClipboard_.height = ts;
    pixelClipboard_.floor.assign(static_cast<std::size_t>(ts * ts), 0u);
    pixelClipboard_.wall.assign(static_cast<std::size_t>(ts * ts), 0u);
    for (int cy = 0; cy < ts; ++cy) {
        for (int cx = 0; cx < ts; ++cx) {
            const int sx = x0 + cx;
            const int sy = y0 + cy;
            const std::size_t dst = static_cast<std::size_t>(cy * ts + cx);
            if (sx >= 0 && sy >= 0 && sx < width_ && sy < height_) {
                const std::size_t src = static_cast<std::size_t>(sy * width_ + sx);
                pixelClipboard_.floor[dst] = floor_.pixels[src];
                pixelClipboard_.wall[dst] = wall_.pixels[src];
            }
        }
    }
    hasPixelClipboard_ = true;
    status_ = "Copied tile [" + std::to_string(x0 / ts) + "," + std::to_string(y0 / ts) + "].";
}

void WallFloorPaintPanel::pastePixelClipboard(int x, int y)
{
    if (!hasPixelClipboard_) {
        return;
    }
    const std::vector<std::uint32_t>& src = activeLayer_ == ActiveLayer::Floor
        ? pixelClipboard_.floor
        : pixelClipboard_.wall;
    for (int cy = 0; cy < pixelClipboard_.height; ++cy) {
        for (int cx = 0; cx < pixelClipboard_.width; ++cx) {
            const std::size_t idx = static_cast<std::size_t>(cy * pixelClipboard_.width + cx);
            const std::uint32_t color = src[idx];
            if ((color >> 24) != 0) {
                setPixel(activeLayer(), x + cx, y + cy, color);
            }
        }
    }
}

void WallFloorPaintPanel::cyclePasteReference()
{
    const int next = (static_cast<int>(pasteReference_) + 1) % 4;
    pasteReference_ = static_cast<PasteReference>(next);
    status_ = std::string("Paste reference: ") + pasteReferenceName() + ".";
}

std::array<int, 2> WallFloorPaintPanel::placementOrigin(
    int referenceX, int referenceY, int contentWidth, int contentHeight, int referenceSpan) const
{
    const int rightOffset = std::max(0, contentWidth - referenceSpan);
    const int bottomOffset = std::max(0, contentHeight - referenceSpan);
    switch (pasteReference_) {
        case PasteReference::TopLeft:
            return {referenceX, referenceY};
        case PasteReference::TopRight:
            return {referenceX - rightOffset, referenceY};
        case PasteReference::BottomRight:
            return {referenceX - rightOffset, referenceY - bottomOffset};
        case PasteReference::BottomLeft:
            return {referenceX, referenceY - bottomOffset};
    }
    return {referenceX, referenceY};
}

const char* WallFloorPaintPanel::pasteReferenceName() const
{
    switch (pasteReference_) {
        case PasteReference::TopLeft:
            return "top-left";
        case PasteReference::TopRight:
            return "top-right";
        case PasteReference::BottomRight:
            return "bottom-right";
        case PasteReference::BottomLeft:
            return "bottom-left";
    }
    return "top-left";
}

std::vector<unsigned char> WallFloorPaintPanel::layerRgba(const PaintLayer& layer) const
{
    std::vector<unsigned char> rgba;
    rgba.reserve(static_cast<std::size_t>(width_ * height_ * 4));
    for (std::uint32_t color : layer.pixels) {
        rgba.push_back(static_cast<unsigned char>((color >> 0) & 0xffu));
        rgba.push_back(static_cast<unsigned char>((color >> 8) & 0xffu));
        rgba.push_back(static_cast<unsigned char>((color >> 16) & 0xffu));
        rgba.push_back(static_cast<unsigned char>((color >> 24) & 0xffu));
    }
    return rgba;
}

std::vector<unsigned char> WallFloorPaintPanel::compositeRgba(bool parallaxPreview) const
{
    std::vector<unsigned char> rgba;
    rgba.reserve(static_cast<std::size_t>(width_ * height_ * 4));
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            std::uint32_t color = 0u;
            if (floor_.visible) {
                int fx = x;
                int fy = y;
                if (parallaxPreview) {
                    const float effectiveScrollX = previewScrollX_ + (animatePreview_ ? previewAnimationX_ : 0.0f);
                    fx = (x + static_cast<int>(effectiveScrollX * floorParallax_)) % width_;
                    fy = (y + static_cast<int>(previewScrollY_ * floorParallax_)) % height_;
                    if (fx < 0) { fx += width_; }
                    if (fy < 0) { fy += height_; }
                }
                color = blendOver(color, floor_.pixels[static_cast<std::size_t>(fy * width_ + fx)], floor_.opacity);
            }
            if (wall_.visible) {
                color = blendOver(color, wall_.pixels[static_cast<std::size_t>(y * width_ + x)], wall_.opacity);
            }
            rgba.push_back(static_cast<unsigned char>((color >> 0) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((color >> 8) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((color >> 16) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((color >> 24) & 0xffu));
        }
    }
    return rgba;
}

bool WallFloorPaintPanel::exportScreenPngs(const EditorContext& context, const std::string& id, const ScreenGraphicsBuffer& buf)
{
    if (id.empty()) {
        return false;
    }

    const std::filesystem::path rawOutputDir = context.assets.rawTilesetPath();
    const std::filesystem::path gameOutputDir = context.assets.gameTilesetPath();
    std::error_code error;
    std::filesystem::create_directories(rawOutputDir, error);
    if (error) { return false; }
    std::filesystem::create_directories(gameOutputDir, error);
    if (error) { return false; }

    // Convert pixel buffers to RGBA bytes
    const auto pixelToRgba = [](const std::vector<std::uint32_t>& pixels) {
        std::vector<unsigned char> rgba;
        rgba.reserve(pixels.size() * 4);
        for (std::uint32_t c : pixels) {
            rgba.push_back(static_cast<unsigned char>((c >> 0) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 8) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 16) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 24) & 0xffu));
        }
        return rgba;
    };

    // Composite preview (floor over transparent background, then wall on top — no parallax shift)
    const auto compositePreview = [&]() {
        std::vector<unsigned char> rgba;
        const std::size_t npx = buf.floor.size();
        rgba.reserve(npx * 4);
        for (std::size_t i = 0; i < npx; ++i) {
            std::uint32_t c = blendOver(0u, buf.floor[i], 1.0f);
            c = blendOver(c, buf.wall[i], 1.0f);
            rgba.push_back(static_cast<unsigned char>((c >> 0) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 8) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 16) & 0xffu));
            rgba.push_back(static_cast<unsigned char>((c >> 24) & 0xffu));
        }
        return rgba;
    };

    const std::vector<unsigned char> floorRgba   = pixelToRgba(buf.floor);
    const std::vector<unsigned char> wallRgba    = pixelToRgba(buf.wall);
    const std::vector<unsigned char> previewRgba = compositePreview();

    if (!writePngRgba(rawOutputDir  / (id + "_floor.png"),   width_, height_, floorRgba)   ||
        !writePngRgba(rawOutputDir  / (id + "_wall.png"),    width_, height_, wallRgba)    ||
        !writePngRgba(rawOutputDir  / (id + "_preview.png"), width_, height_, previewRgba) ||
        !writePngRgba(gameOutputDir / (id + "_floor.png"),   width_, height_, floorRgba)   ||
        !writePngRgba(gameOutputDir / (id + "_wall.png"),    width_, height_, wallRgba)    ||
        !writePngRgba(gameOutputDir / (id + "_preview.png"), width_, height_, previewRgba)) {
        return false;
    }
    return true;
}

bool WallFloorPaintPanel::exportPngs(const EditorContext& context)
{
    const std::string id(assetId_.data());
    if (id.empty()) {
        status_ = "Asset id is empty.";
        return false;
    }

    ScreenGraphicsBuffer buf;
    buf.floor = floor_.pixels;
    buf.wall  = wall_.pixels;
    buf.dirty = true;

    if (!exportScreenPngs(context, id, buf)) {
        status_ = "Failed to export one or more PNGs for " + id + ".";
        return false;
    }

    status_ = "Exported floor, wall, and preview PNGs for " + id + ".";
    documentDirty_ = false;
    return true;
}

} // namespace adventure::editor
