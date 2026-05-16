#pragma once

#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/path.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace adventure::game {

class Engine {
public:
    explicit Engine(std::filesystem::path projectRoot = ".");
    ~Engine();

    [[nodiscard]] bool initialize(const std::filesystem::path& chapterPath, std::string* errorMessage = nullptr);
    void run();

private:
    struct Texture {
        unsigned int id = 0;
        int width = 0;
        int height = 0;
    };

    struct RuntimePathEntity {
        EnemyPath path;
        float x = 0.0f;
        float y = 0.0f;
        std::size_t waypointIndex = 0;
    };

    enum class TransitionState {
        None,
        Sliding,
    };

    std::filesystem::path projectRoot_;
    GLFWwindow* window_ = nullptr;
    Chapter chapter_;
    const ChapterScreen* activeScreen_ = nullptr;
    TileMap activeMap_;
    Texture floorTexture_;
    Texture wallTexture_;
    std::vector<RuntimePathEntity> pathEntities_;
    float playerX_ = 0.0f;
    float playerY_ = 0.0f;
    float transitionTime_ = 0.0f;
    float transitionDuration_ = 0.4f;
    float transitionFromX_ = 0.0f;
    float transitionFromY_ = 0.0f;
    float transitionToX_ = 0.0f;
    float transitionToY_ = 0.0f;
    TransitionState transitionState_ = TransitionState::None;

    [[nodiscard]] bool loadScreen(const std::string& screenId, std::string* errorMessage);
    [[nodiscard]] bool loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage);
    void destroyTexture(Texture& texture);
    void loadPathEntities();
    void update(float dt);
    void updatePlayer(float dt);
    void updatePaths(float dt);
    void beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY);
    [[nodiscard]] bool playerCanOccupy(float x, float y) const;
    [[nodiscard]] bool solidAtPixel(float x, float y) const;
    [[nodiscard]] float screenWidthPx() const;
    [[nodiscard]] float screenHeightPx() const;
    void render();
    void renderTexture(const Texture& texture, float x, float y, float width, float height) const;
    void renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const;
};

} // namespace adventure::game
