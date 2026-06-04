#include "editor/panels/door_placement_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace adventure::editor {
namespace {

void copyToBuffer(std::array<char, 64>& buf, const std::string& value)
{
    std::memset(buf.data(), 0, buf.size());
    const std::size_t len = std::min(value.size(), buf.size() - 1);
    std::memcpy(buf.data(), value.data(), len);
}

const char* lockModeLabel(int mode)
{
    switch (mode) {
        case 1: return "Locked";
        case 2: return "Requires Item";
        default: return "Free Use";
    }
}

ImVec2 tileToCanvas(ImVec2 origin, int tileX, int tileY, float zoom)
{
    return {
        origin.x + static_cast<float>(tileX * game::kTileSize) * zoom,
        origin.y + static_cast<float>(tileY * game::kTileSize) * zoom,
    };
}

} // namespace

void DoorPlacementPanel::openForScreen(EditorContext& context)
{
    selectedDoor_ = -1;
    loadedMapId_.clear();
    bgMapLoaded_ = false;
    loadBackground(context);
}

void DoorPlacementPanel::saveForScreen(EditorContext& context)
{
    writeInspectorToSelected(context);
    if (context.selectedScreenDoorsMapId.empty()) {
        return;
    }
    game::TileMap map;
    const auto mapPath = context.assets.gameMapPath() / (context.selectedScreenDoorsMapId + ".admap");
    if (!game::loadTileMap(mapPath, map, nullptr)) {
        return;
    }
    map.doors = context.selectedScreenDoors;
    (void)game::saveTileMap(mapPath, map, nullptr);
}

void DoorPlacementPanel::loadBackground(EditorContext& context)
{
    if (context.selectedScreenMapId.empty() || context.selectedScreenMapId == loadedMapId_) {
        return;
    }
    loadedMapId_ = context.selectedScreenMapId;
    const auto mapPath = context.assets.gameMapPath() / (context.selectedScreenMapId + ".admap");
    bgMapLoaded_ = game::loadTileMap(mapPath, bgMap_, nullptr);
}

void DoorPlacementPanel::draw(EditorContext& context)
{
    loadBackground(context);
    drawToolbar(context);
    ImGui::Separator();

    const float leftW = 288.0f;
    const float availableH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("DoorLeftColumn", ImVec2(leftW, availableH), false);
    drawDoorList(context);
    ImGui::Separator();
    drawInspector(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("DoorCanvasScroll", ImVec2(0.0f, availableH), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void DoorPlacementPanel::drawToolbar(EditorContext& context)
{
    ImGui::Text("Screen: %s", context.selectedScreenId.empty() ? "(none)" : context.selectedScreenId.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Zoom##doorzoom", &zoom_, 0.05f, 0.5f, 4.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("Doors use tile X/Y and tile W/H. Click canvas to place.");
}

void DoorPlacementPanel::drawDoorList(EditorContext& context)
{
    if (ImGui::Button("Add Door", ImVec2(-1.0f, 0.0f))) {
        placeDoorAt(context, game::kScreenTilesW / 2, game::kScreenTilesH / 2);
    }
    if (selectedDoor_ >= 0 && selectedDoor_ < static_cast<int>(context.selectedScreenDoors.size())) {
        if (ImGui::Button("Delete Door", ImVec2(-1.0f, 0.0f))) {
            context.selectedScreenDoors.erase(context.selectedScreenDoors.begin() + selectedDoor_);
            selectedDoor_ = std::min(selectedDoor_, static_cast<int>(context.selectedScreenDoors.size()) - 1);
            if (selectedDoor_ >= 0) {
                syncInspectorFromSelected(context);
            }
            context.markDirty();
        }
        if (selectedDoor_ >= 0 && ImGui::Button("Edit Door Sprite", ImVec2(-1.0f, 0.0f))) {
            requestEditDoorSprite(context);
        }
    }

    for (int i = 0; i < static_cast<int>(context.selectedScreenDoors.size()); ++i) {
        const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool selected = selectedDoor_ == i;
        const std::string label = door.id.empty() ? ("door_" + std::to_string(i + 1)) : door.id;
        if (ImGui::Selectable(label.c_str(), selected)) {
            writeInspectorToSelected(context);
            selectedDoor_ = i;
            syncInspectorFromSelected(context);
        }
        ImGui::PopID();
    }
}

void DoorPlacementPanel::drawInspector(EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        ImGui::TextDisabled("No door selected.");
        return;
    }

    ImGui::TextUnformatted("Door Properties");
    if (ui::inputTextString("ID##door_id", doorId_.data(), doorId_.size())) { context.markDirty(); }
    if (ImGui::DragInt("Tile X##door_x", &tileX_, 1.0f, 0, game::kScreenTilesW - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Tile Y##door_y", &tileY_, 1.0f, 0, game::kScreenTilesH - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Tile W##door_w", &widthTiles_, 1.0f, 1, game::kScreenTilesW)) { context.markDirty(); }
    if (ImGui::DragInt("Tile H##door_h", &heightTiles_, 1.0f, 1, game::kScreenTilesH)) { context.markDirty(); }

    const char* lockModes[] = {"Free Use", "Locked", "Requires Item"};
    if (ImGui::Combo("Lock##door_lock", &lockMode_, lockModes, 3)) { context.markDirty(); }
    if (lockMode_ == 2) {
        if (ui::inputTextString("Required Item##door_req", requiredItemId_.data(), requiredItemId_.size())) { context.markDirty(); }
        if (ImGui::Checkbox("Consume Key##door_consume", &consumeKey_)) { context.markDirty(); }
    }
    if (ui::inputTextString("Target Screen##door_target", targetScreenId_.data(), targetScreenId_.size())) { context.markDirty(); }
    if (ImGui::DragInt("Target Tile X##door_tx", &targetTileX_, 1.0f, 0, game::kScreenTilesW - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Target Tile Y##door_ty", &targetTileY_, 1.0f, 0, game::kScreenTilesH - 1)) { context.markDirty(); }
    if (ui::inputTextString("Sprite ID##door_sprite", spriteId_.data(), spriteId_.size())) { context.markDirty(); }
    if (ImGui::Button("Edit Door Sprite##door_sprite_edit", ImVec2(-1.0f, 0.0f))) {
        requestEditDoorSprite(context);
    }
    if (ui::inputTextString("Opening Anim##door_anim", openingAnimation_.data(), openingAnimation_.size())) { context.markDirty(); }

    if (ImGui::Button("Apply Door##door_apply", ImVec2(-1.0f, 0.0f))) {
        writeInspectorToSelected(context);
    }
    ImGui::TextDisabled("Mode: %s", lockModeLabel(lockMode_));
    drawValidation(context);
}

void DoorPlacementPanel::drawValidation(EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        return;
    }
    writeInspectorToSelected(context);
    const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];

    bool hasWarning = false;
    const auto warning = [&hasWarning](const char* text) {
        if (!hasWarning) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), "Warnings");
            hasWarning = true;
        }
        ImGui::BulletText("%s", text);
    };

    if (door.id.empty()) {
        warning("Door id is empty.");
    } else {
        const int duplicates = static_cast<int>(std::count_if(context.selectedScreenDoors.begin(), context.selectedScreenDoors.end(),
            [&door](const game::MapDoorPlacement& other) { return other.id == door.id; }));
        if (duplicates > 1) {
            warning("Door id is duplicated on this screen.");
        }
    }

    if (door.lockMode == game::DoorLockMode::RequiresItem) {
        if (door.requiredItemId.empty()) {
            warning("Required-item doors need an item id.");
        } else {
            const bool itemExists = std::any_of(context.itemDefs.begin(), context.itemDefs.end(),
                [&door](const game::ItemDef& item) { return item.id == door.requiredItemId; });
            if (!itemExists) {
                warning("Required item id is not defined in the Items tab.");
            }
        }
    }

    const auto screenIt = std::find_if(context.chapterScreens.begin(), context.chapterScreens.end(),
        [&door](const ChapterScreenEntry& screen) { return screen.id == door.targetScreenId; });
    if (door.targetScreenId.empty()) {
        warning("Target screen is empty.");
        return;
    }
    if (screenIt == context.chapterScreens.end()) {
        warning("Target screen does not exist in this chapter.");
        return;
    }

    game::TileMap targetMap;
    const auto mapPath = context.assets.gameMapPath() / (screenIt->mapId + ".admap");
    if (!game::loadTileMap(mapPath, targetMap, nullptr)) {
        warning("Target screen map could not be loaded.");
        return;
    }
    if (door.targetTileX < 0 || door.targetTileY < 0 ||
        door.targetTileX >= targetMap.width || door.targetTileY >= targetMap.height) {
        warning("Target tile is outside the target map.");
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(door.targetTileY) * static_cast<std::size_t>(targetMap.width) +
        static_cast<std::size_t>(door.targetTileX);
    if (idx < targetMap.layers[1].size() && targetMap.layers[1][idx] != 0u) {
        warning("Target tile is blocked by the target map wall layer.");
    }
}

void DoorPlacementPanel::drawCanvas(EditorContext& context)
{
    const float worldW = static_cast<float>(game::kScreenTilesW * game::kTileSize);
    const float worldH = static_cast<float>(game::kScreenTilesH * game::kTileSize);
    const float canvasW = worldW * zoom_;
    const float canvasH = worldH * zoom_;
    const float tileSize = static_cast<float>(game::kTileSize) * zoom_;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("DoorCanvas", ImVec2(canvasW, canvasH),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    dl->AddRectFilled(origin, ImVec2(origin.x + canvasW, origin.y + canvasH), IM_COL32(22, 26, 30, 255));

    if (bgMapLoaded_) {
        for (int ty = 0; ty < bgMap_.height; ++ty) {
            for (int tx = 0; tx < bgMap_.width; ++tx) {
                const std::size_t idx = static_cast<std::size_t>(ty) * static_cast<std::size_t>(bgMap_.width) + static_cast<std::size_t>(tx);
                if (bgMap_.layers[1][idx] == 0u) {
                    continue;
                }
                const ImVec2 min = tileToCanvas(origin, tx, ty, zoom_);
                dl->AddRectFilled(min, ImVec2(min.x + tileSize, min.y + tileSize), IM_COL32(240, 200, 40, 32));
            }
        }
    }

    for (int tx = 0; tx <= game::kScreenTilesW; ++tx) {
        const float px = origin.x + static_cast<float>(tx * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + canvasH), IM_COL32(60, 60, 60, 70));
    }
    for (int ty = 0; ty <= game::kScreenTilesH; ++ty) {
        const float py = origin.y + static_cast<float>(ty * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + canvasW, py), IM_COL32(60, 60, 60, 70));
    }

    for (int i = 0; i < static_cast<int>(context.selectedScreenDoors.size()); ++i) {
        const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(i)];
        const ImVec2 min = tileToCanvas(origin, door.x, door.y, zoom_);
        const ImVec2 max{min.x + static_cast<float>(door.widthTiles) * tileSize,
            min.y + static_cast<float>(door.heightTiles) * tileSize};
        const bool selected = i == selectedDoor_;
        const ImU32 fill = door.lockMode == game::DoorLockMode::RequiresItem ? IM_COL32(120, 170, 255, 86) :
            (door.lockMode == game::DoorLockMode::Locked ? IM_COL32(255, 90, 80, 86) : IM_COL32(80, 220, 150, 86));
        dl->AddRectFilled(min, max, fill);
        dl->AddRect(min, max, selected ? IM_COL32(255, 255, 255, 245) : IM_COL32(150, 220, 255, 210),
            0.0f, 0, selected ? 3.0f : 1.5f);
        dl->AddText(ImVec2(min.x + 3.0f, min.y + 3.0f), IM_COL32(255, 255, 255, 230), "D");
    }

    if (!hovered) {
        return;
    }

    const int tileX = std::clamp(static_cast<int>((mouse.x - origin.x) / tileSize), 0, game::kScreenTilesW - 1);
    const int tileY = std::clamp(static_cast<int>((mouse.y - origin.y) / tileSize), 0, game::kScreenTilesH - 1);
    const ImVec2 hoverMin = tileToCanvas(origin, tileX, tileY, zoom_);
    dl->AddRect(hoverMin, ImVec2(hoverMin.x + tileSize, hoverMin.y + tileSize), IM_COL32(255, 255, 255, 180), 0.0f, 0, 2.0f);

    auto doorAt = [&](int tx, int ty) {
        for (int i = 0; i < static_cast<int>(context.selectedScreenDoors.size()); ++i) {
            const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(i)];
            if (tx >= door.x && ty >= door.y && tx < door.x + door.widthTiles && ty < door.y + door.heightTiles) {
                return i;
            }
        }
        return -1;
    };

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int idx = doorAt(tileX, tileY);
        if (idx >= 0) {
            context.selectedScreenDoors.erase(context.selectedScreenDoors.begin() + idx);
            selectedDoor_ = std::min(selectedDoor_, static_cast<int>(context.selectedScreenDoors.size()) - 1);
            context.markDirty();
        }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int idx = doorAt(tileX, tileY);
        if (idx >= 0) {
            writeInspectorToSelected(context);
            selectedDoor_ = idx;
            syncInspectorFromSelected(context);
        } else {
            placeDoorAt(context, tileX, tileY);
        }
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && selectedDoor_ >= 0 &&
        selectedDoor_ < static_cast<int>(context.selectedScreenDoors.size())) {
        game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];
        door.x = std::clamp(tileX, 0, game::kScreenTilesW - door.widthTiles);
        door.y = std::clamp(tileY, 0, game::kScreenTilesH - door.heightTiles);
        syncInspectorFromSelected(context);
        context.markDirty();
    }
}

void DoorPlacementPanel::requestEditDoorSprite(EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        return;
    }

    std::string spriteId(spriteId_.data());
    if (spriteId.empty()) {
        const std::string doorId(doorId_.data());
        spriteId = doorId.empty() ? "door_sprite" : doorId;
        copyToBuffer(spriteId_, spriteId);
        context.markDirty();
    }
    writeInspectorToSelected(context);
    context.requestedSpriteReference = (context.assets.gameSpritePath() / (spriteId + ".sprite.json")).generic_string();
    context.requestEditSprite = true;
}

void DoorPlacementPanel::placeDoorAt(EditorContext& context, int tileX, int tileY)
{
    writeInspectorToSelected(context);
    game::MapDoorPlacement door;
    door.id = "door_" + std::to_string(context.selectedScreenDoors.size() + 1);
    door.x = std::clamp(tileX, 0, game::kScreenTilesW - 1);
    door.y = std::clamp(tileY, 0, game::kScreenTilesH - 1);
    door.widthTiles = std::clamp(widthTiles_, 1, game::kScreenTilesW - door.x);
    door.heightTiles = std::clamp(heightTiles_, 1, game::kScreenTilesH - door.y);
    door.lockMode = static_cast<game::DoorLockMode>(std::clamp(lockMode_, 0, 2));
    door.requiredItemId = requiredItemId_.data();
    door.consumeKey = consumeKey_;
    door.targetScreenId = targetScreenId_.data();
    door.targetTileX = targetTileX_;
    door.targetTileY = targetTileY_;
    door.spriteId = spriteId_.data();
    door.openingAnimation = openingAnimation_.data();
    context.selectedScreenDoors.push_back(door);
    selectedDoor_ = static_cast<int>(context.selectedScreenDoors.size()) - 1;
    syncInspectorFromSelected(context);
    context.markDirty();
}

void DoorPlacementPanel::syncInspectorFromSelected(const EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        return;
    }
    const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];
    copyToBuffer(doorId_, door.id);
    tileX_ = door.x;
    tileY_ = door.y;
    widthTiles_ = door.widthTiles;
    heightTiles_ = door.heightTiles;
    lockMode_ = static_cast<int>(door.lockMode);
    copyToBuffer(requiredItemId_, door.requiredItemId);
    consumeKey_ = door.consumeKey;
    copyToBuffer(targetScreenId_, door.targetScreenId);
    targetTileX_ = door.targetTileX;
    targetTileY_ = door.targetTileY;
    copyToBuffer(spriteId_, door.spriteId);
    copyToBuffer(openingAnimation_, door.openingAnimation);
}

void DoorPlacementPanel::writeInspectorToSelected(EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        return;
    }
    game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];
    door.id = doorId_.data();
    door.x = std::clamp(tileX_, 0, game::kScreenTilesW - 1);
    door.y = std::clamp(tileY_, 0, game::kScreenTilesH - 1);
    door.widthTiles = std::clamp(widthTiles_, 1, game::kScreenTilesW - door.x);
    door.heightTiles = std::clamp(heightTiles_, 1, game::kScreenTilesH - door.y);
    door.lockMode = static_cast<game::DoorLockMode>(std::clamp(lockMode_, 0, 2));
    door.requiredItemId = requiredItemId_.data();
    door.consumeKey = consumeKey_;
    door.targetScreenId = targetScreenId_.data();
    door.targetTileX = std::clamp(targetTileX_, 0, game::kScreenTilesW - 1);
    door.targetTileY = std::clamp(targetTileY_, 0, game::kScreenTilesH - 1);
    door.spriteId = spriteId_.data();
    door.openingAnimation = openingAnimation_.data();
}

} // namespace adventure::editor
