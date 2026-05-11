#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/character_editor_panel.hpp"
#include "editor/panels/map_editor_panel.hpp"
#include "editor/panels/sprite_editor_panel.hpp"

namespace adventure::editor {

class EditorApp {
public:
    void draw();

private:
    enum class MainTab {
        Characters,
        Sprites,
        Maps,
        Assets,
    };

    EditorContext context_;
    CharacterEditorPanel characterEditor_;
    SpriteEditorPanel spriteEditor_;
    MapEditorPanel mapEditor_;
    MainTab requestedTab_ = MainTab::Characters;
    bool hasRequestedTab_ = true;
    bool spriteEditorLaunchedFromCharacter_ = false;
};

} // namespace adventure::editor
