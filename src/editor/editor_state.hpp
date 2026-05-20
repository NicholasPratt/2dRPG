#pragma once

#include "editor/editor_context.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace adventure::editor {

struct EditorState {
    std::string selectedScreenId;
    std::vector<TilePaletteEntry> tilePalette;
    std::vector<std::uint32_t> paintPalette;
};

bool saveEditorState(const std::filesystem::path& path, const EditorState& state, std::string* errorMessage = nullptr);
bool loadEditorState(const std::filesystem::path& path, EditorState& state, std::string* errorMessage = nullptr);

} // namespace adventure::editor
