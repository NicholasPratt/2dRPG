#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

struct GameProject {
    std::string id = "game";
    std::string playableCharacterId;
    std::vector<std::string> characterIds;
};

[[nodiscard]] bool saveGameProject(const std::filesystem::path& path, const GameProject& project, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadGameProject(const std::filesystem::path& path, GameProject& project, std::string* errorMessage = nullptr);

} // namespace adventure::game
