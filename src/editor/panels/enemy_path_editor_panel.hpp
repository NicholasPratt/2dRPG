#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"
#include "game/tileset.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

// Enemy placement and path editor. Enemy types are universal game data;
// placements live on the currently selected chapter screen.
class EnemyPathEditorPanel {
public:
    void draw(EditorContext& context);
    void drawTypes(EditorContext& context);
    void saveProjectEnemyTypes(EditorContext& context);

private:
    enum class Behavior { Idle = 0, Patrol = 1, Aggro = 2 };
    enum class CurveMode { Linear = 0, Spline = 1 };
    enum class CanvasMode { AddEnemies = 0, EditSplines = 1 };

    struct Waypoint {
        float x = 0.0f;
        float y = 0.0f;
        float speedOverride = 0.0f;
        float waitSeconds = 0.0f;
        int facing = -1;
        std::string animState;
    };

    struct PixelLayer {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
        bool loaded = false;
    };

    // Placement identity
    std::array<char, 64> placementId_{'e', 'n', 'e', 'm', 'y', '_', '1', '\0'};
    std::array<char, 64> typeId_{'e', 'n', 'e', 'm', 'y', '_', '1', '\0'};
    std::array<char, 64> spriteId_{'e', 'n', 'e', 'm', 'y', '_', '1', '\0'};
    std::array<char, 64> mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};
    int selectedPlacement_ = -1;
    int selectedType_ = 0;
    bool projectLoaded_ = false;

    // Enemy behavior
    Behavior behavior_ = Behavior::Patrol;
    CurveMode curveMode_ = CurveMode::Linear;
    float speed_ = 64.0f;
    int maxHealth_ = 1;
    int contactDamage_ = 1;
    float hitboxWidth_ = 12.0f;
    float hitboxHeight_ = 12.0f;
    float attackCooldownSeconds_ = 1.0f;
    bool loop_ = true;
    bool respawn_ = false;

    // Waypoints
    std::vector<Waypoint> waypoints_;
    int selectedWaypoint_ = -1;
    bool dragging_ = false;
    CanvasMode canvasMode_ = CanvasMode::AddEnemies;

    // Canvas display
    float zoom_ = 2.0f; // canvas pixels per world pixel
    bool snapToGrid_ = true;

    // Optional map background for the selected screen.
    game::TileMap bgMap_;
    game::TilesetDef bgTileset_;
    bool bgMapLoaded_ = false;
    bool bgTilesetLoaded_ = false;
    PixelLayer floorGraphics_;
    PixelLayer wallGraphics_;

    std::string status_;

    // Sprite preview for the enemy types page
    struct SpritePreviewData {
        std::string loadedId;          // sprite ID currently loaded
        std::string loadedProjectRoot; // project root when loaded (detect project switch)
        int frameX = 0, frameY = 0;   // first frame rect in sheet
        int frameW = 16, frameH = 16;
        std::vector<std::uint32_t> sheetPixels;
        int sheetW = 0, sheetH = 0;
        bool loaded = false;
    };
    SpritePreviewData spritePreview_;
    bool showHitboxOverlay_ = true;
    std::string lastLoadedProjectRoot_; // tracks project root for enemy-type reload

    // Attack editor state (mirrors the selected enemy type's attacks)
    int selectedAttack_ = -1;

    void drawToolbar(EditorContext& context);
    void drawEnemyTypePage(EditorContext& context);
    void drawPlacementList(EditorContext& context);
    void drawAnimationStateHelper(EditorContext& context);
    void drawWaypointList(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void loadBgMap(EditorContext& context);
    void loadScreenGraphics(EditorContext& context);
    void loadProjectEnemyTypes(EditorContext& context);
    void createPlacement(EditorContext& context);
    void createPlacementAt(EditorContext& context, float x, float y);
    void selectPlacement(EditorContext& context, int index);
    void writeCurrentPlacement(EditorContext& context);
    void drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const;
    void loadSpritePreview(const EditorContext& context, const std::string& spriteId);
    void drawSpritePreviewPanel(ImDrawList* dl, ImVec2 topLeft, float panelW, float panelH, const game::EnemyType& type) const;
    void drawAttackEditor(EditorContext& context, game::EnemyType& type);
    [[nodiscard]] float snapValue(float v) const;
    [[nodiscard]] ImVec2 waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const;
    [[nodiscard]] Waypoint placementAnchor(const game::EnemyPlacement& placement) const;
    [[nodiscard]] Waypoint splinePoint(int segment, float t) const;
};

} // namespace adventure::editor
