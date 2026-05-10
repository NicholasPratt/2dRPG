#include "editor/asset_directories.hpp"

namespace adventure::editor {

std::filesystem::path AssetDirectories::rawSpritePath() const
{
    return projectRoot / rawSprites;
}

std::filesystem::path AssetDirectories::gameSpritePath() const
{
    return projectRoot / gameSprites;
}

std::filesystem::path AssetDirectories::gameAnimationPath() const
{
    return projectRoot / gameAnimations;
}

std::filesystem::path AssetDirectories::gamePalettePath() const
{
    return projectRoot / gamePalettes;
}

} // namespace adventure::editor
