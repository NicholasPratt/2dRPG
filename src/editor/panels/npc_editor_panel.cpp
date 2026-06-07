#include "editor/panels/npc_editor_panel.hpp"

#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"
#include "game/map.hpp"
#include "game/project.hpp"
#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
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
    if (ui::inputTextString(label, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

std::string graphIdForPlacement(const EditorContext& context, const std::string& npcId)
{
    std::string id = context.selectedScreenId.empty() ? "screen" : context.selectedScreenId;
    id += "_";
    id += npcId.empty() ? "npc" : npcId;
    id += "_dialogue";
    for (char& c : id) {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (!std::isalnum(ch) && c != '_' && c != '-') {
            c = '_';
        }
    }
    return id;
}

std::filesystem::path characterSpriteReference(const EditorContext& context, const std::string& characterId)
{
    if (characterId.empty()) {
        return {};
    }

    std::ifstream input(context.assets.gameCharacterPath() / (characterId + ".adcharacter"));
    if (!input) {
        return {};
    }

    std::string key;
    input >> key;
    if (key != "ADCHARACTER") {
        return {};
    }
    int version = 0;
    input >> version;

    while (input >> key) {
        if (key == "sprite") {
            std::string value;
            input >> std::quoted(value);
            return value;
        }
        if (key == "end") {
            break;
        }
        if (key == "name" || key == "bio") {
            std::string ignored;
            input >> std::quoted(ignored);
        } else if (key == "anim") {
            std::string ignoredA;
            std::string ignoredB;
            input >> std::quoted(ignoredA) >> std::quoted(ignoredB);
        } else if (key == "frame") {
            int ignoredIndex = 0;
            std::string ignoredState;
            std::string ignoredImage;
            input >> ignoredIndex >> std::quoted(ignoredState) >> std::quoted(ignoredImage);
        } else if (key == "playable" || key == "animations" || key == "frames") {
            std::size_t ignored = 0;
            input >> ignored;
        }
    }

    return {};
}

} // namespace

void NpcEditorPanel::draw(EditorContext& context)
{
    const std::string currentRoot = context.assets.projectRoot.string();
    if (!projectLoaded_ || lastLoadedProjectRoot_ != currentRoot) {
        loadProjectNpcTypes(context);
        projectLoaded_ = true;
        lastLoadedProjectRoot_ = currentRoot;
    }
    const std::string placementOwner = context.selectedScreenNpcsOwnerId.empty()
        ? context.selectedScreenId
        : context.selectedScreenNpcsOwnerId;
    const std::string placementContextKey =
        currentRoot + "\n" + context.currentChapterId + "\n" + placementOwner;
    if (loadedPlacementContextKey_ != placementContextKey) {
        openForScreen(context);
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

void NpcEditorPanel::openForScreen(EditorContext& context)
{
    const std::string placementOwner = context.selectedScreenNpcsOwnerId.empty()
        ? context.selectedScreenId
        : context.selectedScreenNpcsOwnerId;
    loadedPlacementContextKey_ =
        context.assets.projectRoot.string() + "\n" + context.currentChapterId + "\n" + placementOwner;

    selectedPlacement_ = -1;
    selectedWaypoint_ = -1;
    dragging_ = false;
    waypoints_.clear();
    dialogueLines_.clear();
    shopInventoryOverride_.clear();

    if (!context.selectedScreenNpcs.empty()) {
        selectPlacement(context, 0);
    }
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
    ImGui::TextDisabled("Project NPC definitions stay in the project. This screen stores only placements and path/dialogue overrides.");
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

    if (ui::inputTextString("ID", placementId_.data(), placementId_.size())) {
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

    if (ui::inputTextString("Graph Override", graphId_.data(), graphId_.size())) {
        writeCurrentPlacement(context);
    }
    if (ImGui::Button("Edit Instance Dialogue", ImVec2(-1.0f, 22.0f))) {
        if (graphId_.data()[0] == '\0') {
            copyToBuffer(graphId_, graphIdForPlacement(context, placementId_.data()));
            writeCurrentPlacement(context);
        }
        context.requestedDialogueGraphId = graphId_.data();
        context.requestEditDialogueGraph = true;
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
        const char* curveNames[] = {"Linear", "Spline"};
        int curve = static_cast<int>(curveMode_);
        if (ImGui::Combo("Path", &curve, curveNames, 2)) {
            curveMode_ = static_cast<CurveMode>(curve);
            writeCurrentPlacement(context);
        }
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
        if (ui::inputTextString("##spk", speakerBuf, sizeof(speakerBuf))) {
            line.speaker = speakerBuf;
            writeCurrentPlacement(context);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-30.0f);
        if (ui::inputTextString("##txt", textBuf, sizeof(textBuf))) {
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

    ImGui::Separator();
    ImGui::TextUnformatted("Shop Override");
    const game::NpcTypeDef* selectedType = nullptr;
    if (selectedType_ >= 0 && selectedType_ < static_cast<int>(context.npcTypes.size())) {
        selectedType = &context.npcTypes[static_cast<std::size_t>(selectedType_)];
    }
    if (selectedType == nullptr || selectedType->defaultInteraction != game::NpcInteractionMode::Shop) {
        ImGui::TextDisabled("Selected NPC type is not a shop.");
    }
    if (shopInventoryOverride_.empty()) {
        ImGui::TextDisabled("Using project NPC type stock.");
        if (selectedType != nullptr && !selectedType->shopInventory.empty() &&
            ImGui::Button("Copy Type Stock", ImVec2(-1.0f, 22.0f))) {
            shopInventoryOverride_ = selectedType->shopInventory;
            writeCurrentPlacement(context);
        }
    }
    for (int i = 0; i < static_cast<int>(shopInventoryOverride_.size()); ++i) {
        ImGui::PushID(6000 + i);
        game::ShopItemDef& shopItem = shopInventoryOverride_[static_cast<std::size_t>(i)];
        const char* preview = shopItem.itemId.empty() ? "(choose item)" : shopItem.itemId.c_str();
        if (ImGui::BeginCombo("Item", preview)) {
            for (const game::ItemDef& item : context.itemDefs) {
                if (ImGui::Selectable(item.id.c_str(), item.id == shopItem.itemId)) {
                    shopItem.itemId = item.id;
                    if (shopItem.buyPrice <= 1) { shopItem.buyPrice = std::max(1, item.value); }
                    if (shopItem.sellPrice <= 1) { shopItem.sellPrice = std::max(1, item.value / 2); }
                    writeCurrentPlacement(context);
                }
            }
            ImGui::EndCombo();
        }
        if (editString("Item ID", shopItem.itemId)) { writeCurrentPlacement(context); }
        if (shopItem.itemId.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Item ID is required.");
        } else if (std::none_of(context.itemDefs.begin(), context.itemDefs.end(),
            [&shopItem](const game::ItemDef& item) { return item.id == shopItem.itemId; })) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Item ID is not defined in the Items tab.");
        }
        if (ImGui::DragInt("Buy", &shopItem.buyPrice, 1.0f, 0, 999999)) { writeCurrentPlacement(context); }
        if (ImGui::DragInt("Sell", &shopItem.sellPrice, 1.0f, 0, 999999)) { writeCurrentPlacement(context); }
        if (ImGui::Checkbox("Unlimited", &shopItem.unlimited)) { writeCurrentPlacement(context); }
        if (!shopItem.unlimited && ImGui::DragInt("Stock", &shopItem.quantity, 1.0f, 0, 999999)) { writeCurrentPlacement(context); }
        if (ImGui::Button("Remove")) {
            shopInventoryOverride_.erase(shopInventoryOverride_.begin() + i);
            writeCurrentPlacement(context);
            ImGui::PopID();
            break;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::Button("+ Override Shop Item", ImVec2(-1.0f, 22.0f))) {
        game::ShopItemDef item;
        if (!context.itemDefs.empty()) {
            item.itemId = context.itemDefs.front().id;
            item.buyPrice = std::max(1, context.itemDefs.front().value);
            item.sellPrice = std::max(1, context.itemDefs.front().value / 2);
        }
        shopInventoryOverride_.push_back(std::move(item));
        writeCurrentPlacement(context);
    }
    if (!shopInventoryOverride_.empty() && ImGui::Button("Clear Override", ImVec2(-1.0f, 22.0f))) {
        shopInventoryOverride_.clear();
        writeCurrentPlacement(context);
    }

    ImGui::Spacing();
    if (ImGui::Button("Remove from Screen", ImVec2(-1.0f, 24.0f))) {
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
        const char* actionTag = wp.action == game::PathWaypointAction::Enter ? " [ENTER]"
            : wp.action == game::PathWaypointAction::Speak ? " [SPEAK]"
            : wp.action == game::PathWaypointAction::Leave ? " [LEAVE]" : "";
        std::snprintf(label, sizeof(label), "WP %d (%.0f, %.0f)%s%s", i, wp.x, wp.y,
            wp.waitSeconds > 0.0f ? " [W]" : "", actionTag);
        if (ImGui::Selectable(label, i == selectedWaypoint_)) {
            selectedWaypoint_ = i;
        }
        ImGui::PopID();
    }
    if (selectedWaypoint_ >= 0 && selectedWaypoint_ < static_cast<int>(waypoints_.size())) {
        ImGui::Separator();
        Waypoint& wp = waypoints_[static_cast<std::size_t>(selectedWaypoint_)];
        ImGui::Text("Waypoint %d", selectedWaypoint_);

        ImGui::TextUnformatted("X");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("##wp_x", &wp.x, 1.0f, 16.0f, "%.0f")) { writeCurrentPlacement(context); }
        ImGui::TextUnformatted("Y");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("##wp_y", &wp.y, 1.0f, 16.0f, "%.0f")) { writeCurrentPlacement(context); }
        ImGui::TextUnformatted("Segment speed");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("##wp_speed", &wp.speedOverride, 1.0f, 10.0f, "%.0f")) { writeCurrentPlacement(context); }
        ImGui::TextUnformatted("Wait (seconds)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputFloat("##wp_wait", &wp.waitSeconds, 0.1f, 0.5f, "%.1f")) { writeCurrentPlacement(context); }
        const char* facingNames[] = {"Unchanged", "South", "North", "East", "West"};
        int facingIdx = wp.facing + 1;
        ImGui::TextUnformatted("Facing");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##wp_facing", &facingIdx, facingNames, 5)) {
            wp.facing = facingIdx - 1;
            writeCurrentPlacement(context);
        }
        char animBuf[64]{};
        std::memcpy(animBuf, wp.animState.data(), std::min(wp.animState.size(), sizeof(animBuf) - 1));
        ImGui::TextUnformatted("Animation");
        ImGui::SetNextItemWidth(-1.0f);
        if (ui::inputTextString("##wp_animation", animBuf, sizeof(animBuf))) {
            wp.animState = animBuf;
            writeCurrentPlacement(context);
        }
        const char* actionNames[] = {"None", "Enter / Show", "Speak", "Leave / Hide"};
        int action = static_cast<int>(wp.action);
        ImGui::TextUnformatted("Action");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##wp_action", &action, actionNames, 4)) {
            wp.action = static_cast<game::PathWaypointAction>(action);
            writeCurrentPlacement(context);
        }
        if (wp.action == game::PathWaypointAction::Speak) {
            ImGui::TextUnformatted("Speech time (seconds)");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputFloat("##wp_speech_time", &wp.speechDurationSeconds, 0.1f, 0.5f, "%.1f")) {
                wp.speechDurationSeconds = std::max(0.0f, wp.speechDurationSeconds);
                writeCurrentPlacement(context);
            }
            char speechBuf[256]{};
            std::memcpy(speechBuf, wp.speechText.data(), std::min(wp.speechText.size(), sizeof(speechBuf) - 1));
            ImGui::TextUnformatted("Speech text");
            ImGui::SetNextItemWidth(-1.0f);
            if (ui::inputTextString("##wp_speech", speechBuf, sizeof(speechBuf))) {
                wp.speechText = speechBuf;
                writeCurrentPlacement(context);
            }
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
        if (curveMode_ == CurveMode::Spline && waypoints_.size() >= 3) {
            const int segmentCount = loop_ ? static_cast<int>(waypoints_.size()) : static_cast<int>(waypoints_.size()) - 1;
            for (int segment = 0; segment < segmentCount; ++segment) {
                Waypoint previous = splinePoint(segment, 0.0f);
                for (int step = 1; step <= 16; ++step) {
                    const Waypoint next = splinePoint(segment, static_cast<float>(step) / 16.0f);
                    dl->AddLine(waypointToCanvas(origin, previous), waypointToCanvas(origin, next), lineCol, 2.0f);
                    previous = next;
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
                    loopLineCol, 2.0f);
            }
        }

        for (int i = 0; i < static_cast<int>(waypoints_.size()); ++i) {
            const ImVec2 wp{origin.x + waypoints_[i].x * zoom_, origin.y + waypoints_[i].y * zoom_};
            const bool sel = i == selectedWaypoint_;
            dl->AddCircleFilled(wp, sel ? kWaypointRadius + 2.0f : kWaypointRadius,
                sel ? IM_COL32(60, 255, 220, 255) : IM_COL32(40, 210, 190, 230));
            dl->AddCircle(wp, sel ? kWaypointRadius + 3.0f : kWaypointRadius + 1.0f, IM_COL32(20, 20, 24, 200), 0, 1.5f);

            const std::string label = std::to_string(i);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 textPos{wp.x + 10.0f, wp.y - textSize.y * 0.5f};
            dl->AddRectFilled(
                {textPos.x - 3.0f, textPos.y - 2.0f},
                {textPos.x + textSize.x + 3.0f, textPos.y + textSize.y + 2.0f},
                IM_COL32(12, 14, 18, 220), 3.0f);
            dl->AddText(textPos, IM_COL32(255, 255, 255, 255), label.c_str());
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
            Waypoint& waypoint = waypoints_[static_cast<std::size_t>(selectedWaypoint_)];
            waypoint.x = sx;
            waypoint.y = sy;
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
    const std::string currentRoot = context.assets.projectRoot.string();
    if (!projectLoaded_ || lastLoadedProjectRoot_ != currentRoot) {
        loadProjectNpcTypes(context);
        projectLoaded_ = true;
        lastLoadedProjectRoot_ = currentRoot;
    }
    if (context.npcTypes.empty()) {
        context.npcTypes.push_back({});
    }
    selectedType_ = std::clamp(selectedType_, 0, static_cast<int>(context.npcTypes.size()) - 1);

    ImGui::TextUnformatted("Project NPC Definitions");
    ImGui::SameLine();
    if (ImGui::Button("New NPC")) {
        game::NpcTypeDef npc;
        npc.id = "npc_" + std::to_string(context.npcTypes.size() + 1);
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

    if (editString("NPC ID", npc.id)) { context.markDirty(); }
    if (ImGui::BeginCombo("Project Character", npc.characterId.empty() ? "-" : npc.characterId.c_str())) {
        if (ImGui::Selectable("-", npc.characterId.empty())) {
            npc.characterId.clear();
            context.markDirty();
        }
        for (const std::string& characterId : context.importedCharacterIds) {
            if (ImGui::Selectable(characterId.c_str(), characterId == npc.characterId)) {
                npc.characterId = characterId;
                context.markDirty();
            }
        }
        ImGui::EndCombo();
    }
    if (editString("Sprite ID Override", npc.spriteId)) { context.markDirty(); }
    ImGui::SameLine();
    if (ImGui::Button("Edit Sprite")) {
        std::filesystem::path spriteReference = characterSpriteReference(context, npc.characterId);
        if (spriteReference.empty() && !npc.spriteId.empty()) {
            spriteReference = context.assets.gameSpritePath() / (npc.spriteId + ".sprite.json");
        }
        if (!spriteReference.empty()) {
            context.requestedSpriteReference = spriteReference.generic_string();
            context.requestEditSprite = true;
        }
    }

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
    if (!npc.defaultGraphId.empty() && ImGui::Button("Open Default Graph")) {
        context.requestedDialogueGraphId = npc.defaultGraphId;
        context.requestEditDialogueGraph = true;
    }

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
        if (ui::inputTextString("Speaker", speakerBuf, sizeof(speakerBuf))) {
            line.speaker = speakerBuf;
            context.markDirty();
        }
        ImGui::SetNextItemWidth(-40.0f);
        if (ui::inputTextString("Text", textBuf, sizeof(textBuf))) {
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

    ImGui::Separator();
    ImGui::TextUnformatted("Shop Inventory");
    if (npc.defaultInteraction != game::NpcInteractionMode::Shop) {
        ImGui::TextDisabled("Set Default Interaction to Shop to use these items at runtime.");
    }
    for (int i = 0; i < static_cast<int>(npc.shopInventory.size()); ++i) {
        ImGui::PushID(5000 + i);
        game::ShopItemDef& shopItem = npc.shopInventory[static_cast<std::size_t>(i)];
        const char* preview = shopItem.itemId.empty() ? "(choose item)" : shopItem.itemId.c_str();
        if (ImGui::BeginCombo("Item", preview)) {
            for (const game::ItemDef& item : context.itemDefs) {
                if (ImGui::Selectable(item.id.c_str(), item.id == shopItem.itemId)) {
                    shopItem.itemId = item.id;
                    if (shopItem.buyPrice <= 1) {
                        shopItem.buyPrice = std::max(1, item.value);
                    }
                    if (shopItem.sellPrice <= 1) {
                        shopItem.sellPrice = std::max(1, item.value / 2);
                    }
                    context.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (editString("Item ID", shopItem.itemId)) { context.markDirty(); }
        if (shopItem.itemId.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Item ID is required.");
        } else if (std::none_of(context.itemDefs.begin(), context.itemDefs.end(),
            [&shopItem](const game::ItemDef& item) { return item.id == shopItem.itemId; })) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Item ID is not defined in the Items tab.");
        }
        if (ImGui::DragInt("Buy Price", &shopItem.buyPrice, 1.0f, 0, 999999)) { context.markDirty(); }
        if (ImGui::DragInt("Sell Price", &shopItem.sellPrice, 1.0f, 0, 999999)) { context.markDirty(); }
        if (ImGui::Checkbox("Unlimited", &shopItem.unlimited)) { context.markDirty(); }
        if (!shopItem.unlimited && ImGui::DragInt("Stock", &shopItem.quantity, 1.0f, 0, 999999)) { context.markDirty(); }
        if (ImGui::Button("Remove Shop Item")) {
            npc.shopInventory.erase(npc.shopInventory.begin() + i);
            context.markDirty();
            ImGui::PopID();
            break;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::Button("+ Add Shop Item")) {
        game::ShopItemDef item;
        if (!context.itemDefs.empty()) {
            item.itemId = context.itemDefs.front().id;
            item.buyPrice = std::max(1, context.itemDefs.front().value);
            item.sellPrice = std::max(1, context.itemDefs.front().value / 2);
        }
        npc.shopInventory.push_back(std::move(item));
        context.markDirty();
    }

    ImGui::Spacing();
    if (context.npcTypes.size() > 1 && ImGui::Button("Delete Project NPC")) {
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
    copyToBuffer(graphId_, npc.graphOverride);
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
    curveMode_ = static_cast<CurveMode>(std::clamp(static_cast<int>(npc.curveMode), 0, 1));
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
        w.action = wp.action;
        w.speechDurationSeconds = wp.speechDurationSeconds;
        w.speechText = wp.speechText;
        waypoints_.push_back(std::move(w));
    }
    dialogueLines_ = npc.dialogueOverride;
    shopInventoryOverride_ = npc.shopInventoryOverride;
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
    npc.graphOverride = graphId_.data();
    npc.facing = facing_;
    npc.awarenessRadius = awarenessRadius_;
    npc.interactionRadius = interactionRadius_;
    npc.movementOverride = static_cast<game::NpcMovementMode>(std::clamp(movementMode_, 0, 2));
    npc.curveMode = static_cast<game::PathCurveMode>(static_cast<int>(curveMode_));
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
        pw.action = wp.action;
        pw.speechDurationSeconds = std::max(0.0f, wp.speechDurationSeconds);
        pw.speechText = wp.speechText;
        npc.waypoints.push_back(std::move(pw));
    }
    if (!npc.waypoints.empty()) {
        npc.x = npc.waypoints.front().x;
        npc.y = npc.waypoints.front().y;
    }
    npc.dialogueOverride = dialogueLines_;
    npc.shopInventoryOverride = shopInventoryOverride_;
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

NpcEditorPanel::Waypoint NpcEditorPanel::splinePoint(int segment, float t) const
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
    Waypoint result;
    result.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
        (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
        (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    result.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    return result;
}

} // namespace adventure::editor
