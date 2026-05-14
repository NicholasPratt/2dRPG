#pragma once

#include "editor/asset_directories.hpp"

#include <string>

namespace adventure::editor {

struct EditorContext {
    AssetDirectories assets;
    std::string currentChapterId;
    std::string selectedScreenId;
    std::string selectedScreenMapId;
    std::string requestedChapterSwitchId;
    bool dirty = false;
    bool requestEditScreenGraphics = false;
    bool requestChapterSwitch = false;

    void markDirty() { dirty = true; }
};

} // namespace adventure::editor
