#include "editor/asset_directories.hpp"

#include <system_error>

namespace adventure::editor {

std::filesystem::path AssetDirectories::rawSpritePath() const
{
    return projectRoot / rawSprites;
}

std::filesystem::path AssetDirectories::rawCharacterSpritePath() const
{
    return projectRoot / rawCharacterSprites;
}

std::filesystem::path AssetDirectories::rawTilesetPath() const
{
    return projectRoot / rawTilesets;
}

std::filesystem::path AssetDirectories::gameSpritePath() const
{
    return projectRoot / gameSprites;
}

std::filesystem::path AssetDirectories::gameCharacterSpritePath() const
{
    return projectRoot / gameCharacterSprites;
}

std::filesystem::path AssetDirectories::gameCharacterPath() const
{
    return projectRoot / gameCharacters;
}

std::filesystem::path AssetDirectories::gameChapterPath() const
{
    return projectRoot / gameChapters;
}

std::filesystem::path AssetDirectories::gameMapPath() const
{
    return projectRoot / gameMaps;
}

std::filesystem::path AssetDirectories::gameTilesetPath() const
{
    return projectRoot / gameTilesets;
}

std::filesystem::path AssetDirectories::gameAnimationPath() const
{
    return projectRoot / gameAnimations;
}

std::filesystem::path AssetDirectories::gamePalettePath() const
{
    return projectRoot / gamePalettes;
}

std::filesystem::path AssetDirectories::gamePathPath() const
{
    return projectRoot / gamePaths;
}

std::filesystem::path AssetDirectories::gameDialoguePath() const
{
    return projectRoot / gameDialogue;
}

std::filesystem::path AssetDirectories::gameFontPath() const
{
    return projectRoot / gameFonts;
}

std::filesystem::path AssetDirectories::gameMusicPath() const
{
    return projectRoot / gameMusic;
}

std::filesystem::path AssetDirectories::gameSfxPath() const
{
    return projectRoot / gameSfx;
}

std::vector<std::filesystem::path> AssetDirectories::requiredPaths() const
{
    return {
        projectRoot / "assets",
        projectRoot / "assets/raw",
        rawSpritePath(),
        rawCharacterSpritePath(),
        rawTilesetPath(),
        projectRoot / "assets/game",
        gameSpritePath(),
        gameCharacterSpritePath(),
        gameCharacterPath(),
        gameChapterPath(),
        gameMapPath(),
        gameTilesetPath(),
        gameAnimationPath(),
        gamePalettePath(),
        gamePathPath(),
        gameDialoguePath(),
        gameFontPath(),
        gameMusicPath(),
        gameSfxPath(),
        gameSfxPath() / "doors",
        gameSfxPath() / "walking",
    };
}

bool AssetDirectories::ensureRequiredPaths(std::string* errorMessage) const
{
    for (const std::filesystem::path& path : requiredPaths()) {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not create asset directory " + path.string() + ": " + error.message();
            }
            return false;
        }
    }
    return true;
}

} // namespace adventure::editor
