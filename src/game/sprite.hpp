#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct SpriteFrameDef {
    int x = 0;
    int y = 0;
    int width = 16;
    int height = 16;
    int durationMs = 100;
    std::string type = "idle";
    std::string direction;  // empty = any direction; "E","W","N","S","NE","NW","SE","SW"
    // Per-frame collision rectangles in frame-local pixels [x, y, w, h], origin at
    // the frame's top-left (same space as the frame's own width/height). width<=0 or
    // height<=0 means "unset" — the runtime falls back to the entity's default box.
    std::array<int, 4> wallBox{0, 0, 0, 0};  // movement / wall collision
    std::array<int, 4> hitBox{0, 0, 0, 0};   // damage reception / being hit
};

struct SpriteMetadata {
    std::string id = "new_sprite";
    std::filesystem::path source;
    std::array<int, 2> canvasSize{16, 16};
    std::array<int, 2> gridSize{16, 16};
    std::array<int, 2> pivot{8, 8};
    // Editor-only authoring guide rectangle [x, y, width, height] in canvas
    // pixels. width/height <= 0 means "unset". Ignored by the game runtime.
    std::array<int, 4> bodyGuide{0, 0, 0, 0};
    std::vector<SpriteFrameDef> frames{{}};
    std::vector<std::string> tags{"idle"};
};

[[nodiscard]] bool saveSpriteMetadata(const std::filesystem::path& path, const SpriteMetadata& metadata, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadSpriteMetadata(const std::filesystem::path& path, SpriteMetadata& metadata, std::string* errorMessage = nullptr);

} // namespace adventure::game
