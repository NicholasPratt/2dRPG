#include "editor/panels/layout_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace adventure::editor {
namespace {

bool inputString(const char* label, std::string& value, std::size_t maxSize = 64)
{
    char buffer[256]{};
    const std::size_t copyLen = std::min(value.size(), std::min(maxSize, sizeof(buffer) - 1));
    std::memcpy(buffer, value.data(), copyLen);
    if (ui::inputTextString(label, buffer, sizeof(buffer))) {
        value = buffer;
        if (value.size() > maxSize) {
            value.resize(maxSize);
        }
        return true;
    }
    return false;
}

ImU32 screenColor(bool selected, bool start)
{
    if (selected) {
        return IM_COL32(236, 203, 49, 255);
    }
    if (start) {
        return IM_COL32(68, 155, 255, 255);
    }
    return IM_COL32(80, 90, 102, 255);
}

ImU32 tileColor(std::uint16_t id)
{
    if (id == 0u) {
        return IM_COL32(27, 30, 34, 255);
    }
    const std::uint32_t h = static_cast<std::uint32_t>(id) * 2654435761u;
    const auto r = static_cast<unsigned char>(80u + (h & 0xffu) * 150u / 255u);
    const auto g = static_cast<unsigned char>(80u + ((h >> 8u) & 0xffu) * 150u / 255u);
    const auto b = static_cast<unsigned char>(80u + ((h >> 16u) & 0xffu) * 150u / 255u);
    return IM_COL32(r, g, b, 255);
}

ImU32 dimColor(ImU32 color, float factor)
{
    const auto r = static_cast<unsigned char>((color & 0xffu) * factor);
    const auto g = static_cast<unsigned char>(((color >> 8u) & 0xffu) * factor);
    const auto b = static_cast<unsigned char>(((color >> 16u) & 0xffu) * factor);
    return IM_COL32(r, g, b, 255);
}

ImU32 packedColor(std::uint32_t color, float opacity = 1.0f)
{
    const int alpha = static_cast<int>(static_cast<float>((color >> 24u) & 0xffu) * std::clamp(opacity, 0.0f, 1.0f));
    return IM_COL32((color >> 0u) & 0xffu, (color >> 8u) & 0xffu, (color >> 16u) & 0xffu, alpha);
}

std::filesystem::path previewPathForMap(const EditorContext& context, const std::string& mapId)
{
    const std::filesystem::path gamePath = context.assets.gameTilesetPath() / (mapId + "_preview.png");
    std::error_code error;
    if (std::filesystem::exists(gamePath, error)) {
        return gamePath;
    }
    return context.assets.rawTilesetPath() / (mapId + "_preview.png");
}

std::string portableProjectPath(const EditorContext& context, const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    const std::filesystem::path absolutePath = path.is_absolute()
        ? std::filesystem::weakly_canonical(path, error)
        : std::filesystem::weakly_canonical(context.assets.projectRoot / path, error);
    const std::filesystem::path absoluteRoot = std::filesystem::weakly_canonical(context.assets.projectRoot, error);
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, absoluteRoot, error);
    if (!error && !relative.empty() && relative.native().find("..") != 0) {
        return relative.generic_string();
    }
    return path.generic_string();
}

} // namespace

void LayoutEditorPanel::draw(EditorContext& context)
{
    applyContextSelectedScreenData(context);
    if (chapter_.screens.empty()) {
        addScreen();
        if (chapter_.startScreenId.empty()) {
            chapter_.startScreenId = chapter_.screens.front().id;
        }
    }
    selectedScreen_ = std::clamp(selectedScreen_, 0, static_cast<int>(chapter_.screens.size()) - 1);
    if (!context.selectedScreenId.empty() && selectedScreenValid() &&
        chapter_.screens[static_cast<std::size_t>(selectedScreen_)].id != context.selectedScreenId) {
        (void)selectScreenById(context, context.selectedScreenId);
    }
    syncSelectedScreenToContext(context);
    syncContextScreens(context);

    drawToolbar(context);
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(260.0f, std::max(190.0f, available.x * 0.24f));
    const float rightWidth = std::min(360.0f, std::max(280.0f, available.x * 0.30f));
    const float centerWidth = std::max(280.0f, available.x - leftWidth - rightWidth - 16.0f);

    ImGui::BeginChild("LayoutScreenList", ImVec2(leftWidth, 0.0f), true);
    drawScreenList(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("LayoutMacroView", ImVec2(centerWidth, 0.0f), true);
    drawMacroView(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("LayoutInspector", ImVec2(0.0f, 0.0f), true);
    drawScreenInspector(context);
    ImGui::EndChild();
}

void LayoutEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(220.0f);
    if (ui::inputTextString("Chapter id", chapterId_.data(), chapterId_.size())) {
        chapter_.id = chapterId_.data();
        context.currentChapterId = chapter_.id;
        context.markDirty();
    }

    if (ImGui::Button("New screen")) {
        addScreen();
        syncContextScreens(context);
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete screen")) {
        deleteSelectedScreen();
        if (selectedScreenValid()) {
            const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
            context.selectedScreenId = screen.id;
            context.selectedScreenMapId = screen.mapId;
        }
        syncContextScreens(context);
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save .adchapter")) {
        (void)saveCurrentChapter(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .adchapter")) {
        context.requestedChapterSwitchId = chapterId_.data();
        context.requestChapterSwitch = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New chapter...")) {
        context.requestCreateChapter = true;
    }

    ImGui::TextWrapped("Chapter files: %s", context.assets.gameChapterPath().string().c_str());
    ImGui::TextDisabled("A chapter is a macro layout of linked screens. Each screen points at one .admap.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void LayoutEditorPanel::drawScreenList(EditorContext& context)
{
    ImGui::TextUnformatted("Screens");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
        ImGui::PushID(i);
        const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(i)];
        const bool start = screen.id == chapter_.startScreenId;
        std::string label = screen.id + "  [" + std::to_string(screen.gridX) + "," + std::to_string(screen.gridY) + "]";
        if (start) {
            label += "  start";
        }
        if (ImGui::Selectable(label.c_str(), selectedScreen_ == i, 0, ImVec2(0.0f, 32.0f))) {
            (void)selectScreenById(context, screen.id);
        }
        ImGui::PopID();
    }
}

void LayoutEditorPanel::drawMacroView(EditorContext& context)
{
    ImGui::TextUnformatted("Screen Layout");
    ImGui::Separator();

    ui::sliderInt("Tile px", "##LayoutTilePx", &layoutTileSize_, 4, 18, 96.0f, 78.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0f);
    ImGui::InputScalar("Paint tile", ImGuiDataType_U16, &layoutSelectedTileId_);

    ImGui::TextUnformatted("Layer:");
    ImGui::SameLine();
    ImGui::RadioButton("Floor", &layoutActiveLayer_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Walls", &layoutActiveLayer_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Ceiling", &layoutActiveLayer_, 2);

    auto paintToolButton = [this](const char* label, LayoutPaintTool tool) {
        const bool selected = layoutPaintTool_ == tool;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
        }
        if (ImGui::Button(label)) {
            layoutPaintTool_ = tool;
            layoutActiveLayer_ = 1;
            layoutSelectedTileId_ = 1;
            layoutShapeDragging_ = false;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    };

    paintToolButton("Wall Brush", LayoutPaintTool::Brush);
    ImGui::SameLine();
    paintToolButton("Wall Line", LayoutPaintTool::WallLine);
    ImGui::SameLine();
    paintToolButton("Wall Rectangle", LayoutPaintTool::WallRect);
    ImGui::SameLine();
    if (ImGui::Button("Save changed maps")) {
        saveDirtyMaps(context);
    }
    ImGui::SameLine();
    ui::checkbox("Graphics preview", "##LayoutGraphicsPreview", &showGraphicsPreview_, 128.0f);
    if (showGraphicsPreview_) {
        ui::sliderFloat("Preview alpha", "##LayoutPreviewAlpha", &graphicsPreviewOpacity_, 0.15f, 1.0f, "%.2f", 96.0f, 110.0f);
    }
    if (layoutPaintTool_ == LayoutPaintTool::WallLine || layoutPaintTool_ == LayoutPaintTool::WallRect) {
        ImGui::TextDisabled("Drag on the selected screen to preview; release to apply. Right-drag erases.");
    }
    ImGui::Separator();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImGui::Dummy(canvasSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, IM_COL32(29, 32, 37, 255));
    drawList->PushClipRect(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, true);

    const ImVec2 center{origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f};
    int selectedGridX = 0;
    int selectedGridY = 0;
    int selectedMapWidth = 24;
    int selectedMapHeight = 16;
    if (selectedScreenValid()) {
        const game::ChapterScreen& selected = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
        selectedGridX = selected.gridX;
        selectedGridY = selected.gridY;
        const game::TileMap& selectedMap = ensureMapLoaded(context, selected.mapId);
        selectedMapWidth = selectedMap.width;
        selectedMapHeight = selectedMap.height;
    }
    const float tileSize = static_cast<float>(layoutTileSize_);
    const float cellW = static_cast<float>(selectedMapWidth) * tileSize;
    const float cellH = static_cast<float>(selectedMapHeight) * tileSize;

    for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
        const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(i)];
        game::TileMap& map = ensureMapLoaded(context, screen.mapId);
        const ImVec2 screenSize{
            static_cast<float>(map.width) * tileSize,
            static_cast<float>(map.height) * tileSize,
        };
        const ImVec2 min{
            center.x + static_cast<float>(screen.gridX - selectedGridX) * cellW - screenSize.x * 0.5f,
            center.y + static_cast<float>(screen.gridY - selectedGridY) * cellH - screenSize.y * 0.5f,
        };
        const ImVec2 max{min.x + screenSize.x, min.y + screenSize.y};
        const bool selected = selectedScreen_ == i;
        const bool start = screen.id == chapter_.startScreenId;
        const bool visible = max.x >= origin.x && max.y >= origin.y &&
            min.x <= origin.x + canvasSize.x && min.y <= origin.y + canvasSize.y;
        if (!visible) {
            continue;
        }

        drawScreenTileLayout(context, drawList, screen, min, tileSize, selected);
        drawList->AddRect(min, max, selected ? IM_COL32(255, 231, 94, 255) : IM_COL32(92, 101, 112, 210), 0.0f, 0, selected ? 4.0f : 1.0f);
        if (start && !selected) {
            drawList->AddRect({min.x + 3.0f, min.y + 3.0f}, {max.x - 3.0f, max.y - 3.0f}, IM_COL32(68, 155, 255, 220), 0.0f, 0, 2.0f);
        }
        if (dirtyMapIds_.find(screen.mapId) != dirtyMapIds_.end()) {
            drawList->AddCircleFilled({max.x - 8.0f, min.y + 8.0f}, 4.0f, IM_COL32(255, 160, 80, 255));
        }

        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(i);
        ImGui::InvisibleButton("ScreenHit", screenSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (!selected && ImGui::IsItemClicked()) {
            (void)selectScreenById(context, screen.id);
        }
        if (selected && layoutShapeDragging_ && layoutShapeMapId_ == screen.mapId) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int tileX = std::clamp(static_cast<int>(std::floor((mouse.x - min.x) / tileSize)), 0, map.width - 1);
            const int tileY = std::clamp(static_cast<int>(std::floor((mouse.y - min.y) / tileSize)), 0, map.height - 1);

            const ImGuiMouseButton button = layoutShapeErase_ ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
            if (layoutDragTool_ == LayoutPaintTool::Brush) {
                if (ImGui::IsMouseDown(button) && (tileX != layoutShapeX1_ || tileY != layoutShapeY1_)) {
                    layoutShapeX0_ = layoutShapeX1_;
                    layoutShapeY0_ = layoutShapeY1_;
                    layoutShapeX1_ = tileX;
                    layoutShapeY1_ = tileY;
                    paintLayoutLine(map, layoutDragLayer_, layoutShapeErase_ ? 0u : layoutDragTileId_);
                    dirtyMapIds_.insert(screen.mapId);
                    context.markDirty();
                }
            } else {
                layoutShapeX1_ = tileX;
                layoutShapeY1_ = tileY;
                drawLayoutShapePreview(drawList, min, tileSize);
            }

            if (ImGui::IsMouseReleased(button)) {
                const std::uint16_t tileId = layoutShapeErase_ ? 0u : layoutDragTileId_;
                if (layoutDragTool_ == LayoutPaintTool::WallLine) {
                    paintLayoutLine(map, layoutDragLayer_, tileId);
                    status_ = std::string(layoutShapeErase_ ? "Erased" : "Drew") + " wall line on " + screen.mapId + ".";
                } else if (layoutDragTool_ == LayoutPaintTool::WallRect) {
                    paintLayoutRect(map, layoutDragLayer_, tileId);
                    status_ = std::string(layoutShapeErase_ ? "Erased" : "Drew") + " wall rectangle on " + screen.mapId + ".";
                }
                layoutShapeDragging_ = false;
                dirtyMapIds_.insert(screen.mapId);
                context.markDirty();
            }
        }
        if (selected && ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int tileX = std::clamp(static_cast<int>(std::floor((mouse.x - min.x) / tileSize)), 0, map.width - 1);
            const int tileY = std::clamp(static_cast<int>(std::floor((mouse.y - min.y) / tileSize)), 0, map.height - 1);
            const ImVec2 hoverMin{min.x + static_cast<float>(tileX) * tileSize, min.y + static_cast<float>(tileY) * tileSize};
            drawList->AddRect(hoverMin, {hoverMin.x + tileSize, hoverMin.y + tileSize}, IM_COL32(255, 255, 255, 220), 0.0f, 0, 2.0f);
            const bool shapeTool = layoutPaintTool_ == LayoutPaintTool::WallLine ||
                layoutPaintTool_ == LayoutPaintTool::WallRect;
            ImGui::SetTooltip("%s [%d,%d] %s", screen.mapId.c_str(), tileX, tileY,
                shapeTool ? "wall shape" : "brush");
            if (!layoutShapeDragging_) {
                const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                const bool rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                if (leftClicked || rightClicked) {
                    if (shapeTool) {
                        layoutActiveLayer_ = 1;
                    }
                    layoutShapeDragging_ = true;
                    layoutShapeErase_ = rightClicked;
                    layoutDragTool_ = layoutPaintTool_;
                    layoutDragLayer_ = layoutActiveLayer_;
                    layoutDragTileId_ = layoutSelectedTileId_;
                    layoutShapeMapId_ = screen.mapId;
                    layoutShapeX0_ = layoutShapeX1_ = tileX;
                    layoutShapeY0_ = layoutShapeY1_ = tileY;
                    if (!shapeTool) {
                        const std::size_t index = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(tileX);
                        map.layers[static_cast<std::size_t>(layoutDragLayer_)][index] =
                            layoutShapeErase_ ? 0u : layoutDragTileId_;
                        dirtyMapIds_.insert(screen.mapId);
                        context.markDirty();
                    }
                }
            }
        }
        else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s -> %s", screen.id.c_str(), screen.mapId.c_str());
        }
        ImGui::PopID();
    }
    drawList->PopClipRect();
}

void LayoutEditorPanel::drawLayoutShapePreview(ImDrawList* drawList, ImVec2 min, float tileSize) const
{
    const ImU32 fill = layoutShapeErase_ ? IM_COL32(255, 70, 70, 95) : IM_COL32(255, 216, 64, 110);
    const ImU32 border = layoutShapeErase_ ? IM_COL32(255, 70, 70, 240) : IM_COL32(255, 240, 150, 245);
    auto drawCell = [&](int x, int y) {
        const ImVec2 cellMin{
            min.x + static_cast<float>(x) * tileSize,
            min.y + static_cast<float>(y) * tileSize,
        };
        drawList->AddRectFilled(cellMin, {cellMin.x + tileSize, cellMin.y + tileSize}, fill);
        drawList->AddRect(cellMin, {cellMin.x + tileSize, cellMin.y + tileSize}, border, 0.0f, 0, 2.0f);
    };

    if (layoutDragTool_ == LayoutPaintTool::WallLine) {
        int x = layoutShapeX0_;
        int y = layoutShapeY0_;
        const int dx = std::abs(layoutShapeX1_ - x);
        const int sx = x < layoutShapeX1_ ? 1 : -1;
        const int dy = -std::abs(layoutShapeY1_ - y);
        const int sy = y < layoutShapeY1_ ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            drawCell(x, y);
            if (x == layoutShapeX1_ && y == layoutShapeY1_) {
                break;
            }
            const int doubledError = error * 2;
            if (doubledError >= dy) {
                error += dy;
                x += sx;
            }
            if (doubledError <= dx) {
                error += dx;
                y += sy;
            }
        }
        return;
    }

    const int left = std::min(layoutShapeX0_, layoutShapeX1_);
    const int right = std::max(layoutShapeX0_, layoutShapeX1_);
    const int top = std::min(layoutShapeY0_, layoutShapeY1_);
    const int bottom = std::max(layoutShapeY0_, layoutShapeY1_);
    for (int x = left; x <= right; ++x) {
        drawCell(x, top);
        if (bottom != top) {
            drawCell(x, bottom);
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        drawCell(left, y);
        if (right != left) {
            drawCell(right, y);
        }
    }
}

void LayoutEditorPanel::paintLayoutLine(game::TileMap& map, int layer, std::uint16_t tileId)
{
    const std::size_t layerIndex = static_cast<std::size_t>(std::clamp(layer, 0, 2));
    int x = layoutShapeX0_;
    int y = layoutShapeY0_;
    const int dx = std::abs(layoutShapeX1_ - x);
    const int sx = x < layoutShapeX1_ ? 1 : -1;
    const int dy = -std::abs(layoutShapeY1_ - y);
    const int sy = y < layoutShapeY1_ ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        map.layers[layerIndex][static_cast<std::size_t>(y * map.width + x)] = tileId;
        if (x == layoutShapeX1_ && y == layoutShapeY1_) {
            break;
        }
        const int doubledError = error * 2;
        if (doubledError >= dy) {
            error += dy;
            x += sx;
        }
        if (doubledError <= dx) {
            error += dx;
            y += sy;
        }
    }
}

void LayoutEditorPanel::paintLayoutRect(game::TileMap& map, int layer, std::uint16_t tileId)
{
    const std::size_t layerIndex = static_cast<std::size_t>(std::clamp(layer, 0, 2));
    const int left = std::min(layoutShapeX0_, layoutShapeX1_);
    const int right = std::max(layoutShapeX0_, layoutShapeX1_);
    const int top = std::min(layoutShapeY0_, layoutShapeY1_);
    const int bottom = std::max(layoutShapeY0_, layoutShapeY1_);
    for (int x = left; x <= right; ++x) {
        map.layers[layerIndex][static_cast<std::size_t>(top * map.width + x)] = tileId;
        map.layers[layerIndex][static_cast<std::size_t>(bottom * map.width + x)] = tileId;
    }
    for (int y = top; y <= bottom; ++y) {
        map.layers[layerIndex][static_cast<std::size_t>(y * map.width + left)] = tileId;
        map.layers[layerIndex][static_cast<std::size_t>(y * map.width + right)] = tileId;
    }
}

void LayoutEditorPanel::drawScreenTileLayout(EditorContext& context, ImDrawList* drawList, const game::ChapterScreen& screen, ImVec2 min, float tileSize, bool selected)
{
    game::TileMap& map = ensureMapLoaded(context, screen.mapId);
    if (showGraphicsPreview_) {
        drawGraphicsPreview(context, drawList, map, min, tileSize);
    }
    drawMapTiles(drawList, map, min, tileSize, selected);
    drawWallOutlines(drawList, map, min, tileSize, selected ? IM_COL32(255, 226, 96, 245) : IM_COL32(255, 226, 96, 190));
}

void LayoutEditorPanel::drawGraphicsPreview(EditorContext& context, ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize)
{
    if (map.width <= 0 || map.height <= 0) {
        return;
    }

    GraphicsPreview& preview = graphicsPreviews_[map.id];
    const std::filesystem::path path = previewPathForMap(context, map.id);
    const double now = ImGui::GetTime();
    const bool shouldCheckDisk = preview.lastCheckedSeconds <= 0.0 ||
        now - preview.lastCheckedSeconds > 1.0 ||
        preview.path != path ||
        preview.mapWidth != map.width ||
        preview.mapHeight != map.height;

    if (!shouldCheckDisk) {
        if (!preview.loaded || preview.tileColors.size() != static_cast<std::size_t>(map.width * map.height)) {
            return;
        }
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const std::uint32_t color = preview.tileColors[static_cast<std::size_t>(y * map.width + x)];
                if (((color >> 24u) & 0xffu) == 0u) {
                    continue;
                }
                const ImVec2 tileMin{min.x + static_cast<float>(x) * tileSize, min.y + static_cast<float>(y) * tileSize};
                drawList->AddRectFilled(tileMin, {tileMin.x + tileSize, tileMin.y + tileSize}, packedColor(color, graphicsPreviewOpacity_));
            }
        }
        return;
    }

    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    const std::filesystem::file_time_type lastWrite = exists ? std::filesystem::last_write_time(path, error) : std::filesystem::file_time_type{};
    preview.lastCheckedSeconds = now;

    if (preview.path != path || preview.lastWrite != lastWrite ||
        preview.mapWidth != map.width || preview.mapHeight != map.height) {
        preview = GraphicsPreview{};
        preview.path = path;
        preview.lastWrite = lastWrite;
        preview.mapWidth = map.width;
        preview.mapHeight = map.height;
        preview.lastCheckedSeconds = now;

        if (exists && !error) {
            int imageW = 0;
            int imageH = 0;
            int channels = 0;
            unsigned char* data = stbi_load(path.string().c_str(), &imageW, &imageH, &channels, 4);
            if (data != nullptr && imageW > 0 && imageH > 0) {
                preview.tileColors.assign(static_cast<std::size_t>(map.width * map.height), 0u);
                for (int tileY = 0; tileY < map.height; ++tileY) {
                    for (int tileX = 0; tileX < map.width; ++tileX) {
                        const int x0 = tileX * imageW / map.width;
                        const int x1 = std::max(x0 + 1, (tileX + 1) * imageW / map.width);
                        const int y0 = tileY * imageH / map.height;
                        const int y1 = std::max(y0 + 1, (tileY + 1) * imageH / map.height);
                        std::uint32_t r = 0;
                        std::uint32_t g = 0;
                        std::uint32_t b = 0;
                        std::uint32_t a = 0;
                        std::uint32_t samples = 0;
                        for (int y = y0; y < std::min(y1, imageH); ++y) {
                            for (int x = x0; x < std::min(x1, imageW); ++x) {
                                const int i = (y * imageW + x) * 4;
                                r += data[i + 0];
                                g += data[i + 1];
                                b += data[i + 2];
                                a += data[i + 3];
                                ++samples;
                            }
                        }
                        if (samples > 0u) {
                            preview.tileColors[static_cast<std::size_t>(tileY * map.width + tileX)] =
                                ((a / samples) << 24u) | ((b / samples) << 16u) | ((g / samples) << 8u) | (r / samples);
                        }
                    }
                }
                preview.loaded = true;
            }
            stbi_image_free(data);
        }
    }

    if (!preview.loaded || preview.tileColors.size() != static_cast<std::size_t>(map.width * map.height)) {
        return;
    }

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const std::uint32_t color = preview.tileColors[static_cast<std::size_t>(y * map.width + x)];
            if (((color >> 24u) & 0xffu) == 0u) {
                continue;
            }
            const ImVec2 tileMin{min.x + static_cast<float>(x) * tileSize, min.y + static_cast<float>(y) * tileSize};
            drawList->AddRectFilled(tileMin, {tileMin.x + tileSize, tileMin.y + tileSize}, packedColor(color, graphicsPreviewOpacity_));
        }
    }
}

void LayoutEditorPanel::drawMapTiles(ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize, bool selected) const
{
    const ImVec2 max{min.x + static_cast<float>(map.width) * tileSize, min.y + static_cast<float>(map.height) * tileSize};
    if (!showGraphicsPreview_) {
        drawList->AddRectFilled(min, max, IM_COL32(13, 16, 20, 255));
    }

    if (selected) {
        for (int layer = 0; layer < 3; ++layer) {
            for (int y = 0; y < map.height; ++y) {
                for (int x = 0; x < map.width; ++x) {
                    const std::uint16_t id = map.layers[static_cast<std::size_t>(layer)][static_cast<std::size_t>(y) * map.width + x];
                    if (id == 0u) {
                        continue;
                    }
                    ImU32 color = tileColor(id);
                    if (layer == 0) {
                        color = dimColor(color, 0.55f);
                    } else if (layer == 2) {
                        color = IM_COL32(70, 105, 170, 210);
                    }
                    const ImVec2 tileMin{min.x + static_cast<float>(x) * tileSize, min.y + static_cast<float>(y) * tileSize};
                    drawList->AddRectFilled(tileMin, {tileMin.x + tileSize, tileMin.y + tileSize}, color);
                }
            }
        }
    } else {
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const std::uint16_t id = map.layers[1][static_cast<std::size_t>(y) * map.width + x];
                if (id == 0u) {
                    continue;
                }
                const ImVec2 tileMin{min.x + static_cast<float>(x) * tileSize, min.y + static_cast<float>(y) * tileSize};
                drawList->AddRectFilled(tileMin, {tileMin.x + tileSize, tileMin.y + tileSize}, IM_COL32(55, 61, 68, 210));
            }
        }
    }

    for (int y = 0; y <= map.height; ++y) {
        const float py = min.y + static_cast<float>(y) * tileSize;
        drawList->AddLine({min.x, py}, {max.x, py}, IM_COL32(55, 60, 68, selected ? 145 : 80));
    }
    for (int x = 0; x <= map.width; ++x) {
        const float px = min.x + static_cast<float>(x) * tileSize;
        drawList->AddLine({px, min.y}, {px, max.y}, IM_COL32(55, 60, 68, selected ? 145 : 80));
    }
}

void LayoutEditorPanel::drawWallOutlines(ImDrawList* drawList, const game::TileMap& map, ImVec2 min, float tileSize, ImU32 color) const
{
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const std::uint16_t id = map.layers[1][static_cast<std::size_t>(y) * map.width + x];
            if (id == 0u) {
                continue;
            }
            const ImVec2 tileMin{min.x + static_cast<float>(x) * tileSize, min.y + static_cast<float>(y) * tileSize};
            const ImVec2 tileMax{tileMin.x + tileSize, tileMin.y + tileSize};
            drawList->AddRect(tileMin, tileMax, color, 0.0f, 0, 1.5f);
        }
    }
}

void LayoutEditorPanel::drawScreenInspector(EditorContext& context)
{
    if (!selectedScreenValid()) {
        ImGui::TextDisabled("No screen selected.");
        return;
    }

    game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
    ImGui::TextUnformatted("Screen");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("Screen id", screen.id)) {
        // Keep start pointer valid when renaming the starting screen.
        if (chapter_.startScreenId.empty() || game::findScreen(chapter_, chapter_.startScreenId) == nullptr) {
            chapter_.startScreenId = screen.id;
        }
        context.selectedScreenId = screen.id;
        syncContextScreens(context);
        context.markDirty();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("Map id", screen.mapId)) {
        context.selectedScreenMapId = screen.mapId;
        syncContextScreens(context);
        context.markDirty();
    }

    int grid[2]{screen.gridX, screen.gridY};
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputInt2("Grid", grid)) {
        screen.gridX = std::clamp(grid[0], -512, 512);
        screen.gridY = std::clamp(grid[1], -512, 512);
        syncContextScreens(context);
        context.markDirty();
    }

    if (ImGui::Button("Set as start")) {
        chapter_.startScreenId = screen.id;
        context.markDirty();
    }
    ImGui::SameLine();
    ImGui::Text("Start: %s", chapter_.startScreenId.c_str());
    if (ImGui::Button("Delete This Screen", ImVec2(-1.0f, 30.0f))) {
        deleteSelectedScreen();
        if (selectedScreenValid()) {
            syncSelectedScreenToContext(context);
        }
        syncContextScreens(context);
        context.markDirty();
        return;
    }

    ImGui::Spacing();
    if (ui::checkbox("Respawn enemies on re-enter", "##RespawnEnemiesOnReenter", &screen.respawnEnemies, 190.0f)) {
        context.markDirty();
    }
    ImGui::TextDisabled("Off = defeated enemies stay gone (spec default).");

    ImGui::Spacing();
    ImGui::TextUnformatted("Links");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("North", screen.links.north)) {
        context.markDirty();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("South", screen.links.south)) {
        context.markDirty();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("East", screen.links.east)) {
        context.markDirty();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (inputString("West", screen.links.west)) {
        context.markDirty();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Add connected screen");
    if (ImGui::Button("North##AddConnectedNorth", ImVec2(96.0f, 30.0f))) {
        addConnectedScreen(context, "north", 0, -1);
    }
    ImGui::SameLine();
    if (ImGui::Button("South##AddConnectedSouth", ImVec2(96.0f, 30.0f))) {
        addConnectedScreen(context, "south", 0, 1);
    }
    if (ImGui::Button("West##AddConnectedWest", ImVec2(96.0f, 30.0f))) {
        addConnectedScreen(context, "west", -1, 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("East##AddConnectedEast", ImVec2(96.0f, 30.0f))) {
        addConnectedScreen(context, "east", 1, 0);
    }

    ImGui::Spacing();
    if (ImGui::Button("Edit Enemies", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditEnemies = true;
    }

    if (ImGui::Button("Edit Enemy Types", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditEnemyTypes = true;
    }

    if (ImGui::Button("Edit Items", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditItems = true;
    }

    if (ImGui::Button("Edit Doors", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditDoors = true;
    }

    if (ImGui::Button("Edit Chapter Exits", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditChapterExits = true;
    }

    if (ImGui::Button("Edit NPCs", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditNpcs = true;
    }

    if (ImGui::Button("Edit NPC Types", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditNpcTypes = true;
    }

    if (ImGui::Button("Edit Music/SFX", ImVec2(-1.0f, 34.0f))) {
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditScreenMusic = true;
    }

    if (ImGui::Button("Edit Screen Graphics", ImVec2(-1.0f, 34.0f))) {
        // EditorApp consumes this request and opens the map/pixel editor on this screen's map.
        // The wall/mid layer remains outlined there for spatial context.
        context.selectedScreenId = screen.id;
        context.selectedScreenMapId = screen.mapId;
        context.requestEditScreenGraphics = true;
    }
}

void LayoutEditorPanel::drawScreenMusicSfx(EditorContext& context)
{
    if (!selectedScreenValid()) {
        ImGui::TextDisabled("No screen selected.");
        return;
    }

    game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
    ImGui::Text("Screen: %s", screen.id.c_str());
    ImGui::Text("Music folder: %s", context.assets.gameMusicPath().string().c_str());
    ImGui::Text("Walking SFX folder: %s", (context.assets.gameSfxPath() / "walking").string().c_str());
    ImGui::Separator();

    drawMusicFilePicker(context, screen);

    if (ui::checkbox("Loop music", "##ScreenMusicLoop", &screen.musicLoop, 120.0f)) {
        context.markDirty();
    }

    ImGui::SeparatorText("Walking SFX");
    drawWalkingSfxFilePicker(context, screen);

    ImGui::Spacing();
    ImGui::TextDisabled("Stored on the chapter screen. Use paths relative to the project, such as assets/game/music/screen1.ogg.");
}

void LayoutEditorPanel::drawMusicFilePicker(EditorContext& context, game::ChapterScreen& screen)
{
    if (inputString("Music path", screen.musicPath, 240)) {
        screen.musicPath = portableProjectPath(context, screen.musicPath);
        context.markDirty();
    }

    const std::filesystem::path musicDir = context.assets.gameMusicPath();
    std::error_code error;
    if (!std::filesystem::exists(musicDir, error)) {
        ImGui::TextDisabled("Music folder does not exist yet.");
        return;
    }

    ImGui::TextUnformatted("Available music");
    bool foundAny = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(musicDir, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || entry.path().extension() != ".ogg") {
            continue;
        }
        foundAny = true;
        const std::string label = entry.path().filename().string();
        if (ImGui::Selectable(label.c_str(), screen.musicPath == portableProjectPath(context, entry.path()))) {
            screen.musicPath = portableProjectPath(context, entry.path());
            context.markDirty();
        }
    }
    if (!foundAny) {
        ImGui::TextDisabled("No .ogg files found.");
    }
}

void LayoutEditorPanel::drawWalkingSfxFilePicker(EditorContext& context, game::ChapterScreen& screen)
{
    if (inputString("Walking SFX path", screen.walkingSfxPath, 240)) {
        screen.walkingSfxPath = portableProjectPath(context, screen.walkingSfxPath);
        context.markDirty();
    }

    const std::filesystem::path walkingDir = context.assets.gameSfxPath() / "walking";
    std::error_code error;
    if (!std::filesystem::exists(walkingDir, error)) {
        ImGui::TextDisabled("Walking SFX folder does not exist yet.");
        return;
    }

    ImGui::TextUnformatted("Available walking SFX");
    bool foundAny = false;
    if (ImGui::Selectable("<None>", screen.walkingSfxPath.empty())) {
        screen.walkingSfxPath.clear();
        context.markDirty();
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(walkingDir, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error)) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".ogg" && extension != ".wav") {
            continue;
        }
        foundAny = true;
        std::error_code relativeError;
        const std::string label = std::filesystem::relative(entry.path(), walkingDir, relativeError).generic_string();
        const std::string path = portableProjectPath(context, entry.path());
        if (ImGui::Selectable(relativeError ? entry.path().filename().string().c_str() : label.c_str(),
                screen.walkingSfxPath == path)) {
            screen.walkingSfxPath = path;
            context.markDirty();
        }
    }
    if (!foundAny) {
        ImGui::TextDisabled("No .ogg or .wav files found.");
    }
}

void LayoutEditorPanel::addScreen()
{
    game::ChapterScreen screen;
    screen.id = nextScreenId();
    screen.mapId = screen.id + "_map";
    screen.gridX = chapter_.screens.empty() ? 0 : chapter_.screens.back().gridX + 1;
    screen.gridY = 0;
    chapter_.screens.push_back(std::move(screen));
    selectedScreen_ = static_cast<int>(chapter_.screens.size()) - 1;
}

void LayoutEditorPanel::addConnectedScreen(EditorContext& context, const char* direction, int dx, int dy)
{
    if (!selectedScreenValid()) {
        return;
    }

    const int sourceIndex = selectedScreen_;
    const std::string sourceId = chapter_.screens[static_cast<std::size_t>(sourceIndex)].id;
    const int targetX = chapter_.screens[static_cast<std::size_t>(sourceIndex)].gridX + dx;
    const int targetY = chapter_.screens[static_cast<std::size_t>(sourceIndex)].gridY + dy;

    game::ChapterScreen* target = screenAt(targetX, targetY);
    std::string targetId;
    if (target == nullptr) {
        game::ChapterScreen screen;
        screen.id = nextScreenId();
        screen.mapId = screen.id + "_map";
        screen.gridX = targetX;
        screen.gridY = targetY;
        targetId = screen.id;
        chapter_.screens.push_back(std::move(screen));
        selectedScreen_ = static_cast<int>(chapter_.screens.size()) - 1;
    } else {
        targetId = target->id;
        for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
            if (chapter_.screens[static_cast<std::size_t>(i)].id == targetId) {
                selectedScreen_ = i;
                break;
            }
        }
    }

    game::ChapterScreen* source = screenById(sourceId);
    target = screenById(targetId);
    if (source == nullptr || target == nullptr) {
        return;
    }

    if (std::strcmp(direction, "north") == 0) {
        source->links.north = target->id;
        target->links.south = source->id;
    } else if (std::strcmp(direction, "south") == 0) {
        source->links.south = target->id;
        target->links.north = source->id;
    } else if (std::strcmp(direction, "east") == 0) {
        source->links.east = target->id;
        target->links.west = source->id;
    } else if (std::strcmp(direction, "west") == 0) {
        source->links.west = target->id;
        target->links.east = source->id;
    }

    syncSelectedScreenToContext(context);
    syncContextScreens(context);
    context.markDirty();
}

void LayoutEditorPanel::deleteSelectedScreen()
{
    if (chapter_.screens.size() <= 1 || !selectedScreenValid()) {
        status_ = "A chapter must keep at least one screen.";
        return;
    }

    const std::string deletedId = chapter_.screens[static_cast<std::size_t>(selectedScreen_)].id;
    chapter_.screens.erase(chapter_.screens.begin() + selectedScreen_);
    selectedScreen_ = std::clamp(selectedScreen_, 0, static_cast<int>(chapter_.screens.size()) - 1);

    for (game::ChapterScreen& screen : chapter_.screens) {
        if (screen.links.north == deletedId) screen.links.north.clear();
        if (screen.links.south == deletedId) screen.links.south.clear();
        if (screen.links.east == deletedId) screen.links.east.clear();
        if (screen.links.west == deletedId) screen.links.west.clear();
    }
    if (chapter_.startScreenId == deletedId) {
        chapter_.startScreenId = chapter_.screens.front().id;
    }
}

game::ChapterScreen* LayoutEditorPanel::screenAt(int gridX, int gridY)
{
    for (game::ChapterScreen& screen : chapter_.screens) {
        if (screen.gridX == gridX && screen.gridY == gridY) {
            return &screen;
        }
    }
    return nullptr;
}

game::ChapterScreen* LayoutEditorPanel::screenById(const std::string& screenId)
{
    for (game::ChapterScreen& screen : chapter_.screens) {
        if (screen.id == screenId) {
            return &screen;
        }
    }
    return nullptr;
}

std::string LayoutEditorPanel::nextScreenId() const
{
    for (int index = static_cast<int>(chapter_.screens.size()) + 1; index < 10000; ++index) {
        const std::string candidate = "screen_" + std::to_string(index);
        if (game::findScreen(chapter_, candidate) == nullptr) {
            return candidate;
        }
    }
    return "screen_" + std::to_string(chapter_.screens.size() + 1);
}

game::TileMap& LayoutEditorPanel::ensureMapLoaded(EditorContext& context, const std::string& mapId)
{
    const std::string resolvedMapId = mapId.empty() ? "new_map" : mapId;
    auto it = loadedMaps_.find(resolvedMapId);
    if (it != loadedMaps_.end()) {
        return it->second;
    }

    game::TileMap map;
    map.id = resolvedMapId;
    const std::filesystem::path inputPath = context.assets.gameMapPath() / (resolvedMapId + ".admap");
    std::string error;
    if (!game::loadTileMap(inputPath, map, &error)) {
        map.id = resolvedMapId;
        status_ = "Using new unsaved map for " + resolvedMapId + ".";
    }

    auto inserted = loadedMaps_.emplace(resolvedMapId, std::move(map));
    return inserted.first->second;
}

void LayoutEditorPanel::saveDirtyMaps(EditorContext& context)
{
    if (dirtyMapIds_.empty()) {
        return;
    }

    int savedCount = 0;
    std::string lastError;
    for (const std::string& mapId : dirtyMapIds_) {
        auto it = loadedMaps_.find(mapId);
        if (it == loadedMaps_.end()) {
            continue;
        }

        std::string error;
        const std::filesystem::path outputPath = context.assets.gameMapPath() / (mapId + ".admap");

        // The cached map only tracks tile layers edited here. Obstacles, items,
        // and doors are authored in other editors and may have changed on disk
        // since this map was cached, so re-read them to avoid clobbering them.
        game::TileMap onDisk;
        if (game::loadTileMap(outputPath, onDisk, nullptr)) {
            it->second.obstacles = std::move(onDisk.obstacles);
            it->second.items = std::move(onDisk.items);
            it->second.doors = std::move(onDisk.doors);
            it->second.chapterExits = std::move(onDisk.chapterExits);
        }

        if (game::saveTileMap(outputPath, it->second, &error)) {
            ++savedCount;
        } else {
            lastError = error;
        }
    }

    if (lastError.empty()) {
        dirtyMapIds_.clear();
        status_ = "Saved " + std::to_string(savedCount) + " changed map(s).";
    } else {
        status_ = "Failed to save one or more maps: " + lastError;
    }
}

bool LayoutEditorPanel::saveCurrentChapter(EditorContext& context)
{
    applyContextSelectedScreenData(context);
    saveDirtyMaps(context);

    chapter_.id = chapterId_.data();
    chapter_.importedCharacterIds = context.importedCharacterIds;
    chapter_.playableCharacterId = context.playableCharacterId;
    if (game::findScreen(chapter_, chapter_.startScreenId) == nullptr && !chapter_.screens.empty()) {
        chapter_.startScreenId = chapter_.screens.front().id;
    }

    std::string error;
    const std::filesystem::path outputPath = context.assets.gameChapterPath() / (chapter_.id + ".adchapter");
    if (game::saveChapter(outputPath, chapter_, &error)) {
        status_ = "Saved chapter: " + outputPath.generic_string();
        context.currentChapterId = chapter_.id;
        syncContextScreens(context);
        context.dirty = false;
        return true;
    } else {
        status_ = "Failed to save chapter: " + error;
        return false;
    }
}

void LayoutEditorPanel::applyContextSelectedScreenData(EditorContext& context)
{
    const std::string enemyOwnerId = context.selectedScreenEnemiesOwnerId.empty()
        ? context.selectedScreenId
        : context.selectedScreenEnemiesOwnerId;
    if (!enemyOwnerId.empty()) {
        if (game::ChapterScreen* screen = screenById(enemyOwnerId)) {
            screen->enemies = context.selectedScreenEnemies;
        }
    }
    const std::string npcOwnerId = context.selectedScreenNpcsOwnerId.empty()
        ? context.selectedScreenId
        : context.selectedScreenNpcsOwnerId;
    if (!npcOwnerId.empty()) {
        if (game::ChapterScreen* screen = screenById(npcOwnerId)) {
            screen->npcs = context.selectedScreenNpcs;
        }
    }
    const std::string animTileOwnerId = context.selectedScreenAnimatedTilesOwnerId.empty()
        ? context.selectedScreenId
        : context.selectedScreenAnimatedTilesOwnerId;
    if (!animTileOwnerId.empty()) {
        if (game::ChapterScreen* screen = screenById(animTileOwnerId)) {
            screen->animatedTiles = context.selectedScreenAnimatedTiles;
        }
    }
}

bool LayoutEditorPanel::selectScreenById(EditorContext& context, const std::string& screenId)
{
    applyContextSelectedScreenData(context);
    for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
        if (chapter_.screens[static_cast<std::size_t>(i)].id == screenId) {
            selectedScreen_ = i;
            syncSelectedScreenToContext(context);
            return true;
        }
    }
    return false;
}

bool LayoutEditorPanel::loadChapterById(EditorContext& context, const std::string& chapterId)
{
    const std::filesystem::path inputPath = context.assets.gameChapterPath() / (chapterId + ".adchapter");

    std::string error;
    game::Chapter loaded;
    if (!game::loadChapter(inputPath, loaded, &error)) {
        status_ = "Failed to load chapter: " + error;
        return false;
    }

    chapter_ = std::move(loaded);
    loadedMaps_.clear();
    dirtyMapIds_.clear();
    graphicsPreviews_.clear();
    layoutShapeDragging_ = false;
    selectedScreen_ = 0;
    syncChapterIdBuffer();
    context.currentChapterId = chapter_.id;
    context.importedCharacterIds = chapter_.importedCharacterIds;
    context.playableCharacterId = chapter_.playableCharacterId;
    if (!chapter_.screens.empty()) {
        context.selectedScreenId = chapter_.screens.front().id;
        context.selectedScreenMapId = chapter_.screens.front().mapId;
        context.selectedScreenEnemies = chapter_.screens.front().enemies;
        context.selectedScreenEnemiesOwnerId = chapter_.screens.front().id;
        context.selectedScreenNpcs = chapter_.screens.front().npcs;
        context.selectedScreenNpcsOwnerId = chapter_.screens.front().id;
        context.selectedScreenAnimatedTiles = chapter_.screens.front().animatedTiles;
        context.selectedScreenAnimatedTilesOwnerId = chapter_.screens.front().id;
    }
    syncContextScreens(context);
    context.dirty = false;
    status_ = "Loaded chapter: " + inputPath.generic_string();
    return true;
}

void LayoutEditorPanel::createChapter(EditorContext& context, const std::string& chapterId)
{
    chapter_ = game::Chapter{};
    chapter_.id = chapterId.empty() ? "chapter_1" : chapterId;
    game::ChapterScreen firstScreen;
    firstScreen.id = "screen_1";
    firstScreen.mapId = chapter_.id + "_screen_1_map";
    chapter_.screens = {std::move(firstScreen)};
    chapter_.startScreenId = chapter_.screens.front().id;
    loadedMaps_.clear();
    dirtyMapIds_.clear();
    graphicsPreviews_.clear();
    layoutShapeDragging_ = false;
    selectedScreen_ = 0;
    syncChapterIdBuffer();
    context.currentChapterId = chapter_.id;
    context.importedCharacterIds.clear();
    context.playableCharacterId.clear();
    context.selectedScreenId = chapter_.screens.front().id;
    context.selectedScreenMapId = chapter_.screens.front().mapId;
    context.selectedScreenEnemies = chapter_.screens.front().enemies;
    context.selectedScreenEnemiesOwnerId = chapter_.screens.front().id;
    context.selectedScreenNpcs = chapter_.screens.front().npcs;
    context.selectedScreenNpcsOwnerId = chapter_.screens.front().id;
    context.selectedScreenAnimatedTiles = chapter_.screens.front().animatedTiles;
    context.selectedScreenAnimatedTilesOwnerId = chapter_.screens.front().id;
    syncContextScreens(context);
    context.markDirty();
    status_ = "Created new chapter: " + chapter_.id;
}

void LayoutEditorPanel::syncContextScreens(EditorContext& context) const
{
    context.chapterScreens.clear();
    context.chapterScreens.reserve(chapter_.screens.size());
    for (const game::ChapterScreen& screen : chapter_.screens) {
        context.chapterScreens.push_back({screen.id, screen.mapId, screen.gridX, screen.gridY});
    }
}

void LayoutEditorPanel::syncSelectedScreenToContext(EditorContext& context) const
{
    if (!selectedScreenValid()) {
        context.selectedScreenId.clear();
        context.selectedScreenMapId.clear();
        context.selectedScreenEnemies.clear();
        context.selectedScreenEnemiesOwnerId.clear();
        context.selectedScreenAnimatedTiles.clear();
        context.selectedScreenAnimatedTilesOwnerId.clear();
        return;
    }
    const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
    context.currentChapterId = chapter_.id;
    context.selectedScreenId = screen.id;
    context.selectedScreenMapId = screen.mapId;
    context.selectedScreenEnemies = screen.enemies;
    context.selectedScreenEnemiesOwnerId = screen.id;
    context.selectedScreenNpcs = screen.npcs;
    context.selectedScreenNpcsOwnerId = screen.id;
    context.selectedScreenAnimatedTiles = screen.animatedTiles;
    context.selectedScreenAnimatedTilesOwnerId = screen.id;
}

void LayoutEditorPanel::syncChapterIdBuffer()
{
    std::memset(chapterId_.data(), 0, chapterId_.size());
    const std::size_t copyLen = std::min(chapter_.id.size(), chapterId_.size() - 1);
    std::memcpy(chapterId_.data(), chapter_.id.data(), copyLen);
}

bool LayoutEditorPanel::selectedScreenValid() const
{
    return selectedScreen_ >= 0 && selectedScreen_ < static_cast<int>(chapter_.screens.size());
}

} // namespace adventure::editor
