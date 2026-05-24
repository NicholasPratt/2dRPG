#include "editor/panels/enemy_path_editor_panel.hpp"

#include "game/constants.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
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
const char* kCurveModeNames[] = {"Linear", "Spline"};

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

void copyToBuffer(std::array<char, 64>& buffer, const std::string& value)
{
    std::memset(buffer.data(), 0, buffer.size());
    std::memcpy(buffer.data(), value.data(), std::min(value.size(), buffer.size() - 1));
}

} // namespace

void EnemyPathEditorPanel::draw(EditorContext& context)
{
    if (!projectLoaded_) {
        loadProjectEnemyTypes(context);
        projectLoaded_ = true;
    }
    if (context.enemyTypes.empty()) {
        context.enemyTypes.push_back({});
    }
    if (std::string(mapId_.data()) != context.selectedScreenMapId && !context.selectedScreenMapId.empty()) {
        copyToBuffer(mapId_, context.selectedScreenMapId);
        loadBgMap(context);
    }
    if (selectedPlacement_ >= static_cast<int>(context.selectedScreenEnemies.size())) {
        selectedPlacement_ = static_cast<int>(context.selectedScreenEnemies.size()) - 1;
    }

    drawToolbar(context);
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(260.0f, std::max(200.0f, available.x * 0.26f));

    ImGui::BeginChild("EnemyPlacementList", ImVec2(leftWidth, 0.0f), true);
    drawPlacementList(context);
    ImGui::Separator();
    drawWaypointList(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("PathCanvas", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void EnemyPathEditorPanel::drawTypes(EditorContext& context)
{
    if (!projectLoaded_) {
        loadProjectEnemyTypes(context);
        projectLoaded_ = true;
    }
    if (context.enemyTypes.empty()) {
        context.enemyTypes.push_back({});
    }
    drawEnemyTypePage(context);
    ImGui::Separator();
    if (ImGui::Button("Save enemy types")) {
        saveProjectEnemyTypes(context);
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void EnemyPathEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::Text("Screen: %s", context.selectedScreenId.empty() ? "(none)" : context.selectedScreenId.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("map %s", context.selectedScreenMapId.empty() ? "(none)" : context.selectedScreenMapId.c_str());
    ImGui::SameLine();
    if (ImGui::Button("New enemy placement")) {
        createPlacement(context);
    }
    ImGui::SameLine();
    if (!context.enemyTypes.empty()) {
        selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("Placement type", context.enemyTypes[static_cast<std::size_t>(selectedType_)].id.c_str())) {
            for (int i = 0; i < static_cast<int>(context.enemyTypes.size()); ++i) {
                if (ImGui::Selectable(context.enemyTypes[static_cast<std::size_t>(i)].id.c_str(), selectedType_ == i)) {
                    selectedType_ = i;
                    const game::EnemyType& selectedType = context.enemyTypes[static_cast<std::size_t>(selectedType_)];
                    copyToBuffer(typeId_, selectedType.id);
                    copyToBuffer(spriteId_, selectedType.spriteId);
                    writeCurrentPlacement(context);
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Placement id", placementId_.data(), placementId_.size())) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Enemy type", typeId_.data(), typeId_.size())) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Type sprite", spriteId_.data(), spriteId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Edit Sprite")) {
        const std::string spriteId(spriteId_.data());
        if (!spriteId.empty()) {
            context.requestedSpriteReference = (context.assets.gameSprites / (spriteId + ".sprite.json")).generic_string();
            context.requestEditSprite = true;
        }
    }

    if (ImGui::Button("Reload screen background")) {
        copyToBuffer(mapId_, context.selectedScreenMapId);
        loadBgMap(context);
    }

    ImGui::TextUnformatted("Behavior:");
    for (int b = 0; b < 3; ++b) {
        ImGui::SameLine();
        int bInt = static_cast<int>(behavior_);
        if (ImGui::RadioButton(kBehaviorNames[b], &bInt, b)) {
            behavior_ = static_cast<Behavior>(bInt);
            writeCurrentPlacement(context);
        }
    }

    ImGui::SameLine(0.0f, 24.0f);
    ImGui::TextUnformatted("Path:");
    for (int m = 0; m < 2; ++m) {
        ImGui::SameLine();
        int mode = static_cast<int>(curveMode_);
        if (ImGui::RadioButton(kCurveModeNames[m], &mode, m)) {
            curveMode_ = static_cast<CurveMode>(mode);
            writeCurrentPlacement(context);
        }
    }

    ImGui::SameLine(0.0f, 24.0f);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Speed override", &speed_, 0.0f, 256.0f, "%.0f px/s")) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Loop", &loop_)) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Respawn enemy", &respawn_)) {
        writeCurrentPlacement(context);
    }

    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("Zoom", &zoom_, 0.5f, 6.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("Snap to grid", &snapToGrid_);

    drawAnimationStateHelper(context);

    ImGui::Text("Placements save inside the selected screen in chapter %s.", context.currentChapterId.c_str());
    ImGui::TextDisabled("Click empty area: add waypoint   Click near waypoint: select   Drag selected: move   Right-click: delete");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void EnemyPathEditorPanel::drawEnemyTypePage(EditorContext& context)
{
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1);
    ImGui::TextUnformatted("Enemy Types");
    ImGui::SameLine();
    if (ImGui::Button("New type")) {
        game::EnemyType type;
        type.id = "enemy_type_" + std::to_string(context.enemyTypes.size() + 1);
        type.spriteId = type.id;
        context.enemyTypes.push_back(type);
        selectedType_ = static_cast<int>(context.enemyTypes.size()) - 1;
        context.markDirty();
    }

    if (!context.enemyTypes.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("Selected type", context.enemyTypes[static_cast<std::size_t>(selectedType_)].id.c_str())) {
            for (int i = 0; i < static_cast<int>(context.enemyTypes.size()); ++i) {
                if (ImGui::Selectable(context.enemyTypes[static_cast<std::size_t>(i)].id.c_str(), selectedType_ == i)) {
                    selectedType_ = i;
                    const game::EnemyType& selectedType = context.enemyTypes[static_cast<std::size_t>(selectedType_)];
                    copyToBuffer(typeId_, selectedType.id);
                    copyToBuffer(spriteId_, selectedType.spriteId);
                    writeCurrentPlacement(context);
                }
            }
            ImGui::EndCombo();
        }

        game::EnemyType& type = context.enemyTypes[static_cast<std::size_t>(selectedType_)];
        char typeId[64]{};
        char spriteId[64]{};
        std::memcpy(typeId, type.id.data(), std::min(type.id.size(), sizeof(typeId) - 1));
        std::memcpy(spriteId, type.spriteId.data(), std::min(type.spriteId.size(), sizeof(spriteId) - 1));
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("Type id", typeId, sizeof(typeId))) {
            type.id = typeId;
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("Sprite", spriteId, sizeof(spriteId))) {
            type.spriteId = spriteId;
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("HP", &type.maxHealth)) {
            type.maxHealth = std::clamp(type.maxHealth, 1, 999);
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("Damage", &type.contactDamage)) {
            type.contactDamage = std::clamp(type.contactDamage, 0, 999);
            context.markDirty();
        }
        float hitbox[2]{type.hitboxWidth, type.hitboxHeight};
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputFloat2("Hitbox", hitbox, "%.1f")) {
            type.hitboxWidth = std::clamp(hitbox[0], 1.0f, 256.0f);
            type.hitboxHeight = std::clamp(hitbox[1], 1.0f, 256.0f);
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat("Cooldown", &type.attackCooldownSeconds, 0.1f, 0.5f, "%.2f")) {
            type.attackCooldownSeconds = std::clamp(type.attackCooldownSeconds, 0.0f, 60.0f);
            context.markDirty();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat("Base speed", &type.speed, 1.0f, 8.0f, "%.0f")) {
            type.speed = std::clamp(type.speed, 0.0f, 512.0f);
            context.markDirty();
        }
    }
}

void EnemyPathEditorPanel::drawPlacementList(EditorContext& context)
{
    ImGui::Text("Enemies on %s", context.selectedScreenId.empty() ? "(no screen)" : context.selectedScreenId.c_str());
    for (int i = 0; i < static_cast<int>(context.selectedScreenEnemies.size()); ++i) {
        ImGui::PushID(i);
        const game::EnemyPlacement& enemy = context.selectedScreenEnemies[static_cast<std::size_t>(i)];
        const std::string label = enemy.id + "  [" + enemy.typeId + "]";
        if (ImGui::Selectable(label.c_str(), selectedPlacement_ == i)) {
            selectPlacement(context, i);
        }
        ImGui::PopID();
    }
    if (selectedPlacement_ >= 0 && selectedPlacement_ < static_cast<int>(context.selectedScreenEnemies.size())) {
        if (ImGui::Button("Delete placement")) {
            context.selectedScreenEnemies.erase(context.selectedScreenEnemies.begin() + selectedPlacement_);
            selectedPlacement_ = std::clamp(selectedPlacement_, -1, static_cast<int>(context.selectedScreenEnemies.size()) - 1);
            if (selectedPlacement_ >= 0) {
                selectPlacement(context, selectedPlacement_);
            } else {
                waypoints_.clear();
            }
            context.markDirty();
        }
    }
}

void EnemyPathEditorPanel::drawAnimationStateHelper(EditorContext& context)
{
    ImGui::TextDisabled("Enemy sprite states use frame Type in the Sprite editor: idle, walk, attack, hurt, dead.");
    ImGui::SameLine();
    if (ImGui::Button("Open sprite states")) {
        const std::string spriteId(spriteId_.data());
        if (!spriteId.empty()) {
            context.requestedSpriteReference = (context.assets.gameSprites / (spriteId + ".sprite.json")).generic_string();
            context.requestEditSprite = true;
        }
    }
}

void EnemyPathEditorPanel::drawWaypointList(EditorContext& context)
{
    ImGui::Text("Waypoints (%d)", static_cast<int>(waypoints_.size()));
    ImGui::Separator();

    if (ImGui::Button("Clear all")) {
        waypoints_.clear();
        selectedWaypoint_ = -1;
        writeCurrentPlacement(context);
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

void EnemyPathEditorPanel::drawCanvas(EditorContext& context)
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

    // Map background: floor, walls, ceiling, and obstacles from the selected screen.
    if (bgMapLoaded_) {
        for (int layer = 0; layer < 3; ++layer) {
            for (int y = 0; y < bgMap_.height; ++y) {
                for (int x = 0; x < bgMap_.width; ++x) {
                    const uint16_t id = bgMap_.layers[static_cast<std::size_t>(layer)][static_cast<std::size_t>(y) * bgMap_.width + x];
                    if (id != 0u) {
                        const ImVec2 tMin{origin.x + x * tileCanvas, origin.y + y * tileCanvas};
                        const ImVec2 tMax{tMin.x + tileCanvas, tMin.y + tileCanvas};
                        ImU32 color = bgColorForTileId(id);
                        if (layer == 0) {
                            color = IM_COL32((color & 0xffu) / 2, ((color >> 8u) & 0xffu) / 2, ((color >> 16u) & 0xffu) / 2, 180);
                        } else if (layer == 2) {
                            color = IM_COL32(70, 105, 170, 150);
                        }
                        dl->AddRectFilled(tMin, tMax, color);
                    }
                }
            }
        }
        for (const game::MapObstacle& obstacle : bgMap_.obstacles) {
            const ImVec2 oMin{origin.x + obstacle.x * tileCanvas, origin.y + obstacle.y * tileCanvas};
            const ImVec2 oMax{oMin.x + obstacle.width * tileCanvas, oMin.y + obstacle.height * tileCanvas};
            dl->AddRectFilled(oMin, oMax, IM_COL32(220, 80, 70, 125));
            dl->AddRect(oMin, oMax, IM_COL32(255, 235, 180, 210), 0.0f, 0, 2.0f);
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
    if (curveMode_ == CurveMode::Spline && waypoints_.size() >= 3) {
        const int segmentCount = loop_ ? static_cast<int>(waypoints_.size()) : static_cast<int>(waypoints_.size()) - 1;
        for (int segment = 0; segment < segmentCount; ++segment) {
            Waypoint prev = splinePoint(segment, 0.0f);
            for (int step = 1; step <= 16; ++step) {
                const Waypoint next = splinePoint(segment, static_cast<float>(step) / 16.0f);
                dl->AddLine(waypointToCanvas(origin, prev), waypointToCanvas(origin, next),
                    (loop_ && segment + 1 == segmentCount) ? loopLineCol : lineCol, 2.0f);
                prev = next;
            }
        }
    } else {
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
            writeCurrentPlacement(context);
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
        writeCurrentPlacement(context);
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
        writeCurrentPlacement(context);
    }

    // Delete key: remove selected waypoint
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedWaypoint_ >= 0) {
        waypoints_.erase(waypoints_.begin() + selectedWaypoint_);
        if (selectedWaypoint_ >= static_cast<int>(waypoints_.size())) {
            selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
        }
        writeCurrentPlacement(context);
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

void EnemyPathEditorPanel::saveProjectEnemyTypes(EditorContext& context)
{
    game::GameProject project;
    (void)game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
    project.id = project.id.empty() ? "game" : project.id;
    project.enemyTypes = context.enemyTypes;
    if (!context.currentChapterId.empty() &&
        std::find(project.chapterIds.begin(), project.chapterIds.end(), context.currentChapterId) == project.chapterIds.end()) {
        project.chapterIds.push_back(context.currentChapterId);
    }
    std::string error;
    if (game::saveGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, &error)) {
        status_ = "Saved universal enemy types.";
    } else {
        status_ = "Failed to save enemy types: " + error;
    }
}

void EnemyPathEditorPanel::loadProjectEnemyTypes(EditorContext& context)
{
    game::GameProject project;
    if (game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr)) {
        context.enemyTypes = std::move(project.enemyTypes);
    }
    if (context.enemyTypes.empty()) {
        context.enemyTypes.push_back({});
    }
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1);
    if (!context.enemyTypes.empty()) {
        copyToBuffer(typeId_, context.enemyTypes.front().id);
        copyToBuffer(spriteId_, context.enemyTypes.front().spriteId);
    }
}

void EnemyPathEditorPanel::createPlacement(EditorContext& context)
{
    game::EnemyPlacement placement;
    placement.id = "enemy_" + std::to_string(context.selectedScreenEnemies.size() + 1);
    if (!context.enemyTypes.empty()) {
        const game::EnemyType& type = context.enemyTypes[static_cast<std::size_t>(std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1))];
        placement.typeId = type.id;
        placement.speedOverride = type.speed;
    }
    placement.waypoints.push_back({static_cast<float>(game::kTileSize * 2), static_cast<float>(game::kTileSize * 2)});
    context.selectedScreenEnemies.push_back(placement);
    selectPlacement(context, static_cast<int>(context.selectedScreenEnemies.size()) - 1);
    context.markDirty();
}

void EnemyPathEditorPanel::selectPlacement(EditorContext& context, int index)
{
    if (index < 0 || index >= static_cast<int>(context.selectedScreenEnemies.size())) {
        return;
    }
    selectedPlacement_ = index;
    const game::EnemyPlacement& placement = context.selectedScreenEnemies[static_cast<std::size_t>(index)];
    copyToBuffer(placementId_, placement.id);
    copyToBuffer(typeId_, placement.typeId);
    const auto typeIt = std::find_if(context.enemyTypes.begin(), context.enemyTypes.end(), [&](const game::EnemyType& type) {
        return type.id == placement.typeId;
    });
    if (typeIt != context.enemyTypes.end()) {
        selectedType_ = static_cast<int>(std::distance(context.enemyTypes.begin(), typeIt));
        copyToBuffer(spriteId_, typeIt->spriteId);
    }
    behavior_ = static_cast<Behavior>(std::clamp(static_cast<int>(placement.behavior), 0, 2));
    curveMode_ = static_cast<CurveMode>(std::clamp(static_cast<int>(placement.curveMode), 0, 1));
    speed_ = placement.speedOverride;
    loop_ = placement.loop;
    respawn_ = placement.respawn;
    waypoints_.clear();
    for (const game::PathWaypoint& waypoint : placement.waypoints) {
        waypoints_.push_back({waypoint.x, waypoint.y});
    }
    selectedWaypoint_ = -1;
}

void EnemyPathEditorPanel::writeCurrentPlacement(EditorContext& context)
{
    if (selectedPlacement_ < 0 || selectedPlacement_ >= static_cast<int>(context.selectedScreenEnemies.size())) {
        return;
    }
    game::EnemyPlacement& placement = context.selectedScreenEnemies[static_cast<std::size_t>(selectedPlacement_)];
    placement.id = placementId_.data();
    placement.typeId = typeId_.data();
    placement.behavior = static_cast<game::PathBehavior>(static_cast<int>(behavior_));
    placement.curveMode = static_cast<game::PathCurveMode>(static_cast<int>(curveMode_));
    placement.speedOverride = std::max(0.0f, speed_);
    placement.loop = loop_;
    placement.respawn = respawn_;
    placement.waypoints.clear();
    placement.waypoints.reserve(waypoints_.size());
    for (const Waypoint& waypoint : waypoints_) {
        placement.waypoints.push_back({waypoint.x, waypoint.y});
    }
    context.markDirty();
}

ImVec2 EnemyPathEditorPanel::waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const
{
    return {origin.x + waypoint.x * zoom_, origin.y + waypoint.y * zoom_};
}

EnemyPathEditorPanel::Waypoint EnemyPathEditorPanel::splinePoint(int segment, float t) const
{
    const int count = static_cast<int>(waypoints_.size());
    if (count == 0) {
        return {};
    }
    const auto at = [this, count](int index) -> const Waypoint& {
        if (loop_) {
            index %= count;
            if (index < 0) {
                index += count;
            }
            return waypoints_[static_cast<std::size_t>(index)];
        }
        return waypoints_[static_cast<std::size_t>(std::clamp(index, 0, count - 1))];
    };

    const Waypoint& p0 = at(segment - 1);
    const Waypoint& p1 = at(segment);
    const Waypoint& p2 = at(segment + 1);
    const Waypoint& p3 = at(segment + 2);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
    };
}

float EnemyPathEditorPanel::snapValue(float v) const
{
    return std::round(v / kWorldTileSize) * kWorldTileSize;
}

} // namespace adventure::editor
