#include "editor/editor_app.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace adventure::editor {

void EditorApp::draw()
{
    if (chapterIds_.empty()) {
        refreshChapterList();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Adventure Editor", nullptr, flags);

    drawChapterMenu();

    if (!startupChapterChosen_) {
        drawStartupChapterModal();
        ImGui::End();
        return;
    }

    if (pendingExit_ || pendingChapterSwitch_) {
        drawUnsavedChangesModal();
    }

    if (context_.requestChapterSwitch) {
        context_.requestChapterSwitch = false;
        requestChapterSwitch(context_.requestedChapterSwitchId);
        context_.requestedChapterSwitchId.clear();
    }

    if (context_.requestEditScreenGraphics) {
        context_.requestEditScreenGraphics = false;
        if (!context_.selectedScreenMapId.empty()) {
            layoutEditor_.saveDirtyMaps(context_);
            wallFloorPaint_.openScreenGraphics(context_, context_.selectedScreenMapId);
        }
        screenGraphicsMode_ = false;
        requestedTab_ = MainTab::WallFloorPaint;
        hasRequestedTab_ = true;
        spriteEditorLaunchedFromCharacter_ = false;
    }

    if (ImGui::BeginTabBar("EditorMainTabs")) {
        ImGuiTabItemFlags characterTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Characters ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Characters", nullptr, characterTabFlags)) {
            if (characterTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            if (auto spriteToOpen = characterEditor_.draw(context_)) {
                spriteEditor_.openCharacterSpriteReference(*spriteToOpen);
                characterEditor_.setSelectedSpriteReference(spriteEditor_.spriteMetadataReference(context_));
                spriteEditorLaunchedFromCharacter_ = true;
                requestedTab_ = MainTab::Sprites;
                hasRequestedTab_ = true;
            }
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags spriteTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Sprites ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Sprites", nullptr, spriteTabFlags)) {
            if (spriteTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditor_.draw(context_);
            if (spriteEditorLaunchedFromCharacter_) {
                characterEditor_.setSelectedSpriteReference(spriteEditor_.spriteMetadataReference(context_));
            }
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags layoutTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Layout ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Screens", nullptr, layoutTabFlags)) {
            if (layoutTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            layoutEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags tilesetsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Tilesets ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Tilesets", nullptr, tilesetsTabFlags)) {
            if (tilesetsTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            tilesetEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags wallFloorTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::WallFloorPaint ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Wall/Floor Paint", nullptr, wallFloorTabFlags)) {
            if (wallFloorTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            wallFloorPaint_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags enemyPathTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::EnemyPaths ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Enemy Paths", nullptr, enemyPathTabFlags)) {
            if (enemyPathTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            enemyPathEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags assetsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Assets ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Assets", nullptr, assetsTabFlags)) {
            if (assetsTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            ImGui::Text("Raw sprites: %s", context_.assets.rawSpritePath().string().c_str());
            ImGui::Text("Raw character sprites: %s", context_.assets.rawCharacterSpritePath().string().c_str());
            ImGui::Text("Raw tilesets: %s", context_.assets.rawTilesetPath().string().c_str());
            ImGui::Text("Game sprites: %s", context_.assets.gameSpritePath().string().c_str());
            ImGui::Text("Game character sprites: %s", context_.assets.gameCharacterSpritePath().string().c_str());
            ImGui::Text("Game characters: %s", context_.assets.gameCharacterPath().string().c_str());
            ImGui::Text("Game chapters: %s", context_.assets.gameChapterPath().string().c_str());
            ImGui::Text("Game maps: %s", context_.assets.gameMapPath().string().c_str());
            ImGui::Text("Game tilesets: %s", context_.assets.gameTilesetPath().string().c_str());
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void EditorApp::requestExit()
{
    if (context_.dirty) {
        pendingExit_ = true;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        exitAccepted_ = true;
    }
}

void EditorApp::refreshChapterList()
{
    chapterIds_.clear();
    std::error_code error;
    const std::filesystem::path chapterDir = context_.assets.gameChapterPath();
    if (!std::filesystem::exists(chapterDir, error)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(chapterDir, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) && entry.path().extension() == ".adchapter") {
            chapterIds_.push_back(entry.path().stem().string());
        }
    }
    std::sort(chapterIds_.begin(), chapterIds_.end());
}

void EditorApp::drawChapterMenu()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Chapter")) {
            ImGui::TextDisabled("Current: %s%s",
                context_.currentChapterId.empty() ? "none" : context_.currentChapterId.c_str(),
                context_.dirty ? " *" : "");
            ImGui::Separator();
            if (ImGui::MenuItem("Save")) {
                saveCurrentChapterAndExports();
            }
            if (ImGui::MenuItem("Refresh list")) {
                refreshChapterList();
            }
            ImGui::Separator();
            for (const std::string& chapterId : chapterIds_) {
                if (ImGui::MenuItem(chapterId.c_str(), nullptr, chapterId == context_.currentChapterId)) {
                    requestChapterSwitch(chapterId);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorApp::drawStartupChapterModal()
{
    ImGui::OpenPopup("Select Chapter");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Select Chapter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Open a chapter to start editing.");
        ImGui::Separator();

        if (chapterIds_.empty()) {
            ImGui::TextDisabled("No chapter files found.");
        }
        for (const std::string& chapterId : chapterIds_) {
            if (ImGui::Button(chapterId.c_str(), ImVec2(260.0f, 32.0f))) {
                chooseChapter(chapterId);
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("New id", newChapterId_.data(), newChapterId_.size());
        if (ImGui::Button("Create Chapter", ImVec2(260.0f, 32.0f))) {
            createChapter();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawUnsavedChangesModal()
{
    ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        if (ImGui::Button("Save", ImVec2(110.0f, 0.0f))) {
            if (pendingChapterSwitch_) {
                completeChapterSwitch(true);
            } else {
                saveCurrentChapterAndExports();
                pendingExit_ = false;
                exitAccepted_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(110.0f, 0.0f))) {
            context_.dirty = false;
            if (pendingChapterSwitch_) {
                completeChapterSwitch(false);
            } else {
                pendingExit_ = false;
                exitAccepted_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            pendingExit_ = false;
            pendingChapterSwitch_ = false;
            pendingChapterId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::chooseChapter(const std::string& chapterId)
{
    if (layoutEditor_.loadChapterById(context_, chapterId)) {
        wallFloorPaint_.resetScreenBuffers();
        spriteEditor_.resetDocumentBuffers();
        startupChapterChosen_ = true;
        screenGraphicsMode_ = false;
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }
}

void EditorApp::createChapter()
{
    layoutEditor_.createChapter(context_, newChapterId_.data());
    wallFloorPaint_.resetScreenBuffers();
    spriteEditor_.resetDocumentBuffers();
    startupChapterChosen_ = true;
    screenGraphicsMode_ = false;
    requestedTab_ = MainTab::Layout;
    hasRequestedTab_ = true;
    refreshChapterList();
}

void EditorApp::requestChapterSwitch(const std::string& chapterId)
{
    if (chapterId == context_.currentChapterId) {
        return;
    }
    if (context_.dirty) {
        pendingChapterSwitch_ = true;
        pendingChapterId_ = chapterId;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        chooseChapter(chapterId);
    }
}

void EditorApp::completeChapterSwitch(bool saveFirst)
{
    if (saveFirst) {
        saveCurrentChapterAndExports();
    }
    const std::string target = pendingChapterId_;
    pendingChapterSwitch_ = false;
    pendingChapterId_.clear();
    chooseChapter(target);
}

void EditorApp::saveCurrentChapterAndExports()
{
    spriteEditor_.saveForChapter(context_);
    wallFloorPaint_.saveForChapter(context_);
    (void)layoutEditor_.saveCurrentChapter(context_);
    refreshChapterList();
}

} // namespace adventure::editor
