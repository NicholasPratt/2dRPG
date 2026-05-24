#include "editor/editor_app.hpp"
#include "editor/editor_state.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace adventure::editor {
namespace {

std::filesystem::path runningExecutableDirectory()
{
#if defined(__APPLE__)
    std::vector<char> buffer(1024);
    std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
    }
    std::error_code error;
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), error).parent_path();
#else
    return {};
#endif
}

std::filesystem::path findProjectRoot()
{
    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    const std::filesystem::path executableDir = runningExecutableDirectory();
    const std::vector<std::filesystem::path> candidates = {
        cwd,
        cwd.parent_path(),
        executableDir,
        executableDir.parent_path(),
        executableDir.parent_path().parent_path(),
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (std::filesystem::exists(candidate / "assets", error)) {
            return std::filesystem::weakly_canonical(candidate, error);
        }
    }

    return cwd.empty() ? std::filesystem::path(".") : cwd;
}

} // namespace

EditorApp::EditorApp()
{
    context_.assets.projectRoot = findProjectRoot();
}

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
            mapEditor_.openMapId(context_, context_.selectedScreenMapId);
        }
        screenGraphicsMode_ = true;
        screenMapLogicMode_ = false;
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
        spriteEditorLaunchedFromCharacter_ = false;
    }

    if (context_.requestEditSprite) {
        context_.requestEditSprite = false;
        if (!context_.requestedSpriteReference.empty()) {
            spriteEditor_.openSpriteReference(context_.requestedSpriteReference);
            context_.requestedSpriteReference.clear();
        }
        spriteEditorLaunchedFromCharacter_ = false;
        requestedTab_ = MainTab::Sprites;
        hasRequestedTab_ = true;
    }

    if (ImGui::BeginTabBar("EditorMainTabs")) {
        ImGuiTabItemFlags characterTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Characters ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Characters", nullptr, characterTabFlags)) {
            if (characterTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            if (auto spriteToOpen = characterEditor_.draw(context_)) {
                spriteEditor_.openCharacterSpriteReference(*spriteToOpen);
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
            if (spriteEditorLaunchedFromCharacter_) {
                if (ImGui::Button("< Return to Character", ImVec2(190.0f, 30.0f))) {
                    spriteEditor_.saveForChapter(context_);
                    characterEditor_.setSelectedSpriteReference(context_, spriteEditor_.spriteMetadataReference(context_));
                    spriteEditorLaunchedFromCharacter_ = false;
                    requestedTab_ = MainTab::Characters;
                    hasRequestedTab_ = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted("Editing character sprite");
                ImGui::Separator();
            }
            spriteEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags layoutTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Layout ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Screens", nullptr, layoutTabFlags)) {
            if (layoutTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            if (screenGraphicsMode_) {
                if (ImGui::Button("< Back to Screens", ImVec2(180.0f, 30.0f))) {
                    screenGraphicsMode_ = false;
                }
                ImGui::SameLine();
                ImGui::Text("Editing graphics for %s", context_.selectedScreenId.empty() ? context_.selectedScreenMapId.c_str() : context_.selectedScreenId.c_str());
                ImGui::SameLine();
                if (ImGui::Button(screenMapLogicMode_ ? "Paint Graphics" : "Map Logic", ImVec2(130.0f, 30.0f))) {
                    screenMapLogicMode_ = !screenMapLogicMode_;
                    if (screenMapLogicMode_ && !context_.selectedScreenMapId.empty()) {
                        wallFloorPaint_.saveForChapter(context_);
                        mapEditor_.openMapId(context_, context_.selectedScreenMapId);
                    }
                }
                ImGui::Separator();
                if (screenMapLogicMode_) {
                    mapEditor_.draw(context_);
                } else {
                    wallFloorPaint_.draw(context_);
                }
            } else {
                layoutEditor_.draw(context_);
            }
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

        ImGuiTabItemFlags enemyPathTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::EnemyPaths ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Enemies", nullptr, enemyPathTabFlags)) {
            if (enemyPathTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            if (!context_.selectedScreenId.empty()) {
                layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
            }
            enemyPathEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags enemyTypesTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::EnemyTypes ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Enemy Types", nullptr, enemyTypesTabFlags)) {
            if (enemyTypesTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            enemyPathEditor_.drawTypes(context_);
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
            if (ImGui::MenuItem("Save and Play Game")) {
                launchGame();
            }
            if (ImGui::MenuItem("Refresh list")) {
                refreshChapterList();
            }
            if (!playStatus_.empty()) {
                ImGui::TextDisabled("%s", playStatus_.c_str());
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
        screenMapLogicMode_ = false;
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;

        const std::filesystem::path statePath = context_.assets.gameChapterPath() / (chapterId + ".adeditor");
        EditorState editorState;
        if (loadEditorState(statePath, editorState)) {
            if (!editorState.selectedScreenId.empty()) {
                layoutEditor_.selectScreenById(context_, editorState.selectedScreenId);
            }
            context_.tilePalette = std::move(editorState.tilePalette);
            if (!editorState.paintPalette.empty()) {
                wallFloorPaint_.setPalette(std::move(editorState.paintPalette));
            }
        }
    }
}

void EditorApp::createChapter()
{
    layoutEditor_.createChapter(context_, newChapterId_.data());
    wallFloorPaint_.resetScreenBuffers();
    spriteEditor_.resetDocumentBuffers();
    startupChapterChosen_ = true;
    screenGraphicsMode_ = false;
    screenMapLogicMode_ = false;
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
    const bool charactersSaved = characterEditor_.saveForChapter(context_);
    spriteEditor_.saveForChapter(context_);
    const bool graphicsSaved = wallFloorPaint_.saveForChapter(context_);
    (void)layoutEditor_.saveCurrentChapter(context_);
    if (!charactersSaved || !graphicsSaved) {
        context_.markDirty();
    }

    if (!context_.currentChapterId.empty()) {
        const std::filesystem::path statePath = context_.assets.gameChapterPath() / (context_.currentChapterId + ".adeditor");
        EditorState editorState;
        editorState.selectedScreenId = context_.selectedScreenId;
        editorState.tilePalette = context_.tilePalette;
        editorState.paintPalette = wallFloorPaint_.getPalette();
        saveEditorState(statePath, editorState);
    }

    refreshChapterList();
}

void EditorApp::launchGame()
{
    saveCurrentChapterAndExports();
    if (context_.currentChapterId.empty()) {
        playStatus_ = "No chapter selected.";
        return;
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    const std::filesystem::path executableDir = runningExecutableDirectory();
    std::filesystem::path projectRoot = std::filesystem::absolute(context_.assets.projectRoot, error);
    if (!std::filesystem::exists(projectRoot / "assets", error) && std::filesystem::exists(cwd / "assets", error)) {
        projectRoot = cwd;
    }
    if (!std::filesystem::exists(projectRoot / "assets", error) && std::filesystem::exists(cwd.parent_path() / "assets", error)) {
        projectRoot = cwd.parent_path();
    }
    if (!executableDir.empty() && !std::filesystem::exists(projectRoot / "assets", error) &&
        std::filesystem::exists(executableDir.parent_path() / "assets", error)) {
        projectRoot = executableDir.parent_path();
    }

    const std::vector<std::filesystem::path> executableCandidates = {
        executableDir / "adventure_game_window",
        executableDir / "build" / "adventure_game_window",
        executableDir.parent_path() / "build" / "adventure_game_window",
        projectRoot / "build" / "adventure_game_window",
        cwd / "build" / "adventure_game_window",
        cwd / "adventure_game_window",
        cwd.parent_path() / "build" / "adventure_game_window",
    };

    std::filesystem::path executable;
    for (const std::filesystem::path& candidate : executableCandidates) {
        if (std::filesystem::exists(candidate, error)) {
            executable = std::filesystem::absolute(candidate, error);
            break;
        }
    }

    if (executable.empty()) {
        playStatus_ = "Game executable not found. Build first: cmake --build build";
        return;
    }

    const std::filesystem::path chapterPath = std::filesystem::absolute(
        projectRoot / context_.assets.gameChapters / (context_.currentChapterId + ".adchapter"),
        error);
    if (!std::filesystem::exists(chapterPath, error)) {
        playStatus_ = "Chapter file not found: " + chapterPath.string();
        return;
    }

    const std::filesystem::path logPath = projectRoot / "build" / "adventure_game_window.log";
    const std::string command =
        "cd \"" + projectRoot.string() + "\" && \"" +
        executable.string() + "\" \"" +
        chapterPath.string() + "\" > \"" +
        logPath.string() + "\" 2>&1 &";

    const int result = std::system(command.c_str());
    if (result == 0) {
        playStatus_ = "Launched game: " + executable.filename().string() + " (Esc closes it)";
    } else {
        playStatus_ = "Failed to launch game. See build/adventure_game_window.log";
    }
}

} // namespace adventure::editor
