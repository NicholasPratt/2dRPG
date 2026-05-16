#include "editor/panels/enemy_path_editor_panel.hpp"

#include "game/constants.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace adventure::editor {
namespace {

constexpr float kWorldTileSize = static_cast<float>(game::kTileSize);
constexpr int kDefaultWorldTilesW = game::kScreenTilesW;
constexpr int kDefaultWorldTilesH = game::kScreenTilesH;
constexpr float kWaypointRadius = 6.0f;
constexpr float kHitRadius = 12.0f; // canvas pixels for waypoint hit detection

const char* kBehaviorNames[] = {"Idle", "Patrol", "Aggro"};

// Deterministic color for a tile id (matches the map editor)
ImU32 bgColorForTileId(uint16_t id)
{
    if (id == 0) {
        return 0u;
    }
    uint32_t h = static_cast<uint32_t>(id) * 2654435761u;
    auto r = static_cast<uint8_t>(40 + (h & 0xFFu) * 80u / 255u);
    auto g = static_cast<uint8_t>(40 + ((h >> 8u) & 0xFFu) * 80u / 255u);
    auto b = static_cast<uint8_t>(40 + ((h >> 16u) & 0xFFu) * 80u / 255u);
    return IM_COL32(r, g, b, 255);
}

void setError(std::string* out, const std::string& msg)
{
    if (out) {
        *out = msg;
    }
}

bool savePathFile(const std::filesystem::path& path,
    const std::string& id, const std::string& mapId,
    int behavior, float speed, bool loop, bool respawn,
    const std::vector<std::pair<float, float>>& waypoints,
    std::string* errorMessage)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open path file for writing.");
        return false;
    }

    output << "ADPATH 1\n";
    output << "id " << id << "\n";
    output << "map " << mapId << "\n";
    output << "behavior " << behavior << "\n";
    output << "speed " << speed << "\n";
    output << "loop " << (loop ? 1 : 0) << "\n";
    output << "respawn " << (respawn ? 1 : 0) << "\n";
    output << "waypoints " << waypoints.size() << "\n";
    for (const auto& [x, y] : waypoints) {
        output << "wp " << x << " " << y << "\n";
    }
    output << "end\n";

    if (!output) {
        setError(errorMessage, "Failed while writing path file.");
        return false;
    }
    return true;
}

bool loadPathFile(const std::filesystem::path& path,
    std::string& id, std::string& mapId,
    int& behavior, float& speed, bool& loop, bool& respawn,
    std::vector<std::pair<float, float>>& waypoints,
    std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open path file for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADPATH" || version != 1) {
        setError(errorMessage, "Unsupported path file type or version.");
        return false;
    }

    std::string key;

    input >> key;
    if (key != "id") { setError(errorMessage, "Expected id."); return false; }
    input >> id;

    input >> key;
    if (key != "map") { setError(errorMessage, "Expected map."); return false; }
    input >> mapId;

    input >> key;
    if (key != "behavior") { setError(errorMessage, "Expected behavior."); return false; }
    input >> behavior;

    input >> key;
    if (key != "speed") { setError(errorMessage, "Expected speed."); return false; }
    input >> speed;

    int loopVal = 0;
    input >> key;
    if (key != "loop") { setError(errorMessage, "Expected loop."); return false; }
    input >> loopVal;
    loop = (loopVal != 0);

    int respawnVal = 0;
    input >> key;
    if (key != "respawn") { setError(errorMessage, "Expected respawn."); return false; }
    input >> respawnVal;
    respawn = (respawnVal != 0);

    int wpCount = 0;
    input >> key;
    if (key != "waypoints") { setError(errorMessage, "Expected waypoints."); return false; }
    input >> wpCount;
    if (wpCount < 0 || wpCount > 4096) { setError(errorMessage, "Waypoint count out of range."); return false; }

    waypoints.clear();
    waypoints.reserve(static_cast<std::size_t>(wpCount));
    for (int i = 0; i < wpCount; ++i) {
        float x = 0.0f;
        float y = 0.0f;
        input >> key;
        if (key != "wp") { setError(errorMessage, "Expected wp entry."); return false; }
        input >> x >> y;
        if (!input) { setError(errorMessage, "Failed to read waypoint."); return false; }
        waypoints.emplace_back(x, y);
    }

    input >> key;
    if (key != "end") { setError(errorMessage, "Expected end marker."); return false; }

    return true;
}

} // namespace

void EnemyPathEditorPanel::draw(EditorContext& context)
{
    drawToolbar(context);
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(200.0f, available.x * 0.22f);

    ImGui::BeginChild("PathWaypointList", ImVec2(leftWidth, 0.0f), true);
    drawWaypointList();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("PathCanvas", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas();
    ImGui::EndChild();
}

void EnemyPathEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Path id", pathId_.data(), pathId_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Map id (ref)", mapId_.data(), mapId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Load map bg")) {
        loadBgMap(context);
    }

    ImGui::TextUnformatted("Behavior:");
    for (int b = 0; b < 3; ++b) {
        ImGui::SameLine();
        int bInt = static_cast<int>(behavior_);
        if (ImGui::RadioButton(kBehaviorNames[b], &bInt, b)) {
            behavior_ = static_cast<Behavior>(bInt);
        }
    }

    ImGui::SameLine(0.0f, 24.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Speed", &speed_, 16.0f, 256.0f, "%.0f px/s");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loop_);
    ImGui::SameLine();
    ImGui::Checkbox("Respawn enemy", &respawn_);

    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("Zoom", &zoom_, 0.5f, 6.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("Snap to grid", &snapToGrid_);

    ImGui::SameLine(0.0f, 24.0f);
    if (ImGui::Button("Save .adpath")) {
        savePath(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .adpath")) {
        loadPath(context);
    }

    ImGui::Text("Path files: %s", context.assets.gamePathPath().string().c_str());
    ImGui::TextDisabled("Click empty area: add waypoint   Click near waypoint: select   Drag selected: move   Right-click: delete");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void EnemyPathEditorPanel::drawWaypointList()
{
    ImGui::Text("Waypoints (%d)", static_cast<int>(waypoints_.size()));
    ImGui::Separator();

    if (ImGui::Button("Clear all")) {
        waypoints_.clear();
        selectedWaypoint_ = -1;
    }

    for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
        ImGui::PushID(i);
        const bool sel = (i == selectedWaypoint_);
        char label[48];
        std::snprintf(label, sizeof(label), "[%d]  %.0f, %.0f", i, waypoints_[i].x, waypoints_[i].y);
        if (ImGui::Selectable(label, sel)) {
            selectedWaypoint_ = i;
        }
        ImGui::PopID();
    }
}

void EnemyPathEditorPanel::drawCanvas()
{
    const int worldTilesW = bgMapLoaded_ ? bgMap_.width : kDefaultWorldTilesW;
    const int worldTilesH = bgMapLoaded_ ? bgMap_.height : kDefaultWorldTilesH;
    const float worldW = static_cast<float>(worldTilesW) * kWorldTileSize;
    const float worldH = static_cast<float>(worldTilesH) * kWorldTileSize;
    const float tileCanvas = kWorldTileSize * zoom_;
    const float canvasW = worldW * zoom_;
    const float canvasH = worldH * zoom_;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("PathCanvas", ImVec2(canvasW, canvasH),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, {origin.x + canvasW, origin.y + canvasH}, IM_COL32(20, 22, 28, 255));

    // Map background (mid layer, dim)
    if (bgMapLoaded_) {
        for (int y = 0; y < bgMap_.height; ++y) {
            for (int x = 0; x < bgMap_.width; ++x) {
                const uint16_t id = bgMap_.layers[1][static_cast<std::size_t>(y) * bgMap_.width + x];
                if (id != 0u) {
                    const ImVec2 tMin{origin.x + x * tileCanvas, origin.y + y * tileCanvas};
                    const ImVec2 tMax{tMin.x + tileCanvas, tMin.y + tileCanvas};
                    dl->AddRectFilled(tMin, tMax, bgColorForTileId(id));
                }
            }
        }
    }

    // Grid lines
    for (int gy = 0; gy <= worldTilesH; ++gy) {
        dl->AddLine({origin.x, origin.y + gy * tileCanvas},
            {origin.x + canvasW, origin.y + gy * tileCanvas}, IM_COL32(50, 55, 65, 200));
    }
    for (int gx = 0; gx <= worldTilesW; ++gx) {
        dl->AddLine({origin.x + gx * tileCanvas, origin.y},
            {origin.x + gx * tileCanvas, origin.y + canvasH}, IM_COL32(50, 55, 65, 200));
    }

    // Path lines between consecutive waypoints
    const ImU32 lineCol = IM_COL32(80, 210, 120, 200);
    const ImU32 loopLineCol = IM_COL32(80, 210, 120, 100);
    for (int i = 0; i + 1 < static_cast<int>(waypoints_.size()); ++i) {
        dl->AddLine(
            {origin.x + waypoints_[i].x * zoom_, origin.y + waypoints_[i].y * zoom_},
            {origin.x + waypoints_[i + 1].x * zoom_, origin.y + waypoints_[i + 1].y * zoom_},
            lineCol, 2.0f);
    }
    if (loop_ && waypoints_.size() >= 2) {
        dl->AddLine(
            {origin.x + waypoints_.back().x * zoom_, origin.y + waypoints_.back().y * zoom_},
            {origin.x + waypoints_.front().x * zoom_, origin.y + waypoints_.front().y * zoom_},
            loopLineCol, 1.5f);
    }

    // Waypoint circles
    for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
        const ImVec2 center{origin.x + waypoints_[i].x * zoom_, origin.y + waypoints_[i].y * zoom_};
        const bool sel = (i == selectedWaypoint_);
        dl->AddCircleFilled(center, kWaypointRadius,
            sel ? IM_COL32(255, 220, 50, 255) : IM_COL32(80, 210, 120, 255));
        dl->AddCircle(center, kWaypointRadius, IM_COL32(10, 15, 20, 220));
        char idx[8];
        std::snprintf(idx, sizeof(idx), "%d", i);
        dl->AddText({center.x + kWaypointRadius + 2.0f, center.y - 6.0f},
            IM_COL32(220, 230, 240, 200), idx);
    }

    // Handle input
    if (!ImGui::IsItemHovered()) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float wx = (mouse.x - origin.x) / zoom_;
    const float wy = (mouse.y - origin.y) / zoom_;

    // Find nearest waypoint
    int nearest = -1;
    float nearestDist = kHitRadius / zoom_;
    for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
        const float dx = waypoints_[i].x - wx;
        const float dy = waypoints_[i].y - wy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < nearestDist) {
            nearestDist = d;
            nearest = i;
        }
    }

    // Left-click: select existing waypoint or add new one
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (nearest >= 0) {
            selectedWaypoint_ = nearest;
            dragging_ = true;
        } else {
            float ax = wx;
            float ay = wy;
            if (snapToGrid_) {
                ax = snapValue(ax);
                ay = snapValue(ay);
            }
            ax = std::clamp(ax, 0.0f, worldW);
            ay = std::clamp(ay, 0.0f, worldH);
            waypoints_.push_back({ax, ay});
            selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
        }
    }

    // Drag selected waypoint
    if (dragging_ && selectedWaypoint_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float nx = wx;
        float ny = wy;
        if (snapToGrid_) {
            nx = snapValue(nx);
            ny = snapValue(ny);
        }
        waypoints_[selectedWaypoint_].x = std::clamp(nx, 0.0f, worldW);
        waypoints_[selectedWaypoint_].y = std::clamp(ny, 0.0f, worldH);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging_ = false;
    }

    // Right-click: delete nearest waypoint
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0) {
        waypoints_.erase(waypoints_.begin() + nearest);
        if (selectedWaypoint_ >= static_cast<int>(waypoints_.size())) {
            selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
        }
    }

    // Delete key: remove selected waypoint
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedWaypoint_ >= 0) {
        waypoints_.erase(waypoints_.begin() + selectedWaypoint_);
        if (selectedWaypoint_ >= static_cast<int>(waypoints_.size())) {
            selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
        }
    }
}

void EnemyPathEditorPanel::loadBgMap(EditorContext& context)
{
    const std::string id(mapId_.data());
    if (id.empty()) {
        status_ = "No map id set.";
        return;
    }
    const std::filesystem::path mapPath = context.assets.gameMapPath() / (id + ".admap");
    std::string error;
    if (!game::loadTileMap(mapPath, bgMap_, &error)) {
        status_ = "Failed to load map background: " + error;
        bgMapLoaded_ = false;
        return;
    }
    bgMapLoaded_ = true;
    status_ = "Loaded map background: " + mapPath.generic_string();
}

void EnemyPathEditorPanel::savePath(EditorContext& context)
{
    game::EnemyPath path;
    path.id = pathId_.data();
    path.mapId = mapId_.data();
    path.behavior = static_cast<game::PathBehavior>(static_cast<int>(behavior_));
    path.speed = speed_;
    path.loop = loop_;
    path.respawn = respawn_;
    path.waypoints.reserve(waypoints_.size());
    for (const auto& wp : waypoints_) {
        path.waypoints.push_back({wp.x, wp.y});
    }

    std::string error;
    const std::filesystem::path outputPath = context.assets.gamePathPath() / (path.id + ".adpath");
    if (game::saveEnemyPath(outputPath, path, &error)) {
        status_ = "Saved path: " + outputPath.generic_string();
    } else {
        status_ = "Failed to save path: " + error;
    }
}

void EnemyPathEditorPanel::loadPath(EditorContext& context)
{
    const std::string id(pathId_.data());
    const std::filesystem::path inputPath = context.assets.gamePathPath() / (id + ".adpath");

    game::EnemyPath path;
    std::string error;
    if (!game::loadEnemyPath(inputPath, path, &error)) {
        status_ = "Failed to load path: " + error;
        return;
    }

    // Apply loaded data
    const std::size_t idLen = std::min(path.id.size(), pathId_.size() - 1);
    std::memset(pathId_.data(), 0, pathId_.size());
    std::memcpy(pathId_.data(), path.id.data(), idLen);

    const std::size_t mapLen = std::min(path.mapId.size(), mapId_.size() - 1);
    std::memset(mapId_.data(), 0, mapId_.size());
    std::memcpy(mapId_.data(), path.mapId.data(), mapLen);

    behavior_ = static_cast<Behavior>(std::clamp(static_cast<int>(path.behavior), 0, 2));
    speed_ = std::clamp(path.speed, 16.0f, 256.0f);
    loop_ = path.loop;
    respawn_ = path.respawn;

    waypoints_.clear();
    waypoints_.reserve(path.waypoints.size());
    for (const game::PathWaypoint& wp : path.waypoints) {
        waypoints_.push_back({wp.x, wp.y});
    }
    selectedWaypoint_ = -1;

    status_ = "Loaded path: " + inputPath.generic_string();
}

float EnemyPathEditorPanel::snapValue(float v) const
{
    return std::round(v / kWorldTileSize) * kWorldTileSize;
}

} // namespace adventure::editor
