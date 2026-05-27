#include "editor/panels/weapon_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/project.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace adventure::editor {
namespace {

void copyToBuffer(std::array<char, 64>& buf, const std::string& value)
{
    std::memset(buf.data(), 0, buf.size());
    const std::size_t len = std::min(value.size(), buf.size() - 1);
    std::memcpy(buf.data(), value.data(), len);
}

} // namespace

void WeaponEditorPanel::loadWeapons(EditorContext& context)
{
    game::GameProject project;
    if (game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr)) {
        context.weaponDefs = project.weaponDefs;
        context.startingWeaponId = project.startingWeaponId;
    }
    projectLoaded_ = true;
    selectedWeapon_ = -1;
}

void WeaponEditorPanel::saveWeapons(EditorContext& context)
{
    game::GameProject project;
    game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
    project.weaponDefs = context.weaponDefs;
    project.startingWeaponId = context.startingWeaponId;
    game::saveGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
}

void WeaponEditorPanel::draw(EditorContext& context)
{
    if (!projectLoaded_) {
        loadWeapons(context);
    }

    ImGui::TextUnformatted("Weapon Definitions");
    ImGui::Separator();

    const float listW = 200.0f;
    ImGui::BeginChild("WeaponList", ImVec2(listW, 0.0f), true);

    for (int i = 0; i < static_cast<int>(context.weaponDefs.size()); ++i) {
        const game::WeaponDef& w = context.weaponDefs[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool sel = (selectedWeapon_ == i);
        const char* typeLabel = (w.type == game::WeaponType::Ranged) ? "[R]" : "[M]";
        if (ImGui::Selectable((std::string(typeLabel) + " " + w.id).c_str(), sel)) {
            if (selectedWeapon_ != i) {
                if (selectedWeapon_ >= 0) {
                    writeInspectorToSelected(context);
                }
                selectedWeapon_ = i;
                syncInspectorFromSelected(context);
            }
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("WeaponInspector", ImVec2(0.0f, 0.0f), false);

    // Add / Delete
    if (ImGui::Button("Add Weapon")) {
        if (selectedWeapon_ >= 0) {
            writeInspectorToSelected(context);
        }
        game::WeaponDef w;
        w.id = "weapon_" + std::to_string(context.weaponDefs.size() + 1);
        context.weaponDefs.push_back(w);
        selectedWeapon_ = static_cast<int>(context.weaponDefs.size()) - 1;
        syncInspectorFromSelected(context);
        context.markDirty();
    }
    ImGui::SameLine();
    if (selectedWeapon_ >= 0 && ImGui::Button("Delete")) {
        context.weaponDefs.erase(context.weaponDefs.begin() + selectedWeapon_);
        selectedWeapon_ = std::min(selectedWeapon_, static_cast<int>(context.weaponDefs.size()) - 1);
        if (selectedWeapon_ >= 0) {
            syncInspectorFromSelected(context);
        }
        context.markDirty();
    }

    if (selectedWeapon_ >= 0 && selectedWeapon_ < static_cast<int>(context.weaponDefs.size())) {
        ImGui::Separator();
        ImGui::TextUnformatted("Edit Weapon");

        if (ImGui::InputText("ID##wid", weaponId_.data(), weaponId_.size())) { context.markDirty(); }
        const char* typeItems[] = { "Melee", "Ranged" };
        if (ImGui::Combo("Type", &weaponType_, typeItems, 2)) { context.markDirty(); }
        if (ImGui::DragInt("Damage", &damage_, 0.1f, 1, 100)) { context.markDirty(); }
        if (ImGui::DragFloat("Range (px)", &range_, 1.0f, 8.0f, 1000.0f)) { context.markDirty(); }
        if (ImGui::DragFloat("Cooldown (s)", &attackCooldown_, 0.01f, 0.05f, 5.0f)) { context.markDirty(); }

        if (weaponType_ == 1) {
            ImGui::Separator();
            ImGui::TextDisabled("Ranged");
            if (ImGui::DragFloat("Projectile speed", &projectileSpeed_, 1.0f, 50.0f, 2000.0f)) { context.markDirty(); }
            if (ImGui::InputText("Ammo type ID", ammoTypeId_.data(), ammoTypeId_.size())) { context.markDirty(); }
            if (ImGui::DragInt("Ammo per shot", &ammoPerShot_, 0.1f, 1, 100)) { context.markDirty(); }
        }

        ImGui::Separator();
        if (ImGui::InputText("Sprite ID", spriteId_.data(), spriteId_.size())) { context.markDirty(); }
        ImGui::SameLine();
        if (ImGui::Button("Edit Sprite##weapon_sprite")) {
            writeInspectorToSelected(context);
            const std::string spriteId(spriteId_.data());
            if (!spriteId.empty()) {
                context.requestedSpriteReference = (context.assets.gameSpritePath() / (spriteId + ".sprite.json")).generic_string();
                context.requestEditSprite = true;
            }
        }

        if (ImGui::Button("Apply", ImVec2(-1.0f, 30.0f))) {
            writeInspectorToSelected(context);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Starting weapon");

    std::array<char, 64> startBuf{};
    copyToBuffer(startBuf, context.startingWeaponId);
    if (ImGui::InputText("Starting weapon ID", startBuf.data(), startBuf.size())) {
        context.startingWeaponId = startBuf.data();
        context.markDirty();
    }
    ImGui::TextDisabled("Leave blank for none. Player starts with this weapon equipped.");

    ImGui::Separator();
    if (ImGui::Button("Save Weapons", ImVec2(-1.0f, 34.0f))) {
        if (selectedWeapon_ >= 0) {
            writeInspectorToSelected(context);
        }
        saveWeapons(context);
        context.dirty = false;
    }

    ImGui::EndChild();
}

void WeaponEditorPanel::syncInspectorFromSelected(const EditorContext& context)
{
    if (selectedWeapon_ < 0 || selectedWeapon_ >= static_cast<int>(context.weaponDefs.size())) {
        return;
    }
    const game::WeaponDef& w = context.weaponDefs[static_cast<std::size_t>(selectedWeapon_)];
    copyToBuffer(weaponId_, w.id);
    weaponType_ = (w.type == game::WeaponType::Ranged) ? 1 : 0;
    damage_ = w.damage;
    range_ = w.range;
    attackCooldown_ = w.attackCooldown;
    projectileSpeed_ = w.projectileSpeed;
    copyToBuffer(spriteId_, w.spriteId);
    copyToBuffer(ammoTypeId_, w.ammoTypeId);
    ammoPerShot_ = w.ammoPerShot;
}

void WeaponEditorPanel::writeInspectorToSelected(EditorContext& context)
{
    if (selectedWeapon_ < 0 || selectedWeapon_ >= static_cast<int>(context.weaponDefs.size())) {
        return;
    }
    game::WeaponDef& w = context.weaponDefs[static_cast<std::size_t>(selectedWeapon_)];
    w.id = weaponId_.data();
    w.type = (weaponType_ == 1) ? game::WeaponType::Ranged : game::WeaponType::Melee;
    w.damage = damage_;
    w.range = range_;
    w.attackCooldown = attackCooldown_;
    w.projectileSpeed = projectileSpeed_;
    w.spriteId = spriteId_.data();
    w.ammoTypeId = ammoTypeId_.data();
    w.ammoPerShot = ammoPerShot_;
}

} // namespace adventure::editor
