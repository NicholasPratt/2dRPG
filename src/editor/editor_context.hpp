#pragma once

#include "editor/asset_directories.hpp"
#include "game/chapter.hpp"
#include "game/project.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

struct TilePaletteFrame {
    std::vector<std::uint32_t> floor;
    std::vector<std::uint32_t> wall;
};

struct TilePaletteEntry {
    std::string name;
    int widthPx = 0;
    int heightPx = 0;
    int frameDurationMs = 250;
    std::vector<TilePaletteFrame> frames;
};

struct ChapterScreenEntry {
    std::string id;
    std::string mapId;
    int gridX = 0;
    int gridY = 0;
};

struct EditorContext {
    AssetDirectories assets;
    std::string currentChapterId;
    std::string selectedScreenId;
    std::string selectedScreenMapId;
    std::string requestedChapterSwitchId;
    std::string requestedSpriteReference;
    bool dirty = false;
    bool requestEditScreenGraphics = false;
    bool requestEditSprite = false;
    bool requestChapterSwitch = false;
    std::vector<TilePaletteEntry> tilePalette;
    std::vector<ChapterScreenEntry> chapterScreens;
    std::vector<game::EnemyPlacement> selectedScreenEnemies;
    std::vector<game::EnemyType> enemyTypes;
    std::vector<std::string> importedCharacterIds;
    std::string playableCharacterId;

    void markDirty() { dirty = true; }
};

} // namespace adventure::editor
