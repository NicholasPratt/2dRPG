#include "game/dialogue_graph.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <system_error>
#include <utility>

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

bool validOptionalToken(const std::string& value)
{
    return value.empty() || value == "-" || validToken(value);
}

std::string writeToken(const std::string& value)
{
    return value.empty() ? "-" : value;
}

std::string readTokenValue(const std::string& value)
{
    return value == "-" ? std::string{} : value;
}

void writeCondition(std::ostream& output, const DialogueCondition& condition)
{
    output << static_cast<int>(condition.type) << ' '
           << static_cast<int>(condition.op) << ' '
           << writeToken(condition.variableId) << ' '
           << condition.intValue << ' '
           << (condition.boolValue ? 1 : 0);
}

bool readCondition(std::istream& input, DialogueCondition& condition)
{
    int type = 0;
    int op = 0;
    std::string variableId;
    int boolValue = 0;
    input >> type >> op >> variableId >> condition.intValue >> boolValue;
    if (!input || !validOptionalToken(variableId)) {
        return false;
    }
    condition.type = static_cast<DialogueConditionType>(std::clamp(type, 0, 4));
    condition.op = static_cast<DialogueCompareOp>(std::clamp(op, 0, 5));
    condition.variableId = readTokenValue(variableId);
    condition.boolValue = boolValue != 0;
    return true;
}

} // namespace

bool saveDialogueGraph(const std::filesystem::path& path, const DialogueGraph& graph, std::string* errorMessage)
{
    if (!validToken(graph.id) || !validToken(graph.startNodeId)) {
        setError(errorMessage, "Dialogue graph id or start node id is invalid.");
        return false;
    }
    for (const DialogueNode& node : graph.nodes) {
        if (!validToken(node.id) || !validOptionalToken(node.nextNodeId) || !validOptionalToken(node.falseNodeId)) {
            setError(errorMessage, "Dialogue node has an invalid id or link: " + node.id);
            return false;
        }
        for (const DialogueChoice& choice : node.choices) {
            if (!validOptionalToken(choice.targetNodeId)) {
                setError(errorMessage, "Dialogue choice has an invalid target node.");
                return false;
            }
        }
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open dialogue graph for writing.");
        return false;
    }

    output << "ADDIALOGUE 1\n";
    output << "id " << graph.id << "\n";
    output << "start " << graph.startNodeId << "\n";
    output << "nodes " << graph.nodes.size() << "\n";
    for (const DialogueNode& node : graph.nodes) {
        output << "node " << node.id << ' '
               << static_cast<int>(node.type) << ' '
               << node.editorX << ' ' << node.editorY << ' '
               << std::quoted(node.speaker) << ' '
               << std::quoted(node.text) << ' '
               << writeToken(node.nextNodeId) << ' '
               << writeToken(node.falseNodeId) << "\n";
        output << "condition ";
        writeCondition(output, node.condition);
        output << "\n";
        output << "choices " << node.choices.size() << "\n";
        for (const DialogueChoice& choice : node.choices) {
            output << "choice " << std::quoted(choice.text) << ' ' << writeToken(choice.targetNodeId) << ' ';
            writeCondition(output, choice.condition);
            output << "\n";
        }
        output << "actions " << node.actions.size() << "\n";
        for (const DialogueAction& action : node.actions) {
            output << "action " << static_cast<int>(action.type) << ' '
                   << writeToken(action.targetId) << ' '
                   << std::quoted(action.textValue) << ' '
                   << action.intValue << ' '
                   << (action.boolValue ? 1 : 0) << ' '
                   << action.x << ' ' << action.y << "\n";
        }
    }
    output << "end\n";
    return static_cast<bool>(output);
}

bool loadDialogueGraph(const std::filesystem::path& path, DialogueGraph& graph, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open dialogue graph for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADDIALOGUE" || version != 1) {
        setError(errorMessage, "Unsupported dialogue graph file.");
        return false;
    }

    DialogueGraph loaded;
    std::string key;
    std::size_t expectedNodes = 0;
    while (input >> key) {
        if (key == "id") {
            input >> loaded.id;
            if (!validToken(loaded.id)) {
                setError(errorMessage, "Invalid dialogue graph id.");
                return false;
            }
        } else if (key == "start") {
            input >> loaded.startNodeId;
            if (!validToken(loaded.startNodeId)) {
                setError(errorMessage, "Invalid dialogue graph start node id.");
                return false;
            }
        } else if (key == "nodes") {
            input >> expectedNodes;
            loaded.nodes.reserve(expectedNodes);
        } else if (key == "node") {
            DialogueNode node;
            int type = 0;
            std::string nextNodeId;
            std::string falseNodeId;
            input >> node.id >> type >> node.editorX >> node.editorY
                  >> std::quoted(node.speaker) >> std::quoted(node.text)
                  >> nextNodeId >> falseNodeId;
            if (!input || !validToken(node.id) || !validOptionalToken(nextNodeId) || !validOptionalToken(falseNodeId)) {
                setError(errorMessage, "Invalid dialogue node.");
                return false;
            }
            node.type = static_cast<DialogueNodeType>(std::clamp(type, 0, 5));
            node.nextNodeId = readTokenValue(nextNodeId);
            node.falseNodeId = readTokenValue(falseNodeId);

            input >> key;
            if (key != "condition" || !readCondition(input, node.condition)) {
                setError(errorMessage, "Expected dialogue node condition.");
                return false;
            }

            std::size_t choiceCount = 0;
            input >> key >> choiceCount;
            if (key != "choices" || !input) {
                setError(errorMessage, "Expected dialogue node choices.");
                return false;
            }
            node.choices.reserve(choiceCount);
            for (std::size_t i = 0; i < choiceCount; ++i) {
                DialogueChoice choice;
                std::string targetNodeId;
                input >> key >> std::quoted(choice.text) >> targetNodeId;
                if (key != "choice" || !validOptionalToken(targetNodeId) || !readCondition(input, choice.condition)) {
                    setError(errorMessage, "Invalid dialogue choice.");
                    return false;
                }
                choice.targetNodeId = readTokenValue(targetNodeId);
                node.choices.push_back(std::move(choice));
            }

            std::size_t actionCount = 0;
            input >> key >> actionCount;
            if (key != "actions" || !input) {
                setError(errorMessage, "Expected dialogue node actions.");
                return false;
            }
            node.actions.reserve(actionCount);
            for (std::size_t i = 0; i < actionCount; ++i) {
                DialogueAction action;
                int typeValue = 0;
                std::string targetId;
                int boolValue = 0;
                input >> key >> typeValue >> targetId >> std::quoted(action.textValue)
                      >> action.intValue >> boolValue >> action.x >> action.y;
                if (key != "action" || !validOptionalToken(targetId) || !input) {
                    setError(errorMessage, "Invalid dialogue action.");
                    return false;
                }
                action.type = static_cast<DialogueActionType>(std::clamp(typeValue, 0, 16));
                action.targetId = readTokenValue(targetId);
                action.boolValue = boolValue != 0;
                node.actions.push_back(std::move(action));
            }

            loaded.nodes.push_back(std::move(node));
        } else if (key == "end") {
            break;
        } else {
            setError(errorMessage, "Unexpected dialogue graph entry: " + key);
            return false;
        }
    }

    if (expectedNodes != 0 && loaded.nodes.size() != expectedNodes) {
        setError(errorMessage, "Dialogue graph node count did not match file header.");
        return false;
    }

    graph = std::move(loaded);
    return true;
}

} // namespace adventure::game
