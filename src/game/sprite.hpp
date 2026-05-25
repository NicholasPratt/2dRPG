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
};

struct SpriteMetadata {
    std::string id = "new_sprite";
    std::filesystem::path source;
    std::array<int, 2> canvasSize{16, 16};
    std::array<int, 2> gridSize{16, 16};
    std::array<int, 2> pivot{8, 8};
    std::vector<SpriteFrameDef> frames{{}};
    std::vector<std::string> tags{"idle"};
};

[[nodiscard]] bool saveSpriteMetadata(const std::filesystem::path& path, const SpriteMetadata& metadata, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadSpriteMetadata(const std::filesystem::path& path, SpriteMetadata& metadata, std::string* errorMessage = nullptr);

} // namespace adventure::game
