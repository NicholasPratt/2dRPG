#include "editor/editor_app.hpp"
#include "editor/editor_state.hpp"
#include "editor/imgui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace adventure::editor {
namespace {

std::filesystem::path runningExecutableDirectory()
{
#if defined(__APPLE__)
    std::vector<char> buffer(1024);
    std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
    }
    std::error_code error;
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), error).parent_path();
#else
    return {};
#endif
}

std::filesystem::path findProjectRoot()
{
    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    const std::filesystem::path executableDir = runningExecutableDirectory();
    const std::vector<std::filesystem::path> candidates = {
#if defined(ADVENTURE_SOURCE_ROOT)
        std::filesystem::path(ADVENTURE_SOURCE_ROOT),
#endif
        cwd,
        cwd.parent_path(),
        executableDir,
        executableDir.parent_path(),
        executableDir.parent_path().parent_path(),
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (std::filesystem::exists(candidate / "CMakeLists.txt", error) &&
            std::filesystem::exists(candidate / "projects", error)) {
            return std::filesystem::weakly_canonical(candidate, error);
        }
    }

    return cwd.empty() ? std::filesystem::path(".") : cwd;
}

bool launchDetachedProcess(const std::filesystem::path& executable,
    const std::filesystem::path& chapterPath,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& logPath,
    const std::vector<std::string>& extraArgs,
    std::string& errorMessage)
{
    std::error_code error;
    std::filesystem::create_directories(logPath.parent_path(), error);

    const std::string executableString = executable.string();
    const std::string chapterString = chapterPath.string();
    const std::string projectRootString = projectRoot.string();
    const std::string logString = logPath.string();

    const pid_t pid = fork();
    if (pid < 0) {
        errorMessage = std::string("fork failed: ") + std::strerror(errno);
        return false;
    }

    if (pid == 0) {
        if (chdir(projectRootString.c_str()) != 0) {
            _exit(126);
        }

        const int logFd = open(logString.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }

        std::vector<std::string> argStrings = {executableString, chapterString};
        argStrings.insert(argStrings.end(), extraArgs.begin(), extraArgs.end());
        std::vector<char*> argv;
        argv.reserve(argStrings.size() + 1);
        for (std::string& s : argStrings) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
        execv(executableString.c_str(), argv.data());
        _exit(127);
    }

    return true;
}

std::string sanitizedId(const char* value, const char* fallback)
{
    std::string id;
    for (const char* p = value; p != nullptr && *p != '\0'; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        if (std::isalnum(ch) || *p == '_' || *p == '-') {
            id.push_back(static_cast<char>(ch));
        } else if (*p == ' ' || *p == '.') {
            id.push_back('_');
        }
    }
    if (id.empty()) {
        id = fallback;
    }
    return id;
}

const char* stateVariableTypeName(game::StateVariableType type)
{
    switch (type) {
        case game::StateVariableType::Integer:
            return "Integer";
        case game::StateVariableType::Boolean:
            return "Boolean";
        case game::StateVariableType::Item:
            return "Item";
    }
    return "Integer";
}

const char* gameEffectTypeName(game::GameEffectType type)
{
    switch (type) {
        case game::GameEffectType::SetInt:
            return "Set Integer";
        case game::GameEffectType::AddInt:
            return "Add Integer";
        case game::GameEffectType::SetBool:
            return "Set Boolean";
        case game::GameEffectType::GiveItem:
            return "Give Item";
        case game::GameEffectType::TakeItem:
            return "Take Item";
    }
    return "Add Integer";
}

const char* itemDefTypeName(game::ItemDefType type)
{
    switch (type) {
        case game::ItemDefType::Weapon: return "Weapon";
        case game::ItemDefType::Ammo: return "Ammo";
        case game::ItemDefType::Health: return "Health";
        case game::ItemDefType::Mana: return "Mana";
        case game::ItemDefType::Currency: return "Currency";
        case game::ItemDefType::Key: return "Key";
        case game::ItemDefType::Quest: return "Quest";
        case game::ItemDefType::Consumable: return "Consumable";
        case game::ItemDefType::Material: return "Material";
        case game::ItemDefType::Equipment: return "Equipment";
        case game::ItemDefType::Custom: return "Custom";
    }
    return "Custom";
}

bool editString(const char* label, std::string& value)
{
    char buffer[128]{};
    std::memcpy(buffer, value.data(), std::min(value.size(), sizeof(buffer) - 1));
    if (ui::inputTextString(label, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

} // namespace

EditorApp::EditorApp()
{
    workspaceRoot_ = findProjectRoot();
    context_.assets.projectRoot = workspaceRoot_;

    // Reopen the project and chapter from the last session, if still valid.
    refreshProjectList();
    loadSession();
}

void EditorApp::draw()
{
    if (projectIds_.empty()) {
        refreshProjectList();
    }
    if (!currentProjectId_.empty() && chapterIds_.empty()) {
        refreshChapterList();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Adventure Editor", nullptr, flags);

    drawChapterMenu();

    if (!startupChapterChosen_) {
        drawStartupChapterModal();
        ImGui::End();
        return;
    }

    if (pendingExit_ || pendingChapterSwitch_ || pendingProjectSwitch_) {
        drawUnsavedChangesModal();
    }

    drawProjectManagerWindow();

    if (context_.requestChapterSwitch) {
        context_.requestChapterSwitch = false;
        requestChapterSwitch(context_.requestedChapterSwitchId);
        context_.requestedChapterSwitchId.clear();
    }

    if (context_.requestEditScreenGraphics) {
        context_.requestEditScreenGraphics = false;
        if (!context_.selectedScreenMapId.empty()) {
            layoutEditor_.saveDirtyMaps(context_);
            wallFloorPaint_.openScreenGraphics(context_, context_.selectedScreenMapId);
            mapEditor_.openMapId(context_, context_.selectedScreenMapId);
        }
        enterScreenMode(ScreenEditMode::Graphics);
        screenMapLogicMode_ = false;
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
        spriteEditorLaunchedFromCharacter_ = false;
    }

    if (context_.requestEditScreenMusic) {
        context_.requestEditScreenMusic = false;
        if (!context_.selectedScreenId.empty()) {
            layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
        }
        enterScreenMode(ScreenEditMode::Music);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditEnemies) {
        context_.requestEditEnemies = false;
        if (!context_.selectedScreenId.empty()) {
            layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
        }
        enterScreenMode(ScreenEditMode::Enemies);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditEnemyTypes) {
        context_.requestEditEnemyTypes = false;
        enterScreenMode(ScreenEditMode::EnemyTypes);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditItems) {
        context_.requestEditItems = false;
        enterScreenMode(ScreenEditMode::Items);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditDoors) {
        context_.requestEditDoors = false;
        enterScreenMode(ScreenEditMode::Doors);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditItemDetails) {
        context_.requestEditItemDetails = false;
        enterScreenMode(ScreenEditMode::ItemEdit);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditNpcs) {
        context_.requestEditNpcs = false;
        if (!context_.selectedScreenId.empty()) {
            layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
        }
        enterScreenMode(ScreenEditMode::Npcs);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditNpcTypes) {
        context_.requestEditNpcTypes = false;
        enterScreenMode(ScreenEditMode::NpcTypes);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditDialogueGraph) {
        context_.requestEditDialogueGraph = false;
        if (screenEditMode_ == ScreenEditMode::Npcs) {
            layoutEditor_.applyContextSelectedScreenData(context_);
        }
        if (!context_.requestedDialogueGraphId.empty()) {
            dialogueGraphEditor_.openGraph(context_, context_.requestedDialogueGraphId);
            context_.requestedDialogueGraphId.clear();
        }
        enterScreenMode(ScreenEditMode::DialogueGraph);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (context_.requestEditSprite) {
        context_.requestEditSprite = false;
        // A seed turns a screen-graphics selection into a new animated-tile sprite,
        // but only when the sprite asset does not already exist (re-edit reopens it).
        const std::filesystem::path seedMetadata = context_.pendingSpriteSeed.valid()
            ? context_.assets.gameSpritePath() / (context_.pendingSpriteSeed.id + ".sprite.json")
            : std::filesystem::path{};
        if (context_.pendingSpriteSeed.valid() && !std::filesystem::exists(seedMetadata)) {
            spriteEditor_.createSpriteFromPixels(context_, context_.pendingSpriteSeed);
        } else if (!context_.requestedSpriteReference.empty()) {
            spriteEditor_.openSpriteReference(context_.requestedSpriteReference);
        }
        context_.requestedSpriteReference.clear();
        context_.pendingSpriteSeed.clear();
        spriteEditorLaunchedFromCharacter_ = false;
        spriteReturnMode_ = screenEditMode_ == ScreenEditMode::Sprite ? ScreenEditMode::Layout : screenEditMode_;
        enterScreenMode(ScreenEditMode::Sprite);
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;
    }

    if (ImGui::BeginTabBar("EditorMainTabs")) {
        if (screenEditMode_ == ScreenEditMode::Layout) {
            ImGuiTabItemFlags characterTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Characters ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Characters", nullptr, characterTabFlags)) {
                if (characterTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                if (auto spriteToOpen = characterEditor_.draw(context_)) {
                    spriteEditor_.openCharacterSpriteReference(*spriteToOpen);
                    spriteEditorLaunchedFromCharacter_ = true;
                    spriteEditorLaunchedFromProjectItems_ = false;
                    spriteReturnMode_ = ScreenEditMode::Layout;
                    enterScreenMode(ScreenEditMode::Sprite);
                    requestedTab_ = MainTab::Layout;
                    hasRequestedTab_ = true;
                }
                ImGui::EndTabItem();
            }

            ImGuiTabItemFlags weaponsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Weapons ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Weapons", nullptr, weaponsTabFlags)) {
                if (weaponsTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                if (auto spriteToOpen = weaponEditor_.draw(context_)) {
                    spriteEditor_.openSpriteReference(*spriteToOpen);
                    spriteEditorLaunchedFromCharacter_ = false;
                    spriteEditorLaunchedFromProjectItems_ = false;
                    spriteReturnMode_ = ScreenEditMode::Layout;
                    enterScreenMode(ScreenEditMode::Sprite);
                    requestedTab_ = MainTab::Layout;
                    hasRequestedTab_ = true;
                }
                ImGui::EndTabItem();
            }

            ImGuiTabItemFlags itemsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Items ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Items", nullptr, itemsTabFlags)) {
                if (itemsTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                drawProjectItemsTab();
                ImGui::EndTabItem();
            }

            ImGuiTabItemFlags questStateTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::QuestState ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Quest State", nullptr, questStateTabFlags)) {
                if (questStateTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                drawProjectStateTab();
                ImGui::EndTabItem();
            }
        }

        ImGuiTabItemFlags layoutTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Layout ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Screens", nullptr, layoutTabFlags)) {
            if (layoutTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            drawScreensTab();
            ImGui::EndTabItem();
        }

        if (screenEditMode_ == ScreenEditMode::Layout) {
            ImGuiTabItemFlags tilesetsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Tilesets ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Tilesets", nullptr, tilesetsTabFlags)) {
                if (tilesetsTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                spriteEditorLaunchedFromCharacter_ = false;
                tilesetEditor_.draw(context_);
                ImGui::EndTabItem();
            }

            ImGuiTabItemFlags assetsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Assets ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Assets", nullptr, assetsTabFlags)) {
                if (assetsTabFlags != 0) {
                    hasRequestedTab_ = false;
                }
                spriteEditorLaunchedFromCharacter_ = false;
                ImGui::Text("Raw sprites: %s", context_.assets.rawSpritePath().string().c_str());
                ImGui::Text("Raw character sprites: %s", context_.assets.rawCharacterSpritePath().string().c_str());
                ImGui::Text("Raw tilesets: %s", context_.assets.rawTilesetPath().string().c_str());
                ImGui::Text("Game sprites: %s", context_.assets.gameSpritePath().string().c_str());
                ImGui::Text("Game character sprites: %s", context_.assets.gameCharacterSpritePath().string().c_str());
                ImGui::Text("Game characters: %s", context_.assets.gameCharacterPath().string().c_str());
                ImGui::Text("Game chapters: %s", context_.assets.gameChapterPath().string().c_str());
                ImGui::Text("Game maps: %s", context_.assets.gameMapPath().string().c_str());
                ImGui::Text("Game tilesets: %s", context_.assets.gameTilesetPath().string().c_str());
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void EditorApp::enterScreenMode(ScreenEditMode mode)
{
    if (mode == ScreenEditMode::Enemies && !context_.selectedScreenId.empty()) {
        layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
        enemyPlacementSnapshot_ = context_.selectedScreenEnemies;
    } else if (mode == ScreenEditMode::EnemyTypes) {
        enemyTypeSnapshot_ = context_.enemyTypes;
    } else if (mode == ScreenEditMode::Npcs && !context_.selectedScreenId.empty()) {
        layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
        npcPlacementSnapshot_ = context_.selectedScreenNpcs;
        npcEditor_.openForScreen(context_);
    } else if (mode == ScreenEditMode::NpcTypes) {
        npcTypeSnapshot_ = context_.npcTypes;
    } else if (mode == ScreenEditMode::Items && !context_.selectedScreenMapId.empty()) {
        // Load items from the current screen's map
        game::TileMap map;
        if (game::loadTileMap(context_.assets.gameMapPath() / (context_.selectedScreenMapId + ".admap"), map, nullptr)) {
            context_.selectedScreenItems = map.items;
        } else {
            context_.selectedScreenItems.clear();
        }
        context_.selectedScreenItemsMapId = context_.selectedScreenMapId;
        itemPlacementSnapshot_ = context_.selectedScreenItems;
        itemPlacementEditor_.openForScreen(context_);
    } else if (mode == ScreenEditMode::Doors && !context_.selectedScreenMapId.empty()) {
        game::TileMap map;
        if (game::loadTileMap(context_.assets.gameMapPath() / (context_.selectedScreenMapId + ".admap"), map, nullptr)) {
            context_.selectedScreenDoors = map.doors;
        } else {
            context_.selectedScreenDoors.clear();
        }
        context_.selectedScreenDoorsMapId = context_.selectedScreenMapId;
        doorPlacementSnapshot_ = context_.selectedScreenDoors;
        doorPlacementEditor_.openForScreen(context_);
    }
    screenEditMode_ = mode;
}

void EditorApp::drawScreensTab()
{
    switch (screenEditMode_) {
        case ScreenEditMode::Layout:
            layoutEditor_.draw(context_);
            break;
        case ScreenEditMode::Graphics:
            drawScopedEditHeader(screenMapLogicMode_ ? "Editing Screen Logic" : "Editing Screen Graphics", true, true);
            ImGui::SameLine();
            if (ImGui::Button(screenMapLogicMode_ ? "Paint Graphics" : "Map Logic", ImVec2(130.0f, 30.0f))) {
                screenMapLogicMode_ = !screenMapLogicMode_;
                if (screenMapLogicMode_ && !context_.selectedScreenMapId.empty()) {
                    wallFloorPaint_.saveForChapter(context_);
                    mapEditor_.openMapId(context_, context_.selectedScreenMapId);
                } else if (!screenMapLogicMode_ && !context_.selectedScreenMapId.empty()) {
                    mapEditor_.saveMap(context_);
                    wallFloorPaint_.openScreenGraphics(context_, context_.selectedScreenMapId);
                }
            }
            ImGui::Separator();
            if (screenMapLogicMode_) {
                mapEditor_.draw(context_);
            } else {
                if (context_.requestScreenPlacementSync) {
                    context_.requestScreenPlacementSync = false;
                    if (!context_.selectedScreenId.empty()) {
                        layoutEditor_.selectScreenById(context_, context_.selectedScreenId);
                    }
                }
                wallFloorPaint_.draw(context_);
            }
            break;
        case ScreenEditMode::Music:
            drawScopedEditHeader("Editing Screen Music", true, true);
            ImGui::Separator();
            layoutEditor_.drawScreenMusic(context_);
            break;
        case ScreenEditMode::Enemies:
            drawScopedEditHeader("Editing Screen Enemies", true, true);
            ImGui::Separator();
            enemyPathEditor_.draw(context_);
            break;
        case ScreenEditMode::EnemyTypes:
            drawScopedEditHeader("Editing Enemy Types", true, true);
            ImGui::Separator();
            enemyPathEditor_.drawTypes(context_);
            break;
        case ScreenEditMode::Items:
            drawScopedEditHeader("Placing Screen Items", true, true);
            ImGui::Separator();
            itemPlacementEditor_.drawPlacement(context_);
            break;
        case ScreenEditMode::ItemEdit:
            drawScopedEditHeader("Editing Screen Item", true, true);
            ImGui::Separator();
            itemPlacementEditor_.drawEdit(context_);
            break;
        case ScreenEditMode::Doors:
            drawScopedEditHeader("Editing Screen Doors", true, true);
            ImGui::Separator();
            doorPlacementEditor_.draw(context_);
            break;
        case ScreenEditMode::Npcs:
            drawScopedEditHeader("Editing Screen NPCs", true, true);
            ImGui::Separator();
            npcEditor_.draw(context_);
            break;
        case ScreenEditMode::NpcTypes:
            drawScopedEditHeader("Editing NPC Types", true, true);
            ImGui::Separator();
            npcEditor_.drawTypes(context_);
            break;
        case ScreenEditMode::DialogueGraph:
            drawScopedEditHeader("Editing NPC Dialogue", true, true);
            ImGui::Separator();
            dialogueGraphEditor_.draw(context_);
            break;
        case ScreenEditMode::Sprite:
            drawScopedEditHeader("Editing Sprite", true, true);
            ImGui::Separator();
            spriteEditor_.draw(context_);
            break;
    }
}

void EditorApp::drawProjectStateTab()
{
    ImGui::TextUnformatted("State Variables");
    ImGui::SameLine();
    if (ImGui::Button("Add Variable")) {
        game::StateVariableDef variable;
        variable.id = "Variable_" + std::to_string(context_.stateVariables.size() + 1);
        context_.stateVariables.push_back(std::move(variable));
        context_.markDirty();
    }

    if (context_.stateVariables.empty()) {
        ImGui::TextDisabled("No variables defined.");
    }
    for (int i = 0; i < static_cast<int>(context_.stateVariables.size()); ++i) {
        game::StateVariableDef& variable = context_.stateVariables[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::Separator();
        if (editString("ID", variable.id)) {
            context_.markDirty();
        }
        int type = static_cast<int>(variable.type);
        if (ImGui::BeginCombo("Type", stateVariableTypeName(variable.type))) {
            for (int t = 0; t < 3; ++t) {
                const auto candidate = static_cast<game::StateVariableType>(t);
                if (ImGui::Selectable(stateVariableTypeName(candidate), type == t)) {
                    variable.type = candidate;
                    context_.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (variable.type == game::StateVariableType::Integer) {
            if (ImGui::DragInt("Default", &variable.defaultInt, 0.2f)) {
                context_.markDirty();
            }
        } else if (variable.type == game::StateVariableType::Boolean) {
            if (ImGui::Checkbox("Default", &variable.defaultBool)) {
                context_.markDirty();
            }
        }
        if (ImGui::Button("Delete Variable")) {
            context_.stateVariables.erase(context_.stateVariables.begin() + i);
            context_.markDirty();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Reusable Effects");
    ImGui::SameLine();
    if (ImGui::Button("Add Effect")) {
        game::GameEffectDef effect;
        effect.id = "effect_" + std::to_string(context_.effectDefs.size() + 1);
        if (!context_.stateVariables.empty()) {
            effect.targetId = context_.stateVariables.front().id;
        }
        context_.effectDefs.push_back(std::move(effect));
        context_.markDirty();
    }

    if (context_.effectDefs.empty()) {
        ImGui::TextDisabled("No effects defined.");
    }
    for (int i = 0; i < static_cast<int>(context_.effectDefs.size()); ++i) {
        game::GameEffectDef& effect = context_.effectDefs[static_cast<std::size_t>(i)];
        ImGui::PushID(1000 + i);
        ImGui::Separator();
        if (editString("ID", effect.id)) {
            context_.markDirty();
        }
        int type = static_cast<int>(effect.type);
        if (ImGui::BeginCombo("Effect", gameEffectTypeName(effect.type))) {
            for (int t = 0; t < 5; ++t) {
                const auto candidate = static_cast<game::GameEffectType>(t);
                if (ImGui::Selectable(gameEffectTypeName(candidate), type == t)) {
                    effect.type = candidate;
                    context_.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (editString("Target ID", effect.targetId)) {
            context_.markDirty();
        }
        if (effect.type == game::GameEffectType::SetInt || effect.type == game::GameEffectType::AddInt) {
            if (ImGui::DragInt("Value", &effect.intValue, 0.2f)) {
                context_.markDirty();
            }
        } else if (effect.type == game::GameEffectType::SetBool) {
            if (ImGui::Checkbox("Value", &effect.boolValue)) {
                context_.markDirty();
            }
        }
        if (ImGui::Button("Delete Effect")) {
            context_.effectDefs.erase(context_.effectDefs.begin() + i);
            context_.markDirty();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Definitions", ImVec2(180.0f, 32.0f))) {
        saveProjectMetadata();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Project Font");
    if (editString("TTF path", context_.fontPath)) {
        context_.markDirty();
    }
    ImGui::Text("Font folder: %s", context_.assets.gameFontPath().string().c_str());
}

void EditorApp::drawProjectItemsTab()
{
    const auto itemIdCount = [this](const std::string& id) {
        return static_cast<int>(std::count_if(context_.itemDefs.begin(), context_.itemDefs.end(), [&id](const game::ItemDef& item) {
            return item.id == id;
        }));
    };
    const auto addDefaultIfMissing = [this](const game::ItemDef& item) {
        const auto it = std::find_if(context_.itemDefs.begin(), context_.itemDefs.end(), [&item](const game::ItemDef& existing) {
            return existing.id == item.id;
        });
        if (it == context_.itemDefs.end()) {
            context_.itemDefs.push_back(item);
            return true;
        }
        return false;
    };

    ImGui::TextUnformatted("Project Items");
    ImGui::SameLine();
    if (ImGui::Button("Add Item Def")) {
        game::ItemDef item;
        item.id = "item_" + std::to_string(context_.itemDefs.size() + 1);
        item.name = "New Item";
        context_.itemDefs.push_back(std::move(item));
        context_.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Missing Defaults")) {
        bool changed = false;
        changed |= addDefaultIfMissing({"gold", "Gold", game::ItemDefType::Currency, "gold", "Money", 1, true, ""});
        changed |= addDefaultIfMissing({"small_key", "Small Key", game::ItemDefType::Key, "small_key", "", 1, true, ""});
        changed |= addDefaultIfMissing({"quest_token", "Quest Token", game::ItemDefType::Quest, "quest_token", "", 1, true, ""});
        changed |= addDefaultIfMissing({"potion", "Potion", game::ItemDefType::Health, "potion", "", 3, true, ""});
        changed |= addDefaultIfMissing({"herb", "Herb", game::ItemDefType::Material, "herb", "", 1, true, ""});
        changed |= addDefaultIfMissing({"mana_orb", "Mana Orb", game::ItemDefType::Mana, "mana_orb", "Mana", 5, true, ""});
        changed |= addDefaultIfMissing({"iron_ore", "Iron Ore", game::ItemDefType::Material, "iron_ore", "", 1, true, ""});
        changed |= addDefaultIfMissing({"bronze_ring", "Bronze Ring", game::ItemDefType::Equipment, "bronze_ring", "", 1, false, ""});
        if (changed) {
            context_.markDirty();
        }
    }

    for (int i = 0; i < static_cast<int>(context_.itemDefs.size()); ++i) {
        game::ItemDef& item = context_.itemDefs[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::Separator();
        if (editString("ID", item.id)) { context_.markDirty(); }
        if (item.id.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "ID is required.");
        } else if (itemIdCount(item.id) > 1) {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "Duplicate item ID.");
        }
        if (editString("Name", item.name)) { context_.markDirty(); }
        int type = static_cast<int>(item.type);
        if (ImGui::BeginCombo("Type", itemDefTypeName(item.type))) {
            for (int t = 0; t <= 10; ++t) {
                const auto candidate = static_cast<game::ItemDefType>(t);
                if (ImGui::Selectable(itemDefTypeName(candidate), t == type)) {
                    item.type = candidate;
                    if (item.type == game::ItemDefType::Currency && item.targetId.empty()) {
                        item.targetId = "Money";
                    }
                    context_.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (editString("Sprite ID", item.spriteId)) { context_.markDirty(); }
        ImGui::SameLine();
        if (ImGui::Button("Edit Sprite")) {
            if (item.spriteId.empty()) {
                item.spriteId = item.id.empty() ? "item_sprite" : item.id;
                context_.markDirty();
            }
            context_.requestedSpriteReference = (context_.assets.gameSpritePath() / (item.spriteId + ".sprite.json")).generic_string();
            context_.requestEditSprite = true;
            spriteEditorLaunchedFromProjectItems_ = true;
        }
        const char* targetLabel = "Target";
        if (item.type == game::ItemDefType::Currency) { targetLabel = "Money Var"; }
        else if (item.type == game::ItemDefType::Mana) { targetLabel = "Mana Var"; }
        else if (item.type == game::ItemDefType::Ammo) { targetLabel = "Ammo Type"; }
        else if (item.type == game::ItemDefType::Weapon) { targetLabel = "Weapon ID"; }
        if (editString(targetLabel, item.targetId)) { context_.markDirty(); }
        if (item.type == game::ItemDefType::Currency) {
            const bool numericTarget = !item.targetId.empty() &&
                std::all_of(item.targetId.begin(), item.targetId.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                });
            if (item.targetId.empty() || numericTarget) {
                item.targetId = "Money";
                context_.markDirty();
            }
        }
        if (item.type == game::ItemDefType::Custom && editString("Custom Type", item.customType)) { context_.markDirty(); }
        if (ImGui::DragInt("Value", &item.value, 1.0f, 0, 999999)) { context_.markDirty(); }
        if (ImGui::Checkbox("Stackable", &item.stackable)) { context_.markDirty(); }
        if (ImGui::Button("Delete Item Def")) {
            context_.itemDefs.erase(context_.itemDefs.begin() + i);
            context_.markDirty();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}

void EditorApp::drawScopedEditHeader(const char* title, bool saveAndExit, bool exitWithoutSaving)
{
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    ImGui::TextDisabled("Screen: %s", context_.selectedScreenId.empty() ? "(none)" : context_.selectedScreenId.c_str());
    ImGui::SameLine();
    if (saveAndExit && ImGui::Button("Save and Exit", ImVec2(140.0f, 30.0f))) {
        exitScreenModeSaving();
    }
    ImGui::SameLine();
    if (saveAndExit && ImGui::Button("Save and Play", ImVec2(140.0f, 30.0f))) {
        launchGame();
    }
    ImGui::SameLine();
    if (saveAndExit && ImGui::Button("Play Selected Screen", ImVec2(170.0f, 30.0f))) {
        launchGame(/*fresh=*/true, context_.selectedScreenId, /*fromCheckpoint=*/false);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Test-launch fresh on the selected screen (full HP, all placed enemies present).");
    ImGui::SameLine();
    if (saveAndExit && ImGui::Button("Play From Last Entry", ImVec2(170.0f, 30.0f))) {
        launchGame(/*fresh=*/true, /*startScreen=*/{}, /*fromCheckpoint=*/true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Test-launch fresh where you last entered a screen in play.");
    ImGui::SameLine();
    if (exitWithoutSaving && ImGui::Button("Exit without Saving", ImVec2(170.0f, 30.0f))) {
        exitScreenModeDiscarding();
    }
    if (!playStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", playStatus_.c_str());
    }
}

void EditorApp::exitScreenModeSaving()
{
    switch (screenEditMode_) {
        case ScreenEditMode::Graphics:
            if (screenMapLogicMode_) {
                mapEditor_.saveMap(context_);
                (void)layoutEditor_.saveCurrentChapter(context_);
            } else {
                wallFloorPaint_.saveForChapter(context_);
                (void)layoutEditor_.saveCurrentChapter(context_);
            }
            break;
        case ScreenEditMode::Music:
            (void)layoutEditor_.saveCurrentChapter(context_);
            break;
        case ScreenEditMode::Enemies:
            layoutEditor_.applyContextSelectedScreenData(context_);
            (void)layoutEditor_.saveCurrentChapter(context_);
            break;
        case ScreenEditMode::EnemyTypes:
            enemyPathEditor_.saveProjectEnemyTypes(context_);
            break;
        case ScreenEditMode::Npcs:
            layoutEditor_.applyContextSelectedScreenData(context_);
            (void)layoutEditor_.saveCurrentChapter(context_);
            break;
        case ScreenEditMode::NpcTypes:
            npcEditor_.saveProjectNpcTypes(context_);
            break;
        case ScreenEditMode::DialogueGraph:
            dialogueGraphEditor_.save(context_);
            layoutEditor_.applyContextSelectedScreenData(context_);
            (void)layoutEditor_.saveCurrentChapter(context_);
            break;
        case ScreenEditMode::Items:
            itemPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::ItemEdit:
            itemPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::Doors:
            doorPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::Sprite:
            spriteEditor_.saveForChapter(context_);
            if (spriteEditorLaunchedFromCharacter_) {
                characterEditor_.setSelectedSpriteReference(context_, spriteEditor_.spriteMetadataReference(context_));
                spriteEditorLaunchedFromCharacter_ = false;
            }
            if (spriteEditorLaunchedFromProjectItems_) {
                requestedTab_ = MainTab::Items;
                hasRequestedTab_ = true;
                spriteEditorLaunchedFromProjectItems_ = false;
            }
            break;
        case ScreenEditMode::Layout:
            break;
    }
    screenEditMode_ = screenEditMode_ == ScreenEditMode::Sprite ? spriteReturnMode_ :
        (screenEditMode_ == ScreenEditMode::ItemEdit ? ScreenEditMode::Items :
        (screenEditMode_ == ScreenEditMode::DialogueGraph ? ScreenEditMode::Npcs : ScreenEditMode::Layout));
}

void EditorApp::exitScreenModeDiscarding()
{
    switch (screenEditMode_) {
        case ScreenEditMode::Graphics:
            wallFloorPaint_.resetScreenBuffers();
            if (!context_.selectedScreenMapId.empty()) {
                wallFloorPaint_.openScreenGraphics(context_, context_.selectedScreenMapId);
                mapEditor_.openMapId(context_, context_.selectedScreenMapId);
            }
            break;
        case ScreenEditMode::Music:
            break;
        case ScreenEditMode::Enemies:
            context_.selectedScreenEnemies = enemyPlacementSnapshot_;
            layoutEditor_.applyContextSelectedScreenData(context_);
            break;
        case ScreenEditMode::EnemyTypes:
            context_.enemyTypes = enemyTypeSnapshot_;
            break;
        case ScreenEditMode::Npcs:
            context_.selectedScreenNpcs = npcPlacementSnapshot_;
            layoutEditor_.applyContextSelectedScreenData(context_);
            break;
        case ScreenEditMode::NpcTypes:
            context_.npcTypes = npcTypeSnapshot_;
            break;
        case ScreenEditMode::DialogueGraph:
            break;
        case ScreenEditMode::Items:
            context_.selectedScreenItems = itemPlacementSnapshot_;
            itemPlacementEditor_.openForScreen(context_);
            break;
        case ScreenEditMode::ItemEdit:
            context_.selectedScreenItems = itemPlacementSnapshot_;
            itemPlacementEditor_.openForScreen(context_);
            break;
        case ScreenEditMode::Doors:
            context_.selectedScreenDoors = doorPlacementSnapshot_;
            doorPlacementEditor_.openForScreen(context_);
            break;
        case ScreenEditMode::Sprite:
            spriteEditor_.resetDocumentBuffers();
            spriteEditorLaunchedFromCharacter_ = false;
            if (spriteEditorLaunchedFromProjectItems_) {
                requestedTab_ = MainTab::Items;
                hasRequestedTab_ = true;
                spriteEditorLaunchedFromProjectItems_ = false;
            }
            break;
        case ScreenEditMode::Layout:
            break;
    }
    screenEditMode_ = screenEditMode_ == ScreenEditMode::Sprite ? spriteReturnMode_ :
        (screenEditMode_ == ScreenEditMode::ItemEdit ? ScreenEditMode::Items :
        (screenEditMode_ == ScreenEditMode::DialogueGraph ? ScreenEditMode::Npcs : ScreenEditMode::Layout));
}

void EditorApp::requestExit()
{
    if (context_.dirty) {
        pendingExit_ = true;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        exitAccepted_ = true;
    }
}

void EditorApp::refreshChapterList()
{
    chapterIds_.clear();
    if (currentProjectId_.empty()) {
        return;
    }
    std::error_code error;
    const std::filesystem::path chapterDir = context_.assets.gameChapterPath();
    if (!std::filesystem::exists(chapterDir, error)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(chapterDir, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) && entry.path().extension() == ".adchapter") {
            chapterIds_.push_back(entry.path().stem().string());
        }
    }
    std::sort(chapterIds_.begin(), chapterIds_.end());
}

std::filesystem::path EditorApp::projectsRoot() const
{
    return workspaceRoot_ / "projects";
}

std::filesystem::path EditorApp::sessionFilePath() const
{
    return projectsRoot() / ".editor_session";
}

void EditorApp::saveSession() const
{
    if (currentProjectId_.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(projectsRoot(), error);
    std::ofstream out(sessionFilePath(), std::ios::trunc);
    if (!out) {
        return;
    }
    out << "project " << currentProjectId_ << "\n";
    out << "chapter " << context_.currentChapterId << "\n";
}

void EditorApp::loadSession()
{
    std::ifstream in(sessionFilePath());
    if (!in) {
        return;
    }
    std::string projectId;
    std::string chapterId;
    std::string key;
    while (in >> key) {
        if (key == "project") {
            in >> std::ws;
            std::getline(in, projectId);
        } else if (key == "chapter") {
            in >> std::ws;
            std::getline(in, chapterId);
        }
    }
    // Trim trailing carriage returns / whitespace.
    const auto trim = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
            s.pop_back();
        }
    };
    trim(projectId);
    trim(chapterId);

    if (projectId.empty() ||
        std::find(projectIds_.begin(), projectIds_.end(), projectId) == projectIds_.end()) {
        return;
    }
    selectProject(projectId);
    if (chapterId.empty() ||
        std::find(chapterIds_.begin(), chapterIds_.end(), chapterId) == chapterIds_.end()) {
        return;
    }
    chooseChapter(chapterId);  // sets startupChapterChosen_ on success
}

void EditorApp::openProject(const std::string& projectId, const std::string& chapterId)
{
    selectProject(projectId);
    std::string target = chapterId;
    if (target.empty() ||
        std::find(chapterIds_.begin(), chapterIds_.end(), target) == chapterIds_.end()) {
        target = chapterIds_.empty() ? std::string{} : chapterIds_.front();
    }
    if (!target.empty()) {
        chooseChapter(target);
    }
    refreshProjectList();
}

void EditorApp::requestProjectOpen(const std::string& projectId, const std::string& chapterId)
{
    if (projectId == currentProjectId_ && chapterId == context_.currentChapterId &&
        startupChapterChosen_) {
        return;
    }
    if (startupChapterChosen_ && context_.dirty) {
        pendingProjectSwitch_ = true;
        pendingProjectId_ = projectId;
        pendingProjectChapterId_ = chapterId;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        openProject(projectId, chapterId);
    }
}

void EditorApp::deleteProject(const std::string& projectId)
{
    if (projectId.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove_all(projectsRoot() / projectId, error);
    if (projectId == currentProjectId_) {
        currentProjectId_.clear();
        context_.currentChapterId.clear();
        chapterIds_.clear();
        startupChapterChosen_ = false;  // fall back to the chooser
    }
    refreshProjectList();
}

void EditorApp::refreshProjectList()
{
    projectIds_.clear();
    std::error_code error;
    const std::filesystem::path root = projectsRoot();
    if (!std::filesystem::exists(root, error)) {
        return;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        if (entry.is_directory(error) && std::filesystem::exists(entry.path() / "assets/game/chapters", error)) {
            projectIds_.push_back(entry.path().filename().string());
        }
    }
    std::sort(projectIds_.begin(), projectIds_.end());
}

void EditorApp::ensureProjectDirectories() const
{
    (void)context_.assets.ensureRequiredPaths();
}

void EditorApp::selectProject(const std::string& projectId)
{
    currentProjectId_ = projectId;
    context_.assets.projectRoot = projectsRoot() / currentProjectId_;
    ensureProjectDirectories();
    loadProjectMetadata();
    chapterIds_.clear();
    refreshChapterList();
}

void EditorApp::loadProjectMetadata()
{
    game::GameProject project;
    if (game::loadGameProject(context_.assets.projectRoot / "assets/game/project.adgame", project, nullptr)) {
        context_.playableCharacterId = project.playableCharacterId;
        context_.importedCharacterIds = project.characterIds;
        context_.enemyTypes = project.enemyTypes;
        context_.weaponDefs = project.weaponDefs;
        context_.itemDefs = project.itemDefs;
        context_.startingWeaponId = project.startingWeaponId;
        context_.stateVariables = project.stateVariables;
        context_.effectDefs = project.effectDefs;
        context_.npcTypes = project.npcTypes;
        context_.fontPath = project.fontPath;
    } else {
        context_.playableCharacterId.clear();
        context_.importedCharacterIds.clear();
        context_.enemyTypes.clear();
        context_.weaponDefs.clear();
        context_.itemDefs.clear();
        context_.startingWeaponId.clear();
        context_.stateVariables.clear();
        context_.effectDefs.clear();
        context_.npcTypes.clear();
        context_.fontPath.clear();
    }
}

void EditorApp::saveProjectMetadata()
{
    if (currentProjectId_.empty()) {
        return;
    }

    game::GameProject project;
    if (!game::loadGameProject(context_.assets.projectRoot / "assets/game/project.adgame", project, nullptr)) {
        project.id = "game";
    }
    project.playableCharacterId = context_.playableCharacterId;
    project.characterIds = context_.importedCharacterIds;
    project.enemyTypes = context_.enemyTypes;
    project.weaponDefs = context_.weaponDefs;
    project.itemDefs = context_.itemDefs;
    project.startingWeaponId = context_.startingWeaponId;
    project.stateVariables = context_.stateVariables;
    project.effectDefs = context_.effectDefs;
    project.npcTypes = context_.npcTypes;
    project.fontPath = context_.fontPath;
    (void)game::saveGameProject(context_.assets.projectRoot / "assets/game/project.adgame", project, nullptr);
}

void EditorApp::drawChapterMenu()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            ImGui::TextDisabled("Current: %s", currentProjectId_.empty() ? "none" : currentProjectId_.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Project Manager...")) {
                refreshProjectList();
                showProjectManager_ = true;
            }
            if (ImGui::MenuItem("Save Project", nullptr, false, !currentProjectId_.empty())) {
                saveCurrentChapterAndExports();
                saveSession();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Chapter")) {
            ImGui::TextDisabled("Current: %s%s",
                context_.currentChapterId.empty() ? "none" : context_.currentChapterId.c_str(),
                context_.dirty ? " *" : "");
            ImGui::Separator();
            if (ImGui::MenuItem("Save")) {
                saveCurrentChapterAndExports();
            }
            if (ImGui::MenuItem("Save and Play Game")) {
                launchGame();
            }
            if (ImGui::MenuItem("Test: Play From Last Entry")) {
                launchGame(/*fresh=*/true, /*startScreen=*/{}, /*fromCheckpoint=*/true);
            }
            if (ImGui::MenuItem("Test: Play Selected Screen", nullptr, false, !context_.selectedScreenId.empty())) {
                launchGame(/*fresh=*/true, context_.selectedScreenId, /*fromCheckpoint=*/false);
            }
            if (ImGui::MenuItem("Refresh list")) {
                refreshChapterList();
            }
            if (!playStatus_.empty()) {
                ImGui::TextDisabled("%s", playStatus_.c_str());
            }
            ImGui::Separator();
            for (const std::string& chapterId : chapterIds_) {
                if (ImGui::MenuItem(chapterId.c_str(), nullptr, chapterId == context_.currentChapterId)) {
                    requestChapterSwitch(chapterId);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorApp::drawStartupChapterModal()
{
    ImGui::OpenPopup("Open Project");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Choose a project folder and chapter.");
        ImGui::Separator();

        if (projectIds_.empty()) {
            ImGui::TextDisabled("No project folders found.");
        }
        for (const std::string& projectId : projectIds_) {
            ImGui::PushID(projectId.c_str());
            const bool selected = projectId == currentProjectId_;
            if (ImGui::Selectable(projectId.c_str(), selected, 0, ImVec2(300.0f, 30.0f))) {
                selectProject(projectId);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                projectPendingDelete_ = projectId;
                ImGui::OpenPopup("Delete Project");
            }
            drawDeleteProjectConfirm();
            ImGui::PopID();
        }

        if (!currentProjectId_.empty()) {
            ImGui::Spacing();
            ImGui::Text("Chapters in %s", currentProjectId_.c_str());
            if (chapterIds_.empty()) {
                ImGui::TextDisabled("No chapter files found.");
            }
            for (const std::string& chapterId : chapterIds_) {
                if (ImGui::Button(chapterId.c_str(), ImVec2(360.0f, 32.0f))) {
                    chooseChapter(chapterId);
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::SetNextItemWidth(220.0f);
        ui::inputTextString("Project name", newProjectId_.data(), newProjectId_.size());
        ImGui::SetNextItemWidth(220.0f);
        ui::inputTextString("Chapter name", newChapterId_.data(), newChapterId_.size());
        if (ImGui::Button("Create / Open Project", ImVec2(360.0f, 34.0f))) {
            createProjectAndChapter();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawProjectManagerWindow()
{
    if (!showProjectManager_) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420.0f, 460.0f}, ImGuiCond_Appearing);
    if (ImGui::Begin("Project Manager", &showProjectManager_)) {
        ImGui::Text("Current: %s%s", currentProjectId_.empty() ? "(none)" : currentProjectId_.c_str(),
            context_.dirty ? " *" : "");
        if (ImGui::Button("Save Project")) {
            saveCurrentChapterAndExports();
            saveSession();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            refreshProjectList();
        }
        ImGui::Separator();

        ImGui::TextUnformatted("Projects");
        ImGui::BeginChild("ProjectManagerList", ImVec2(0.0f, 240.0f), true);
        if (projectIds_.empty()) {
            ImGui::TextDisabled("No projects found.");
        }
        for (const std::string& projectId : projectIds_) {
            ImGui::PushID(projectId.c_str());
            const bool isCurrent = projectId == currentProjectId_;
            ImGui::Text("%s%s", projectId.c_str(), isCurrent ? "  (open)" : "");
            ImGui::SameLine(240.0f);
            if (ImGui::SmallButton("Open")) {
                requestProjectOpen(projectId, {});
                showProjectManager_ = false;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(isCurrent);
            if (ImGui::SmallButton("Delete")) {
                projectPendingDelete_ = projectId;
                ImGui::OpenPopup("Delete Project");
            }
            ImGui::EndDisabled();
            drawDeleteProjectConfirm();
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::TextUnformatted("New project");
        ImGui::SetNextItemWidth(200.0f);
        ui::inputTextString("Project name##pm", newProjectId_.data(), newProjectId_.size());
        ImGui::SetNextItemWidth(200.0f);
        ui::inputTextString("First chapter##pm", newChapterId_.data(), newChapterId_.size());
        if (ImGui::Button("Create Project##pm")) {
            if (context_.dirty) {
                saveCurrentChapterAndExports();
            }
            createProjectAndChapter();
            saveSession();
            showProjectManager_ = false;
        }
    }
    ImGui::End();
}

void EditorApp::drawDeleteProjectConfirm()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Delete Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete project '%s'?", projectPendingDelete_.c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "This permanently removes its folder and cannot be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
            deleteProject(projectPendingDelete_);
            projectPendingDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            projectPendingDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawUnsavedChangesModal()
{
    ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        if (ImGui::Button("Save", ImVec2(110.0f, 0.0f))) {
            if (pendingProjectSwitch_) {
                saveCurrentChapterAndExports();
                const std::string proj = pendingProjectId_;
                const std::string chap = pendingProjectChapterId_;
                pendingProjectSwitch_ = false;
                pendingProjectId_.clear();
                pendingProjectChapterId_.clear();
                openProject(proj, chap);
            } else if (pendingChapterSwitch_) {
                completeChapterSwitch(true);
            } else {
                saveCurrentChapterAndExports();
                pendingExit_ = false;
                exitAccepted_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(110.0f, 0.0f))) {
            context_.dirty = false;
            if (pendingProjectSwitch_) {
                const std::string proj = pendingProjectId_;
                const std::string chap = pendingProjectChapterId_;
                pendingProjectSwitch_ = false;
                pendingProjectId_.clear();
                pendingProjectChapterId_.clear();
                openProject(proj, chap);
            } else if (pendingChapterSwitch_) {
                completeChapterSwitch(false);
            } else {
                pendingExit_ = false;
                exitAccepted_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            pendingExit_ = false;
            pendingChapterSwitch_ = false;
            pendingChapterId_.clear();
            pendingProjectSwitch_ = false;
            pendingProjectId_.clear();
            pendingProjectChapterId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::chooseChapter(const std::string& chapterId)
{
    if (currentProjectId_.empty()) {
        return;
    }
    loadProjectMetadata();
    if (layoutEditor_.loadChapterById(context_, chapterId)) {
        wallFloorPaint_.resetScreenBuffers();
        spriteEditor_.resetDocumentBuffers();
        weaponEditor_.loadWeapons(context_);
        startupChapterChosen_ = true;
        screenEditMode_ = ScreenEditMode::Layout;
        screenMapLogicMode_ = false;
        requestedTab_ = MainTab::Layout;
        hasRequestedTab_ = true;

        const std::filesystem::path statePath = context_.assets.gameChapterPath() / (chapterId + ".adeditor");
        EditorState editorState;
        if (loadEditorState(statePath, editorState)) {
            if (!editorState.selectedScreenId.empty()) {
                layoutEditor_.selectScreenById(context_, editorState.selectedScreenId);
            }
            context_.tilePalette = std::move(editorState.tilePalette);
            if (!editorState.paintPalette.empty()) {
                wallFloorPaint_.setPalette(std::move(editorState.paintPalette));
            }
        }
        saveSession();
    }
}

void EditorApp::createChapter()
{
    if (currentProjectId_.empty()) {
        return;
    }
    layoutEditor_.createChapter(context_, newChapterId_.data());
    if (!context_.currentChapterId.empty()) {
        std::error_code error;
        std::filesystem::create_directories(
            context_.assets.gameDialoguePath() / context_.currentChapterId, error);
    }
    game::TileMap map;
    map.id = context_.selectedScreenMapId.empty() ? "screen_1_map" : context_.selectedScreenMapId;
    map.width = game::kScreenTilesW;
    map.height = game::kScreenTilesH;
    const std::size_t mapSize = static_cast<std::size_t>(map.width * map.height);
    for (auto& layer : map.layers) {
        layer.assign(mapSize, 0u);
    }
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (x == 0 || y == 0 || x + 1 == map.width || y + 1 == map.height) {
                map.layers[1][static_cast<std::size_t>(y) * map.width + x] = 1u;
            }
        }
    }
    std::string mapError;
    (void)game::saveTileMap(context_.assets.gameMapPath() / (map.id + ".admap"), map, &mapError);
    (void)layoutEditor_.saveCurrentChapter(context_);
    wallFloorPaint_.resetScreenBuffers();
    spriteEditor_.resetDocumentBuffers();
    startupChapterChosen_ = true;
    screenEditMode_ = ScreenEditMode::Layout;
    screenMapLogicMode_ = false;
    requestedTab_ = MainTab::Layout;
    hasRequestedTab_ = true;
    refreshChapterList();
    saveSession();
}

void EditorApp::createProjectAndChapter()
{
    const std::string projectId = sanitizedId(newProjectId_.data(), "project_1");
    const std::string chapterId = sanitizedId(newChapterId_.data(), "chapter_1");
    selectProject(projectId);

    std::memset(newChapterId_.data(), 0, newChapterId_.size());
    std::memcpy(newChapterId_.data(), chapterId.data(), std::min(chapterId.size(), newChapterId_.size() - 1));

    if (std::find(chapterIds_.begin(), chapterIds_.end(), chapterId) != chapterIds_.end()) {
        chooseChapter(chapterId);
    } else {
        createChapter();
    }
    refreshProjectList();
}

void EditorApp::requestChapterSwitch(const std::string& chapterId)
{
    if (chapterId == context_.currentChapterId) {
        return;
    }
    if (context_.dirty) {
        pendingChapterSwitch_ = true;
        pendingChapterId_ = chapterId;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        chooseChapter(chapterId);
    }
}

void EditorApp::completeChapterSwitch(bool saveFirst)
{
    if (saveFirst) {
        saveCurrentChapterAndExports();
    }
    const std::string target = pendingChapterId_;
    pendingChapterSwitch_ = false;
    pendingChapterId_.clear();
    chooseChapter(target);
}

void EditorApp::saveCurrentChapterAndExports()
{
    saveActiveEditingScope();

    const bool charactersSaved = characterEditor_.saveForChapter(context_);
    spriteEditor_.saveForChapter(context_);
    const bool graphicsSaved = wallFloorPaint_.saveForChapter(context_);
    (void)layoutEditor_.saveCurrentChapter(context_);
    if (!charactersSaved || !graphicsSaved) {
        context_.markDirty();
    }

    if (!context_.currentChapterId.empty()) {
        const std::filesystem::path statePath = context_.assets.gameChapterPath() / (context_.currentChapterId + ".adeditor");
        EditorState editorState;
        editorState.selectedScreenId = context_.selectedScreenId;
        editorState.tilePalette = context_.tilePalette;
        editorState.paintPalette = wallFloorPaint_.getPalette();
        saveEditorState(statePath, editorState);
    }

    saveProjectMetadata();
    refreshChapterList();
}

void EditorApp::saveActiveEditingScope()
{
    switch (screenEditMode_) {
        case ScreenEditMode::Graphics:
            if (screenMapLogicMode_) {
                mapEditor_.saveMap(context_);
            } else {
                wallFloorPaint_.saveForChapter(context_);
            }
            break;
        case ScreenEditMode::Music:
            (void)layoutEditor_.saveCurrentChapter(context_);
            break;
        case ScreenEditMode::Enemies:
            layoutEditor_.applyContextSelectedScreenData(context_);
            break;
        case ScreenEditMode::EnemyTypes:
            enemyPathEditor_.saveProjectEnemyTypes(context_);
            break;
        case ScreenEditMode::Npcs:
            layoutEditor_.applyContextSelectedScreenData(context_);
            break;
        case ScreenEditMode::NpcTypes:
            npcEditor_.saveProjectNpcTypes(context_);
            break;
        case ScreenEditMode::Items:
            itemPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::ItemEdit:
            itemPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::Doors:
            doorPlacementEditor_.saveForScreen(context_);
            break;
        case ScreenEditMode::Sprite:
            spriteEditor_.saveForChapter(context_);
            if (spriteEditorLaunchedFromCharacter_) {
                characterEditor_.setSelectedSpriteReference(context_, spriteEditor_.spriteMetadataReference(context_));
            }
            break;
        case ScreenEditMode::Layout:
            break;
    }
}

void EditorApp::launchGame(bool fresh, const std::string& startScreen, bool fromCheckpoint)
{
    saveCurrentChapterAndExports();
    if (context_.currentChapterId.empty()) {
        playStatus_ = "No chapter selected.";
        return;
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    const std::filesystem::path executableDir = runningExecutableDirectory();
    std::filesystem::path projectRoot = std::filesystem::absolute(context_.assets.projectRoot, error);
    if (!std::filesystem::exists(projectRoot / "assets", error) && std::filesystem::exists(cwd / "assets", error)) {
        projectRoot = cwd;
    }
    if (!std::filesystem::exists(projectRoot / "assets", error) && std::filesystem::exists(cwd.parent_path() / "assets", error)) {
        projectRoot = cwd.parent_path();
    }
    if (!executableDir.empty() && !std::filesystem::exists(projectRoot / "assets", error) &&
        std::filesystem::exists(executableDir.parent_path() / "assets", error)) {
        projectRoot = executableDir.parent_path();
    }

    const std::vector<std::filesystem::path> executableCandidates = {
        executableDir / "adventure_game_window",
        executableDir / "build" / "adventure_game_window",
        executableDir.parent_path() / "build" / "adventure_game_window",
        projectRoot / "build" / "adventure_game_window",
        cwd / "build" / "adventure_game_window",
        cwd / "adventure_game_window",
        cwd.parent_path() / "build" / "adventure_game_window",
    };

    std::filesystem::path executable;
    for (const std::filesystem::path& candidate : executableCandidates) {
        if (std::filesystem::exists(candidate, error)) {
            executable = std::filesystem::absolute(candidate, error);
            break;
        }
    }

    if (executable.empty()) {
        playStatus_ = "Game executable not found. Build first: cmake --build build";
        return;
    }

    const std::filesystem::path chapterPath = std::filesystem::absolute(
        projectRoot / context_.assets.gameChapters / (context_.currentChapterId + ".adchapter"),
        error);
    if (!std::filesystem::exists(chapterPath, error)) {
        playStatus_ = "Chapter file not found: " + chapterPath.string();
        return;
    }

    const std::filesystem::path logPath = projectRoot / "build" / "adventure_game_window.log";
    if (access(executable.string().c_str(), X_OK) != 0) {
        playStatus_ = "Game executable is not runnable: " + executable.string();
        return;
    }

    // Build runtime arguments for test launches.
    std::vector<std::string> extraArgs;
    if (fresh) {
        extraArgs.emplace_back("--fresh");
    }

    std::string launchScreen = startScreen;
    std::string posDescription;
    if (fromCheckpoint) {
        // Read the runtime's last-entered screen + position.
        const std::filesystem::path checkpointPath = projectRoot / "assets/game/test_checkpoint";
        std::ifstream in(checkpointPath);
        if (!in) {
            playStatus_ = "No checkpoint yet — play once so the runtime records where you entered a screen.";
            return;
        }
        std::string key;
        std::string cpScreen;
        float cpX = -1.0f;
        float cpY = -1.0f;
        while (in >> key) {
            if (key == "screen") {
                in >> cpScreen;
            } else if (key == "pos") {
                in >> cpX >> cpY;
            }
        }
        if (cpScreen.empty()) {
            playStatus_ = "Checkpoint file is unreadable.";
            return;
        }
        launchScreen = cpScreen;
        if (cpX >= 0.0f && cpY >= 0.0f) {
            extraArgs.emplace_back("--pos");
            extraArgs.emplace_back(std::to_string(cpX));
            extraArgs.emplace_back(std::to_string(cpY));
            posDescription = " @ (" + std::to_string(static_cast<int>(cpX)) + "," + std::to_string(static_cast<int>(cpY)) + ")";
        }
    }
    if (!launchScreen.empty()) {
        extraArgs.emplace_back("--screen");
        extraArgs.emplace_back(launchScreen);
    }

    std::string launchError;
    if (launchDetachedProcess(executable, chapterPath, projectRoot, logPath, extraArgs, launchError)) {
        std::string where = launchScreen.empty() ? "chapter start" : launchScreen;
        playStatus_ = std::string("Launched ") + (fresh ? "test" : "game") + ": " + where + posDescription +
            " (log: " + logPath.filename().string() + ")";
    } else {
        playStatus_ = "Failed to launch game: " + launchError;
    }
}

} // namespace adventure::editor
