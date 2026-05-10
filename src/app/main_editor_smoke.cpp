#include "editor/editor_app.hpp"

#include "imgui.h"

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    adventure::editor::EditorApp editor;

    ImGui::NewFrame();
    editor.draw();
    ImGui::Render();

    ImGui::DestroyContext();
    return 0;
}
