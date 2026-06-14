#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/character_editor_panel.hpp"
#include "editor/panels/door_placement_panel.hpp"
#include "editor/panels/dialogue_graph_editor_panel.hpp"
#include "editor/panels/enemy_path_editor_panel.hpp"
#include "editor/panels/layout_editor_panel.hpp"
#include "editor/panels/map_editor_panel.hpp"
#include "editor/panels/sprite_editor_panel.hpp"
#include "editor/panels/tileset_editor_panel.hpp"
#include "editor/panels/item_placement_panel.hpp"
#include "editor/panels/npc_editor_panel.hpp"
#include "editor/panels/wall_floor_paint_panel.hpp"
#include "editor/panels/weapon_editor_panel.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

class EditorApp {
public:
    EditorApp();

    void draw();
    void requestExit();
    [[nodiscard]] bool exitAccepted() const { return exitAccepted_; }

private:
    enum class MainTab {
        Characters,
        Weapons,
        Items,
        QuestState,
        Layout,
        Tilesets,
        WallFloorPaint,
        Assets,
    };

    enum class ScreenEditMode {
        Layout,
        Graphics,
        Music,
        Enemies,
        EnemyTypes,
        Items,
        ItemEdit,
        Doors,
        Npcs,
        NpcTypes,
        DialogueGraph,
        Sprite,
    };

    EditorContext context_;
    CharacterEditorPanel characterEditor_;
    SpriteEditorPanel spriteEditor_;
    LayoutEditorPanel layoutEditor_;
    MapEditorPanel mapEditor_;
    TilesetEditorPanel tilesetEditor_;
    WallFloorPaintPanel wallFloorPaint_;
    EnemyPathEditorPanel enemyPathEditor_;
    DialogueGraphEditorPanel dialogueGraphEditor_;
    WeaponEditorPanel weaponEditor_;
    ItemPlacementPanel itemPlacementEditor_;
    DoorPlacementPanel doorPlacementEditor_;
    NpcEditorPanel npcEditor_;
    MainTab requestedTab_ = MainTab::Characters;
    bool hasRequestedTab_ = true;
    bool spriteEditorLaunchedFromCharacter_ = false;
    bool spriteEditorLaunchedFromProjectItems_ = false;
    bool screenMapLogicMode_ = false;
    ScreenEditMode screenEditMode_ = ScreenEditMode::Layout;
    ScreenEditMode spriteReturnMode_ = ScreenEditMode::Layout;
    std::vector<game::EnemyPlacement> enemyPlacementSnapshot_;
    std::vector<game::EnemyType> enemyTypeSnapshot_;
    std::vector<game::MapItemPlacement> itemPlacementSnapshot_;
    std::vector<game::MapDoorPlacement> doorPlacementSnapshot_;
    std::vector<game::NpcPlacement> npcPlacementSnapshot_;
    std::vector<game::NpcTypeDef> npcTypeSnapshot_;
    bool startupChapterChosen_ = false;
    bool pendingExit_ = false;
    bool exitAccepted_ = false;
    bool pendingChapterSwitch_ = false;
    std::string pendingChapterId_;
    std::string playStatus_;
    std::filesystem::path workspaceRoot_;
    std::string currentProjectId_;
    std::array<char, 64> newProjectId_{'p', 'r', 'o', 'j', 'e', 'c', 't', '_', '1', '\0'};
    std::array<char, 64> newChapterId_{'c', 'h', 'a', 'p', 't', 'e', 'r', '_', '1', '\0'};
    std::vector<std::string> projectIds_;
    std::vector<std::string> chapterIds_;
    bool showProjectManager_ = false;
    std::string projectPendingDelete_;
    bool pendingProjectSwitch_ = false;
    std::string pendingProjectId_;
    std::string pendingProjectChapterId_;
    bool variablePickerActive_ = false;
    int selectedVariable_ = -1;

    void drawStartupChapterModal();
    void drawProjectManagerWindow();
    void drawDeleteProjectConfirm();
    void drawUnsavedChangesModal();
    void drawChapterMenu();
    void openProject(const std::string& projectId, const std::string& chapterId);
    void requestProjectOpen(const std::string& projectId, const std::string& chapterId);
    void deleteProject(const std::string& projectId);
    void loadSession();
    void saveSession() const;
    [[nodiscard]] std::filesystem::path sessionFilePath() const;
    void refreshProjectList();
    void refreshChapterList();
    void selectProject(const std::string& projectId);
    void loadProjectMetadata();
    void saveProjectMetadata();
    void chooseChapter(const std::string& chapterId);
    void createChapter();
    void createProjectAndChapter();
    void ensureProjectDirectories() const;
    [[nodiscard]] std::filesystem::path projectsRoot() const;
    void requestChapterSwitch(const std::string& chapterId);
    void completeChapterSwitch(bool saveFirst);
    void saveActiveEditingScope();
    void saveCurrentChapterAndExports();
    // Launch the runtime. fresh = ignore/preserve save; startScreen empty = chapter start;
    // fromCheckpoint = resume the last-entered screen + position recorded by the runtime.
    void launchGame(bool fresh = false, const std::string& startScreen = {}, bool fromCheckpoint = false);
    void enterScreenMode(ScreenEditMode mode);
    void drawScreensTab();
    void drawProjectStateTab(bool pickerMode = false);
    void drawProjectItemsTab();
    void drawScopedEditHeader(const char* title, bool saveAndExit, bool exitWithoutSaving);
    void exitScreenModeSaving();
    void exitScreenModeDiscarding();
};

} // namespace adventure::editor
