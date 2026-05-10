#pragma once

#include <filesystem>

namespace adventure::editor {

struct AssetDirectories {
    std::filesystem::path projectRoot{"."};
    std::filesystem::path rawSprites{"assets/raw/sprites"};
    std::filesystem::path rawTilesets{"assets/raw/tilesets"};
    std::filesystem::path gameSprites{"assets/game/sprites"};
    std::filesystem::path gameTilesets{"assets/game/tilesets"};
    std::filesystem::path gameAnimations{"assets/game/animations"};
    std::filesystem::path gamePalettes{"assets/game/palettes"};

    [[nodiscard]] std::filesystem::path rawSpritePath() const;
    [[nodiscard]] std::filesystem::path gameSpritePath() const;
    [[nodiscard]] std::filesystem::path gameAnimationPath() const;
    [[nodiscard]] std::filesystem::path gamePalettePath() const;
};

} // namespace adventure::editor
