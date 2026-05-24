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

    struct Waypoint {
        float x = 0.0f; // world pixels
        float y = 0.0f;
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

    // Canvas display
    float zoom_ = 2.0f; // canvas pixels per world pixel
    bool snapToGrid_ = true;

    // Optional map background (mid layer only, for reference)
    game::TileMap bgMap_;
    game::TilesetDef bgTileset_;
    bool bgMapLoaded_ = false;
    bool bgTilesetLoaded_ = false;

    std::string status_;

    void drawToolbar(EditorContext& context);
    void drawEnemyTypePage(EditorContext& context);
    void drawPlacementList(EditorContext& context);
    void drawAnimationStateHelper(EditorContext& context);
    void drawWaypointList(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void loadBgMap(EditorContext& context);
    void loadProjectEnemyTypes(EditorContext& context);
    void createPlacement(EditorContext& context);
    void selectPlacement(EditorContext& context, int index);
    void writeCurrentPlacement(EditorContext& context);
    [[nodiscard]] float snapValue(float v) const;
    [[nodiscard]] ImVec2 waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const;
    [[nodiscard]] Waypoint splinePoint(int segment, float t) const;
};

} // namespace adventure::editor
