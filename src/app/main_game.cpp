#include "game/engine.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    const std::filesystem::path projectRoot = ".";
    const std::filesystem::path chapterPath = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("assets/game/chapters/chapter_1.adchapter");

    adventure::game::Engine engine(projectRoot);
    std::string error;
    if (!engine.initialize(chapterPath, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    engine.run();
    return 0;
}
