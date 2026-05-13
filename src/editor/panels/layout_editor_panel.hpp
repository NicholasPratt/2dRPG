#pragma once

#include "editor/editor_context.hpp"
#include "game/chapter.hpp"

#include <array>
#include <string>

namespace adventure::editor {

class LayoutEditorPanel {
public:
    void draw(EditorContext& context);

private:
    game::Chapter chapter_;
    int selectedScreen_ = 0;
    std::array<char, 64> chapterId_{'c', 'h', 'a', 'p', 't', 'e', 'r', '_', '1', '\0'};
    std::string status_;

    void drawToolbar(EditorContext& context);
    void drawScreenList();
    void drawMacroView();
    void drawScreenInspector();
    void addScreen();
    void deleteSelectedScreen();
    void saveChapter(EditorContext& context);
    void loadChapter(EditorContext& context);
    void syncChapterIdBuffer();
    [[nodiscard]] bool selectedScreenValid() const;
};

} // namespace adventure::editor
