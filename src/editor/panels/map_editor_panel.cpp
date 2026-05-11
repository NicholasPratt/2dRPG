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

} // namespace

void MapEditorPanel::draw(EditorContext& context)
{
    drawToolbar(context);
    ImGui::Separator();
    if (testMode_) {
        drawTestGame();
    } else {
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

    std::vector<unsigned char> resized(static_cast<std::size_t>(width * height), 0u);
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
    walls_ = std::move(resized);
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

    if (ImGui::RadioButton("Wall", editMode_ == 0)) {
        editMode_ = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("No wall", editMode_ == 1)) {
        editMode_ = 1;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Spawn", editMode_ == 2)) {
        editMode_ = 2;
    }

    if (ImGui::Button("Fill walls")) {
        std::fill(walls_.begin(), walls_.end(), 1u);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear walls")) {
        std::fill(walls_.begin(), walls_.end(), 0u);
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
    ImGui::Text("Spawn: %d, %d", spawnX_, spawnY_);
    ImGui::Text("Map files: %s", context.assets.gameMapPath().string().c_str());
    ImGui::TextDisabled("White = wall. Black = no wall. Spawn sets the player start. Test mode uses cursor keys and blocks walls.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
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
            if (tileAt(x, y) != 0u) {
                drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 255));
            }
            drawList->AddRect(min, max, IM_COL32(65, 65, 65, 255));
        }
    }

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

    const ImVec2 min{
        origin.x + static_cast<float>(x * tileSize_),
        origin.y + static_cast<float>(y * tileSize_),
    };
    const ImVec2 max{min.x + static_cast<float>(tileSize_), min.y + static_cast<float>(tileSize_)};
    drawList->AddRect(min, max, IM_COL32(255, 216, 64, 255), 0.0f, 0, 2.0f);

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (editMode_ == 2) {
            spawnX_ = x;
            spawnY_ = y;
            tileAt(x, y) = 0u;
        } else {
            tileAt(x, y) = editMode_ == 0 ? 1u : 0u;
        }
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
            if (tileAt(x, y) != 0u) {
                drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 255));
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
    tileAt(spawnX_, spawnY_) = 0u;
    playerX_ = static_cast<float>(spawnX_ * kWorldTileSize);
    playerY_ = static_cast<float>(spawnY_ * kWorldTileSize);
    testMode_ = true;
    status_ = "Testing map. Use cursor keys to move; walls block the player.";
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

bool MapEditorPanel::playerCanMoveTo(float x, float y) const
{
    return !wallAtPixel(x, y) &&
        !wallAtPixel(x + kPlayerSize - 1.0f, y) &&
        !wallAtPixel(x, y + kPlayerSize - 1.0f) &&
        !wallAtPixel(x + kPlayerSize - 1.0f, y + kPlayerSize - 1.0f);
}

bool MapEditorPanel::wallAtPixel(float x, float y) const
{
    if (x < 0.0f || y < 0.0f) {
        return true;
    }

    const int tileX = static_cast<int>(x) / kWorldTileSize;
    const int tileY = static_cast<int>(y) / kWorldTileSize;
    if (tileX < 0 || tileY < 0 || tileX >= width_ || tileY >= height_) {
        return true;
    }

    return tileAt(tileX, tileY) != 0u;
}

void MapEditorPanel::saveMap(EditorContext& context)
{
    adventure::game::TileMap map;
    map.id = mapId_.data();
    map.width = width_;
    map.height = height_;
    map.spawnX = spawnX_;
    map.spawnY = spawnY_;
    map.walls = walls_;

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
    walls_ = std::move(map.walls);
    const std::size_t length = std::min(map.id.size(), mapId_.size() - 1);
    std::memset(mapId_.data(), 0, mapId_.size());
    std::memcpy(mapId_.data(), map.id.data(), length);
    status_ = "Loaded map: " + inputPath.generic_string();
}

unsigned char& MapEditorPanel::tileAt(int x, int y)
{
    return walls_[static_cast<std::size_t>(y) * width_ + x];
}

const unsigned char& MapEditorPanel::tileAt(int x, int y) const
{
    return walls_[static_cast<std::size_t>(y) * width_ + x];
}

} // namespace adventure::editor
