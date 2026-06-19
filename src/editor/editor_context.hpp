#pragma once

#include "editor/asset_directories.hpp"
#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/project.hpp"

#include <cstdint>
#include <functional>
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
    // Sprite asset backing this tile's animation. When the sprite has >= 2 frames
    // the tile is "animated" and stamping it places a runtime animated tile.
    std::string spriteId;
};

struct ChapterScreenEntry {
    std::string id;
    std::string mapId;
    int gridX = 0;
    int gridY = 0;
};

// Seed pixels handed from the screen-graphics editor to the sprite editor so a
// selected region can be turned into an animated tile sprite. Pixels are
// 0xAABBGGRR, row-major (same encoding as the paint panel and sprite cels).
struct PendingSpriteSeed {
    std::string id;
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    void clear() {
        id.clear();
        width = 0;
        height = 0;
        pixels.clear();
    }
    [[nodiscard]] bool valid() const { return !id.empty() && width > 0 && height > 0; }
};

struct EditorContext {
    AssetDirectories assets;
    std::string currentChapterId;
    std::string selectedScreenId;
    std::string selectedScreenMapId;
    std::string requestedChapterSwitchId;
    std::string requestedSpriteReference;
    PendingSpriteSeed pendingSpriteSeed;
    std::string requestedDialogueGraphId;
    bool dirty = false;
    bool requestEditScreenGraphics = false;
    // Set when the screen-graphics panel switches screens so the host can re-sync
    // per-screen placement lists (animated tiles, etc.) from the chapter.
    bool requestScreenPlacementSync = false;
    bool requestEditScreenMusic = false;
    bool requestEditEnemies = false;
    bool requestEditEnemyTypes = false;
    bool requestEditSprite = false;
    bool requestEditDialogueGraph = false;
    bool requestChapterSwitch = false;
    bool requestCreateChapter = false;
    std::vector<TilePaletteEntry> tilePalette;
    std::vector<ChapterScreenEntry> chapterScreens;
    std::vector<game::EnemyPlacement> selectedScreenEnemies;
    std::string selectedScreenEnemiesOwnerId;
    std::vector<game::EnemyType> enemyTypes;
    std::vector<game::WeaponDef> weaponDefs;
    std::vector<game::ItemDef> itemDefs;
    std::vector<game::StateVariableDef> stateVariables;
    std::vector<game::GameEffectDef> effectDefs;
    std::vector<game::ChapterSynopsisDef> chapterSynopses;
    std::string startingWeaponId;
    std::string fontPath;
    std::vector<game::MapItemPlacement> selectedScreenItems;
    std::string selectedScreenItemsMapId;
    std::vector<game::MapDoorPlacement> selectedScreenDoors;
    std::string selectedScreenDoorsMapId;
    std::vector<game::MapChapterExitPlacement> selectedScreenChapterExits;
    std::string selectedScreenChapterExitsMapId;
    std::vector<game::NpcPlacement> selectedScreenNpcs;
    std::string selectedScreenNpcsOwnerId;
    std::vector<game::AnimatedTilePlacement> selectedScreenAnimatedTiles;
    std::string selectedScreenAnimatedTilesOwnerId;
    std::vector<game::NpcTypeDef> npcTypes;
    std::vector<std::string> importedCharacterIds;
    std::string playableCharacterId;
    bool requestEditItems = false;
    bool requestEditItemDetails = false;
    bool requestEditDoors = false;
    bool requestEditChapterExits = false;
    bool requestEditNpcs = false;
    bool requestEditNpcTypes = false;
    bool requestVariablePicker = false;
    std::string requestedVariableId;
    game::StateVariableType requestedVariableType = game::StateVariableType::Integer;
    game::StateVariableScope requestedVariableScope = game::StateVariableScope::Universal;
    std::function<void(const game::StateVariableDef&)> applyPickedVariable;

    void markDirty() { dirty = true; }
    void openVariablePicker(
        const std::string& currentId,
        game::StateVariableType type,
        game::StateVariableScope scope,
        std::function<void(const game::StateVariableDef&)> apply)
    {
        requestedVariableId = currentId;
        requestedVariableType = type;
        requestedVariableScope = scope;
        applyPickedVariable = std::move(apply);
        requestVariablePicker = true;
    }
};

} // namespace adventure::editor
