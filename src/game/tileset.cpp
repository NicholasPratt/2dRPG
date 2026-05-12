#include "game/tileset.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr int kMaxTileDimension = 256;
constexpr int kMaxTiles = 65536;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

// Minimal JSON helpers — hand-written to match the existing project style.

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else { out += c; }
    }
    return out;
}

// Advances past whitespace in the string view starting at pos.
void skipWhitespace(const std::string& s, std::size_t& pos)
{
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
}

// Reads past the next occurrence of ch, returning false if not found.
bool expect(const std::string& s, std::size_t& pos, char ch)
{
    skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != ch) {
        return false;
    }
    ++pos;
    return true;
}

// Reads a JSON string (assumes pos is just past the opening '"').
// On success pos is just past the closing '"'.
bool readJsonString(const std::string& s, std::size_t& pos, std::string& out)
{
    out.clear();
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') {
            return true;
        }
        if (c == '\\' && pos < s.size()) {
            char esc = s[pos++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += esc;  break;
            }
        } else {
            out += c;
        }
    }
    return false; // unterminated string
}

// Reads a JSON integer (decimal, optional leading '-').
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

// Reads a JSON boolean (true/false).
bool readJsonBool(const std::string& s, std::size_t& pos, bool& out)
{
    skipWhitespace(s, pos);
    if (s.compare(pos, 4, "true") == 0) {
        out = true;
        pos += 4;
        return true;
    }
    if (s.compare(pos, 5, "false") == 0) {
        out = false;
        pos += 5;
        return true;
    }
    return false;
}

// Reads the next JSON key (the part between '"' ... '":').
// Returns false if parsing fails.
bool readKey(const std::string& s, std::size_t& pos, std::string& key)
{
    skipWhitespace(s, pos);
    if (!expect(s, pos, '"')) return false;
    if (!readJsonString(s, pos, key)) return false;
    skipWhitespace(s, pos);
    if (!expect(s, pos, ':')) return false;
    return true;
}

} // namespace

const TileDef* TilesetDef::findTile(int tileId) const
{
    for (const TileDef& t : tiles) {
        if (t.id == tileId) return &t;
    }
    return nullptr;
}

bool saveTileset(const std::filesystem::path& path, const TilesetDef& tileset, std::string* errorMessage)
{
    if (tileset.id.empty()) {
        setError(errorMessage, "Tileset id must not be empty.");
        return false;
    }
    if (tileset.tileWidth <= 0 || tileset.tileWidth > kMaxTileDimension ||
        tileset.tileHeight <= 0 || tileset.tileHeight > kMaxTileDimension) {
        setError(errorMessage, "Tile dimensions are outside supported range (1–256).");
        return false;
    }
    if (static_cast<int>(tileset.tiles.size()) > kMaxTiles) {
        setError(errorMessage, "Too many tile definitions (max 65536).");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path);
    if (!out) {
        setError(errorMessage, "Could not open tileset file for writing.");
        return false;
    }

    out << "{\n";
    out << "  \"id\": \"" << jsonEscape(tileset.id) << "\",\n";
    out << "  \"source\": \"" << jsonEscape(tileset.sourcePath) << "\",\n";
    out << "  \"tileWidth\": " << tileset.tileWidth << ",\n";
    out << "  \"tileHeight\": " << tileset.tileHeight << ",\n";
    out << "  \"tiles\": [\n";

    for (std::size_t i = 0; i < tileset.tiles.size(); ++i) {
        const TileDef& t = tileset.tiles[i];
        bool last = (i + 1 == tileset.tiles.size());
        out << "    { \"id\": " << t.id
            << ", \"name\": \"" << jsonEscape(t.name) << "\""
            << ", \"solid\": " << (t.solid ? "true" : "false")
            << " }" << (last ? "" : ",") << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    if (!out) {
        setError(errorMessage, "Failed while writing tileset file.");
        return false;
    }

    return true;
}

bool loadTileset(const std::filesystem::path& path, TilesetDef& tileset, std::string* errorMessage)
{
    std::ifstream in(path);
    if (!in) {
        setError(errorMessage, "Could not open tileset file for reading.");
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t pos = 0;

    TilesetDef loaded;

    // Expect opening '{'
    if (!expect(content, pos, '{')) {
        setError(errorMessage, "Tileset file is not valid JSON (expected '{').");
        return false;
    }

    // Parse top-level key/value pairs until '}'.
    while (true) {
        skipWhitespace(content, pos);
        if (pos >= content.size()) {
            setError(errorMessage, "Unexpected end of tileset file.");
            return false;
        }
        if (content[pos] == '}') {
            ++pos;
            break;
        }
        // Optional comma between entries
        if (content[pos] == ',') {
            ++pos;
            continue;
        }

        std::string key;
        if (!readKey(content, pos, key)) {
            setError(errorMessage, "Failed to read tileset JSON key.");
            return false;
        }

        skipWhitespace(content, pos);

        if (key == "id") {
            if (!expect(content, pos, '"') || !readJsonString(content, pos, loaded.id)) {
                setError(errorMessage, "Failed to read tileset id.");
                return false;
            }
        } else if (key == "source") {
            if (!expect(content, pos, '"') || !readJsonString(content, pos, loaded.sourcePath)) {
                setError(errorMessage, "Failed to read tileset source path.");
                return false;
            }
        } else if (key == "tileWidth") {
            if (!readJsonInt(content, pos, loaded.tileWidth)) {
                setError(errorMessage, "Failed to read tileWidth.");
                return false;
            }
        } else if (key == "tileHeight") {
            if (!readJsonInt(content, pos, loaded.tileHeight)) {
                setError(errorMessage, "Failed to read tileHeight.");
                return false;
            }
        } else if (key == "tiles") {
            // Expect '[' then a sequence of tile objects.
            if (!expect(content, pos, '[')) {
                setError(errorMessage, "Expected '[' for tiles array.");
                return false;
            }
            while (true) {
                skipWhitespace(content, pos);
                if (pos >= content.size()) {
                    setError(errorMessage, "Unexpected end inside tiles array.");
                    return false;
                }
                if (content[pos] == ']') {
                    ++pos;
                    break;
                }
                if (content[pos] == ',') {
                    ++pos;
                    continue;
                }
                // Each tile is a JSON object: { "id": N, "name": "...", "solid": bool }
                if (!expect(content, pos, '{')) {
                    setError(errorMessage, "Expected '{' for tile object.");
                    return false;
                }

                TileDef tile;
                while (true) {
                    skipWhitespace(content, pos);
                    if (pos >= content.size()) {
                        setError(errorMessage, "Unexpected end inside tile object.");
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

                    std::string tkey;
                    if (!readKey(content, pos, tkey)) {
                        setError(errorMessage, "Failed to read tile object key.");
                        return false;
                    }
                    skipWhitespace(content, pos);

                    if (tkey == "id") {
                        if (!readJsonInt(content, pos, tile.id)) {
                            setError(errorMessage, "Failed to read tile id.");
                            return false;
                        }
                    } else if (tkey == "name") {
                        if (!expect(content, pos, '"') || !readJsonString(content, pos, tile.name)) {
                            setError(errorMessage, "Failed to read tile name.");
                            return false;
                        }
                    } else if (tkey == "solid") {
                        if (!readJsonBool(content, pos, tile.solid)) {
                            setError(errorMessage, "Failed to read tile solid flag.");
                            return false;
                        }
                    } else {
                        // Unknown key — skip the value (strings and primitives only for now).
                        if (content[pos] == '"') {
                            ++pos;
                            std::string ignored;
                            readJsonString(content, pos, ignored);
                        } else {
                            while (pos < content.size() && content[pos] != ',' && content[pos] != '}') {
                                ++pos;
                            }
                        }
                    }
                }

                if (static_cast<int>(loaded.tiles.size()) >= kMaxTiles) {
                    setError(errorMessage, "Too many tile definitions in tileset (max 65536).");
                    return false;
                }
                loaded.tiles.push_back(tile);
            }
        } else {
            // Unknown top-level key — skip its value.
            if (content[pos] == '"') {
                ++pos;
                std::string ignored;
                readJsonString(content, pos, ignored);
            } else {
                while (pos < content.size() && content[pos] != ',' && content[pos] != '}') {
                    ++pos;
                }
            }
        }
    }

    if (loaded.id.empty()) {
        setError(errorMessage, "Tileset id is missing.");
        return false;
    }
    if (loaded.tileWidth <= 0 || loaded.tileWidth > kMaxTileDimension ||
        loaded.tileHeight <= 0 || loaded.tileHeight > kMaxTileDimension) {
        setError(errorMessage, "Tile dimensions are outside supported range.");
        return false;
    }

    tileset = std::move(loaded);
    return true;
}

} // namespace adventure::game
