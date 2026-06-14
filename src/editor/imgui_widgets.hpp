#pragma once

#include "imgui.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace adventure::editor::ui {

inline std::string stringFieldLabel(const char* label)
{
    const char* source = label == nullptr ? "" : label;
    const char* idPart = std::strstr(source, "##");
    if (idPart != nullptr) {
        if (idPart == source) {
            return source;
        }
        std::string result = "$ ";
        result.append(source, static_cast<std::size_t>(idPart - source));
        result += idPart;
        return result;
    }
    std::string result = "$ ";
    result += source;
    result += "##";
    result += source;
    return result;
}

inline bool inputTextString(const char* label, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags = 0)
{
    const std::string displayLabel = stringFieldLabel(label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.13f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.11f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.13f, 0.25f, 0.30f, 1.0f));
    const bool changed = ImGui::InputText(displayLabel.c_str(), buffer, bufferSize, flags);
    ImGui::PopStyleColor(3);
    return changed;
}

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
