#include "editor/panels/door_placement_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <system_error>
#include <vector>

namespace adventure::editor {
namespace {

void copyToBuffer(std::array<char, 64>& buf, const std::string& value)
{
    std::memset(buf.data(), 0, buf.size());
    const std::size_t len = std::min(value.size(), buf.size() - 1);
    std::memcpy(buf.data(), value.data(), len);
}

void copyToBuffer(std::array<char, 256>& buf, const std::string& value)
{
    std::memset(buf.data(), 0, buf.size());
    const std::size_t len = std::min(value.size(), buf.size() - 1);
    std::memcpy(buf.data(), value.data(), len);
}

std::string portableProjectPath(const EditorContext& context, const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, context.assets.projectRoot, error);
    return error ? path.generic_string() : relative.generic_string();
}

bool supportedSoundFile(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".ogg" || extension == ".wav";
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
    loadedTargetScreenId_.clear();
    targetMapLoaded_ = false;
    pickingTargetTile_ = false;
    loadBackground(context);
}

void DoorPlacementPanel::saveForScreen(EditorContext& context)
{
    writeInspectorToSelected(context);
    // Reconcile paired doors before persisting so each linked door's targetDoorId
    // is written and the partner door exists on its target screen.
    for (int i = 0; i < static_cast<int>(context.selectedScreenDoors.size()); ++i) {
        ensurePairedDoor(context, i);
    }
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
    ImGui::TextDisabled(pickingTargetTile_
        ? "Target spawn picker active."
        : "Doors use tile X/Y and tile W/H. Click canvas to place.");
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
    ImGui::TextUnformatted("Door ID");
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::inputTextString("##door_id", doorId_.data(), doorId_.size())) { context.markDirty(); }
    if (ImGui::DragInt("Tile X##door_x", &tileX_, 1.0f, 0, game::kScreenTilesW - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Tile Y##door_y", &tileY_, 1.0f, 0, game::kScreenTilesH - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Tile W##door_w", &widthTiles_, 1.0f, 1, game::kScreenTilesW)) { context.markDirty(); }
    if (ImGui::DragInt("Tile H##door_h", &heightTiles_, 1.0f, 1, game::kScreenTilesH)) { context.markDirty(); }

    const char* lockModes[] = {"Free Use", "Locked", "Requires Item"};
    ImGui::TextUnformatted("Lock mode");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##door_lock", &lockMode_, lockModes, 3)) { context.markDirty(); }
    if (lockMode_ == 1) {
        ImGui::TextDisabled("Locked is permanent.");
    }
    if (lockMode_ == 2) {
        ImGui::TextUnformatted("Required item ID");
        ImGui::SetNextItemWidth(-1.0f);
        if (ui::inputTextString("##door_req", requiredItemId_.data(), requiredItemId_.size())) { context.markDirty(); }
        if (ImGui::Checkbox("Consume Key##door_consume", &consumeKey_)) { context.markDirty(); }
    }
    ImGui::TextUnformatted("Target screen ID");
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::inputTextString("##door_target", targetScreenId_.data(), targetScreenId_.size())) { context.markDirty(); }

    // Paired-door linking: the player spawns at the linked door on the target
    // screen. The paired door is created automatically (centre of that screen).
    const std::string pairedDoorId = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)].targetDoorId;
    if (pairedDoorId.empty()) {
        ImGui::TextDisabled("Paired door: none yet");
    } else {
        ImGui::TextDisabled("Paired door: %s", pairedDoorId.c_str());
    }
    if (ImGui::Button("Link / create paired door##door_pair", ImVec2(-1.0f, 0.0f))) {
        writeInspectorToSelected(context);
        ensurePairedDoor(context, selectedDoor_);
    }
    if (!pairingStatus_.empty()) {
        ImGui::TextWrapped("%s", pairingStatus_.c_str());
    }

    ImGui::TextUnformatted("Target spawn tile");
    if (ImGui::DragInt("Target Tile X##door_tx", &targetTileX_, 1.0f, 0, game::kScreenTilesW - 1)) { context.markDirty(); }
    if (ImGui::DragInt("Target Tile Y##door_ty", &targetTileY_, 1.0f, 0, game::kScreenTilesH - 1)) { context.markDirty(); }
    if (ImGui::Button(pickingTargetTile_ ? "Picking Spawn Tile..." : "Set Spawn Tile On Target Screen",
            ImVec2(-1.0f, 0.0f))) {
        writeInspectorToSelected(context);
        pickingTargetTile_ = !pickingTargetTile_;
        loadedTargetScreenId_.clear();
        targetMapLoaded_ = false;
    }
    ImGui::TextUnformatted("Sprite ID");
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::inputTextString("##door_sprite", spriteId_.data(), spriteId_.size())) { context.markDirty(); }
    if (ImGui::Button("Edit Door Sprite##door_sprite_edit", ImVec2(-1.0f, 0.0f))) {
        requestEditDoorSprite(context);
    }
    if (ImGui::Checkbox("Render above walls##door_above_walls", &renderAboveWalls_)) { context.markDirty(); }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("On: door sprite draws over the wall texture (for doors set into wall art).\nOff (default): player draws in front of the door.");
    }
    ImGui::TextUnformatted("Opening animation ID");
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::inputTextString("##door_anim", openingAnimation_.data(), openingAnimation_.size())) { context.markDirty(); }
    drawSoundPicker(context);

    if (ImGui::Button("Apply Door##door_apply", ImVec2(-1.0f, 0.0f))) {
        writeInspectorToSelected(context);
        ensurePairedDoor(context, selectedDoor_);
    }
    ImGui::TextDisabled("Mode: %s", lockModeLabel(lockMode_));
    drawValidation(context);
}

void DoorPlacementPanel::drawSoundPicker(EditorContext& context)
{
    ImGui::SeparatorText("Door sounds");
    const std::filesystem::path sfxDir = context.assets.gameSfxPath() / "doors";
    ImGui::TextDisabled("Assets: %s", sfxDir.string().c_str());
    std::error_code error;
    if (!std::filesystem::exists(sfxDir, error)) {
        ImGui::TextDisabled("No door SFX folder yet.");
        return;
    }

    std::vector<std::filesystem::path> soundFiles;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(sfxDir, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || !supportedSoundFile(entry.path())) {
            continue;
        }
        soundFiles.push_back(entry.path());
    }
    std::sort(soundFiles.begin(), soundFiles.end());

    const auto drawDropdown = [&context, &sfxDir, &soundFiles](
        const char* label, const char* id, std::array<char, 256>& selectedPath) {
        ImGui::TextUnformatted(label);
        const std::string current(selectedPath.data());
        std::string preview = "<None>";
        if (!current.empty()) {
            const std::filesystem::path currentPath(current);
            std::error_code relativeError;
            const std::filesystem::path relative = std::filesystem::relative(
                currentPath.is_absolute() ? currentPath : context.assets.projectRoot / currentPath,
                sfxDir, relativeError);
            preview = relativeError ? currentPath.filename().string() : relative.generic_string();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(id, preview.c_str())) {
            if (ImGui::Selectable("<None>", current.empty())) {
                copyToBuffer(selectedPath, "");
                context.markDirty();
            }
            for (const std::filesystem::path& file : soundFiles) {
                const std::string path = portableProjectPath(context, file);
                std::error_code relativeError;
                const std::string display = std::filesystem::relative(file, sfxDir, relativeError).generic_string();
                if (ImGui::Selectable(relativeError ? file.filename().string().c_str() : display.c_str(),
                        current == path)) {
                    copyToBuffer(selectedPath, path);
                    context.markDirty();
                }
            }
            ImGui::EndCombo();
        }
    };

    drawDropdown("Open sound", "##door_open_sound", openSoundPath_);
    drawDropdown("Close sound", "##door_close_sound", closeSoundPath_);
    drawDropdown("Locked sound", "##door_locked_sound", lockedSoundPath_);
    if (soundFiles.empty()) {
        ImGui::TextDisabled("No .ogg or .wav files found.");
    }
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

    const auto validateSound = [&context, &warning](const std::string& path, const char* label) {
        if (path.empty()) {
            return;
        }
        const std::filesystem::path configured(path);
        const std::filesystem::path fullPath = configured.is_absolute()
            ? configured
            : context.assets.projectRoot / configured;
        std::error_code error;
        if (!supportedSoundFile(fullPath)) {
            const std::string message = std::string(label) + " must be an .ogg or .wav file.";
            warning(message.c_str());
        } else if (!std::filesystem::exists(fullPath, error)) {
            const std::string message = std::string(label) + " file does not exist.";
            warning(message.c_str());
        }
    };
    validateSound(door.openSoundPath, "Open sound");
    validateSound(door.closeSoundPath, "Close sound");
    validateSound(door.lockedSoundPath, "Locked sound");

    const auto screenIt = std::find_if(context.chapterScreens.begin(), context.chapterScreens.end(),
        [&door](const ChapterScreenEntry& screen) { return screen.id == door.targetScreenId; });
    if (door.targetScreenId.empty()) {
        if (door.lockMode == game::DoorLockMode::FreeUse) {
            warning("Target screen is empty.");
        }
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

bool DoorPlacementPanel::loadTargetMap(EditorContext& context)
{
    if (selectedDoor_ < 0 || selectedDoor_ >= static_cast<int>(context.selectedScreenDoors.size())) {
        targetMapLoaded_ = false;
        loadedTargetScreenId_.clear();
        return false;
    }

    writeInspectorToSelected(context);
    const game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];
    if (door.targetScreenId.empty()) {
        targetMapLoaded_ = false;
        loadedTargetScreenId_.clear();
        return false;
    }
    if (door.targetScreenId == loadedTargetScreenId_) {
        return targetMapLoaded_;
    }

    loadedTargetScreenId_ = door.targetScreenId;
    targetMapLoaded_ = false;
    const auto targetIt = std::find_if(context.chapterScreens.begin(), context.chapterScreens.end(),
        [&door](const ChapterScreenEntry& screen) { return screen.id == door.targetScreenId; });
    if (targetIt == context.chapterScreens.end()) {
        return false;
    }
    const auto mapPath = context.assets.gameMapPath() / (targetIt->mapId + ".admap");
    targetMapLoaded_ = game::loadTileMap(mapPath, targetMap_, nullptr);
    return targetMapLoaded_;
}

void DoorPlacementPanel::drawCanvas(EditorContext& context)
{
    const bool targetTileMode = pickingTargetTile_ && selectedDoor_ >= 0 &&
        selectedDoor_ < static_cast<int>(context.selectedScreenDoors.size());
    const bool targetLoaded = targetTileMode && loadTargetMap(context);
    const game::TileMap* canvasMap = targetLoaded ? &targetMap_ : (bgMapLoaded_ ? &bgMap_ : nullptr);
    const int canvasTilesW = canvasMap == nullptr ? game::kScreenTilesW : canvasMap->width;
    const int canvasTilesH = canvasMap == nullptr ? game::kScreenTilesH : canvasMap->height;
    const float worldW = static_cast<float>(canvasTilesW * game::kTileSize);
    const float worldH = static_cast<float>(canvasTilesH * game::kTileSize);
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

    if (canvasMap != nullptr) {
        for (int ty = 0; ty < canvasMap->height; ++ty) {
            for (int tx = 0; tx < canvasMap->width; ++tx) {
                const std::size_t idx = static_cast<std::size_t>(ty) * static_cast<std::size_t>(canvasMap->width) + static_cast<std::size_t>(tx);
                if (canvasMap->layers[1][idx] == 0u) {
                    continue;
                }
                const ImVec2 min = tileToCanvas(origin, tx, ty, zoom_);
                dl->AddRectFilled(min, ImVec2(min.x + tileSize, min.y + tileSize), IM_COL32(240, 200, 40, 32));
            }
        }
    }

    for (int tx = 0; tx <= canvasTilesW; ++tx) {
        const float px = origin.x + static_cast<float>(tx * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + canvasH), IM_COL32(60, 60, 60, 70));
    }
    for (int ty = 0; ty <= canvasTilesH; ++ty) {
        const float py = origin.y + static_cast<float>(ty * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + canvasW, py), IM_COL32(60, 60, 60, 70));
    }

    const std::vector<game::MapDoorPlacement>* doors =
        targetLoaded ? &targetMap_.doors : &context.selectedScreenDoors;
    for (int i = 0; i < static_cast<int>(doors->size()); ++i) {
        const game::MapDoorPlacement& door = (*doors)[static_cast<std::size_t>(i)];
        const ImVec2 min = tileToCanvas(origin, door.x, door.y, zoom_);
        const ImVec2 max{min.x + static_cast<float>(door.widthTiles) * tileSize,
            min.y + static_cast<float>(door.heightTiles) * tileSize};
        const bool selected = !targetLoaded && i == selectedDoor_;
        const ImU32 fill = door.lockMode == game::DoorLockMode::RequiresItem ? IM_COL32(120, 170, 255, 86) :
            (door.lockMode == game::DoorLockMode::Locked ? IM_COL32(255, 90, 80, 86) : IM_COL32(80, 220, 150, 86));
        dl->AddRectFilled(min, max, fill);
        dl->AddRect(min, max, selected ? IM_COL32(255, 255, 255, 245) : IM_COL32(150, 220, 255, 210),
            0.0f, 0, selected ? 3.0f : 1.5f);
        dl->AddText(ImVec2(min.x + 3.0f, min.y + 3.0f), IM_COL32(255, 255, 255, 230), "D");
    }

    if (targetLoaded && selectedDoor_ >= 0 && selectedDoor_ < static_cast<int>(context.selectedScreenDoors.size())) {
        const game::MapDoorPlacement& selectedDoor = context.selectedScreenDoors[static_cast<std::size_t>(selectedDoor_)];
        const int spawnX = std::clamp(selectedDoor.targetTileX, 0, canvasTilesW - 1);
        const int spawnY = std::clamp(selectedDoor.targetTileY, 0, canvasTilesH - 1);
        const ImVec2 min = tileToCanvas(origin, spawnX, spawnY, zoom_);
        const ImVec2 max{min.x + tileSize, min.y + tileSize};
        dl->AddRectFilled(min, max, IM_COL32(255, 220, 80, 96));
        dl->AddRect(min, max, IM_COL32(255, 255, 255, 245), 0.0f, 0, 3.0f);
        dl->AddText(ImVec2(min.x + 3.0f, min.y + 3.0f), IM_COL32(255, 255, 255, 240), "S");
    }

    if (!hovered) {
        return;
    }

    const int tileX = std::clamp(static_cast<int>((mouse.x - origin.x) / tileSize), 0, canvasTilesW - 1);
    const int tileY = std::clamp(static_cast<int>((mouse.y - origin.y) / tileSize), 0, canvasTilesH - 1);
    const ImVec2 hoverMin = tileToCanvas(origin, tileX, tileY, zoom_);
    dl->AddRect(hoverMin, ImVec2(hoverMin.x + tileSize, hoverMin.y + tileSize), IM_COL32(255, 255, 255, 180), 0.0f, 0, 2.0f);

    if (targetLoaded) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            targetTileX_ = tileX;
            targetTileY_ = tileY;
            writeInspectorToSelected(context);
            context.markDirty();
            pickingTargetTile_ = false;
        }
        return;
    }

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
    door.renderAboveWalls = renderAboveWalls_;
    door.targetScreenId = targetScreenId_.data();
    door.targetTileX = targetTileX_;
    door.targetTileY = targetTileY_;
    door.spriteId = spriteId_.data();
    door.openingAnimation = openingAnimation_.data();
    door.openSoundPath = openSoundPath_.data();
    door.closeSoundPath = closeSoundPath_.data();
    door.lockedSoundPath = lockedSoundPath_.data();
    context.selectedScreenDoors.push_back(door);
    selectedDoor_ = static_cast<int>(context.selectedScreenDoors.size()) - 1;
    syncInspectorFromSelected(context);
    context.markDirty();
}

void DoorPlacementPanel::ensurePairedDoor(EditorContext& context, int doorIndex)
{
    if (doorIndex < 0 || doorIndex >= static_cast<int>(context.selectedScreenDoors.size())) {
        return;
    }
    game::MapDoorPlacement& door = context.selectedScreenDoors[static_cast<std::size_t>(doorIndex)];
    if (door.targetScreenId.empty()) {
        pairingStatus_.clear();
        return;
    }
    const std::string currentScreenId = context.selectedScreenId;
    if (door.targetScreenId == currentScreenId) {
        pairingStatus_ = "Same-screen door: no paired door is created.";
        return;
    }
    if (door.id.empty()) {
        pairingStatus_ = "Give the door an id before linking screens.";
        return;
    }
    const auto targetIt = std::find_if(context.chapterScreens.begin(), context.chapterScreens.end(),
        [&door](const ChapterScreenEntry& screen) { return screen.id == door.targetScreenId; });
    if (targetIt == context.chapterScreens.end()) {
        pairingStatus_ = "Target screen is not in this chapter.";
        return;
    }

    // Load-merge: read the whole target map so appending a door preserves its
    // layers, obstacles, items, and existing doors.
    const auto mapPath = context.assets.gameMapPath() / (targetIt->mapId + ".admap");
    game::TileMap targetMap;
    if (!game::loadTileMap(mapPath, targetMap, nullptr)) {
        pairingStatus_ = "Could not load the target screen map.";
        return;
    }

    // Already paired and the partner still exists: only reconcile its back-links.
    if (!door.targetDoorId.empty()) {
        const auto pairedIt = std::find_if(targetMap.doors.begin(), targetMap.doors.end(),
            [&door](const game::MapDoorPlacement& other) { return other.id == door.targetDoorId; });
        if (pairedIt != targetMap.doors.end()) {
            bool changed = false;
            if (pairedIt->targetScreenId != currentScreenId) { pairedIt->targetScreenId = currentScreenId; changed = true; }
            if (pairedIt->targetDoorId != door.id) { pairedIt->targetDoorId = door.id; changed = true; }
            if (changed) {
                (void)game::saveTileMap(mapPath, targetMap, nullptr);
            }
            pairingStatus_ = "Linked to '" + door.targetDoorId + "' on " + door.targetScreenId + ".";
            return;
        }
        // The referenced partner is gone; fall through and create a fresh one.
    }

    // Create the paired door at the centre of the target screen with a unique id.
    std::string pairedId = "door_to_" + currentScreenId;
    {
        const std::string base = pairedId;
        int suffix = 1;
        while (std::any_of(targetMap.doors.begin(), targetMap.doors.end(),
            [&pairedId](const game::MapDoorPlacement& other) { return other.id == pairedId; })) {
            pairedId = base + "_" + std::to_string(++suffix);
        }
    }
    game::MapDoorPlacement paired;
    paired.id = pairedId;
    paired.widthTiles = std::clamp(door.widthTiles, 1, targetMap.width);
    paired.heightTiles = std::clamp(door.heightTiles, 1, targetMap.height);
    paired.x = std::clamp(targetMap.width / 2 - paired.widthTiles / 2, 0, targetMap.width - paired.widthTiles);
    paired.y = std::clamp(targetMap.height / 2 - paired.heightTiles / 2, 0, targetMap.height - paired.heightTiles);
    paired.lockMode = game::DoorLockMode::FreeUse;
    paired.targetScreenId = currentScreenId;
    paired.targetTileX = std::clamp(door.x + door.widthTiles / 2, 0, game::kScreenTilesW - 1);
    paired.targetTileY = std::clamp(door.y + door.heightTiles / 2, 0, game::kScreenTilesH - 1);
    paired.targetDoorId = door.id;
    paired.spriteId = door.spriteId;
    targetMap.doors.push_back(paired);
    if (!game::saveTileMap(mapPath, targetMap, nullptr)) {
        pairingStatus_ = "Failed to write the paired door to the target screen.";
        return;
    }
    door.targetDoorId = pairedId;
    context.markDirty();
    pairingStatus_ = "Created paired door '" + pairedId + "' at the centre of " +
        door.targetScreenId + ". Open that screen to reposition it.";
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
    renderAboveWalls_ = door.renderAboveWalls;
    copyToBuffer(targetScreenId_, door.targetScreenId);
    targetTileX_ = door.targetTileX;
    targetTileY_ = door.targetTileY;
    copyToBuffer(spriteId_, door.spriteId);
    copyToBuffer(openingAnimation_, door.openingAnimation);
    copyToBuffer(openSoundPath_, door.openSoundPath);
    copyToBuffer(closeSoundPath_, door.closeSoundPath);
    copyToBuffer(lockedSoundPath_, door.lockedSoundPath);
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
    door.renderAboveWalls = renderAboveWalls_;
    door.targetScreenId = targetScreenId_.data();
    door.targetTileX = std::clamp(targetTileX_, 0, game::kScreenTilesW - 1);
    door.targetTileY = std::clamp(targetTileY_, 0, game::kScreenTilesH - 1);
    door.spriteId = spriteId_.data();
    door.openingAnimation = openingAnimation_.data();
    door.openSoundPath = openSoundPath_.data();
    door.closeSoundPath = closeSoundPath_.data();
    door.lockedSoundPath = lockedSoundPath_.data();
}

} // namespace adventure::editor
