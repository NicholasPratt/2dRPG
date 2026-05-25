#pragma once

#include "imgui.h"

namespace adventure::editor::ui {

inline void label(const char* text, float width = 118.0f)
{
    const float startX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine();
    ImGui::SetCursorPosX(startX + width);
}

inline bool checkbox(const char* labelText, const char* id, bool* value, float labelWidth = 118.0f)
{
    label(labelText, labelWidth);
    return ImGui::Checkbox(id, value);
}

inline bool sliderInt(const char* labelText, const char* id, int* value, int min, int max, float itemWidth = 120.0f, float labelWidth = 118.0f)
{
    label(labelText, labelWidth);
    ImGui::SetNextItemWidth(itemWidth);
    return ImGui::SliderInt(id, value, min, max);
}

inline bool sliderFloat(const char* labelText, const char* id, float* value, float min, float max, const char* format = "%.3f",
    float itemWidth = 120.0f, float labelWidth = 118.0f)
{
    label(labelText, labelWidth);
    ImGui::SetNextItemWidth(itemWidth);
    return ImGui::SliderFloat(id, value, min, max, format);
}

} // namespace adventure::editor::ui
