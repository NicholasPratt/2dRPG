#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"
#include "game/tileset.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

// Enemy path / spline editor (spec §4.4).
// Draws polyline waypoint paths on a world-grid canvas.
// Saved to .adpath files in assets/game/paths/.
class EnemyPathEditorPanel {
public:
    void draw(EditorContext& context);

private:
    enum class Behavior { Idle = 0, Patrol = 1, Aggro = 2 };

    struct Waypoint {
        float x = 0.0f; // world pixels
        float y = 0.0f;
    };

    // Identity
    std::array<char, 64> pathId_{'p', 'a', 't', 'h', '_', '1', '\0'};
    std::array<char, 64> mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};

    // Enemy behavior
    Behavior behavior_ = Behavior::Patrol;
    float speed_ = 64.0f;
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
    void drawWaypointList();
    void drawCanvas();
    void loadBgMap(EditorContext& context);
    void savePath(EditorContext& context);
    void loadPath(EditorContext& context);
    [[nodiscard]] float snapValue(float v) const;
};

} // namespace adventure::editor
