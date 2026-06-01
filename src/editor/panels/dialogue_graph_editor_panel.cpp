#include "editor/panels/dialogue_graph_editor_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace adventure::editor {
namespace {

const char* nodeTypeName(game::DialogueNodeType type)
{
    switch (type) {
        case game::DialogueNodeType::Start: return "Start";
        case game::DialogueNodeType::Dialogue: return "Dialogue";
        case game::DialogueNodeType::Choice: return "Choice";
        case game::DialogueNodeType::Condition: return "Condition";
        case game::DialogueNodeType::Action: return "Action";
        case game::DialogueNodeType::End: return "End";
    }
    return "Dialogue";
}

const char* conditionTypeName(game::DialogueConditionType type)
{
    switch (type) {
        case game::DialogueConditionType::Always: return "Always";
        case game::DialogueConditionType::IntCompare: return "Int Compare";
        case game::DialogueConditionType::BoolEquals: return "Bool Equals";
        case game::DialogueConditionType::HasItem: return "Has Item";
        case game::DialogueConditionType::HasMoney: return "Has Money";
    }
    return "Always";
}

const char* compareOpName(game::DialogueCompareOp op)
{
    switch (op) {
        case game::DialogueCompareOp::Equal: return "==";
        case game::DialogueCompareOp::NotEqual: return "!=";
        case game::DialogueCompareOp::Less: return "<";
        case game::DialogueCompareOp::LessOrEqual: return "<=";
        case game::DialogueCompareOp::Greater: return ">";
        case game::DialogueCompareOp::GreaterOrEqual: return ">=";
    }
    return ">=";
}

const char* actionTypeName(game::DialogueActionType type)
{
    switch (type) {
        case game::DialogueActionType::SetInt: return "Set Int";
        case game::DialogueActionType::AddInt: return "Add Int";
        case game::DialogueActionType::SetBool: return "Set Bool";
        case game::DialogueActionType::GiveItem: return "Give Item";
        case game::DialogueActionType::TakeItem: return "Take Item";
        case game::DialogueActionType::GiveMoney: return "Give Money";
        case game::DialogueActionType::TakeMoney: return "Take Money";
        case game::DialogueActionType::HealPlayer: return "Heal Player";
        case game::DialogueActionType::DamagePlayer: return "Damage Player";
        case game::DialogueActionType::MoveNpc: return "Move NPC";
        case game::DialogueActionType::HideNpc: return "Hide NPC";
        case game::DialogueActionType::ShowNpc: return "Show NPC";
        case game::DialogueActionType::FollowPlayer: return "Follow Player";
        case game::DialogueActionType::StopFollowingPlayer: return "Stop Following";
        case game::DialogueActionType::SetNpcAnimation: return "Set NPC Anim";
        case game::DialogueActionType::StartQuest: return "Start Quest";
        case game::DialogueActionType::CompleteQuest: return "Complete Quest";
    }
    return "Add Int";
}

bool editString(const char* label, std::string& value, std::size_t capacity = 256)
{
    std::vector<char> buffer(capacity, '\0');
    std::memcpy(buffer.data(), value.data(), std::min(value.size(), buffer.size() - 1));
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool editMultiline(const char* label, std::string& value)
{
    char buffer[1024]{};
    std::memcpy(buffer, value.data(), std::min(value.size(), sizeof(buffer) - 1));
    if (ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImVec2(-1.0f, 82.0f))) {
        value = buffer;
        return true;
    }
    return false;
}

bool editCondition(game::DialogueCondition& condition)
{
    bool changed = false;
    int type = static_cast<int>(condition.type);
    if (ImGui::BeginCombo("Condition", conditionTypeName(condition.type))) {
        for (int i = 0; i <= 4; ++i) {
            const auto candidate = static_cast<game::DialogueConditionType>(i);
            if (ImGui::Selectable(conditionTypeName(candidate), i == type)) {
                condition.type = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (condition.type == game::DialogueConditionType::Always) {
        return changed;
    }
    if (condition.type != game::DialogueConditionType::HasMoney) {
        changed = editString("Variable/Item", condition.variableId, 128) || changed;
    }
    if (condition.type == game::DialogueConditionType::IntCompare) {
        int op = static_cast<int>(condition.op);
        if (ImGui::BeginCombo("Compare", compareOpName(condition.op))) {
            for (int i = 0; i <= 5; ++i) {
                const auto candidate = static_cast<game::DialogueCompareOp>(i);
                if (ImGui::Selectable(compareOpName(candidate), i == op)) {
                    condition.op = candidate;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        changed = ImGui::DragInt("Value", &condition.intValue, 1.0f) || changed;
    } else if (condition.type == game::DialogueConditionType::BoolEquals ||
               condition.type == game::DialogueConditionType::HasItem) {
        changed = ImGui::Checkbox("Expected", &condition.boolValue) || changed;
    } else if (condition.type == game::DialogueConditionType::HasMoney) {
        changed = ImGui::DragInt("Money", &condition.intValue, 1.0f, 0, 999999) || changed;
    }
    return changed;
}

ImVec2 nodeInputPoint(ImVec2 origin, const game::DialogueNode& node)
{
    return {origin.x + node.editorX, origin.y + node.editorY + 43.0f};
}

ImVec2 nodeOutputPoint(ImVec2 origin, const game::DialogueNode& node)
{
    return {origin.x + node.editorX + 170.0f, origin.y + node.editorY + 43.0f};
}

void addArrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color)
{
    const float mid = std::max(40.0f, std::abs(to.x - from.x) * 0.45f);
    const ImVec2 c1(from.x + mid, from.y);
    const ImVec2 c2(to.x - mid, to.y);
    drawList->AddBezierCubic(from, c1, c2, to, color, 2.0f, 24);
    const float angle = std::atan2(to.y - c2.y, to.x - c2.x);
    const float size = 7.0f;
    const ImVec2 p1(to.x - std::cos(angle - 0.45f) * size, to.y - std::sin(angle - 0.45f) * size);
    const ImVec2 p2(to.x - std::cos(angle + 0.45f) * size, to.y - std::sin(angle + 0.45f) * size);
    drawList->AddTriangleFilled(to, p1, p2, color);
}

std::filesystem::path chapterDialoguePath(const EditorContext& context)
{
    const std::string chapterId = context.currentChapterId.empty() ? "chapter_1" : context.currentChapterId;
    return context.assets.gameDialoguePath() / chapterId;
}

} // namespace

void DialogueGraphEditorPanel::draw(EditorContext& context)
{
    const std::string root = context.assets.projectRoot.string();
    if (!loaded_ || root != lastProjectRoot_) {
        lastProjectRoot_ = root;
        refreshGraphList(context);
        loaded_ = true;
    }

    const float leftW = 230.0f;
    const float rightW = 360.0f;
    ImGui::BeginChild("DialogueGraphList", ImVec2(leftW, 0.0f), true);
    drawGraphList(context);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("DialogueGraphCanvas", ImVec2(-rightW - 8.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas(context);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("DialogueGraphInspector", ImVec2(rightW, 0.0f), true);
    drawInspector(context);
    ImGui::EndChild();
}

void DialogueGraphEditorPanel::openGraph(EditorContext& context, const std::string& graphId)
{
    if (dirty_) {
        saveCurrentGraph(context);
    }
    refreshGraphList(context, false);
    const auto it = std::find(graphIds_.begin(), graphIds_.end(), graphId);
    if (it != graphIds_.end()) {
        selectedGraph_ = static_cast<int>(std::distance(graphIds_.begin(), it));
        std::snprintf(graphId_.data(), graphId_.size(), "%s", graphId.c_str());
        loadSelectedGraph(context);
        return;
    }
    std::snprintf(graphId_.data(), graphId_.size(), "%s", graphId.c_str());
    std::snprintf(newGraphId_.data(), newGraphId_.size(), "%s", graphId.c_str());
    createGraph(context);
}

void DialogueGraphEditorPanel::save(EditorContext& context)
{
    saveCurrentGraph(context);
}

void DialogueGraphEditorPanel::refreshGraphList(EditorContext& context, bool loadSelected)
{
    graphIds_.clear();
    std::error_code error;
    const std::filesystem::path dialogueDir = chapterDialoguePath(context);
    std::filesystem::create_directories(dialogueDir, error);
    for (const auto& entry : std::filesystem::directory_iterator(dialogueDir, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".addialogue") {
            graphIds_.push_back(entry.path().stem().string());
        }
    }
    std::sort(graphIds_.begin(), graphIds_.end());
    if (selectedGraph_ >= static_cast<int>(graphIds_.size())) {
        selectedGraph_ = static_cast<int>(graphIds_.size()) - 1;
    }
    if (loadSelected && selectedGraph_ >= 0) {
        loadSelectedGraph(context);
    } else if (loadSelected) {
        graph_ = {};
        selectedNode_ = -1;
    }
}

void DialogueGraphEditorPanel::loadSelectedGraph(EditorContext& context)
{
    if (selectedGraph_ < 0 || selectedGraph_ >= static_cast<int>(graphIds_.size())) {
        return;
    }
    const std::string id = graphIds_[static_cast<std::size_t>(selectedGraph_)];
    std::string error;
    if (game::loadDialogueGraph(chapterDialoguePath(context) / (id + ".addialogue"), graph_, &error)) {
        std::snprintf(graphId_.data(), graphId_.size(), "%s", graph_.id.c_str());
        selectNode(graph_.nodes.empty() ? -1 : 0);
        dirty_ = false;
        status_ = "Loaded " + id;
    } else {
        status_ = "Load failed: " + error;
    }
}

void DialogueGraphEditorPanel::saveCurrentGraph(EditorContext& context)
{
    if (graph_.id.empty()) {
        graph_.id = "dialogue_1";
    }
    std::string error;
    if (game::saveDialogueGraph(chapterDialoguePath(context) / (graph_.id + ".addialogue"), graph_, &error)) {
        status_ = "Saved " + graph_.id;
        const std::string savedId = graph_.id;
        refreshGraphList(context, false);
        dirty_ = false;
        const auto it = std::find(graphIds_.begin(), graphIds_.end(), graph_.id);
        if (it != graphIds_.end()) {
            selectedGraph_ = static_cast<int>(std::distance(graphIds_.begin(), it));
        }
        std::snprintf(graphId_.data(), graphId_.size(), "%s", savedId.c_str());
    } else {
        status_ = "Save failed: " + error;
    }
}

void DialogueGraphEditorPanel::createGraph(EditorContext& context)
{
    graph_ = {};
    graph_.id = newGraphId_.data()[0] == '\0' ? "dialogue_1" : newGraphId_.data();
    std::snprintf(graphId_.data(), graphId_.size(), "%s", graph_.id.c_str());
    graph_.startNodeId = "start";
    graph_.nodes.push_back({"start", game::DialogueNodeType::Start, 40.0f, 80.0f, "", "", "dialogue_1"});
    graph_.nodes.push_back({"dialogue_1", game::DialogueNodeType::Dialogue, 280.0f, 80.0f, "NPC", "Hello.", "end"});
    graph_.nodes.push_back({"end", game::DialogueNodeType::End, 520.0f, 80.0f});
    selectNode(0);
    dirty_ = true;
    saveCurrentGraph(context);
}

void DialogueGraphEditorPanel::drawGraphList(EditorContext& context)
{
    ImGui::TextUnformatted("Dialogue Graphs");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##new_graph_id", newGraphId_.data(), newGraphId_.size());
    if (ImGui::Button("New Graph", ImVec2(-1.0f, 26.0f))) {
        if (dirty_) {
            saveCurrentGraph(context);
        }
        createGraph(context);
    }
    if (ImGui::Button("Refresh", ImVec2(-1.0f, 24.0f))) {
        if (dirty_) {
            saveCurrentGraph(context);
        }
        refreshGraphList(context);
    }
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(graphIds_.size()); ++i) {
        if (ImGui::Selectable(graphIds_[static_cast<std::size_t>(i)].c_str(), i == selectedGraph_)) {
            if (dirty_) {
                saveCurrentGraph(context);
            }
            selectedGraph_ = i;
            std::snprintf(graphId_.data(), graphId_.size(), "%s", graphIds_[static_cast<std::size_t>(i)].c_str());
            loadSelectedGraph(context);
        }
    }
    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_.c_str());
    }
    if (dirty_) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "Unsaved changes");
    }
    ImGui::Separator();
    drawNodeNavigator();
}

void DialogueGraphEditorPanel::drawNodeNavigator()
{
    ImGui::TextUnformatted("Nodes");
    if (graph_.nodes.empty()) {
        ImGui::TextDisabled("No nodes");
        return;
    }
    ImGui::BeginChild("DialogueNodeNavigator", ImVec2(0.0f, 0.0f), false);
    for (int i = 0; i < static_cast<int>(graph_.nodes.size()); ++i) {
        const game::DialogueNode& node = graph_.nodes[static_cast<std::size_t>(i)];
        ImGui::PushID(12000 + i);
        const std::string label = node.id + "  [" + nodeTypeName(node.type) + "]";
        if (ImGui::Selectable(label.c_str(), i == selectedNode_)) {
            selectNode(i);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void DialogueGraphEditorPanel::drawCanvas(EditorContext& context)
{
    ImGui::Text("Graph: %s", graph_.id.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(72.0f, 24.0f))) {
        saveCurrentGraph(context);
    }
    ImGui::Separator();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize(1600.0f, 900.0f);
    if (centerSelectedNode_ && selectedNode_ >= 0 && selectedNode_ < static_cast<int>(graph_.nodes.size())) {
        const game::DialogueNode& selected = graph_.nodes[static_cast<std::size_t>(selectedNode_)];
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetScrollX(std::max(0.0f, selected.editorX - available.x * 0.5f + 85.0f));
        ImGui::SetScrollY(std::max(0.0f, selected.editorY - available.y * 0.5f + 43.0f));
        centerSelectedNode_ = false;
    }
    ImGui::InvisibleButton("DialogueCanvasSurface", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, {origin.x + canvasSize.x, origin.y + canvasSize.y}, IM_COL32(24, 27, 31, 255));
    drawLinks(dl, origin);

    for (int i = 0; i < static_cast<int>(graph_.nodes.size()); ++i) {
        game::DialogueNode& node = graph_.nodes[static_cast<std::size_t>(i)];
        const ImVec2 min(origin.x + node.editorX, origin.y + node.editorY);
        const ImVec2 max(min.x + 170.0f, min.y + 86.0f);
        const bool selected = i == selectedNode_;
        const ImU32 fill = selected ? IM_COL32(47, 70, 86, 255) : IM_COL32(38, 43, 50, 255);
        dl->AddRectFilled(min, max, fill, 6.0f);
        dl->AddRect(min, max, selected ? IM_COL32(104, 184, 220, 255) : IM_COL32(82, 90, 100, 255), 6.0f, 0, selected ? 2.0f : 1.0f);
        dl->AddText({min.x + 9.0f, min.y + 7.0f}, IM_COL32(230, 235, 240, 255), node.id.c_str());
        dl->AddText({min.x + 9.0f, min.y + 25.0f}, IM_COL32(148, 204, 226, 255), nodeTypeName(node.type));
        const std::string preview = node.type == game::DialogueNodeType::Action
            ? std::to_string(node.actions.size()) + " action(s)"
            : (node.text.empty() ? node.nextNodeId : node.text.substr(0, 32));
        dl->AddText({min.x + 9.0f, min.y + 51.0f}, IM_COL32(196, 201, 207, 255), preview.c_str());

        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(7000 + i);
        ImGui::InvisibleButton("node", ImVec2(170.0f, 86.0f), ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemClicked()) {
            selectNode(i);
        }
        if (selected && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            node.editorX = std::max(0.0f, node.editorX + delta.x);
            node.editorY = std::max(0.0f, node.editorY + delta.y);
            dirty_ = true;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + canvasSize.y});
}

void DialogueGraphEditorPanel::drawLinks(ImDrawList* drawList, ImVec2 origin) const
{
    auto drawTarget = [&](const game::DialogueNode& from, const std::string& targetId, ImU32 color) {
        if (targetId.empty()) {
            return;
        }
        const game::DialogueNode* to = nodeById(targetId);
        if (to == nullptr) {
            return;
        }
        addArrow(drawList, nodeOutputPoint(origin, from), nodeInputPoint(origin, *to), color);
    };

    for (const game::DialogueNode& node : graph_.nodes) {
        drawTarget(node, node.nextNodeId, IM_COL32(104, 184, 220, 190));
        if (node.type == game::DialogueNodeType::Condition) {
            drawTarget(node, node.falseNodeId, IM_COL32(220, 124, 104, 190));
        }
        if (node.type == game::DialogueNodeType::Choice) {
            for (const game::DialogueChoice& choice : node.choices) {
                drawTarget(node, choice.targetNodeId, IM_COL32(184, 220, 124, 170));
            }
        }
    }
}

void DialogueGraphEditorPanel::drawInspector(EditorContext& context)
{
    if (focusSelectedNodeEditor_) {
        ImGui::SetScrollY(0.0f);
        focusSelectedNodeEditor_ = false;
    }
    ImGui::TextUnformatted("Graph");
    if (editString("ID", graph_.id, 128)) {
        std::snprintf(graphId_.data(), graphId_.size(), "%s", graph_.id.c_str());
        dirty_ = true;
    }
    if (editString("Start Node", graph_.startNodeId, 128)) { dirty_ = true; }
    if (ImGui::Button("Save Graph", ImVec2(-1.0f, 26.0f))) {
        saveCurrentGraph(context);
    }
    if (ImGui::Button("Validate", ImVec2(-1.0f, 24.0f))) {
        validateGraph(context);
    }
    if (ImGui::Button("Simulate", ImVec2(-1.0f, 24.0f))) {
        simulateGraph(context);
    }
    if (!validationWarnings_.empty() && ImGui::TreeNode("Validation")) {
        for (const std::string& warning : validationWarnings_) {
            ImGui::TextWrapped("%s", warning.c_str());
        }
        ImGui::TreePop();
    }
    if (!simulationLog_.empty() && ImGui::TreeNode("Simulation")) {
        for (const std::string& line : simulationLog_) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::Separator();
    if (ImGui::Button("+ Dialogue")) { addNode(game::DialogueNodeType::Dialogue); }
    ImGui::SameLine();
    if (ImGui::Button("+ Choice")) { addNode(game::DialogueNodeType::Choice); }
    if (ImGui::Button("+ Condition")) { addNode(game::DialogueNodeType::Condition); }
    ImGui::SameLine();
    if (ImGui::Button("+ Action")) { addNode(game::DialogueNodeType::Action); }
    if (ImGui::Button("+ End")) { addNode(game::DialogueNodeType::End); }
    ImGui::Separator();

    game::DialogueNode* node = selectedNode();
    if (node == nullptr) {
        ImGui::TextUnformatted("Select a node.");
        return;
    }

    ImGui::Text("Node: %s", node->id.c_str());
    if (editString("Node ID", node->id, 128)) { dirty_ = true; }
    int type = static_cast<int>(node->type);
    if (ImGui::BeginCombo("Type", nodeTypeName(node->type))) {
        for (int i = 0; i <= 5; ++i) {
            const auto candidate = static_cast<game::DialogueNodeType>(i);
            if (ImGui::Selectable(nodeTypeName(candidate), i == type)) {
                node->type = candidate;
                dirty_ = true;
            }
        }
        ImGui::EndCombo();
    }
    if (drawTargetPicker("Next", node->nextNodeId)) { dirty_ = true; }
    if (node->type == game::DialogueNodeType::Condition) {
        if (drawTargetPicker("False", node->falseNodeId)) { dirty_ = true; }
        if (editCondition(node->condition)) { dirty_ = true; }
    }
    if (node->type == game::DialogueNodeType::Dialogue || node->type == game::DialogueNodeType::Choice) {
        if (editString("Speaker", node->speaker, 128)) { dirty_ = true; }
        if (editMultiline("Text", node->text)) { dirty_ = true; }
    }
    if (node->type == game::DialogueNodeType::Choice) {
        ImGui::Separator();
        ImGui::TextUnformatted("Choices");
        for (int i = 0; i < static_cast<int>(node->choices.size()); ++i) {
            ImGui::PushID(9000 + i);
            game::DialogueChoice& choice = node->choices[static_cast<std::size_t>(i)];
            if (editString("Text", choice.text, 256)) { dirty_ = true; }
            if (drawTargetPicker("Target", choice.targetNodeId)) { dirty_ = true; }
            if (ImGui::TreeNode("Condition")) {
                if (editCondition(choice.condition)) { dirty_ = true; }
                ImGui::TreePop();
            }
            if (ImGui::Button("Remove Choice")) {
                node->choices.erase(node->choices.begin() + i);
                dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("+ Choice Row", ImVec2(-1.0f, 24.0f))) {
            node->choices.push_back({});
            dirty_ = true;
        }
    }
    if (node->type == game::DialogueNodeType::Action) {
        ImGui::Separator();
        ImGui::TextUnformatted("Actions");
        for (int i = 0; i < static_cast<int>(node->actions.size()); ++i) {
            ImGui::PushID(10000 + i);
            game::DialogueAction& action = node->actions[static_cast<std::size_t>(i)];
            int actionType = static_cast<int>(action.type);
            if (ImGui::BeginCombo("Type", actionTypeName(action.type))) {
                for (int t = 0; t <= 16; ++t) {
                    const auto candidate = static_cast<game::DialogueActionType>(t);
                    if (ImGui::Selectable(actionTypeName(candidate), t == actionType)) {
                        action.type = candidate;
                        dirty_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (editString("Target", action.targetId, 128)) { dirty_ = true; }
            if (editString("Text Value", action.textValue, 128)) { dirty_ = true; }
            if (ImGui::DragInt("Int", &action.intValue, 1.0f)) { dirty_ = true; }
            if (ImGui::Checkbox("Bool", &action.boolValue)) { dirty_ = true; }
            if (ImGui::DragFloat("X", &action.x, 1.0f)) { dirty_ = true; }
            if (ImGui::DragFloat("Y", &action.y, 1.0f)) { dirty_ = true; }
            if (ImGui::Button("Remove Action")) {
                node->actions.erase(node->actions.begin() + i);
                dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("+ Action Row", ImVec2(-1.0f, 24.0f))) {
            node->actions.push_back({});
            dirty_ = true;
        }
    }
    ImGui::Spacing();
    if (node->type != game::DialogueNodeType::Start && ImGui::Button("Delete Node", ImVec2(-1.0f, 24.0f))) {
        deleteSelectedNode();
    }
}

void DialogueGraphEditorPanel::addNode(game::DialogueNodeType type)
{
    game::DialogueNode node;
    node.type = type;
    node.id = uniqueNodeId(type == game::DialogueNodeType::Condition ? "condition" :
        type == game::DialogueNodeType::Action ? "action" :
        type == game::DialogueNodeType::Choice ? "choice" :
        type == game::DialogueNodeType::End ? "end" : "dialogue");
    node.editorX = 80.0f + static_cast<float>(graph_.nodes.size() % 5) * 210.0f;
    node.editorY = 120.0f + static_cast<float>(graph_.nodes.size() / 5) * 130.0f;
    if (type == game::DialogueNodeType::Dialogue || type == game::DialogueNodeType::Choice) {
        node.speaker = "NPC";
        node.text = "Hello.";
    }
    if (type == game::DialogueNodeType::Choice) {
        node.choices.push_back({"Continue", ""});
    }
    if (type == game::DialogueNodeType::Action) {
        node.actions.push_back({});
    }
    graph_.nodes.push_back(std::move(node));
    dirty_ = true;
    selectNode(static_cast<int>(graph_.nodes.size()) - 1);
}

void DialogueGraphEditorPanel::deleteSelectedNode()
{
    if (selectedNode_ < 0 || selectedNode_ >= static_cast<int>(graph_.nodes.size())) {
        return;
    }
    graph_.nodes.erase(graph_.nodes.begin() + selectedNode_);
    dirty_ = true;
    selectedNode_ = std::min(selectedNode_, static_cast<int>(graph_.nodes.size()) - 1);
}

void DialogueGraphEditorPanel::selectNode(int index)
{
    if (index < 0 || index >= static_cast<int>(graph_.nodes.size())) {
        selectedNode_ = -1;
        return;
    }
    selectedNode_ = index;
    centerSelectedNode_ = true;
    focusSelectedNodeEditor_ = true;
}

void DialogueGraphEditorPanel::validateGraph(const EditorContext& context)
{
    validationWarnings_.clear();
    std::unordered_set<std::string> ids;
    for (const game::DialogueNode& node : graph_.nodes) {
        if (node.id.empty()) {
            validationWarnings_.push_back("A node has an empty ID.");
        } else if (!ids.insert(node.id).second) {
            validationWarnings_.push_back("Duplicate node ID: " + node.id);
        }
    }

    if (!nodeExists(graph_.startNodeId)) {
        validationWarnings_.push_back("Start node does not exist: " + graph_.startNodeId);
    }

    auto checkTarget = [&](const std::string& owner, const std::string& label, const std::string& target) {
        if (!target.empty() && !nodeExists(target)) {
            validationWarnings_.push_back(owner + " has broken " + label + " target: " + target);
        }
    };

    auto stateDefExists = [&](const std::string& id, game::StateVariableType type) {
        return std::any_of(context.stateVariables.begin(), context.stateVariables.end(), [&](const game::StateVariableDef& def) {
            return def.id == id && def.type == type;
        });
    };

    auto validateCondition = [&](const std::string& owner, const game::DialogueCondition& condition) {
        if (condition.type == game::DialogueConditionType::IntCompare &&
            !stateDefExists(condition.variableId, game::StateVariableType::Integer)) {
            validationWarnings_.push_back(owner + " references missing integer variable: " + condition.variableId);
        } else if (condition.type == game::DialogueConditionType::BoolEquals &&
            !stateDefExists(condition.variableId, game::StateVariableType::Boolean)) {
            validationWarnings_.push_back(owner + " references missing boolean variable: " + condition.variableId);
        } else if (condition.type == game::DialogueConditionType::HasItem &&
            !stateDefExists(condition.variableId, game::StateVariableType::Item)) {
            validationWarnings_.push_back(owner + " references missing item variable: " + condition.variableId);
        }
    };

    for (const game::DialogueNode& node : graph_.nodes) {
        checkTarget(node.id, "next", node.nextNodeId);
        if (node.type == game::DialogueNodeType::Condition) {
            checkTarget(node.id, "false", node.falseNodeId);
            validateCondition(node.id, node.condition);
        }
        if (node.type == game::DialogueNodeType::Choice) {
            if (node.choices.empty()) {
                validationWarnings_.push_back(node.id + " has no choices.");
            }
            for (const game::DialogueChoice& choice : node.choices) {
                checkTarget(node.id, "choice", choice.targetNodeId);
                validateCondition(node.id + " choice", choice.condition);
            }
        }
        if (node.type == game::DialogueNodeType::Action) {
            for (const game::DialogueAction& action : node.actions) {
                const bool needsInt = action.type == game::DialogueActionType::SetInt || action.type == game::DialogueActionType::AddInt;
                const bool needsBool = action.type == game::DialogueActionType::SetBool ||
                    action.type == game::DialogueActionType::StartQuest ||
                    action.type == game::DialogueActionType::CompleteQuest;
                const bool needsItem = action.type == game::DialogueActionType::GiveItem || action.type == game::DialogueActionType::TakeItem;
                if (needsInt && !stateDefExists(action.targetId, game::StateVariableType::Integer)) {
                    validationWarnings_.push_back(node.id + " action references missing integer variable: " + action.targetId);
                } else if (needsBool && !stateDefExists(action.targetId, game::StateVariableType::Boolean)) {
                    validationWarnings_.push_back(node.id + " action references missing boolean variable: " + action.targetId);
                } else if (needsItem && !stateDefExists(action.targetId, game::StateVariableType::Item)) {
                    validationWarnings_.push_back(node.id + " action references missing item variable: " + action.targetId);
                }
            }
        }
    }

    std::unordered_set<std::string> reachable;
    std::vector<std::string> stack{graph_.startNodeId};
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        if (id.empty() || !reachable.insert(id).second) {
            continue;
        }
        const game::DialogueNode* node = nodeById(id);
        if (node == nullptr) {
            continue;
        }
        if (!node->nextNodeId.empty()) {
            stack.push_back(node->nextNodeId);
        }
        if (!node->falseNodeId.empty()) {
            stack.push_back(node->falseNodeId);
        }
        for (const game::DialogueChoice& choice : node->choices) {
            if (!choice.targetNodeId.empty()) {
                stack.push_back(choice.targetNodeId);
            }
        }
    }
    for (const game::DialogueNode& node : graph_.nodes) {
        if (!reachable.count(node.id)) {
            validationWarnings_.push_back("Unreachable node: " + node.id);
        }
    }

    if (validationWarnings_.empty()) {
        validationWarnings_.push_back("No validation warnings.");
    }
}

void DialogueGraphEditorPanel::simulateGraph(const EditorContext& context)
{
    simulationLog_.clear();
    std::unordered_map<std::string, int> ints;
    std::unordered_map<std::string, bool> bools;
    std::unordered_set<std::string> items;
    for (const game::StateVariableDef& def : context.stateVariables) {
        if (def.type == game::StateVariableType::Integer) {
            ints[def.id] = def.defaultInt;
        } else if (def.type == game::StateVariableType::Boolean) {
            bools[def.id] = def.defaultBool;
        }
    }

    auto conditionPasses = [&](const game::DialogueCondition& condition) {
        switch (condition.type) {
            case game::DialogueConditionType::Always:
                return true;
            case game::DialogueConditionType::IntCompare: {
                const int value = ints.count(condition.variableId) ? ints[condition.variableId] : 0;
                switch (condition.op) {
                    case game::DialogueCompareOp::Equal: return value == condition.intValue;
                    case game::DialogueCompareOp::NotEqual: return value != condition.intValue;
                    case game::DialogueCompareOp::Less: return value < condition.intValue;
                    case game::DialogueCompareOp::LessOrEqual: return value <= condition.intValue;
                    case game::DialogueCompareOp::Greater: return value > condition.intValue;
                    case game::DialogueCompareOp::GreaterOrEqual: return value >= condition.intValue;
                }
                return false;
            }
            case game::DialogueConditionType::BoolEquals:
                return (bools.count(condition.variableId) ? bools[condition.variableId] : false) == condition.boolValue;
            case game::DialogueConditionType::HasItem:
                return (items.count(condition.variableId) > 0) == condition.boolValue;
            case game::DialogueConditionType::HasMoney:
                return (ints.count("Money") ? ints["Money"] : 0) >= condition.intValue;
        }
        return false;
    };

    std::string current = graph_.startNodeId;
    for (int steps = 0; steps < 80; ++steps) {
        const game::DialogueNode* node = nodeById(current);
        if (node == nullptr) {
            simulationLog_.push_back("Stopped: missing node " + current);
            return;
        }
        simulationLog_.push_back(node->id + " [" + nodeTypeName(node->type) + "]");
        switch (node->type) {
            case game::DialogueNodeType::Start:
                current = node->nextNodeId;
                break;
            case game::DialogueNodeType::Dialogue:
                simulationLog_.push_back((node->speaker.empty() ? "NPC" : node->speaker) + ": " + node->text);
                current = node->nextNodeId;
                break;
            case game::DialogueNodeType::Choice: {
                simulationLog_.push_back((node->speaker.empty() ? "NPC" : node->speaker) + ": " + node->text);
                const auto it = std::find_if(node->choices.begin(), node->choices.end(), [&](const game::DialogueChoice& choice) {
                    return conditionPasses(choice.condition);
                });
                if (it == node->choices.end()) {
                    simulationLog_.push_back("Stopped: no available choice.");
                    return;
                }
                simulationLog_.push_back("Choice: " + it->text);
                current = it->targetNodeId;
                break;
            }
            case game::DialogueNodeType::Condition:
                current = conditionPasses(node->condition) ? node->nextNodeId : node->falseNodeId;
                simulationLog_.push_back(std::string("Condition result: ") + (current == node->nextNodeId ? "true" : "false"));
                break;
            case game::DialogueNodeType::Action:
                for (const game::DialogueAction& action : node->actions) {
                    simulationLog_.push_back(std::string("Action: ") + actionTypeName(action.type));
                    if (action.type == game::DialogueActionType::SetInt) {
                        ints[action.targetId] = action.intValue;
                    } else if (action.type == game::DialogueActionType::AddInt) {
                        ints[action.targetId] += action.intValue;
                    } else if (action.type == game::DialogueActionType::SetBool) {
                        bools[action.targetId] = action.boolValue;
                    } else if (action.type == game::DialogueActionType::GiveItem) {
                        items.insert(action.targetId);
                    } else if (action.type == game::DialogueActionType::TakeItem) {
                        items.erase(action.targetId);
                    } else if (action.type == game::DialogueActionType::GiveMoney) {
                        ints["Money"] += action.intValue;
                    } else if (action.type == game::DialogueActionType::TakeMoney) {
                        ints["Money"] = std::max(0, ints["Money"] - action.intValue);
                    }
                }
                current = node->nextNodeId;
                break;
            case game::DialogueNodeType::End:
                simulationLog_.push_back("End.");
                return;
        }
        if (current.empty()) {
            simulationLog_.push_back("Stopped: empty next node.");
            return;
        }
    }
    simulationLog_.push_back("Stopped: possible cycle or very long graph.");
}

bool DialogueGraphEditorPanel::nodeExists(const std::string& nodeId) const
{
    return nodeById(nodeId) != nullptr;
}

bool DialogueGraphEditorPanel::drawTargetPicker(const char* label, std::string& targetNodeId)
{
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(label, targetNodeId.empty() ? "-" : targetNodeId.c_str())) {
        if (ImGui::Selectable("-", targetNodeId.empty())) {
            targetNodeId.clear();
            changed = true;
        }
        for (const game::DialogueNode& node : graph_.nodes) {
            if (ImGui::Selectable(node.id.c_str(), node.id == targetNodeId)) {
                targetNodeId = node.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

const game::DialogueNode* DialogueGraphEditorPanel::nodeById(const std::string& nodeId) const
{
    const auto it = std::find_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const game::DialogueNode& node) {
        return node.id == nodeId;
    });
    return it == graph_.nodes.end() ? nullptr : &*it;
}

game::DialogueNode* DialogueGraphEditorPanel::selectedNode()
{
    if (selectedNode_ < 0 || selectedNode_ >= static_cast<int>(graph_.nodes.size())) {
        return nullptr;
    }
    return &graph_.nodes[static_cast<std::size_t>(selectedNode_)];
}

const game::DialogueNode* DialogueGraphEditorPanel::selectedNode() const
{
    if (selectedNode_ < 0 || selectedNode_ >= static_cast<int>(graph_.nodes.size())) {
        return nullptr;
    }
    return &graph_.nodes[static_cast<std::size_t>(selectedNode_)];
}

std::string DialogueGraphEditorPanel::uniqueNodeId(const char* prefix) const
{
    for (int i = 1; i < 10000; ++i) {
        const std::string candidate = std::string(prefix) + "_" + std::to_string(i);
        const auto it = std::find_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const game::DialogueNode& node) {
            return node.id == candidate;
        });
        if (it == graph_.nodes.end()) {
            return candidate;
        }
    }
    return std::string(prefix) + "_x";
}

} // namespace adventure::editor
