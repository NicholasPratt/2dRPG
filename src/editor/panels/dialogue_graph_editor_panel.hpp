#pragma once

#include "editor/editor_context.hpp"
#include "game/dialogue_graph.hpp"

#include "imgui.h"

#include <array>
#include <string>
#include <vector>

namespace adventure::editor {

class DialogueGraphEditorPanel {
public:
    void draw(EditorContext& context);
    void openGraph(EditorContext& context, const std::string& graphId);
    void save(EditorContext& context);

private:
    game::DialogueGraph graph_;
    std::vector<std::string> graphIds_;
    std::array<char, 64> graphId_{'d', 'i', 'a', 'l', 'o', 'g', 'u', 'e', '_', '1', '\0'};
    std::array<char, 64> newGraphId_{'d', 'i', 'a', 'l', 'o', 'g', 'u', 'e', '_', '1', '\0'};
    int selectedGraph_ = -1;
    int selectedNode_ = -1;
    bool centerSelectedNode_ = false;
    bool focusSelectedNodeEditor_ = false;
    bool loaded_ = false;
    bool dirty_ = false;
    std::string lastProjectRoot_;
    std::string status_;
    std::vector<std::string> validationWarnings_;
    std::vector<std::string> simulationLog_;

    void refreshGraphList(EditorContext& context, bool loadSelected = true);
    void loadSelectedGraph(EditorContext& context);
    void saveCurrentGraph(EditorContext& context);
    void createGraph(EditorContext& context);
    void drawGraphList(EditorContext& context);
    void drawNodeNavigator();
    void drawCanvas(EditorContext& context);
    void drawInspector(EditorContext& context);
    void drawLinks(ImDrawList* drawList, ImVec2 origin) const;
    void addNode(game::DialogueNodeType type);
    void deleteSelectedNode();
    void selectNode(int index);
    void validateGraph(const EditorContext& context);
    void simulateGraph(const EditorContext& context);
    [[nodiscard]] bool nodeExists(const std::string& nodeId) const;
    [[nodiscard]] bool drawTargetPicker(const char* label, std::string& targetNodeId);
    [[nodiscard]] const game::DialogueNode* nodeById(const std::string& nodeId) const;
    [[nodiscard]] game::DialogueNode* selectedNode();
    [[nodiscard]] const game::DialogueNode* selectedNode() const;
    [[nodiscard]] std::string uniqueNodeId(const char* prefix) const;
};

} // namespace adventure::editor
