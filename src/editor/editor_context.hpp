#pragma once

#include "editor/asset_directories.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

struct TilePaletteEntry {
    std::string name;
    int widthPx = 0;
    int heightPx = 0;
    std::vector<std::uint32_t> floor;
    std::vector<std::uint32_t> wall;
};

struct EditorContext {
    AssetDirectories assets;
    std::string currentChapterId;
    std::string selectedScreenId;
    std::string selectedScreenMapId;
    std::string requestedChapterSwitchId;
    bool dirty = false;
    bool requestEditScreenGraphics = false;
    bool requestChapterSwitch = false;
    std::vector<TilePaletteEntry> tilePalette;

    void markDirty() { dirty = true; }
};

} // namespace adventure::editor
