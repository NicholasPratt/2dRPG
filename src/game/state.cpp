#include "game/state.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <vector>

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

template <typename T>
std::vector<std::string> sortedKeys(const T& values)
{
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (const auto& entry : values) {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> sortedItems(const std::unordered_set<std::string>& values)
{
    std::vector<std::string> items(values.begin(), values.end());
    std::sort(items.begin(), items.end());
    return items;
}

} // namespace

int GameState::getInt(const std::string& id, int fallback) const
{
    const auto it = ints_.find(id);
    return it == ints_.end() ? fallback : it->second;
}

void GameState::setInt(const std::string& id, int value)
{
    if (!id.empty()) {
        ints_[id] = value;
    }
}

int GameState::addInt(const std::string& id, int delta)
{
    if (id.empty()) {
        return 0;
    }
    int& value = ints_[id];
    value += delta;
    return value;
}

bool GameState::getBool(const std::string& id, bool fallback) const
{
    const auto it = bools_.find(id);
    return it == bools_.end() ? fallback : it->second;
}

void GameState::setBool(const std::string& id, bool value)
{
    if (!id.empty()) {
        bools_[id] = value;
    }
}

bool GameState::hasItem(const std::string& id) const
{
    return items_.find(id) != items_.end();
}

void GameState::giveItem(const std::string& id)
{
    if (!id.empty()) {
        items_.insert(id);
    }
}

void GameState::takeItem(const std::string& id)
{
    items_.erase(id);
}

bool GameState::isEnemyDefeated(const std::string& key) const
{
    return defeatedEnemies_.find(key) != defeatedEnemies_.end();
}

void GameState::markEnemyDefeated(const std::string& key)
{
    if (!key.empty()) {
        defeatedEnemies_.insert(key);
    }
}

void GameState::clear()
{
    ints_.clear();
    bools_.clear();
    items_.clear();
    defeatedEnemies_.clear();
}

bool saveGameState(const std::filesystem::path& path, const GameState& state, std::string* errorMessage)
{
    for (const auto& [id, value] : state.ints()) {
        (void)value;
        if (!validToken(id)) {
            setError(errorMessage, "Integer state id is invalid: " + id);
            return false;
        }
    }
    for (const auto& [id, value] : state.bools()) {
        (void)value;
        if (!validToken(id)) {
            setError(errorMessage, "Boolean state id is invalid: " + id);
            return false;
        }
    }
    for (const std::string& id : state.items()) {
        if (!validToken(id)) {
            setError(errorMessage, "Item state id is invalid: " + id);
            return false;
        }
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open game state for writing.");
        return false;
    }

    output << "ADSTATE 2\n";

    const std::vector<std::string> intKeys = sortedKeys(state.ints());
    output << "ints " << intKeys.size() << "\n";
    for (const std::string& id : intKeys) {
        output << "int " << id << ' ' << state.ints().at(id) << "\n";
    }

    const std::vector<std::string> boolKeys = sortedKeys(state.bools());
    output << "bools " << boolKeys.size() << "\n";
    for (const std::string& id : boolKeys) {
        output << "bool " << id << ' ' << (state.bools().at(id) ? 1 : 0) << "\n";
    }

    const std::vector<std::string> itemKeys = sortedItems(state.items());
    output << "items " << itemKeys.size() << "\n";
    for (const std::string& id : itemKeys) {
        output << "item " << id << "\n";
    }

    const std::vector<std::string> defeatedKeys = sortedItems(state.defeatedEnemies());
    output << "defeated " << defeatedKeys.size() << "\n";
    for (const std::string& key : defeatedKeys) {
        output << "defeated_enemy " << key << "\n";
    }

    output << "end\n";
    return static_cast<bool>(output);
}

bool loadGameState(const std::filesystem::path& path, GameState& state, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open game state for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADSTATE" || version < 1 || version > 2) {
        setError(errorMessage, "Unsupported game state file.");
        return false;
    }

    GameState loaded;
    std::string key;
    while (input >> key) {
        if (key == "ints" || key == "bools" || key == "items" || key == "defeated") {
            std::size_t ignoredCount = 0;
            input >> ignoredCount;
        } else if (key == "defeated_enemy") {
            std::string enemyKey;
            input >> enemyKey;
            loaded.markEnemyDefeated(enemyKey);
        } else if (key == "int") {
            std::string id;
            int value = 0;
            input >> id >> value;
            if (!validToken(id)) {
                setError(errorMessage, "Invalid integer state id.");
                return false;
            }
            loaded.setInt(id, value);
        } else if (key == "bool") {
            std::string id;
            int value = 0;
            input >> id >> value;
            if (!validToken(id)) {
                setError(errorMessage, "Invalid boolean state id.");
                return false;
            }
            loaded.setBool(id, value != 0);
        } else if (key == "item") {
            std::string id;
            input >> id;
            if (!validToken(id)) {
                setError(errorMessage, "Invalid item state id.");
                return false;
            }
            loaded.giveItem(id);
        } else if (key == "end") {
            break;
        } else {
            setError(errorMessage, "Unexpected game state entry: " + key);
            return false;
        }
    }

    state = std::move(loaded);
    return true;
}

} // namespace adventure::game
