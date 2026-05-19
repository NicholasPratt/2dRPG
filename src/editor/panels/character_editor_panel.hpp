#pragma once

#include "editor/editor_context.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace adventure::editor {

struct CharacterAnimationSlot {
    std::array<char, 48> state{};
    std::array<char, 256> spriteReference{};
};

struct CharacterFrameAssignment {
    int frameIndex = 0;
    std::array<char, 48> state{};
    std::array<char, 256> frameImage{};
};

struct CharacterSheet {
    std::array<char, 64> name{};
    std::array<char, 1024> bio{};
    std::array<char, 256> spriteReference{};
    bool playable = false;
    std::vector<CharacterAnimationSlot> animations;
    std::vector<CharacterFrameAssignment> frameAssignments;
};

class CharacterEditorPanel {
public:
    CharacterEditorPanel();

    [[nodiscard]] std::optional<std::filesystem::path> draw(EditorContext& context);
    void setSelectedSpriteReference(EditorContext& context, const std::filesystem::path& spriteReference);
    bool saveForChapter(EditorContext& context);

private:
    std::vector<CharacterSheet> characters_;
    int selectedCharacter_ = 0;
    int spriteEditAnimation_ = -1;
    bool loadedFromDisk_ = false;

    void addCharacter();
    void addDefaultAnimationSlots(CharacterSheet& character);
    void syncFrameAssignmentsFromSprite(EditorContext& context, CharacterSheet& character);
    void loadCharacters(const EditorContext& context);
    bool saveCharacter(const EditorContext& context, const CharacterSheet& character);
    void drawCharacterList();
    void drawFrameAssignments(EditorContext& context, CharacterSheet& character);
    [[nodiscard]] std::optional<std::filesystem::path> drawCharacterSheet(EditorContext& context);
};

} // namespace adventure::editor
