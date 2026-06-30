#pragma once

#include "imgui.h"

#include <algorithm>
#include <cstdint>

namespace adventure::editor {

inline int& editorThemeLevelStorage()
{
    static int level = 4;
    return level;
}

inline int editorThemeLevel()
{
    return editorThemeLevelStorage();
}

inline void setEditorThemeLevel(int level)
{
    editorThemeLevelStorage() = std::clamp(level, 1, 5);
}

inline float editorThemeT()
{
    return static_cast<float>(editorThemeLevel() - 1) / 4.0f;
}

inline float mixThemeValue(float light, float dark)
{
    const float t = editorThemeT();
    return light + (dark - light) * t;
}

inline ImVec4 mixThemeColor(ImVec4 light, ImVec4 dark)
{
    const float t = editorThemeT();
    return ImVec4(
        light.x + (dark.x - light.x) * t,
        light.y + (dark.y - light.y) * t,
        light.z + (dark.z - light.z) * t,
        light.w + (dark.w - light.w) * t);
}

inline int mixThemeByte(int light, int dark)
{
    return static_cast<int>(mixThemeValue(static_cast<float>(light), static_cast<float>(dark)) + 0.5f);
}

inline ImU32 editorCanvasColor()
{
    return IM_COL32(mixThemeByte(202, 35), mixThemeByte(207, 39), mixThemeByte(212, 45), 255);
}

inline ImU32 editorSurfaceColor()
{
    return IM_COL32(mixThemeByte(226, 28), mixThemeByte(229, 32), mixThemeByte(232, 38), 255);
}

inline ImU32 editorPanelColor()
{
    return IM_COL32(mixThemeByte(238, 38), mixThemeByte(240, 43), mixThemeByte(242, 50), 255);
}

inline ImU32 editorButtonColor()
{
    return IM_COL32(mixThemeByte(214, 52), mixThemeByte(218, 58), mixThemeByte(222, 66), 255);
}

inline ImU32 editorButtonHoveredColor()
{
    return IM_COL32(mixThemeByte(196, 66), mixThemeByte(207, 76), mixThemeByte(218, 88), 255);
}

inline ImU32 editorButtonActiveColor()
{
    return IM_COL32(mixThemeByte(177, 78), mixThemeByte(194, 92), mixThemeByte(211, 108), 255);
}

inline ImU32 editorTextColor(int alpha = 255)
{
    return editorThemeLevel() <= 2 ? IM_COL32(35, 39, 44, alpha) : IM_COL32(232, 236, 240, alpha);
}

inline ImU32 editorAccentColor()
{
    return IM_COL32(mixThemeByte(234, 214), mixThemeByte(198, 171), mixThemeByte(74, 54), 255);
}

inline ImU32 editorAccentBorderColor()
{
    return IM_COL32(mixThemeByte(190, 238), mixThemeByte(148, 197), mixThemeByte(38, 74), 255);
}

inline ImVec4 editorClearColor()
{
    return mixThemeColor(ImVec4(0.78f, 0.80f, 0.82f, 1.00f), ImVec4(0.14f, 0.16f, 0.18f, 1.00f));
}

inline void applyEditorTheme(int level = editorThemeLevel())
{
    setEditorThemeLevel(level);
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = mixThemeColor(ImVec4(0.16f, 0.18f, 0.20f, 1.00f), ImVec4(0.90f, 0.92f, 0.94f, 1.00f));
    colors[ImGuiCol_TextDisabled] = mixThemeColor(ImVec4(0.47f, 0.50f, 0.54f, 1.00f), ImVec4(0.56f, 0.59f, 0.63f, 1.00f));
    colors[ImGuiCol_WindowBg] = mixThemeColor(ImVec4(0.78f, 0.80f, 0.82f, 1.00f), ImVec4(0.14f, 0.16f, 0.18f, 1.00f));
    colors[ImGuiCol_ChildBg] = mixThemeColor(ImVec4(0.86f, 0.87f, 0.88f, 1.00f), ImVec4(0.19f, 0.21f, 0.24f, 1.00f));
    colors[ImGuiCol_PopupBg] = mixThemeColor(ImVec4(0.93f, 0.94f, 0.95f, 1.00f), ImVec4(0.18f, 0.20f, 0.23f, 1.00f));
    colors[ImGuiCol_Border] = mixThemeColor(ImVec4(0.48f, 0.52f, 0.57f, 1.00f), ImVec4(0.33f, 0.37f, 0.42f, 1.00f));
    colors[ImGuiCol_BorderShadow] = mixThemeColor(ImVec4(1.00f, 1.00f, 1.00f, 0.20f), ImVec4(0.05f, 0.06f, 0.07f, 0.35f));

    colors[ImGuiCol_FrameBg] = mixThemeColor(ImVec4(0.91f, 0.92f, 0.93f, 1.00f), ImVec4(0.12f, 0.15f, 0.18f, 1.00f));
    colors[ImGuiCol_FrameBgHovered] = mixThemeColor(ImVec4(0.84f, 0.88f, 0.92f, 1.00f), ImVec4(0.20f, 0.27f, 0.33f, 1.00f));
    colors[ImGuiCol_FrameBgActive] = mixThemeColor(ImVec4(0.78f, 0.84f, 0.90f, 1.00f), ImVec4(0.27f, 0.36f, 0.44f, 1.00f));

    colors[ImGuiCol_TitleBg] = mixThemeColor(ImVec4(0.61f, 0.65f, 0.70f, 1.00f), ImVec4(0.12f, 0.15f, 0.18f, 1.00f));
    colors[ImGuiCol_TitleBgActive] = mixThemeColor(ImVec4(0.52f, 0.59f, 0.67f, 1.00f), ImVec4(0.20f, 0.29f, 0.37f, 1.00f));
    colors[ImGuiCol_TitleBgCollapsed] = mixThemeColor(ImVec4(0.67f, 0.70f, 0.74f, 1.00f), ImVec4(0.13f, 0.15f, 0.18f, 1.00f));
    colors[ImGuiCol_MenuBarBg] = mixThemeColor(ImVec4(0.68f, 0.71f, 0.74f, 1.00f), ImVec4(0.13f, 0.15f, 0.18f, 1.00f));

    colors[ImGuiCol_ScrollbarBg] = mixThemeColor(ImVec4(0.77f, 0.79f, 0.81f, 1.00f), ImVec4(0.13f, 0.15f, 0.18f, 1.00f));
    colors[ImGuiCol_ScrollbarGrab] = mixThemeColor(ImVec4(0.56f, 0.60f, 0.65f, 1.00f), ImVec4(0.34f, 0.39f, 0.45f, 1.00f));
    colors[ImGuiCol_ScrollbarGrabHovered] = mixThemeColor(ImVec4(0.48f, 0.54f, 0.61f, 1.00f), ImVec4(0.43f, 0.51f, 0.59f, 1.00f));
    colors[ImGuiCol_ScrollbarGrabActive] = mixThemeColor(ImVec4(0.38f, 0.47f, 0.57f, 1.00f), ImVec4(0.52f, 0.62f, 0.71f, 1.00f));

    colors[ImGuiCol_CheckMark] = ImVec4(0.12f, 0.43f, 0.64f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.33f, 0.53f, 0.67f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.45f, 0.65f, 1.00f);

    colors[ImGuiCol_Button] = mixThemeColor(ImVec4(0.78f, 0.83f, 0.88f, 1.00f), ImVec4(0.22f, 0.27f, 0.32f, 1.00f));
    colors[ImGuiCol_ButtonHovered] = mixThemeColor(ImVec4(0.70f, 0.79f, 0.87f, 1.00f), ImVec4(0.33f, 0.43f, 0.52f, 1.00f));
    colors[ImGuiCol_ButtonActive] = mixThemeColor(ImVec4(0.58f, 0.70f, 0.82f, 1.00f), ImVec4(0.42f, 0.54f, 0.65f, 1.00f));

    colors[ImGuiCol_Header] = ImVec4(0.32f, 0.42f, 0.50f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.40f, 0.52f, 0.62f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.46f, 0.60f, 0.71f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.44f, 0.49f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.34f, 0.50f, 0.64f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.22f, 0.43f, 0.62f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.48f, 0.58f, 0.68f, 0.55f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.36f, 0.52f, 0.68f, 0.75f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.46f, 0.65f, 0.95f);

    colors[ImGuiCol_Tab] = mixThemeColor(ImVec4(0.70f, 0.73f, 0.77f, 1.00f), ImVec4(0.18f, 0.21f, 0.25f, 1.00f));
    colors[ImGuiCol_TabHovered] = mixThemeColor(ImVec4(0.64f, 0.76f, 0.86f, 1.00f), ImVec4(0.35f, 0.47f, 0.58f, 1.00f));
    colors[ImGuiCol_TabActive] = mixThemeColor(ImVec4(0.86f, 0.88f, 0.90f, 1.00f), ImVec4(0.28f, 0.35f, 0.42f, 1.00f));
    colors[ImGuiCol_TabUnfocused] = mixThemeColor(ImVec4(0.67f, 0.70f, 0.74f, 1.00f), ImVec4(0.15f, 0.18f, 0.21f, 1.00f));
    colors[ImGuiCol_TabUnfocusedActive] = mixThemeColor(ImVec4(0.78f, 0.81f, 0.84f, 1.00f), ImVec4(0.23f, 0.28f, 0.34f, 1.00f));

    colors[ImGuiCol_PlotLines] = ImVec4(0.24f, 0.38f, 0.50f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.55f, 0.46f, 0.24f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.30f, 0.34f, 0.39f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.42f, 0.47f, 0.52f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.34f, 0.38f, 0.43f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.25f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.29f, 0.31f, 0.34f, 1.00f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.43f, 0.61f, 0.76f, 0.45f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.13f, 0.42f, 0.64f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.18f, 0.48f, 0.70f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.34f, 0.36f, 0.39f, 0.45f);
}

} // namespace adventure::editor
