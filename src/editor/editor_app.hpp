#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/character_editor_panel.hpp"
#include "editor/panels/enemy_path_editor_panel.hpp"
#include "editor/panels/layout_editor_panel.hpp"
#include "editor/panels/map_editor_panel.hpp"
#include "editor/panels/sprite_editor_panel.hpp"
#include "editor/panels/tileset_editor_panel.hpp"
#include "editor/panels/wall_floor_paint_panel.hpp"

namespace adventure::editor {

class EditorApp {
public:
    void draw();

private:
    enum class MainTab {
        Characters,
        Sprites,
        Layout,
        Maps,
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
};

} // namespace adventure::editor
