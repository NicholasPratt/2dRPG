#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct TileDef {
    int id = 0;
    std::string name;
    bool solid = false;
};

struct TilesetDef {
    std::string id;
    std::string sourcePath; // relative to project root, e.g. "assets/raw/tilesets/overworld.png"
    int tileWidth = 16;
    int tileHeight = 16;
    std::vector<TileDef> tiles;

    // Returns nullptr if the id is out of range.
    [[nodiscard]] const TileDef* findTile(int tileId) const;
};

[[nodiscard]] bool saveTileset(const std::filesystem::path& path, const TilesetDef& tileset, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTileset(const std::filesystem::path& path, TilesetDef& tileset, std::string* errorMessage = nullptr);

} // namespace adventure::game
