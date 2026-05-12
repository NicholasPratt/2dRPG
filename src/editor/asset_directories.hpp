#pragma once

#include <filesystem>

namespace adventure::editor {

struct AssetDirectories {
    std::filesystem::path projectRoot{"."};
    std::filesystem::path rawSprites{"assets/raw/sprites"};
    std::filesystem::path rawCharacterSprites{"assets/raw/character_sprites"};
    std::filesystem::path rawTilesets{"assets/raw/tilesets"};
    std::filesystem::path gameSprites{"assets/game/sprites"};
    std::filesystem::path gameCharacterSprites{"assets/game/character_sprites"};
    std::filesystem::path gameCharacters{"assets/game/characters"};
    std::filesystem::path gameMaps{"assets/game/maps"};
    std::filesystem::path gameTilesets{"assets/game/tilesets"};
    std::filesystem::path gameAnimations{"assets/game/animations"};
    std::filesystem::path gamePalettes{"assets/game/palettes"};

    [[nodiscard]] std::filesystem::path rawSpritePath() const;
    [[nodiscard]] std::filesystem::path rawCharacterSpritePath() const;
    [[nodiscard]] std::filesystem::path rawTilesetPath() const;
    [[nodiscard]] std::filesystem::path gameSpritePath() const;
    [[nodiscard]] std::filesystem::path gameCharacterSpritePath() const;
    [[nodiscard]] std::filesystem::path gameCharacterPath() const;
    [[nodiscard]] std::filesystem::path gameMapPath() const;
    [[nodiscard]] std::filesystem::path gameTilesetPath() const;
    [[nodiscard]] std::filesystem::path gameAnimationPath() const;
    [[nodiscard]] std::filesystem::path gamePalettePath() const;
};

} // namespace adventure::editor
