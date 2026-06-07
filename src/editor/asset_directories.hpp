#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

struct AssetDirectories {
    std::filesystem::path projectRoot{"."};
    std::filesystem::path rawSprites{"assets/raw/sprites"};
    std::filesystem::path rawCharacterSprites{"assets/raw/character_sprites"};
    std::filesystem::path rawTilesets{"assets/raw/tilesets"};
    std::filesystem::path gameSprites{"assets/game/sprites"};
    std::filesystem::path gameCharacterSprites{"assets/game/character_sprites"};
    std::filesystem::path gameCharacters{"assets/game/characters"};
    std::filesystem::path gameChapters{"assets/game/chapters"};
    std::filesystem::path gameMaps{"assets/game/maps"};
    std::filesystem::path gameTilesets{"assets/game/tilesets"};
    std::filesystem::path gameAnimations{"assets/game/animations"};
    std::filesystem::path gamePalettes{"assets/game/palettes"};
    std::filesystem::path gamePaths{"assets/game/paths"};
    std::filesystem::path gameDialogue{"assets/game/dialogue"};
    std::filesystem::path gameFonts{"assets/game/fonts"};
    std::filesystem::path gameMusic{"assets/game/music"};
    std::filesystem::path gameSfx{"assets/game/sfx"};

    [[nodiscard]] std::filesystem::path rawSpritePath() const;
    [[nodiscard]] std::filesystem::path rawCharacterSpritePath() const;
    [[nodiscard]] std::filesystem::path rawTilesetPath() const;
    [[nodiscard]] std::filesystem::path gameSpritePath() const;
    [[nodiscard]] std::filesystem::path gameCharacterSpritePath() const;
    [[nodiscard]] std::filesystem::path gameCharacterPath() const;
    [[nodiscard]] std::filesystem::path gameChapterPath() const;
    [[nodiscard]] std::filesystem::path gameMapPath() const;
    [[nodiscard]] std::filesystem::path gameTilesetPath() const;
    [[nodiscard]] std::filesystem::path gameAnimationPath() const;
    [[nodiscard]] std::filesystem::path gamePalettePath() const;
    [[nodiscard]] std::filesystem::path gamePathPath() const;
    [[nodiscard]] std::filesystem::path gameDialoguePath() const;
    [[nodiscard]] std::filesystem::path gameFontPath() const;
    [[nodiscard]] std::filesystem::path gameMusicPath() const;
    [[nodiscard]] std::filesystem::path gameSfxPath() const;
    [[nodiscard]] std::vector<std::filesystem::path> requiredPaths() const;
    [[nodiscard]] bool ensureRequiredPaths(std::string* errorMessage = nullptr) const;
};

} // namespace adventure::editor
