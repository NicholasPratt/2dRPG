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
        if (ImGui::BeginTabItem("Sprites")) {
            spriteEditor_.draw(context_);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Maps")) {
            ImGui::TextUnformatted("Map editor panel will live here.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Assets")) {
            ImGui::Text("Raw sprites: %s", context_.assets.rawSpritePath().string().c_str());
            ImGui::Text("Game sprites: %s", context_.assets.gameSpritePath().string().c_str());
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace adventure::editor
