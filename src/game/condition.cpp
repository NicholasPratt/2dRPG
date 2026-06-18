#include "game/condition.hpp"

#include "game/state.hpp"

namespace adventure::game {

bool gameConditionPasses(
    const GameCondition& condition, const GameState& state, const std::string& chapterId)
{
    const auto scopedId = [&chapterId](StateVariableScope scope, const std::string& id) {
        if (scope == StateVariableScope::Universal || chapterId.empty() || id.empty()) {
            return id;
        }
        return "chapter." + chapterId + "." + id;
    };

    switch (condition.type) {
        case GameConditionType::Always:
            return true;
        case GameConditionType::IntCompare: {
            const int value = state.getInt(scopedId(condition.scope, condition.variableId), 0);
            switch (condition.op) {
                case GameCompareOp::Equal: return value == condition.intValue;
                case GameCompareOp::NotEqual: return value != condition.intValue;
                case GameCompareOp::Less: return value < condition.intValue;
                case GameCompareOp::LessOrEqual: return value <= condition.intValue;
                case GameCompareOp::Greater: return value > condition.intValue;
                case GameCompareOp::GreaterOrEqual: return value >= condition.intValue;
            }
            return false;
        }
        case GameConditionType::BoolEquals:
            return state.getBool(scopedId(condition.scope, condition.variableId), false) ==
                condition.boolValue;
        case GameConditionType::HasItem:
            return state.hasItem(condition.variableId) == condition.boolValue;
        case GameConditionType::HasMoney:
            return state.getInt("Money", 0) >= condition.intValue;
    }
    return false;
}

} // namespace adventure::game
