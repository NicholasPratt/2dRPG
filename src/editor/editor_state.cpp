#include "editor/editor_state.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace adventure::editor {
namespace {

void setError(std::string* out, const char* msg)
{
    if (out) { *out = msg; }
}

} // namespace

bool saveEditorState(const std::filesystem::path& path, const EditorState& state, std::string* errorMessage)
{
    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open editor state file for writing.");
        return false;
    }

    auto writeHex = [&](const std::vector<std::uint32_t>& data) {
        output << data.size();
        for (std::uint32_t v : data) {
            output << ' ' << std::hex << std::setfill('0') << std::setw(8) << v << std::dec;
        }
        output << '\n';
    };

    output << "ADEDITOR 3\n";
    output << "selectedScreen " << (state.selectedScreenId.empty() ? "-" : state.selectedScreenId) << "\n";
    output << "tilePalette " << state.tilePalette.size() << "\n";
    for (const TilePaletteEntry& e : state.tilePalette) {
        output << "entry " << (e.name.empty() ? "-" : e.name) << " " << e.widthPx << " " << e.heightPx
               << " " << e.frameDurationMs << " " << e.frames.size()
               << " " << (e.spriteId.empty() ? "-" : e.spriteId) << "\n";
        for (const TilePaletteFrame& frame : e.frames) {
            output << "frame\n";
            output << "floor ";
            writeHex(frame.floor);
            output << "wall ";
            writeHex(frame.wall);
        }
    }
    output << "paintPalette ";
    writeHex(state.paintPalette);
    output << "end\n";

    if (!output) {
        setError(errorMessage, "Failed while writing editor state file.");
        return false;
    }

    return true;
}

bool loadEditorState(const std::filesystem::path& path, EditorState& state, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open editor state file for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADEDITOR" || version < 1 || version > 3) {
        setError(errorMessage, "Unsupported editor state file type or version.");
        return false;
    }

    auto readHex = [&](std::vector<std::uint32_t>& data) -> bool {
        std::size_t count = 0;
        if (!(input >> count)) { return false; }
        data.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (!(input >> std::hex >> data[i] >> std::dec)) { return false; }
        }
        return true;
    };

    EditorState loaded;
    std::string key;

    if (!(input >> key) || key != "selectedScreen") {
        setError(errorMessage, "Expected selectedScreen key.");
        return false;
    }
    if (!(input >> loaded.selectedScreenId)) {
        setError(errorMessage, "Expected selectedScreen value.");
        return false;
    }
    if (loaded.selectedScreenId == "-") {
        loaded.selectedScreenId.clear();
    }

    std::size_t paletteCount = 0;
    if (!(input >> key) || key != "tilePalette" || !(input >> paletteCount)) {
        setError(errorMessage, "Expected tilePalette count.");
        return false;
    }

    for (std::size_t i = 0; i < paletteCount; ++i) {
        TilePaletteEntry e;
        if (!(input >> key) || key != "entry") {
            setError(errorMessage, "Expected entry keyword.");
            return false;
        }
        if (!(input >> e.name >> e.widthPx >> e.heightPx)) {
            setError(errorMessage, "Expected entry name and dimensions.");
            return false;
        }
        if (e.name == "-") { e.name.clear(); }

        if (version == 1) {
            TilePaletteFrame frame;
            if (!(input >> key) || key != "floor" || !readHex(frame.floor)) {
                setError(errorMessage, "Expected floor pixel data.");
                return false;
            }
            if (!(input >> key) || key != "wall" || !readHex(frame.wall)) {
                setError(errorMessage, "Expected wall pixel data.");
                return false;
            }
            e.frames.push_back(std::move(frame));
        } else {
            std::size_t frameCount = 0;
            if (!(input >> e.frameDurationMs >> frameCount) || frameCount == 0) {
                setError(errorMessage, "Expected animation frame data.");
                return false;
            }
            if (version >= 3) {
                if (!(input >> e.spriteId)) {
                    setError(errorMessage, "Expected entry spriteId.");
                    return false;
                }
                if (e.spriteId == "-") { e.spriteId.clear(); }
            }
            e.frames.reserve(frameCount);
            for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                TilePaletteFrame frame;
                if (!(input >> key) || key != "frame") {
                    setError(errorMessage, "Expected frame keyword.");
                    return false;
                }
                if (!(input >> key) || key != "floor" || !readHex(frame.floor)) {
                    setError(errorMessage, "Expected floor pixel data.");
                    return false;
                }
                if (!(input >> key) || key != "wall" || !readHex(frame.wall)) {
                    setError(errorMessage, "Expected wall pixel data.");
                    return false;
                }
                e.frames.push_back(std::move(frame));
            }
        }
        loaded.tilePalette.push_back(std::move(e));
    }

    if (!(input >> key) || key != "paintPalette" || !readHex(loaded.paintPalette)) {
        setError(errorMessage, "Expected paintPalette data.");
        return false;
    }

    state = std::move(loaded);
    return true;
}

} // namespace adventure::editor
