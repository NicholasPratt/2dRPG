#include "editor/panels/wall_floor_paint_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace adventure::editor {
namespace {

constexpr int kMinCanvasSize = 4;
constexpr int kMaxCanvasSize = 1024;
constexpr int kMaxChapterTiles = 32;
constexpr int kMaxTileFrames = 16;
constexpr unsigned char kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

ImU32 packedColor(std::uint32_t color)
{
    return IM_COL32((color >> 0) & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, (color >> 24) & 0xff);
}

std::uint8_t alphaOf(std::uint32_t color)
{
    return static_cast<std::uint8_t>((color >> 24) & 0xff);
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
    for (float y = min.y; y < max.y; y += cellSize) {
        for (float x = min.x; x < max.x; x += cellSize) {
            const bool even = (static_cast<int>((x - min.x) / cellSize) + static_cast<int>((y - min.y) / cellSize)) % 2 == 0;
            drawList->AddRectFilled({x, y}, {std::min(x + cellSize, max.x), std::min(y + cellSize, max.y)},
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
    if (!ImGui::GetIO().WantTextInput) {
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
            if (hasPixelClipboard_) {
                pasteMode_ = true;
                tool_ = pixelClipboard_.width == game::kTileSize && pixelClipboard_.height == game::kTileSize
                    ? PaintTool::TilePaste
                    : PaintTool::Select;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pasteMode_ = false;
            if (tool_ == PaintTool::Select || tool_ == PaintTool::TileSelect) {
                selectionActive_ = false;
                selectionDragging_ = false;
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
    selectionActive_ = false;
    selectionDragging_ = false;
    pasteMode_ = false;

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
    ImGui::InputText("Asset id", assetId_.data(), assetId_.size());
    ImGui::SameLine();
    ImGui::Text("%dx%d px", width_, height_);

    ui::sliderInt("Zoom", "##ScreenGraphicsZoom", &zoom_, 1, 16, 80.0f);
    ImGui::SameLine(220.0f);
    ui::sliderInt("Brush", "##ScreenGraphicsBrush", &brushSize_, 1, 12, 80.0f);
    ui::checkbox("Grid", "##ScreenGraphicsGrid", &showGrid_);
    if (!wallGuide_.empty()) {
        ImGui::SameLine(220.0f);
        ui::checkbox("Wall guide", "##ScreenGraphicsWallGuide", &showWallGuide_);
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
    ImGui::TextDisabled("Tile Draw fills one 16x16 tile. Tile Select copies/pastes exact tiles. Ctrl+Z undo, Ctrl+C/V copy/paste.");
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
    drawToolButton("Pencil", PaintTool::Pencil);
    ImGui::SameLine();
    drawToolButton("Eraser", PaintTool::Eraser);
    drawToolButton("Fill", PaintTool::Fill);
    ImGui::SameLine();
    drawToolButton("Line", PaintTool::Line);
    drawToolButton("Rect", PaintTool::Rect);
    ImGui::SameLine();
    drawToolButton("Select", PaintTool::Select);
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
    drawToolButton("Tile Fill", PaintTool::TileFill);
    ImGui::SameLine();
    drawToolButton("Tile Erase", PaintTool::TileErase);

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
    ImGui::TextUnformatted("Colors");
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float swatch = 26.0f;
    constexpr int cols = 4;
    for (int i = 0; i < static_cast<int>(palette_.size()); ++i) {
        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("swatch", {swatch, swatch});
        const ImVec2 max{min.x + swatch, min.y + swatch};
        drawList->AddRectFilled(min, max, packedColor(palette_[static_cast<std::size_t>(i)]));
        drawList->AddRect(min, max, palette_[static_cast<std::size_t>(i)] == activeColor_ ? IM_COL32(255, 216, 64, 255) : IM_COL32(0, 0, 0, 180), 0.0f, 0, 2.0f);
        if (ImGui::IsItemClicked()) {
            activeColor_ = palette_[static_cast<std::size_t>(i)];
        }
        if ((i + 1) % cols != 0) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    float color[4] = {
        static_cast<float>((activeColor_ >> 0) & 0xffu) / 255.0f,
        static_cast<float>((activeColor_ >> 8) & 0xffu) / 255.0f,
        static_cast<float>((activeColor_ >> 16) & 0xffu) / 255.0f,
        static_cast<float>((activeColor_ >> 24) & 0xffu) / 255.0f,
    };
    if (ImGui::ColorEdit4("Active", color, ImGuiColorEditFlags_NoInputs)) {
        activeColor_ = (static_cast<std::uint32_t>(std::round(color[3] * 255.0f)) << 24u) |
            (static_cast<std::uint32_t>(std::round(color[2] * 255.0f)) << 16u) |
            (static_cast<std::uint32_t>(std::round(color[1] * 255.0f)) << 8u) |
            static_cast<std::uint32_t>(std::round(color[0] * 255.0f));
    }
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
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const int sx = sx0 + px;
            const int sy = sy0 + py;
            if (sx < width_ && sy < height_) {
                const auto src = static_cast<std::size_t>(sy * width_ + sx);
                const auto dst = static_cast<std::size_t>(py * w + px);
                frame.floor[dst] = floor_.pixels[src];
                frame.wall[dst] = wall_.pixels[src];
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
    status_ += " Animate exported tile sprites in the Sprite editor.";
}

void WallFloorPaintPanel::drawTilePalette(EditorContext& context)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Tile Palette (%d/%d)", static_cast<int>(context.tilePalette.size()), kMaxChapterTiles);
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
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
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
        ImGui::Text("Frames: %d", static_cast<int>(tile.frames.size()));
        if (selected) {
            stampFrameIndex_ = std::clamp(stampFrameIndex_, 0, static_cast<int>(tile.frames.size()) - 1);
            int frameNumber = stampFrameIndex_ + 1;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("Stamp frame", &frameNumber)) {
                stampFrameIndex_ = std::clamp(frameNumber - 1, 0, static_cast<int>(tile.frames.size()) - 1);
            }
            ImGui::TextDisabled("Animate tile assets in the Sprite editor.");
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
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

    if ((tool_ == PaintTool::TileDraw || tool_ == PaintTool::TileSelect || tool_ == PaintTool::TilePaste || tool_ == PaintTool::TileFill) && ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int ts = game::kTileSize;
        const int mx = (static_cast<int>((mouse.x - origin.x) / pixelSize) / ts) * ts;
        const int my = (static_cast<int>((mouse.y - origin.y) / pixelSize) / ts) * ts;
        if (mx >= 0 && my >= 0 && mx < width_ && my < height_) {
            const ImVec2 tMin{origin.x + static_cast<float>(mx) * pixelSize, origin.y + static_cast<float>(my) * pixelSize};
            const ImVec2 tMax{tMin.x + static_cast<float>(ts) * pixelSize, tMin.y + static_cast<float>(ts) * pixelSize};
            const ImU32 col = tool_ == PaintTool::TileDraw ? IM_COL32(90, 180, 255, 180) :
                (tool_ == PaintTool::TileFill ? IM_COL32(120, 100, 255, 190) :
                    (tool_ == PaintTool::TilePaste ? IM_COL32(150, 220, 120, 180) : IM_COL32(255, 216, 64, 220)));
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
    }

    // Tile stamp ghost preview
    if ((tool_ == PaintTool::TileStamp || tool_ == PaintTool::TileFill) && stampTileIndex_ >= 0 &&
        stampTileIndex_ < static_cast<int>(context.tilePalette.size()) &&
        ImGui::IsItemHovered()) {
        const auto& tile = context.tilePalette[static_cast<std::size_t>(stampTileIndex_)];
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int ts = game::kTileSize;
        const int mx = (static_cast<int>((mouse.x - origin.x) / pixelSize) / ts) * ts;
        const int my = (static_cast<int>((mouse.y - origin.y) / pixelSize) / ts) * ts;
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

void WallFloorPaintPanel::drawToolButton(const char* label, PaintTool tool)
{
    const bool selected = tool_ == tool;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
    }
    if (ImGui::Button(label, {132.0f, 0.0f})) {
        if (tool_ != tool) {
            pasteMode_ = false;
        }
        tool_ = tool;
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
    const int repeatX = wrap ? 2 : 1;
    const int repeatY = wrap ? 2 : 1;
    const float layerWidth = static_cast<float>(width_) * pixelSize;
    const float layerHeight = static_cast<float>(height_) * pixelSize;
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
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::uint32_t color = layer.pixels[static_cast<std::size_t>(y * width_ + x)];
                    if (alphaOf(color) == 0u) {
                        continue;
                    }
                    const ImVec2 min{layerOrigin.x + static_cast<float>(x) * pixelSize, layerOrigin.y + static_cast<float>(y) * pixelSize};
                    const ImVec2 max{min.x + pixelSize, min.y + pixelSize};
                    drawCompositePixel(drawList, min, max, color, layer.opacity);
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
        }
        return;
    }

    if (tool_ == PaintTool::TilePaste) {
        const int ts = game::kTileSize;
        const int tx = (x / ts) * ts;
        const int ty = (y / ts) * ts;
        ImGui::SetTooltip("Paste tile [%d,%d]", tx / ts, ty / ts);
        if (hasPixelClipboard_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            recordUndo();
            pastePixelClipboard(tx, ty);
            status_ = "Pasted tile at [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
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

    // Select tool handling
    if (tool_ == PaintTool::Select) {
        if (pasteMode_ && hasPixelClipboard_) {
            ImGui::SetTooltip("Paste %dx%d  [%d,%d]", pixelClipboard_.width, pixelClipboard_.height, x, y);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                recordUndo();
                pastePixelClipboard(x, y);
                status_ = "Pasted at [" + std::to_string(x) + "," + std::to_string(y) + "].";
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

    // Tile erase tool handling
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
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && strokeCaptured_) {
            if (lastPaint_[0] != tx || lastPaint_[1] != ty) {
                lastPaint_ = {tx, ty};
                eraseTile(tx, ty);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            strokeCaptured_ = false;
            lastPaint_ = {-1, -1};
        }
        return;
    }

    // Tile stamp tool handling
    if (tool_ == PaintTool::TileStamp) {
        if (stampTileIndex_ >= 0 && stampTileIndex_ < static_cast<int>(context.tilePalette.size())) {
            const int ts = game::kTileSize;
            const int tx = (x / ts) * ts;
            const int ty = (y / ts) * ts;
            ImGui::SetTooltip("Stamp tile [%d,%d]", tx / ts, ty / ts);
            const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !strokeCaptured_) {
                recordUndo();
                strokeCaptured_ = true;
                lastPaint_ = {-1, -1};
            }
            if (leftDown && strokeCaptured_ && (lastPaint_[0] != tx || lastPaint_[1] != ty)) {
                stampTile(tx, ty, context.tilePalette[static_cast<std::size_t>(stampTileIndex_)]);
                lastPaint_ = {tx, ty};
                status_ = "Stamped at [" + std::to_string(tx / ts) + "," + std::to_string(ty / ts) + "].";
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

    ImGui::SetTooltip("%s [%d,%d]", activeLayer().name.c_str(), x, y);
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (clicked && !strokeCaptured_) {
        recordUndo();
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

    if (!leftDown && !rightDown && strokeCaptured_) {
        if (tool_ == PaintTool::Line) {
            drawLine(activeLayer(), dragStart_[0], dragStart_[1], x, y, activeColor_);
        } else if (tool_ == PaintTool::Rect) {
            drawRect(activeLayer(), dragStart_[0], dragStart_[1], x, y, activeColor_);
        }
        strokeCaptured_ = false;
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
            const int count = std::max(1, diameter * diameter / 3);
            for (int i = 0; i < count; ++i) {
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const int rx = radius > 0 ? static_cast<int>(spraySeed_ % static_cast<std::uint32_t>(diameter)) - radius : 0;
                spraySeed_ = spraySeed_ * 1664525u + 1013904223u;
                const int ry = radius > 0 ? static_cast<int>(spraySeed_ % static_cast<std::uint32_t>(diameter)) - radius : 0;
                setPixel(layer, x + rx, y + ry, color);
            }
            break;
        }
        case BrushShape::Dither:
            for (int py = y - radius; py <= y + radius; ++py) {
                for (int px = x - radius; px <= x + radius; ++px) {
                    if (((px + py) & 1) == 0) {
                        setPixel(layer, px, py, color);
                    }
                }
            }
            break;
    }
}

void WallFloorPaintPanel::paintStroke(int x0, int y0, int x1, int y1, std::uint32_t color)
{
    drawLine(activeLayer(), x0, y0, x1, y1, color);
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
