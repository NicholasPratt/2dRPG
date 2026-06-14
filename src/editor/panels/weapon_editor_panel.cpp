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

std::optional<std::filesystem::path> WeaponEditorPanel::draw(EditorContext& context)
{
    std::optional<std::filesystem::path> spriteToOpen;

    const std::string currentRoot = context.assets.projectRoot.string();
    if (!projectLoaded_ || lastLoadedProjectRoot_ != currentRoot) {
        loadWeapons(context);
        lastLoadedProjectRoot_ = currentRoot;
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

        if (ui::inputTextString("ID##wid", weaponId_.data(), weaponId_.size())) { context.markDirty(); }
        const char* typeItems[] = { "Melee", "Ranged" };
        if (ImGui::Combo("Type", &weaponType_, typeItems, 2)) { context.markDirty(); }
        if (ImGui::DragInt("Damage", &damage_, 0.1f, 1, 100)) { context.markDirty(); }
        if (ImGui::DragFloat("Range (px)", &range_, 1.0f, 8.0f, 1000.0f)) { context.markDirty(); }
        if (ImGui::DragFloat("Cooldown (s)", &attackCooldown_, 0.01f, 0.05f, 5.0f)) { context.markDirty(); }

        // Player animation action played when attacking with this weapon.
        {
            const std::string current(attackAnim_.data());
            const char* defaultLabel = "Default (attack_1 melee / cast ranged)";
            const char* preview = current.empty() ? defaultLabel : attackAnim_.data();
            if (ImGui::BeginCombo("Attack animation", preview)) {
                if (ImGui::Selectable(defaultLabel, current.empty())) {
                    copyToBuffer(attackAnim_, "");
                    context.markDirty();
                }
                static const char* kAttackAnimOptions[] = {
                    "attack_1", "attack_2", "attack_3", "attack_4", "attack_5",
                    "attack_6", "attack_7", "attack_8", "attack_9", "attack_10",
                    "attack_11", "attack_12", "attack_13", "attack_14", "attack_15",
                    "attack_16", "attack_17", "attack_18", "attack_19", "attack_20",
                    "cast",
                };
                bool currentListed = current.empty();
                for (const char* option : kAttackAnimOptions) {
                    if (ImGui::Selectable(option, current == option)) {
                        copyToBuffer(attackAnim_, option);
                        context.markDirty();
                    }
                    if (current == option) { currentListed = true; }
                }
                if (!currentListed) {
                    ImGui::Selectable(current.c_str(), true);  // keep an unknown custom value visible
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which player sprite animation plays when attacking with this weapon.\nMust match a frame action type authored in the Sprite editor (attack_1 through attack_20, or cast).");
        }

        if (weaponType_ == 1) {
            ImGui::Separator();
            ImGui::TextDisabled("Ranged");
            if (ImGui::DragFloat("Projectile speed", &projectileSpeed_, 1.0f, 50.0f, 2000.0f)) { context.markDirty(); }
            if (ui::inputTextString("Ammo type ID", ammoTypeId_.data(), ammoTypeId_.size())) { context.markDirty(); }
            if (ImGui::DragInt("Ammo per shot", &ammoPerShot_, 0.1f, 1, 100)) { context.markDirty(); }
            const char* wallItems[] = { "Break (arrow/bullet)", "Rebound (slingshot stone)" };
            if (ImGui::Combo("On wall hit", &wallBehavior_, wallItems, 2)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Break: projectile vanishes on a wall.\nRebound: bounces, loses energy, then settles on the ground.");

            ImGui::Separator();
            ImGui::TextDisabled("Draw / Charge");
            ImGui::TextDisabled("Charge or steady time > 0 makes this a hold-to-draw weapon: hold fire to draw, release to shoot.");
            if (ImGui::DragFloat("Charge time (s)", &chargeTime_, 0.01f, 0.0f, 10.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Seconds to reach full draw. 0 = fires instantly on press.");
            if (ImGui::DragFloat("Damage scale (tap)", &chargeDamageMin_, 0.01f, 0.0f, 10.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Damage multiplier for an instant tap shot.");
            if (ImGui::DragFloat("Damage scale (full draw)", &chargeDamageMax_, 0.01f, 0.0f, 10.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Damage multiplier at full draw (slingshot: e.g. 2.0).");
            if (ImGui::DragFloat("Overhold grace (s)", &overchargeTime_, 0.01f, 0.0f, 10.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long the weapon can be held past full draw before the overhold effect kicks in.");
            const char* overItems[] = { "None (hold forever)", "Wild shot (accuracy decays)", "Misfire (shot fizzles)", "Break (weapon breaks)" };
            if (ImGui::Combo("Overhold effect", &overchargeEffect_, overItems, 4)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("What happens when held past full draw + grace.\nWild shot: spread keeps growing.\nMisfire: ammo spent, shot does nothing.\nBreak: the weapon is destroyed and unequipped.");

            ImGui::Separator();
            ImGui::TextDisabled("Accuracy");
            if (ImGui::DragFloat("Spread start (deg)", &spreadStart_, 0.1f, 0.0f, 180.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shot direction error when fired immediately. Shotgun: high. 0 = perfectly straight.");
            if (ImGui::DragFloat("Spread end (deg)", &spreadEnd_, 0.1f, 0.0f, 180.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spread after holding steady for the full steady time. Sniper: start high, end 0.");
            if (ImGui::DragFloat("Steady time (s)", &steadyTime_, 0.01f, 0.0f, 10.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold time for spread to move from start to end. 0 = spread stays at start value.");
            if (ImGui::DragInt("Pellets per shot", &pelletCount_, 0.1f, 1, 24)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projectiles per shot, each with its own spread roll (shotgun: 6+). Ammo cost is per shot, not per pellet.");

            ImGui::Separator();
            ImGui::TextDisabled("Damage falloff");
            if (ImGui::DragFloat("Falloff start (px)", &falloffStart_, 1.0f, 0.0f, 1000.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Full damage up to this flight distance. 0 = no falloff.");
            if (ImGui::DragFloat("Falloff end (px)", &falloffEnd_, 1.0f, 0.0f, 1000.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance where damage bottoms out at the minimum scale.");
            if (ImGui::DragFloat("Min damage scale", &falloffMinScale_, 0.01f, 0.0f, 1.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Damage multiplier at and beyond falloff end (shotgun: e.g. 0.2).");

            ImGui::Separator();
            ImGui::TextDisabled("Auto-aim");
            if (ImGui::DragFloat("Aim cone (deg)", &aimCone_, 0.5f, 0.0f, 360.0f)) { context.markDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enemies inside this cone (centred on player facing, out to weapon range) are selectable targets.\nTab/Q or controller bumpers cycle targets. 0 disables auto-aim.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Weapon Sprite");
        ImGui::TextDisabled("Visual for the weapon definition.");
        if (ui::inputTextString("Weapon sprite ID", spriteId_.data(), spriteId_.size())) { context.markDirty(); }
        ImGui::SameLine();
        if (ImGui::Button("Edit Weapon Sprite##weapon_sprite")) {
            std::string spriteId(spriteId_.data());
            if (spriteId.empty()) {
                spriteId = weaponId_.data();
                copyToBuffer(spriteId_, spriteId);
                context.markDirty();
            }
            writeInspectorToSelected(context);
            if (!spriteId.empty()) {
                spriteToOpen = context.assets.gameSpritePath() / (spriteId + ".sprite.json");
            }
        }

        if (weaponType_ == 1) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Ammo Projectile Sprite");
            ImGui::TextDisabled("Visual for shots fired by this ranged weapon. Leave blank to use the weapon sprite.");
            if (ui::inputTextString("Ammo sprite ID", ammoSpriteId_.data(), ammoSpriteId_.size())) { context.markDirty(); }
            ImGui::SameLine();
            if (ImGui::Button("Edit Ammo Sprite##weapon_ammo_sprite")) {
                std::string ammoSpriteId(ammoSpriteId_.data());
                if (ammoSpriteId.empty()) {
                    ammoSpriteId = std::string(weaponId_.data()) + "_ammo";
                    copyToBuffer(ammoSpriteId_, ammoSpriteId);
                    context.markDirty();
                }
                writeInspectorToSelected(context);
                if (!ammoSpriteId.empty()) {
                    spriteToOpen = context.assets.gameSpritePath() / (ammoSpriteId + ".sprite.json");
                }
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
    if (ui::inputTextString("Starting weapon ID", startBuf.data(), startBuf.size())) {
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
    return spriteToOpen;
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
    copyToBuffer(ammoSpriteId_, w.ammoSpriteId);
    copyToBuffer(attackAnim_, w.attackAnimState);
    ammoPerShot_ = w.ammoPerShot;
    wallBehavior_ = (w.wallBehavior == game::ProjectileWallBehavior::Rebound) ? 1 : 0;
    chargeTime_ = w.chargeTimeSeconds;
    chargeDamageMin_ = w.chargeDamageScaleMin;
    chargeDamageMax_ = w.chargeDamageScaleMax;
    overchargeTime_ = w.overchargeTimeSeconds;
    overchargeEffect_ = static_cast<int>(w.overchargeEffect);
    spreadStart_ = w.spreadStartDegrees;
    spreadEnd_ = w.spreadEndDegrees;
    steadyTime_ = w.steadyTimeSeconds;
    pelletCount_ = w.pelletCount;
    falloffStart_ = w.falloffStartPx;
    falloffEnd_ = w.falloffEndPx;
    falloffMinScale_ = w.falloffMinDamageScale;
    aimCone_ = w.aimConeDegrees;
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
    w.ammoSpriteId = ammoSpriteId_.data();
    w.attackAnimState = attackAnim_.data();
    w.ammoPerShot = ammoPerShot_;
    w.wallBehavior = (wallBehavior_ == 1) ? game::ProjectileWallBehavior::Rebound : game::ProjectileWallBehavior::Break;
    w.chargeTimeSeconds = std::max(0.0f, chargeTime_);
    w.chargeDamageScaleMin = std::max(0.0f, chargeDamageMin_);
    w.chargeDamageScaleMax = std::max(0.0f, chargeDamageMax_);
    w.overchargeTimeSeconds = std::max(0.0f, overchargeTime_);
    w.overchargeEffect = static_cast<game::OverchargeEffect>(std::clamp(overchargeEffect_, 0, 3));
    w.spreadStartDegrees = std::max(0.0f, spreadStart_);
    w.spreadEndDegrees = std::max(0.0f, spreadEnd_);
    w.steadyTimeSeconds = std::max(0.0f, steadyTime_);
    w.pelletCount = std::max(1, pelletCount_);
    w.falloffStartPx = std::max(0.0f, falloffStart_);
    w.falloffEndPx = std::max(0.0f, falloffEnd_);
    w.falloffMinDamageScale = std::clamp(falloffMinScale_, 0.0f, 1.0f);
    w.aimConeDegrees = std::clamp(aimCone_, 0.0f, 360.0f);
}

} // namespace adventure::editor
