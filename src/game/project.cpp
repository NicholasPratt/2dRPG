#include "game/project.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace adventure::game {
namespace {

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validToken(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    });
}

} // namespace

bool saveGameProject(const std::filesystem::path& path, const GameProject& project, std::string* errorMessage)
{
    if (!validToken(project.id)) {
        setError(errorMessage, "Project id is invalid.");
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open game project for writing.");
        return false;
    }

    output << "ADGAME 1\n";
    output << "id " << project.id << "\n";
    output << "playable " << (project.playableCharacterId.empty() ? "-" : project.playableCharacterId) << "\n";
    output << "characters " << project.characterIds.size() << "\n";
    for (const std::string& id : project.characterIds) {
        output << "character " << id << "\n";
    }
    output << "end\n";
    return static_cast<bool>(output);
}

bool loadGameProject(const std::filesystem::path& path, GameProject& project, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open game project for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADGAME" || version != 1) {
        setError(errorMessage, "Unsupported game project file.");
        return false;
    }

    GameProject loaded;
    std::string key;
    while (input >> key) {
        if (key == "id") {
            input >> loaded.id;
        } else if (key == "playable") {
            input >> loaded.playableCharacterId;
            if (loaded.playableCharacterId == "-") {
                loaded.playableCharacterId.clear();
            }
        } else if (key == "characters") {
            std::size_t count = 0;
            input >> count;
            loaded.characterIds.reserve(count);
        } else if (key == "character") {
            std::string id;
            input >> id;
            if (!id.empty()) {
                loaded.characterIds.push_back(id);
            }
        } else if (key == "end") {
            break;
        }
    }

    if (!validToken(loaded.id)) {
        setError(errorMessage, "Loaded game project is invalid.");
        return false;
    }

    project = std::move(loaded);
    return true;
}

} // namespace adventure::game
