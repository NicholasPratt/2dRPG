#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace adventure::game {

class GameState {
public:
    [[nodiscard]] int getInt(const std::string& id, int fallback = 0) const;
    void setInt(const std::string& id, int value);
    int addInt(const std::string& id, int delta);

    [[nodiscard]] bool getBool(const std::string& id, bool fallback = false) const;
    void setBool(const std::string& id, bool value);

    [[nodiscard]] bool hasItem(const std::string& id) const;
    void giveItem(const std::string& id);
    void takeItem(const std::string& id);

    void clear();

    [[nodiscard]] const std::unordered_map<std::string, int>& ints() const { return ints_; }
    [[nodiscard]] const std::unordered_map<std::string, bool>& bools() const { return bools_; }
    [[nodiscard]] const std::unordered_set<std::string>& items() const { return items_; }

private:
    std::unordered_map<std::string, int> ints_;
    std::unordered_map<std::string, bool> bools_;
    std::unordered_set<std::string> items_;
};

[[nodiscard]] bool saveGameState(const std::filesystem::path& path, const GameState& state, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadGameState(const std::filesystem::path& path, GameState& state, std::string* errorMessage = nullptr);

} // namespace adventure::game
