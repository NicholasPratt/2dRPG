#include "editor/panels/tileset_editor_panel.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace adventure::editor {
namespace {

// Deterministic color per tile ID for visual distinction — tile 0 is always dark.
ImU32 colorForTileId(uint16_t id)
{
    if (id == 0) {
        return IM_COL32(30, 30, 30, 255);
    }
    uint32_t h = static_cast<uint32_t>(id) * 2654435761u;
    auto r = static_cast<uint8_t>(80 + (h & 0xFFu) * 175u / 255u);
    auto g = static_cast<uint8_t>(80 + ((h >> 8u) & 0xFFu) * 175u / 255u);
    auto b = static_cast<uint8_t>(80 + ((h >> 16u) & 0xFFu) * 175u / 255u);
    return IM_COL32(r, g, b, 255);
}

} // namespace

void TilesetEditorPanel::draw(EditorContext& context)
{
    drawToolbar(context);
    ImGui::Separator();
    drawTileList();
}

void TilesetEditorPanel::drawToolbar(EditorContext& context)
{
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Tileset id", tilesetId_.data(), tilesetId_.size());

    ImGui::SetNextItemWidth(380.0f);
    ImGui::InputText("Source PNG", sourcePath_.data(), sourcePath_.size());
    ImGui::SameLine();
    ImGui::TextDisabled("(relative to project root)");

    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile W px", &tileWidthPx_);
    tileWidthPx_ = std::clamp(tileWidthPx_, 1, 256);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile H px", &tileHeightPx_);
    tileHeightPx_ = std::clamp(tileHeightPx_, 1, 256);

    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Cols", &cols_);
    cols_ = std::clamp(cols_, 1, 256);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Rows", &rows_);
    rows_ = std::clamp(rows_, 1, 256);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d tiles total)", cols_ * rows_);

    if (ImGui::Button("Generate tiles")) {
        generateTiles();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        tileset_ = game::TilesetDef{};
        const std::string id(tilesetId_.data());
        tileset_.id = id;
        tileset_.sourcePath = std::string(sourcePath_.data());
        tileset_.tileWidth = tileWidthPx_;
        tileset_.tileHeight = tileHeightPx_;
        status_ = "New tileset created. Generate tiles or add them manually.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save .tileset.json")) {
        saveTileset(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .tileset.json")) {
        loadTileset(context);
    }

    ImGui::Text("Game tilesets: %s", context.assets.gameTilesetPath().string().c_str());
    ImGui::Text("Raw tilesets:  %s", context.assets.rawTilesetPath().string().c_str());
    ImGui::TextDisabled("Set cols/rows to match your tileset image, then Generate to populate the tile list.");

    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void TilesetEditorPanel::drawTileList()
{
    if (tileset_.tiles.empty()) {
        ImGui::TextDisabled("No tiles. Enter cols/rows above and click Generate, or Load an existing tileset.");
        return;
    }

    ImGui::Text("%d tile(s)", static_cast<int>(tileset_.tiles.size()));
    ImGui::Separator();

    // Column headers
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("  ID   Color    Name                         Solid");
    ImGui::PopStyleColor();
    ImGui::Separator();

    constexpr float kSwatchSize = 18.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (std::size_t i = 0; i < tileset_.tiles.size(); ++i) {
        game::TileDef& tile = tileset_.tiles[i];
        ImGui::PushID(static_cast<int>(i));

        // ID badge
        ImGui::Text("%5d", tile.id);
        ImGui::SameLine();

        // Color swatch
        const ImVec2 swatchMin = ImGui::GetCursorScreenPos();
        const ImVec2 swatchMax{swatchMin.x + kSwatchSize, swatchMin.y + kSwatchSize};
        dl->AddRectFilled(swatchMin, swatchMax, colorForTileId(static_cast<uint16_t>(tile.id)));
        dl->AddRect(swatchMin, swatchMax, IM_COL32(180, 180, 180, 200));
        ImGui::Dummy(ImVec2(kSwatchSize, kSwatchSize));
        ImGui::SameLine();

        // Name field
        char nameBuf[64]{};
        const std::size_t copyLen = std::min(tile.name.size(), sizeof(nameBuf) - 1);
        std::memcpy(nameBuf, tile.name.data(), copyLen);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
            tile.name = nameBuf;
        }
        ImGui::SameLine();

        // Solid checkbox
        ImGui::Checkbox("Solid", &tile.solid);
        ImGui::SameLine();

        // Delete button
        if (ImGui::SmallButton("X")) {
            tileset_.tiles.erase(tileset_.tiles.begin() + static_cast<ptrdiff_t>(i));
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add tile")) {
        game::TileDef t;
        t.id = tileset_.tiles.empty() ? 0 : tileset_.tiles.back().id + 1;
        t.name = "tile_" + std::to_string(t.id);
        tileset_.tiles.push_back(t);
    }
}

void TilesetEditorPanel::generateTiles()
{
    tileset_.id = std::string(tilesetId_.data());
    tileset_.sourcePath = std::string(sourcePath_.data());
    tileset_.tileWidth = tileWidthPx_;
    tileset_.tileHeight = tileHeightPx_;

    const int total = cols_ * rows_;
    tileset_.tiles.clear();
    tileset_.tiles.reserve(static_cast<std::size_t>(total));

    for (int i = 0; i < total; ++i) {
        game::TileDef t;
        t.id = i;
        t.name = "tile_" + std::to_string(i);
        t.solid = (i != 0); // convention: tile 0 = passable ground, others default solid
        tileset_.tiles.push_back(t);
    }

    status_ = "Generated " + std::to_string(total) + " tile(s). Edit names and solid flags below.";
}

void TilesetEditorPanel::saveTileset(EditorContext& context)
{
    tileset_.id = std::string(tilesetId_.data());
    tileset_.sourcePath = std::string(sourcePath_.data());
    tileset_.tileWidth = tileWidthPx_;
    tileset_.tileHeight = tileHeightPx_;

    std::string error;
    const std::filesystem::path outputPath =
        context.assets.gameTilesetPath() / (tileset_.id + ".tileset.json");

    if (game::saveTileset(outputPath, tileset_, &error)) {
        status_ = "Saved: " + outputPath.generic_string();
    } else {
        status_ = "Failed to save: " + error;
    }
}

void TilesetEditorPanel::loadTileset(EditorContext& context)
{
    const std::string id(tilesetId_.data());
    const std::filesystem::path inputPath =
        context.assets.gameTilesetPath() / (id + ".tileset.json");

    std::string error;
    game::TilesetDef loaded;
    if (!game::loadTileset(inputPath, loaded, &error)) {
        status_ = "Failed to load: " + error;
        return;
    }

    tileset_ = std::move(loaded);

    // Sync UI fields back from the loaded tileset
    const std::size_t idLen = std::min(tileset_.id.size(), tilesetId_.size() - 1);
    std::memset(tilesetId_.data(), 0, tilesetId_.size());
    std::memcpy(tilesetId_.data(), tileset_.id.data(), idLen);

    const std::size_t srcLen = std::min(tileset_.sourcePath.size(), sourcePath_.size() - 1);
    std::memset(sourcePath_.data(), 0, sourcePath_.size());
    std::memcpy(sourcePath_.data(), tileset_.sourcePath.data(), srcLen);

    tileWidthPx_ = tileset_.tileWidth;
    tileHeightPx_ = tileset_.tileHeight;

    status_ = "Loaded: " + inputPath.generic_string() +
        " (" + std::to_string(tileset_.tiles.size()) + " tiles)";
}

} // namespace adventure::editor
