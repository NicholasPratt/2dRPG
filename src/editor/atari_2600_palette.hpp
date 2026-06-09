#pragma once

#include "imgui.h"

#include <array>
#include <cstdio>
#include <cstdint>

namespace adventure::editor::atari2600 {

// Stella's standard NTSC palette (128 TIA colors), stored as 0xAABBGGRR.
// Source: stella-emu/stella, src/common/PaletteHandler.cxx.
inline constexpr std::array<std::uint32_t, 128> kNtscPalette{
    0xff000000u, 0xff4a4a4au, 0xff6f6f6fu, 0xff8e8e8eu, 0xffaaaaaau, 0xffc0c0c0u, 0xffd6d6d6u, 0xffecececu,
    0xff004848u, 0xff0f6969u, 0xff1d8686u, 0xff2aa2a2u, 0xff35bbbbu, 0xff40d2d2u, 0xff4ae8e8u, 0xff54fcfcu,
    0xff002c7cu, 0xff114890u, 0xff2162a2u, 0xff307ab4u, 0xff3d90c3u, 0xff4aa4d2u, 0xff55b7dfu, 0xff60c8ecu,
    0xff001c90u, 0xff1539a3u, 0xff2853b5u, 0xff3a6cc6u, 0xff4a82d5u, 0xff5997e3u, 0xff67aaf0u, 0xff74bcfcu,
    0xff000094u, 0xff1a1aa7u, 0xff3232b8u, 0xff4848c8u, 0xff5c5cd6u, 0xff6f6fe4u, 0xff8080f0u, 0xff9090fcu,
    0xff640084u, 0xff7a1997u, 0xff8f30a8u, 0xffa246b8u, 0xffb359c6u, 0xffc36cd4u, 0xffd27ce0u, 0xffe08cecu,
    0xff840050u, 0xff9a1968u, 0xffad307du, 0xffc04692u, 0xffd059a4u, 0xffe06cb5u, 0xffee7cc5u, 0xfffc8cd4u,
    0xff900014u, 0xffa31a33u, 0xffb5324eu, 0xffc64868u, 0xffd55c7fu, 0xffe36f95u, 0xfff080a9u, 0xfffc90bcu,
    0xff940000u, 0xffa71a18u, 0xffb8322du, 0xffc84842u, 0xffd65c54u, 0xffe46f65u, 0xfff08075u, 0xfffc9084u,
    0xff881c00u, 0xff9d3b18u, 0xffb0572du, 0xffc27242u, 0xffd28a54u, 0xffe1a065u, 0xffefb575u, 0xfffcc884u,
    0xff643000u, 0xff805018u, 0xff986d2du, 0xffb08842u, 0xffc5a054u, 0xffd9b765u, 0xffebcc75u, 0xfffce084u,
    0xff304000u, 0xff4e6218u, 0xff69812du, 0xff829e42u, 0xff99b854u, 0xffaed165u, 0xffc2e775u, 0xffd4fc84u,
    0xff004400u, 0xff1a661au, 0xff328432u, 0xff48a048u, 0xff5cba5cu, 0xff6fd26fu, 0xff80e880u, 0xff90fc90u,
    0xff003c14u, 0xff185f35u, 0xff2d7e52u, 0xff429c6eu, 0xff54b787u, 0xff65d09eu, 0xff75e7b4u, 0xff84fcc8u,
    0xff003830u, 0xff165950u, 0xff2b766du, 0xff3e9288u, 0xff4faba0u, 0xff5fc2b7u, 0xff6ed8ccu, 0xff7cece0u,
    0xff002c48u, 0xff144d69u, 0xff266a86u, 0xff3886a2u, 0xff479fbbu, 0xff56b6d2u, 0xff63cce8u, 0xff70e0fcu,
};

inline ImVec4 unpackColor(std::uint32_t color)
{
    return {
        static_cast<float>((color >> 0u) & 0xffu) / 255.0f,
        static_cast<float>((color >> 8u) & 0xffu) / 255.0f,
        static_cast<float>((color >> 16u) & 0xffu) / 255.0f,
        static_cast<float>((color >> 24u) & 0xffu) / 255.0f,
    };
}

inline bool drawNtscPaletteSelector(
    const char* id,
    std::uint32_t& selectedColor,
    bool includeTransparent = false,
    int columns = 8,
    float swatchSize = 22.0f)
{
    bool changed = false;
    int itemIndex = 0;
    ImGui::PushID(id);

    const auto drawSwatch = [&](std::uint32_t color, const char* tooltip) {
        ImGui::PushID(itemIndex);
        if (ImGui::ColorButton(
                "##AtariColor",
                unpackColor(color),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                ImVec2(swatchSize, swatchSize))) {
            selectedColor = color;
            changed = true;
        }
        if (selectedColor == color) {
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(),
                IM_COL32(255, 216, 64, 255),
                0.0f,
                0,
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ++itemIndex;
        if (itemIndex % columns != 0) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    };

    if (includeTransparent) {
        drawSwatch(0u, "Transparent");
        ImGui::NewLine();
        itemIndex = 0;
    }

    for (std::size_t index = 0; index < kNtscPalette.size(); ++index) {
        char tooltip[48]{};
        const unsigned int tiaCode = static_cast<unsigned int>(index * 2u);
        const std::uint32_t color = kNtscPalette[index];
        std::snprintf(
            tooltip,
            sizeof(tooltip),
            "TIA $%02X  #%02X%02X%02X",
            tiaCode,
            static_cast<unsigned int>((color >> 0u) & 0xffu),
            static_cast<unsigned int>((color >> 8u) & 0xffu),
            static_cast<unsigned int>((color >> 16u) & 0xffu));
        drawSwatch(color, tooltip);
    }

    ImGui::PopID();
    return changed;
}

} // namespace adventure::editor::atari2600
