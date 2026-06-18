#pragma once

#include "editor/editor_context.hpp"
#include "game/chapter.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <string>
#include <vector>

namespace adventure::editor {

class ChapterExitPanel {
public:
    void openForScreen(EditorContext& context);
    void draw(EditorContext& context);
    void saveForScreen(EditorContext& context);

private:
    int selectedExit_ = -1;
    float zoom_ = 1.5f;
    bool bgMapLoaded_ = false;
    game::TileMap bgMap_;
    std::string loadedMapId_;
    bool pickingTargetTile_ = false;
    std::string loadedTargetChapterId_;
    std::string loadedTargetScreenId_;
    game::Chapter targetChapter_;
    game::TileMap targetMap_;
    bool targetMapLoaded_ = false;
    std::string status_;
    std::array<char, 64> newChapterId_{'c', 'h', 'a', 'p', 't', 'e', 'r', '_', '2', '\0'};

    void loadBackground(EditorContext& context);
    void drawList(EditorContext& context);
    void drawInspector(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void drawValidation(EditorContext& context);
    void drawCondition(EditorContext& context, game::MapChapterExitPlacement& exit);
    void drawTargetPicker(EditorContext& context, game::MapChapterExitPlacement& exit);
    bool createDestinationChapter(EditorContext& context, game::MapChapterExitPlacement& exit);
    bool loadTarget(EditorContext& context, const game::MapChapterExitPlacement& exit);
    [[nodiscard]] std::vector<std::string> chapterIds(const EditorContext& context) const;
};

} // namespace adventure::editor
