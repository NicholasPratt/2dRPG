#include "game/engine.hpp"

#include "game/constants.hpp"

#include "stb_image.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <system_error>

namespace adventure::game {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 60.0f;
constexpr float kPlayerSpeedPxPerSecond = 96.0f;
constexpr float kPlayerSizePx = 12.0f;

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

std::filesystem::path assetPath(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    return root / relative;
}

} // namespace

Engine::Engine(std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot))
{
}

Engine::~Engine()
{
    destroyTexture(floorTexture_);
    destroyTexture(wallTexture_);
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Engine::initialize(const std::filesystem::path& chapterPath, std::string* errorMessage)
{
    if (!glfwInit()) {
        setError(errorMessage, "Failed to initialize GLFW.");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window_ = glfwCreateWindow(kScreenTilesW * kTileSize * 2, kScreenTilesH * kTileSize * 2, "Adventure Runtime", nullptr, nullptr);
    if (window_ == nullptr) {
        setError(errorMessage, "Failed to create runtime window.");
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    std::string error;
    if (!loadChapter(chapterPath, chapter_, &error)) {
        setError(errorMessage, "Failed to load chapter: " + error);
        return false;
    }

    if (!loadScreen(chapter_.startScreenId, &error)) {
        setError(errorMessage, error);
        return false;
    }
    playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
    playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
    return true;
}

void Engine::run()
{
    double currentTime = glfwGetTime();
    double accumulator = 0.0;

    while (window_ != nullptr && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        const double newTime = glfwGetTime();
        const double frameTime = std::min(0.25, newTime - currentTime);
        currentTime = newTime;
        accumulator += frameTime;

        while (accumulator >= kFixedStepSeconds) {
            update(kFixedStepSeconds);
            accumulator -= kFixedStepSeconds;
        }

        render();
        glfwSwapBuffers(window_);
    }
}

bool Engine::loadScreen(const std::string& screenId, std::string* errorMessage)
{
    const ChapterScreen* screen = findScreen(chapter_, screenId);
    if (screen == nullptr) {
        setError(errorMessage, "Screen not found: " + screenId);
        return false;
    }

    TileMap map;
    std::string error;
    const std::filesystem::path mapPath = assetPath(projectRoot_, "assets/game/maps") / (screen->mapId + ".admap");
    if (!loadTileMap(mapPath, map, &error)) {
        setError(errorMessage, "Failed to load map for screen " + screenId + ": " + error);
        return false;
    }

    activeScreen_ = screen;
    activeMap_ = std::move(map);
    destroyTexture(floorTexture_);
    destroyTexture(wallTexture_);

    const std::filesystem::path tilesetDir = assetPath(projectRoot_, "assets/game/tilesets");
    if (!loadTexture(tilesetDir / (screen->mapId + "_floor.png"), floorTexture_, nullptr)) {
        floorTexture_ = {};
    }
    if (!loadTexture(tilesetDir / (screen->mapId + "_wall.png"), wallTexture_, nullptr)) {
        wallTexture_ = {};
    }

    loadPathEntities();
    std::cout << "Loaded screen " << screen->id << " map " << activeMap_.id << "\n";
    return true;
}

bool Engine::loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        setError(errorMessage, "Failed to load texture: " + path.string());
        return false;
    }

    unsigned int glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    texture.id = glTexture;
    texture.width = width;
    texture.height = height;
    return true;
}

void Engine::destroyTexture(Texture& texture)
{
    if (texture.id != 0) {
        const unsigned int id = texture.id;
        glDeleteTextures(1, &id);
    }
    texture = {};
}

void Engine::loadPathEntities()
{
    pathEntities_.clear();
    const std::filesystem::path pathDir = assetPath(projectRoot_, "assets/game/paths");
    std::error_code ec;
    if (!std::filesystem::exists(pathDir, ec)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(pathDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".adpath") {
            continue;
        }
        EnemyPath path;
        if (!loadEnemyPath(entry.path(), path, nullptr) || activeScreen_ == nullptr ||
            path.mapId != activeScreen_->mapId || path.waypoints.empty()) {
            continue;
        }
        RuntimePathEntity entity;
        entity.path = std::move(path);
        entity.x = entity.path.waypoints.front().x;
        entity.y = entity.path.waypoints.front().y;
        entity.waypointIndex = entity.path.waypoints.size() > 1 ? 1u : 0u;
        pathEntities_.push_back(std::move(entity));
    }
}

void Engine::update(float dt)
{
    if (transitionState_ == TransitionState::Sliding) {
        transitionTime_ += dt;
        if (transitionTime_ >= transitionDuration_) {
            transitionState_ = TransitionState::None;
        }
        return;
    }

    updatePlayer(dt);
    updatePaths(dt);
}

void Engine::updatePlayer(float dt)
{
    float dx = 0.0f;
    float dy = 0.0f;
    if (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) { dx -= 1.0f; }
    if (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) { dx += 1.0f; }
    if (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) { dy -= 1.0f; }
    if (glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) { dy += 1.0f; }
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }

    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
    }

    const float newX = playerX_ + dx * kPlayerSpeedPxPerSecond * dt;
    const float newY = playerY_ + dy * kPlayerSpeedPxPerSecond * dt;
    if (playerCanOccupy(newX, playerY_)) {
        playerX_ = newX;
    }
    if (playerCanOccupy(playerX_, newY)) {
        playerY_ = newY;
    }

    const float width = screenWidthPx();
    const float height = screenHeightPx();
    if (activeScreen_ == nullptr) {
        return;
    }
    if (playerX_ < 0.0f && !activeScreen_->links.west.empty()) {
        beginScreenTransition(activeScreen_->links.west, width - kTileSize, playerY_, -width, 0.0f);
    } else if (playerX_ > width && !activeScreen_->links.east.empty()) {
        beginScreenTransition(activeScreen_->links.east, kTileSize, playerY_, width, 0.0f);
    } else if (playerY_ < 0.0f && !activeScreen_->links.north.empty()) {
        beginScreenTransition(activeScreen_->links.north, playerX_, height - kTileSize, 0.0f, -height);
    } else if (playerY_ > height && !activeScreen_->links.south.empty()) {
        beginScreenTransition(activeScreen_->links.south, playerX_, kTileSize, 0.0f, height);
    } else {
        playerX_ = std::clamp(playerX_, kPlayerSizePx * 0.5f, width - kPlayerSizePx * 0.5f);
        playerY_ = std::clamp(playerY_, kPlayerSizePx * 0.5f, height - kPlayerSizePx * 0.5f);
    }
}

void Engine::updatePaths(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.path.behavior == PathBehavior::Idle || entity.path.waypoints.empty()) {
            continue;
        }
        PathWaypoint target = entity.path.waypoints[entity.waypointIndex];
        const float dx = target.x - entity.x;
        const float dy = target.y - entity.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1.0f) {
            if (entity.waypointIndex + 1 < entity.path.waypoints.size()) {
                ++entity.waypointIndex;
            } else if (entity.path.loop) {
                entity.waypointIndex = 0;
            }
            continue;
        }
        const float step = std::min(distance, entity.path.speed * dt);
        entity.x += dx / distance * step;
        entity.y += dy / distance * step;
    }
}

void Engine::beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY)
{
    std::string error;
    if (!loadScreen(targetScreenId, &error)) {
        std::cerr << error << "\n";
        return;
    }
    playerX_ = std::clamp(spawnX, kPlayerSizePx * 0.5f, screenWidthPx() - kPlayerSizePx * 0.5f);
    playerY_ = std::clamp(spawnY, kPlayerSizePx * 0.5f, screenHeightPx() - kPlayerSizePx * 0.5f);
    transitionState_ = TransitionState::Sliding;
    transitionTime_ = 0.0f;
    transitionFromX_ = fromX;
    transitionFromY_ = fromY;
    transitionToX_ = 0.0f;
    transitionToY_ = 0.0f;
}

bool Engine::playerCanOccupy(float x, float y) const
{
    const float half = kPlayerSizePx * 0.5f;
    return !solidAtPixel(x - half, y - half) &&
        !solidAtPixel(x + half, y - half) &&
        !solidAtPixel(x - half, y + half) &&
        !solidAtPixel(x + half, y + half);
}

bool Engine::solidAtPixel(float x, float y) const
{
    if (x < 0.0f || y < 0.0f || x >= screenWidthPx() || y >= screenHeightPx()) {
        return false;
    }
    const int tileX = std::clamp(static_cast<int>(x) / kTileSize, 0, activeMap_.width - 1);
    const int tileY = std::clamp(static_cast<int>(y) / kTileSize, 0, activeMap_.height - 1);
    const std::size_t index = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(activeMap_.width) + static_cast<std::size_t>(tileX);
    return activeMap_.layers[1][index] != 0u;
}

float Engine::screenWidthPx() const
{
    return static_cast<float>(activeMap_.width * kTileSize);
}

float Engine::screenHeightPx() const
{
    return static_cast<float>(activeMap_.height * kTileSize);
}

void Engine::render()
{
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.03f, 0.035f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, screenWidthPx(), screenHeightPx(), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float tx = 0.0f;
    float ty = 0.0f;
    if (transitionState_ == TransitionState::Sliding) {
        const float t = std::clamp(transitionTime_ / transitionDuration_, 0.0f, 1.0f);
        tx = transitionFromX_ + (transitionToX_ - transitionFromX_) * t;
        ty = transitionFromY_ + (transitionToY_ - transitionFromY_) * t;
    }

    glPushMatrix();
    glTranslatef(tx, ty, 0.0f);

    if (floorTexture_.id != 0) {
        renderTexture(floorTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    } else {
        renderFilledRect(0.0f, 0.0f, screenWidthPx(), screenHeightPx(), 0.12f, 0.18f, 0.20f, 1.0f);
    }

    for (int y = 0; y < activeMap_.height; ++y) {
        for (int x = 0; x < activeMap_.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(activeMap_.width) + static_cast<std::size_t>(x);
            if (activeMap_.layers[1][index] != 0u) {
                renderFilledRect(static_cast<float>(x * kTileSize), static_cast<float>(y * kTileSize),
                    static_cast<float>(kTileSize), static_cast<float>(kTileSize), 0.95f, 0.82f, 0.20f, 0.22f);
            }
        }
    }

    for (const RuntimePathEntity& entity : pathEntities_) {
        renderFilledRect(entity.x - 4.0f, entity.y - 4.0f, 8.0f, 8.0f, 0.90f, 0.18f, 0.14f, 1.0f);
    }

    renderFilledRect(playerX_ - kPlayerSizePx * 0.5f, playerY_ - kPlayerSizePx * 0.5f,
        kPlayerSizePx, kPlayerSizePx, 0.20f, 0.62f, 1.0f, 1.0f);

    if (wallTexture_.id != 0) {
        renderTexture(wallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    }

    glPopMatrix();
}

void Engine::renderTexture(const Texture& texture, float x, float y, float width, float height) const
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + width, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + width, y + height);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void Engine::renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

} // namespace adventure::game
