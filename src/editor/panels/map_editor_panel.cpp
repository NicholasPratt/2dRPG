#include "editor/panels/map_editor_panel.hpp"

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
constexpr int kWorldTileSize = 16;
constexpr float kPlayerSize = 32.0f;
constexpr float kPlayerSpeed = 96.0f;

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

} // namespace

void MapEditorPanel::draw(EditorContext& context)
{
    drawToolbar(context);
    ImGui::Separator();
    if (testMode_) {
        drawTestGame();
    } else {
        if (tilesetLoaded_) {
            drawTilesetPalette();
            ImGui::Separator();
        }
        drawGrid();
    }
}

void MapEditorPanel::resizeMap(int width, int height)
{
    width = std::clamp(width, kMinMapSize, kMaxMapSize);
    height = std::clamp(height, kMinMapSize, kMaxMapSize);
    if (width == width_ && height == height_) {
        return;
    }

    std::vector<uint16_t> resized(static_cast<std::size_t>(width * height), 0u);
    const int copyWidth = std::min(width_, width);
    const int copyHeight = std::min(height_, height);
    for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
            resized[static_cast<std::size_t>(y) * width + x] = tileAt(x, y);
        }
    }

    width_ = width;
    height_ = height;
    spawnX_ = std::clamp(spawnX_, 0, width_ - 1);
    spawnY_ = std::clamp(spawnY_, 0, height_ - 1);
    tiles_ = std::move(resized);
}

void MapEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Map id", mapId_.data(), mapId_.size());

    int size[2]{width_, height_};
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputInt2("Map size", size)) {
        resizeMap(size[0], size[1]);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderInt("Tile px", &tileSize_, kMinTileSize, kMaxTileSize)) {
        tileSize_ = std::clamp(tileSize_, kMinTileSize, kMaxTileSize);
    }

    // Tileset selector
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Tileset id", tilesetId_.data(), tilesetId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Load tileset")) {
        loadTileset(context);
    }
    if (tilesetLoaded_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "loaded (%d tiles)", static_cast<int>(loadedTileset_.tiles.size()));
    }

    if (ImGui::Button("Fill")) {
        std::fill(tiles_.begin(), tiles_.end(), selectedTileId_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear (fill 0)")) {
        std::fill(tiles_.begin(), tiles_.end(), static_cast<uint16_t>(0u));
    }

    if (ImGui::Button("Save .admap")) {
        saveMap(context);
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

    ImGui::Text("Spawn: %d, %d   Active tile ID: %d", spawnX_, spawnY_, static_cast<int>(selectedTileId_));
    ImGui::Text("Map files: %s", context.assets.gameMapPath().string().c_str());
    ImGui::TextDisabled("Left-click paints selected tile. Right-click clears to 0. Spawn mode places player start.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void MapEditorPanel::drawTilesetPalette()
{
    constexpr float kPaletteSize = 24.0f;
    constexpr int kPaletteCols = 16;

    ImGui::Text("Tileset: %s — click a tile to select it for painting", loadedTileset_.id.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    int count = static_cast<int>(loadedTileset_.tiles.size());
    int paletteRows = (count + kPaletteCols - 1) / kPaletteCols;
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

        // Highlight selected tile
        if (tileId == selectedTileId_) {
            dl->AddRect(min, max, IM_COL32(255, 216, 64, 255), 0.0f, 0, 2.0f);
        } else {
            dl->AddRect(min, max, IM_COL32(0, 0, 0, 120));
        }

        // Solid indicator: small dot in corner
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

void MapEditorPanel::drawGrid()
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridSize{
        static_cast<float>(width_ * tileSize_),
        static_cast<float>(height_ * tileSize_),
    };
    ImGui::InvisibleButton("MapGrid", gridSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + gridSize.x, origin.y + gridSize.y}, IM_COL32(0, 0, 0, 255));

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const ImVec2 min{
                origin.x + static_cast<float>(x * tileSize_),
                origin.y + static_cast<float>(y * tileSize_),
            };
            const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};

            const uint16_t id = tileAt(x, y);
            if (id != 0u) {
                drawList->AddRectFilled(min, max, colorForTileId(id));
            }
            drawList->AddRect(min, max, IM_COL32(50, 50, 50, 180));
        }
    }

    // Spawn indicator
    const ImVec2 spawnCenter{
        origin.x + static_cast<float>(spawnX_ * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
        origin.y + static_cast<float>(spawnY_ * tileSize_) + static_cast<float>(tileSize_) * 0.5f,
    };
    drawList->AddCircleFilled(spawnCenter, std::max(4.0f, static_cast<float>(tileSize_) * 0.25f), IM_COL32(80, 180, 255, 255));
    drawList->AddCircle(spawnCenter, std::max(5.0f, static_cast<float>(tileSize_) * 0.32f), IM_COL32(5, 25, 45, 255), 0, 2.0f);

    if (!ImGui::IsItemHovered()) {
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

    // Tooltip showing tile info
    const uint16_t hoverId = tileAt(x, y);
    const game::TileDef* def = tilesetLoaded_ ? loadedTileset_.findTile(hoverId) : nullptr;
    if (def) {
        ImGui::SetTooltip("[%d,%d] ID %d: %s (%s)", x, y, hoverId, def->name.c_str(), def->solid ? "solid" : "passable");
    } else {
        ImGui::SetTooltip("[%d,%d] ID %d", x, y, static_cast<int>(hoverId));
    }

    // Spawn mode: right-click controls (radio button now hidden in favour of Spawn key)
    if (ImGui::IsKeyDown(ImGuiKey_S) && !ImGui::GetIO().WantTextInput) {
        spawnX_ = x;
        spawnY_ = y;
        tileAt(x, y) = 0u;
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        tileAt(x, y) = selectedTileId_;
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        tileAt(x, y) = 0u;
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

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const ImVec2 min{
                origin.x + static_cast<float>(x * kWorldTileSize) * scale,
                origin.y + static_cast<float>(y * kWorldTileSize) * scale,
            };
            const ImVec2 max{
                min.x + static_cast<float>(kWorldTileSize) * scale,
                min.y + static_cast<float>(kWorldTileSize) * scale,
            };
            const uint16_t id = tileAt(x, y);
            if (isSolid(id)) {
                drawList->AddRectFilled(min, max, colorForTileId(id));
            }
            drawList->AddRect(min, max, IM_COL32(55, 55, 55, 255));
        }
    }

    const ImVec2 playerMin{origin.x + playerX_ * scale, origin.y + playerY_ * scale};
    const ImVec2 playerMax{playerMin.x + kPlayerSize * scale, playerMin.y + kPlayerSize * scale};
    drawList->AddRectFilled(playerMin, playerMax, IM_COL32(55, 155, 255, 255), 3.0f);
    drawList->AddRect(playerMin, playerMax, IM_COL32(5, 25, 55, 255), 3.0f, 0, 2.0f);
}

void MapEditorPanel::startTestGame()
{
    spawnX_ = std::clamp(spawnX_, 0, width_ - 1);
    spawnY_ = std::clamp(spawnY_, 0, height_ - 1);
    playerX_ = static_cast<float>(spawnX_ * kWorldTileSize);
    playerY_ = static_cast<float>(spawnY_ * kWorldTileSize);
    testMode_ = true;
    status_ = "Testing map. Use cursor keys to move; solid tiles block the player.";
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

    const float length = std::sqrt(dx * dx + dy * dy);
    dx /= length;
    dy /= length;

    const float step = kPlayerSpeed * ImGui::GetIO().DeltaTime;
    const float nextX = playerX_ + dx * step;
    if (playerCanMoveTo(nextX, playerY_)) {
        playerX_ = nextX;
    }

    const float nextY = playerY_ + dy * step;
    if (playerCanMoveTo(playerX_, nextY)) {
        playerY_ = nextY;
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

    return isSolid(tileAt(tileX, tileY));
}

void MapEditorPanel::saveMap(EditorContext& context)
{
    adventure::game::TileMap map;
    map.id = mapId_.data();
    map.tilesetId = tilesetId_.data();
    map.width = width_;
    map.height = height_;
    map.spawnX = spawnX_;
    map.spawnY = spawnY_;
    map.tiles = tiles_;

    std::string error;
    const std::filesystem::path outputPath = context.assets.gameMapPath() / (map.id + ".admap");
    if (adventure::game::saveTileMap(outputPath, map, &error)) {
        status_ = "Saved map: " + outputPath.generic_string();
    } else {
        status_ = "Failed to save map: " + error;
    }
}

void MapEditorPanel::loadMap(EditorContext& context)
{
    adventure::game::TileMap map;
    std::string error;
    const std::filesystem::path inputPath = context.assets.gameMapPath() / (std::string(mapId_.data()) + ".admap");
    if (!adventure::game::loadTileMap(inputPath, map, &error)) {
        status_ = "Failed to load map: " + error;
        return;
    }

    width_ = map.width;
    height_ = map.height;
    spawnX_ = map.spawnX;
    spawnY_ = map.spawnY;
    tiles_ = std::move(map.tiles);

    const std::size_t idLen = std::min(map.id.size(), mapId_.size() - 1);
    std::memset(mapId_.data(), 0, mapId_.size());
    std::memcpy(mapId_.data(), map.id.data(), idLen);

    const std::size_t tsLen = std::min(map.tilesetId.size(), tilesetId_.size() - 1);
    std::memset(tilesetId_.data(), 0, tilesetId_.size());
    std::memcpy(tilesetId_.data(), map.tilesetId.data(), tsLen);

    // Auto-load the referenced tileset if the map has one
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

uint16_t& MapEditorPanel::tileAt(int x, int y)
{
    return tiles_[static_cast<std::size_t>(y) * width_ + x];
}

const uint16_t& MapEditorPanel::tileAt(int x, int y) const
{
    return tiles_[static_cast<std::size_t>(y) * width_ + x];
}

} // namespace adventure::editor
