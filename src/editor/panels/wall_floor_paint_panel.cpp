#include "editor/panels/wall_floor_paint_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace adventure::editor {
namespace {

constexpr int kMinCanvasSize = 4;
constexpr int kMaxCanvasSize = 256;
constexpr unsigned char kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

ImU32 packedColor(std::uint32_t color)
{
    return IM_COL32((color >> 0) & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, (color >> 24) & 0xff);
}

std::uint8_t alphaOf(std::uint32_t color)
{
    return static_cast<std::uint8_t>((color >> 24) & 0xff);
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

} // namespace

void WallFloorPaintPanel::draw(EditorContext& context)
{
    ensureDocument();
    drawToolbar(context);
    ImGui::Separator();

    ImGui::Columns(2, "WallFloorPaintColumns", true);
    ImGui::SetColumnWidth(0, 260.0f);
    drawLayerControls();
    drawPalette();
    ImGui::NextColumn();
    drawCanvas();
    ImGui::Spacing();
    drawParallaxPreview();
    ImGui::Columns(1);
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
        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                resized[static_cast<std::size_t>(y * width + x)] = layer.pixels[static_cast<std::size_t>(y * width_ + x)];
            }
        }
        layer.pixels = std::move(resized);
    };

    resizeLayer(floor_);
    resizeLayer(wall_);
    width_ = width;
    height_ = height;
    resizeWidth_ = width_;
    resizeHeight_ = height_;
}

void WallFloorPaintPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Asset id", assetId_.data(), assetId_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("W", &resizeWidth_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("H", &resizeHeight_);
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        recordUndo();
        resizeDocument(resizeWidth_, resizeHeight_);
    }

    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderInt("Zoom", &zoom_, 4, 24);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Brush", &brushSize_, 1, 12);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    if (ImGui::Button("Undo")) {
        undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export PNGs")) {
        exportPngs(context);
    }

    ImGui::Text("Exports: %s", context.assets.rawTilesetPath().string().c_str());
    ImGui::TextDisabled("Left mouse paints. Right mouse erases. Pick Floor for depth texture, Wall for foreground occluders.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void WallFloorPaintPanel::drawLayerControls()
{
    ImGui::TextUnformatted("Layers");
    int layerIndex = static_cast<int>(activeLayer_);
    ImGui::RadioButton("Wall", &layerIndex, static_cast<int>(ActiveLayer::Wall));
    ImGui::SameLine();
    ImGui::Checkbox("Wall visible", &wall_.visible);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Wall opacity", &wall_.opacity, 0.0f, 1.0f);

    ImGui::RadioButton("Floor", &layerIndex, static_cast<int>(ActiveLayer::Floor));
    ImGui::SameLine();
    ImGui::Checkbox("Floor visible", &floor_.visible);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Floor opacity", &floor_.opacity, 0.0f, 1.0f);
    activeLayer_ = static_cast<ActiveLayer>(layerIndex);

    ImGui::Spacing();
    ImGui::TextUnformatted("Tools");
    drawToolButton("Pencil", PaintTool::Pencil);
    ImGui::SameLine();
    drawToolButton("Eraser", PaintTool::Eraser);
    drawToolButton("Fill", PaintTool::Fill);
    ImGui::SameLine();
    drawToolButton("Line", PaintTool::Line);
    ImGui::SameLine();
    drawToolButton("Rect", PaintTool::Rect);

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

void WallFloorPaintPanel::drawCanvas()
{
    const float pixelSize = static_cast<float>(zoom_);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize{static_cast<float>(width_) * pixelSize, static_cast<float>(height_) * pixelSize};
    ImGui::InvisibleButton("WallFloorCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    checkerboard(drawList, origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, pixelSize);
    drawList->PushClipRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, true);
    if (floor_.visible) {
        drawLayerPixels(drawList, floor_, origin, pixelSize);
    }
    if (wall_.visible) {
        drawLayerPixels(drawList, wall_, origin, pixelSize);
    }
    drawList->PopClipRect();

    if (showGrid_ && pixelSize >= 6.0f) {
        for (int x = 0; x <= width_; ++x) {
            const float px = origin.x + static_cast<float>(x) * pixelSize;
            drawList->AddLine({px, origin.y}, {px, origin.y + canvasSize.y}, IM_COL32(0, 0, 0, 70));
        }
        for (int y = 0; y <= height_; ++y) {
            const float py = origin.y + static_cast<float>(y) * pixelSize;
            drawList->AddLine({origin.x, py}, {origin.x + canvasSize.x, py}, IM_COL32(0, 0, 0, 70));
        }
    }

    drawList->AddRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, IM_COL32(220, 220, 220, 255));
    handleCanvasInput(origin, pixelSize);
}

void WallFloorPaintPanel::drawParallaxPreview()
{
    if (animatePreview_) {
        previewScrollX_ += ImGui::GetIO().DeltaTime * 36.0f;
    }

    ImGui::TextUnformatted("Parallax preview");
    ImGui::Checkbox("Animate floor", &animatePreview_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("Floor factor", &floorParallax_, 0.0f, 1.0f);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("Scroll X", &previewScrollX_, -256.0f, 256.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("Scroll Y", &previewScrollY_, -256.0f, 256.0f);

    const float previewScale = std::max(3.0f, std::min(8.0f, 520.0f / static_cast<float>(std::max(width_, height_))));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{static_cast<float>(width_) * previewScale, static_cast<float>(height_) * previewScale};
    ImGui::InvisibleButton("WallFloorParallaxPreview", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(14, 18, 24, 255));
    drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    if (floor_.visible) {
        drawLayerPixels(drawList, floor_, origin, previewScale, {previewScrollX_ * floorParallax_, previewScrollY_ * floorParallax_}, true);
    }
    if (wall_.visible) {
        drawLayerPixels(drawList, wall_, origin, previewScale, {previewScrollX_, previewScrollY_}, false);
    }
    drawList->PopClipRect();
    drawList->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(220, 220, 220, 255));
}

void WallFloorPaintPanel::drawToolButton(const char* label, PaintTool tool)
{
    if (tool_ == tool) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
    }
    if (ImGui::Button(label, {76.0f, 0.0f})) {
        tool_ = tool;
    }
    if (tool_ == tool) {
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

void WallFloorPaintPanel::handleCanvasInput(const ImVec2& origin, float pixelSize)
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
    undoFloor_ = floor_.pixels;
    undoWall_ = wall_.pixels;
}

void WallFloorPaintPanel::undo()
{
    if (undoFloor_.empty() || undoWall_.empty()) {
        status_ = "Nothing to undo.";
        return;
    }
    floor_.pixels = undoFloor_;
    wall_.pixels = undoWall_;
    undoFloor_.clear();
    undoWall_.clear();
    status_ = "Undid last paint operation.";
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
    for (int py = y - radius; py <= y + radius; ++py) {
        for (int px = x - radius; px <= x + radius; ++px) {
            setPixel(layer, px, py, color);
        }
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
                    fx = (x + static_cast<int>(previewScrollX_ * floorParallax_)) % width_;
                    fy = (y + static_cast<int>(previewScrollY_ * floorParallax_)) % height_;
                    if (fx < 0) {
                        fx += width_;
                    }
                    if (fy < 0) {
                        fy += height_;
                    }
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

void WallFloorPaintPanel::exportPngs(const EditorContext& context)
{
    const std::string id(assetId_.data());
    if (id.empty()) {
        status_ = "Asset id is empty.";
        return;
    }

    const std::filesystem::path outputDir = context.assets.rawTilesetPath();
    std::error_code error;
    std::filesystem::create_directories(outputDir, error);
    if (error) {
        status_ = "Failed to create export directory: " + error.message();
        return;
    }

    const std::filesystem::path floorPath = outputDir / (id + "_floor.png");
    const std::filesystem::path wallPath = outputDir / (id + "_wall.png");
    const std::filesystem::path previewPath = outputDir / (id + "_preview.png");
    if (!writePngRgba(floorPath, width_, height_, layerRgba(floor_)) ||
        !writePngRgba(wallPath, width_, height_, layerRgba(wall_)) ||
        !writePngRgba(previewPath, width_, height_, compositeRgba(true))) {
        status_ = "Failed to export one or more PNGs.";
        return;
    }

    status_ = "Exported floor, wall, and parallax preview PNGs for " + id + ".";
}

} // namespace adventure::editor
