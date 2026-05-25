#include "game/engine.hpp"

#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

struct ChapterChoice {
    std::string label;
    std::string projectName;
    std::string chapterName;
    std::filesystem::path projectRoot;
    std::filesystem::path chapterPath;
};

std::filesystem::path executableDirectory()
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
#elif defined(__linux__)
    std::error_code error;
    return std::filesystem::weakly_canonical("/proc/self/exe", error).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path findProjectRoot()
{
    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    const std::filesystem::path exeDir = executableDirectory();
    const std::vector<std::filesystem::path> candidates = {
#if defined(ADVENTURE_SOURCE_ROOT)
        std::filesystem::path(ADVENTURE_SOURCE_ROOT),
#endif
        cwd,
        cwd.parent_path(),
        exeDir,
        exeDir.parent_path(),
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty() &&
            std::filesystem::exists(candidate / "CMakeLists.txt", error) &&
            std::filesystem::exists(candidate / "assets/game/chapters", error)) {
            return std::filesystem::weakly_canonical(candidate, error);
        }
    }
    return cwd.empty() ? std::filesystem::path(".") : cwd;
}

std::filesystem::path projectRootFromChapterPath(const std::filesystem::path& chapterPath, const std::filesystem::path& fallbackRoot)
{
    std::error_code error;
    const std::filesystem::path absoluteChapter = std::filesystem::absolute(chapterPath, error);
    const std::filesystem::path chaptersDir = absoluteChapter.parent_path();
    if (chaptersDir.filename() == "chapters" &&
        chaptersDir.parent_path().filename() == "game" &&
        chaptersDir.parent_path().parent_path().filename() == "assets") {
        return chaptersDir.parent_path().parent_path().parent_path();
    }
    return fallbackRoot;
}

void addChapterChoices(const std::filesystem::path& projectRoot, const std::string& projectName, std::vector<ChapterChoice>& choices)
{
    std::error_code error;
    const std::filesystem::path chapterDir = projectRoot / "assets/game/chapters";
    if (!std::filesystem::exists(chapterDir, error)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(chapterDir, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".adchapter") {
            continue;
        }
        ChapterChoice choice;
        choice.projectName = projectName;
        choice.chapterName = entry.path().stem().string();
        choice.projectRoot = projectRoot;
        choice.chapterPath = entry.path();
        choice.label = choice.projectName + " / " + choice.chapterName;
        choices.push_back(std::move(choice));
    }
}

std::vector<ChapterChoice> scanChapterChoices(const std::filesystem::path& workspaceRoot)
{
    std::vector<ChapterChoice> choices;
    std::error_code error;

    const std::filesystem::path projectsRoot = workspaceRoot / "projects";
    if (std::filesystem::exists(projectsRoot, error)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(projectsRoot, error)) {
            if (entry.is_directory(error)) {
                addChapterChoices(entry.path(), entry.path().filename().string(), choices);
            }
        }
    }

    addChapterChoices(workspaceRoot, "Repository assets", choices);

    std::sort(choices.begin(), choices.end(), [](const ChapterChoice& a, const ChapterChoice& b) {
        if (a.projectName != b.projectName) {
            return a.projectName < b.projectName;
        }
        return a.chapterName < b.chapterName;
    });
    return choices;
}

bool chooseChapterWithWindow(const std::filesystem::path& workspaceRoot, ChapterChoice& selected)
{
    std::vector<ChapterChoice> choices = scanChapterChoices(workspaceRoot);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW for project picker.\n";
        return false;
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    GLFWwindow* window = glfwCreateWindow(520, 420, "Choose Adventure Project", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create project picker window.\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    bool picked = false;
    int selectedIndex = choices.empty() ? -1 : 0;
    while (!glfwWindowShouldClose(window) && !picked) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Choose Project", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Choose a project and chapter to load");
        ImGui::Separator();

        if (choices.empty()) {
            ImGui::TextWrapped("No projects with chapter files were found.");
            ImGui::TextWrapped("Expected: projects/<project>/assets/game/chapters/*.adchapter");
            if (ImGui::Button("Quit", ImVec2(120.0f, 32.0f))) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else {
            ImGui::BeginChild("ProjectChapterList", ImVec2(0.0f, -46.0f), true);
            for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
                const bool isSelected = i == selectedIndex;
                if (ImGui::Selectable(choices[static_cast<std::size_t>(i)].label.c_str(), isSelected)) {
                    selectedIndex = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selectedIndex = i;
                    picked = true;
                }
            }
            ImGui::EndChild();

            if (ImGui::Button("Play", ImVec2(120.0f, 32.0f)) && selectedIndex >= 0) {
                picked = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit", ImVec2(120.0f, 32.0f))) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        ImGui::End();
        ImGui::Render();

        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (picked && selectedIndex >= 0 && selectedIndex < static_cast<int>(choices.size())) {
        selected = choices[static_cast<std::size_t>(selectedIndex)];
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return picked;
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path workspaceRoot = findProjectRoot();
    std::filesystem::path projectRoot = workspaceRoot;
    std::filesystem::path chapterPath;

    if (argc > 1) {
        chapterPath = std::filesystem::path(argv[1]).is_absolute()
            ? std::filesystem::path(argv[1])
            : workspaceRoot / std::filesystem::path(argv[1]);
        projectRoot = projectRootFromChapterPath(chapterPath, workspaceRoot);
    } else {
        ChapterChoice selected;
        if (!chooseChapterWithWindow(workspaceRoot, selected)) {
            return 0;
        }
        projectRoot = selected.projectRoot;
        chapterPath = selected.chapterPath;
    }

    adventure::game::Engine engine(projectRoot);
    std::string error;
    if (!engine.initialize(chapterPath, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    engine.run();
    return 0;
}
