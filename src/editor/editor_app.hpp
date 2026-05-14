#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/character_editor_panel.hpp"
#include "editor/panels/enemy_path_editor_panel.hpp"
#include "editor/panels/layout_editor_panel.hpp"
#include "editor/panels/map_editor_panel.hpp"
#include "editor/panels/sprite_editor_panel.hpp"
#include "editor/panels/tileset_editor_panel.hpp"
#include "editor/panels/wall_floor_paint_panel.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

class EditorApp {
public:
    void draw();
    void requestExit();
    [[nodiscard]] bool exitAccepted() const { return exitAccepted_; }

private:
    enum class MainTab {
        Characters,
        Sprites,
        Layout,
        Tilesets,
        WallFloorPaint,
        EnemyPaths,
        Assets,
    };

    EditorContext context_;
    CharacterEditorPanel characterEditor_;
    SpriteEditorPanel spriteEditor_;
    LayoutEditorPanel layoutEditor_;
    MapEditorPanel mapEditor_;
    TilesetEditorPanel tilesetEditor_;
    WallFloorPaintPanel wallFloorPaint_;
    EnemyPathEditorPanel enemyPathEditor_;
    MainTab requestedTab_ = MainTab::Characters;
    bool hasRequestedTab_ = true;
    bool spriteEditorLaunchedFromCharacter_ = false;
    bool screenGraphicsMode_ = false;
    bool startupChapterChosen_ = false;
    bool pendingExit_ = false;
    bool exitAccepted_ = false;
    bool pendingChapterSwitch_ = false;
    std::string pendingChapterId_;
    std::array<char, 64> newChapterId_{'c', 'h', 'a', 'p', 't', 'e', 'r', '_', '1', '\0'};
    std::vector<std::string> chapterIds_;

    void drawStartupChapterModal();
    void drawUnsavedChangesModal();
    void drawChapterMenu();
    void refreshChapterList();
    void chooseChapter(const std::string& chapterId);
    void createChapter();
    void requestChapterSwitch(const std::string& chapterId);
    void completeChapterSwitch(bool saveFirst);
    void saveCurrentChapterAndExports();
};

} // namespace adventure::editor
