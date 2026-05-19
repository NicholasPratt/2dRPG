#include "game/sprite.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

std::string jsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '"') { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else { out += c; }
    }
    return out;
}

void skipWhitespace(const std::string& s, std::size_t& pos)
{
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
}

bool expect(const std::string& s, std::size_t& pos, char expected)
{
    skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != expected) {
        return false;
    }
    ++pos;
    return true;
}

bool readJsonString(const std::string& s, std::size_t& pos, std::string& out)
{
    out.clear();
    while (pos < s.size()) {
        const char c = s[pos++];
        if (c == '"') {
            return true;
        }
        if (c == '\\' && pos < s.size()) {
            const char esc = s[pos++];
            if (esc == '"') { out += '"'; }
            else if (esc == '\\') { out += '\\'; }
            else if (esc == 'n') { out += '\n'; }
            else if (esc == 'r') { out += '\r'; }
            else if (esc == 't') { out += '\t'; }
            else { out += esc; }
        } else {
            out += c;
        }
    }
    return false;
}

bool readJsonInt(const std::string& s, std::size_t& pos, int& out)
{
    skipWhitespace(s, pos);
    const char* begin = s.data() + pos;
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc{}) {
        return false;
    }
    pos += static_cast<std::size_t>(ptr - begin);
    return true;
}

bool readKey(const std::string& s, std::size_t& pos, std::string& key)
{
    if (!expect(s, pos, '"')) {
        return false;
    }
    if (!readJsonString(s, pos, key)) {
        return false;
    }
    return expect(s, pos, ':');
}

void skipJsonValue(const std::string& s, std::size_t& pos)
{
    skipWhitespace(s, pos);
    if (pos >= s.size()) {
        return;
    }
    if (s[pos] == '"') {
        ++pos;
        std::string ignored;
        (void)readJsonString(s, pos, ignored);
        return;
    }
    if (s[pos] == '{') {
        int depth = 0;
        do {
            if (s[pos] == '"') {
                ++pos;
                std::string ignored;
                (void)readJsonString(s, pos, ignored);
                continue;
            }
            if (s[pos] == '{') { ++depth; }
            if (s[pos] == '}') { --depth; }
            ++pos;
        } while (pos < s.size() && depth > 0);
        return;
    }
    if (s[pos] == '[') {
        int depth = 0;
        do {
            if (s[pos] == '"') {
                ++pos;
                std::string ignored;
                (void)readJsonString(s, pos, ignored);
                continue;
            }
            if (s[pos] == '[') { ++depth; }
            if (s[pos] == ']') { --depth; }
            ++pos;
        } while (pos < s.size() && depth > 0);
        return;
    }
    while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']') {
        ++pos;
    }
}

bool readIntArray2(const std::string& s, std::size_t& pos, std::array<int, 2>& out)
{
    if (!expect(s, pos, '[')) {
        return false;
    }
    if (!readJsonInt(s, pos, out[0])) {
        return false;
    }
    if (!expect(s, pos, ',')) {
        return false;
    }
    if (!readJsonInt(s, pos, out[1])) {
        return false;
    }
    return expect(s, pos, ']');
}

bool readFrame(const std::string& s, std::size_t& pos, SpriteFrameDef& frame)
{
    if (!expect(s, pos, '{')) {
        return false;
    }
    while (true) {
        skipWhitespace(s, pos);
        if (pos >= s.size()) {
            return false;
        }
        if (s[pos] == '}') {
            ++pos;
            return true;
        }
        if (s[pos] == ',') {
            ++pos;
            continue;
        }
        std::string key;
        if (!readKey(s, pos, key)) {
            return false;
        }
        if (key == "rect") {
            if (!expect(s, pos, '[')) {
                return false;
            }
            if (!readJsonInt(s, pos, frame.x) || !expect(s, pos, ',') ||
                !readJsonInt(s, pos, frame.y) || !expect(s, pos, ',') ||
                !readJsonInt(s, pos, frame.width) || !expect(s, pos, ',') ||
                !readJsonInt(s, pos, frame.height) || !expect(s, pos, ']')) {
                return false;
            }
        } else if (key == "durationMs") {
            if (!readJsonInt(s, pos, frame.durationMs)) {
                return false;
            }
        } else if (key == "type") {
            if (!expect(s, pos, '"') || !readJsonString(s, pos, frame.type)) {
                return false;
            }
        } else {
            skipJsonValue(s, pos);
        }
    }
}

bool readFrames(const std::string& s, std::size_t& pos, std::vector<SpriteFrameDef>& frames)
{
    if (!expect(s, pos, '[')) {
        return false;
    }
    frames.clear();
    while (true) {
        skipWhitespace(s, pos);
        if (pos >= s.size()) {
            return false;
        }
        if (s[pos] == ']') {
            ++pos;
            return true;
        }
        if (s[pos] == ',') {
            ++pos;
            continue;
        }
        SpriteFrameDef frame;
        if (!readFrame(s, pos, frame)) {
            return false;
        }
        frames.push_back(frame);
    }
}

bool readTags(const std::string& s, std::size_t& pos, std::vector<std::string>& tags)
{
    if (!expect(s, pos, '[')) {
        return false;
    }
    tags.clear();
    while (true) {
        skipWhitespace(s, pos);
        if (pos >= s.size()) {
            return false;
        }
        if (s[pos] == ']') {
            ++pos;
            return true;
        }
        if (s[pos] == ',') {
            ++pos;
            continue;
        }
        std::string tag;
        if (!expect(s, pos, '"') || !readJsonString(s, pos, tag)) {
            return false;
        }
        tags.push_back(tag);
    }
}

} // namespace

bool saveSpriteMetadata(const std::filesystem::path& path, const SpriteMetadata& metadata, std::string* errorMessage)
{
    if (metadata.id.empty()) {
        setError(errorMessage, "Sprite id must not be empty.");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open sprite metadata for writing.");
        return false;
    }

    output << "{\n";
    output << "  \"id\": \"" << jsonEscape(metadata.id) << "\",\n";
    output << "  \"source\": \"" << jsonEscape(metadata.source.generic_string()) << "\",\n";
    output << "  \"canvasSize\": [" << metadata.canvasSize[0] << ", " << metadata.canvasSize[1] << "],\n";
    output << "  \"gridSize\": [" << metadata.gridSize[0] << ", " << metadata.gridSize[1] << "],\n";
    output << "  \"pivot\": [" << metadata.pivot[0] << ", " << metadata.pivot[1] << "],\n";
    output << "  \"frames\": [\n";
    for (std::size_t i = 0; i < metadata.frames.size(); ++i) {
        const SpriteFrameDef& frame = metadata.frames[i];
        output << "    {\"rect\": [" << frame.x << ", " << frame.y << ", " << frame.width << ", " << frame.height
               << "], \"durationMs\": " << frame.durationMs << ", \"type\": \"" << jsonEscape(frame.type) << "\"}";
        output << (i + 1 == metadata.frames.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"tags\": [";
    for (std::size_t i = 0; i < metadata.tags.size(); ++i) {
        output << "\"" << jsonEscape(metadata.tags[i]) << "\"" << (i + 1 == metadata.tags.size() ? "" : ", ");
    }
    output << "]\n";
    output << "}\n";

    if (!output) {
        setError(errorMessage, "Failed while writing sprite metadata.");
        return false;
    }
    return true;
}

bool loadSpriteMetadata(const std::filesystem::path& path, SpriteMetadata& metadata, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open sprite metadata for reading.");
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::size_t pos = 0;
    SpriteMetadata loaded;

    if (!expect(content, pos, '{')) {
        setError(errorMessage, "Sprite metadata is not valid JSON.");
        return false;
    }

    while (true) {
        skipWhitespace(content, pos);
        if (pos >= content.size()) {
            setError(errorMessage, "Unexpected end of sprite metadata.");
            return false;
        }
        if (content[pos] == '}') {
            ++pos;
            break;
        }
        if (content[pos] == ',') {
            ++pos;
            continue;
        }

        std::string key;
        if (!readKey(content, pos, key)) {
            setError(errorMessage, "Failed to read sprite metadata key.");
            return false;
        }

        if (key == "id") {
            if (!expect(content, pos, '"') || !readJsonString(content, pos, loaded.id)) {
                setError(errorMessage, "Failed to read sprite id.");
                return false;
            }
        } else if (key == "source") {
            std::string source;
            if (!expect(content, pos, '"') || !readJsonString(content, pos, source)) {
                setError(errorMessage, "Failed to read sprite source.");
                return false;
            }
            loaded.source = source;
        } else if (key == "canvasSize") {
            if (!readIntArray2(content, pos, loaded.canvasSize)) {
                setError(errorMessage, "Failed to read canvasSize.");
                return false;
            }
        } else if (key == "gridSize") {
            if (!readIntArray2(content, pos, loaded.gridSize)) {
                setError(errorMessage, "Failed to read gridSize.");
                return false;
            }
        } else if (key == "pivot") {
            if (!readIntArray2(content, pos, loaded.pivot)) {
                setError(errorMessage, "Failed to read pivot.");
                return false;
            }
        } else if (key == "frames") {
            if (!readFrames(content, pos, loaded.frames)) {
                setError(errorMessage, "Failed to read frames.");
                return false;
            }
        } else if (key == "tags") {
            if (!readTags(content, pos, loaded.tags)) {
                setError(errorMessage, "Failed to read tags.");
                return false;
            }
        } else {
            skipJsonValue(content, pos);
        }
    }

    if (loaded.frames.empty()) {
        loaded.frames.push_back({});
    }
    if (loaded.tags.empty()) {
        loaded.tags.push_back("idle");
    }
    for (SpriteFrameDef& frame : loaded.frames) {
        if (frame.type.empty()) {
            frame.type = loaded.tags.empty() ? "idle" : loaded.tags.front();
        }
    }
    loaded.canvasSize[0] = std::max(1, loaded.canvasSize[0]);
    loaded.canvasSize[1] = std::max(1, loaded.canvasSize[1]);
    loaded.gridSize[0] = std::max(1, loaded.gridSize[0]);
    loaded.gridSize[1] = std::max(1, loaded.gridSize[1]);

    metadata = std::move(loaded);
    return true;
}

} // namespace adventure::game
