#include "editor/editor_app.hpp"
#include "editor/asset_directories.hpp"

#include "imgui.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    adventure::editor::AssetDirectories smokeAssets;
    smokeAssets.projectRoot = "build/editor_asset_structure_smoke";
    std::string error;
    if (!smokeAssets.ensureRequiredPaths(&error)) {
        std::cerr << error << "\n";
        return 1;
    }
    for (const std::filesystem::path& path : smokeAssets.requiredPaths()) {
        if (!std::filesystem::is_directory(path)) {
            std::cerr << "Missing required asset directory: " << path << "\n";
            return 1;
        }
    }
    if (argc > 1 && std::string(argv[1]) == "--directories-only") {
        std::cout << "Created and verified " << smokeAssets.requiredPaths().size()
                  << " required asset directories.\n";
        return 0;
    }

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
