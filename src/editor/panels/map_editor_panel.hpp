#pragma once

#include "editor/editor_context.hpp"

#include <array>
#include <string>
#include <vector>

namespace adventure::editor {

class MapEditorPanel {
public:
    void draw(EditorContext& context);

private:
    int width_ = 24;
    int height_ = 16;
    int tileSize_ = 16;
    int editMode_ = 0;
    int spawnX_ = 1;
    int spawnY_ = 1;
    bool testMode_ = false;
    float playerX_ = 16.0f;
    float playerY_ = 16.0f;
    std::array<char, 64> mapId_{'n', 'e', 'w', '_', 'm', 'a', 'p', '\0'};
    std::vector<unsigned char> walls_ = std::vector<unsigned char>(static_cast<std::size_t>(24 * 16), 0u);
    std::string status_;

    void resizeMap(int width, int height);
    void drawToolbar(EditorContext& context);
    void drawGrid();
    void drawTestGame();
    void startTestGame();
    void updateTestPlayer();
    [[nodiscard]] bool playerCanMoveTo(float x, float y) const;
    [[nodiscard]] bool wallAtPixel(float x, float y) const;
    void saveMap(EditorContext& context);
    void loadMap(EditorContext& context);
    [[nodiscard]] unsigned char& tileAt(int x, int y);
    [[nodiscard]] const unsigned char& tileAt(int x, int y) const;
};

} // namespace adventure::editor
