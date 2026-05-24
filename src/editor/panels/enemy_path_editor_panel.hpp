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

// Enemy path / spline editor (spec §4.4).
// Draws linear or spline waypoint paths on a world-grid canvas.
// Saved to .adpath files in assets/game/paths/.
class EnemyPathEditorPanel {
public:
    void draw(EditorContext& context);

private:
    enum class Behavior { Idle = 0, Patrol = 1, Aggro = 2 };
    enum class CurveMode { Linear = 0, Spline = 1 };

    struct Waypoint {
        float x = 0.0f; // world pixels
        float y = 0.0f;
    };

    // Identity
    std::array<char, 64> pathId_{'p', 'a', 't', 'h', '_', '1', '\0'};
    std::array<char, 64> enemyId_{'e', 'n', 'e', 'm', 'y', '_', '1', '\0'};
    std::array<char, 64> spriteId_{'e', 'n', 'e', 'm', 'y', '_', '1', '\0'};
    std::array<char, 64> mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};

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
    void drawAnimationStateHelper(EditorContext& context);
    void drawWaypointList();
    void drawCanvas();
    void loadBgMap(EditorContext& context);
    void savePath(EditorContext& context);
    void loadPath(EditorContext& context);
    [[nodiscard]] float snapValue(float v) const;
    [[nodiscard]] ImVec2 waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const;
    [[nodiscard]] Waypoint splinePoint(int segment, float t) const;
};

} // namespace adventure::editor
