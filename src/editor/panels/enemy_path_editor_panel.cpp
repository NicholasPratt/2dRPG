#include "editor/panels/enemy_path_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
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

constexpr float kPreviewPanelW = 128.0f;
constexpr float kPreviewPanelH = 128.0f;
const char* kAttackTypeNames[] = {"Contact", "Melee", "Ranged"};

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

std::vector<std::uint32_t> rgbaToPixels(const unsigned char* rgba, int width, int height)
{
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int i = 0; i < width * height; ++i) {
        const int j = i * 4;
        pixels[static_cast<std::size_t>(i)] =
            (static_cast<std::uint32_t>(rgba[j + 3]) << 24) |
            (static_cast<std::uint32_t>(rgba[j + 2]) << 16) |
            (static_cast<std::uint32_t>(rgba[j + 1]) << 8) |
             static_cast<std::uint32_t>(rgba[j + 0]);
    }
    return pixels;
}

std::uint8_t alphaOf(std::uint32_t color)
{
    return static_cast<std::uint8_t>((color >> 24u) & 0xffu);
}

ImU32 packedColor(std::uint32_t color, float opacity)
{
    const auto alpha = static_cast<std::uint8_t>(static_cast<float>(alphaOf(color)) * std::clamp(opacity, 0.0f, 1.0f));
    return IM_COL32((color >> 0u) & 0xffu, (color >> 8u) & 0xffu, (color >> 16u) & 0xffu, alpha);
}

} // namespace

void EnemyPathEditorPanel::draw(EditorContext& context)
{
    const std::string currentRoot = context.assets.projectRoot.string();
    if (!projectLoaded_ || lastLoadedProjectRoot_ != currentRoot) {
        loadProjectEnemyTypes(context);
        projectLoaded_ = true;
        lastLoadedProjectRoot_ = currentRoot;
        spritePreview_ = {};
    }
    if (context.enemyTypes.empty()) {
        context.enemyTypes.push_back({});
    }
    if (std::string(mapId_.data()) != context.selectedScreenMapId && !context.selectedScreenMapId.empty()) {
        copyToBuffer(mapId_, context.selectedScreenMapId);
        loadBgMap(context);
        loadScreenGraphics(context);
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
    if (canvasMode_ == CanvasMode::EditSplines) {
        drawWaypointList(context);
    } else {
        ImGui::TextWrapped("Select Edit splines to change the selected enemy path.");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("PathCanvas", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void EnemyPathEditorPanel::drawTypes(EditorContext& context)
{
    const std::string currentRoot = context.assets.projectRoot.string();
    if (!projectLoaded_ || lastLoadedProjectRoot_ != currentRoot) {
        loadProjectEnemyTypes(context);
        projectLoaded_ = true;
        lastLoadedProjectRoot_ = currentRoot;
        spritePreview_ = {};
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
            context.requestedSpriteReference = (context.assets.gameSpritePath() / (spriteId + ".sprite.json")).generic_string();
            context.requestEditSprite = true;
        }
    }

    if (ImGui::Button("Reload screen background")) {
        copyToBuffer(mapId_, context.selectedScreenMapId);
        loadBgMap(context);
        loadScreenGraphics(context);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Enemy canvas mode");
    const auto modeButton = [this](CanvasMode mode, const char* label) {
        const bool selected = canvasMode_ == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(74, 128, 214, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(86, 146, 240, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(62, 110, 194, 255));
        }
        const bool clicked = ImGui::Button(label, ImVec2(180.0f, 38.0f));
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        if (clicked) {
            canvasMode_ = mode;
            dragging_ = false;
        }
    };
    modeButton(CanvasMode::AddEnemies, "ADD ENEMIES");
    ImGui::SameLine();
    modeButton(CanvasMode::EditSplines, "EDIT SPLINES");

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

    ImGui::Spacing();
    if (ui::sliderFloat("Speed override", "##EnemySpeedOverride", &speed_, 0.0f, 256.0f, "%.0f px/s", 120.0f, 110.0f)) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine(260.0f);
    if (ui::checkbox("Loop", "##EnemyLoop", &loop_, 54.0f)) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine(360.0f);
    if (ui::checkbox("Respawn enemy", "##EnemyRespawn", &respawn_, 110.0f)) {
        writeCurrentPlacement(context);
    }
    ImGui::SameLine(500.0f);
    if (ui::checkbox("Above walls", "##EnemyAboveWalls", &renderAboveWalls_, 110.0f)) {
        writeCurrentPlacement(context);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Render this enemy after the wall texture instead of underneath it.");
    }

    ui::sliderFloat("Zoom", "##EnemyEditorZoom", &zoom_, 0.5f, 6.0f, "%.1fx", 80.0f, 54.0f);
    ImGui::SameLine();
    ui::checkbox("Snap to grid", "##EnemySnapToGrid", &snapToGrid_, 96.0f);

    drawAnimationStateHelper(context);

    ImGui::Text("Project enemy definitions stay in the project. This screen stores only placements and path overrides.");
    if (canvasMode_ == CanvasMode::AddEnemies) {
        ImGui::TextDisabled("Add enemies: click empty area to place, click a marker to select, right-click marker to remove from this screen.");
    } else {
        ImGui::TextDisabled("Edit splines: click empty area to add waypoint, click waypoint to select, drag selected to move, right-click to delete.");
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void EnemyPathEditorPanel::loadSpritePreview(const EditorContext& context, const std::string& spriteId)
{
    spritePreview_ = {};
    spritePreview_.loadedId = spriteId;
    spritePreview_.loadedProjectRoot = context.assets.projectRoot.string();
    if (spriteId.empty()) return;

    // Load sprite metadata
    const std::filesystem::path metaPath = context.assets.gameSpritePath() / (spriteId + ".sprite.json");
    game::SpriteMetadata meta;
    if (!game::loadSpriteMetadata(metaPath, meta, nullptr)) return;

    // Find first usable frame (prefer "idle", else first frame)
    const game::SpriteFrameDef* firstFrame = nullptr;
    for (const game::SpriteFrameDef& f : meta.frames) {
        if (firstFrame == nullptr) firstFrame = &f;
        if (f.type == "idle") { firstFrame = &f; break; }
    }
    if (firstFrame == nullptr) return;

    spritePreview_.frameX = firstFrame->x;
    spritePreview_.frameY = firstFrame->y;
    spritePreview_.frameW = firstFrame->width;
    spritePreview_.frameH = firstFrame->height;

    // Load PNG sheet
    std::filesystem::path sheetPath = meta.source;
    if (sheetPath.empty()) {
        sheetPath = context.assets.rawSpritePath() / (spriteId + "_sheet.png");
    }
    if (!sheetPath.is_absolute()) {
        sheetPath = context.assets.projectRoot / sheetPath;
    }

    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(sheetPath.string().c_str(), &w, &h, &ch, 4);
    if (!data || w <= 0 || h <= 0) { if (data) stbi_image_free(data); return; }

    spritePreview_.sheetW = w;
    spritePreview_.sheetH = h;
    spritePreview_.sheetPixels = rgbaToPixels(data, w, h);
    stbi_image_free(data);
    spritePreview_.loaded = true;
}

void EnemyPathEditorPanel::drawSpritePreviewPanel(ImDrawList* dl, ImVec2 topLeft, float panelW, float panelH, const game::EnemyType& type) const
{
    // Dark background
    dl->AddRectFilled(topLeft, {topLeft.x + panelW, topLeft.y + panelH}, IM_COL32(30, 32, 40, 255));
    dl->AddRect(topLeft, {topLeft.x + panelW, topLeft.y + panelH}, IM_COL32(70, 80, 100, 200));

    if (!spritePreview_.loaded || spritePreview_.frameW <= 0 || spritePreview_.frameH <= 0) {
        // "No sprite" placeholder text
        dl->AddText({topLeft.x + 8.0f, topLeft.y + panelH * 0.5f - 6.0f}, IM_COL32(120, 130, 150, 200), "No sprite");
        return;
    }

    // Scale frame to fit panel while preserving aspect ratio
    const float scale = std::min(panelW / static_cast<float>(spritePreview_.frameW),
                                 panelH / static_cast<float>(spritePreview_.frameH));
    const float drawW = static_cast<float>(spritePreview_.frameW) * scale;
    const float drawH = static_cast<float>(spritePreview_.frameH) * scale;
    const float offX = topLeft.x + (panelW - drawW) * 0.5f;
    const float offY = topLeft.y + (panelH - drawH) * 0.5f;

    const float pixW = scale;
    const float pixH = scale;

    // Draw pixels from the frame region
    for (int py = 0; py < spritePreview_.frameH; ++py) {
        const int sheetY = spritePreview_.frameY + py;
        if (sheetY < 0 || sheetY >= spritePreview_.sheetH) continue;
        int runStart = -1;
        std::uint32_t runColor = 0u;
        for (int px = 0; px <= spritePreview_.frameW; ++px) {
            const int sheetX = spritePreview_.frameX + px;
            const bool inBounds = (px < spritePreview_.frameW && sheetX >= 0 && sheetX < spritePreview_.sheetW);
            const std::uint32_t color = inBounds ? spritePreview_.sheetPixels[static_cast<std::size_t>(sheetY * spritePreview_.sheetW + sheetX)] : 0u;
            const bool visible = inBounds && alphaOf(color) > 0u;
            if (!visible) {
                if (runStart >= 0) {
                    dl->AddRectFilled(
                        {offX + static_cast<float>(runStart) * pixW, offY + static_cast<float>(py) * pixH},
                        {offX + static_cast<float>(px) * pixW, offY + static_cast<float>(py + 1) * pixH},
                        packedColor(runColor, 1.0f));
                    runStart = -1;
                }
                continue;
            }
            if (runStart < 0) { runStart = px; runColor = color; }
            else if (color != runColor) {
                dl->AddRectFilled(
                    {offX + static_cast<float>(runStart) * pixW, offY + static_cast<float>(py) * pixH},
                    {offX + static_cast<float>(px) * pixW, offY + static_cast<float>(py + 1) * pixH},
                    packedColor(runColor, 1.0f));
                runStart = px; runColor = color;
            }
        }
    }

    // Hitbox overlay
    if (showHitboxOverlay_ && (type.hitboxWidth > 0.0f || type.hitboxHeight > 0.0f)) {
        const float hbW = type.hitboxWidth  * scale;
        const float hbH = type.hitboxHeight * scale;
        const float hbX = offX + (drawW - hbW) * 0.5f;
        const float hbY = offY + (drawH - hbH) * 0.5f;
        dl->AddRectFilled({hbX, hbY}, {hbX + hbW, hbY + hbH}, IM_COL32(255, 60, 60, 45));
        dl->AddRect({hbX, hbY}, {hbX + hbW, hbY + hbH}, IM_COL32(255, 80, 80, 200), 0.0f, 0, 1.5f);
    }
}

void EnemyPathEditorPanel::drawAttackEditor(EditorContext& context, game::EnemyType& type)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Active Attacks");
    ImGui::SameLine();
    ImGui::TextDisabled("(up to 2; each fires independently of contact damage)");

    if (type.attacks.size() < 2 && ImGui::Button("+ Add Attack")) {
        game::EnemyAttackDef atk;
        atk.type = type.attacks.empty() ? game::EnemyAttackType::Melee : game::EnemyAttackType::Ranged;
        atk.animState = type.attacks.empty() ? "attack_1" : "attack_2";
        type.attacks.push_back(atk);
        selectedAttack_ = static_cast<int>(type.attacks.size()) - 1;
        context.markDirty();
    }

    for (int ai = 0; ai < static_cast<int>(type.attacks.size()); ++ai) {
        ImGui::PushID(ai);
        game::EnemyAttackDef& atk = type.attacks[static_cast<std::size_t>(ai)];

        const bool open = ImGui::CollapsingHeader(
            (std::string("Attack ") + std::to_string(ai + 1) + "  [" + kAttackTypeNames[static_cast<int>(atk.type)] + "]").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove")) {
                type.attacks.erase(type.attacks.begin() + ai);
                context.markDirty();
                ImGui::EndPopup();
                ImGui::PopID();
                break;
            }
            ImGui::EndPopup();
        }

        if (open) {
            int typeInt = static_cast<int>(atk.type);
            if (ImGui::Combo("Type##atktype", &typeInt, kAttackTypeNames, 3)) {
                atk.type = static_cast<game::EnemyAttackType>(typeInt);
                context.markDirty();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::InputInt("Damage##atkdmg", &atk.damage)) {
                atk.damage = std::clamp(atk.damage, 0, 999);
                context.markDirty();
            }

            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputFloat("Range (px)##atkrange", &atk.range, 1.0f, 10.0f, "%.0f")) {
                atk.range = std::clamp(atk.range, 1.0f, 2000.0f);
                context.markDirty();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputFloat("Cooldown (s)##atkcd", &atk.cooldown, 0.1f, 0.5f, "%.2f")) {
                atk.cooldown = std::clamp(atk.cooldown, 0.05f, 60.0f);
                context.markDirty();
            }

            // Anim state
            char animBuf[64]{};
            std::memcpy(animBuf, atk.animState.data(), std::min(atk.animState.size(), sizeof(animBuf) - 1));
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("Anim state##atkanim", animBuf, sizeof(animBuf))) {
                atk.animState = animBuf;
                context.markDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animation state name to play when this attack fires, e.g. attack_1");

            if (atk.type == game::EnemyAttackType::Ranged) {
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputFloat("Proj speed##atkps", &atk.projectileSpeed, 10.0f, 50.0f, "%.0f")) {
                    atk.projectileSpeed = std::clamp(atk.projectileSpeed, 10.0f, 2000.0f);
                    context.markDirty();
                }
                ImGui::SameLine();
                char ammoBuf[64]{};
                std::memcpy(ammoBuf, atk.ammoSpriteId.data(), std::min(atk.ammoSpriteId.size(), sizeof(ammoBuf) - 1));
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("Ammo sprite##atkammo", ammoBuf, sizeof(ammoBuf))) {
                    atk.ammoSpriteId = ammoBuf;
                    context.markDirty();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sprite ID for the projectile (e.g. arrow_sprite)");
            }
        }
        ImGui::PopID();
    }
}

void EnemyPathEditorPanel::drawEnemyTypePage(EditorContext& context)
{
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1);

    // ── Top bar: type selector + add ─────────────────────────────────────────
    ImGui::TextUnformatted("Project Enemy Definitions");
    ImGui::SameLine();
    if (ImGui::Button("New type")) {
        game::EnemyType type;
        type.id = "enemy_type_" + std::to_string(context.enemyTypes.size() + 1);
        type.spriteId = type.id;
        context.enemyTypes.push_back(type);
        selectedType_ = static_cast<int>(context.enemyTypes.size()) - 1;
        spritePreview_ = {};
        context.markDirty();
    }

    if (context.enemyTypes.empty()) return;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##TypeSel", context.enemyTypes[static_cast<std::size_t>(selectedType_)].id.c_str())) {
        for (int i = 0; i < static_cast<int>(context.enemyTypes.size()); ++i) {
            if (ImGui::Selectable(context.enemyTypes[static_cast<std::size_t>(i)].id.c_str(), selectedType_ == i)) {
                selectedType_ = i;
                spritePreview_ = {};   // force reload
            }
        }
        ImGui::EndCombo();
    }

    game::EnemyType& type = context.enemyTypes[static_cast<std::size_t>(selectedType_)];

    // Reload sprite preview if the selected type changed or sprite ID changed
    if (spritePreview_.loadedId != type.spriteId ||
        spritePreview_.loadedProjectRoot != context.assets.projectRoot.string()) {
        loadSpritePreview(context, type.spriteId);
    }

    ImGui::Separator();

    // ── Two-column layout: left = sprite preview, right = stats ──────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Left column: preview panel + hitbox toggle
    ImGui::BeginGroup();
    const ImVec2 previewPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(kPreviewPanelW, kPreviewPanelH));
    drawSpritePreviewPanel(dl, previewPos, kPreviewPanelW, kPreviewPanelH, type);
    ImGui::Checkbox("Show hitbox", &showHitboxOverlay_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Red rectangle shows the collision hitbox size relative to the sprite frame");
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);

    // Right column: identity + stats
    ImGui::BeginGroup();

    // Type ID and sprite
    char typeIdBuf[64]{};
    std::memcpy(typeIdBuf, type.id.data(), std::min(type.id.size(), sizeof(typeIdBuf) - 1));
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::InputText("Type ID##tid", typeIdBuf, sizeof(typeIdBuf))) {
        type.id = typeIdBuf;
        context.markDirty();
    }
    ImGui::SameLine();
    char spriteBuf[64]{};
    std::memcpy(spriteBuf, type.spriteId.data(), std::min(type.spriteId.size(), sizeof(spriteBuf) - 1));
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::InputText("Sprite ID##sid", spriteBuf, sizeof(spriteBuf))) {
        type.spriteId = spriteBuf;
        context.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit Sprite##etype")) {
        if (!type.spriteId.empty()) {
            context.requestedSpriteReference = (context.assets.gameSpritePath() / (type.spriteId + ".sprite.json")).generic_string();
            context.requestEditSprite = true;
        }
    }

    // HP and movement speed
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("Max HP##mhp", &type.maxHealth)) {
        type.maxHealth = std::clamp(type.maxHealth, 1, 9999);
        context.markDirty();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputFloat("Speed (px/s)##spd", &type.speed, 1.0f, 8.0f, "%.0f")) {
        type.speed = std::clamp(type.speed, 0.0f, 512.0f);
        context.markDirty();
    }

    // Hitbox — labelled clearly
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputFloat("Hitbox W (px)##hbw", &type.hitboxWidth, 1.0f, 4.0f, "%.1f")) {
        type.hitboxWidth = std::clamp(type.hitboxWidth, 1.0f, 256.0f);
        context.markDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Width of the collision rectangle centred on the enemy (pixels)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputFloat("Hitbox H (px)##hbh", &type.hitboxHeight, 1.0f, 4.0f, "%.1f")) {
        type.hitboxHeight = std::clamp(type.hitboxHeight, 1.0f, 256.0f);
        context.markDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Height of the collision rectangle centred on the enemy (pixels)");

    // Contact damage
    ImGui::Separator();
    ImGui::TextDisabled("Contact Damage  (deals damage when player overlaps the enemy)");
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("Contact Dmg##cdmg", &type.contactDamage)) {
        type.contactDamage = std::clamp(type.contactDamage, 0, 999);
        context.markDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("HP removed from player each time they overlap this enemy");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputFloat("Cooldown (s)##ccd", &type.attackCooldownSeconds, 0.1f, 0.5f, "%.2f")) {
        type.attackCooldownSeconds = std::clamp(type.attackCooldownSeconds, 0.0f, 60.0f);
        context.markDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum seconds between contact damage hits");

    ImGui::EndGroup();

    // Below both columns: attack editor
    ImGui::Spacing();
    drawAttackEditor(context, type);
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
        if (ImGui::Button("Remove from Screen")) {
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
            context.requestedSpriteReference = (context.assets.gameSpritePath() / (spriteId + ".sprite.json")).generic_string();
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
        char label[64];
        const Waypoint& wp = waypoints_[static_cast<std::size_t>(i)];
        std::snprintf(label, sizeof(label), "[%d]  %.0f, %.0f%s", i, wp.x, wp.y, wp.waitSeconds > 0.0f ? " [W]" : "");
        if (ImGui::Selectable(label, sel)) {
            selectedWaypoint_ = i;
        }
        ImGui::PopID();
    }
    if (selectedWaypoint_ >= 0 && selectedWaypoint_ < static_cast<int>(waypoints_.size())) {
        ImGui::Separator();
        Waypoint& wp = waypoints_[static_cast<std::size_t>(selectedWaypoint_)];
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("Seg Speed", &wp.speedOverride, 1.0f, 10.0f, "%.0f")) { writeCurrentPlacement(context); }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("Wait (s)", &wp.waitSeconds, 0.1f, 0.5f, "%.1f")) { writeCurrentPlacement(context); }
        const char* facingNames[] = {"Unchanged", "South", "North", "East", "West"};
        int facingIdx = wp.facing + 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Facing##wp", &facingIdx, facingNames, 5)) {
            wp.facing = facingIdx - 1;
            writeCurrentPlacement(context);
        }
        char animBuf[64]{};
        std::memcpy(animBuf, wp.animState.data(), std::min(wp.animState.size(), sizeof(animBuf) - 1));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Anim##wp", animBuf, sizeof(animBuf))) {
            wp.animState = animBuf;
            writeCurrentPlacement(context);
        }
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

    if (floorGraphics_.loaded) {
        drawPixelLayer(dl, origin, floorGraphics_, canvasW, canvasH, 1.0f);
    }

    // Map background: layers, walls, ceiling, and obstacles from the selected screen.
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

    if (wallGraphics_.loaded) {
        drawPixelLayer(dl, origin, wallGraphics_, canvasW, canvasH, 0.9f);
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

    // Enemy anchors for every placement on the screen.
    for (int i = 0; i < static_cast<int>(context.selectedScreenEnemies.size()); ++i) {
        const game::EnemyPlacement& placement = context.selectedScreenEnemies[static_cast<std::size_t>(i)];
        const Waypoint anchor = placementAnchor(placement);
        const ImVec2 center{origin.x + anchor.x * zoom_, origin.y + anchor.y * zoom_};
        const bool selected = i == selectedPlacement_;
        dl->AddCircleFilled(center, selected ? 8.0f : 6.0f,
            selected ? IM_COL32(255, 170, 60, 255) : IM_COL32(225, 95, 80, 235));
        dl->AddCircle(center, selected ? 9.0f : 7.0f, IM_COL32(20, 20, 24, 230), 0, 2.0f);
        if (!placement.id.empty()) {
            dl->AddText({center.x + 9.0f, center.y - 7.0f}, IM_COL32(245, 245, 245, 220), placement.id.c_str());
        }
    }

    // Path lines between consecutive waypoints for the selected placement.
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

    // Waypoint circles are editable only in spline mode, but remain visible for context.
    if (selectedPlacement_ >= 0) {
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
    }

    // Handle input
    if (!ImGui::IsItemHovered()) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float wx = (mouse.x - origin.x) / zoom_;
    const float wy = (mouse.y - origin.y) / zoom_;

    int nearestPlacement = -1;
    float nearestPlacementDist = kHitRadius / zoom_;
    for (int i = 0; i < static_cast<int>(context.selectedScreenEnemies.size()); ++i) {
        const Waypoint anchor = placementAnchor(context.selectedScreenEnemies[static_cast<std::size_t>(i)]);
        const float dx = anchor.x - wx;
        const float dy = anchor.y - wy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < nearestPlacementDist) {
            nearestPlacementDist = d;
            nearestPlacement = i;
        }
    }

    if (canvasMode_ == CanvasMode::AddEnemies) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (nearestPlacement >= 0) {
                selectPlacement(context, nearestPlacement);
            } else {
                float ax = wx;
                float ay = wy;
                if (snapToGrid_) {
                    ax = snapValue(ax);
                    ay = snapValue(ay);
                }
                createPlacementAt(context, std::clamp(ax, 0.0f, worldW), std::clamp(ay, 0.0f, worldH));
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearestPlacement >= 0) {
            context.selectedScreenEnemies.erase(context.selectedScreenEnemies.begin() + nearestPlacement);
            selectedPlacement_ = std::clamp(selectedPlacement_, -1, static_cast<int>(context.selectedScreenEnemies.size()) - 1);
            if (selectedPlacement_ >= 0) {
                selectPlacement(context, selectedPlacement_);
            } else {
                waypoints_.clear();
                selectedWaypoint_ = -1;
            }
            context.markDirty();
        }
        return;
    }

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

void EnemyPathEditorPanel::loadScreenGraphics(EditorContext& context)
{
    const std::string id(mapId_.data());
    floorGraphics_ = {};
    wallGraphics_ = {};
    if (id.empty()) {
        return;
    }

    const auto loadLayer = [](const std::filesystem::path& path, PixelLayer& layer) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (data == nullptr || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            return false;
        }
        layer.width = width;
        layer.height = height;
        layer.pixels = rgbaToPixels(data, width, height);
        layer.loaded = true;
        stbi_image_free(data);
        return true;
    };

    (void)loadLayer(context.assets.rawTilesetPath() / (id + "_floor.png"), floorGraphics_);
    (void)loadLayer(context.assets.rawTilesetPath() / (id + "_wall.png"), wallGraphics_);
    if (!floorGraphics_.loaded) {
        (void)loadLayer(context.assets.gameTilesetPath() / (id + "_floor.png"), floorGraphics_);
    }
    if (!wallGraphics_.loaded) {
        (void)loadLayer(context.assets.gameTilesetPath() / (id + "_wall.png"), wallGraphics_);
    }
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
    createPlacementAt(context, static_cast<float>(game::kTileSize * 2), static_cast<float>(game::kTileSize * 2));
}

void EnemyPathEditorPanel::createPlacementAt(EditorContext& context, float x, float y)
{
    game::EnemyPlacement placement;
    placement.id = "enemy_" + std::to_string(context.selectedScreenEnemies.size() + 1);
    if (!context.enemyTypes.empty()) {
        const game::EnemyType& type = context.enemyTypes[static_cast<std::size_t>(std::clamp(selectedType_, 0, static_cast<int>(context.enemyTypes.size()) - 1))];
        placement.typeId = type.id;
        placement.speedOverride = type.speed;
    }
    placement.waypoints.push_back({x, y});
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
    renderAboveWalls_ = placement.renderAboveWalls;
    waypoints_.clear();
    for (const game::PathWaypoint& waypoint : placement.waypoints) {
        Waypoint wp;
        wp.x = waypoint.x;
        wp.y = waypoint.y;
        wp.speedOverride = waypoint.speedOverride;
        wp.waitSeconds = waypoint.waitSeconds;
        wp.facing = waypoint.facing;
        wp.animState = waypoint.animState;
        waypoints_.push_back(std::move(wp));
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
    placement.renderAboveWalls = renderAboveWalls_;
    placement.waypoints.clear();
    placement.waypoints.reserve(waypoints_.size());
    for (const Waypoint& waypoint : waypoints_) {
        game::PathWaypoint pw;
        pw.x = waypoint.x;
        pw.y = waypoint.y;
        pw.speedOverride = waypoint.speedOverride;
        pw.waitSeconds = waypoint.waitSeconds;
        pw.facing = waypoint.facing;
        pw.animState = waypoint.animState;
        placement.waypoints.push_back(std::move(pw));
    }
    context.markDirty();
}

ImVec2 EnemyPathEditorPanel::waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const
{
    return {origin.x + waypoint.x * zoom_, origin.y + waypoint.y * zoom_};
}

EnemyPathEditorPanel::Waypoint EnemyPathEditorPanel::placementAnchor(const game::EnemyPlacement& placement) const
{
    if (!placement.waypoints.empty()) {
        return {placement.waypoints.front().x, placement.waypoints.front().y};
    }
    return {static_cast<float>(game::kTileSize * 2), static_cast<float>(game::kTileSize * 2)};
}

void EnemyPathEditorPanel::drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const
{
    if (!layer.loaded || layer.width <= 0 || layer.height <= 0 ||
        layer.pixels.size() != static_cast<std::size_t>(layer.width * layer.height)) {
        return;
    }

    const float pixelW = targetW / static_cast<float>(layer.width);
    const float pixelH = targetH / static_cast<float>(layer.height);
    for (int y = 0; y < layer.height; ++y) {
        int runStart = -1;
        std::uint32_t runColor = 0u;
        for (int x = 0; x <= layer.width; ++x) {
            const std::uint32_t color = x < layer.width
                ? layer.pixels[static_cast<std::size_t>(y) * layer.width + x]
                : 0u;
            const bool visible = x < layer.width && alphaOf(color) > 0u;
            if (!visible) {
                if (runStart >= 0) {
                    dl->AddRectFilled(
                        {origin.x + static_cast<float>(runStart) * pixelW, origin.y + static_cast<float>(y) * pixelH},
                        {origin.x + static_cast<float>(x) * pixelW, origin.y + static_cast<float>(y + 1) * pixelH},
                        packedColor(runColor, opacity));
                    runStart = -1;
                }
                continue;
            }
            if (runStart < 0) {
                runStart = x;
                runColor = color;
            } else if (color != runColor) {
                dl->AddRectFilled(
                    {origin.x + static_cast<float>(runStart) * pixelW, origin.y + static_cast<float>(y) * pixelH},
                    {origin.x + static_cast<float>(x) * pixelW, origin.y + static_cast<float>(y + 1) * pixelH},
                    packedColor(runColor, opacity));
                runStart = x;
                runColor = color;
            }
        }
    }
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
