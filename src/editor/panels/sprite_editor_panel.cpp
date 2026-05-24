#include "editor/panels/sprite_editor_panel.hpp"

#include "game/sprite.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "imgui.h"

namespace adventure::editor {
namespace {

enum ToolIndex {
    kToolPen = 0,
    kToolMirror,
    kToolBucket,
    kToolEraser,
    kToolStroke,
    kToolLine,
    kToolRect,
    kToolCircle,
    kToolMove,
    kToolSelect,
    kToolPicker,
    kToolShade,
};

constexpr const char* kToolNames[] = {
    "Pen", "Mirror", "Bucket", "Eraser", "Stroke", "Line",
    "Rect", "Circle", "Move", "Select", "Picker", "Shade",
};

constexpr int kMinCanvasSize = 1;
constexpr int kMaxCanvasSize = 256;
constexpr unsigned char kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

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

std::filesystem::path projectRootFromSpriteMetadataPath(const std::filesystem::path& metadataPath)
{
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(metadataPath, error);
    if (absolute.empty()) {
        absolute = metadataPath;
    }

    std::filesystem::path current = absolute.parent_path();
    for (int i = 0; i < 5 && !current.empty(); ++i) {
        if (std::filesystem::exists(current / "assets", error)) {
            return current;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path(error);
}

std::filesystem::path resolveAgainstProjectRoot(const std::filesystem::path& projectRoot, const std::filesystem::path& path)
{
    return path.is_absolute() ? path : projectRoot / path;
}

std::filesystem::path portableProjectPath(const std::filesystem::path& projectRoot, const std::filesystem::path& path)
{
    if (path.empty() || !path.is_absolute()) {
        return path;
    }

    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, error);
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    const std::filesystem::path relative = std::filesystem::relative(canonicalPath, canonicalRoot, error);
    if (!error && !relative.empty() && relative.native().find("..") != 0) {
        return relative;
    }
    return path;
}

int clampDimension(int value)
{
    return std::clamp(value, kMinCanvasSize, kMaxCanvasSize);
}

void appendBigEndian32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<unsigned char>(value & 0xffu));
}

std::uint32_t readBigEndian32(const unsigned char* bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}

std::uint32_t crc32(const unsigned char* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1u));
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::uint32_t adler32(const unsigned char* data, std::size_t size)
{
    constexpr std::uint32_t kMod = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

void appendChunk(std::vector<unsigned char>& png, const char* type, const std::vector<unsigned char>& data)
{
    appendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = png.size();
    png.push_back(static_cast<unsigned char>(type[0]));
    png.push_back(static_cast<unsigned char>(type[1]));
    png.push_back(static_cast<unsigned char>(type[2]));
    png.push_back(static_cast<unsigned char>(type[3]));
    png.insert(png.end(), data.begin(), data.end());

    const std::uint32_t chunkCrc = crc32(png.data() + typeOffset, 4 + data.size());
    appendBigEndian32(png, chunkCrc);
}

bool writePngRgba(const std::filesystem::path& path, int width, int height, const std::vector<unsigned char>& rgba)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0u);
        const std::size_t rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(rowOffset),
            rgba.begin() + static_cast<std::ptrdiff_t>(rowOffset + static_cast<std::size_t>(width) * 4u));
    }

    std::vector<unsigned char> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535u * 5u + 16u);
    zlib.push_back(0x78u);
    zlib.push_back(0x01u);

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t remaining = raw.size() - offset;
        const std::uint16_t blockSize = static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535u));
        const bool finalBlock = offset + blockSize == raw.size();
        zlib.push_back(finalBlock ? 0x01u : 0x00u);
        zlib.push_back(static_cast<unsigned char>(blockSize & 0xffu));
        zlib.push_back(static_cast<unsigned char>((blockSize >> 8) & 0xffu));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~blockSize);
        zlib.push_back(static_cast<unsigned char>(nlen & 0xffu));
        zlib.push_back(static_cast<unsigned char>((nlen >> 8) & 0xffu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }

    appendBigEndian32(zlib, adler32(raw.data(), raw.size()));

    std::vector<unsigned char> ihdr;
    appendBigEndian32(ihdr, static_cast<std::uint32_t>(width));
    appendBigEndian32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8u);
    ihdr.push_back(6u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);

    std::vector<unsigned char> png(kPngSignature, kPngSignature + std::size(kPngSignature));
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(output);
}

bool inflateStoredZlib(const std::vector<unsigned char>& compressed, std::vector<unsigned char>& output)
{
    if (compressed.size() < 6 || compressed[0] != 0x78u) {
        return false;
    }

    std::size_t offset = 2;
    const std::size_t dataEnd = compressed.size() - 4;
    while (offset < dataEnd) {
        const unsigned char blockHeader = compressed[offset++];
        const bool finalBlock = (blockHeader & 0x01u) != 0u;
        const unsigned char blockType = (blockHeader >> 1) & 0x03u;
        if (blockType != 0u || offset + 4 > dataEnd) {
            return false;
        }

        const std::uint16_t length = static_cast<std::uint16_t>(compressed[offset] | (compressed[offset + 1] << 8));
        const std::uint16_t inverseLength = static_cast<std::uint16_t>(compressed[offset + 2] | (compressed[offset + 3] << 8));
        offset += 4;
        if (static_cast<std::uint16_t>(~length) != inverseLength || offset + length > dataEnd) {
            return false;
        }

        output.insert(output.end(), compressed.begin() + static_cast<std::ptrdiff_t>(offset), compressed.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        if (finalBlock) {
            return true;
        }
    }

    return false;
}

bool readEditorPngRgba(const std::filesystem::path& path, int& width, int& height, std::vector<unsigned char>& rgba)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    const std::vector<unsigned char> png(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (png.size() < std::size(kPngSignature) || std::memcmp(png.data(), kPngSignature, std::size(kPngSignature)) != 0) {
        return false;
    }

    std::vector<unsigned char> idat;
    std::size_t offset = std::size(kPngSignature);
    bool foundIhdr = false;
    while (offset + 12 <= png.size()) {
        const std::uint32_t chunkSize = readBigEndian32(png.data() + offset);
        offset += 4;
        if (offset + 4 + chunkSize + 4 > png.size()) {
            return false;
        }

        const unsigned char* type = png.data() + offset;
        offset += 4;
        const unsigned char* data = png.data() + offset;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (chunkSize != 13u || data[8] != 8u || data[9] != 6u || data[10] != 0u || data[11] != 0u || data[12] != 0u) {
                return false;
            }
            width = static_cast<int>(readBigEndian32(data));
            height = static_cast<int>(readBigEndian32(data + 4));
            foundIhdr = width > 0 && height > 0;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + chunkSize);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }

        offset += chunkSize + 4;
    }

    if (!foundIhdr || idat.empty()) {
        return false;
    }

    std::vector<unsigned char> raw;
    if (!inflateStoredZlib(idat, raw)) {
        return false;
    }

    const std::size_t expected = static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u);
    if (raw.size() != expected) {
        return false;
    }

    rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    for (int y = 0; y < height; ++y) {
        const std::size_t rawRow = static_cast<std::size_t>(y) * (1u + static_cast<std::size_t>(width) * 4u);
        if (raw[rawRow] != 0u) {
            return false;
        }
        std::copy(
            raw.begin() + static_cast<std::ptrdiff_t>(rawRow + 1u),
            raw.begin() + static_cast<std::ptrdiff_t>(rawRow + 1u + static_cast<std::size_t>(width) * 4u),
            rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u));
    }

    return true;
}

ImVec2 rectCenter(const ImVec2& min, const ImVec2& max)
{
    return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
}

void drawToolGlyph(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int toolIndex, ImU32 color)
{
    const ImVec2 center = rectCenter(min, max);
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float unit = std::max(1.0f, std::min(width, height) / 18.0f);
    const float thin = std::max(1.5f, unit * 1.7f);
    const float thick = std::max(2.0f, unit * 2.4f);
    const float left = min.x + unit * 3.0f;
    const float right = max.x - unit * 3.0f;
    const float top = min.y + unit * 3.0f;
    const float bottom = max.y - unit * 3.0f;

    switch (toolIndex) {
    case kToolPen:
        drawList->AddLine({left + unit, bottom - unit}, {right - unit * 3.0f, top + unit * 4.0f}, thick, color);
        drawList->AddTriangleFilled(
            {right - unit * 3.8f, top + unit * 4.2f},
            {right - unit * 0.6f, top + unit * 2.4f},
            {right - unit * 2.2f, top + unit * 6.0f},
            color);
        drawList->AddRectFilled({left - unit * 0.3f, bottom - unit * 2.0f}, {left + unit * 2.0f, bottom + unit * 0.3f}, color, unit * 0.4f);
        break;
    case kToolMirror:
        drawList->AddLine({center.x, top}, {center.x, bottom}, thin, color);
        drawList->AddLine({left + unit, bottom - unit}, {center.x - unit, top + unit * 3.0f}, thick, color);
        drawList->AddTriangleFilled(
            {center.x + unit, top + unit * 3.2f},
            {right - unit * 2.2f, top + unit * 5.2f},
            {right - unit * 0.6f, top + unit * 1.8f},
            color);
        drawList->AddCircleFilled({left + unit * 3.0f, bottom - unit * 2.0f}, unit * 1.2f, color);
        drawList->AddCircleFilled({right - unit * 3.0f, bottom - unit * 2.0f}, unit * 1.2f, color);
        break;
    case kToolBucket:
        drawList->AddQuadFilled(
            {left + unit * 2.0f, top + unit * 5.0f},
            {center.x + unit * 1.5f, top + unit * 1.8f},
            {right - unit * 1.8f, center.y + unit * 1.6f},
            {center.x, bottom - unit * 2.0f},
            color);
        drawList->AddCircleFilled({right - unit * 2.3f, bottom - unit * 3.0f}, unit * 1.6f, color);
        drawList->AddCircleFilled({right - unit * 2.3f, bottom - unit * 0.8f}, unit * 0.8f, color);
        break;
    case kToolEraser:
        drawList->AddQuadFilled(
            {left + unit * 1.2f, bottom - unit * 2.8f},
            {center.x, top + unit * 2.0f},
            {right - unit * 1.2f, center.y + unit * 0.8f},
            {center.x + unit * 0.2f, bottom - unit * 0.4f},
            color);
        drawList->AddLine({left + unit * 3.0f, bottom - unit * 1.0f}, {right - unit * 2.5f, top + unit * 3.2f}, thin, IM_COL32(43, 46, 50, 180));
        break;
    case kToolStroke:
        drawList->AddCircleFilled({left + unit * 2.6f, center.y}, unit * 1.0f, color);
        drawList->AddCircleFilled({right - unit * 2.6f, center.y}, unit * 1.0f, color);
        drawList->AddLine({left + unit * 4.6f, center.y}, {right - unit * 4.6f, center.y}, thick, color);
        break;
    case kToolLine:
        drawList->AddCircleFilled({left + unit * 1.8f, bottom - unit * 2.0f}, unit * 1.0f, color);
        drawList->AddCircleFilled({right - unit * 1.8f, top + unit * 2.0f}, unit * 1.0f, color);
        drawList->AddLine({left + unit * 3.3f, bottom - unit * 3.1f}, {right - unit * 3.3f, top + unit * 3.1f}, thick, color);
        break;
    case kToolRect:
        drawList->AddRect({left + unit, top + unit}, {right - unit, bottom - unit}, color, unit * 0.6f, 0, thick);
        break;
    case kToolCircle:
        drawList->AddCircle(center, std::min(width, height) * 0.28f, color, 0, thick);
        break;
    case kToolMove:
        drawList->AddLine({center.x, top + unit}, {center.x, bottom - unit}, thick, color);
        drawList->AddLine({left + unit, center.y}, {right - unit, center.y}, thick, color);
        drawList->AddTriangleFilled({center.x, top}, {center.x - unit * 1.8f, top + unit * 2.6f}, {center.x + unit * 1.8f, top + unit * 2.6f}, color);
        drawList->AddTriangleFilled({center.x, bottom}, {center.x - unit * 1.8f, bottom - unit * 2.6f}, {center.x + unit * 1.8f, bottom - unit * 2.6f}, color);
        drawList->AddTriangleFilled({left, center.y}, {left + unit * 2.6f, center.y - unit * 1.8f}, {left + unit * 2.6f, center.y + unit * 1.8f}, color);
        drawList->AddTriangleFilled({right, center.y}, {right - unit * 2.6f, center.y - unit * 1.8f}, {right - unit * 2.6f, center.y + unit * 1.8f}, color);
        break;
    case kToolSelect:
        drawList->AddRect({left + unit, top + unit}, {right - unit, bottom - unit}, color, 0.0f, 0, thin);
        drawList->AddRectFilled({left, top}, {left + unit * 2.0f, top + unit * 2.0f}, color);
        drawList->AddRectFilled({right - unit * 2.0f, top}, {right, top + unit * 2.0f}, color);
        drawList->AddRectFilled({left, bottom - unit * 2.0f}, {left + unit * 2.0f, bottom}, color);
        drawList->AddRectFilled({right - unit * 2.0f, bottom - unit * 2.0f}, {right, bottom}, color);
        break;
    case kToolPicker:
        drawList->AddCircleFilled({left + unit * 3.0f, bottom - unit * 2.6f}, unit * 1.6f, color);
        drawList->AddLine({left + unit * 4.0f, bottom - unit * 3.6f}, {right - unit * 2.0f, top + unit * 2.2f}, thick, color);
        drawList->AddCircle({right - unit * 2.2f, top + unit * 2.2f}, unit * 1.0f, color, 0, thin);
        break;
    case kToolShade:
        drawList->AddTriangleFilled({center.x, top}, {left + unit * 1.8f, bottom - unit}, {right - unit * 1.8f, bottom - unit}, color);
        drawList->AddLine({left + unit * 3.6f, bottom - unit * 3.2f}, {right - unit * 3.6f, bottom - unit * 3.2f}, thin, IM_COL32(43, 46, 50, 180));
        drawList->AddLine({left + unit * 4.6f, bottom - unit * 5.2f}, {right - unit * 4.6f, bottom - unit * 5.2f}, thin, IM_COL32(43, 46, 50, 180));
        break;
    default:
        break;
    }
}

} // namespace

void SpriteEditorPanel::draw(EditorContext& context)
{
    ensureDocumentState();
    handleShortcuts();

    if (runAnimationPreview_ && document_.frames.size() > 1) {
        previewTimeSeconds_ += ImGui::GetIO().DeltaTime;
    } else {
        previewTimeSeconds_ = 0.0f;
    }

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

    if (documentDirty_) {
        context.markDirty();
    }
}

void SpriteEditorPanel::openSpriteReference(const std::filesystem::path& spriteReference)
{
    if (spriteReference.empty()) {
        return;
    }

    const std::filesystem::path filename = spriteReference.filename();
    std::string newId = filename.string();
    constexpr const char* kSpriteMetadataSuffix = ".sprite.json";
    if (newId.size() > std::char_traits<char>::length(kSpriteMetadataSuffix) &&
        newId.compare(newId.size() - std::char_traits<char>::length(kSpriteMetadataSuffix), std::char_traits<char>::length(kSpriteMetadataSuffix), kSpriteMetadataSuffix) == 0) {
        newId.erase(newId.size() - std::char_traits<char>::length(kSpriteMetadataSuffix));
    } else if (filename.has_stem()) {
        newId = filename.stem().string();
    }

    if (newId.empty()) {
        return;
    }

    // Stash current document when switching to a different sprite
    if (!document_.id.empty() && document_.id != newId) {
        auto& buf = documentBuffers_[document_.id];
        buf.document = document_;
        buf.dirty = buf.dirty || documentDirty_;
        documentDirty_ = false;
        undoStack_.clear();
    }

    // Restore from in-memory buffer if available
    auto it = documentBuffers_.find(newId);
    if (it != documentBuffers_.end()) {
        document_ = it->second.document;
        documentDirty_ = it->second.dirty;
        if (spriteReference.extension() == ".png") {
            document_.sourcePng = spriteReference;
        }
        return;
    }

    if (spriteReference.extension() == ".json" && loadDocumentFromMetadata(spriteReference)) {
        documentDirty_ = false;
        undoStack_.clear();
        selectedFrame_ = 0;
        selectedLayer_ = 0;
        return;
    }

    // New sprite: set ID and sourcePng (existing behaviour)
    document_.id = newId;
    if (spriteReference.extension() == ".png") {
        document_.sourcePng = spriteReference;
    }
}

void SpriteEditorPanel::openCharacterSpriteReference(const std::filesystem::path& spriteReference)
{
    openSpriteReference(spriteReference);

    const bool blankDocument = std::all_of(document_.cels.begin(), document_.cels.end(), [](const std::vector<SpriteCel>& frame) {
        return std::all_of(frame.begin(), frame.end(), [](const SpriteCel& cel) {
            return std::all_of(cel.pixels.begin(), cel.pixels.end(), [](unsigned int color) {
                return color == 0u;
            });
        });
    });

    newSpriteSize_ = {game::kTileSize, game::kTileSize};
    resizeSpriteSize_ = {game::kTileSize, game::kTileSize};
    if (blankDocument && document_.canvasSize != std::array<int, 2>{game::kTileSize, game::kTileSize}) {
        createBlankSprite(game::kTileSize, game::kTileSize);
        openSpriteReference(spriteReference);
    }
}

std::filesystem::path SpriteEditorPanel::spriteMetadataReference(const EditorContext& context) const
{
    return context.assets.gameSprites / (document_.id + ".sprite.json");
}

bool SpriteEditorPanel::saveForChapter(const EditorContext& context)
{
    // Flush current document into the buffer map
    if (!document_.id.empty()) {
        auto& buf = documentBuffers_[document_.id];
        buf.document = document_;
        buf.dirty = buf.dirty || documentDirty_;
    }

    // Save every dirty document by temporarily making it the active document
    for (auto& [id, buf] : documentBuffers_) {
        if (!buf.dirty) {
            continue;
        }
        SpriteDocument live = std::move(document_);
        document_ = buf.document;
        exportFramePngs(context);
        exportSpriteSheetPng(context);
        saveSpriteMetadata(context);
        buf.document = document_;  // capture updated sourcePng
        buf.dirty = false;
        document_ = std::move(live);
    }

    // Re-sync live document's sourcePng from its buffer (updated by exportSpriteSheetPng above)
    if (!document_.id.empty()) {
        auto it = documentBuffers_.find(document_.id);
        if (it != documentBuffers_.end()) {
            document_.sourcePng = it->second.document.sourcePng;
        }
    }

    documentDirty_ = false;
    return !document_.id.empty();
}

void SpriteEditorPanel::resetDocumentBuffers()
{
    documentBuffers_.clear();
    document_ = SpriteDocument{};
    documentDirty_ = false;
    undoStack_.clear();
    selectedFrame_ = 0;
    selectedLayer_ = 0;
    trackedCanvasSize_ = {game::kTileSize, game::kTileSize};
}

void SpriteEditorPanel::drawTopBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 31, 35, 255));
    ImGui::BeginChild("SpriteTopBar", ImVec2(0.0f, 42.0f), false);
    ImGui::SetCursorPos(ImVec2(12.0f, 10.0f));
    ImGui::Text("Sprite: %s", document_.id.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%dx%d]", document_.canvasSize[0], document_.canvasSize[1]);
    ImGui::SameLine();
    if (ImGui::Button("New Sprite", ImVec2(96.0f, 22.0f))) {
        newSpriteSize_ = document_.canvasSize;
        openNewSpritePopup_ = true;
    }

    const float controlsStart = std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - 360.0f);
    ImGui::SameLine(controlsStart);
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &onionSkin_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Zoom", &zoom_, 2, 32);
    ImGui::EndChild();

    if (openNewSpritePopup_) {
        ImGui::OpenPopup("New Sprite");
        openNewSpritePopup_ = false;
    }

    if (ImGui::BeginPopupModal("New Sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Dimensions  %dx%d", game::kTileSize, game::kTileSize);

        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
            recordUndoState();
            createBlankSprite(game::kTileSize, game::kTileSize);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openResizePopup_) {
        ImGui::OpenPopup("Resize Sprite");
        openResizePopup_ = false;
    }

    if (ImGui::BeginPopupModal("Resize Sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Dimensions  %dx%d", game::kTileSize, game::kTileSize);
        ImGui::TextUnformatted("Resizes all frames and layers using nearest-neighbor scaling.");

        if (ImGui::Button("Resize", ImVec2(120.0f, 0.0f))) {
            recordUndoState();
            resizeSprite(game::kTileSize, game::kTileSize);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openResizeSelectionPopup_) {
        ImGui::OpenPopup("Resize Selection");
        openResizeSelectionPopup_ = false;
    }

    if (ImGui::BeginPopupModal("Resize Selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int size[2]{resizeSelectionSize_[0], resizeSelectionSize_[1]};
        if (ImGui::InputInt2("Dimensions", size)) {
            resizeSelectionSize_[0] = clampDimension(size[0]);
            resizeSelectionSize_[1] = clampDimension(size[1]);
        }

        ImGui::TextUnformatted("Resizes the active layer selection using nearest-neighbor scaling.");

        if (ImGui::Button("Resize", ImVec2(120.0f, 0.0f))) {
            recordUndoState();
            resizeSelection(resizeSelectionSize_[0], resizeSelectionSize_[1]);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
}

void SpriteEditorPanel::drawLeftRail()
{
    sectionHeader("Frames");
    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = selectedFrame_ == i;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(230, 199, 34, 255) : IM_COL32(47, 51, 56, 255));
        const std::string thumbId = "##FrameThumb" + std::to_string(i);
        if (ImGui::Button(thumbId.c_str(), ImVec2(86.0f, 86.0f))) {
            selectedFrame_ = i;
        }
        ImGui::PopStyleColor();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 thumbMin{min.x + 5.0f, min.y + 5.0f};
        const ImVec2 thumbMax{max.x - 5.0f, max.y - 18.0f};
        checkerboard(drawList, thumbMin, thumbMax, 8.0f);

        const float pixelSize = std::max(
            1.0f,
            std::floor(std::min(
                (thumbMax.x - thumbMin.x) / static_cast<float>(document_.canvasSize[0]),
                (thumbMax.y - thumbMin.y) / static_cast<float>(document_.canvasSize[1]))));
        const ImVec2 spriteSize{
            static_cast<float>(document_.canvasSize[0]) * pixelSize,
            static_cast<float>(document_.canvasSize[1]) * pixelSize,
        };
        const ImVec2 spriteOrigin{
            thumbMin.x + ((thumbMax.x - thumbMin.x) - spriteSize.x) * 0.5f,
            thumbMin.y + ((thumbMax.y - thumbMin.y) - spriteSize.y) * 0.5f,
        };
        drawSpritePixels(drawList, spriteOrigin, pixelSize, i);
        drawList->AddText({min.x + 8.0f, max.y - 16.0f}, IM_COL32(235, 238, 242, 255), std::to_string(i + 1).c_str());
        ImGui::PopID();
    }

    if (ImGui::Button("+ Add frame", ImVec2(-1.0f, 36.0f))) {
        recordUndoState();
        document_.frames.push_back(document_.frames[selectedFrame_]);
        document_.cels.push_back(std::vector<SpriteCel>(
            document_.layers.size(),
            SpriteCel{std::vector<unsigned int>(static_cast<std::size_t>(document_.canvasSize[0] * document_.canvasSize[1]), 0u)}));
        selectedFrame_ = static_cast<int>(document_.frames.size()) - 1;
    }
    if (ImGui::Button("Copy frame", ImVec2(-1.0f, 30.0f))) {
        duplicateCurrentFrame();
    }
    if (ImGui::Button("Delete frame", ImVec2(-1.0f, 30.0f))) {
        deleteCurrentFrame();
    }
    if (ImGui::Button("Clear frame", ImVec2(-1.0f, 30.0f))) {
        clearCurrentFrame();
    }

    sectionHeader("Tools");
    for (int i = 0; i < static_cast<int>(std::size(kToolNames)); ++i) {
        drawToolButton(kToolNames[i], kToolNames[i], i);
        if (i % 4 != 3) {
            ImGui::SameLine();
        }
    }

    sectionHeader("Brush");
    const int brushSizes[] = {1, 2, 4};
    for (int i = 0; i < static_cast<int>(std::size(brushSizes)); ++i) {
        ImGui::PushID(brushSizes[i]);
        const bool selected = brushSize_ == brushSizes[i];
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(236, 203, 49, 255) : IM_COL32(56, 61, 67, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? IM_COL32(24, 25, 28, 255) : IM_COL32(240, 242, 245, 255));
        const std::string label = std::to_string(brushSizes[i]) + "x" + std::to_string(brushSizes[i]);
        if (ImGui::Button(label.c_str(), ImVec2(58.0f, 30.0f))) {
            brushSize_ = brushSizes[i];
        }
        ImGui::PopStyleColor(2);
        if (i + 1 < static_cast<int>(std::size(brushSizes))) {
            ImGui::SameLine();
        }
        ImGui::PopID();
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
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            secondaryColor_ = i;
        }
        ImGui::PopStyleColor(3);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (primaryColor_ == i) {
            drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 224, 64, 255), 0.0f, 0, 3.0f);
        }
        if (secondaryColor_ == i) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            drawList->AddRect({min.x + 3.0f, min.y + 3.0f}, {max.x - 3.0f, max.y - 3.0f}, IM_COL32(85, 180, 255, 255), 0.0f, 0, 2.0f);
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
    const ImVec2 buttonSize(42.0f, 38.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(236, 203, 49, 255) : IM_COL32(56, 61, 67, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? IM_COL32(24, 25, 28, 255) : IM_COL32(240, 242, 245, 255));
    const std::string buttonId = std::string("##ToolButton") + label;
    if (ImGui::Button(buttonId.c_str(), buttonSize)) {
        selectedTool_ = toolIndex;
    }
    ImGui::PopStyleColor(2);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImU32 iconColor = selected ? IM_COL32(24, 25, 28, 255) : IM_COL32(240, 242, 245, 255);
    drawToolGlyph(drawList, min, max, toolIndex, iconColor);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

void SpriteEditorPanel::drawCenterWorkspace()
{
    ImGui::Text("Canvas  %dx%d", document_.canvasSize[0], document_.canvasSize[1]);
    ImGui::SameLine();
    ImGui::Text("Grid  %dx%d", document_.gridSize[0], document_.gridSize[1]);

    ImGui::SameLine();
    int pivot[2]{document_.pivot[0], document_.pivot[1]};
    if (ImGui::InputInt2("Pivot", pivot)) {
        document_.pivot[0] = std::clamp(pivot[0], 0, document_.canvasSize[0] - 1);
        document_.pivot[1] = std::clamp(pivot[1], 0, document_.canvasSize[1] - 1);
    }

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

    handleCanvasInput(canvasOrigin, pixelSize);

    drawList->AddRectFilled({canvasOrigin.x - 18.0f, canvasOrigin.y - 18.0f}, {canvasMax.x + 18.0f, canvasMax.y + 18.0f}, IM_COL32(166, 168, 170, 255));
    checkerboard(drawList, canvasOrigin, canvasMax, pixelSize);
    drawSpritePixels(drawList, canvasOrigin, pixelSize, selectedFrame_);
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

    drawSelectionOverlay(drawList, canvasOrigin, pixelSize);

    int hoverX = 0;
    int hoverY = 0;
    if (canvasPixelAt(canvasOrigin, pixelSize, hoverX, hoverY)) {
        const ImVec2 min{
            canvasOrigin.x + static_cast<float>(hoverX) * pixelSize,
            canvasOrigin.y + static_cast<float>(hoverY) * pixelSize,
        };
        const ImVec2 max{min.x + pixelSize, min.y + pixelSize};
        drawList->AddRect(min, max, IM_COL32(255, 255, 255, 180), 0.0f, 0, 2.0f);
    }

}

void SpriteEditorPanel::drawSpritePixels(ImDrawList* drawList, ImVec2 origin, float pixelSize, int frameIndex) const
{
    for (int layerIndex = 0; layerIndex < static_cast<int>(document_.layers.size()); ++layerIndex) {
        const SpriteLayer& layer = document_.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        const float opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
        if (opacity <= 0.0f) {
            continue;
        }

        const SpriteCel& cel = celAt(frameIndex, layerIndex);
        for (int y = 0; y < document_.canvasSize[1]; ++y) {
            for (int x = 0; x < document_.canvasSize[0]; ++x) {
                const unsigned int color = cel.pixels[static_cast<std::size_t>(y) * document_.canvasSize[0] + x];
                const unsigned int alpha = (color >> 24) & 0xff;
                if (alpha == 0u) {
                    continue;
                }

                const unsigned int scaledAlpha = static_cast<unsigned int>(std::clamp(opacity * static_cast<float>(alpha), 0.0f, 255.0f));
                const unsigned int layerColor = (color & 0x00ffffffu) | (scaledAlpha << 24);
                drawList->AddRectFilled(
                    {origin.x + static_cast<float>(x) * pixelSize, origin.y + static_cast<float>(y) * pixelSize},
                    {origin.x + static_cast<float>(x + 1) * pixelSize, origin.y + static_cast<float>(y + 1) * pixelSize},
                    packedColor(layerColor));
            }
        }
    }
}

void SpriteEditorPanel::drawRightInspector(EditorContext& context)
{
    sectionHeader("Preview");
    drawPreview(ImVec2(-1.0f, 178.0f));
    ImGui::Checkbox("Run animation", &runAnimationPreview_);
    if (!runAnimationPreview_) {
        previewTimeSeconds_ = 0.0f;
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("FPS", &playbackFps_, 1, 30);

    sectionHeader("Layers");
    drawLayers();

    sectionHeader("Transform");
    if (ImGui::Button("Flip H", ImVec2(62.0f, 34.0f))) {
        flipHorizontal();
    }
    ImGui::SameLine();
    if (ImGui::Button("Flip V", ImVec2(62.0f, 34.0f))) {
        flipVertical();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate", ImVec2(72.0f, 34.0f))) {
        rotateClockwise();
    }
    if (hasSelection_ && selectionAppliesToActiveCel()) {
        ImGui::SameLine();
        if (ImGui::Button("Resize Sel", ImVec2(92.0f, 34.0f))) {
            resizeSelectionSize_[0] = selectionBounds_[2] - selectionBounds_[0] + 1;
            resizeSelectionSize_[1] = selectionBounds_[3] - selectionBounds_[1] + 1;
            openResizeSelectionPopup_ = true;
        }
    }
    if (ImGui::Button("Resize", ImVec2(72.0f, 34.0f))) {
        resizeSpriteSize_ = document_.canvasSize;
        openResizePopup_ = true;
    }
    ImGui::InputInt2("Pivot", document_.pivot.data());
    document_.pivot[0] = std::clamp(document_.pivot[0], 0, document_.canvasSize[0] - 1);
    document_.pivot[1] = std::clamp(document_.pivot[1], 0, document_.canvasSize[1] - 1);

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

    const float pixelSize = std::max(
        1.0f,
        std::floor(std::min(
            (width - 24.0f) / static_cast<float>(document_.canvasSize[0]),
            (height - 24.0f) / static_cast<float>(document_.canvasSize[1]))));
    const ImVec2 spriteSize{
        static_cast<float>(document_.canvasSize[0]) * pixelSize,
        static_cast<float>(document_.canvasSize[1]) * pixelSize,
    };
    const ImVec2 spriteOrigin{
        origin.x + (width - spriteSize.x) * 0.5f,
        origin.y + (height - spriteSize.y) * 0.5f,
    };
    drawSpritePixels(drawList, spriteOrigin, pixelSize, previewFrameIndex());
    drawList->AddRect(origin, max, IM_COL32(118, 126, 135, 255));
}

void SpriteEditorPanel::drawFrames()
{
    constexpr const char* kFrameTypes[] = {
        "idle",
        "walk",
        "run",
        "attack_1",
        "attack_2",
        "attack_3",
        "cast",
        "hit_react",
        "guard",
        "interact",
        "conversation",
        "death",
        "fall",
        "sleep",
        "jump",
        "land",
        "pickup",
        "use_item",
        "hurt_loop",
        "stunned",
        "victory",
        "emote",
        "climb",
        "swim",
        "dash",
        "block_break",
        "revive",
    };

    if (ImGui::Button("Add frame")) {
        recordUndoState();
        document_.frames.push_back(document_.frames[selectedFrame_]);
        document_.cels.push_back(std::vector<SpriteCel>(
            document_.layers.size(),
            SpriteCel{std::vector<unsigned int>(static_cast<std::size_t>(document_.canvasSize[0] * document_.canvasSize[1]), 0u)}));
        selectedFrame_ = static_cast<int>(document_.frames.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate frame") && !document_.frames.empty()) {
        recordUndoState();
        document_.frames.insert(document_.frames.begin() + selectedFrame_ + 1, document_.frames[selectedFrame_]);
        document_.cels.insert(document_.cels.begin() + selectedFrame_ + 1, document_.cels[selectedFrame_]);
        ++selectedFrame_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete frame") && document_.frames.size() > 1) {
        recordUndoState();
        document_.frames.erase(document_.frames.begin() + selectedFrame_);
        document_.cels.erase(document_.cels.begin() + selectedFrame_);
        selectedFrame_ = std::clamp(selectedFrame_, 0, static_cast<int>(document_.frames.size()) - 1);
    }

    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        const std::string frameLabel = "Frame " + std::to_string(i + 1) + "  [" + document_.frames[i].type + "]";
        if (ImGui::Selectable(frameLabel.c_str(), selectedFrame_ == i)) {
            selectedFrame_ = i;
        }
        if (selectedFrame_ == i) {
            ImGui::InputInt("X", &document_.frames[i].x);
            ImGui::InputInt("Y", &document_.frames[i].y);
            ImGui::InputInt("Width", &document_.frames[i].width);
            ImGui::InputInt("Height", &document_.frames[i].height);
            ImGui::InputInt("Duration ms", &document_.frames[i].durationMs);
            int selectedType = -1;
            for (int typeIndex = 0; typeIndex < static_cast<int>(std::size(kFrameTypes)); ++typeIndex) {
                if (document_.frames[i].type == kFrameTypes[typeIndex]) {
                    selectedType = typeIndex;
                    break;
                }
            }
            const char* preview = selectedType >= 0 ? kFrameTypes[selectedType] : document_.frames[i].type.c_str();
            if (ImGui::BeginCombo("Frame type", preview)) {
                for (int typeIndex = 0; typeIndex < static_cast<int>(std::size(kFrameTypes)); ++typeIndex) {
                    const bool selected = selectedType == typeIndex;
                    if (ImGui::Selectable(kFrameTypes[typeIndex], selected)) {
                        recordUndoState();
                        document_.frames[i].type = kFrameTypes[typeIndex];
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            char typeBuffer[64]{};
            std::copy_n(document_.frames[i].type.c_str(), std::min(document_.frames[i].type.size(), sizeof(typeBuffer) - 1), typeBuffer);
            if (ImGui::InputText("Custom type", typeBuffer, sizeof(typeBuffer))) {
                document_.frames[i].type = typeBuffer;
                documentDirty_ = true;
            }
        }
        ImGui::PopID();
    }
}

void SpriteEditorPanel::drawLayers()
{
    if (ImGui::Button("Add layer")) {
        recordUndoState();
        document_.layers.push_back({"Layer " + std::to_string(document_.layers.size() + 1), true, 1.0f});
        for (auto& frame : document_.cels) {
            frame.push_back(SpriteCel{std::vector<unsigned int>(static_cast<std::size_t>(document_.canvasSize[0] * document_.canvasSize[1]), 0u)});
        }
        selectedLayer_ = static_cast<int>(document_.layers.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete layer") && document_.layers.size() > 1) {
        recordUndoState();
        document_.layers.erase(document_.layers.begin() + selectedLayer_);
        for (auto& frame : document_.cels) {
            frame.erase(frame.begin() + selectedLayer_);
        }
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
    if (ImGui::Button("Export single frame PNG")) {
        exportSingleFramePng(context);
    }
    if (ImGui::Button("Export all frame PNGs")) {
        exportFramePngs(context);
    }
    if (ImGui::Button("Export sprite sheet PNG")) {
        exportSpriteSheetPng(context);
        saveSpriteMetadata(context);
    }
    if (ImGui::Button("Import source PNG")) {
        importPng(context);
    }
    ImGui::TextDisabled("Import supports RGBA PNGs exported by this editor.");
    if (!ioStatus_.empty()) {
        ImGui::TextWrapped("%s", ioStatus_.c_str());
    }
}

bool SpriteEditorPanel::loadDocumentFromMetadata(const std::filesystem::path& metadataPath)
{
    const std::filesystem::path projectRoot = projectRootFromSpriteMetadataPath(metadataPath);
    game::SpriteMetadata metadata;
    std::string error;
    if (!game::loadSpriteMetadata(metadataPath, metadata, &error)) {
        ioStatus_ = "Failed to load sprite metadata: " + error;
        return false;
    }

    document_ = SpriteDocument{};
    document_.id = metadata.id;
    document_.sourcePng = metadata.source;
    document_.canvasSize = metadata.canvasSize;
    document_.gridSize = metadata.gridSize;
    document_.pivot = metadata.pivot;
    document_.frames.clear();
    document_.frames.reserve(metadata.frames.size());
    for (const game::SpriteFrameDef& frame : metadata.frames) {
        document_.frames.push_back(SpriteFrame{frame.x, frame.y, frame.width, frame.height, frame.durationMs, frame.type});
    }
    document_.tags = metadata.tags;
    document_.layers.assign(1, SpriteLayer{});
    document_.cels.assign(document_.frames.size(), std::vector<SpriteCel>(1));
    trackedCanvasSize_ = document_.canvasSize;
    resizeSpriteSize_ = document_.canvasSize;
    newSpriteSize_ = document_.canvasSize;
    clearSelection();
    resizeCanvasStorage(document_.canvasSize[0], document_.canvasSize[1], document_.canvasSize[0], document_.canvasSize[1]);

    if (!document_.sourcePng.empty()) {
        (void)importSheetPixels(resolveAgainstProjectRoot(projectRoot, document_.sourcePng));
    }

    ensureDocumentState();
    ioStatus_ = "Loaded sprite metadata: " + metadataPath.generic_string();
    return true;
}

bool SpriteEditorPanel::importSheetPixels(const std::filesystem::path& inputPath)
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    if (!readEditorPngRgba(inputPath, width, height, rgba)) {
        return false;
    }

    const int frameWidth = document_.canvasSize[0];
    const int frameHeight = document_.canvasSize[1];
    if (frameWidth <= 0 || frameHeight <= 0 || height != frameHeight || width < frameWidth) {
        return false;
    }

    const int availableFrames = std::max(1, width / frameWidth);
    const int frameCount = std::min(availableFrames, static_cast<int>(document_.frames.size()));
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        SpriteCel& cel = document_.cels[static_cast<std::size_t>(frameIndex)][0];
        cel.pixels.assign(static_cast<std::size_t>(frameWidth * frameHeight), 0u);
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const std::size_t srcIndex = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(frameIndex * frameWidth + x)) * 4u;
                cel.pixels[static_cast<std::size_t>(y) * frameWidth + x] =
                    (static_cast<unsigned int>(rgba[srcIndex + 3]) << 24) |
                    (static_cast<unsigned int>(rgba[srcIndex + 2]) << 16) |
                    (static_cast<unsigned int>(rgba[srcIndex + 1]) << 8) |
                    static_cast<unsigned int>(rgba[srcIndex + 0]);
            }
        }
    }
    return true;
}

void SpriteEditorPanel::saveSpriteMetadata(const EditorContext& context) const
{
    game::SpriteMetadata metadata;
    metadata.id = document_.id;
    metadata.source = portableProjectPath(context.assets.projectRoot, document_.sourcePng);
    metadata.canvasSize = document_.canvasSize;
    metadata.gridSize = document_.gridSize;
    metadata.pivot = document_.pivot;
    metadata.tags = document_.tags;
    metadata.frames.clear();
    metadata.frames.reserve(document_.frames.size());
    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        const SpriteFrame& frame = document_.frames[static_cast<std::size_t>(i)];
        metadata.frames.push_back({i * document_.canvasSize[0], 0, document_.canvasSize[0], document_.canvasSize[1], frame.durationMs, frame.type});
    }

    std::string ignoredError;
    (void)game::saveSpriteMetadata(context.assets.gameSpritePath() / (document_.id + ".sprite.json"), metadata, &ignoredError);
}

void SpriteEditorPanel::exportSpriteSheetPng(const EditorContext& context)
{
    ensureDirectory(context.assets.rawSpritePath());
    const int frameWidth = document_.canvasSize[0];
    const int frameHeight = document_.canvasSize[1];
    const int frameCount = static_cast<int>(document_.frames.size());
    if (frameWidth <= 0 || frameHeight <= 0 || frameCount <= 0) {
        return;
    }

    const int sheetWidth = frameWidth * frameCount;
    const int sheetHeight = frameHeight;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(sheetWidth) * static_cast<std::size_t>(sheetHeight) * 4u, 0u);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const std::vector<unsigned char> frameRgba = compositeFrameRgba(frameIndex);
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const int sheetX = frameIndex * frameWidth + x;
                const std::size_t outIndex = (static_cast<std::size_t>(y) * sheetWidth + static_cast<std::size_t>(sheetX)) * 4u;
                const std::size_t frameIndexRgba = (static_cast<std::size_t>(y) * frameWidth + x) * 4u;
                std::copy(frameRgba.begin() + static_cast<std::ptrdiff_t>(frameIndexRgba), frameRgba.begin() + static_cast<std::ptrdiff_t>(frameIndexRgba + 4u), rgba.begin() + static_cast<std::ptrdiff_t>(outIndex));
            }
        }
    }

    const std::filesystem::path outputPath = context.assets.rawSpritePath() / (document_.id + "_sheet.png");
    if (writePngRgba(outputPath, sheetWidth, sheetHeight, rgba)) {
        const std::filesystem::path relativePath = context.assets.rawSprites / (document_.id + "_sheet.png");
        document_.sourcePng = relativePath;
        ioStatus_ = "Exported sprite sheet: " + relativePath.generic_string();
    } else {
        ioStatus_ = "Failed to export sprite sheet PNG.";
    }
}

void SpriteEditorPanel::exportSingleFramePng(const EditorContext& context)
{
    ensureDirectory(context.assets.rawSpritePath());
    if (selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.frames.size())) {
        ioStatus_ = "No frame selected for PNG export.";
        return;
    }

    const std::filesystem::path outputPath = context.assets.rawSpritePath() / (document_.id + "_frame_" + std::to_string(selectedFrame_ + 1) + ".png");
    if (writePngRgba(outputPath, document_.canvasSize[0], document_.canvasSize[1], compositeFrameRgba(selectedFrame_))) {
        const std::filesystem::path relativePath = context.assets.rawSprites / outputPath.filename();
        document_.sourcePng = relativePath;
        ioStatus_ = "Exported single frame: " + relativePath.generic_string();
    } else {
        ioStatus_ = "Failed to export single-frame PNG.";
    }
}

void SpriteEditorPanel::exportFramePngs(const EditorContext& context)
{
    ensureDirectory(context.assets.rawSpritePath());
    const int frameCount = static_cast<int>(document_.frames.size());
    if (document_.canvasSize[0] <= 0 || document_.canvasSize[1] <= 0 || frameCount <= 0) {
        ioStatus_ = "No frames available for PNG export.";
        return;
    }

    int exported = 0;
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const std::filesystem::path outputPath = context.assets.rawSpritePath() /
            (document_.id + "_frame_" + std::to_string(frameIndex + 1) + ".png");
        if (writePngRgba(outputPath, document_.canvasSize[0], document_.canvasSize[1], compositeFrameRgba(frameIndex))) {
            ++exported;
        }
    }

    ioStatus_ = "Exported " + std::to_string(exported) + " separate frame PNG(s).";
}

void SpriteEditorPanel::importPng(const EditorContext& context)
{
    const std::filesystem::path inputPath = document_.sourcePng.is_absolute() ? document_.sourcePng : context.assets.projectRoot / document_.sourcePng;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    if (!readEditorPngRgba(inputPath, width, height, rgba)) {
        ioStatus_ = "Failed to import PNG. Import currently supports RGBA PNGs exported by this editor.";
        return;
    }
    if (width > kMaxCanvasSize * kMaxCanvasSize || height > kMaxCanvasSize) {
        ioStatus_ = "PNG is too large for the sprite editor import limits.";
        return;
    }

    const int frameWidth = width % document_.canvasSize[0] == 0 && height == document_.canvasSize[1] ? document_.canvasSize[0] : width;
    const int frameHeight = height;
    const int frameCount = std::max(1, width / frameWidth);
    if (frameWidth > kMaxCanvasSize || frameHeight > kMaxCanvasSize) {
        ioStatus_ = "Imported frame dimensions exceed the sprite editor limit.";
        return;
    }

    recordUndoState();
    document_.canvasSize = {frameWidth, frameHeight};
    document_.gridSize = document_.canvasSize;
    document_.pivot = {frameWidth / 2, frameHeight / 2};
    document_.frames.assign(static_cast<std::size_t>(frameCount), SpriteFrame{});
    document_.layers.assign(1, SpriteLayer{});
    document_.cels.assign(static_cast<std::size_t>(frameCount), std::vector<SpriteCel>(1));

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document_.frames[frameIndex].width = frameWidth;
        document_.frames[frameIndex].height = frameHeight;
        SpriteCel& cel = document_.cels[frameIndex][0];
        cel.pixels.assign(static_cast<std::size_t>(frameWidth * frameHeight), 0u);
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const std::size_t srcIndex = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(frameIndex * frameWidth + x)) * 4u;
                cel.pixels[static_cast<std::size_t>(y) * frameWidth + x] =
                    (static_cast<unsigned int>(rgba[srcIndex + 3]) << 24) |
                    (static_cast<unsigned int>(rgba[srcIndex + 2]) << 16) |
                    (static_cast<unsigned int>(rgba[srcIndex + 1]) << 8) |
                    static_cast<unsigned int>(rgba[srcIndex + 0]);
            }
        }
    }

    selectedFrame_ = 0;
    selectedLayer_ = 0;
    trackedCanvasSize_ = document_.canvasSize;
    resizeSpriteSize_ = document_.canvasSize;
    clearSelection();
    ioStatus_ = "Imported PNG: " + document_.sourcePng.generic_string();
}

std::vector<unsigned char> SpriteEditorPanel::compositeFrameRgba(int frameIndex) const
{
    const int frameWidth = document_.canvasSize[0];
    const int frameHeight = document_.canvasSize[1];
    std::vector<unsigned char> rgba(static_cast<std::size_t>(frameWidth) * static_cast<std::size_t>(frameHeight) * 4u, 0u);

    for (int y = 0; y < frameHeight; ++y) {
        for (int x = 0; x < frameWidth; ++x) {
            float outR = 0.0f;
            float outG = 0.0f;
            float outB = 0.0f;
            float outA = 0.0f;

            for (int layerIndex = 0; layerIndex < static_cast<int>(document_.layers.size()); ++layerIndex) {
                const SpriteLayer& layer = document_.layers[layerIndex];
                if (!layer.visible || layer.opacity <= 0.0f) {
                    continue;
                }

                const SpriteCel& cel = celAt(frameIndex, layerIndex);
                const unsigned int color = cel.pixels[static_cast<std::size_t>(y) * frameWidth + x];
                const float srcA = (static_cast<float>((color >> 24) & 0xffu) / 255.0f) * std::clamp(layer.opacity, 0.0f, 1.0f);
                if (srcA <= 0.0f) {
                    continue;
                }

                const float srcR = static_cast<float>((color >> 0) & 0xffu) / 255.0f;
                const float srcG = static_cast<float>((color >> 8) & 0xffu) / 255.0f;
                const float srcB = static_cast<float>((color >> 16) & 0xffu) / 255.0f;

                outR = srcR * srcA + outR * (1.0f - srcA);
                outG = srcG * srcA + outG * (1.0f - srcA);
                outB = srcB * srcA + outB * (1.0f - srcA);
                outA = srcA + outA * (1.0f - srcA);
            }

            const std::size_t outIndex = (static_cast<std::size_t>(y) * frameWidth + x) * 4u;
            rgba[outIndex + 0] = static_cast<unsigned char>(std::clamp(outR * 255.0f, 0.0f, 255.0f));
            rgba[outIndex + 1] = static_cast<unsigned char>(std::clamp(outG * 255.0f, 0.0f, 255.0f));
            rgba[outIndex + 2] = static_cast<unsigned char>(std::clamp(outB * 255.0f, 0.0f, 255.0f));
            rgba[outIndex + 3] = static_cast<unsigned char>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
        }
    }

    return rgba;
}

int SpriteEditorPanel::previewFrameIndex() const
{
    if (!runAnimationPreview_ || document_.frames.size() <= 1) {
        return selectedFrame_;
    }

    int totalDurationMs = 0;
    for (const SpriteFrame& frame : document_.frames) {
        totalDurationMs += std::max(frame.durationMs, 1000 / std::max(playbackFps_, 1));
    }

    if (totalDurationMs <= 0) {
        return selectedFrame_;
    }

    const int currentTimeMs = static_cast<int>(previewTimeSeconds_ * 1000.0f) % totalDurationMs;
    int accumulatedMs = 0;
    for (int frameIndex = 0; frameIndex < static_cast<int>(document_.frames.size()); ++frameIndex) {
        accumulatedMs += std::max(document_.frames[frameIndex].durationMs, 1000 / std::max(playbackFps_, 1));
        if (currentTimeMs < accumulatedMs) {
            return frameIndex;
        }
    }

    return static_cast<int>(document_.frames.size()) - 1;
}

void SpriteEditorPanel::handleShortcuts()
{
    if (ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput) {
        return;
    }

    if (!primaryShortcutDown()) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
        undo();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C)) {
        copySelection();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_V)) {
        pasteSelection();
    }
}

bool SpriteEditorPanel::primaryShortcutDown() const
{
    const ImGuiIO& io = ImGui::GetIO();
#if defined(__APPLE__)
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

void SpriteEditorPanel::recordUndoState()
{
    documentDirty_ = true;
    constexpr std::size_t kMaxUndoStates = 64;
    undoStack_.push_back(SpriteUndoState{
        document_,
        trackedCanvasSize_,
        selectionBounds_,
        selectedFrame_,
        selectedLayer_,
        selectedTool_,
        brushSize_,
        primaryColor_,
        secondaryColor_,
        selectionFrame_,
        selectionLayer_,
        hasSelection_,
    });

    if (undoStack_.size() > kMaxUndoStates) {
        undoStack_.erase(undoStack_.begin());
    }
}

void SpriteEditorPanel::undo()
{
    if (undoStack_.empty()) {
        return;
    }

    const SpriteUndoState state = std::move(undoStack_.back());
    undoStack_.pop_back();

    document_ = state.document;
    trackedCanvasSize_ = state.trackedCanvasSize;
    selectionBounds_ = state.selectionBounds;
    selectedFrame_ = state.selectedFrame;
    selectedLayer_ = state.selectedLayer;
    selectedTool_ = state.selectedTool;
    brushSize_ = state.brushSize;
    primaryColor_ = state.primaryColor;
    secondaryColor_ = state.secondaryColor;
    selectionFrame_ = state.selectionFrame;
    selectionLayer_ = state.selectionLayer;
    hasSelection_ = state.hasSelection;

    selectionDragActive_ = false;
    moveDragActive_ = false;
    shapeDragActive_ = false;
    strokeUndoCaptured_ = false;
    lastPaintPixel_ = {-1, -1};
    moveSourcePixels_.clear();
    ensureDocumentState();
}

void SpriteEditorPanel::ensureDocumentState()
{
    document_.canvasSize[0] = clampDimension(document_.canvasSize[0]);
    document_.canvasSize[1] = clampDimension(document_.canvasSize[1]);
    document_.gridSize[0] = std::clamp(document_.gridSize[0], 1, document_.canvasSize[0]);
    document_.gridSize[1] = std::clamp(document_.gridSize[1], 1, document_.canvasSize[1]);
    document_.pivot[0] = std::clamp(document_.pivot[0], 0, document_.canvasSize[0] - 1);
    document_.pivot[1] = std::clamp(document_.pivot[1], 0, document_.canvasSize[1] - 1);

    if (document_.frames.empty()) {
        document_.frames.push_back(SpriteFrame{});
    }
    if (document_.layers.empty()) {
        document_.layers.push_back(SpriteLayer{});
    }

    const bool frameCountChanged = document_.cels.size() != document_.frames.size();
    const bool layerCountChanged = std::any_of(document_.cels.begin(), document_.cels.end(), [&](const auto& frame) {
        return frame.size() != document_.layers.size();
    });
    const bool canvasSizeChanged = trackedCanvasSize_ != document_.canvasSize;

    if (frameCountChanged || layerCountChanged || canvasSizeChanged) {
        resizeCanvasStorage(
            trackedCanvasSize_[0],
            trackedCanvasSize_[1],
            document_.canvasSize[0],
            document_.canvasSize[1]);
        trackedCanvasSize_ = document_.canvasSize;
    }

    for (SpriteFrame& frame : document_.frames) {
        frame.x = std::clamp(frame.x, 0, document_.canvasSize[0] - 1);
        frame.y = std::clamp(frame.y, 0, document_.canvasSize[1] - 1);
        frame.width = std::clamp(frame.width, 1, document_.canvasSize[0] - frame.x);
        frame.height = std::clamp(frame.height, 1, document_.canvasSize[1] - frame.y);
        frame.durationMs = std::max(frame.durationMs, 1);
    }

    selectedFrame_ = std::clamp(selectedFrame_, 0, static_cast<int>(document_.frames.size()) - 1);
    selectedLayer_ = std::clamp(selectedLayer_, 0, static_cast<int>(document_.layers.size()) - 1);
    primaryColor_ = std::clamp(primaryColor_, 0, static_cast<int>(document_.palette.size()) - 1);
    secondaryColor_ = std::clamp(secondaryColor_, 0, static_cast<int>(document_.palette.size()) - 1);

    if (hasSelection_) {
        if (!selectionAppliesToActiveCel()) {
            clearSelection();
        } else {
            selectionBounds_[0] = std::clamp(selectionBounds_[0], 0, document_.canvasSize[0] - 1);
            selectionBounds_[1] = std::clamp(selectionBounds_[1], 0, document_.canvasSize[1] - 1);
            selectionBounds_[2] = std::clamp(selectionBounds_[2], selectionBounds_[0], document_.canvasSize[0] - 1);
            selectionBounds_[3] = std::clamp(selectionBounds_[3], selectionBounds_[1], document_.canvasSize[1] - 1);
        }
    }
}

void SpriteEditorPanel::createBlankSprite(int width, int height)
{
    document_ = SpriteDocument{};
    document_.sourcePng.clear();
    document_.canvasSize = {clampDimension(width), clampDimension(height)};
    document_.gridSize = document_.canvasSize;
    document_.pivot = {document_.canvasSize[0] / 2, document_.canvasSize[1] / 2};
    document_.frames[0].width = document_.canvasSize[0];
    document_.frames[0].height = document_.canvasSize[1];

    selectedFrame_ = 0;
    selectedLayer_ = 0;
    selectedTool_ = kToolPen;
    brushSize_ = 1;
    primaryColor_ = std::min(1, static_cast<int>(document_.palette.size()) - 1);
    secondaryColor_ = 0;
    lastPaintPixel_ = {-1, -1};
    shapeDragActive_ = false;
    dragUsesSecondaryColor_ = false;
    trackedCanvasSize_ = document_.canvasSize;
    newSpriteSize_ = document_.canvasSize;
    resizeSpriteSize_ = document_.canvasSize;
    clearSelection();

    resizeCanvasStorage(document_.canvasSize[0], document_.canvasSize[1], document_.canvasSize[0], document_.canvasSize[1]);
}

void SpriteEditorPanel::clearCurrentFrame()
{
    if (selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.cels.size())) {
        return;
    }

    recordUndoState();
    for (SpriteCel& cel : document_.cels[selectedFrame_]) {
        std::fill(cel.pixels.begin(), cel.pixels.end(), 0u);
    }
    clearSelection();
}

void SpriteEditorPanel::resizeSprite(int newWidth, int newHeight)
{
    newWidth = clampDimension(newWidth);
    newHeight = clampDimension(newHeight);

    const int oldWidth = document_.canvasSize[0];
    const int oldHeight = document_.canvasSize[1];
    if (oldWidth == newWidth && oldHeight == newHeight) {
        return;
    }

    auto oldCels = document_.cels;
    document_.canvasSize = {newWidth, newHeight};
    document_.gridSize[0] = std::min(document_.gridSize[0], newWidth);
    document_.gridSize[1] = std::min(document_.gridSize[1], newHeight);
    document_.pivot[0] = std::clamp(static_cast<int>(std::lround(static_cast<float>(document_.pivot[0]) * newWidth / std::max(oldWidth, 1))), 0, newWidth - 1);
    document_.pivot[1] = std::clamp(static_cast<int>(std::lround(static_cast<float>(document_.pivot[1]) * newHeight / std::max(oldHeight, 1))), 0, newHeight - 1);

    document_.cels.assign(document_.frames.size(), std::vector<SpriteCel>(document_.layers.size()));
    for (int frameIndex = 0; frameIndex < static_cast<int>(document_.frames.size()); ++frameIndex) {
        SpriteFrame& frame = document_.frames[frameIndex];
        frame.x = std::clamp(static_cast<int>(std::lround(static_cast<float>(frame.x) * newWidth / std::max(oldWidth, 1))), 0, newWidth - 1);
        frame.y = std::clamp(static_cast<int>(std::lround(static_cast<float>(frame.y) * newHeight / std::max(oldHeight, 1))), 0, newHeight - 1);
        frame.width = std::clamp(static_cast<int>(std::lround(static_cast<float>(frame.width) * newWidth / std::max(oldWidth, 1))), 1, newWidth - frame.x);
        frame.height = std::clamp(static_cast<int>(std::lround(static_cast<float>(frame.height) * newHeight / std::max(oldHeight, 1))), 1, newHeight - frame.y);

        for (int layerIndex = 0; layerIndex < static_cast<int>(document_.layers.size()); ++layerIndex) {
            SpriteCel& newCel = document_.cels[frameIndex][layerIndex];
            newCel.pixels.assign(static_cast<std::size_t>(newWidth * newHeight), 0u);
            const SpriteCel& oldCel = oldCels[frameIndex][layerIndex];

            for (int y = 0; y < newHeight; ++y) {
                const int srcY = std::clamp((y * oldHeight) / std::max(newHeight, 1), 0, oldHeight - 1);
                for (int x = 0; x < newWidth; ++x) {
                    const int srcX = std::clamp((x * oldWidth) / std::max(newWidth, 1), 0, oldWidth - 1);
                    newCel.pixels[static_cast<std::size_t>(y) * newWidth + x] =
                        oldCel.pixels[static_cast<std::size_t>(srcY) * oldWidth + srcX];
                }
            }
        }
    }

    trackedCanvasSize_ = document_.canvasSize;
    resizeSpriteSize_ = document_.canvasSize;
    clearSelection();
}

void SpriteEditorPanel::resizeCanvasStorage(int oldWidth, int oldHeight, int newWidth, int newHeight)
{
    const auto oldCels = std::move(document_.cels);
    document_.cels.assign(document_.frames.size(), std::vector<SpriteCel>(document_.layers.size()));

    for (int frameIndex = 0; frameIndex < static_cast<int>(document_.frames.size()); ++frameIndex) {
        for (int layerIndex = 0; layerIndex < static_cast<int>(document_.layers.size()); ++layerIndex) {
            SpriteCel& newCel = document_.cels[frameIndex][layerIndex];
            newCel.pixels.assign(static_cast<std::size_t>(newWidth * newHeight), 0u);

            if (frameIndex >= static_cast<int>(oldCels.size()) || layerIndex >= static_cast<int>(oldCels[frameIndex].size())) {
                continue;
            }

            const SpriteCel& oldCel = oldCels[frameIndex][layerIndex];
            const int copyWidth = std::min(oldWidth, newWidth);
            const int copyHeight = std::min(oldHeight, newHeight);
            for (int y = 0; y < copyHeight; ++y) {
                for (int x = 0; x < copyWidth; ++x) {
                    const std::size_t oldIndex = static_cast<std::size_t>(y) * oldWidth + x;
                    const std::size_t newIndex = static_cast<std::size_t>(y) * newWidth + x;
                    if (oldIndex < oldCel.pixels.size()) {
                        newCel.pixels[newIndex] = oldCel.pixels[oldIndex];
                    }
                }
            }
        }
    }
}

void SpriteEditorPanel::handleCanvasInput(const ImVec2& canvasOrigin, float pixelSize)
{
    int x = 0;
    int y = 0;
    const bool hoveredPixel = canvasPixelAt(canvasOrigin, pixelSize, x, y);
    const bool leftClicked = hoveredPixel && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool rightClicked = hoveredPixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (selectedTool_ == kToolLine || selectedTool_ == kToolRect || selectedTool_ == kToolCircle) {
        if (leftClicked || rightClicked) {
            shapeDragActive_ = true;
            dragStartPixel_ = {x, y};
            dragUsesSecondaryColor_ = rightClicked;
        }

        const bool releasedShapeButton =
            (!dragUsesSecondaryColor_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ||
            (dragUsesSecondaryColor_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right));

        if (shapeDragActive_ && releasedShapeButton) {
            if (hoveredPixel) {
                recordUndoState();
                SpriteCel& cel = activeCel();
                const unsigned int color = currentPaletteColor(dragUsesSecondaryColor_);
                if (selectedTool_ == kToolLine) {
                    drawLine(cel, dragStartPixel_[0], dragStartPixel_[1], x, y, color, false);
                } else if (selectedTool_ == kToolRect) {
                    drawRectangle(cel, dragStartPixel_[0], dragStartPixel_[1], x, y, color);
                } else {
                    drawCircle(cel, dragStartPixel_[0], dragStartPixel_[1], x, y, color);
                }
            }
            shapeDragActive_ = false;
        }
        return;
    }

    if (selectedTool_ == kToolBucket || selectedTool_ == kToolPicker || selectedTool_ == kToolShade) {
        if (leftClicked) {
            recordUndoState();
            applyClickTool(x, y, false);
        }
        if (rightClicked) {
            recordUndoState();
            applyClickTool(x, y, true);
        }
        return;
    }

    if (selectedTool_ == kToolSelect) {
        if (leftClicked) {
            selectionDragActive_ = true;
            dragStartPixel_ = {x, y};
            selectionDragCurrent_ = {x, y};
            moveDragActive_ = false;
        }

        if (selectionDragActive_) {
            if (hoveredPixel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selectionDragCurrent_ = {x, y};
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                setSelectionFromDrag(
                    dragStartPixel_[0],
                    dragStartPixel_[1],
                    selectionDragCurrent_[0],
                    selectionDragCurrent_[1]);
                selectionDragActive_ = false;
            }
        }
        return;
    }

    if (selectedTool_ == kToolMove) {
        if (!hasSelection_ || !selectionAppliesToActiveCel()) {
            return;
        }

        if (leftClicked && pixelInSelection(x, y)) {
            dragStartPixel_ = {x, y};
            moveDragOffset_ = {0, 0};
            beginMoveDrag();
        }

        if (moveDragActive_) {
            if (hoveredPixel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const int offsetX = x - dragStartPixel_[0];
                const int offsetY = y - dragStartPixel_[1];
                if (offsetX != moveDragOffset_[0] || offsetY != moveDragOffset_[1]) {
                    previewMoveSelection(offsetX, offsetY);
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                finalizeMoveSelection();
            }
        }
        return;
    }

    if (leftClicked || rightClicked) {
        recordUndoState();
        strokeUndoCaptured_ = true;
        dragUsesSecondaryColor_ = rightClicked;
        paintStroke(x, y, x, y, dragUsesSecondaryColor_);
        lastPaintPixel_ = {x, y};
        return;
    }

    if (!hoveredPixel) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            lastPaintPixel_ = {-1, -1};
            strokeUndoCaptured_ = false;
        }
        return;
    }

    const bool draggingLeft = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool draggingRight = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!draggingLeft && !draggingRight) {
        lastPaintPixel_ = {-1, -1};
        strokeUndoCaptured_ = false;
        return;
    }

    if (lastPaintPixel_[0] < 0 || lastPaintPixel_[1] < 0) {
        if (!strokeUndoCaptured_) {
            recordUndoState();
            strokeUndoCaptured_ = true;
        }
        lastPaintPixel_ = {x, y};
    }

    const bool useSecondary = draggingRight && !draggingLeft ? true : dragUsesSecondaryColor_;
    paintStroke(lastPaintPixel_[0], lastPaintPixel_[1], x, y, useSecondary);
    lastPaintPixel_ = {x, y};
}

bool SpriteEditorPanel::canvasPixelAt(const ImVec2& canvasOrigin, float pixelSize, int& x, int& y) const
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 canvasMax{
        canvasOrigin.x + static_cast<float>(document_.canvasSize[0]) * pixelSize,
        canvasOrigin.y + static_cast<float>(document_.canvasSize[1]) * pixelSize,
    };
    if (mouse.x < canvasOrigin.x || mouse.y < canvasOrigin.y || mouse.x >= canvasMax.x || mouse.y >= canvasMax.y) {
        return false;
    }

    x = std::clamp(static_cast<int>((mouse.x - canvasOrigin.x) / pixelSize), 0, document_.canvasSize[0] - 1);
    y = std::clamp(static_cast<int>((mouse.y - canvasOrigin.y) / pixelSize), 0, document_.canvasSize[1] - 1);
    return true;
}

SpriteCel& SpriteEditorPanel::activeCel()
{
    return celAt(selectedFrame_, selectedLayer_);
}

SpriteCel& SpriteEditorPanel::celAt(int frameIndex, int layerIndex)
{
    return document_.cels[frameIndex][layerIndex];
}

const SpriteCel& SpriteEditorPanel::celAt(int frameIndex, int layerIndex) const
{
    return document_.cels[frameIndex][layerIndex];
}

void SpriteEditorPanel::setPixel(SpriteCel& cel, int x, int y, unsigned int color)
{
    if (x < 0 || y < 0 || x >= document_.canvasSize[0] || y >= document_.canvasSize[1]) {
        return;
    }

    cel.pixels[static_cast<std::size_t>(y) * document_.canvasSize[0] + x] = color;
}

void SpriteEditorPanel::setBrushPixel(SpriteCel& cel, int x, int y, unsigned int color)
{
    const int size = std::clamp(brushSize_, 1, 4);
    for (int offsetY = 0; offsetY < size; ++offsetY) {
        for (int offsetX = 0; offsetX < size; ++offsetX) {
            setPixel(cel, x + offsetX, y + offsetY, color);
        }
    }
}

unsigned int SpriteEditorPanel::currentPaletteColor(bool useSecondaryColor) const
{
    const int paletteIndex = useSecondaryColor ? secondaryColor_ : primaryColor_;
    return document_.palette[std::clamp(paletteIndex, 0, static_cast<int>(document_.palette.size()) - 1)];
}

unsigned int SpriteEditorPanel::sampledVisibleColor(int x, int y) const
{
    const std::size_t pixelIndex = static_cast<std::size_t>(y) * document_.canvasSize[0] + x;
    for (int layerIndex = static_cast<int>(document_.layers.size()) - 1; layerIndex >= 0; --layerIndex) {
        const SpriteLayer& layer = document_.layers[layerIndex];
        if (!layer.visible || layer.opacity <= 0.0f) {
            continue;
        }

        const unsigned int color = celAt(selectedFrame_, layerIndex).pixels[pixelIndex];
        if (((color >> 24) & 0xffu) != 0u) {
            return color;
        }
    }
    return 0u;
}

void SpriteEditorPanel::paintStroke(int x0, int y0, int x1, int y1, bool useSecondaryColor)
{
    SpriteCel& cel = activeCel();
    const bool mirror = selectedTool_ == kToolMirror;
    const unsigned int color = selectedTool_ == kToolEraser ? 0u : currentPaletteColor(useSecondaryColor);
    drawLine(cel, x0, y0, x1, y1, color, mirror);
}

void SpriteEditorPanel::applyClickTool(int x, int y, bool useSecondaryColor)
{
    if (selectedTool_ == kToolBucket) {
        floodFill(activeCel(), x, y, currentPaletteColor(useSecondaryColor));
        return;
    }

    if (selectedTool_ == kToolPicker) {
        const unsigned int color = sampledVisibleColor(x, y);
        if (color == 0u) {
            return;
        }

        auto paletteIt = std::find(document_.palette.begin(), document_.palette.end(), color);
        if (paletteIt == document_.palette.end()) {
            document_.palette.push_back(color);
            paletteIt = std::prev(document_.palette.end());
        }

        const int paletteIndex = static_cast<int>(std::distance(document_.palette.begin(), paletteIt));
        if (useSecondaryColor) {
            secondaryColor_ = paletteIndex;
        } else {
            primaryColor_ = paletteIndex;
        }
        return;
    }

    if (selectedTool_ == kToolShade) {
        const int size = std::clamp(brushSize_, 1, 4);
        for (int offsetY = 0; offsetY < size; ++offsetY) {
            for (int offsetX = 0; offsetX < size; ++offsetX) {
                const int brushX = x + offsetX;
                const int brushY = y + offsetY;
                if (brushX < document_.canvasSize[0] && brushY < document_.canvasSize[1]) {
                    shadePixel(brushX, brushY, useSecondaryColor);
                }
            }
        }
    }
}

void SpriteEditorPanel::drawLine(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color, bool mirror)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        setBrushPixel(cel, x0, y0, color);
        if (mirror) {
            const int mirroredX = document_.canvasSize[0] - std::clamp(brushSize_, 1, 4) - x0;
            setBrushPixel(cel, mirroredX, y0, color);
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        const int twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void SpriteEditorPanel::drawRectangle(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color)
{
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int top = std::min(y0, y1);
    const int bottom = std::max(y0, y1);
    drawLine(cel, left, top, right, top, color, false);
    drawLine(cel, left, bottom, right, bottom, color, false);
    drawLine(cel, left, top, left, bottom, color, false);
    drawLine(cel, right, top, right, bottom, color, false);
}

void SpriteEditorPanel::drawCircle(SpriteCel& cel, int x0, int y0, int x1, int y1, unsigned int color)
{
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int top = std::min(y0, y1);
    const int bottom = std::max(y0, y1);

    const float centerX = (static_cast<float>(left + right)) * 0.5f;
    const float centerY = (static_cast<float>(top + bottom)) * 0.5f;
    const float radiusX = std::max(0.5f, static_cast<float>(right - left) * 0.5f);
    const float radiusY = std::max(0.5f, static_cast<float>(bottom - top) * 0.5f);
    const int steps = std::max(12, static_cast<int>((radiusX + radiusY) * 6.0f));

    for (int step = 0; step <= steps; ++step) {
        const float angle = (static_cast<float>(step) / static_cast<float>(steps)) * 2.0f * 3.14159265f;
        const int px = static_cast<int>(std::lround(centerX + std::cos(angle) * radiusX));
        const int py = static_cast<int>(std::lround(centerY + std::sin(angle) * radiusY));
        setBrushPixel(cel, px, py, color);
    }
}

void SpriteEditorPanel::floodFill(SpriteCel& cel, int startX, int startY, unsigned int replacementColor)
{
    const std::size_t startIndex = static_cast<std::size_t>(startY) * document_.canvasSize[0] + startX;
    const unsigned int targetColor = cel.pixels[startIndex];
    if (targetColor == replacementColor) {
        return;
    }

    std::vector<std::array<int, 2>> stack;
    stack.push_back({startX, startY});

    while (!stack.empty()) {
        const std::array<int, 2> point = stack.back();
        stack.pop_back();

        const int x = point[0];
        const int y = point[1];
        if (x < 0 || y < 0 || x >= document_.canvasSize[0] || y >= document_.canvasSize[1]) {
            continue;
        }

        const std::size_t index = static_cast<std::size_t>(y) * document_.canvasSize[0] + x;
        if (cel.pixels[index] != targetColor) {
            continue;
        }

        cel.pixels[index] = replacementColor;
        stack.push_back({x - 1, y});
        stack.push_back({x + 1, y});
        stack.push_back({x, y - 1});
        stack.push_back({x, y + 1});
    }
}

void SpriteEditorPanel::shadePixel(int x, int y, bool useSecondaryColor)
{
    SpriteCel& cel = activeCel();
    const std::size_t pixelIndex = static_cast<std::size_t>(y) * document_.canvasSize[0] + x;
    const unsigned int color = cel.pixels[pixelIndex];
    if (color == 0u) {
        return;
    }

    const auto paletteIt = std::find(document_.palette.begin(), document_.palette.end(), color);
    if (paletteIt == document_.palette.end()) {
        return;
    }

    const int paletteIndex = static_cast<int>(std::distance(document_.palette.begin(), paletteIt));
    const int delta = useSecondaryColor ? -1 : 1;
    const int nextIndex = std::clamp(paletteIndex + delta, 0, static_cast<int>(document_.palette.size()) - 1);
    cel.pixels[pixelIndex] = document_.palette[nextIndex];
}

void SpriteEditorPanel::drawSelectionOverlay(ImDrawList* drawList, ImVec2 canvasOrigin, float pixelSize) const
{
    if (!hasSelection_ || !selectionAppliesToActiveCel()) {
        if (!selectionDragActive_) {
            return;
        }
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    if (selectionDragActive_) {
        left = std::min(dragStartPixel_[0], selectionDragCurrent_[0]);
        top = std::min(dragStartPixel_[1], selectionDragCurrent_[1]);
        right = std::max(dragStartPixel_[0], selectionDragCurrent_[0]);
        bottom = std::max(dragStartPixel_[1], selectionDragCurrent_[1]);
    } else {
        left = selectionBounds_[0];
        top = selectionBounds_[1];
        right = selectionBounds_[2];
        bottom = selectionBounds_[3];
    }

    const ImVec2 min{
        canvasOrigin.x + static_cast<float>(left) * pixelSize,
        canvasOrigin.y + static_cast<float>(top) * pixelSize,
    };
    const ImVec2 max{
        canvasOrigin.x + static_cast<float>(right + 1) * pixelSize,
        canvasOrigin.y + static_cast<float>(bottom + 1) * pixelSize,
    };

    drawList->AddRectFilled(min, max, IM_COL32(90, 170, 255, 40));
    drawList->AddRect(min, max, IM_COL32(20, 20, 20, 255), 0.0f, 0, 3.0f);
    drawList->AddRect({min.x + 1.0f, min.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f}, IM_COL32(180, 225, 255, 255), 0.0f, 0, 1.5f);
}

void SpriteEditorPanel::clearSelection()
{
    hasSelection_ = false;
    selectionDragActive_ = false;
    moveDragActive_ = false;
    selectionBounds_ = {0, 0, 0, 0};
    selectionDragCurrent_ = {0, 0};
    moveDragOffset_ = {0, 0};
    moveSourcePixels_.clear();
}

bool SpriteEditorPanel::selectionAppliesToActiveCel() const
{
    return selectionFrame_ == selectedFrame_ && selectionLayer_ == selectedLayer_;
}

bool SpriteEditorPanel::pixelInSelection(int x, int y) const
{
    if (!hasSelection_ || !selectionAppliesToActiveCel()) {
        return false;
    }
    return x >= selectionBounds_[0] && x <= selectionBounds_[2] &&
        y >= selectionBounds_[1] && y <= selectionBounds_[3];
}

void SpriteEditorPanel::setSelectionFromDrag(int x0, int y0, int x1, int y1)
{
    selectionBounds_[0] = std::clamp(std::min(x0, x1), 0, document_.canvasSize[0] - 1);
    selectionBounds_[1] = std::clamp(std::min(y0, y1), 0, document_.canvasSize[1] - 1);
    selectionBounds_[2] = std::clamp(std::max(x0, x1), selectionBounds_[0], document_.canvasSize[0] - 1);
    selectionBounds_[3] = std::clamp(std::max(y0, y1), selectionBounds_[1], document_.canvasSize[1] - 1);
    selectionFrame_ = selectedFrame_;
    selectionLayer_ = selectedLayer_;
    hasSelection_ = true;
}

void SpriteEditorPanel::beginMoveDrag()
{
    recordUndoState();
    moveDragActive_ = true;
    selectionFrame_ = selectedFrame_;
    selectionLayer_ = selectedLayer_;
    moveSourcePixels_ = activeCel().pixels;
}

void SpriteEditorPanel::previewMoveSelection(int offsetX, int offsetY)
{
    if (!moveDragActive_ || moveSourcePixels_.empty()) {
        return;
    }

    moveDragOffset_ = {offsetX, offsetY};
    SpriteCel& cel = activeCel();
    cel.pixels = moveSourcePixels_;

    const int width = document_.canvasSize[0];
    const int height = document_.canvasSize[1];

    for (int y = selectionBounds_[1]; y <= selectionBounds_[3]; ++y) {
        for (int x = selectionBounds_[0]; x <= selectionBounds_[2]; ++x) {
            cel.pixels[static_cast<std::size_t>(y) * width + x] = 0u;
        }
    }

    for (int y = selectionBounds_[1]; y <= selectionBounds_[3]; ++y) {
        for (int x = selectionBounds_[0]; x <= selectionBounds_[2]; ++x) {
            const unsigned int color = moveSourcePixels_[static_cast<std::size_t>(y) * width + x];
            if (color == 0u) {
                continue;
            }

            const int dstX = x + offsetX;
            const int dstY = y + offsetY;
            if (dstX < 0 || dstY < 0 || dstX >= width || dstY >= height) {
                continue;
            }
            cel.pixels[static_cast<std::size_t>(dstY) * width + dstX] = color;
        }
    }
}

void SpriteEditorPanel::finalizeMoveSelection()
{
    if (!moveDragActive_) {
        return;
    }

    const int offsetX = moveDragOffset_[0];
    const int offsetY = moveDragOffset_[1];
    selectionBounds_[0] = std::clamp(selectionBounds_[0] + offsetX, 0, document_.canvasSize[0] - 1);
    selectionBounds_[1] = std::clamp(selectionBounds_[1] + offsetY, 0, document_.canvasSize[1] - 1);
    selectionBounds_[2] = std::clamp(selectionBounds_[2] + offsetX, selectionBounds_[0], document_.canvasSize[0] - 1);
    selectionBounds_[3] = std::clamp(selectionBounds_[3] + offsetY, selectionBounds_[1], document_.canvasSize[1] - 1);

    moveDragActive_ = false;
    moveDragOffset_ = {0, 0};
    moveSourcePixels_.clear();
}

void SpriteEditorPanel::duplicateCurrentFrame()
{
    if (document_.frames.empty() || selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.frames.size())) {
        return;
    }

    recordUndoState();
    const int insertIndex = selectedFrame_ + 1;
    document_.frames.insert(document_.frames.begin() + insertIndex, document_.frames[selectedFrame_]);
    document_.cels.insert(document_.cels.begin() + insertIndex, document_.cels[selectedFrame_]);
    selectedFrame_ = insertIndex;
}

void SpriteEditorPanel::deleteCurrentFrame()
{
    if (document_.frames.size() <= 1 || selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.frames.size())) {
        return;
    }

    recordUndoState();
    document_.frames.erase(document_.frames.begin() + selectedFrame_);
    document_.cels.erase(document_.cels.begin() + selectedFrame_);
    selectedFrame_ = std::clamp(selectedFrame_, 0, static_cast<int>(document_.frames.size()) - 1);
    if (selectionFrame_ == selectedFrame_) {
        clearSelection();
    } else if (selectionFrame_ > selectedFrame_) {
        --selectionFrame_;
    }
}

void SpriteEditorPanel::flipHorizontal()
{
    if (selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.cels.size())) {
        return;
    }

    recordUndoState();
    if (hasSelection_ && selectionAppliesToActiveCel()) {
        flipRegion(activeCel(), selectionBounds_[0], selectionBounds_[1], selectionBounds_[2], selectionBounds_[3], true);
        return;
    }

    for (SpriteCel& cel : document_.cels[selectedFrame_]) {
        flipRegion(cel, 0, 0, document_.canvasSize[0] - 1, document_.canvasSize[1] - 1, true);
    }
}

void SpriteEditorPanel::flipVertical()
{
    if (selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.cels.size())) {
        return;
    }

    recordUndoState();
    if (hasSelection_ && selectionAppliesToActiveCel()) {
        flipRegion(activeCel(), selectionBounds_[0], selectionBounds_[1], selectionBounds_[2], selectionBounds_[3], false);
        return;
    }

    for (SpriteCel& cel : document_.cels[selectedFrame_]) {
        flipRegion(cel, 0, 0, document_.canvasSize[0] - 1, document_.canvasSize[1] - 1, false);
    }
}

void SpriteEditorPanel::rotateClockwise()
{
    if (selectedFrame_ < 0 || selectedFrame_ >= static_cast<int>(document_.cels.size())) {
        return;
    }

    recordUndoState();
    if (hasSelection_ && selectionAppliesToActiveCel()) {
        rotateRegionClockwise(activeCel(), selectionBounds_[0], selectionBounds_[1], selectionBounds_[2], selectionBounds_[3]);
        return;
    }

    for (SpriteCel& cel : document_.cels[selectedFrame_]) {
        rotateRegionClockwise(cel, 0, 0, document_.canvasSize[0] - 1, document_.canvasSize[1] - 1);
    }
}

void SpriteEditorPanel::flipRegion(SpriteCel& cel, int left, int top, int right, int bottom, bool horizontal)
{
    const int width = document_.canvasSize[0];
    if (horizontal) {
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= left + (right - left) / 2; ++x) {
                const int mirroredX = right - (x - left);
                std::swap(
                    cel.pixels[static_cast<std::size_t>(y) * width + x],
                    cel.pixels[static_cast<std::size_t>(y) * width + mirroredX]);
            }
        }
        return;
    }

    for (int y = top; y <= top + (bottom - top) / 2; ++y) {
        const int mirroredY = bottom - (y - top);
        for (int x = left; x <= right; ++x) {
            std::swap(
                cel.pixels[static_cast<std::size_t>(y) * width + x],
                cel.pixels[static_cast<std::size_t>(mirroredY) * width + x]);
        }
    }
}

void SpriteEditorPanel::rotateRegionClockwise(SpriteCel& cel, int left, int top, int right, int bottom)
{
    const int canvasWidth = document_.canvasSize[0];
    const int regionWidth = right - left + 1;
    const int regionHeight = bottom - top + 1;
    if (regionWidth <= 0 || regionHeight <= 0) {
        return;
    }

    const std::vector<unsigned int> originalPixels = cel.pixels;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            cel.pixels[static_cast<std::size_t>(y) * canvasWidth + x] = 0u;
        }
    }

    for (int sourceY = 0; sourceY < regionHeight; ++sourceY) {
        for (int sourceX = 0; sourceX < regionWidth; ++sourceX) {
            const int destinationX = left + (regionHeight - 1 - sourceY);
            const int destinationY = top + sourceX;
            if (destinationX > right || destinationY > bottom) {
                continue;
            }

            cel.pixels[static_cast<std::size_t>(destinationY) * canvasWidth + destinationX] =
                originalPixels[static_cast<std::size_t>(top + sourceY) * canvasWidth + (left + sourceX)];
        }
    }
}

void SpriteEditorPanel::copySelection()
{
    if (!hasSelection_ || !selectionAppliesToActiveCel()) {
        return;
    }

    const int width = selectionBounds_[2] - selectionBounds_[0] + 1;
    const int height = selectionBounds_[3] - selectionBounds_[1] + 1;
    clipboard_.width = width;
    clipboard_.height = height;
    clipboard_.pixels.assign(static_cast<std::size_t>(width * height), 0u);

    const SpriteCel& cel = activeCel();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int srcX = selectionBounds_[0] + x;
            const int srcY = selectionBounds_[1] + y;
            clipboard_.pixels[static_cast<std::size_t>(y) * width + x] =
                cel.pixels[static_cast<std::size_t>(srcY) * document_.canvasSize[0] + srcX];
        }
    }
}

void SpriteEditorPanel::pasteSelection()
{
    if (clipboard_.width <= 0 || clipboard_.height <= 0 || clipboard_.pixels.empty()) {
        return;
    }

    SpriteCel& cel = activeCel();
    const int startX = hasSelection_ && selectionAppliesToActiveCel() ? selectionBounds_[0] : 0;
    const int startY = hasSelection_ && selectionAppliesToActiveCel() ? selectionBounds_[1] : 0;
    const int pasteWidth = std::min(clipboard_.width, document_.canvasSize[0] - startX);
    const int pasteHeight = std::min(clipboard_.height, document_.canvasSize[1] - startY);
    if (pasteWidth <= 0 || pasteHeight <= 0) {
        return;
    }

    recordUndoState();
    for (int y = 0; y < pasteHeight; ++y) {
        for (int x = 0; x < pasteWidth; ++x) {
            cel.pixels[static_cast<std::size_t>(startY + y) * document_.canvasSize[0] + (startX + x)] =
                clipboard_.pixels[static_cast<std::size_t>(y) * clipboard_.width + x];
        }
    }

    selectionBounds_[0] = startX;
    selectionBounds_[1] = startY;
    selectionBounds_[2] = startX + pasteWidth - 1;
    selectionBounds_[3] = startY + pasteHeight - 1;
    selectionFrame_ = selectedFrame_;
    selectionLayer_ = selectedLayer_;
    hasSelection_ = true;
}

void SpriteEditorPanel::resizeSelection(int newWidth, int newHeight)
{
    if (!hasSelection_ || !selectionAppliesToActiveCel()) {
        return;
    }

    newWidth = clampDimension(newWidth);
    newHeight = clampDimension(newHeight);

    const int srcLeft = selectionBounds_[0];
    const int srcTop = selectionBounds_[1];
    const int srcWidth = selectionBounds_[2] - selectionBounds_[0] + 1;
    const int srcHeight = selectionBounds_[3] - selectionBounds_[1] + 1;
    if (srcWidth <= 0 || srcHeight <= 0) {
        return;
    }

    const int dstWidth = std::min(newWidth, document_.canvasSize[0] - srcLeft);
    const int dstHeight = std::min(newHeight, document_.canvasSize[1] - srcTop);
    if (dstWidth <= 0 || dstHeight <= 0) {
        return;
    }

    SpriteCel& cel = activeCel();
    const std::vector<unsigned int> originalPixels = cel.pixels;
    std::vector<unsigned int> selectionPixels(static_cast<std::size_t>(srcWidth * srcHeight), 0u);

    for (int y = 0; y < srcHeight; ++y) {
        for (int x = 0; x < srcWidth; ++x) {
            selectionPixels[static_cast<std::size_t>(y) * srcWidth + x] =
                originalPixels[static_cast<std::size_t>(srcTop + y) * document_.canvasSize[0] + (srcLeft + x)];
        }
    }

    const int clearRight = std::min(document_.canvasSize[0] - 1, std::max(selectionBounds_[2], srcLeft + dstWidth - 1));
    const int clearBottom = std::min(document_.canvasSize[1] - 1, std::max(selectionBounds_[3], srcTop + dstHeight - 1));
    for (int y = srcTop; y <= clearBottom; ++y) {
        for (int x = srcLeft; x <= clearRight; ++x) {
            cel.pixels[static_cast<std::size_t>(y) * document_.canvasSize[0] + x] = 0u;
        }
    }

    for (int y = 0; y < dstHeight; ++y) {
        const int srcY = std::clamp((y * srcHeight) / std::max(dstHeight, 1), 0, srcHeight - 1);
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX = std::clamp((x * srcWidth) / std::max(dstWidth, 1), 0, srcWidth - 1);
            cel.pixels[static_cast<std::size_t>(srcTop + y) * document_.canvasSize[0] + (srcLeft + x)] =
                selectionPixels[static_cast<std::size_t>(srcY) * srcWidth + srcX];
        }
    }

    selectionBounds_[2] = srcLeft + dstWidth - 1;
    selectionBounds_[3] = srcTop + dstHeight - 1;
    resizeSelectionSize_ = {dstWidth, dstHeight};
}

} // namespace adventure::editor
