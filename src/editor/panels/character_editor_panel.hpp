#pragma once

#include "editor/editor_context.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace adventure::editor {

struct CharacterSheet {
    std::array<char, 64> name{};
    std::array<char, 1024> bio{};
    std::array<char, 256> spriteReference{};
};

class CharacterEditorPanel {
public:
    CharacterEditorPanel();

    [[nodiscard]] std::optional<std::filesystem::path> draw(EditorContext& context);
    void setSelectedSpriteReference(const std::filesystem::path& spriteReference);

private:
    std::vector<CharacterSheet> characters_;
    int selectedCharacter_ = 0;

    void addCharacter();
    void drawCharacterList();
    [[nodiscard]] std::optional<std::filesystem::path> drawCharacterSheet(EditorContext& context);
};

} // namespace adventure::editor
