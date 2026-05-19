#include "game/engine.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

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
        cwd,
        cwd.parent_path(),
        exeDir,
        exeDir.parent_path(),
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate / "assets", error)) {
            return candidate;
        }
    }
    return cwd.empty() ? std::filesystem::path(".") : cwd;
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path projectRoot = findProjectRoot();
    const std::filesystem::path chapterPath = argc > 1
        ? std::filesystem::path(argv[1])
        : projectRoot / "assets/game/chapters/chapter_1.adchapter";

    adventure::game::Engine engine(projectRoot);
    std::string error;
    if (!engine.initialize(chapterPath, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    engine.run();
    return 0;
}
