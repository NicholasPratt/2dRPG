#include "editor/panels/chapter_exit_panel.hpp"

#include "editor/editor_theme.hpp"
#include "editor/imgui_widgets.hpp"
#include "game/constants.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace adventure::editor {
namespace {

bool editString(const char* label, std::string& value, std::size_t maxSize = 64)
{
    char buffer[256]{};
    const std::size_t length = std::min(value.size(), std::min(maxSize, sizeof(buffer) - 1));
    std::memcpy(buffer, value.data(), length);
    if (!ui::inputTextString(label, buffer, sizeof(buffer))) {
        return false;
    }
    value = buffer;
    if (value.size() > maxSize) {
        value.resize(maxSize);
    }
    return true;
}

const char* conditionName(game::GameConditionType type)
{
    switch (type) {
        case game::GameConditionType::Always: return "Always";
        case game::GameConditionType::IntCompare: return "Integer comparison";
        case game::GameConditionType::BoolEquals: return "Boolean";
        case game::GameConditionType::HasItem: return "Has item";
        case game::GameConditionType::HasMoney: return "Money at least";
    }
    return "Always";
}

const char* compareName(game::GameCompareOp op)
{
    switch (op) {
        case game::GameCompareOp::Equal: return "==";
        case game::GameCompareOp::NotEqual: return "!=";
        case game::GameCompareOp::Less: return "<";
        case game::GameCompareOp::LessOrEqual: return "<=";
        case game::GameCompareOp::Greater: return ">";
        case game::GameCompareOp::GreaterOrEqual: return ">=";
    }
    return ">=";
}

ImVec2 tileToCanvas(ImVec2 origin, int x, int y, float scale)
{
    return {
        origin.x + static_cast<float>(x * game::kTileSize) * scale,
        origin.y + static_cast<float>(y * game::kTileSize) * scale,
    };
}

std::string sanitizedId(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if (std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.') {
            result.push_back(static_cast<char>(c));
        } else if (!result.empty() && result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result;
}

} // namespace

void ChapterExitPanel::openForScreen(EditorContext& context)
{
    selectedExit_ = -1;
    loadedMapId_.clear();
    bgMapLoaded_ = false;
    pickingTargetTile_ = false;
    targetMapLoaded_ = false;
    loadedTargetChapterId_.clear();
    loadedTargetScreenId_.clear();
    loadBackground(context);
}

void ChapterExitPanel::saveForScreen(EditorContext& context)
{
    if (context.selectedScreenChapterExitsMapId.empty()) {
        return;
    }
    game::TileMap map;
    const std::filesystem::path path = context.assets.gameMapPath() /
        (context.selectedScreenChapterExitsMapId + ".admap");
    if (!game::loadTileMap(path, map, nullptr)) {
        status_ = "Could not load map for saving.";
        return;
    }
    map.chapterExits = context.selectedScreenChapterExits;
    std::string error;
    if (game::saveTileMap(path, map, &error)) {
        status_ = "Saved chapter exits.";
    } else {
        status_ = "Save failed: " + error;
    }
}

void ChapterExitPanel::loadBackground(EditorContext& context)
{
    if (context.selectedScreenMapId.empty() || context.selectedScreenMapId == loadedMapId_) {
        return;
    }
    loadedMapId_ = context.selectedScreenMapId;
    bgMapLoaded_ = game::loadTileMap(
        context.assets.gameMapPath() / (loadedMapId_ + ".admap"), bgMap_, nullptr);
}

void ChapterExitPanel::draw(EditorContext& context)
{
    loadBackground(context);
    ImGui::Text("Screen: %s", context.selectedScreenId.empty() ? "(none)" : context.selectedScreenId.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Zoom##chapter_exit", &zoom_, 0.05f, 0.5f, 4.0f);
    ImGui::Separator();

    const float leftWidth = 330.0f;
    ImGui::BeginChild("ChapterExitInspector", ImVec2(leftWidth, 0.0f), true);
    drawList(context);
    ImGui::Separator();
    drawInspector(context);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ChapterExitCanvas", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
}

void ChapterExitPanel::drawList(EditorContext& context)
{
    if (ImGui::Button("Add Chapter Exit", ImVec2(-1.0f, 0.0f))) {
        game::MapChapterExitPlacement exit;
        exit.id = "chapter_exit_" + std::to_string(context.selectedScreenChapterExits.size() + 1);
        exit.x = bgMapLoaded_ ? bgMap_.width / 2 : game::kScreenTilesW / 2;
        exit.y = bgMapLoaded_ ? bgMap_.height / 2 : game::kScreenTilesH / 2;
        exit.targetChapterId = context.currentChapterId;
        context.selectedScreenChapterExits.push_back(std::move(exit));
        selectedExit_ = static_cast<int>(context.selectedScreenChapterExits.size()) - 1;
        context.markDirty();
    }
    if (selectedExit_ >= 0 && selectedExit_ < static_cast<int>(context.selectedScreenChapterExits.size()) &&
        ImGui::Button("Delete Chapter Exit", ImVec2(-1.0f, 0.0f))) {
        context.selectedScreenChapterExits.erase(
            context.selectedScreenChapterExits.begin() + selectedExit_);
        selectedExit_ = std::min(selectedExit_,
            static_cast<int>(context.selectedScreenChapterExits.size()) - 1);
        context.markDirty();
    }
    for (int i = 0; i < static_cast<int>(context.selectedScreenChapterExits.size()); ++i) {
        ImGui::PushID(i);
        const game::MapChapterExitPlacement& exit =
            context.selectedScreenChapterExits[static_cast<std::size_t>(i)];
        if (ImGui::Selectable(exit.id.empty() ? "(unnamed exit)" : exit.id.c_str(), selectedExit_ == i)) {
            selectedExit_ = i;
            pickingTargetTile_ = false;
        }
        ImGui::PopID();
    }
}

void ChapterExitPanel::drawInspector(EditorContext& context)
{
    if (selectedExit_ < 0 || selectedExit_ >= static_cast<int>(context.selectedScreenChapterExits.size())) {
        ImGui::TextDisabled("Select or add a chapter exit.");
        return;
    }
    game::MapChapterExitPlacement& exit =
        context.selectedScreenChapterExits[static_cast<std::size_t>(selectedExit_)];
    if (editString("Exit ID", exit.id)) context.markDirty();
    if (ImGui::DragInt("Tile X", &exit.x, 1.0f, 0, bgMapLoaded_ ? bgMap_.width - 1 : 127)) context.markDirty();
    if (ImGui::DragInt("Tile Y", &exit.y, 1.0f, 0, bgMapLoaded_ ? bgMap_.height - 1 : 127)) context.markDirty();
    if (ImGui::DragInt("Tile W", &exit.widthTiles, 1.0f, 1,
            bgMapLoaded_ ? std::max(1, bgMap_.width - exit.x) : 128)) context.markDirty();
    if (ImGui::DragInt("Tile H", &exit.heightTiles, 1.0f, 1,
            bgMapLoaded_ ? std::max(1, bgMap_.height - exit.y) : 128)) context.markDirty();

    const char* activations[] = {"Interact", "Enter Area", "Condition Change", "Enter Area + Condition"};
    int activation = static_cast<int>(exit.activation);
    if (ImGui::Combo("Activation", &activation, activations, 4)) {
        exit.activation = static_cast<game::ChapterExitActivation>(activation);
        context.markDirty();
    }
    drawCondition(context, exit);
    drawTargetPicker(context, exit);
    if (ImGui::Checkbox("One shot", &exit.oneShot)) context.markDirty();
    if (editString("Transition sound", exit.transitionSoundPath, 240)) context.markDirty();
    if (ImGui::Button(pickingTargetTile_ ? "Picking target tile..." : "Pick target spawn",
            ImVec2(-1.0f, 0.0f))) {
        pickingTargetTile_ = !pickingTargetTile_;
        targetMapLoaded_ = false;
    }
    drawValidation(context);
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void ChapterExitPanel::drawCondition(EditorContext& context, game::MapChapterExitPlacement& exit)
{
    ImGui::SeparatorText("Condition");
    int type = static_cast<int>(exit.condition.type);
    if (ImGui::BeginCombo("Type", conditionName(exit.condition.type))) {
        for (int i = 0; i <= 4; ++i) {
            const auto candidate = static_cast<game::GameConditionType>(i);
            if (ImGui::Selectable(conditionName(candidate), type == i)) {
                exit.condition.type = candidate;
                context.markDirty();
            }
        }
        ImGui::EndCombo();
    }
    if (exit.condition.type == game::GameConditionType::Always) {
        return;
    }
    if (exit.condition.type == game::GameConditionType::IntCompare ||
        exit.condition.type == game::GameConditionType::BoolEquals) {
        int scope = static_cast<int>(exit.condition.scope);
        if (ImGui::Combo("Scope", &scope, "Universal\0Chapter\0")) {
            exit.condition.scope = static_cast<game::StateVariableScope>(scope);
            context.markDirty();
        }
    }
    if (exit.condition.type != game::GameConditionType::HasMoney) {
        if (editString(exit.condition.type == game::GameConditionType::HasItem ? "Item ID" : "Variable ID",
                exit.condition.variableId)) {
            context.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Pick##chapter_exit_condition")) {
            const int index = selectedExit_;
            const game::StateVariableType variableType =
                exit.condition.type == game::GameConditionType::IntCompare
                ? game::StateVariableType::Integer
                : (exit.condition.type == game::GameConditionType::BoolEquals
                    ? game::StateVariableType::Boolean : game::StateVariableType::Item);
            context.openVariablePicker(exit.condition.variableId, variableType, exit.condition.scope,
                [&context, index](const game::StateVariableDef& variable) {
                    if (index >= 0 && index < static_cast<int>(context.selectedScreenChapterExits.size())) {
                        auto& condition = context.selectedScreenChapterExits[static_cast<std::size_t>(index)].condition;
                        condition.variableId = variable.id;
                        condition.scope = variable.scope;
                        context.markDirty();
                    }
                });
        }
    }
    if (exit.condition.type == game::GameConditionType::IntCompare) {
        if (ImGui::BeginCombo("Operator", compareName(exit.condition.op))) {
            for (int i = 0; i <= 5; ++i) {
                const auto candidate = static_cast<game::GameCompareOp>(i);
                if (ImGui::Selectable(compareName(candidate), exit.condition.op == candidate)) {
                    exit.condition.op = candidate;
                    context.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragInt("Value", &exit.condition.intValue)) context.markDirty();
    } else if (exit.condition.type == game::GameConditionType::BoolEquals ||
               exit.condition.type == game::GameConditionType::HasItem) {
        if (ImGui::Checkbox("Expected", &exit.condition.boolValue)) context.markDirty();
    } else if (exit.condition.type == game::GameConditionType::HasMoney) {
        if (ImGui::DragInt("Required money", &exit.condition.intValue, 1.0f, 0)) context.markDirty();
    }
}

std::vector<std::string> ChapterExitPanel::chapterIds(const EditorContext& context) const
{
    std::vector<std::string> ids;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator(context.assets.gameChapterPath(), error)) {
        if (!error && entry.is_regular_file(error) && entry.path().extension() == ".adchapter") {
            ids.push_back(entry.path().stem().string());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ChapterExitPanel::drawTargetPicker(EditorContext& context, game::MapChapterExitPlacement& exit)
{
    ImGui::SeparatorText("Destination");
    ImGui::SetNextItemWidth(-1.0f);
    ui::inputTextString("New chapter ID", newChapterId_.data(), newChapterId_.size());
    if (ImGui::Button("Create Chapter", ImVec2(-1.0f, 0.0f))) {
        (void)createDestinationChapter(context, exit);
    }

    const std::vector<std::string> chapters = chapterIds(context);
    if (ImGui::BeginCombo("Chapter", exit.targetChapterId.empty() ? "(none)" : exit.targetChapterId.c_str())) {
        for (const std::string& id : chapters) {
            if (ImGui::Selectable(id.c_str(), id == exit.targetChapterId)) {
                exit.targetChapterId = id;
                exit.targetScreenId.clear();
                targetMapLoaded_ = false;
                context.markDirty();
            }
        }
        ImGui::EndCombo();
    }

    game::Chapter chapter;
    const bool chapterLoaded = !exit.targetChapterId.empty() &&
        game::loadChapter(context.assets.gameChapterPath() / (exit.targetChapterId + ".adchapter"),
            chapter, nullptr);
    if (ImGui::BeginCombo("Screen", exit.targetScreenId.empty() ? "(none)" : exit.targetScreenId.c_str())) {
        if (chapterLoaded) {
            for (const game::ChapterScreen& screen : chapter.screens) {
                if (ImGui::Selectable(screen.id.c_str(), screen.id == exit.targetScreenId)) {
                    exit.targetScreenId = screen.id;
                    targetMapLoaded_ = false;
                    context.markDirty();
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::DragInt("Target X", &exit.targetTileX, 1.0f, 0, 127)) context.markDirty();
    if (ImGui::DragInt("Target Y", &exit.targetTileY, 1.0f, 0, 127)) context.markDirty();
}

bool ChapterExitPanel::createDestinationChapter(
    EditorContext& context, game::MapChapterExitPlacement& exit)
{
    const std::string chapterId = sanitizedId(newChapterId_.data());
    if (chapterId.empty()) {
        status_ = "Enter a valid chapter ID.";
        return false;
    }

    const std::filesystem::path chapterPath =
        context.assets.gameChapterPath() / (chapterId + ".adchapter");
    std::error_code filesystemError;
    if (std::filesystem::exists(chapterPath, filesystemError)) {
        status_ = "Chapter already exists: " + chapterId;
        return false;
    }

    const std::string mapId = chapterId + "_screen_1_map";
    const std::filesystem::path mapPath = context.assets.gameMapPath() / (mapId + ".admap");
    if (std::filesystem::exists(mapPath, filesystemError)) {
        status_ = "First map already exists: " + mapId;
        return false;
    }

    std::string directoryError;
    if (!context.assets.ensureRequiredPaths(&directoryError)) {
        status_ = "Could not create project folders: " + directoryError;
        return false;
    }
    std::filesystem::create_directories(
        context.assets.gameDialoguePath() / chapterId, filesystemError);
    if (filesystemError) {
        status_ = "Could not create chapter dialogue folder: " + filesystemError.message();
        return false;
    }

    game::TileMap map;
    map.id = mapId;
    map.width = game::kScreenTilesW;
    map.height = game::kScreenTilesH;
    map.spawnX = 1;
    map.spawnY = 1;
    const std::size_t mapSize = static_cast<std::size_t>(map.width * map.height);
    for (auto& layer : map.layers) {
        layer.assign(mapSize, 0u);
    }
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (x == 0 || y == 0 || x + 1 == map.width || y + 1 == map.height) {
                map.layers[1][static_cast<std::size_t>(y * map.width + x)] = 1u;
            }
        }
    }

    std::string error;
    if (!game::saveTileMap(mapPath, map, &error)) {
        status_ = "Could not create first map: " + error;
        return false;
    }

    game::Chapter chapter;
    chapter.id = chapterId;
    chapter.startScreenId = "screen_1";
    chapter.playableCharacterId = context.playableCharacterId;
    chapter.importedCharacterIds = context.importedCharacterIds;
    chapter.screens.front().id = "screen_1";
    chapter.screens.front().mapId = mapId;
    if (!game::saveChapter(chapterPath, chapter, &error)) {
        status_ = "Could not create chapter: " + error;
        return false;
    }

    game::GameProject project;
    const std::filesystem::path projectPath =
        context.assets.projectRoot / "assets/game/project.adgame";
    if (game::loadGameProject(projectPath, project, nullptr)) {
        if (std::find(project.chapterIds.begin(), project.chapterIds.end(), chapterId) ==
            project.chapterIds.end()) {
            project.chapterIds.push_back(chapterId);
            std::sort(project.chapterIds.begin(), project.chapterIds.end());
            if (!game::saveGameProject(projectPath, project, &error)) {
                status_ = "Chapter created, but project registration failed: " + error;
                return false;
            }
        }
    }

    exit.targetChapterId = chapterId;
    exit.targetScreenId = chapter.startScreenId;
    exit.targetTileX = map.spawnX;
    exit.targetTileY = map.spawnY;
    loadedTargetChapterId_.clear();
    loadedTargetScreenId_.clear();
    targetMapLoaded_ = false;
    context.markDirty();
    status_ = "Created chapter " + chapterId + ", first map " + mapId +
        ", and assets/game/dialogue/" + chapterId + "/.";
    return true;
}

bool ChapterExitPanel::loadTarget(EditorContext& context, const game::MapChapterExitPlacement& exit)
{
    if (exit.targetChapterId == loadedTargetChapterId_ &&
        exit.targetScreenId == loadedTargetScreenId_) {
        return targetMapLoaded_;
    }
    loadedTargetChapterId_ = exit.targetChapterId;
    loadedTargetScreenId_ = exit.targetScreenId;
    targetMapLoaded_ = false;
    if (!game::loadChapter(context.assets.gameChapterPath() / (exit.targetChapterId + ".adchapter"),
            targetChapter_, nullptr)) {
        return false;
    }
    const game::ChapterScreen* screen = game::findScreen(targetChapter_, exit.targetScreenId);
    if (screen == nullptr) {
        return false;
    }
    targetMapLoaded_ = game::loadTileMap(
        context.assets.gameMapPath() / (screen->mapId + ".admap"), targetMap_, nullptr);
    return targetMapLoaded_;
}

void ChapterExitPanel::drawCanvas(EditorContext& context)
{
    if (selectedExit_ >= 0 && selectedExit_ < static_cast<int>(context.selectedScreenChapterExits.size()) &&
        pickingTargetTile_) {
        game::MapChapterExitPlacement& exit =
            context.selectedScreenChapterExits[static_cast<std::size_t>(selectedExit_)];
        if (!loadTarget(context, exit)) {
            ImGui::TextDisabled("Select a valid destination chapter and screen.");
            return;
        }
        const float tile = static_cast<float>(game::kTileSize) * zoom_;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size{targetMap_.width * tile, targetMap_.height * tile};
        ImGui::InvisibleButton("ChapterExitTargetCanvas", size, ImGuiButtonFlags_MouseButtonLeft);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, editorCanvasColor());
        for (int y = 0; y < targetMap_.height; ++y) {
            for (int x = 0; x < targetMap_.width; ++x) {
                const ImVec2 min{origin.x + x * tile, origin.y + y * tile};
                if (targetMap_.layers[1][static_cast<std::size_t>(y * targetMap_.width + x)] != 0u) {
                    drawList->AddRectFilled(min, {min.x + tile, min.y + tile}, IM_COL32(170, 120, 70, 180));
                }
                drawList->AddRect(min, {min.x + tile, min.y + tile}, IM_COL32(70, 75, 84, 120));
            }
        }
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int x = std::clamp(static_cast<int>((mouse.x - origin.x) / tile), 0, targetMap_.width - 1);
            const int y = std::clamp(static_cast<int>((mouse.y - origin.y) / tile), 0, targetMap_.height - 1);
            const ImVec2 min{origin.x + x * tile, origin.y + y * tile};
            drawList->AddRect(min, {min.x + tile, min.y + tile}, IM_COL32(80, 220, 255, 255), 0, 0, 2.0f);
            ImGui::SetTooltip("Set destination [%d,%d]", x, y);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                exit.targetTileX = x;
                exit.targetTileY = y;
                pickingTargetTile_ = false;
                context.markDirty();
            }
        }
        return;
    }

    if (!bgMapLoaded_) {
        ImGui::TextDisabled("Current map could not be loaded.");
        return;
    }
    const float tile = static_cast<float>(game::kTileSize) * zoom_;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{bgMap_.width * tile, bgMap_.height * tile};
    ImGui::InvisibleButton("ChapterExitSourceCanvas", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, editorCanvasColor());
    for (int y = 0; y < bgMap_.height; ++y) {
        for (int x = 0; x < bgMap_.width; ++x) {
            const ImVec2 min{origin.x + x * tile, origin.y + y * tile};
            if (bgMap_.layers[1][static_cast<std::size_t>(y * bgMap_.width + x)] != 0u) {
                drawList->AddRectFilled(min, {min.x + tile, min.y + tile}, IM_COL32(170, 120, 70, 180));
            }
            drawList->AddRect(min, {min.x + tile, min.y + tile}, IM_COL32(70, 75, 84, 120));
        }
    }
    for (int i = 0; i < static_cast<int>(context.selectedScreenChapterExits.size()); ++i) {
        const game::MapChapterExitPlacement& exit =
            context.selectedScreenChapterExits[static_cast<std::size_t>(i)];
        const ImVec2 min = tileToCanvas(origin, exit.x, exit.y, zoom_);
        const ImVec2 max = tileToCanvas(origin, exit.x + std::max(1, exit.widthTiles),
            exit.y + std::max(1, exit.heightTiles), zoom_);
        drawList->AddRectFilled(min, max, i == selectedExit_
            ? IM_COL32(70, 220, 255, 90) : IM_COL32(70, 220, 255, 45));
        drawList->AddRect(min, max, i == selectedExit_
            ? IM_COL32(90, 240, 255, 255) : IM_COL32(80, 180, 220, 210), 0, 0, 2.0f);
    }
    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = std::clamp(static_cast<int>((mouse.x - origin.x) / tile), 0, bgMap_.width - 1);
        const int y = std::clamp(static_cast<int>((mouse.y - origin.y) / tile), 0, bgMap_.height - 1);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = -1;
            for (int i = static_cast<int>(context.selectedScreenChapterExits.size()) - 1; i >= 0; --i) {
                const auto& exit = context.selectedScreenChapterExits[static_cast<std::size_t>(i)];
                if (x >= exit.x && y >= exit.y && x < exit.x + exit.widthTiles && y < exit.y + exit.heightTiles) {
                    hit = i;
                    break;
                }
            }
            if (hit >= 0) {
                selectedExit_ = hit;
            } else if (selectedExit_ >= 0 && selectedExit_ < static_cast<int>(context.selectedScreenChapterExits.size())) {
                auto& exit = context.selectedScreenChapterExits[static_cast<std::size_t>(selectedExit_)];
                exit.x = x;
                exit.y = y;
                context.markDirty();
            }
        }
    }
}

void ChapterExitPanel::drawValidation(EditorContext& context)
{
    if (selectedExit_ < 0 || selectedExit_ >= static_cast<int>(context.selectedScreenChapterExits.size())) {
        return;
    }
    const game::MapChapterExitPlacement& exit =
        context.selectedScreenChapterExits[static_cast<std::size_t>(selectedExit_)];
    std::vector<std::string> warnings;
    if (exit.id.empty()) warnings.push_back("Exit ID is empty.");
    if (!exit.id.empty() && std::count_if(
            context.selectedScreenChapterExits.begin(), context.selectedScreenChapterExits.end(),
            [&exit](const game::MapChapterExitPlacement& other) { return other.id == exit.id; }) > 1) {
        warnings.push_back("Exit ID is duplicated on this screen.");
    }
    if (bgMapLoaded_ && (exit.x < 0 || exit.y < 0 ||
        exit.x + exit.widthTiles > bgMap_.width || exit.y + exit.heightTiles > bgMap_.height)) {
        warnings.push_back("Exit rectangle is outside the source map.");
    }
    if (exit.targetChapterId.empty()) warnings.push_back("Target chapter is empty.");
    if (exit.targetScreenId.empty()) warnings.push_back("Target screen is empty.");
    if (exit.condition.type != game::GameConditionType::Always &&
        exit.condition.type != game::GameConditionType::HasMoney && exit.condition.variableId.empty()) {
        warnings.push_back("Condition variable/item is empty.");
    } else if (exit.condition.type == game::GameConditionType::IntCompare ||
               exit.condition.type == game::GameConditionType::BoolEquals) {
        const game::StateVariableType expected = exit.condition.type == game::GameConditionType::IntCompare
            ? game::StateVariableType::Integer : game::StateVariableType::Boolean;
        const bool found = std::any_of(context.stateVariables.begin(), context.stateVariables.end(),
            [&exit, expected](const game::StateVariableDef& variable) {
                return variable.id == exit.condition.variableId &&
                    variable.type == expected && variable.scope == exit.condition.scope;
            });
        if (!found) warnings.push_back("Condition variable is not defined with the selected type and scope.");
    } else if (exit.condition.type == game::GameConditionType::HasItem) {
        const bool found = std::any_of(context.itemDefs.begin(), context.itemDefs.end(),
            [&exit](const game::ItemDef& item) { return item.id == exit.condition.variableId; });
        if (!found) warnings.push_back("Condition item is not defined in the Items tab.");
    }
    game::Chapter chapter;
    if (!exit.targetChapterId.empty() &&
        !game::loadChapter(context.assets.gameChapterPath() / (exit.targetChapterId + ".adchapter"),
            chapter, nullptr)) {
        warnings.push_back("Target chapter cannot be loaded.");
    } else if (!exit.targetScreenId.empty() && game::findScreen(chapter, exit.targetScreenId) == nullptr) {
        warnings.push_back("Target screen does not exist in the target chapter.");
    } else if (!exit.targetScreenId.empty()) {
        const game::ChapterScreen* screen = game::findScreen(chapter, exit.targetScreenId);
        game::TileMap target;
        if (screen == nullptr || !game::loadTileMap(
                context.assets.gameMapPath() / (screen->mapId + ".admap"), target, nullptr)) {
            warnings.push_back("Target screen map cannot be loaded.");
        } else if (exit.targetTileX < 0 || exit.targetTileY < 0 ||
                   exit.targetTileX >= target.width || exit.targetTileY >= target.height) {
            warnings.push_back("Target spawn is outside the destination map.");
        } else {
            const std::size_t index = static_cast<std::size_t>(
                exit.targetTileY * target.width + exit.targetTileX);
            if (index < target.layers[1].size() && target.layers[1][index] != 0u) {
                warnings.push_back("Target spawn is blocked by a wall tile.");
            }
        }
    }
    if (!warnings.empty()) {
        ImGui::SeparatorText("Warnings");
        for (const std::string& warning : warnings) {
            ImGui::BulletText("%s", warning.c_str());
        }
    }
}

} // namespace adventure::editor
