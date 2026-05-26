#include "editor/panels/npc_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"
#include "game/map.hpp"
#include "game/project.hpp"
#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <system_error>

namespace adventure::editor {
namespace {

constexpr float kWorldTileSize = static_cast<float>(game::kTileSize);
constexpr int kDefaultWorldTilesW = game::kScreenTilesW;
constexpr int kDefaultWorldTilesH = game::kScreenTilesH;
constexpr float kWaypointRadius = 6.0f;
constexpr float kHitRadius = 12.0f;

const char* kFacingNames[] = {"South", "North", "East", "West"};
const char* kMovementNames[] = {"Stationary", "Patrol", "Wander"};
const char* kInteractionNames[] = {"None", "Talk", "Shop", "Quest"};

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

bool editString(const char* label, std::string& value)
{
    char buffer[128]{};
    std::memcpy(buffer, value.data(), std::min(value.size(), sizeof(buffer) - 1));
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

} // namespace

void NpcEditorPanel::draw(EditorContext& context)
{
    if (!projectLoaded_) {
        loadProjectNpcTypes(context);
        projectLoaded_ = true;
    }
    if (context.npcTypes.empty()) {
        context.npcTypes.push_back({});
    }
    if (std::string(mapId_.data()) != context.selectedScreenMapId && !context.selectedScreenMapId.empty()) {
        copyToBuffer(mapId_, context.selectedScreenMapId);
        loadBgMap(context);
        loadScreenGraphics(context);
    }
    if (selectedPlacement_ >= static_cast<int>(context.selectedScreenNpcs.size())) {
        selectedPlacement_ = static_cast<int>(context.selectedScreenNpcs.size()) - 1;
    }

    drawToolbar(context);
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(280.0f, std::max(200.0f, available.x * 0.28f));

    ImGui::BeginChild("NpcPlacementList", ImVec2(leftWidth, 0.0f), true);
    drawPlacementList(context);
    if (canvasMode_ == CanvasMode::EditPath && selectedPlacement_ >= 0) {
        ImGui::Separator();
        drawWaypointList(context);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("NpcCanvas", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void NpcEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::TextUnformatted("NPC Placements");
    ImGui::SameLine();
    ImGui::TextDisabled("Screen: %s", context.selectedScreenId.c_str());
    ImGui::SameLine();

    const bool placeMode = canvasMode_ == CanvasMode::PlaceNpcs;
    if (placeMode) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.7f, 1.0f)); }
    if (ImGui::Button("PLACE NPCS", ImVec2(120.0f, 28.0f))) { canvasMode_ = CanvasMode::PlaceNpcs; }
    if (placeMode) { ImGui::PopStyleColor(); }
    ImGui::SameLine();
    const bool pathMode = canvasMode_ == CanvasMode::EditPath;
    if (pathMode) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.7f, 1.0f)); }
    if (ImGui::Button("EDIT PATH", ImVec2(120.0f, 28.0f))) { canvasMode_ = CanvasMode::EditPath; }
    if (pathMode) { ImGui::PopStyleColor(); }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("Zoom", &zoom_, 0.5f, 6.0f, "%.1f");

    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
    }
}

void NpcEditorPanel::drawPlacementList(EditorContext& context)
{
    if (ImGui::Button("New NPC", ImVec2(-1.0f, 28.0f))) {
        createPlacementAt(context, static_cast<float>(game::kTileSize * 4), static_cast<float>(game::kTileSize * 4));
    }

    for (int i = 0; i < static_cast<int>(context.selectedScreenNpcs.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = i == selectedPlacement_;
        if (ImGui::Selectable(context.selectedScreenNpcs[static_cast<std::size_t>(i)].id.c_str(), selected)) {
            if (selectedPlacement_ >= 0) { writeCurrentPlacement(context); }
            selectPlacement(context, i);
        }
        ImGui::PopID();
    }

    if (selectedPlacement_ < 0 || selectedPlacement_ >= static_cast<int>(context.selectedScreenNpcs.size())) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Selected NPC");

    if (ImGui::InputText("ID", placementId_.data(), placementId_.size())) {
        writeCurrentPlacement(context);
    }

    if (ImGui::BeginCombo("Type", context.npcTypes.empty() ? "-" :
        context.npcTypes[static_cast<std::size_t>(std::clamp(selectedType_, 0, static_cast<int>(context.npcTypes.size()) - 1))].id.c_str())) {
        for (int i = 0; i < static_cast<int>(context.npcTypes.size()); ++i) {
            if (ImGui::Selectable(context.npcTypes[static_cast<std::size_t>(i)].id.c_str(), i == selectedType_)) {
                selectedType_ = i;
                copyToBuffer(typeId_, context.npcTypes[static_cast<std::size_t>(i)].id);
                writeCurrentPlacement(context);
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Combo("Facing", &facing_, kFacingNames, 4)) {
        writeCurrentPlacement(context);
    }

    if (ImGui::SliderFloat("Awareness", &awarenessRadius_, 0.0f, 256.0f, "%.0f px")) {
        writeCurrentPlacement(context);
    }
    if (ImGui::SliderFloat("Interact", &interactionRadius_, 0.0f, 128.0f, "%.0f px")) {
        writeCurrentPlacement(context);
    }

    if (ImGui::Combo("Movement", &movementMode_, kMovementNames, 3)) {
        writeCurrentPlacement(context);
    }

    if (movementMode_ == 1) {
        if (ImGui::Checkbox("Loop", &loop_)) { writeCurrentPlacement(context); }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::SliderFloat("Speed", &speed_, 0.0f, 256.0f, "%.0f")) { writeCurrentPlacement(context); }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Dialogue Override");
    for (int i = 0; i < static_cast<int>(dialogueLines_.size()); ++i) {
        ImGui::PushID(4000 + i);
        game::DialogueLine& line = dialogueLines_[static_cast<std::size_t>(i)];
        char speakerBuf[128]{};
        std::memcpy(speakerBuf, line.speaker.data(), std::min(line.speaker.size(), sizeof(speakerBuf) - 1));
        char textBuf[256]{};
        std::memcpy(textBuf, line.text.data(), std::min(line.text.size(), sizeof(textBuf) - 1));
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputText("##spk", speakerBuf, sizeof(speakerBuf))) {
            line.speaker = speakerBuf;
            writeCurrentPlacement(context);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-30.0f);
        if (ImGui::InputText("##txt", textBuf, sizeof(textBuf))) {
            line.text = textBuf;
            writeCurrentPlacement(context);
        }
        ImGui::SameLine();
        if (ImGui::Button("X")) {
            dialogueLines_.erase(dialogueLines_.begin() + i);
            writeCurrentPlacement(context);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("+ Dialogue Line", ImVec2(-1.0f, 22.0f))) {
        dialogueLines_.push_back({"", "Hello!"});
        writeCurrentPlacement(context);
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete NPC", ImVec2(-1.0f, 24.0f))) {
        context.selectedScreenNpcs.erase(context.selectedScreenNpcs.begin() + selectedPlacement_);
        selectedPlacement_ = std::min(selectedPlacement_, static_cast<int>(context.selectedScreenNpcs.size()) - 1);
        if (selectedPlacement_ >= 0) {
            selectPlacement(context, selectedPlacement_);
        }
        context.markDirty();
    }
}

void NpcEditorPanel::drawWaypointList(EditorContext& context)
{
    ImGui::TextUnformatted("Waypoints");
    for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
        ImGui::PushID(1000 + i);
        const Waypoint& wp = waypoints_[static_cast<std::size_t>(i)];
        char label[64];
        std::snprintf(label, sizeof(label), "WP %d (%.0f, %.0f)%s", i, wp.x, wp.y, wp.waitSeconds > 0.0f ? " [W]" : "");
        if (ImGui::Selectable(label, i == selectedWaypoint_)) {
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
        if (ImGui::Button("Delete Waypoint")) {
            waypoints_.erase(waypoints_.begin() + selectedWaypoint_);
            selectedWaypoint_ = std::min(selectedWaypoint_, static_cast<int>(waypoints_.size()) - 1);
            writeCurrentPlacement(context);
        }
    }
}

void NpcEditorPanel::drawCanvas(EditorContext& context)
{
    const int worldTilesW = bgMapLoaded_ ? bgMap_.width : kDefaultWorldTilesW;
    const int worldTilesH = bgMapLoaded_ ? bgMap_.height : kDefaultWorldTilesH;
    const float worldW = static_cast<float>(worldTilesW) * kWorldTileSize;
    const float worldH = static_cast<float>(worldTilesH) * kWorldTileSize;
    const float tileCanvas = kWorldTileSize * zoom_;
    const float canvasW = worldW * zoom_;
    const float canvasH = worldH * zoom_;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("NpcCanvas", ImVec2(canvasW, canvasH),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, {origin.x + canvasW, origin.y + canvasH}, IM_COL32(20, 22, 28, 255));

    if (floorGraphics_.loaded) {
        drawPixelLayer(dl, origin, floorGraphics_, canvasW, canvasH, 1.0f);
    }

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
    }

    if (wallGraphics_.loaded) {
        drawPixelLayer(dl, origin, wallGraphics_, canvasW, canvasH, 0.9f);
    }

    for (int gy = 0; gy <= worldTilesH; ++gy) {
        dl->AddLine({origin.x, origin.y + gy * tileCanvas},
            {origin.x + canvasW, origin.y + gy * tileCanvas}, IM_COL32(50, 55, 65, 200));
    }
    for (int gx = 0; gx <= worldTilesW; ++gx) {
        dl->AddLine({origin.x + gx * tileCanvas, origin.y},
            {origin.x + gx * tileCanvas, origin.y + canvasH}, IM_COL32(50, 55, 65, 200));
    }

    // NPC markers
    for (int i = 0; i < static_cast<int>(context.selectedScreenNpcs.size()); ++i) {
        const game::NpcPlacement& npc = context.selectedScreenNpcs[static_cast<std::size_t>(i)];
        float ax = npc.x;
        float ay = npc.y;
        if (!npc.waypoints.empty()) {
            ax = npc.waypoints.front().x;
            ay = npc.waypoints.front().y;
        }
        const ImVec2 center{origin.x + ax * zoom_, origin.y + ay * zoom_};
        const bool selected = i == selectedPlacement_;
        dl->AddCircleFilled(center, selected ? 9.0f : 7.0f,
            selected ? IM_COL32(60, 220, 210, 255) : IM_COL32(30, 190, 180, 220));
        dl->AddCircle(center, selected ? 10.0f : 8.0f, IM_COL32(20, 20, 24, 230), 0, 2.0f);
        if (!npc.id.empty()) {
            dl->AddText({center.x + 10.0f, center.y - 7.0f}, IM_COL32(245, 245, 245, 220), npc.id.c_str());
        }
        if (selected) {
            dl->AddCircle(center, npc.awarenessRadius * zoom_, IM_COL32(100, 200, 255, 80), 32, 1.0f);
            dl->AddCircle(center, npc.interactionRadius * zoom_, IM_COL32(80, 255, 120, 100), 24, 1.0f);
        }
    }

    // Path lines for the selected NPC
    if (selectedPlacement_ >= 0 && canvasMode_ == CanvasMode::EditPath) {
        const ImU32 lineCol = IM_COL32(60, 200, 210, 200);
        const ImU32 loopLineCol = IM_COL32(60, 200, 210, 100);
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
                loopLineCol, 2.0f);
        }

        for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
            const ImVec2 wp{origin.x + waypoints_[i].x * zoom_, origin.y + waypoints_[i].y * zoom_};
            const bool sel = i == selectedWaypoint_;
            dl->AddCircleFilled(wp, sel ? kWaypointRadius + 2.0f : kWaypointRadius,
                sel ? IM_COL32(60, 255, 220, 255) : IM_COL32(40, 210, 190, 230));
            dl->AddCircle(wp, sel ? kWaypointRadius + 3.0f : kWaypointRadius + 1.0f, IM_COL32(20, 20, 24, 200), 0, 1.5f);
        }
    }

    // Mouse interaction
    const ImVec2 mousePos = ImGui::GetMousePos();
    const float mx = (mousePos.x - origin.x) / zoom_;
    const float my = (mousePos.y - origin.y) / zoom_;

    if (canvasMode_ == CanvasMode::PlaceNpcs) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            int hitIndex = -1;
            for (int i = 0; i < static_cast<int>(context.selectedScreenNpcs.size()); ++i) {
                const game::NpcPlacement& npc = context.selectedScreenNpcs[static_cast<std::size_t>(i)];
                float ax = npc.x;
                float ay = npc.y;
                if (!npc.waypoints.empty()) { ax = npc.waypoints.front().x; ay = npc.waypoints.front().y; }
                const float dx = (mousePos.x - origin.x) - ax * zoom_;
                const float dy = (mousePos.y - origin.y) - ay * zoom_;
                if (std::sqrt(dx * dx + dy * dy) <= kHitRadius) {
                    hitIndex = i;
                    break;
                }
            }
            if (hitIndex >= 0) {
                if (selectedPlacement_ >= 0) { writeCurrentPlacement(context); }
                selectPlacement(context, hitIndex);
            } else {
                if (selectedPlacement_ >= 0) { writeCurrentPlacement(context); }
                const float sx = snapToGrid_ ? snapValue(mx) : mx;
                const float sy = snapToGrid_ ? snapValue(my) : my;
                createPlacementAt(context, sx, sy);
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            for (int i = 0; i < static_cast<int>(context.selectedScreenNpcs.size()); ++i) {
                const game::NpcPlacement& npc = context.selectedScreenNpcs[static_cast<std::size_t>(i)];
                float ax = npc.x;
                float ay = npc.y;
                if (!npc.waypoints.empty()) { ax = npc.waypoints.front().x; ay = npc.waypoints.front().y; }
                const float dx = (mousePos.x - origin.x) - ax * zoom_;
                const float dy = (mousePos.y - origin.y) - ay * zoom_;
                if (std::sqrt(dx * dx + dy * dy) <= kHitRadius) {
                    context.selectedScreenNpcs.erase(context.selectedScreenNpcs.begin() + i);
                    if (selectedPlacement_ >= static_cast<int>(context.selectedScreenNpcs.size())) {
                        selectedPlacement_ = static_cast<int>(context.selectedScreenNpcs.size()) - 1;
                    }
                    if (selectedPlacement_ >= 0) { selectPlacement(context, selectedPlacement_); }
                    context.markDirty();
                    break;
                }
            }
        }
    } else if (canvasMode_ == CanvasMode::EditPath && selectedPlacement_ >= 0) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            int hitWp = -1;
            for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
                const float dx = (mousePos.x - origin.x) - waypoints_[i].x * zoom_;
                const float dy = (mousePos.y - origin.y) - waypoints_[i].y * zoom_;
                if (std::sqrt(dx * dx + dy * dy) <= kHitRadius) {
                    hitWp = i;
                    break;
                }
            }
            if (hitWp >= 0) {
                selectedWaypoint_ = hitWp;
                dragging_ = true;
            } else {
                const float sx = snapToGrid_ ? snapValue(mx) : mx;
                const float sy = snapToGrid_ ? snapValue(my) : my;
                waypoints_.push_back({sx, sy});
                selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
                writeCurrentPlacement(context);
            }
        }
        if (dragging_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
            selectedWaypoint_ >= 0 && selectedWaypoint_ < static_cast<int>(waypoints_.size())) {
            const float sx = snapToGrid_ ? snapValue(mx) : mx;
            const float sy = snapToGrid_ ? snapValue(my) : my;
            waypoints_[static_cast<std::size_t>(selectedWaypoint_)] = {sx, sy};
            writeCurrentPlacement(context);
        }
        if (dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragging_ = false;
            writeCurrentPlacement(context);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
                const float dx = (mousePos.x - origin.x) - waypoints_[i].x * zoom_;
                const float dy = (mousePos.y - origin.y) - waypoints_[i].y * zoom_;
                if (std::sqrt(dx * dx + dy * dy) <= kHitRadius) {
                    waypoints_.erase(waypoints_.begin() + i);
                    if (selectedWaypoint_ >= static_cast<int>(waypoints_.size())) {
                        selectedWaypoint_ = static_cast<int>(waypoints_.size()) - 1;
                    }
                    writeCurrentPlacement(context);
                    break;
                }
            }
        }
    }
}

void NpcEditorPanel::drawTypes(EditorContext& context)
{
    if (!projectLoaded_) {
        loadProjectNpcTypes(context);
        projectLoaded_ = true;
    }
    if (context.npcTypes.empty()) {
        context.npcTypes.push_back({});
    }
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.npcTypes.size()) - 1);

    ImGui::TextUnformatted("NPC Type Definitions");
    ImGui::SameLine();
    if (ImGui::Button("New Type")) {
        game::NpcTypeDef npc;
        npc.id = "npc_type_" + std::to_string(context.npcTypes.size() + 1);
        context.npcTypes.push_back(std::move(npc));
        selectedType_ = static_cast<int>(context.npcTypes.size()) - 1;
        context.markDirty();
    }

    for (int i = 0; i < static_cast<int>(context.npcTypes.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(context.npcTypes[static_cast<std::size_t>(i)].id.c_str(), i == selectedType_)) {
            selectedType_ = i;
        }
        ImGui::PopID();
    }

    if (selectedType_ < 0 || selectedType_ >= static_cast<int>(context.npcTypes.size())) {
        return;
    }

    ImGui::Separator();
    game::NpcTypeDef& npc = context.npcTypes[static_cast<std::size_t>(selectedType_)];

    if (editString("ID", npc.id)) { context.markDirty(); }
    if (editString("Sprite ID", npc.spriteId)) { context.markDirty(); }
    if (editString("Character ID", npc.characterId)) { context.markDirty(); }

    int movement = static_cast<int>(npc.defaultMovement);
    if (ImGui::Combo("Default Movement", &movement, kMovementNames, 3)) {
        npc.defaultMovement = static_cast<game::NpcMovementMode>(std::clamp(movement, 0, 2));
        context.markDirty();
    }
    int interaction = static_cast<int>(npc.defaultInteraction);
    if (ImGui::Combo("Default Interaction", &interaction, kInteractionNames, 4)) {
        npc.defaultInteraction = static_cast<game::NpcInteractionMode>(std::clamp(interaction, 0, 3));
        context.markDirty();
    }
    if (ImGui::SliderFloat("Default Speed", &npc.defaultSpeed, 0.0f, 256.0f, "%.0f px/s")) {
        context.markDirty();
    }
    if (editString("Default Graph ID", npc.defaultGraphId)) { context.markDirty(); }

    ImGui::Separator();
    ImGui::TextUnformatted("Default Dialogue");
    for (int i = 0; i < static_cast<int>(npc.defaultDialogue.size()); ++i) {
        ImGui::PushID(3000 + i);
        game::DialogueLine& line = npc.defaultDialogue[static_cast<std::size_t>(i)];
        char speakerBuf[128]{};
        std::memcpy(speakerBuf, line.speaker.data(), std::min(line.speaker.size(), sizeof(speakerBuf) - 1));
        char textBuf[256]{};
        std::memcpy(textBuf, line.text.data(), std::min(line.text.size(), sizeof(textBuf) - 1));
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputText("Speaker", speakerBuf, sizeof(speakerBuf))) {
            line.speaker = speakerBuf;
            context.markDirty();
        }
        ImGui::SetNextItemWidth(-40.0f);
        if (ImGui::InputText("Text", textBuf, sizeof(textBuf))) {
            line.text = textBuf;
            context.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("X")) {
            npc.defaultDialogue.erase(npc.defaultDialogue.begin() + i);
            context.markDirty();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("+ Add Dialogue Line")) {
        npc.defaultDialogue.push_back({"", "Hello!"});
        context.markDirty();
    }

    ImGui::Spacing();
    if (context.npcTypes.size() > 1 && ImGui::Button("Delete Type")) {
        context.npcTypes.erase(context.npcTypes.begin() + selectedType_);
        selectedType_ = std::min(selectedType_, static_cast<int>(context.npcTypes.size()) - 1);
        context.markDirty();
    }
}

void NpcEditorPanel::loadBgMap(EditorContext& context)
{
    const std::string id(mapId_.data());
    if (id.empty()) {
        return;
    }
    const std::filesystem::path mapPath = context.assets.gameMapPath() / (id + ".admap");
    std::string error;
    if (!game::loadTileMap(mapPath, bgMap_, &error)) {
        bgMapLoaded_ = false;
        return;
    }
    bgMapLoaded_ = true;
}

void NpcEditorPanel::loadScreenGraphics(EditorContext& context)
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

void NpcEditorPanel::saveProjectNpcTypes(EditorContext& context)
{
    game::GameProject project;
    (void)game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
    project.id = project.id.empty() ? "game" : project.id;
    project.npcTypes = context.npcTypes;
    if (!context.currentChapterId.empty() &&
        std::find(project.chapterIds.begin(), project.chapterIds.end(), context.currentChapterId) == project.chapterIds.end()) {
        project.chapterIds.push_back(context.currentChapterId);
    }
    std::string error;
    if (game::saveGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, &error)) {
        status_ = "Saved NPC types.";
    } else {
        status_ = "Failed to save NPC types: " + error;
    }
}

void NpcEditorPanel::loadProjectNpcTypes(EditorContext& context)
{
    game::GameProject project;
    if (game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr)) {
        context.npcTypes = std::move(project.npcTypes);
    }
    if (context.npcTypes.empty()) {
        context.npcTypes.push_back({});
    }
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.npcTypes.size()) - 1);
}

void NpcEditorPanel::createPlacementAt(EditorContext& context, float x, float y)
{
    game::NpcPlacement placement;
    placement.id = "npc_" + std::to_string(context.selectedScreenNpcs.size() + 1);
    if (!context.npcTypes.empty()) {
        const game::NpcTypeDef& type = context.npcTypes[static_cast<std::size_t>(std::clamp(selectedType_, 0, static_cast<int>(context.npcTypes.size()) - 1))];
        placement.typeId = type.id;
        placement.movementOverride = type.defaultMovement;
        placement.speedOverride = type.defaultSpeed;
    }
    placement.x = x;
    placement.y = y;
    placement.waypoints.push_back({x, y});
    context.selectedScreenNpcs.push_back(placement);
    selectPlacement(context, static_cast<int>(context.selectedScreenNpcs.size()) - 1);
    context.markDirty();
}

void NpcEditorPanel::selectPlacement(EditorContext& context, int index)
{
    if (index < 0 || index >= static_cast<int>(context.selectedScreenNpcs.size())) {
        return;
    }
    selectedPlacement_ = index;
    const game::NpcPlacement& npc = context.selectedScreenNpcs[static_cast<std::size_t>(index)];
    copyToBuffer(placementId_, npc.id);
    copyToBuffer(typeId_, npc.typeId);
    const auto typeIt = std::find_if(context.npcTypes.begin(), context.npcTypes.end(), [&](const game::NpcTypeDef& type) {
        return type.id == npc.typeId;
    });
    if (typeIt != context.npcTypes.end()) {
        selectedType_ = static_cast<int>(std::distance(context.npcTypes.begin(), typeIt));
    }
    facing_ = std::clamp(npc.facing, 0, 3);
    awarenessRadius_ = npc.awarenessRadius;
    interactionRadius_ = npc.interactionRadius;
    movementMode_ = static_cast<int>(npc.movementOverride);
    loop_ = npc.loop;
    speed_ = npc.speedOverride;
    waypoints_.clear();
    for (const game::PathWaypoint& wp : npc.waypoints) {
        Waypoint w;
        w.x = wp.x;
        w.y = wp.y;
        w.speedOverride = wp.speedOverride;
        w.waitSeconds = wp.waitSeconds;
        w.facing = wp.facing;
        w.animState = wp.animState;
        waypoints_.push_back(std::move(w));
    }
    dialogueLines_ = npc.dialogueOverride;
    selectedWaypoint_ = -1;
}

void NpcEditorPanel::writeCurrentPlacement(EditorContext& context)
{
    if (selectedPlacement_ < 0 || selectedPlacement_ >= static_cast<int>(context.selectedScreenNpcs.size())) {
        return;
    }
    game::NpcPlacement& npc = context.selectedScreenNpcs[static_cast<std::size_t>(selectedPlacement_)];
    npc.id = placementId_.data();
    npc.typeId = typeId_.data();
    npc.facing = facing_;
    npc.awarenessRadius = awarenessRadius_;
    npc.interactionRadius = interactionRadius_;
    npc.movementOverride = static_cast<game::NpcMovementMode>(std::clamp(movementMode_, 0, 2));
    npc.loop = loop_;
    npc.speedOverride = std::max(0.0f, speed_);
    npc.waypoints.clear();
    npc.waypoints.reserve(waypoints_.size());
    for (const Waypoint& wp : waypoints_) {
        game::PathWaypoint pw;
        pw.x = wp.x;
        pw.y = wp.y;
        pw.speedOverride = wp.speedOverride;
        pw.waitSeconds = wp.waitSeconds;
        pw.facing = wp.facing;
        pw.animState = wp.animState;
        npc.waypoints.push_back(std::move(pw));
    }
    if (!npc.waypoints.empty()) {
        npc.x = npc.waypoints.front().x;
        npc.y = npc.waypoints.front().y;
    }
    npc.dialogueOverride = dialogueLines_;
    context.markDirty();
}

float NpcEditorPanel::snapValue(float v) const
{
    if (!snapToGrid_) {
        return v;
    }
    return std::round(v / kWorldTileSize) * kWorldTileSize + kWorldTileSize * 0.5f;
}

ImVec2 NpcEditorPanel::waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const
{
    return {origin.x + waypoint.x * zoom_, origin.y + waypoint.y * zoom_};
}

void NpcEditorPanel::drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const
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

} // namespace adventure::editor
