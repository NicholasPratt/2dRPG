#include "editor/panels/layout_editor_panel.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace adventure::editor {
namespace {

void inputString(const char* label, std::string& value, std::size_t maxSize = 64)
{
    char buffer[128]{};
    const std::size_t copyLen = std::min(value.size(), std::min(maxSize, sizeof(buffer) - 1));
    std::memcpy(buffer, value.data(), copyLen);
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        value = buffer;
        if (value.size() > maxSize) {
            value.resize(maxSize);
        }
    }
}

ImU32 screenColor(bool selected, bool start)
{
    if (selected) {
        return IM_COL32(236, 203, 49, 255);
    }
    if (start) {
        return IM_COL32(68, 155, 255, 255);
    }
    return IM_COL32(80, 90, 102, 255);
}

} // namespace

void LayoutEditorPanel::draw(EditorContext& context)
{
    if (chapter_.screens.empty()) {
        chapter_.screens.push_back(game::ChapterScreen{});
    }
    selectedScreen_ = std::clamp(selectedScreen_, 0, static_cast<int>(chapter_.screens.size()) - 1);

    drawToolbar(context);
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = std::min(260.0f, std::max(190.0f, available.x * 0.24f));
    const float rightWidth = std::min(360.0f, std::max(280.0f, available.x * 0.30f));
    const float centerWidth = std::max(280.0f, available.x - leftWidth - rightWidth - 16.0f);

    ImGui::BeginChild("LayoutScreenList", ImVec2(leftWidth, 0.0f), true);
    drawScreenList();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("LayoutMacroView", ImVec2(centerWidth, 0.0f), true);
    drawMacroView();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("LayoutInspector", ImVec2(0.0f, 0.0f), true);
    drawScreenInspector();
    ImGui::EndChild();
}

void LayoutEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Chapter id", chapterId_.data(), chapterId_.size());
    chapter_.id = chapterId_.data();

    ImGui::SameLine();
    if (ImGui::Button("New screen")) {
        addScreen();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete screen")) {
        deleteSelectedScreen();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save .adchapter")) {
        saveChapter(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .adchapter")) {
        loadChapter(context);
    }

    ImGui::Text("Chapter files: %s", context.assets.gameChapterPath().string().c_str());
    ImGui::TextDisabled("A chapter is a macro layout of linked screens. Each screen points at one .admap.");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void LayoutEditorPanel::drawScreenList()
{
    ImGui::TextUnformatted("Screens");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
        ImGui::PushID(i);
        const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(i)];
        const bool start = screen.id == chapter_.startScreenId;
        std::string label = screen.id + "  [" + std::to_string(screen.gridX) + "," + std::to_string(screen.gridY) + "]";
        if (start) {
            label += "  start";
        }
        if (ImGui::Selectable(label.c_str(), selectedScreen_ == i, 0, ImVec2(0.0f, 32.0f))) {
            selectedScreen_ = i;
        }
        ImGui::PopID();
    }
}

void LayoutEditorPanel::drawMacroView()
{
    ImGui::TextUnformatted("Macro Layout");
    ImGui::Separator();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("ChapterMacroCanvas", canvasSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, IM_COL32(29, 32, 37, 255));

    constexpr float cellW = 128.0f;
    constexpr float cellH = 86.0f;
    const ImVec2 center{origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f};

    for (int i = 0; i < static_cast<int>(chapter_.screens.size()); ++i) {
        const game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(i)];
        const ImVec2 min{
            center.x + static_cast<float>(screen.gridX) * cellW - 48.0f,
            center.y + static_cast<float>(screen.gridY) * cellH - 28.0f,
        };
        const ImVec2 max{min.x + 96.0f, min.y + 56.0f};
        const bool selected = selectedScreen_ == i;
        const bool start = screen.id == chapter_.startScreenId;

        drawList->AddRectFilled(min, max, screenColor(selected, start), 4.0f);
        drawList->AddRect(min, max, IM_COL32(12, 15, 18, 255), 4.0f, 0, 2.0f);
        drawList->AddText({min.x + 8.0f, min.y + 8.0f}, selected ? IM_COL32(24, 25, 28, 255) : IM_COL32(238, 241, 245, 255), screen.id.c_str());
        drawList->AddText({min.x + 8.0f, min.y + 30.0f}, selected ? IM_COL32(24, 25, 28, 210) : IM_COL32(190, 198, 208, 255), screen.mapId.c_str());

        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(i);
        ImGui::InvisibleButton("ScreenHit", {96.0f, 56.0f});
        if (ImGui::IsItemClicked()) {
            selectedScreen_ = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s -> %s", screen.id.c_str(), screen.mapId.c_str());
        }
        ImGui::PopID();

        auto drawLink = [&](const std::string& targetId, const ImVec2& from) {
            const game::ChapterScreen* target = game::findScreen(chapter_, targetId);
            if (target == nullptr) {
                return;
            }
            const ImVec2 to{
                center.x + static_cast<float>(target->gridX) * cellW,
                center.y + static_cast<float>(target->gridY) * cellH,
            };
            drawList->AddLine(from, to, IM_COL32(160, 174, 190, 180), 2.0f);
        };
        drawLink(screen.links.north, {min.x + 48.0f, min.y});
        drawLink(screen.links.south, {min.x + 48.0f, max.y});
        drawLink(screen.links.east, {max.x, min.y + 28.0f});
        drawLink(screen.links.west, {min.x, min.y + 28.0f});
    }
}

void LayoutEditorPanel::drawScreenInspector()
{
    if (!selectedScreenValid()) {
        ImGui::TextDisabled("No screen selected.");
        return;
    }

    game::ChapterScreen& screen = chapter_.screens[static_cast<std::size_t>(selectedScreen_)];
    ImGui::TextUnformatted("Screen");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    inputString("Screen id", screen.id);
    ImGui::SetNextItemWidth(-1.0f);
    inputString("Map id", screen.mapId);

    int grid[2]{screen.gridX, screen.gridY};
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputInt2("Grid", grid)) {
        screen.gridX = std::clamp(grid[0], -512, 512);
        screen.gridY = std::clamp(grid[1], -512, 512);
    }

    if (ImGui::Button("Set as start")) {
        chapter_.startScreenId = screen.id;
    }
    ImGui::SameLine();
    ImGui::Text("Start: %s", chapter_.startScreenId.c_str());

    ImGui::Spacing();
    ImGui::Checkbox("Respawn enemies on re-enter", &screen.respawnEnemies);
    ImGui::TextDisabled("Off = defeated enemies stay gone (spec default).");

    ImGui::Spacing();
    ImGui::TextUnformatted("Links");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    inputString("North", screen.links.north);
    ImGui::SetNextItemWidth(-1.0f);
    inputString("South", screen.links.south);
    ImGui::SetNextItemWidth(-1.0f);
    inputString("East", screen.links.east);
    ImGui::SetNextItemWidth(-1.0f);
    inputString("West", screen.links.west);
}

void LayoutEditorPanel::addScreen()
{
    game::ChapterScreen screen;
    const int index = static_cast<int>(chapter_.screens.size()) + 1;
    screen.id = "screen_" + std::to_string(index);
    screen.mapId = "new_map";
    screen.gridX = index - 1;
    screen.gridY = 0;
    chapter_.screens.push_back(std::move(screen));
    selectedScreen_ = static_cast<int>(chapter_.screens.size()) - 1;
}

void LayoutEditorPanel::deleteSelectedScreen()
{
    if (chapter_.screens.size() <= 1 || !selectedScreenValid()) {
        status_ = "A chapter must keep at least one screen.";
        return;
    }

    const std::string deletedId = chapter_.screens[static_cast<std::size_t>(selectedScreen_)].id;
    chapter_.screens.erase(chapter_.screens.begin() + selectedScreen_);
    selectedScreen_ = std::clamp(selectedScreen_, 0, static_cast<int>(chapter_.screens.size()) - 1);

    for (game::ChapterScreen& screen : chapter_.screens) {
        if (screen.links.north == deletedId) screen.links.north.clear();
        if (screen.links.south == deletedId) screen.links.south.clear();
        if (screen.links.east == deletedId) screen.links.east.clear();
        if (screen.links.west == deletedId) screen.links.west.clear();
    }
    if (chapter_.startScreenId == deletedId) {
        chapter_.startScreenId = chapter_.screens.front().id;
    }
}

void LayoutEditorPanel::saveChapter(EditorContext& context)
{
    chapter_.id = chapterId_.data();
    if (game::findScreen(chapter_, chapter_.startScreenId) == nullptr && !chapter_.screens.empty()) {
        chapter_.startScreenId = chapter_.screens.front().id;
    }

    std::string error;
    const std::filesystem::path outputPath = context.assets.gameChapterPath() / (chapter_.id + ".adchapter");
    if (game::saveChapter(outputPath, chapter_, &error)) {
        status_ = "Saved chapter: " + outputPath.generic_string();
    } else {
        status_ = "Failed to save chapter: " + error;
    }
}

void LayoutEditorPanel::loadChapter(EditorContext& context)
{
    const std::filesystem::path inputPath = context.assets.gameChapterPath() / (std::string(chapterId_.data()) + ".adchapter");

    std::string error;
    game::Chapter loaded;
    if (!game::loadChapter(inputPath, loaded, &error)) {
        status_ = "Failed to load chapter: " + error;
        return;
    }

    chapter_ = std::move(loaded);
    selectedScreen_ = 0;
    syncChapterIdBuffer();
    status_ = "Loaded chapter: " + inputPath.generic_string();
}

void LayoutEditorPanel::syncChapterIdBuffer()
{
    std::memset(chapterId_.data(), 0, chapterId_.size());
    const std::size_t copyLen = std::min(chapter_.id.size(), chapterId_.size() - 1);
    std::memcpy(chapterId_.data(), chapter_.id.data(), copyLen);
}

bool LayoutEditorPanel::selectedScreenValid() const
{
    return selectedScreen_ >= 0 && selectedScreen_ < static_cast<int>(chapter_.screens.size());
}

} // namespace adventure::editor
