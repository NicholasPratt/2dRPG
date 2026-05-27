#pragma once

#include "editor/editor_context.hpp"
#include "game/map.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adventure::editor {

class NpcEditorPanel {
public:
    void draw(EditorContext& context);
    void drawTypes(EditorContext& context);
    void saveProjectNpcTypes(EditorContext& context);

private:
    enum class CanvasMode { PlaceNpcs = 0, EditPath = 1 };

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

    std::array<char, 64> placementId_{'n', 'p', 'c', '_', '1', '\0'};
    std::array<char, 64> typeId_{'n', 'p', 'c', '_', '1', '\0'};
    std::array<char, 64> mapId_{'\0'};
    int selectedPlacement_ = -1;
    int selectedType_ = 0;
    bool projectLoaded_ = false;
    std::string lastLoadedProjectRoot_;

    int facing_ = 0;
    float awarenessRadius_ = 64.0f;
    float interactionRadius_ = 24.0f;
    int movementMode_ = 0;
    bool loop_ = true;
    float speed_ = 32.0f;

    std::vector<Waypoint> waypoints_;
    std::vector<game::DialogueLine> dialogueLines_;
    int selectedWaypoint_ = -1;
    bool dragging_ = false;
    CanvasMode canvasMode_ = CanvasMode::PlaceNpcs;

    float zoom_ = 2.0f;
    bool snapToGrid_ = true;

    game::TileMap bgMap_;
    bool bgMapLoaded_ = false;
    PixelLayer floorGraphics_;
    PixelLayer wallGraphics_;

    std::string status_;

    void drawToolbar(EditorContext& context);
    void drawPlacementList(EditorContext& context);
    void drawWaypointList(EditorContext& context);
    void drawCanvas(EditorContext& context);
    void loadBgMap(EditorContext& context);
    void loadScreenGraphics(EditorContext& context);
    void loadProjectNpcTypes(EditorContext& context);
    void createPlacementAt(EditorContext& context, float x, float y);
    void selectPlacement(EditorContext& context, int index);
    void writeCurrentPlacement(EditorContext& context);
    void drawPixelLayer(ImDrawList* dl, ImVec2 origin, const PixelLayer& layer, float targetW, float targetH, float opacity) const;
    [[nodiscard]] float snapValue(float v) const;
    [[nodiscard]] ImVec2 waypointToCanvas(ImVec2 origin, const Waypoint& waypoint) const;
};

} // namespace adventure::editor
