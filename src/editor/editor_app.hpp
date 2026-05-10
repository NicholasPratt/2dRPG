#pragma once

#include "editor/editor_context.hpp"
#include "editor/panels/sprite_editor_panel.hpp"

namespace adventure::editor {

class EditorApp {
public:
    void draw();

private:
    EditorContext context_;
    SpriteEditorPanel spriteEditor_;
};

} // namespace adventure::editor
