#include "editor/panels/character_editor_panel.hpp"

#include "editor/editor_theme.hpp"
#include "editor/imgui_widgets.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "imgui.h"
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

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

std::string textOf(const char* text)
{
    return text == nullptr ? std::string{} : std::string{text};
}

std::string portableProjectPath(const std::filesystem::path& projectRoot, const char* path)
{
    const std::filesystem::path imagePath = textOf(path);
    if (imagePath.empty() || !imagePath.is_absolute()) {
        return imagePath.generic_string();
    }

    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, error);
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(imagePath, error);
    const std::filesystem::path relative = std::filesystem::relative(canonicalPath, canonicalRoot, error);
    if (!error && !relative.empty() && relative.native().find("..") != 0) {
        return relative.generic_string();
    }
    return imagePath.generic_string();
}

std::string characterId(const CharacterSheet& character)
{
    std::string id = character.name[0] == '\0' ? "character" : character.name.data();
    for (char& c : id) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            c = '_';
        }
    }
    id.erase(std::unique(id.begin(), id.end(), [](char a, char b) { return a == '_' && b == '_'; }), id.end());
    while (!id.empty() && id.front() == '_') { id.erase(id.begin()); }
    while (!id.empty() && id.back() == '_') { id.pop_back(); }
    return id.empty() ? "character" : id;
}

constexpr const char* kAnimationStates[] = {
    "idle",
    "walk",
    "run",
    "attack_1",
    "attack_2",
    "attack_3",
    "attack_4",
    "attack_5",
    "attack_6",
    "attack_7",
    "attack_8",
    "attack_9",
    "attack_10",
    "attack_11",
    "attack_12",
    "attack_13",
    "attack_14",
    "attack_15",
    "attack_16",
    "attack_17",
    "attack_18",
    "attack_19",
    "attack_20",
    "cast",
    "hit_react",
    "guard",
    "interact",
    "conversation",
    "death",
    "fall",
    "sleep",
    "jump",
    "land",
    "pickup",
    "use_item",
    "hurt_loop",
    "stunned",
    "victory",
    "emote",
    "climb",
    "swim",
    "dash",
    "block_break",
    "revive",
};

std::filesystem::path resolveProjectPath(const EditorContext& context, const std::filesystem::path& path)
{
    return path.is_absolute() ? path : context.assets.projectRoot / path;
}

} // namespace

CharacterEditorPanel::CharacterEditorPanel()
{
    addCharacter();
    copyText(characters_[0].name.data(), characters_[0].name.size(), "Hero");
    copyText(characters_[0].bio.data(), characters_[0].bio.size(), "A new character. Use this sheet for story notes, personality, and role.");
    copyText(characters_[0].spriteReference.data(), characters_[0].spriteReference.size(), "assets/game/sprites/new_sprite.sprite.json");
    characters_[0].playable = true;
    addDefaultAnimationSlots(characters_[0]);
}

std::optional<std::filesystem::path> CharacterEditorPanel::draw(EditorContext& context)
{
    if (!loadedFromDisk_) {
        loadCharacters(context);
        loadedFromDisk_ = true;
    }
    if (characters_.empty()) {
        addCharacter();
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float listWidth = std::min(260.0f, std::max(180.0f, available.x * 0.28f));

    ImGui::BeginChild("CharacterList", ImVec2(listWidth, 0.0f), true);
    drawCharacterList(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("CharacterSheet", ImVec2(0.0f, 0.0f), true);
    std::optional<std::filesystem::path> spriteToOpen = drawCharacterSheet(context);
    ImGui::EndChild();

    return spriteToOpen;
}

void CharacterEditorPanel::setSelectedSpriteReference(EditorContext& context, const std::filesystem::path& spriteReference)
{
    if (characters_.empty()) {
        return;
    }

    selectedCharacter_ = std::clamp(selectedCharacter_, 0, static_cast<int>(characters_.size()) - 1);
    const std::string reference = spriteReference.generic_string();
    copyText(characters_[selectedCharacter_].spriteReference.data(), characters_[selectedCharacter_].spriteReference.size(), reference.c_str());
    syncFrameAssignmentsFromSprite(context, characters_[selectedCharacter_]);
    syncedFrameCharacter_ = selectedCharacter_;
    syncedFrameSpriteReference_ = textOf(characters_[selectedCharacter_].spriteReference.data());
    frameThumbnails_.clear();
    context.markDirty();
    spriteEditAnimation_ = -1;
}

bool CharacterEditorPanel::saveForChapter(EditorContext& context)
{
    bool allOk = true;
    purgeDeletedCharacters(context);

    game::GameProject project;
    (void)game::loadGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
    project.id = "game";
    if (!context.enemyTypes.empty()) {
        project.enemyTypes = context.enemyTypes;
    } else {
        context.enemyTypes = project.enemyTypes;
    }
    if (!context.currentChapterId.empty() &&
        std::find(project.chapterIds.begin(), project.chapterIds.end(), context.currentChapterId) == project.chapterIds.end()) {
        project.chapterIds.push_back(context.currentChapterId);
    }
    context.importedCharacterIds.clear();
    context.playableCharacterId.clear();
    project.characterIds.clear();

    int playableIndex = -1;
    for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
        if (characters_[static_cast<std::size_t>(i)].playable) {
            playableIndex = i;
            break;
        }
    }
    if (playableIndex < 0 && !characters_.empty()) {
        playableIndex = 0;
    }
    for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
        characters_[static_cast<std::size_t>(i)].playable = i == playableIndex;
    }

    for (const CharacterSheet& character : characters_) {
        CharacterSheet writableCharacter = character;
        allOk = ensureUniqueSprite(context, writableCharacter) && allOk;
        const std::string id = characterId(character);
        project.characterIds.push_back(id);
        context.importedCharacterIds.push_back(id);
        if (character.playable) {
            project.playableCharacterId = id;
            context.playableCharacterId = id;
        }
        allOk = saveCharacter(context, writableCharacter) && allOk;
    }
    if (project.playableCharacterId.empty() && !project.characterIds.empty()) {
        project.playableCharacterId = project.characterIds.front();
        context.playableCharacterId = project.playableCharacterId;
    }
    std::string error;
    allOk = game::saveGameProject(context.assets.projectRoot / "assets/game/project.adgame", project, &error) && allOk;
    return allOk;
}

void CharacterEditorPanel::addCharacter()
{
    CharacterSheet character;
    copyText(character.name.data(), character.name.size(), "New Character");
    copyText(character.bio.data(), character.bio.size(), "");
    copyText(character.spriteReference.data(), character.spriteReference.size(), "assets/game/sprites/new_sprite.sprite.json");
    addDefaultAnimationSlots(character);
    if (characters_.empty()) {
        character.playable = true;
    }
    characters_.push_back(character);
    selectedCharacter_ = static_cast<int>(characters_.size()) - 1;
    syncedFrameCharacter_ = -1;
}

void CharacterEditorPanel::deleteSelectedCharacter(EditorContext& context)
{
    if (characters_.size() <= 1 || selectedCharacter_ < 0 || selectedCharacter_ >= static_cast<int>(characters_.size())) {
        return;
    }

    const int deletedIndex = selectedCharacter_;
    const bool deletedPlayable = characters_[static_cast<std::size_t>(deletedIndex)].playable;
    const std::string deletedId = characterId(characters_[static_cast<std::size_t>(deletedIndex)]);
    if (!deletedId.empty()) {
        deletedCharacterIds_.push_back(deletedId);
    }

    characters_.erase(characters_.begin() + deletedIndex);
    selectedCharacter_ = std::clamp(deletedIndex, 0, static_cast<int>(characters_.size()) - 1);
    spriteEditAnimation_ = -1;
    syncedFrameCharacter_ = -1;
    syncedFrameSpriteReference_.clear();
    frameThumbnails_.clear();

    if (deletedPlayable && !characters_.empty()) {
        for (CharacterSheet& character : characters_) {
            character.playable = false;
        }
        characters_[static_cast<std::size_t>(selectedCharacter_)].playable = true;
    }

    context.markDirty();
}

void CharacterEditorPanel::purgeDeletedCharacters(const EditorContext& context)
{
    if (deletedCharacterIds_.empty()) {
        return;
    }

    std::error_code error;
    for (const std::string& id : deletedCharacterIds_) {
        std::filesystem::remove(context.assets.gameCharacterPath() / (id + ".adcharacter"), error);
        error.clear();
    }
    deletedCharacterIds_.clear();
}

void CharacterEditorPanel::addDefaultAnimationSlots(CharacterSheet& character)
{
    for (const char* state : kAnimationStates) {
        const auto existing = std::find_if(character.animations.begin(), character.animations.end(), [&](const CharacterAnimationSlot& slot) {
            return std::string(slot.state.data()) == state;
        });
        if (existing != character.animations.end()) {
            continue;
        }
        CharacterAnimationSlot slot;
        copyText(slot.state.data(), slot.state.size(), state);
        copyText(slot.spriteReference.data(), slot.spriteReference.size(), character.spriteReference.data());
        character.animations.push_back(slot);
    }
}

void CharacterEditorPanel::syncFrameAssignmentsFromSprite(EditorContext& context, CharacterSheet& character)
{
    if (character.spriteReference[0] == '\0') {
        return;
    }

    game::SpriteMetadata metadata;
    std::string error;
    const std::filesystem::path metadataPath = resolveProjectPath(context, character.spriteReference.data());
    if (!game::loadSpriteMetadata(metadataPath, metadata, &error)) {
        return;
    }

    for (int i = 0; i < static_cast<int>(metadata.frames.size()); ++i) {
        const auto existing = std::find_if(character.frameAssignments.begin(), character.frameAssignments.end(), [&](const CharacterFrameAssignment& assignment) {
            return assignment.frameIndex == i;
        });
        if (existing != character.frameAssignments.end()) {
            continue;
        }

        CharacterFrameAssignment assignment;
        assignment.frameIndex = i;
        const std::string state = metadata.frames[static_cast<std::size_t>(i)].type.empty() ? "idle" : metadata.frames[static_cast<std::size_t>(i)].type;
        copyText(assignment.state.data(), assignment.state.size(), state.c_str());
        const std::filesystem::path framePath = context.assets.rawSprites / (metadata.id + "_frame_" + std::to_string(i + 1) + ".png");
        copyText(assignment.frameImage.data(), assignment.frameImage.size(), framePath.generic_string().c_str());
        character.frameAssignments.push_back(assignment);
    }

    character.frameAssignments.erase(
        std::remove_if(character.frameAssignments.begin(), character.frameAssignments.end(), [&](const CharacterFrameAssignment& assignment) {
            return assignment.frameIndex < 0 || assignment.frameIndex >= static_cast<int>(metadata.frames.size());
        }),
        character.frameAssignments.end());
    std::sort(character.frameAssignments.begin(), character.frameAssignments.end(), [](const CharacterFrameAssignment& a, const CharacterFrameAssignment& b) {
        return a.frameIndex < b.frameIndex;
    });
}

bool CharacterEditorPanel::ensureUniqueSprite(EditorContext& context, CharacterSheet& character)
{
    const std::string id = characterId(character);
    const std::string targetReference = (std::filesystem::path("assets/game/sprites") / (id + ".sprite.json")).generic_string();
    const std::string currentReference = textOf(character.spriteReference.data());
    if (currentReference == targetReference) {
        return true;
    }

    game::SpriteMetadata metadata;
    std::string error;
    const std::filesystem::path currentPath = currentReference.empty()
        ? std::filesystem::path{}
        : resolveProjectPath(context, currentReference);
    if (currentPath.empty() || !game::loadSpriteMetadata(currentPath, metadata, &error)) {
        metadata = game::SpriteMetadata{};
        constexpr int defaultSpriteSize = game::kTileSize * 2;
        metadata.canvasSize = {defaultSpriteSize, defaultSpriteSize};
        metadata.gridSize = metadata.canvasSize;
        metadata.pivot = {defaultSpriteSize / 2, defaultSpriteSize / 2};
        metadata.frames = {{0, 0, defaultSpriteSize, defaultSpriteSize, 100, "idle", ""}};
        metadata.tags = {"idle"};
    }

    const std::string oldSpriteId = metadata.id;
    metadata.id = id;
    const std::filesystem::path targetSourceRelative = std::filesystem::path("assets/raw/sprites") / (id + "_sheet.png");
    const std::filesystem::path targetSource = context.assets.projectRoot / targetSourceRelative;
    const std::filesystem::path sourcePath = metadata.source.empty()
        ? std::filesystem::path{}
        : resolveProjectPath(context, metadata.source);
    std::error_code copyError;
    std::filesystem::create_directories(context.assets.rawSprites, copyError);
    if (!sourcePath.empty() && std::filesystem::exists(sourcePath, copyError) && sourcePath != targetSource) {
        std::filesystem::copy_file(sourcePath, targetSource, std::filesystem::copy_options::overwrite_existing, copyError);
    }
    metadata.source = targetSourceRelative;

    for (int i = 0; i < static_cast<int>(metadata.frames.size()); ++i) {
        const std::filesystem::path oldFrame = context.assets.rawSprites / (oldSpriteId + "_frame_" + std::to_string(i + 1) + ".png");
        const std::filesystem::path newFrame = context.assets.rawSprites / (id + "_frame_" + std::to_string(i + 1) + ".png");
        copyError.clear();
        if (!oldSpriteId.empty() && oldFrame != newFrame && std::filesystem::exists(oldFrame, copyError)) {
            std::filesystem::copy_file(oldFrame, newFrame, std::filesystem::copy_options::overwrite_existing, copyError);
        }
    }

    std::filesystem::create_directories(context.assets.gameSpritePath(), copyError);
    if (!game::saveSpriteMetadata(context.assets.gameSpritePath() / (id + ".sprite.json"), metadata, &error)) {
        return false;
    }

    copyText(character.spriteReference.data(), character.spriteReference.size(), targetReference.c_str());
    for (CharacterAnimationSlot& slot : character.animations) {
        if (textOf(slot.spriteReference.data()).empty() || textOf(slot.spriteReference.data()) == currentReference) {
            copyText(slot.spriteReference.data(), slot.spriteReference.size(), targetReference.c_str());
        }
    }
    for (CharacterFrameAssignment& assignment : character.frameAssignments) {
        const std::filesystem::path imagePath = assignment.frameImage.data();
        if (imagePath.filename().string().find(oldSpriteId + "_frame_") == 0) {
            const std::filesystem::path newFrame = std::filesystem::path("assets/raw/sprites") /
                (id + "_frame_" + std::to_string(assignment.frameIndex + 1) + ".png");
            copyText(assignment.frameImage.data(), assignment.frameImage.size(), newFrame.generic_string().c_str());
        }
    }
    syncFrameAssignmentsFromSprite(context, character);
    syncedFrameCharacter_ = selectedCharacter_;
    syncedFrameSpriteReference_ = textOf(character.spriteReference.data());
    frameThumbnails_.clear();
    context.markDirty();
    return true;
}

void CharacterEditorPanel::syncSelectedFrameAssignments(EditorContext& context, CharacterSheet& character)
{
    const std::string spriteReference = textOf(character.spriteReference.data());
    if (syncedFrameCharacter_ == selectedCharacter_ && syncedFrameSpriteReference_ == spriteReference) {
        return;
    }

    syncFrameAssignmentsFromSprite(context, character);
    syncedFrameCharacter_ = selectedCharacter_;
    syncedFrameSpriteReference_ = spriteReference;
    frameThumbnails_.clear();
}

void CharacterEditorPanel::loadCharacters(const EditorContext& context)
{
    std::error_code error;
    const std::filesystem::path dir = context.assets.gameCharacterPath();
    if (!std::filesystem::exists(dir, error)) {
        return;
    }

    std::vector<CharacterSheet> loaded;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, error)) {
        if (error || !entry.is_regular_file(error) || entry.path().extension() != ".adcharacter") {
            continue;
        }

        std::ifstream input(entry.path());
        if (!input) {
            continue;
        }

        CharacterSheet character;
        std::string key;
        std::string value;
        std::size_t animationCount = 0;
        std::size_t frameCount = 0;
        input >> key;
        if (key != "ADCHARACTER") {
            continue;
        }
        int version = 0;
        input >> version;
        while (input >> key) {
            if (key == "name" && input >> std::quoted(value)) {
                copyText(character.name.data(), character.name.size(), value.c_str());
            } else if (key == "bio" && input >> std::quoted(value)) {
                copyText(character.bio.data(), character.bio.size(), value.c_str());
            } else if (key == "sprite" && input >> std::quoted(value)) {
                copyText(character.spriteReference.data(), character.spriteReference.size(), value.c_str());
            } else if (key == "playable") {
                int playable = 0;
                input >> playable;
                character.playable = playable != 0;
            } else if (key == "animations") {
                input >> animationCount;
            } else if (key == "frames") {
                input >> frameCount;
            } else if (key == "anim") {
                std::string state;
                std::string reference;
                if (input >> std::quoted(state) >> std::quoted(reference)) {
                    CharacterAnimationSlot slot;
                    copyText(slot.state.data(), slot.state.size(), state.c_str());
                    copyText(slot.spriteReference.data(), slot.spriteReference.size(), reference.c_str());
                    character.animations.push_back(slot);
                }
            } else if (key == "frame") {
                int frameIndex = 0;
                std::string state;
                std::string image;
                if (input >> frameIndex >> std::quoted(state) >> std::quoted(image)) {
                    CharacterFrameAssignment assignment;
                    assignment.frameIndex = frameIndex;
                    copyText(assignment.state.data(), assignment.state.size(), state.c_str());
                    copyText(assignment.frameImage.data(), assignment.frameImage.size(), image.c_str());
                    character.frameAssignments.push_back(assignment);
                }
            } else if (key == "end") {
                break;
            }
        }

        (void)animationCount;
        (void)frameCount;
        addDefaultAnimationSlots(character);
        loaded.push_back(std::move(character));
    }

    if (!loaded.empty()) {
        characters_ = std::move(loaded);
        const bool hasPlayable = std::any_of(characters_.begin(), characters_.end(), [](const CharacterSheet& character) {
            return character.playable;
        });
        if (!hasPlayable) {
            characters_.front().playable = true;
        }
        selectedCharacter_ = 0;
    }
}

bool CharacterEditorPanel::saveCharacter(const EditorContext& context, const CharacterSheet& character)
{
    std::error_code error;
    std::filesystem::create_directories(context.assets.gameCharacterPath(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path outputPath = context.assets.gameCharacterPath() / (characterId(character) + ".adcharacter");
    std::ofstream output(outputPath);
    if (!output) {
        return false;
    }

    output << "ADCHARACTER 1\n";
    output << "name " << std::quoted(textOf(character.name.data())) << "\n";
    output << "bio " << std::quoted(textOf(character.bio.data())) << "\n";
    output << "sprite " << std::quoted(textOf(character.spriteReference.data())) << "\n";
    output << "playable " << (character.playable ? 1 : 0) << "\n";
    output << "animations " << character.animations.size() << "\n";
    for (const CharacterAnimationSlot& slot : character.animations) {
        output << "anim " << std::quoted(textOf(slot.state.data())) << ' '
               << std::quoted(textOf(slot.spriteReference.data())) << "\n";
    }
    output << "frames " << character.frameAssignments.size() << "\n";
    for (const CharacterFrameAssignment& assignment : character.frameAssignments) {
        output << "frame " << assignment.frameIndex << ' '
               << std::quoted(textOf(assignment.state.data())) << ' '
               << std::quoted(portableProjectPath(context.assets.projectRoot, assignment.frameImage.data())) << "\n";
    }
    output << "end\n";
    return static_cast<bool>(output);
}

void CharacterEditorPanel::drawFrameThumbnail(const EditorContext& context, const std::filesystem::path& imagePath)
{
    const std::filesystem::path resolved = resolveProjectPath(context, imagePath);
    const std::string cacheKey = resolved.generic_string();
    FrameThumbnail& thumbnail = frameThumbnails_[cacheKey];
    if (!thumbnail.loaded) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(resolved.string().c_str(), &width, &height, &channels, 4);
        if (data != nullptr && width > 0 && height > 0) {
            thumbnail.width = width;
            thumbnail.height = height;
            thumbnail.pixels.resize(static_cast<std::size_t>(width * height));
            for (int i = 0; i < width * height; ++i) {
                const int j = i * 4;
                thumbnail.pixels[static_cast<std::size_t>(i)] =
                    (static_cast<std::uint32_t>(data[j + 3]) << 24u) |
                    (static_cast<std::uint32_t>(data[j + 2]) << 16u) |
                    (static_cast<std::uint32_t>(data[j + 1]) << 8u) |
                     static_cast<std::uint32_t>(data[j + 0]);
            }
        }
        stbi_image_free(data);
        thumbnail.loaded = true;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{48.0f, 48.0f};
    ImGui::InvisibleButton("frame_thumb", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, editorSurfaceColor());
    if (thumbnail.width > 0 && thumbnail.height > 0 && !thumbnail.pixels.empty()) {
        const float pixel = std::max(1.0f, std::min(size.x / static_cast<float>(thumbnail.width), size.y / static_cast<float>(thumbnail.height)));
        const ImVec2 spriteOrigin{
            origin.x + (size.x - static_cast<float>(thumbnail.width) * pixel) * 0.5f,
            origin.y + (size.y - static_cast<float>(thumbnail.height) * pixel) * 0.5f,
        };
        for (int y = 0; y < thumbnail.height; ++y) {
            for (int x = 0; x < thumbnail.width; ++x) {
                const std::uint32_t rgba = thumbnail.pixels[static_cast<std::size_t>(y * thumbnail.width + x)];
                const auto a = static_cast<unsigned char>((rgba >> 24u) & 0xffu);
                if (a == 0) {
                    continue;
                }
                const ImU32 color = IM_COL32((rgba >> 0u) & 0xffu, (rgba >> 8u) & 0xffu, (rgba >> 16u) & 0xffu, a);
                const ImVec2 min{spriteOrigin.x + static_cast<float>(x) * pixel, spriteOrigin.y + static_cast<float>(y) * pixel};
                drawList->AddRectFilled(min, {min.x + pixel, min.y + pixel}, color);
            }
        }
    }
    drawList->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(120, 128, 136, 220));
}

void CharacterEditorPanel::drawCharacterList(EditorContext& context)
{
    ImGui::TextUnformatted("Characters");
    if (ImGui::Button("Add Character", ImVec2(-1.0f, 34.0f))) {
        addCharacter();
        spriteEditAnimation_ = -1;
        context.markDirty();
    }

    const bool canDelete = characters_.size() > 1 && selectedCharacter_ >= 0 && selectedCharacter_ < static_cast<int>(characters_.size());
    if (!canDelete) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Character", ImVec2(-1.0f, 34.0f))) {
        deleteSelectedCharacter(context);
    }
    if (!canDelete) {
        ImGui::EndDisabled();
    }
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
        ImGui::PushID(i);
        const char* label = characters_[i].name[0] == '\0' ? "Unnamed Character" : characters_[i].name.data();
        if (ImGui::Selectable(label, selectedCharacter_ == i, 0, ImVec2(0.0f, 34.0f))) {
            selectedCharacter_ = i;
        }
        ImGui::PopID();
    }

}

std::optional<std::filesystem::path> CharacterEditorPanel::drawCharacterSheet(EditorContext& context)
{
    selectedCharacter_ = std::clamp(selectedCharacter_, 0, static_cast<int>(characters_.size()) - 1);
    CharacterSheet& character = characters_[selectedCharacter_];
    syncSelectedFrameAssignments(context, character);

    ImGui::TextUnformatted("Character Sheet");
    ImGui::SameLine();
    if (ImGui::Button("Add Character", ImVec2(130.0f, 30.0f))) {
        addCharacter();
        spriteEditAnimation_ = -1;
        context.markDirty();
        return std::nullopt;
    }
    ImGui::SameLine();
    const bool canDelete = characters_.size() > 1;
    if (!canDelete) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Character", ImVec2(150.0f, 30.0f))) {
        deleteSelectedCharacter(context);
        return std::nullopt;
    }
    if (!canDelete) {
        ImGui::EndDisabled();
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
    if (ui::inputTextString("Name", character.name.data(), character.name.size())) {
        context.markDirty();
    }
    if (ui::checkbox("Playable character", "##PlayableCharacter", &character.playable, 128.0f)) {
        if (character.playable) {
            for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
                if (i != selectedCharacter_) {
                    characters_[static_cast<std::size_t>(i)].playable = false;
                }
            }
        } else {
            character.playable = true;
        }
        context.markDirty();
    }

    ImGui::TextUnformatted("Bio");
    if (ImGui::InputTextMultiline(
        "##CharacterBio",
        character.bio.data(),
        character.bio.size(),
        ImVec2(-1.0f, 220.0f))) {
        context.markDirty();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::inputTextString("Sprite", character.spriteReference.data(), character.spriteReference.size())) {
        context.markDirty();
    }
    ImGui::Text("Sprite assets: %s", context.assets.gameSpritePath().string().c_str());
    ImGui::Text("Character assets: %s", context.assets.gameCharacterPath().string().c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, editorButtonColor());
    const bool openSprite = ImGui::Button(character.spriteReference.data()[0] == '\0' ? "No sprite linked" : character.spriteReference.data(), ImVec2(-1.0f, 72.0f));
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open this sprite in the sprite editor");
    }

    if (openSprite) {
        ensureUniqueSprite(context, character);
        spriteEditAnimation_ = -1;
        return resolveProjectPath(context, character.spriteReference.data());
    }

    if (ImGui::Button("Edit Sprite", ImVec2(140.0f, 32.0f))) {
        ensureUniqueSprite(context, character);
        spriteEditAnimation_ = -1;
        return resolveProjectPath(context, character.spriteReference.data());
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh Frames", ImVec2(140.0f, 32.0f))) {
        syncFrameAssignmentsFromSprite(context, character);
        syncedFrameCharacter_ = selectedCharacter_;
        syncedFrameSpriteReference_ = textOf(character.spriteReference.data());
        frameThumbnails_.clear();
        context.markDirty();
    }

    ImGui::Spacing();
    ImGui::Separator();
    drawFrameAssignments(context, character);

    return std::nullopt;
}

void CharacterEditorPanel::drawFrameAssignments(EditorContext& context, CharacterSheet& character)
{
    ImGui::TextUnformatted("Sprite Frames");
    ImGui::TextDisabled("Assign each frame to an animation state. Multiple frames can share the same state.");

    if (character.frameAssignments.empty()) {
        ImGui::TextDisabled("No exported frame PNGs found yet. Return from the sprite editor to save/export frames.");
    }

    for (int i = 0; i < static_cast<int>(character.frameAssignments.size()); ++i) {
        CharacterFrameAssignment& assignment = character.frameAssignments[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        drawFrameThumbnail(context, assignment.frameImage.data());
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Frame %d", assignment.frameIndex + 1);
        const char* preview = assignment.state.data()[0] == '\0' ? "idle" : assignment.state.data();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("Animation State", preview)) {
            for (const char* state : kAnimationStates) {
                const bool selected = std::string(assignment.state.data()) == state;
                if (ImGui::Selectable(state, selected)) {
                    copyText(assignment.state.data(), assignment.state.size(), state);
                    context.markDirty();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ui::inputTextString("Custom State", assignment.state.data(), assignment.state.size())) {
            context.markDirty();
        }
        ImGui::TextDisabled("%s", assignment.frameImage.data());
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

} // namespace adventure::editor
