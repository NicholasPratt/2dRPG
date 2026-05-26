#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/character_editor_panel.hpp"
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
        QuestState,
        Layout,
        Tilesets,
        WallFloorPaint,
        Assets,
    };

    enum class ScreenEditMode {
        Layout,
        Graphics,
        Enemies,
        EnemyTypes,
        Items,
        Npcs,
        NpcTypes,
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
    WeaponEditorPanel weaponEditor_;
    ItemPlacementPanel itemPlacementEditor_;
    NpcEditorPanel npcEditor_;
    MainTab requestedTab_ = MainTab::Characters;
    bool hasRequestedTab_ = true;
    bool spriteEditorLaunchedFromCharacter_ = false;
    bool screenMapLogicMode_ = false;
    ScreenEditMode screenEditMode_ = ScreenEditMode::Layout;
    ScreenEditMode spriteReturnMode_ = ScreenEditMode::Layout;
    std::vector<game::EnemyPlacement> enemyPlacementSnapshot_;
    std::vector<game::EnemyType> enemyTypeSnapshot_;
    std::vector<game::MapItemPlacement> itemPlacementSnapshot_;
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

    void drawStartupChapterModal();
    void drawUnsavedChangesModal();
    void drawChapterMenu();
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
    void launchGame();
    void enterScreenMode(ScreenEditMode mode);
    void drawScreensTab();
    void drawProjectStateTab();
    void drawScopedEditHeader(const char* title, bool saveAndExit, bool exitWithoutSaving);
    void exitScreenModeSaving();
    void exitScreenModeDiscarding();
};

} // namespace adventure::editor
