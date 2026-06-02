#include "editor/panels/item_placement_panel.hpp"

#include "game/constants.hpp"
#include "game/map.hpp"

#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace adventure::editor {
namespace {

void copyToBuffer(std::array<char, 64>& buf, const std::string& value)
{
    std::memset(buf.data(), 0, buf.size());
    const std::size_t len = std::min(value.size(), buf.size() - 1);
    std::memcpy(buf.data(), value.data(), len);
}

ImVec2 worldToCanvas(ImVec2 origin, float worldX, float worldY, float zoom)
{
    return { origin.x + worldX * zoom, origin.y + worldY * zoom };
}

ImVec2 canvasToWorld(ImVec2 origin, ImVec2 canvas, float zoom)
{
    return { (canvas.x - origin.x) / zoom, (canvas.y - origin.y) / zoom };
}

const char* pickupTypeLabel(int t)
{
    switch (t) {
        case 0: return "Weapon";
        case 1: return "Ammo";
        case 2: return "Health";
        case 3: return "Item";
        default: return "Unknown";
    }
}

const game::WeaponDef* findWeaponDef(const EditorContext& context, const std::string& id)
{
    for (const game::WeaponDef& weapon : context.weaponDefs) {
        if (weapon.id == id) {
            return &weapon;
        }
    }
    return nullptr;
}

} // namespace

void ItemPlacementPanel::openForScreen(EditorContext& context)
{
    selectedItem_ = -1;
    loadedMapId_.clear();
    bgMapLoaded_ = false;
    floorGraphics_ = {};
    wallGraphics_ = {};
    loadBackground(context);
}

void ItemPlacementPanel::saveForScreen(EditorContext& context)
{
    flushSelectedInspector(context);
    if (context.selectedScreenItemsMapId.empty()) {
        return;
    }
    game::TileMap map;
    const auto mapPath = context.assets.gameMapPath() / (context.selectedScreenItemsMapId + ".admap");
    if (!game::loadTileMap(mapPath, map, nullptr)) {
        return;
    }
    map.items = context.selectedScreenItems;
    game::saveTileMap(mapPath, map, nullptr);
}

void ItemPlacementPanel::loadBackground(EditorContext& context)
{
    if (context.selectedScreenMapId.empty() || context.selectedScreenMapId == loadedMapId_) {
        return;
    }
    loadedMapId_ = context.selectedScreenMapId;
    bgMapLoaded_ = false;
    floorGraphics_ = {};
    wallGraphics_ = {};

    const auto mapPath = context.assets.gameMapPath() / (context.selectedScreenMapId + ".admap");
    bgMapLoaded_ = game::loadTileMap(mapPath, bgMap_, nullptr);

    auto loadLayer = [&](const std::filesystem::path& p, PixelLayer& layer) {
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(p.string().c_str(), &w, &h, &ch, 4);
        if (!data) {
            return;
        }
        layer.width = w;
        layer.height = h;
        layer.pixels.resize(static_cast<std::size_t>(w * h));
        for (int i = 0; i < w * h; ++i) {
            const unsigned char* px = data + i * 4;
            layer.pixels[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(px[3]) << 24u) |
                (static_cast<std::uint32_t>(px[2]) << 16u) |
                (static_cast<std::uint32_t>(px[1]) << 8u) |
                static_cast<std::uint32_t>(px[0]);
        }
        layer.loaded = true;
        stbi_image_free(data);
    };

    const auto tilesetDir = context.assets.gameTilesetPath();
    loadLayer(tilesetDir / (context.selectedScreenMapId + "_floor.png"), floorGraphics_);
    loadLayer(tilesetDir / (context.selectedScreenMapId + "_wall.png"), wallGraphics_);
}

void ItemPlacementPanel::draw(EditorContext& context)
{
    drawPlacement(context);
}

void ItemPlacementPanel::drawPlacement(EditorContext& context)
{
    loadBackground(context);

    drawToolbar(context);
    ImGui::Separator();

    const float leftW = 240.0f;
    const float availableH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("ItemLeftColumn", ImVec2(leftW, availableH), false);
    drawPlacementDefaults(context);
    ImGui::Separator();
    ImGui::BeginChild("ItemListRegion", ImVec2(0.0f, 0.0f), true);
    drawItemList(context);
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ItemCanvas", ImVec2(0.0f, availableH), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void ItemPlacementPanel::drawEdit(EditorContext& context)
{
    ImGui::BeginChild("ItemEditInspector", ImVec2(420.0f, 0.0f), true);
    drawInspector(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ItemEditList", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("Screen Items");
    ImGui::Separator();
    drawItemList(context);
    ImGui::EndChild();
}

void ItemPlacementPanel::drawToolbar(EditorContext& context)
{
    ImGui::Text("Screen: %s", context.selectedScreenId.empty() ? "(none)" : context.selectedScreenId.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Zoom##itemzoom", &zoom_, 0.05f, 0.5f, 4.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("Click canvas to place | Right-click to delete");
}

void ItemPlacementPanel::drawPlacementDefaults(EditorContext& context)
{
    ImGui::TextUnformatted("Place Item");

    const char* typeItems[] = { "Weapon Pickup", "Ammo Pickup", "Health Pickup", "Project Item" };
    if (ImGui::Combo("New item type##place_type", &pickupType_, typeItems, 4)) {
        if (pickupType_ == 0 && !context.weaponDefs.empty()) {
            const game::WeaponDef& weapon = context.weaponDefs.front();
            copyToBuffer(targetId_, weapon.id);
            copyToBuffer(spriteId_, weapon.spriteId);
        }
    }

    if (pickupType_ == 0) {
        const std::string selectedTarget(targetId_.data());
        const char* preview = selectedTarget.empty() ? "(choose weapon)" : selectedTarget.c_str();
        if (ImGui::BeginCombo("Weapon##place_weapon", preview)) {
            for (const game::WeaponDef& weapon : context.weaponDefs) {
                const bool selected = weapon.id == selectedTarget;
                if (ImGui::Selectable(weapon.id.c_str(), selected)) {
                    copyToBuffer(targetId_, weapon.id);
                    copyToBuffer(spriteId_, weapon.spriteId);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (context.weaponDefs.empty()) {
            ImGui::TextDisabled("Define weapons in the Weapons tab.");
        }
    } else if (pickupType_ == 1) {
        ImGui::InputText("Ammo type ID##place_ammo", targetId_.data(), targetId_.size());
        ImGui::DragInt("Quantity##place_qty", &quantity_, 1.0f, 1, 999);
    } else if (pickupType_ == 2) {
        ImGui::DragInt("HP restore##place_hp", &quantity_, 1.0f, 1, 99);
    } else {
        const std::string selectedTarget(targetId_.data());
        const char* preview = selectedTarget.empty() ? "(choose item)" : selectedTarget.c_str();
        if (ImGui::BeginCombo("Item##place_item", preview)) {
            for (const game::ItemDef& item : context.itemDefs) {
                if (ImGui::Selectable(item.id.c_str(), item.id == selectedTarget)) {
                    copyToBuffer(targetId_, item.id);
                    copyToBuffer(spriteId_, item.spriteId);
                    quantity_ = std::max(1, item.value);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt("Quantity##place_item_qty", &quantity_, 1.0f, 1, 999);
    }

    ImGui::InputText("Pickup sprite ID##place_sprite", spriteId_.data(), spriteId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Edit Sprite##place_pickup_sprite")) {
        std::string spriteId(spriteId_.data());
        if (spriteId.empty()) {
            spriteId = pickupType_ == 0 && targetId_[0] != '\0' ? std::string(targetId_.data()) + "_pickup" : "item_pickup";
            copyToBuffer(spriteId_, spriteId);
        }
        context.requestedSpriteReference = (context.assets.gameSpritePath() / (spriteId + ".sprite.json")).generic_string();
        context.requestEditSprite = true;
    }
    ImGui::TextDisabled("This is the map pickup visual, not the weapon or projectile sprite.");
}

void ItemPlacementPanel::drawItemList(EditorContext& context)
{
    if (ImGui::Button("Add Item")) {
        game::MapItemPlacement item;
        item.id = "item_" + std::to_string(context.selectedScreenItems.size() + 1);
        item.pickupType = game::ItemPickupType::Ammo;
        item.quantity = 5;
        item.x = static_cast<float>(game::kScreenTilesW * game::kTileSize / 2);
        item.y = static_cast<float>(game::kScreenTilesH * game::kTileSize / 2);
        context.selectedScreenItems.push_back(item);
        selectedItem_ = static_cast<int>(context.selectedScreenItems.size()) - 1;
        syncInspectorFromSelected(context);
        context.markDirty();
    }
    if (selectedItem_ >= 0 && selectedItem_ < static_cast<int>(context.selectedScreenItems.size())) {
        ImGui::SameLine();
        if (ImGui::Button("Edit Item")) {
            flushSelectedInspector(context);
            context.requestEditItemDetails = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit Sprite")) {
            requestEditPickupSprite(context);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            context.selectedScreenItems.erase(context.selectedScreenItems.begin() + selectedItem_);
            selectedItem_ = std::min(selectedItem_, static_cast<int>(context.selectedScreenItems.size()) - 1);
            if (selectedItem_ >= 0) {
                syncInspectorFromSelected(context);
            }
            context.markDirty();
        }
    }

    for (int i = 0; i < static_cast<int>(context.selectedScreenItems.size()); ++i) {
        const game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool sel = (selectedItem_ == i);
        const std::string label = std::string(pickupTypeLabel(static_cast<int>(item.pickupType))) + ": " + item.id;
        if (ImGui::Selectable(label.c_str(), sel)) {
            if (selectedItem_ != i) {
                if (selectedItem_ >= 0) {
                    writeInspectorToSelected(context);
                }
                selectedItem_ = i;
                syncInspectorFromSelected(context);
            }
        }
        ImGui::PopID();
    }
}

void ItemPlacementPanel::drawInspector(EditorContext& context)
{
    if (selectedItem_ < 0 || selectedItem_ >= static_cast<int>(context.selectedScreenItems.size())) {
        ImGui::TextDisabled("No item selected");
        return;
    }

    ImGui::TextUnformatted("Item Properties");
    if (ImGui::InputText("ID##iid", itemId_.data(), itemId_.size())) { context.markDirty(); }

    const char* typeItems[] = { "Weapon", "Ammo", "Health", "Project Item" };
    if (ImGui::Combo("Type##itype", &pickupType_, typeItems, 4)) { context.markDirty(); }

    if (pickupType_ == 0) {
        const std::string selectedTarget(targetId_.data());
        const char* preview = selectedTarget.empty() ? "(choose weapon)" : selectedTarget.c_str();
        if (ImGui::BeginCombo("Weapon ID##itarget_combo", preview)) {
            for (const game::WeaponDef& weapon : context.weaponDefs) {
                const bool selected = weapon.id == selectedTarget;
                if (ImGui::Selectable(weapon.id.c_str(), selected)) {
                    copyToBuffer(targetId_, weapon.id);
                    copyToBuffer(spriteId_, weapon.spriteId);
                    context.markDirty();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (context.weaponDefs.empty()) {
            if (ImGui::InputText("Weapon ID##itarget", targetId_.data(), targetId_.size())) { context.markDirty(); }
        }
    } else if (pickupType_ == 1) {
        if (ImGui::InputText("Ammo type ID##itarget", targetId_.data(), targetId_.size())) { context.markDirty(); }
        if (ImGui::DragInt("Quantity##iqty", &quantity_, 1.0f, 1, 999)) { context.markDirty(); }
    } else if (pickupType_ == 2) {
        if (ImGui::DragInt("HP restore##iqty", &quantity_, 1.0f, 1, 99)) { context.markDirty(); }
    } else {
        const std::string selectedTarget(targetId_.data());
        const char* preview = selectedTarget.empty() ? "(choose item)" : selectedTarget.c_str();
        if (ImGui::BeginCombo("Item ID##itarget_item_combo", preview)) {
            for (const game::ItemDef& item : context.itemDefs) {
                if (ImGui::Selectable(item.id.c_str(), item.id == selectedTarget)) {
                    copyToBuffer(targetId_, item.id);
                    copyToBuffer(spriteId_, item.spriteId);
                    quantity_ = std::max(1, item.value);
                    context.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (context.itemDefs.empty()) {
            if (ImGui::InputText("Item ID##itarget_item", targetId_.data(), targetId_.size())) { context.markDirty(); }
        }
        if (ImGui::DragInt("Quantity##iqty_item", &quantity_, 1.0f, 1, 999)) { context.markDirty(); }
    }

    if (ImGui::Checkbox("Respawn##iresp", &respawn_)) { context.markDirty(); }
    if (ImGui::InputText("Pickup sprite ID##isprite", spriteId_.data(), spriteId_.size())) { context.markDirty(); }
    ImGui::SameLine();
    if (ImGui::Button("Edit Pickup Sprite##item_sprite")) {
        requestEditPickupSprite(context);
    }
    if (pickupType_ == 0) {
        const game::WeaponDef* weapon = findWeaponDef(context, targetId_.data());
        ImGui::TextDisabled("Weapon sprite: %s | Ammo sprite: %s",
            weapon == nullptr || weapon->spriteId.empty() ? "(none)" : weapon->spriteId.c_str(),
            weapon == nullptr || weapon->ammoSpriteId.empty() ? "(uses weapon sprite)" : weapon->ammoSpriteId.c_str());
    }

    if (ImGui::Button("Apply##iapply", ImVec2(-1.0f, 0.0f))) {
        writeInspectorToSelected(context);
    }
}

void ItemPlacementPanel::flushSelectedInspector(EditorContext& context)
{
    writeInspectorToSelected(context);
}

void ItemPlacementPanel::requestEditPickupSprite(EditorContext& context)
{
    if (selectedItem_ < 0 || selectedItem_ >= static_cast<int>(context.selectedScreenItems.size())) {
        return;
    }

    writeInspectorToSelected(context);
    game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(selectedItem_)];
    if (item.spriteId.empty()) {
        item.spriteId = item.id.empty() ? "item_pickup" : item.id;
        copyToBuffer(spriteId_, item.spriteId);
        context.markDirty();
    }

    context.requestedSpriteReference = (context.assets.gameSpritePath() / (item.spriteId + ".sprite.json")).generic_string();
    context.requestEditSprite = true;
}

void ItemPlacementPanel::drawCanvas(EditorContext& context)
{
    const float worldW = static_cast<float>(game::kScreenTilesW * game::kTileSize);
    const float worldH = static_cast<float>(game::kScreenTilesH * game::kTileSize);
    const float canvasW = worldW * zoom_;
    const float canvasH = worldH * zoom_;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("ItemCanvas", ImVec2(canvasW, canvasH),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    // Background
    dl->AddRectFilled(origin, ImVec2(origin.x + canvasW, origin.y + canvasH), IM_COL32(22, 26, 30, 255));

    drawPixelLayer(dl, origin, floorGraphics_, canvasW, canvasH, 0.85f);

    if (bgMapLoaded_) {
        for (int ty = 0; ty < bgMap_.height; ++ty) {
            for (int tx = 0; tx < bgMap_.width; ++tx) {
                const std::size_t idx = static_cast<std::size_t>(ty) * static_cast<std::size_t>(bgMap_.width) + static_cast<std::size_t>(tx);
                if (bgMap_.layers[1][idx] != 0u) {
                    const float px = origin.x + static_cast<float>(tx * game::kTileSize) * zoom_;
                    const float py = origin.y + static_cast<float>(ty * game::kTileSize) * zoom_;
                    const float sz = static_cast<float>(game::kTileSize) * zoom_;
                    dl->AddRectFilled(ImVec2(px, py), ImVec2(px + sz, py + sz), IM_COL32(240, 200, 40, 30));
                }
            }
        }
    }

    drawPixelLayer(dl, origin, wallGraphics_, canvasW, canvasH, 0.75f);

    // Grid
    for (int tx = 0; tx <= game::kScreenTilesW; ++tx) {
        const float px = origin.x + static_cast<float>(tx * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + canvasH), IM_COL32(60, 60, 60, 60));
    }
    for (int ty = 0; ty <= game::kScreenTilesH; ++ty) {
        const float py = origin.y + static_cast<float>(ty * game::kTileSize) * zoom_;
        dl->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + canvasW, py), IM_COL32(60, 60, 60, 60));
    }

    // Draw items
    for (int i = 0; i < static_cast<int>(context.selectedScreenItems.size()); ++i) {
        const game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(i)];
        const ImVec2 cp = worldToCanvas(origin, item.x, item.y, zoom_);
        const float r = 7.0f * zoom_;
        ImU32 col = IM_COL32(240, 210, 30, 220);
        if (item.pickupType == game::ItemPickupType::Ammo) { col = IM_COL32(30, 190, 255, 220); }
        else if (item.pickupType == game::ItemPickupType::Health) { col = IM_COL32(40, 220, 60, 220); }
        else if (item.pickupType == game::ItemPickupType::ProjectItem) { col = IM_COL32(190, 120, 255, 220); }

        dl->AddQuadFilled(
            ImVec2(cp.x, cp.y - r), ImVec2(cp.x + r, cp.y),
            ImVec2(cp.x, cp.y + r), ImVec2(cp.x - r, cp.y), col);

        if (i == selectedItem_) {
            dl->AddQuad(
                ImVec2(cp.x, cp.y - r - 2.0f), ImVec2(cp.x + r + 2.0f, cp.y),
                ImVec2(cp.x, cp.y + r + 2.0f), ImVec2(cp.x - r - 2.0f, cp.y),
                IM_COL32(255, 255, 255, 200), 1.5f);
        }
    }

    // Mouse interactions
    if (hovered) {
        const ImVec2 worldPos = canvasToWorld(origin, mousePos, zoom_);

        // Right click: delete closest item
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            float bestDist = 12.0f * zoom_;
            int bestIdx = -1;
            for (int i = 0; i < static_cast<int>(context.selectedScreenItems.size()); ++i) {
                const game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(i)];
                const ImVec2 cp = worldToCanvas(origin, item.x, item.y, zoom_);
                const float dx = mousePos.x - cp.x;
                const float dy = mousePos.y - cp.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < bestDist) { bestDist = dist; bestIdx = i; }
            }
            if (bestIdx >= 0) {
                context.selectedScreenItems.erase(context.selectedScreenItems.begin() + bestIdx);
                if (selectedItem_ >= static_cast<int>(context.selectedScreenItems.size())) {
                    selectedItem_ = static_cast<int>(context.selectedScreenItems.size()) - 1;
                }
                context.markDirty();
            }
        }

        // Left click: select or place
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float bestDist = 12.0f * zoom_;
            int bestIdx = -1;
            for (int i = 0; i < static_cast<int>(context.selectedScreenItems.size()); ++i) {
                const game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(i)];
                const ImVec2 cp = worldToCanvas(origin, item.x, item.y, zoom_);
                const float dx = mousePos.x - cp.x;
                const float dy = mousePos.y - cp.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < bestDist) { bestDist = dist; bestIdx = i; }
            }
            if (bestIdx >= 0) {
                if (selectedItem_ >= 0) {
                    writeInspectorToSelected(context);
                }
                selectedItem_ = bestIdx;
                syncInspectorFromSelected(context);
            } else {
                placeItemAt(context, worldPos.x, worldPos.y);
            }
        }

        // Drag selected item
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && selectedItem_ >= 0 &&
            selectedItem_ < static_cast<int>(context.selectedScreenItems.size())) {
            game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(selectedItem_)];
            item.x = std::clamp(worldPos.x, 0.0f, worldW);
            item.y = std::clamp(worldPos.y, 0.0f, worldH);
            context.markDirty();
        }
    }
}

void ItemPlacementPanel::placeItemAt(EditorContext& context, float worldX, float worldY)
{
    if (selectedItem_ >= 0) {
        writeInspectorToSelected(context);
    }
    game::MapItemPlacement item;
    item.id = "item_" + std::to_string(context.selectedScreenItems.size() + 1);
    item.pickupType = static_cast<game::ItemPickupType>(pickupType_);
    item.targetId = targetId_.data();
    item.quantity = quantity_;
    item.respawn = respawn_;
    item.spriteId = spriteId_.data();
    item.x = worldX;
    item.y = worldY;
    context.selectedScreenItems.push_back(item);
    selectedItem_ = static_cast<int>(context.selectedScreenItems.size()) - 1;
    syncInspectorFromSelected(context);
    context.markDirty();
}

void ItemPlacementPanel::syncInspectorFromSelected(const EditorContext& context)
{
    if (selectedItem_ < 0 || selectedItem_ >= static_cast<int>(context.selectedScreenItems.size())) {
        return;
    }
    const game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(selectedItem_)];
    copyToBuffer(itemId_, item.id);
    pickupType_ = static_cast<int>(item.pickupType);
    copyToBuffer(targetId_, item.targetId);
    quantity_ = item.quantity;
    respawn_ = item.respawn;
    copyToBuffer(spriteId_, item.spriteId);
}

void ItemPlacementPanel::writeInspectorToSelected(EditorContext& context)
{
    if (selectedItem_ < 0 || selectedItem_ >= static_cast<int>(context.selectedScreenItems.size())) {
        return;
    }
    game::MapItemPlacement& item = context.selectedScreenItems[static_cast<std::size_t>(selectedItem_)];
    item.id = itemId_.data();
    item.pickupType = static_cast<game::ItemPickupType>(pickupType_);
    item.targetId = targetId_.data();
    item.quantity = quantity_;
    item.respawn = respawn_;
    item.spriteId = spriteId_.data();
}

void ItemPlacementPanel::drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const
{
    if (!layer.loaded || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const float sx = targetW / static_cast<float>(layer.width);
    const float sy = targetH / static_cast<float>(layer.height);
    const int cellW = std::max(1, static_cast<int>(sx));
    const int cellH = std::max(1, static_cast<int>(sy));
    const int alpha = static_cast<int>(opacity * 255.0f);

    for (int y = 0; y < layer.height; y += std::max(1, cellH)) {
        for (int x = 0; x < layer.width; x += std::max(1, cellW)) {
            const std::uint32_t pixel = layer.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(x)];
            const int r = static_cast<int>(pixel & 0xffu);
            const int g = static_cast<int>((pixel >> 8u) & 0xffu);
            const int b = static_cast<int>((pixel >> 16u) & 0xffu);
            const int a = static_cast<int>(static_cast<float>((pixel >> 24u) & 0xffu) * opacity);
            if (a < 4) { continue; }
            const float px = origin.x + static_cast<float>(x) * sx;
            const float py = origin.y + static_cast<float>(y) * sy;
            dl->AddRectFilled(ImVec2(px, py), ImVec2(px + static_cast<float>(cellW) + 0.5f, py + static_cast<float>(cellH) + 0.5f),
                IM_COL32(r, g, b, std::min(a, alpha)));
        }
    }
}

} // namespace adventure::editor
