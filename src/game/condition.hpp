#pragma once

#include "game/state_types.hpp"

#include <string>

namespace adventure::game {

class GameState;

enum class GameConditionType {
    Always = 0,
    IntCompare = 1,
    BoolEquals = 2,
    HasItem = 3,
    HasMoney = 4,
};

enum class GameCompareOp {
    Equal = 0,
    NotEqual = 1,
    Less = 2,
    LessOrEqual = 3,
    Greater = 4,
    GreaterOrEqual = 5,
};

struct GameCondition {
    GameConditionType type = GameConditionType::Always;
    GameCompareOp op = GameCompareOp::GreaterOrEqual;
    std::string variableId;
    StateVariableScope scope = StateVariableScope::Universal;
    int intValue = 0;
    bool boolValue = true;
};

[[nodiscard]] bool gameConditionPasses(
    const GameCondition& condition, const GameState& state, const std::string& chapterId);

} // namespace adventure::game
