#include "editor/panels/sprite_editor_panel.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "imgui.h"

namespace adventure::editor {
namespace {

constexpr const char* kToolNames[] = {
    "Pen", "Mirror", "Bucket", "Eraser", "Stroke", "Line",
    "Rect", "Circle", "Move", "Select", "Picker", "Shade",
};

void ensureDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
}

ImU32 packedColor(unsigned int color)
{
    return IM_COL32((color >> 0) & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, (color >> 24) & 0xff);
}

void checkerboard(ImDrawList* drawList, ImVec2 min, ImVec2 max, float cellSize)
{
    const ImU32 dark = IM_COL32(73, 77, 82, 255);
    const ImU32 light = IM_COL32(92, 97, 103, 255);
    for (float y = min.y; y < max.y; y += cellSize) {
        for (float x = min.x; x < max.x; x += cellSize) {
            const bool even = (static_cast<int>((x - min.x) / cellSize) + static_cast<int>((y - min.y) / cellSize)) % 2 == 0;
            drawList->AddRectFilled({x, y}, {std::min(x + cellSize, max.x), std::min(y + cellSize, max.y)}, even ? dark : light);
        }
    }
}

void sectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::TextUnformatted(label);
    ImGui::Separator();
}

} // namespace

void SpriteEditorPanel::draw(EditorContext& context)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

    drawTopBar();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = 220.0f;
    const float rightWidth = 280.0f;
    const float centerWidth = std::max(240.0f, available.x - leftWidth - rightWidth - 12.0f);

    ImGui::BeginChild("SpriteLeftRail", ImVec2(leftWidth, 0.0f), true);
    drawLeftRail();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SpriteCanvasWorkspace", ImVec2(centerWidth, 0.0f), true);
    drawCenterWorkspace();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SpriteRightInspector", ImVec2(rightWidth, 0.0f), true);
    drawRightInspector(context);
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
}

void SpriteEditorPanel::drawTopBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 31, 35, 255));
    ImGui::BeginChild("SpriteTopBar", ImVec2(0.0f, 42.0f), false);
    ImGui::SetCursorPos(ImVec2(12.0f, 10.0f));
    ImGui::Text("Sprite: %s", document_.id.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%dx%d]", document_.canvasSize[0], document_.canvasSize[1]);
    ImGui::SameLine(ImGui::GetWindowWidth() - 360.0f);
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &onionSkin_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Zoom", &zoom_, 2, 32);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void SpriteEditorPanel::drawLeftRail()
{
    sectionHeader("Frames");
    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = selectedFrame_ == i;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(230, 199, 34, 255) : IM_COL32(47, 51, 56, 255));
        if (ImGui::Button(("##FrameThumb" + std::to_string(i)).c_str(), ImVec2(86.0f, 86.0f))) {
            selectedFrame_ = i;
        }
        ImGui::PopStyleColor();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        checkerboard(drawList, {min.x + 5.0f, min.y + 5.0f}, {max.x - 5.0f, max.y - 18.0f}, 8.0f);
        drawSpritePixels(drawList, {min.x + 28.0f, min.y + 12.0f}, 2.0f);
        drawList->AddText({min.x + 8.0f, max.y - 16.0f}, IM_COL32(235, 238, 242, 255), std::to_string(i + 1).c_str());
        ImGui::PopID();
    }

    if (ImGui::Button("+ Add frame", ImVec2(-1.0f, 36.0f))) {
        document_.frames.push_back(document_.frames.empty() ? SpriteFrame{} : document_.frames.back());
        selectedFrame_ = static_cast<int>(document_.frames.size()) - 1;
    }

    sectionHeader("Tools");
    const char* toolLabels[] = {"P", "M", "B", "E", "S", "/", "R", "O", "V", "[]", "I", "+/-"};
    for (int i = 0; i < static_cast<int>(std::size(toolLabels)); ++i) {
        drawToolButton(toolLabels[i], kToolNames[i], i);
        if (i % 4 != 3) {
            ImGui::SameLine();
        }
    }

    sectionHeader("Colors");
    const float swatch = 34.0f;
    for (int i = 0; i < static_cast<int>(document_.palette.size()); ++i) {
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, packedColor(document_.palette[i]));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, packedColor(document_.palette[i]));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, packedColor(document_.palette[i]));
        if (ImGui::Button("##Swatch", ImVec2(swatch, swatch))) {
            primaryColor_ = i;
        }
        ImGui::PopStyleColor(3);
        if (primaryColor_ == i) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 224, 64, 255), 0.0f, 0, 3.0f);
        }
        if (i % 4 != 3) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    ImGui::Text("Primary %d  Secondary %d", primaryColor_ + 1, secondaryColor_ + 1);
}

void SpriteEditorPanel::drawToolButton(const char* label, const char* tooltip, int toolIndex)
{
    const bool selected = selectedTool_ == toolIndex;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(236, 203, 49, 255) : IM_COL32(56, 61, 67, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? IM_COL32(24, 25, 28, 255) : IM_COL32(240, 242, 245, 255));
    if (ImGui::Button(label, ImVec2(42.0f, 38.0f))) {
        selectedTool_ = toolIndex;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

void SpriteEditorPanel::drawCenterWorkspace()
{
    ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
    ImGui::InputInt2("Canvas", document_.canvasSize.data());
    ImGui::SameLine();
    ImGui::InputInt2("Grid", document_.gridSize.data());
    ImGui::SameLine();
    ImGui::InputInt2("Pivot", document_.pivot.data());

    ImGui::Separator();
    drawCanvas(ImGui::GetContentRegionAvail());
}

void SpriteEditorPanel::drawCanvas(const ImVec2& availableSize)
{
    const float pixelSize = static_cast<float>(zoom_);
    const ImVec2 pixelCanvasSize{
        static_cast<float>(document_.canvasSize[0]) * pixelSize,
        static_cast<float>(document_.canvasSize[1]) * pixelSize,
    };
    const ImVec2 paddedCanvasSize{
        std::max(pixelCanvasSize.x + 96.0f, availableSize.x),
        std::max(pixelCanvasSize.y + 96.0f, availableSize.y),
    };

    const ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("CanvasArea", paddedCanvasSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(childOrigin, {childOrigin.x + paddedCanvasSize.x, childOrigin.y + paddedCanvasSize.y}, IM_COL32(43, 46, 50, 255));

    const ImVec2 canvasOrigin{
        childOrigin.x + std::max(48.0f, (paddedCanvasSize.x - pixelCanvasSize.x) * 0.5f),
        childOrigin.y + std::max(48.0f, (paddedCanvasSize.y - pixelCanvasSize.y) * 0.5f),
    };
    const ImVec2 canvasSize{
        static_cast<float>(document_.canvasSize[0]) * pixelSize,
        static_cast<float>(document_.canvasSize[1]) * pixelSize,
    };
    const ImVec2 canvasMax{canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y};

    drawList->AddRectFilled({canvasOrigin.x - 18.0f, canvasOrigin.y - 18.0f}, {canvasMax.x + 18.0f, canvasMax.y + 18.0f}, IM_COL32(166, 168, 170, 255));
    checkerboard(drawList, canvasOrigin, canvasMax, pixelSize);
    drawSpritePixels(drawList, canvasOrigin, pixelSize);
    drawList->AddRect(canvasOrigin, canvasMax, IM_COL32(155, 166, 178, 255));

    if (showGrid_) {
        for (int x = 0; x <= document_.canvasSize[0]; ++x) {
            const float sx = canvasOrigin.x + static_cast<float>(x) * pixelSize;
            drawList->AddLine({sx, canvasOrigin.y}, {sx, canvasOrigin.y + canvasSize.y}, IM_COL32(68, 74, 82, 255));
        }
        for (int y = 0; y <= document_.canvasSize[1]; ++y) {
            const float sy = canvasOrigin.y + static_cast<float>(y) * pixelSize;
            drawList->AddLine({canvasOrigin.x, sy}, {canvasOrigin.x + canvasSize.x, sy}, IM_COL32(68, 74, 82, 255));
        }
    }

    const ImVec2 pivot{
        canvasOrigin.x + static_cast<float>(document_.pivot[0]) * pixelSize,
        canvasOrigin.y + static_cast<float>(document_.pivot[1]) * pixelSize,
    };
    drawList->AddLine({pivot.x - 5.0f, pivot.y}, {pivot.x + 5.0f, pivot.y}, IM_COL32(255, 215, 0, 255), 2.0f);
    drawList->AddLine({pivot.x, pivot.y - 5.0f}, {pivot.x, pivot.y + 5.0f}, IM_COL32(255, 215, 0, 255), 2.0f);

}

void SpriteEditorPanel::drawSpritePixels(ImDrawList* drawList, ImVec2 origin, float pixelSize) const
{
    const auto pixel = [&](int x, int y, ImU32 color) {
        drawList->AddRectFilled(
            {origin.x + static_cast<float>(x) * pixelSize, origin.y + static_cast<float>(y) * pixelSize},
            {origin.x + static_cast<float>(x + 1) * pixelSize, origin.y + static_cast<float>(y + 1) * pixelSize},
            color);
    };

    const ImU32 leafDark = IM_COL32(14, 65, 9, 255);
    const ImU32 leafMid = IM_COL32(30, 111, 24, 255);
    const ImU32 leafBright = IM_COL32(80, 210, 45, 255);
    const ImU32 trunk = IM_COL32(128, 55, 12, 255);
    const ImU32 flower = IM_COL32(230, 42, 48, 255);
    const ImU32 spark = IM_COL32(215, 228, 30, 255);

    for (int y = 2; y < 20; ++y) {
        const int left = std::max(0, 11 - y / 2);
        const int right = std::min(31, 20 + y / 3);
        for (int x = left; x <= right; ++x) {
            if ((x + y) % 11 != 0) {
                pixel(x, y, leafDark);
            }
        }
    }
    for (int y = 8; y < 22; ++y) {
        for (int x = 2; x < 29; ++x) {
            if (x > y - 8 && x < 34 - y && (x + y) % 5 != 0) {
                pixel(x, y, leafDark);
            }
        }
    }
    for (int y = 9; y < 22; ++y) {
        pixel(4, y, leafMid);
        pixel(5, y + 1, leafMid);
    }
    for (int x = 13; x <= 18; ++x) {
        for (int y = 22; y < 31; ++y) {
            pixel(x, y, trunk);
        }
    }
    for (int x = 0; x < 32; ++x) {
        pixel(x, 31, (x % 8 == 3) ? flower : leafBright);
        if (x % 6 == 0) {
            pixel(x, 30, leafMid);
        }
    }
    pixel(10, 6, spark);
    pixel(19, 5, spark);
    pixel(25, 6, spark);
    pixel(11, 12, spark);
    pixel(20, 14, spark);
    pixel(24, 16, spark);
    pixel(12, 18, spark);
}

void SpriteEditorPanel::drawRightInspector(EditorContext& context)
{
    sectionHeader("Preview");
    drawPreview(ImVec2(-1.0f, 178.0f));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("FPS", &playbackFps_, 1, 30);

    sectionHeader("Layers");
    drawLayers();

    sectionHeader("Transform");
    if (ImGui::Button("Flip H", ImVec2(62.0f, 34.0f))) {}
    ImGui::SameLine();
    if (ImGui::Button("Flip V", ImVec2(62.0f, 34.0f))) {}
    ImGui::SameLine();
    if (ImGui::Button("Rotate", ImVec2(72.0f, 34.0f))) {}
    ImGui::InputInt2("Pivot", document_.pivot.data());

    sectionHeader("Palette");
    drawPalette(context);

    sectionHeader("Export");
    drawExport(context);
}

void SpriteEditorPanel::drawPreview(const ImVec2& availableSize)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = availableSize.y;
    ImGui::InvisibleButton("PreviewCanvas", ImVec2(width, height));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 max{origin.x + width, origin.y + height};
    checkerboard(drawList, origin, max, 10.0f);
    drawSpritePixels(drawList, {origin.x + width * 0.5f - 64.0f, origin.y + 14.0f}, 4.0f);
    drawList->AddRect(origin, max, IM_COL32(118, 126, 135, 255));
}

void SpriteEditorPanel::drawFrames()
{
    if (ImGui::Button("Add frame")) {
        document_.frames.push_back(document_.frames.empty() ? SpriteFrame{} : document_.frames.back());
        selectedFrame_ = static_cast<int>(document_.frames.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate frame") && !document_.frames.empty()) {
        document_.frames.insert(document_.frames.begin() + selectedFrame_ + 1, document_.frames[selectedFrame_]);
        ++selectedFrame_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete frame") && document_.frames.size() > 1) {
        document_.frames.erase(document_.frames.begin() + selectedFrame_);
        selectedFrame_ = std::clamp(selectedFrame_, 0, static_cast<int>(document_.frames.size()) - 1);
    }

    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(("Frame " + std::to_string(i + 1)).c_str(), selectedFrame_ == i)) {
            selectedFrame_ = i;
        }
        if (selectedFrame_ == i) {
            ImGui::InputInt("X", &document_.frames[i].x);
            ImGui::InputInt("Y", &document_.frames[i].y);
            ImGui::InputInt("Width", &document_.frames[i].width);
            ImGui::InputInt("Height", &document_.frames[i].height);
            ImGui::InputInt("Duration ms", &document_.frames[i].durationMs);
        }
        ImGui::PopID();
    }
}

void SpriteEditorPanel::drawLayers()
{
    if (ImGui::Button("Add layer")) {
        document_.layers.push_back({"Layer " + std::to_string(document_.layers.size() + 1), true, 1.0f});
        selectedLayer_ = static_cast<int>(document_.layers.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete layer") && document_.layers.size() > 1) {
        document_.layers.erase(document_.layers.begin() + selectedLayer_);
        selectedLayer_ = std::clamp(selectedLayer_, 0, static_cast<int>(document_.layers.size()) - 1);
    }

    for (int i = 0; i < static_cast<int>(document_.layers.size()); ++i) {
        ImGui::PushID(i);
        ImGui::Checkbox("Visible", &document_.layers[i].visible);
        ImGui::SameLine();
        if (ImGui::Selectable(document_.layers[i].name.c_str(), selectedLayer_ == i)) {
            selectedLayer_ = i;
        }
        ImGui::SliderFloat("Opacity", &document_.layers[i].opacity, 0.0f, 1.0f);
        ImGui::PopID();
    }
}

void SpriteEditorPanel::drawPalette(EditorContext& context)
{
    ImGui::Text("Palette output: %s", context.assets.gamePalettePath().string().c_str());

    for (int i = 0; i < static_cast<int>(document_.palette.size()); ++i) {
        ImGui::PushID(i);
        const unsigned int color = document_.palette[i];
        float rgba[4] = {
            static_cast<float>((color >> 0) & 0xff) / 255.0f,
            static_cast<float>((color >> 8) & 0xff) / 255.0f,
            static_cast<float>((color >> 16) & 0xff) / 255.0f,
            static_cast<float>((color >> 24) & 0xff) / 255.0f,
        };
        if (ImGui::ColorEdit4("##Color", rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            document_.palette[i] =
                (static_cast<unsigned int>(rgba[3] * 255.0f) << 24) |
                (static_cast<unsigned int>(rgba[2] * 255.0f) << 16) |
                (static_cast<unsigned int>(rgba[1] * 255.0f) << 8) |
                (static_cast<unsigned int>(rgba[0] * 255.0f) << 0);
        }
        if ((i + 1) % 5 != 0) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Add color")) {
        document_.palette.push_back(0xffffffffu);
    }
}

void SpriteEditorPanel::drawExport(EditorContext& context)
{
    char idBuffer[128]{};
    std::copy_n(document_.id.c_str(), std::min(document_.id.size(), sizeof(idBuffer) - 1), idBuffer);
    if (ImGui::InputText("Sprite id", idBuffer, sizeof(idBuffer))) {
        document_.id = idBuffer;
    }

    char sourceBuffer[256]{};
    const std::string source = document_.sourcePng.generic_string();
    std::copy_n(source.c_str(), std::min(source.size(), sizeof(sourceBuffer) - 1), sourceBuffer);
    if (ImGui::InputText("Source PNG", sourceBuffer, sizeof(sourceBuffer))) {
        document_.sourcePng = sourceBuffer;
    }

    ImGui::Text("Import PNGs from: %s", context.assets.rawSpritePath().string().c_str());
    ImGui::Text("Save metadata to: %s", context.assets.gameSpritePath().string().c_str());
    ImGui::Text("Animation clips to: %s", context.assets.gameAnimationPath().string().c_str());

    if (ImGui::Button("Save .sprite.json")) {
        saveSpriteMetadata(context);
    }
}

void SpriteEditorPanel::saveSpriteMetadata(const EditorContext& context) const
{
    ensureDirectory(context.assets.gameSpritePath());
    const std::filesystem::path outputPath = context.assets.gameSpritePath() / (document_.id + ".sprite.json");
    std::ofstream output(outputPath);
    if (!output) {
        return;
    }

    output << "{\n";
    output << "  \"id\": \"" << document_.id << "\",\n";
    output << "  \"source\": \"" << document_.sourcePng.generic_string() << "\",\n";
    output << "  \"canvasSize\": [" << document_.canvasSize[0] << ", " << document_.canvasSize[1] << "],\n";
    output << "  \"gridSize\": [" << document_.gridSize[0] << ", " << document_.gridSize[1] << "],\n";
    output << "  \"pivot\": [" << document_.pivot[0] << ", " << document_.pivot[1] << "],\n";
    output << "  \"frames\": [\n";
    for (std::size_t i = 0; i < document_.frames.size(); ++i) {
        const SpriteFrame& frame = document_.frames[i];
        output << "    {\"rect\": [" << frame.x << ", " << frame.y << ", " << frame.width << ", " << frame.height
               << "], \"durationMs\": " << frame.durationMs << "}";
        output << (i + 1 == document_.frames.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"tags\": [";
    for (std::size_t i = 0; i < document_.tags.size(); ++i) {
        output << "\"" << document_.tags[i] << "\"" << (i + 1 == document_.tags.size() ? "" : ", ");
    }
    output << "]\n";
    output << "}\n";
}

} // namespace adventure::editor
