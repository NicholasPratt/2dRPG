#include "editor/panels/character_editor_panel.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace adventure::editor {
namespace {

void copyText(char* destination, std::size_t destinationSize, const char* source)
{
    if (destinationSize == 0) {
        return;
    }

    const std::size_t length = std::min(std::strlen(source), destinationSize - 1);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

} // namespace

CharacterEditorPanel::CharacterEditorPanel()
{
    addCharacter();
    copyText(characters_[0].name.data(), characters_[0].name.size(), "Hero");
    copyText(characters_[0].bio.data(), characters_[0].bio.size(), "A new character. Use this sheet for story notes, personality, and role.");
    copyText(characters_[0].spriteReference.data(), characters_[0].spriteReference.size(), "assets/game/sprites/new_sprite.sprite.json");
}

std::optional<std::filesystem::path> CharacterEditorPanel::draw(EditorContext& context)
{
    if (characters_.empty()) {
        addCharacter();
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float listWidth = std::min(260.0f, std::max(180.0f, available.x * 0.28f));

    ImGui::BeginChild("CharacterList", ImVec2(listWidth, 0.0f), true);
    drawCharacterList();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("CharacterSheet", ImVec2(0.0f, 0.0f), true);
    std::optional<std::filesystem::path> spriteToOpen = drawCharacterSheet(context);
    ImGui::EndChild();

    return spriteToOpen;
}

void CharacterEditorPanel::setSelectedSpriteReference(const std::filesystem::path& spriteReference)
{
    if (characters_.empty()) {
        return;
    }

    selectedCharacter_ = std::clamp(selectedCharacter_, 0, static_cast<int>(characters_.size()) - 1);
    const std::string reference = spriteReference.generic_string();
    copyText(characters_[selectedCharacter_].spriteReference.data(), characters_[selectedCharacter_].spriteReference.size(), reference.c_str());
}

void CharacterEditorPanel::addCharacter()
{
    CharacterSheet character;
    copyText(character.name.data(), character.name.size(), "New Character");
    copyText(character.bio.data(), character.bio.size(), "");
    copyText(character.spriteReference.data(), character.spriteReference.size(), "assets/game/sprites/new_sprite.sprite.json");
    characters_.push_back(character);
    selectedCharacter_ = static_cast<int>(characters_.size()) - 1;
}

void CharacterEditorPanel::drawCharacterList()
{
    ImGui::TextUnformatted("Characters");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
        ImGui::PushID(i);
        const char* label = characters_[i].name[0] == '\0' ? "Unnamed Character" : characters_[i].name.data();
        if (ImGui::Selectable(label, selectedCharacter_ == i, 0, ImVec2(0.0f, 34.0f))) {
            selectedCharacter_ = i;
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add character", ImVec2(-1.0f, 34.0f))) {
        addCharacter();
    }
}

std::optional<std::filesystem::path> CharacterEditorPanel::drawCharacterSheet(EditorContext& context)
{
    selectedCharacter_ = std::clamp(selectedCharacter_, 0, static_cast<int>(characters_.size()) - 1);
    CharacterSheet& character = characters_[selectedCharacter_];

    ImGui::TextUnformatted("Character Sheet");
    ImGui::Separator();

    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputText("Name", character.name.data(), character.name.size());

    ImGui::TextUnformatted("Bio");
    ImGui::InputTextMultiline(
        "##CharacterBio",
        character.bio.data(),
        character.bio.size(),
        ImVec2(-1.0f, 220.0f));

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Sprite", character.spriteReference.data(), character.spriteReference.size());
    ImGui::Text("Sprite assets: %s", context.assets.gameSpritePath().string().c_str());
    ImGui::Text("Character assets: %s", context.assets.gameCharacterPath().string().c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 58, 64, 255));
    const bool openSprite = ImGui::Button(character.spriteReference.data()[0] == '\0' ? "No sprite linked" : character.spriteReference.data(), ImVec2(-1.0f, 72.0f));
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open this sprite in the sprite editor");
    }

    if (openSprite && character.spriteReference.data()[0] != '\0') {
        return std::filesystem::path(character.spriteReference.data());
    }

    return std::nullopt;
}

} // namespace adventure::editor
