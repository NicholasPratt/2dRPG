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

    // Defeated enemy instances, keyed "<screenId>/<enemyId>" — persists kills across screens/sessions.
    [[nodiscard]] bool isEnemyDefeated(const std::string& key) const;
    void markEnemyDefeated(const std::string& key);

    void setLocation(const std::string& chapterId, const std::string& screenId, float x, float y);
    void clearLocation();
    [[nodiscard]] bool hasLocation() const { return hasLocation_; }
    [[nodiscard]] const std::string& chapterId() const { return chapterId_; }
    [[nodiscard]] const std::string& screenId() const { return screenId_; }
    [[nodiscard]] float playerX() const { return playerX_; }
    [[nodiscard]] float playerY() const { return playerY_; }

    void clear();

    [[nodiscard]] const std::unordered_map<std::string, int>& ints() const { return ints_; }
    [[nodiscard]] const std::unordered_map<std::string, bool>& bools() const { return bools_; }
    [[nodiscard]] const std::unordered_set<std::string>& items() const { return items_; }
    [[nodiscard]] const std::unordered_set<std::string>& defeatedEnemies() const { return defeatedEnemies_; }

private:
    std::unordered_map<std::string, int> ints_;
    std::unordered_map<std::string, bool> bools_;
    std::unordered_set<std::string> items_;
    std::unordered_set<std::string> defeatedEnemies_;
    std::string chapterId_;
    std::string screenId_;
    float playerX_ = 0.0f;
    float playerY_ = 0.0f;
    bool hasLocation_ = false;
};

[[nodiscard]] bool saveGameState(const std::filesystem::path& path, const GameState& state, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadGameState(const std::filesystem::path& path, GameState& state, std::string* errorMessage = nullptr);
[[nodiscard]] std::string interpolateGameStateText(
    const std::string& text, const GameState& state, const std::string& chapterId = {});

} // namespace adventure::game
