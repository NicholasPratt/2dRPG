#include "editor/editor_app.hpp"

#include "imgui.h"

namespace adventure::editor {

void EditorApp::draw()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Adventure Editor", nullptr, flags);

    if (ImGui::BeginTabBar("EditorMainTabs")) {
        ImGuiTabItemFlags characterTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Characters ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Characters", nullptr, characterTabFlags)) {
            if (characterTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            if (auto spriteToOpen = characterEditor_.draw(context_)) {
                spriteEditor_.openCharacterSpriteReference(*spriteToOpen);
                characterEditor_.setSelectedSpriteReference(spriteEditor_.spriteMetadataReference(context_));
                spriteEditorLaunchedFromCharacter_ = true;
                requestedTab_ = MainTab::Sprites;
                hasRequestedTab_ = true;
            }
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags spriteTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Sprites ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Sprites", nullptr, spriteTabFlags)) {
            if (spriteTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditor_.draw(context_);
            if (spriteEditorLaunchedFromCharacter_) {
                characterEditor_.setSelectedSpriteReference(spriteEditor_.spriteMetadataReference(context_));
            }
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags layoutTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Layout ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Layout", nullptr, layoutTabFlags)) {
            if (layoutTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            layoutEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags mapsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Maps ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Maps", nullptr, mapsTabFlags)) {
            if (mapsTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            mapEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags tilesetsTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::Tilesets ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Tilesets", nullptr, tilesetsTabFlags)) {
            if (tilesetsTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            tilesetEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags wallFloorTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::WallFloorPaint ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Wall/Floor Paint", nullptr, wallFloorTabFlags)) {
            if (wallFloorTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            wallFloorPaint_.draw(context_);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags enemyPathTabFlags = hasRequestedTab_ && requestedTab_ == MainTab::EnemyPaths ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Enemy Paths", nullptr, enemyPathTabFlags)) {
            if (enemyPathTabFlags != 0) {
                hasRequestedTab_ = false;
            }
            spriteEditorLaunchedFromCharacter_ = false;
            enemyPathEditor_.draw(context_);
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

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace adventure::editor
