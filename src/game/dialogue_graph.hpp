#pragma once

#include "game/condition.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

enum class DialogueNodeType {
    Start = 0,
    Dialogue = 1,
    Choice = 2,
    Condition = 3,
    Action = 4,
    End = 5,
};

using DialogueConditionType = GameConditionType;
using DialogueCompareOp = GameCompareOp;

enum class DialogueActionType {
    SetInt = 0,
    AddInt = 1,
    SetBool = 2,
    GiveItem = 3,
    TakeItem = 4,
    GiveMoney = 5,
    TakeMoney = 6,
    HealPlayer = 7,
    DamagePlayer = 8,
    MoveNpc = 9,
    HideNpc = 10,
    ShowNpc = 11,
    FollowPlayer = 12,
    StopFollowingPlayer = 13,
    SetNpcAnimation = 14,
    StartQuest = 15,
    CompleteQuest = 16,
};

using DialogueCondition = GameCondition;

struct DialogueAction {
    DialogueActionType type = DialogueActionType::AddInt;
    std::string targetId;
    StateVariableScope scope = StateVariableScope::Universal;
    std::string textValue;
    int intValue = 1;
    bool boolValue = true;
    float x = 0.0f;
    float y = 0.0f;
};

struct DialogueChoice {
    std::string text = "Continue";
    std::string targetNodeId;
    DialogueCondition condition;
};

struct DialogueNode {
    std::string id = "node_1";
    DialogueNodeType type = DialogueNodeType::Dialogue;
    float editorX = 0.0f;
    float editorY = 0.0f;
    std::string speaker;
    std::string text;
    std::string nextNodeId;
    DialogueCondition condition;
    std::string falseNodeId;
    std::vector<DialogueChoice> choices;
    std::vector<DialogueAction> actions;
};

struct DialogueGraph {
    std::string id = "dialogue_1";
    std::string startNodeId = "start";
    std::vector<DialogueNode> nodes;
};

[[nodiscard]] bool saveDialogueGraph(const std::filesystem::path& path, const DialogueGraph& graph, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadDialogueGraph(const std::filesystem::path& path, DialogueGraph& graph, std::string* errorMessage = nullptr);

} // namespace adventure::game
