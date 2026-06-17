#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <string>

namespace adventure::editor {

class DoorPlacementPanel {
public:
    void draw(EditorContext& context);
    void openForScreen(EditorContext& context);
    void saveForScreen(EditorContext& context);

private:
    std::string loadedMapId_;
    game::TileMap bgMap_;
    bool bgMapLoaded_ = false;
    std::string loadedTargetScreenId_;
    game::TileMap targetMap_;
    bool targetMapLoaded_ = false;
    bool pickingTargetTile_ = false;
    int selectedDoor_ = -1;
    float zoom_ = 1.5f;

    std::array<char, 64> doorId_{'d', 'o', 'o', 'r', '_', '1', '\0'};
    int tileX_ = 0;
    int tileY_ = 0;
    int widthTiles_ = 1;
    int heightTiles_ = 1;
    int lockMode_ = 0;
    std::array<char, 64> requiredItemId_{'\0'};
    bool consumeKey_ = false;
    bool renderAboveWalls_ = false;
    std::array<char, 64> targetScreenId_{'\0'};
    int targetTileX_ = 1;
    int targetTileY_ = 1;
    std::array<char, 64> spriteId_{'\0'};
    std::array<char, 64> openingAnimation_{'\0'};
    std::array<char, 256> openSoundPath_{'\0'};
    std::array<char, 256> closeSoundPath_{'\0'};
    std::array<char, 256> lockedSoundPath_{'\0'};

    void loadBackground(EditorContext& context);
    void drawToolbar(EditorContext& context);
    void drawDoorList(EditorContext& context);
    void drawInspector(EditorContext& context);
    void drawValidation(EditorContext& context);
    void drawSoundPicker(EditorContext& context);
    void drawCanvas(EditorContext& context);
    [[nodiscard]] bool loadTargetMap(EditorContext& context);
    void syncInspectorFromSelected(const EditorContext& context);
    void writeInspectorToSelected(EditorContext& context);
    void requestEditDoorSprite(EditorContext& context);
    void placeDoorAt(EditorContext& context, int tileX, int tileY);
    // Creates (or reconciles) the paired door on the target screen and links the
    // two together via targetDoorId. Each door's target tile remains its explicit
    // spawn point. No-op for same-screen or untargeted doors.
    void ensurePairedDoor(EditorContext& context, int doorIndex);

    std::string pairingStatus_;
};

} // namespace adventure::editor
