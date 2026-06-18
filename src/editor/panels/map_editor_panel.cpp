#include "editor/panels/map_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/map.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace adventure::editor {
namespace {

constexpr int kMinMapSize = 1;
constexpr int kMaxMapSize = 128;
constexpr int kMinTileSize = 8;
constexpr int kMaxTileSize = 48;
constexpr int kWorldTileSize = game::kTileSize;
constexpr float kPlayerSize = 32.0f;
constexpr float kPlayerSpeed = 96.0f;

const char* kLayerNames[] = {"Floor", "Mid", "Ceiling"};
const char* kObstacleTypeNames[] = {"Spikes", "Pit", "Timed Spikes"};

// Deterministic hashed color per tile ID. Tile 0 = dark background.
ImU32 colorForTileId(uint16_t id)
{
    if (id == 0) {
        return IM_COL32(30, 30, 30, 255);
    }
    uint32_t h = static_cast<uint32_t>(id) * 2654435761u;
    auto r = static_cast<uint8_t>(80 + (h & 0xFFu) * 175u / 255u);
    auto g = static_cast<uint8_t>(80 + ((h >> 8u) & 0xFFu) * 175u / 255u);
    auto b = static_cast<uint8_t>(80 + ((h >> 16u) & 0xFFu) * 175u / 255u);
    return IM_COL32(r, g, b, 255);
}

ImU32 dimColor(ImU32 col, float factor)
{
    auto r = static_cast<uint8_t>((col & 0xFFu) * factor);
    auto g = static_cast<uint8_t>(((col >> 8u) & 0xFFu) * factor);
    auto b = static_cast<uint8_t>(((col >> 16u) & 0xFFu) * factor);
    return IM_COL32(r, g, b, 255);
}

// Blue-tinted color for ceiling layer tiles
ImU32 ceilingColor(ImU32 col)
{
    auto r = static_cast<uint8_t>((col & 0xFFu) * 0.4f + 20);
    auto g = static_cast<uint8_t>(((col >> 8u) & 0xFFu) * 0.4f + 20);
    auto b = static_cast<uint8_t>(((col >> 16u) & 0xFFu) * 0.4f + 90);
    return IM_COL32(r, g, b, 255);
}

ImU32 obstacleColor(game::ObstacleType type, bool active)
{
    switch (type) {
        case game::ObstacleType::Spike: return IM_COL32(230, 60, 70, 165);
        case game::ObstacleType::Pit: return IM_COL32(20, 20, 28, 220);
        case game::ObstacleType::TimedSpike: return active ? IM_COL32(245, 160, 45, 170) : IM_COL32(80, 150, 210, 95);
    }
    return IM_COL32(255, 255, 255, 120);
}

const char* obstacleTypeName(game::ObstacleType type)
{
    switch (type) {
        case game::ObstacleType::Spike: return "Spikes";
        case game::ObstacleType::Pit: return "Pit";
        case game::ObstacleType::TimedSpike: return "Timed Spikes";
    }
    return "Obstacle";
}

} // namespace

void MapEditorPanel::draw(EditorContext& context)
{
    if (!context.selectedScreenId.empty()) {
        ImGui::Text("Selected screen: %s", context.selectedScreenId.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("map %s", context.selectedScreenMapId.c_str());
    }

    drawToolbar(context);
    ImGui::Separator();
    if (testMode_) {
        drawTestGame();
    } else {
        if (tilesetLoaded_) {
            drawTilesetPalette();
            ImGui::Separator();
        }
        drawGrid(context);
    }
}

void MapEditorPanel::resizeMap(int width, int height)
{
    width = std::clamp(width, kMinMapSize, kMaxMapSize);
    height = std::clamp(height, kMinMapSize, kMaxMapSize);
    if (width == width_ && height == height_) {
        return;
    }

    const int copyWidth = std::min(width_, width);
    const int copyHeight = std::min(height_, height);
    for (int l = 0; l < 3; ++l) {
        std::vector<uint16_t> resized(static_cast<std::size_t>(width * height), 0u);
        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                resized[static_cast<std::size_t>(y) * width + x] = tileAt(x, y, l);
            }
        }
        layers_[l] = std::move(resized);
    }

    width_ = width;
    height_ = height;
    obstacles_.erase(std::remove_if(obstacles_.begin(), obstacles_.end(), [this](const game::MapObstacle& obstacle) {
        return obstacle.x < 0 || obstacle.y < 0 || obstacle.x + obstacle.width > width_ || obstacle.y + obstacle.height > height_;
    }), obstacles_.end());
    selectedObstacle_ = -1;
    obstacleBufFor_ = -1;
    spawnX_ = std::clamp(spawnX_, 0, width_ - 1);
    spawnY_ = std::clamp(spawnY_, 0, height_ - 1);
}

void MapEditorPanel::openMapId(EditorContext& context, const std::string& mapId)
{
    if (mapId.empty()) {
        return;
    }

    std::memset(mapId_.data(), 0, mapId_.size());
    const std::size_t copyLen = std::min(mapId.size(), mapId_.size() - 1);
    std::memcpy(mapId_.data(), mapId.data(), copyLen);
    loadMap(context);
}

void MapEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(220.0f);
    if (ui::inputTextString("Map id", mapId_.data(), mapId_.size())) {
        context.markDirty();
    }

    int size[2]{width_, height_};
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputInt2("Map size", size)) {
        resizeMap(size[0], size[1]);
        context.markDirty();
    }
    ImGui::SameLine();
    if (ui::sliderInt("Tile px", "##MapTilePx", &tileSize_, kMinTileSize, kMaxTileSize, 100.0f, 70.0f)) {
        tileSize_ = std::clamp(tileSize_, kMinTileSize, kMaxTileSize);
    }

    ui::checkbox("Obstacle edit", "##MapObstacleEdit", &obstacleMode_, 104.0f);
    ImGui::SameLine();
    if (ui::checkbox("Player place", "##MapPlayerPlace", &playerPlacementMode_, 104.0f) && playerPlacementMode_) {
        obstacleMode_ = false;
        editMode_ = EditMode::Paint;
    }
    if (obstacleMode_) {
        playerPlacementMode_ = false;
    }
    if (!obstacleMode_ && !playerPlacementMode_) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Layer:");
        for (int l = 0; l < 3; ++l) {
            ImGui::SameLine();
            ImGui::RadioButton(kLayerNames[l], &activeLayer_, l);
        }
    } else if (obstacleMode_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("New type", &activeObstacleType_, kObstacleTypeNames, 3);
        ImGui::SameLine();
        ImGui::TextDisabled("(click an empty cell to add)");
        drawObstacleInspector(context);
    }

    if (playerPlacementMode_) {
        int spawn[2]{spawnX_, spawnY_};
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt2("Player tile", spawn)) {
            spawnX_ = std::clamp(spawn[0], 0, width_ - 1);
            spawnY_ = std::clamp(spawn[1], 0, height_ - 1);
            context.markDirty();
        }
    }

    // Tileset selector
    ImGui::SetNextItemWidth(220.0f);
    ui::inputTextString("Tileset id", tilesetId_.data(), tilesetId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Load tileset")) {
        loadTileset(context);
    }
    if (tilesetLoaded_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "loaded (%d tiles)",
            static_cast<int>(loadedTileset_.tiles.size()));
    }

    if (ImGui::Button("Fill")) {
        recordUndo();
        std::fill(layers_[activeLayer_].begin(), layers_[activeLayer_].end(), selectedTileId_);
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear layer")) {
        recordUndo();
        std::fill(layers_[activeLayer_].begin(), layers_[activeLayer_].end(), static_cast<uint16_t>(0u));
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo")) {
        undoMap();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", static_cast<int>(undoStack_.size()));

    auto wallToolButton = [this](const char* label, EditMode mode) {
        const bool selected = editMode_ == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
        }
        if (ImGui::Button(label)) {
            editMode_ = mode;
            activeLayer_ = 1;
            obstacleMode_ = false;
            playerPlacementMode_ = false;
            selectionDragging_ = false;
            hasSelection_ = false;
            wallShapeDragging_ = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Drag on the grid to draw Mid-layer collision walls. Right-drag erases.");
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    };

    ImGui::SameLine();
    wallToolButton("Wall line", EditMode::WallLine);
    ImGui::SameLine();
    wallToolButton("Wall rectangle", EditMode::WallRect);

    // Copy / paste controls
    ImGui::SameLine();
    if (editMode_ == EditMode::Paint) {
        if (ImGui::Button("Select region")) {
            editMode_ = EditMode::Select;
            hasSelection_ = false;
        }
    } else if (editMode_ == EditMode::Select) {
        if (hasSelection_ && ImGui::Button("Copy")) {
            copySelection();
        }
        if (!clipboard_.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Paste")) {
                editMode_ = EditMode::Paste;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel select")) {
            editMode_ = EditMode::Paint;
            hasSelection_ = false;
        }
    } else if (editMode_ == EditMode::Paste) {
        if (!clipboard_.empty() && ImGui::Button("Paste again")) {
            // stays in paste mode
        }
        ImGui::SameLine();
        if (ImGui::Button("Done paste")) {
            editMode_ = EditMode::Paint;
        }
    } else {
        if (ImGui::Button("Freehand paint")) {
            editMode_ = EditMode::Paint;
            wallShapeDragging_ = false;
        }
    }

    if (ImGui::Button("Save .admap")) {
        saveMap(context);
        context.dirty = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .admap")) {
        loadMap(context);
    }
    ImGui::SameLine();
    if (ImGui::Button(testMode_ ? "Stop test" : "Test map")) {
        if (testMode_) {
            testMode_ = false;
        } else {
            startTestGame();
        }
    }

    const char* editingName = playerPlacementMode_ ? "player placement" :
        (obstacleMode_ ? "obstacles" :
            (editMode_ == EditMode::WallLine ? "wall line" :
                (editMode_ == EditMode::WallRect ? "wall rectangle" : kLayerNames[activeLayer_])));
    ImGui::Text("Spawn: %d,%d   Active tile ID: %d   Editing: %s",
        spawnX_, spawnY_, static_cast<int>(selectedTileId_),
        editingName);
    ImGui::Text("Map files: %s", context.assets.gameMapPath().string().c_str());
    ImGui::TextDisabled("Tiles: left paint, right erase. Wall tools: drag to draw, right-drag to erase. Player place: left set spawn. Obstacles: left add/select, right delete. Ctrl+Z undo.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }

    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        undoMap();
    }
}

void MapEditorPanel::drawTilesetPalette()
{
    constexpr float kPaletteSize = 24.0f;
    constexpr int kPaletteCols = 16;

    ImGui::Text("Tileset: %s — click to select", loadedTileset_.id.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const int count = static_cast<int>(loadedTileset_.tiles.size());
    const int paletteRows = (count + kPaletteCols - 1) / kPaletteCols;
    const ImVec2 paletteSize{
        static_cast<float>(kPaletteCols) * kPaletteSize,
        static_cast<float>(paletteRows) * kPaletteSize,
    };

    ImGui::InvisibleButton("TilePalette", paletteSize, ImGuiButtonFlags_MouseButtonLeft);

    for (int i = 0; i < count; ++i) {
        const game::TileDef& t = loadedTileset_.tiles[static_cast<std::size_t>(i)];
        const int col = i % kPaletteCols;
        const int row = i / kPaletteCols;
        const ImVec2 min{origin.x + col * kPaletteSize, origin.y + row * kPaletteSize};
        const ImVec2 max{min.x + kPaletteSize, min.y + kPaletteSize};
        const auto tileId = static_cast<uint16_t>(t.id);

        dl->AddRectFilled(min, max, colorForTileId(tileId));

        if (tileId == selectedTileId_) {
            dl->AddRect(min, max, IM_COL32(255, 216, 64, 255), 0.0f, 0, 2.0f);
        } else {
            dl->AddRect(min, max, IM_COL32(0, 0, 0, 120));
        }

        if (t.solid) {
            dl->AddCircleFilled({min.x + 4.0f, min.y + 4.0f}, 3.0f, IM_COL32(255, 80, 80, 200));
        }
    }

    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int col = static_cast<int>((mouse.x - origin.x) / kPaletteSize);
        const int row = static_cast<int>((mouse.y - origin.y) / kPaletteSize);
        const int idx = row * kPaletteCols + col;
        if (idx >= 0 && idx < count) {
            const game::TileDef& t = loadedTileset_.tiles[static_cast<std::size_t>(idx)];
            ImGui::SetTooltip("ID %d: %s (%s)", t.id, t.name.c_str(), t.solid ? "solid" : "passable");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selectedTileId_ = static_cast<uint16_t>(t.id);
            }
        }
    }
}

void MapEditorPanel::drawGrid(EditorContext& context)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridSize{
        static_cast<float>(width_ * tileSize_),
        static_cast<float>(height_ * tileSize_),
    };
    ImGui::InvisibleButton("MapGrid", gridSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + gridSize.x, origin.y + gridSize.y}, IM_COL32(0, 0, 0, 255));

    // Render floor, mid, ceiling in order
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const uint16_t id = tileAt(x, y, pass);
                if (id == 0u) {
                    continue;
                }
                const ImVec2 min{
                    origin.x + static_cast<float>(x * tileSize_),
                    origin.y + static_cast<float>(y * tileSize_),
                };
                const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};
                ImU32 color = colorForTileId(id);
                if (pass == 0) {
                    color = dimColor(color, 0.5f);
                } else if (pass == 2) {
                    color = ceilingColor(color);
                }
                // Dim inactive layers slightly when not the editing target
                if (pass != activeLayer_) {
                    color = dimColor(color, 0.75f);
                }
                drawList->AddRectFilled(min, max, color);
            }
        }
    }

    drawWallOutlines(drawList, origin);
    drawObstacles(drawList, origin);

    // Grid lines
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const ImVec2 min{
                origin.x + static_cast<float>(x * tileSize_),
                origin.y + static_cast<float>(y * tileSize_),
            };
            const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};
            drawList->AddRect(min, max, IM_COL32(50, 50, 50, 180));
        }
    }

    // Player spawn indicator
    const ImVec2 spawnCenter{
        origin.x + static_cast<float>(spawnX_ * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
        origin.y + static_cast<float>(spawnY_ * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
    };
    const float playerScale = static_cast<float>(tileSize_) / static_cast<float>(kWorldTileSize);
    const float playerDraw = kPlayerSize * playerScale;
    const ImVec2 playerMin{spawnCenter.x - playerDraw * 0.5f, spawnCenter.y - playerDraw * 0.5f};
    const ImVec2 playerMax{playerMin.x + playerDraw, playerMin.y + playerDraw};
    drawList->AddRectFilled(playerMin, playerMax, IM_COL32(80, 180, 255, playerPlacementMode_ ? 92 : 48), 3.0f);
    drawList->AddRect(playerMin, playerMax, IM_COL32(5, 25, 45, 220), 3.0f, 0, playerPlacementMode_ ? 2.0f : 1.0f);
    drawList->AddCircleFilled(spawnCenter, std::max(4.0f, static_cast<float>(tileSize_) * 0.25f),
        IM_COL32(80, 180, 255, 255));
    drawList->AddCircle(spawnCenter, std::max(5.0f, static_cast<float>(tileSize_) * 0.32f),
        IM_COL32(5, 25, 45, 255), 0, 2.0f);

    // Selection overlay
    if ((editMode_ == EditMode::Select || editMode_ == EditMode::Paste) && hasSelection_) {
        const int sx0 = std::min(selX0_, selX1_);
        const int sy0 = std::min(selY0_, selY1_);
        const int sx1 = std::max(selX0_, selX1_);
        const int sy1 = std::max(selY0_, selY1_);
        const ImVec2 sMin{
            origin.x + static_cast<float>(sx0 * tileSize_),
            origin.y + static_cast<float>(sy0 * tileSize_),
        };
        const ImVec2 sMax{
            origin.x + static_cast<float>((sx1 + 1) * tileSize_),
            origin.y + static_cast<float>((sy1 + 1) * tileSize_),
        };
        drawList->AddRect(sMin, sMax, IM_COL32(255, 216, 64, 220), 0.0f, 0, 2.0f);
        drawList->AddRectFilled(sMin, sMax, IM_COL32(255, 216, 64, 30));
    }

    const bool gridHovered = ImGui::IsItemHovered();
    if (wallShapeDragging_) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        wallShapeX1_ = std::clamp(static_cast<int>((mouse.x - origin.x) / static_cast<float>(tileSize_)), 0, width_ - 1);
        wallShapeY1_ = std::clamp(static_cast<int>((mouse.y - origin.y) / static_cast<float>(tileSize_)), 0, height_ - 1);
        drawWallShapePreview(drawList, origin);

        const ImGuiMouseButton button = wallShapeErase_ ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
        if (ImGui::IsMouseReleased(button)) {
            const uint16_t tileId = wallShapeErase_ ? 0u : selectedTileId_;
            if (editMode_ == EditMode::WallLine) {
                paintWallLine(wallShapeX0_, wallShapeY0_, wallShapeX1_, wallShapeY1_, tileId);
                status_ = std::string(wallShapeErase_ ? "Erased" : "Drew") + " wall line.";
            } else {
                paintWallRect(wallShapeX0_, wallShapeY0_, wallShapeX1_, wallShapeY1_, tileId);
                status_ = std::string(wallShapeErase_ ? "Erased" : "Drew") + " wall rectangle.";
            }
            wallShapeDragging_ = false;
            context.markDirty();
        }
    }

    if (!gridHovered) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int x = std::clamp(static_cast<int>((mouse.x - origin.x) / static_cast<float>(tileSize_)), 0, width_ - 1);
    const int y = std::clamp(static_cast<int>((mouse.y - origin.y) / static_cast<float>(tileSize_)), 0, height_ - 1);

    // Hover highlight
    const ImVec2 hMin{
        origin.x + static_cast<float>(x * tileSize_),
        origin.y + static_cast<float>(y * tileSize_),
    };
    const ImVec2 hMax{hMin.x + static_cast<float>(tileSize_), hMin.y + static_cast<float>(tileSize_)};
    drawList->AddRect(hMin, hMax, IM_COL32(255, 216, 64, 255), 0.0f, 0, 2.0f);

    // Tile tooltip
    const uint16_t hoverId = tileAt(x, y, activeLayer_);
    const game::TileDef* def = tilesetLoaded_ ? loadedTileset_.findTile(hoverId) : nullptr;
    if (def) {
        ImGui::SetTooltip("[%d,%d] %s  ID %d: %s (%s)", x, y, kLayerNames[activeLayer_],
            hoverId, def->name.c_str(), def->solid ? "solid" : "passable");
    } else {
        ImGui::SetTooltip("[%d,%d] %s  ID %d", x, y, kLayerNames[activeLayer_], static_cast<int>(hoverId));
    }

    if (obstacleMode_) {
        const int hovered = obstacleIndexAt(x, y);
        if (hovered >= 0) {
            const game::MapObstacle& o = obstacles_[static_cast<std::size_t>(hovered)];
            ImGui::SetTooltip("Select %s (%s)", o.id.c_str(), obstacleTypeName(o.type));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selectedObstacle_ = hovered;
                obstacleBufFor_ = -1;
            }
        } else {
            const auto type = static_cast<game::ObstacleType>(activeObstacleType_);
            const ImVec2 oMin{origin.x + static_cast<float>(x * tileSize_), origin.y + static_cast<float>(y * tileSize_)};
            const ImVec2 oMax{oMin.x + static_cast<float>(tileSize_), oMin.y + static_cast<float>(tileSize_)};
            drawList->AddRectFilled(oMin, oMax, obstacleColor(type, true));
            drawList->AddRect(oMin, oMax, IM_COL32(255, 255, 255, 230), 0.0f, 0, 2.0f);
            ImGui::SetTooltip("Add %s here", obstacleTypeName(type));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                recordUndo();
                placeObstacle(x, y);
                context.markDirty();
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            recordUndo();
            eraseObstacleAt(x, y);
            context.markDirty();
        }
        return;
    }

    if (playerPlacementMode_) {
        const ImVec2 playerGhostCenter{
            origin.x + static_cast<float>(x * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
            origin.y + static_cast<float>(y * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
        };
        const ImVec2 ghostMin{playerGhostCenter.x - playerDraw * 0.5f, playerGhostCenter.y - playerDraw * 0.5f};
        const ImVec2 ghostMax{ghostMin.x + playerDraw, ghostMin.y + playerDraw};
        drawList->AddRectFilled(ghostMin, ghostMax, IM_COL32(80, 180, 255, 70), 3.0f);
        drawList->AddRect(ghostMin, ghostMax, IM_COL32(255, 255, 255, 230), 3.0f, 0, 2.0f);
        ImGui::SetTooltip("Set player spawn [%d,%d]", x, y);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            spawnX_ = x;
            spawnY_ = y;
            context.markDirty();
            status_ = "Player spawn set to [" + std::to_string(x) + "," + std::to_string(y) + "].";
        }
        return;
    }

    if (editMode_ == EditMode::WallLine || editMode_ == EditMode::WallRect) {
        ImGui::SetTooltip("%s wall from [%d,%d]", editMode_ == EditMode::WallLine ? "Draw line" : "Draw rectangle", x, y);
        const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!wallShapeDragging_ && (leftClicked || rightClicked)) {
            recordUndo();
            wallShapeDragging_ = true;
            wallShapeErase_ = rightClicked;
            wallShapeX0_ = wallShapeX1_ = x;
            wallShapeY0_ = wallShapeY1_ = y;
        }
        return;
    }

    // Paste ghost preview
    if (editMode_ == EditMode::Paste && !clipboard_.empty()) {
        for (int py = 0; py < clipboardH_; ++py) {
            for (int px = 0; px < clipboardW_; ++px) {
                const int tx = x + px;
                const int ty = y + py;
                if (tx >= width_ || ty >= height_) {
                    continue;
                }
                const uint16_t id = clipboard_[static_cast<std::size_t>(py) * clipboardW_ + px];
                if (id != 0u) {
                    const ImVec2 gMin{
                        origin.x + static_cast<float>(tx * tileSize_),
                        origin.y + static_cast<float>(ty * tileSize_),
                    };
                    const ImVec2 gMax{gMin.x + static_cast<float>(tileSize_), gMin.y + static_cast<float>(tileSize_)};
                    drawList->AddRectFilled(gMin, gMax, dimColor(colorForTileId(id), 0.6f));
                    drawList->AddRect(gMin, gMax, IM_COL32(200, 200, 255, 180));
                }
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            recordUndo();
            for (int py = 0; py < clipboardH_; ++py) {
                for (int px = 0; px < clipboardW_; ++px) {
                    const int tx = x + px;
                    const int ty = y + py;
                    if (tx >= 0 && tx < width_ && ty >= 0 && ty < height_) {
                        tileAt(tx, ty, activeLayer_) = clipboard_[static_cast<std::size_t>(py) * clipboardW_ + px];
                        context.markDirty();
                    }
                }
            }
        }
        return;
    }

    // Selection rubber-band
    if (editMode_ == EditMode::Select) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selX0_ = x;
            selY0_ = y;
            selX1_ = x;
            selY1_ = y;
            selectionDragging_ = true;
            hasSelection_ = false;
        }
        if (selectionDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            selX1_ = x;
            selY1_ = y;
        }
        if (selectionDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selectionDragging_ = false;
            hasSelection_ = true;
        }
        return;
    }

    // Paint mode input
    if (ImGui::IsKeyDown(ImGuiKey_S) && !ImGui::GetIO().WantTextInput) {
        spawnX_ = x;
        spawnY_ = y;
        tileAt(x, y, activeLayer_) = 0u;
        context.markDirty();
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        recordUndo();
        tileAt(x, y, activeLayer_) = selectedTileId_;
        context.markDirty();
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        tileAt(x, y, activeLayer_) = selectedTileId_;
        context.markDirty();
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        recordUndo();
        tileAt(x, y, activeLayer_) = 0u;
        context.markDirty();
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        tileAt(x, y, activeLayer_) = 0u;
        context.markDirty();
    }
}

void MapEditorPanel::copySelection()
{
    const int sx0 = std::min(selX0_, selX1_);
    const int sy0 = std::min(selY0_, selY1_);
    const int sx1 = std::max(selX0_, selX1_);
    const int sy1 = std::max(selY0_, selY1_);
    clipboardW_ = sx1 - sx0 + 1;
    clipboardH_ = sy1 - sy0 + 1;
    clipboard_.resize(static_cast<std::size_t>(clipboardW_ * clipboardH_));
    for (int py = 0; py < clipboardH_; ++py) {
        for (int px = 0; px < clipboardW_; ++px) {
            clipboard_[static_cast<std::size_t>(py) * clipboardW_ + px] = tileAt(sx0 + px, sy0 + py, activeLayer_);
        }
    }
    status_ = "Copied " + std::to_string(clipboardW_) + "x" + std::to_string(clipboardH_) +
        " region from " + kLayerNames[activeLayer_] + " layer.";
}

void MapEditorPanel::paintWallLine(int x0, int y0, int x1, int y1, uint16_t tileId)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        tileAt(x0, y0, 1) = tileId;
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubledError = error * 2;
        if (doubledError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubledError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void MapEditorPanel::paintWallRect(int x0, int y0, int x1, int y1, uint16_t tileId)
{
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int top = std::min(y0, y1);
    const int bottom = std::max(y0, y1);
    for (int x = left; x <= right; ++x) {
        tileAt(x, top, 1) = tileId;
        tileAt(x, bottom, 1) = tileId;
    }
    for (int y = top; y <= bottom; ++y) {
        tileAt(left, y, 1) = tileId;
        tileAt(right, y, 1) = tileId;
    }
}

void MapEditorPanel::drawWallShapePreview(ImDrawList* drawList, ImVec2 origin) const
{
    const ImU32 fill = wallShapeErase_ ? IM_COL32(255, 70, 70, 90) : IM_COL32(255, 216, 64, 105);
    const ImU32 border = wallShapeErase_ ? IM_COL32(255, 70, 70, 235) : IM_COL32(255, 240, 150, 245);
    auto drawCell = [&](int x, int y) {
        const ImVec2 min{
            origin.x + static_cast<float>(x * tileSize_),
            origin.y + static_cast<float>(y * tileSize_),
        };
        const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};
        drawList->AddRectFilled(min, max, fill);
        drawList->AddRect(min, max, border, 0.0f, 0, 2.0f);
    };

    if (editMode_ == EditMode::WallLine) {
        int x0 = wallShapeX0_;
        int y0 = wallShapeY0_;
        const int dx = std::abs(wallShapeX1_ - x0);
        const int sx = x0 < wallShapeX1_ ? 1 : -1;
        const int dy = -std::abs(wallShapeY1_ - y0);
        const int sy = y0 < wallShapeY1_ ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            drawCell(x0, y0);
            if (x0 == wallShapeX1_ && y0 == wallShapeY1_) {
                break;
            }
            const int doubledError = error * 2;
            if (doubledError >= dy) {
                error += dy;
                x0 += sx;
            }
            if (doubledError <= dx) {
                error += dx;
                y0 += sy;
            }
        }
        return;
    }

    const int left = std::min(wallShapeX0_, wallShapeX1_);
    const int right = std::max(wallShapeX0_, wallShapeX1_);
    const int top = std::min(wallShapeY0_, wallShapeY1_);
    const int bottom = std::max(wallShapeY0_, wallShapeY1_);
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

void MapEditorPanel::drawObstacles(ImDrawList* drawList, ImVec2 origin) const
{
    for (int i = 0; i < static_cast<int>(obstacles_.size()); ++i) {
        const game::MapObstacle& obstacle = obstacles_[static_cast<std::size_t>(i)];
        const ImVec2 min{
            origin.x + static_cast<float>(obstacle.x * tileSize_),
            origin.y + static_cast<float>(obstacle.y * tileSize_),
        };
        const ImVec2 max{
            min.x + static_cast<float>(obstacle.width * tileSize_),
            min.y + static_cast<float>(obstacle.height * tileSize_),
        };
        const bool active = obstacle.type != game::ObstacleType::TimedSpike ||
            std::fmod(static_cast<float>(ImGui::GetTime()) + obstacle.phaseSeconds,
                std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds)) < obstacle.activeSeconds;
        drawList->AddRectFilled(min, max, obstacleColor(obstacle.type, active));
        if (i == selectedObstacle_) {
            drawList->AddRect(min, max, IM_COL32(255, 216, 64, 255), 0.0f, 0, 3.0f);
        } else {
            drawList->AddRect(min, max, active ? IM_COL32(255, 255, 255, 220) : IM_COL32(120, 180, 230, 190), 0.0f, 0, 2.0f);
        }
        const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
        const char* label = obstacle.type == game::ObstacleType::Pit ? "P" :
            (obstacle.type == game::ObstacleType::TimedSpike ? "T" : "S");
        drawList->AddText({center.x - 4.0f, center.y - 7.0f}, IM_COL32(255, 255, 255, 235), label);
    }
}

int MapEditorPanel::obstacleIndexAt(int x, int y) const
{
    // Topmost (last-drawn) obstacle covering the tile.
    for (int i = static_cast<int>(obstacles_.size()) - 1; i >= 0; --i) {
        const game::MapObstacle& o = obstacles_[static_cast<std::size_t>(i)];
        if (x >= o.x && y >= o.y && x < o.x + o.width && y < o.y + o.height) {
            return i;
        }
    }
    return -1;
}

std::string MapEditorPanel::makeUniqueObstacleId() const
{
    for (int n = 1;; ++n) {
        const std::string candidate = "obstacle_" + std::to_string(n);
        const bool used = std::any_of(obstacles_.begin(), obstacles_.end(),
            [&candidate](const game::MapObstacle& o) { return o.id == candidate; });
        if (!used) {
            return candidate;
        }
    }
}

void MapEditorPanel::placeObstacle(int x, int y)
{
    const auto type = static_cast<game::ObstacleType>(activeObstacleType_);
    const char* defaultSprite = type == game::ObstacleType::Pit ? "pit" :
        (type == game::ObstacleType::TimedSpike ? "timed_spikes" : "spikes");

    game::MapObstacle obstacle;
    obstacle.id = makeUniqueObstacleId();
    obstacle.type = type;
    obstacle.spriteId = defaultSprite;
    obstacle.x = std::clamp(x, 0, width_ - 1);
    obstacle.y = std::clamp(y, 0, height_ - 1);
    obstacle.width = 1;
    obstacle.height = 1;
    obstacle.activeSeconds = 1.0f;
    obstacle.inactiveSeconds = 1.0f;
    obstacle.phaseSeconds = 0.0f;
    obstacles_.push_back(obstacle);
    selectedObstacle_ = static_cast<int>(obstacles_.size()) - 1;
    obstacleBufFor_ = -1;
    status_ = "Added " + std::string(obstacleTypeName(obstacle.type)) + " (" + obstacle.id + ").";
}

void MapEditorPanel::eraseObstacleAt(int x, int y)
{
    const int idx = obstacleIndexAt(x, y);
    if (idx < 0) {
        return;
    }
    status_ = "Removed obstacle " + obstacles_[static_cast<std::size_t>(idx)].id + ".";
    obstacles_.erase(obstacles_.begin() + idx);
    if (selectedObstacle_ == idx) {
        selectedObstacle_ = -1;
    } else if (selectedObstacle_ > idx) {
        --selectedObstacle_;
    }
    obstacleBufFor_ = -1;
}

void MapEditorPanel::drawObstacleInspector(EditorContext& context)
{
    if (selectedObstacle_ >= static_cast<int>(obstacles_.size())) {
        selectedObstacle_ = -1;
    }

    ImGui::Separator();
    ImGui::Text("Obstacles (%d)", static_cast<int>(obstacles_.size()));
    ImGui::SameLine();
    if (ImGui::Button("Add obstacle")) {
        recordUndo();
        placeObstacle(spawnX_, spawnY_);
        context.markDirty();
    }

    ImGui::BeginChild("ObstacleList", ImVec2(240.0f, 150.0f), true);
    for (int i = 0; i < static_cast<int>(obstacles_.size()); ++i) {
        const game::MapObstacle& o = obstacles_[static_cast<std::size_t>(i)];
        const std::string label = o.id + "  (" + obstacleTypeName(o.type) + ")##obs" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), i == selectedObstacle_)) {
            selectedObstacle_ = i;
            obstacleBufFor_ = -1;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();
    if (selectedObstacle_ < 0) {
        ImGui::TextDisabled("Select an obstacle to edit, or click an empty cell to add one.");
        ImGui::EndGroup();
        return;
    }

    game::MapObstacle& o = obstacles_[static_cast<std::size_t>(selectedObstacle_)];
    if (obstacleBufFor_ != selectedObstacle_) {
        std::memset(obstacleIdEdit_.data(), 0, obstacleIdEdit_.size());
        std::memcpy(obstacleIdEdit_.data(), o.id.data(), std::min(o.id.size(), obstacleIdEdit_.size() - 1));
        std::memset(obstacleSpriteId_.data(), 0, obstacleSpriteId_.size());
        std::memcpy(obstacleSpriteId_.data(), o.spriteId.data(), std::min(o.spriteId.size(), obstacleSpriteId_.size() - 1));
        obstacleBufFor_ = selectedObstacle_;
    }

    ImGui::SetNextItemWidth(180.0f);
    if (ui::inputTextString("Id", obstacleIdEdit_.data(), obstacleIdEdit_.size())) {
        o.id = obstacleIdEdit_.data();
        context.markDirty();
    }
    const bool duplicateId = std::count_if(obstacles_.begin(), obstacles_.end(),
        [&o](const game::MapObstacle& other) { return other.id == o.id; }) > 1;
    if (o.id.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.3f, 1.0f), "Id must not be empty.");
    } else if (duplicateId) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.3f, 1.0f), "Duplicate id.");
    }

    int typeValue = static_cast<int>(o.type);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("Type", &typeValue, kObstacleTypeNames, 3)) {
        o.type = static_cast<game::ObstacleType>(typeValue);
        context.markDirty();
    }

    ImGui::SetNextItemWidth(180.0f);
    if (ui::inputTextString("Sprite id", obstacleSpriteId_.data(), obstacleSpriteId_.size())) {
        o.spriteId = obstacleSpriteId_.data();
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit Sprite")) {
        const std::string spriteId(obstacleSpriteId_.data());
        if (!spriteId.empty()) {
            context.requestedSpriteReference = (context.assets.gameSprites / (spriteId + ".sprite.json")).generic_string();
            context.requestEditSprite = true;
        }
    }

    int pos[2]{o.x, o.y};
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt2("Tile pos", pos)) {
        o.x = std::clamp(pos[0], 0, width_ - 1);
        o.y = std::clamp(pos[1], 0, height_ - 1);
        context.markDirty();
    }
    int sz[2]{o.width, o.height};
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt2("Size (tiles)", sz)) {
        o.width = std::clamp(sz[0], 1, width_ - o.x);
        o.height = std::clamp(sz[1], 1, height_ - o.y);
        context.markDirty();
    }

    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Damage", &o.damage)) {
        o.damage = std::clamp(o.damage, 0, 999);
        context.markDirty();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputFloat("Damage rate s", &o.damageIntervalSeconds, 0.05f, 0.25f, "%.2f")) {
        o.damageIntervalSeconds = std::clamp(o.damageIntervalSeconds, 0.1f, 60.0f);
        context.markDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(0 damage = harmless)");

    if (o.type == game::ObstacleType::TimedSpike) {
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat("Active s", &o.activeSeconds, 0.1f, 0.5f, "%.2f")) {
            o.activeSeconds = std::clamp(o.activeSeconds, 0.05f, 60.0f);
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat("Safe s", &o.inactiveSeconds, 0.1f, 0.5f, "%.2f")) {
            o.inactiveSeconds = std::clamp(o.inactiveSeconds, 0.0f, 60.0f);
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat("Phase s", &o.phaseSeconds, 0.1f, 0.5f, "%.2f")) {
            o.phaseSeconds = std::clamp(o.phaseSeconds, 0.0f, 60.0f);
            context.markDirty();
        }
    } else {
        ImGui::TextDisabled("Timing applies to Timed Spikes only.");
    }

    if (ImGui::Button("Delete obstacle")) {
        recordUndo();
        obstacles_.erase(obstacles_.begin() + selectedObstacle_);
        selectedObstacle_ = -1;
        obstacleBufFor_ = -1;
        context.markDirty();
        status_ = "Deleted obstacle.";
    }
    ImGui::EndGroup();
}

void MapEditorPanel::drawWallOutlines(ImDrawList* drawList, ImVec2 origin) const
{
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const uint16_t id = tileAt(x, y, 1);
            if (id == 0u) {
                continue;
            }

            const ImVec2 min{
                origin.x + static_cast<float>(x * tileSize_),
                origin.y + static_cast<float>(y * tileSize_),
            };
            const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};
            drawList->AddRect(min, max, IM_COL32(255, 226, 96, 235), 0.0f, 0, 2.0f);

            if (x == 0 || tileAt(x - 1, y, 1) == 0u) {
                drawList->AddLine(min, {min.x, max.y}, IM_COL32(10, 10, 10, 240), 2.0f);
            }
            if (x + 1 >= width_ || tileAt(x + 1, y, 1) == 0u) {
                drawList->AddLine({max.x, min.y}, max, IM_COL32(10, 10, 10, 240), 2.0f);
            }
            if (y == 0 || tileAt(x, y - 1, 1) == 0u) {
                drawList->AddLine(min, {max.x, min.y}, IM_COL32(10, 10, 10, 240), 2.0f);
            }
            if (y + 1 >= height_ || tileAt(x, y + 1, 1) == 0u) {
                drawList->AddLine({min.x, max.y}, max, IM_COL32(10, 10, 10, 240), 2.0f);
            }
        }
    }
}

void MapEditorPanel::drawTestGame()
{
    updateTestPlayer();

    const float scale = static_cast<float>(tileSize_) / static_cast<float>(kWorldTileSize);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 viewSize{
        static_cast<float>(width_ * kWorldTileSize) * scale,
        static_cast<float>(height_ * kWorldTileSize) * scale,
    };
    ImGui::InvisibleButton("MapTestGame", viewSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewSize.x, origin.y + viewSize.y}, IM_COL32(0, 0, 0, 255));

    // Draw floor layer (dim)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const uint16_t id = tileAt(x, y, 0);
            if (id != 0u) {
                const ImVec2 min{
                    origin.x + static_cast<float>(x * kWorldTileSize) * scale,
                    origin.y + static_cast<float>(y * kWorldTileSize) * scale,
                };
                const ImVec2 max{
                    min.x + static_cast<float>(kWorldTileSize) * scale,
                    min.y + static_cast<float>(kWorldTileSize) * scale,
                };
                drawList->AddRectFilled(min, max, dimColor(colorForTileId(id), 0.5f));
            }
        }
    }

    // Draw mid layer (collision layer)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const uint16_t id = tileAt(x, y, 1);
            if (isSolid(id)) {
                const ImVec2 min{
                    origin.x + static_cast<float>(x * kWorldTileSize) * scale,
                    origin.y + static_cast<float>(y * kWorldTileSize) * scale,
                };
                const ImVec2 max{
                    min.x + static_cast<float>(kWorldTileSize) * scale,
                    min.y + static_cast<float>(kWorldTileSize) * scale,
                };
                drawList->AddRectFilled(min, max, colorForTileId(id));
            }
            const ImVec2 min{
                origin.x + static_cast<float>(x * kWorldTileSize) * scale,
                origin.y + static_cast<float>(y * kWorldTileSize) * scale,
            };
            const ImVec2 max{
                min.x + static_cast<float>(kWorldTileSize) * scale,
                min.y + static_cast<float>(kWorldTileSize) * scale,
            };
            drawList->AddRect(min, max, IM_COL32(55, 55, 55, 255));
        }
    }

    const float now = static_cast<float>(ImGui::GetTime());
    for (const game::MapObstacle& obstacle : obstacles_) {
        const bool active = obstacle.type != game::ObstacleType::TimedSpike ||
            std::fmod(now + obstacle.phaseSeconds, std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds)) < obstacle.activeSeconds;
        const ImVec2 min{
            origin.x + static_cast<float>(obstacle.x * kWorldTileSize) * scale,
            origin.y + static_cast<float>(obstacle.y * kWorldTileSize) * scale,
        };
        const ImVec2 max{
            min.x + static_cast<float>(obstacle.width * kWorldTileSize) * scale,
            min.y + static_cast<float>(obstacle.height * kWorldTileSize) * scale,
        };
        drawList->AddRectFilled(min, max, obstacleColor(obstacle.type, active));
        drawList->AddRect(min, max, IM_COL32(255, 255, 255, 190), 0.0f, 0, 2.0f);
    }

    // Player
    const ImVec2 playerMin{origin.x + testPlayerX_ * scale, origin.y + testPlayerY_ * scale};
    const ImVec2 playerMax{playerMin.x + kPlayerSize * scale, playerMin.y + kPlayerSize * scale};
    drawList->AddRectFilled(playerMin, playerMax, IM_COL32(55, 155, 255, 255), 3.0f);
    drawList->AddRect(playerMin, playerMax, IM_COL32(5, 25, 55, 255), 3.0f, 0, 2.0f);

    // Draw ceiling layer on top (blue tint, semi-transparent look)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const uint16_t id = tileAt(x, y, 2);
            if (id != 0u) {
                const ImVec2 min{
                    origin.x + static_cast<float>(x * kWorldTileSize) * scale,
                    origin.y + static_cast<float>(y * kWorldTileSize) * scale,
                };
                const ImVec2 max{
                    min.x + static_cast<float>(kWorldTileSize) * scale,
                    min.y + static_cast<float>(kWorldTileSize) * scale,
                };
                drawList->AddRectFilled(min, max, ceilingColor(colorForTileId(id)));
            }
        }
    }
}

void MapEditorPanel::startTestGame()
{
    spawnX_ = std::clamp(spawnX_, 0, width_ - 1);
    spawnY_ = std::clamp(spawnY_, 0, height_ - 1);
    testPlayerX_ = static_cast<float>(spawnX_ * kWorldTileSize);
    testPlayerY_ = static_cast<float>(spawnY_ * kWorldTileSize);
    testMode_ = true;
    status_ = "Testing map. Arrow keys to move; mid-layer solid tiles block the player.";
}

void MapEditorPanel::updateTestPlayer()
{
    if (ImGui::GetIO().WantTextInput) {
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
        dx -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
        dx += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
        dy -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
        dy += 1.0f;
    }

    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    const float len = std::sqrt(dx * dx + dy * dy);
    dx /= len;
    dy /= len;

    const float step = kPlayerSpeed * ImGui::GetIO().DeltaTime;
    const float nextX = testPlayerX_ + dx * step;
    if (playerCanMoveTo(nextX, testPlayerY_)) {
        testPlayerX_ = nextX;
    }
    const float nextY = testPlayerY_ + dy * step;
    if (playerCanMoveTo(testPlayerX_, nextY)) {
        testPlayerY_ = nextY;
    }

    const float centerX = testPlayerX_ + kPlayerSize * 0.5f;
    const float centerY = testPlayerY_ + kPlayerSize * 0.5f;
    const float now = static_cast<float>(ImGui::GetTime());
    for (const game::MapObstacle& obstacle : obstacles_) {
        if (obstacle.damage <= 0) {
            continue;
        }
        const float minX = static_cast<float>(obstacle.x * kWorldTileSize);
        const float minY = static_cast<float>(obstacle.y * kWorldTileSize);
        const float maxX = static_cast<float>((obstacle.x + obstacle.width) * kWorldTileSize);
        const float maxY = static_cast<float>((obstacle.y + obstacle.height) * kWorldTileSize);
        if (centerX < minX || centerY < minY || centerX >= maxX || centerY >= maxY) {
            continue;
        }
        const bool active = obstacle.type != game::ObstacleType::TimedSpike ||
            std::fmod(now + obstacle.phaseSeconds, std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds)) < obstacle.activeSeconds;
        if (active) {
            testPlayerX_ = static_cast<float>(spawnX_ * kWorldTileSize);
            testPlayerY_ = static_cast<float>(spawnY_ * kWorldTileSize);
            status_ = "Obstacle hit: respawned at map spawn.";
            break;
        }
    }
}

bool MapEditorPanel::isSolid(uint16_t tileId) const
{
    if (tilesetLoaded_) {
        const game::TileDef* def = loadedTileset_.findTile(static_cast<int>(tileId));
        if (def) {
            return def->solid;
        }
    }
    return tileId != 0u;
}

bool MapEditorPanel::playerCanMoveTo(float x, float y) const
{
    return !solidAtPixel(x, y) &&
        !solidAtPixel(x + kPlayerSize - 1.0f, y) &&
        !solidAtPixel(x, y + kPlayerSize - 1.0f) &&
        !solidAtPixel(x + kPlayerSize - 1.0f, y + kPlayerSize - 1.0f);
}

bool MapEditorPanel::solidAtPixel(float x, float y) const
{
    if (x < 0.0f || y < 0.0f) {
        return true;
    }
    const int tileX = static_cast<int>(x) / kWorldTileSize;
    const int tileY = static_cast<int>(y) / kWorldTileSize;
    if (tileX < 0 || tileY < 0 || tileX >= width_ || tileY >= height_) {
        return true;
    }
    // Collision is only checked on the mid layer (player-level)
    return isSolid(tileAt(tileX, tileY, 1));
}

void MapEditorPanel::saveMap(EditorContext& context)
{
    game::TileMap map;
    map.id = mapId_.data();

    // The .admap also stores item and door placements owned by other editors.
    // Load the existing file first so saving map-logic data does not wipe them.
    const std::filesystem::path outputPath = context.assets.gameMapPath() / (map.id + ".admap");
    game::TileMap existing;
    if (game::loadTileMap(outputPath, existing, nullptr)) {
        map.items = std::move(existing.items);
        map.doors = std::move(existing.doors);
        map.chapterExits = std::move(existing.chapterExits);
    }

    map.tilesetId = tilesetId_.data();
    map.width = width_;
    map.height = height_;
    map.spawnX = spawnX_;
    map.spawnY = spawnY_;
    map.layers = layers_;
    map.obstacles = obstacles_;

    std::string error;
    if (game::saveTileMap(outputPath, map, &error)) {
        status_ = "Saved map: " + outputPath.generic_string();
    } else {
        status_ = "Failed to save map: " + error;
    }
}

void MapEditorPanel::loadMap(EditorContext& context)
{
    game::TileMap map;
    std::string error;
    const std::filesystem::path inputPath =
        context.assets.gameMapPath() / (std::string(mapId_.data()) + ".admap");
    if (!game::loadTileMap(inputPath, map, &error)) {
        status_ = "Failed to load map: " + error;
        return;
    }

    width_ = map.width;
    height_ = map.height;
    spawnX_ = map.spawnX;
    spawnY_ = map.spawnY;
    layers_ = std::move(map.layers);
    obstacles_ = std::move(map.obstacles);
    selectedObstacle_ = -1;
    obstacleBufFor_ = -1;

    const std::size_t idLen = std::min(map.id.size(), mapId_.size() - 1);
    std::memset(mapId_.data(), 0, mapId_.size());
    std::memcpy(mapId_.data(), map.id.data(), idLen);

    const std::size_t tsLen = std::min(map.tilesetId.size(), tilesetId_.size() - 1);
    std::memset(tilesetId_.data(), 0, tilesetId_.size());
    std::memcpy(tilesetId_.data(), map.tilesetId.data(), tsLen);

    if (!map.tilesetId.empty()) {
        loadTileset(context);
    }

    status_ = "Loaded map: " + inputPath.generic_string();
}

void MapEditorPanel::loadTileset(EditorContext& context)
{
    const std::string id(tilesetId_.data());
    if (id.empty()) {
        status_ = "No tileset id set.";
        return;
    }

    const std::filesystem::path inputPath =
        context.assets.gameTilesetPath() / (id + ".tileset.json");

    std::string error;
    game::TilesetDef ts;
    if (!game::loadTileset(inputPath, ts, &error)) {
        status_ = "Failed to load tileset: " + error;
        tilesetLoaded_ = false;
        return;
    }

    loadedTileset_ = std::move(ts);
    tilesetLoaded_ = true;
    status_ = "Loaded tileset: " + inputPath.generic_string();
}

uint16_t& MapEditorPanel::tileAt(int x, int y, int layer)
{
    return layers_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(y) * width_ + x];
}

const uint16_t& MapEditorPanel::tileAt(int x, int y, int layer) const
{
    return layers_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(y) * width_ + x];
}

void MapEditorPanel::recordUndo()
{
    undoStack_.push_back({layers_, obstacles_});
    if (undoStack_.size() > static_cast<std::size_t>(kMaxUndoSteps)) {
        undoStack_.erase(undoStack_.begin());
    }
}

void MapEditorPanel::undoMap()
{
    if (undoStack_.empty()) {
        status_ = "Nothing to undo.";
        return;
    }
    layers_ = std::move(undoStack_.back().layers);
    obstacles_ = std::move(undoStack_.back().obstacles);
    selectedObstacle_ = -1;
    obstacleBufFor_ = -1;
    undoStack_.pop_back();
    status_ = "Undone. (" + std::to_string(undoStack_.size()) + " steps left)";
}

} // namespace adventure::editor
