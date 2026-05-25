#include "game/engine.hpp"

#include "game/constants.hpp"
#include "game/project.hpp"

#include "stb_image.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <system_error>

namespace adventure::game {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 60.0f;
constexpr float kPlayerSpeedPxPerSecond = 96.0f;
constexpr float kPlayerSizePx = 12.0f;
constexpr float kScreenTransitionExitRatio = 0.30f;
constexpr float kHazardRespawnCooldownSeconds = 0.75f;
constexpr float kPlayerDamageInvulnerableSeconds = 0.65f;

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

PathWaypoint catmullPoint(const EnemyPath& path, int segment, float t)
{
    const int count = static_cast<int>(path.waypoints.size());
    const auto at = [&path, count](int index) -> const PathWaypoint& {
        if (path.loop) {
            index %= count;
            if (index < 0) {
                index += count;
            }
            return path.waypoints[static_cast<std::size_t>(index)];
        }
        return path.waypoints[static_cast<std::size_t>(std::clamp(index, 0, count - 1))];
    };
    const PathWaypoint& p0 = at(segment - 1);
    const PathWaypoint& p1 = at(segment);
    const PathWaypoint& p2 = at(segment + 1);
    const PathWaypoint& p3 = at(segment + 2);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
    };
}

float distance(PathWaypoint a, PathWaypoint b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

float approximatePathLength(const EnemyPath& path)
{
    if (path.waypoints.size() < 2) {
        return 0.0f;
    }
    float total = 0.0f;
    if (path.curveMode == PathCurveMode::Spline && path.waypoints.size() >= 3) {
        const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
        for (int s = 0; s < segments; ++s) {
            PathWaypoint prev = catmullPoint(path, s, 0.0f);
            for (int i = 1; i <= 12; ++i) {
                PathWaypoint next = catmullPoint(path, s, static_cast<float>(i) / 12.0f);
                total += distance(prev, next);
                prev = next;
            }
        }
        return total;
    }
    for (std::size_t i = 1; i < path.waypoints.size(); ++i) {
        total += distance(path.waypoints[i - 1], path.waypoints[i]);
    }
    if (path.loop) {
        total += distance(path.waypoints.back(), path.waypoints.front());
    }
    return total;
}

PathWaypoint pointAtDistance(const EnemyPath& path, float targetDistance)
{
    if (path.waypoints.empty()) {
        return {};
    }
    if (path.waypoints.size() == 1) {
        return path.waypoints.front();
    }
    const float totalLength = approximatePathLength(path);
    if (totalLength <= 0.0f) {
        return path.waypoints.front();
    }
    if (path.loop) {
        targetDistance = std::fmod(targetDistance, totalLength);
        if (targetDistance < 0.0f) {
            targetDistance += totalLength;
        }
    } else {
        targetDistance = std::clamp(targetDistance, 0.0f, totalLength);
    }

    float walked = 0.0f;
    if (path.curveMode == PathCurveMode::Spline && path.waypoints.size() >= 3) {
        const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
        for (int s = 0; s < segments; ++s) {
            PathWaypoint prev = catmullPoint(path, s, 0.0f);
            for (int i = 1; i <= 12; ++i) {
                PathWaypoint next = catmullPoint(path, s, static_cast<float>(i) / 12.0f);
                const float segLen = distance(prev, next);
                if (walked + segLen >= targetDistance) {
                    const float t = segLen > 0.0f ? (targetDistance - walked) / segLen : 0.0f;
                    return {prev.x + (next.x - prev.x) * t, prev.y + (next.y - prev.y) * t};
                }
                walked += segLen;
                prev = next;
            }
        }
        return path.loop ? path.waypoints.front() : path.waypoints.back();
    }

    const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
    for (int i = 0; i < segments; ++i) {
        const PathWaypoint a = path.waypoints[static_cast<std::size_t>(i)];
        const PathWaypoint b = path.waypoints[static_cast<std::size_t>((i + 1) % static_cast<int>(path.waypoints.size()))];
        const float segLen = distance(a, b);
        if (walked + segLen >= targetDistance) {
            const float t = segLen > 0.0f ? (targetDistance - walked) / segLen : 0.0f;
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        }
        walked += segLen;
    }
    return path.loop ? path.waypoints.front() : path.waypoints.back();
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
    destroyTexture(playerTexture_);
    for (auto& [id, sprite] : obstacleSprites_) {
        destroyTexture(sprite.texture);
    }
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

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    window_ = glfwCreateWindow(kScreenTilesW * kTileSize, kScreenTilesH * kTileSize, "Adventure Runtime", nullptr, nullptr);
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
    loadPlayableCharacter();
    const float centerX = screenWidthPx() * 0.5f;
    const float centerY = screenHeightPx() * 0.5f;
    if (playerCanOccupy(centerX, centerY)) {
        playerX_ = centerX;
        playerY_ = centerY;
    } else {
        playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
        playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
    }
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
    loadObstacleSprites();
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
    GameProject project;
    (void)loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr);

    auto typeForId = [&project](const std::string& id) -> const EnemyType* {
        for (const EnemyType& type : project.enemyTypes) {
            if (type.id == id) {
                return &type;
            }
        }
        return nullptr;
    };

    if (activeScreen_ != nullptr) {
        for (const EnemyPlacement& placement : activeScreen_->enemies) {
            if (placement.waypoints.empty()) {
                continue;
            }
            EnemyPath path;
            path.id = placement.id;
            path.enemyId = placement.typeId;
            path.mapId = activeScreen_->mapId;
            path.behavior = placement.behavior;
            path.curveMode = placement.curveMode;
            path.speed = placement.speedOverride;
            path.loop = placement.loop;
            path.respawn = placement.respawn;
            path.waypoints = placement.waypoints;
            if (const EnemyType* type = typeForId(placement.typeId)) {
                path.spriteId = type->spriteId;
                path.combat.maxHealth = type->maxHealth;
                path.combat.contactDamage = type->contactDamage;
                path.combat.hitboxWidth = type->hitboxWidth;
                path.combat.hitboxHeight = type->hitboxHeight;
                path.combat.attackCooldownSeconds = type->attackCooldownSeconds;
                if (path.speed <= 0.0f) {
                    path.speed = type->speed;
                }
            }
            RuntimePathEntity entity;
            entity.path = std::move(path);
            entity.x = entity.path.waypoints.front().x;
            entity.y = entity.path.waypoints.front().y;
            entity.health = std::max(1, entity.path.combat.maxHealth);
            entity.waypointIndex = entity.path.waypoints.size() > 1 ? 1u : 0u;
            pathEntities_.push_back(std::move(entity));
        }
    }

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
        entity.health = std::max(1, entity.path.combat.maxHealth);
        entity.waypointIndex = entity.path.waypoints.size() > 1 ? 1u : 0u;
        pathEntities_.push_back(std::move(entity));
    }
}

void Engine::loadObstacleSprites()
{
    for (auto& [id, sprite] : obstacleSprites_) {
        destroyTexture(sprite.texture);
    }
    obstacleSprites_.clear();

    const std::filesystem::path spriteDir = assetPath(projectRoot_, "assets/game/sprites");
    auto loadSprite = [&](const std::string& spriteId) {
        if (spriteId.empty() || obstacleSprites_.find(spriteId) != obstacleSprites_.end()) {
            return;
        }

        RuntimeSprite runtime;
        std::string error;
        const std::filesystem::path metadataPath = spriteDir / (spriteId + ".sprite.json");
        if (!loadSpriteMetadata(metadataPath, runtime.metadata, &error)) {
            obstacleSprites_[spriteId] = std::move(runtime);
            return;
        }

        std::filesystem::path sourcePath = runtime.metadata.source;
        if (sourcePath.empty()) {
            sourcePath = std::filesystem::path("assets/raw/sprites") / (spriteId + "_sheet.png");
        }
        sourcePath = sourcePath.is_absolute() ? sourcePath : projectRoot_ / sourcePath;
        runtime.loaded = loadTexture(sourcePath, runtime.texture, nullptr);
        obstacleSprites_[spriteId] = std::move(runtime);
    };

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        loadSprite(obstacle.spriteId);
    }
    for (const RuntimePathEntity& entity : pathEntities_) {
        loadSprite(entity.path.spriteId);
    }
}

void Engine::loadPlayableCharacter()
{
    destroyTexture(playerTexture_);
    const std::filesystem::path characterDir = assetPath(projectRoot_, "assets/game/characters");
    std::error_code ec;
    if (!std::filesystem::exists(characterDir, ec)) {
        return;
    }

    auto frameForCharacter = [&](const std::filesystem::path& path, bool* playableOut) -> std::filesystem::path {
        if (playableOut != nullptr) {
            *playableOut = false;
        }
        std::ifstream input(path);
        if (!input) {
            return {};
        }

        std::string key;
        input >> key;
        if (key != "ADCHARACTER") {
            return {};
        }
        int version = 0;
        input >> version;

        bool playable = false;
        std::filesystem::path idleFrame;
        std::filesystem::path firstFrame;
        while (input >> key) {
            if (key == "playable") {
                int value = 0;
                input >> value;
                playable = value != 0;
                if (playableOut != nullptr) {
                    *playableOut = playable;
                }
            } else if (key == "frame") {
                int frameIndex = 0;
                std::string state;
                std::string image;
                if (input >> frameIndex >> std::quoted(state) >> std::quoted(image)) {
                    if (firstFrame.empty()) {
                        firstFrame = image;
                    }
                    if (idleFrame.empty() && state == "idle") {
                        idleFrame = image;
                    }
                }
            } else if (key == "end") {
                break;
            } else if (key == "name" || key == "bio" || key == "sprite") {
                std::string ignored;
                input >> std::quoted(ignored);
            } else if (key == "animations" || key == "frames") {
                std::size_t ignored = 0;
                input >> ignored;
            } else if (key == "anim") {
                std::string ignoredA;
                std::string ignoredB;
                input >> std::quoted(ignoredA) >> std::quoted(ignoredB);
            }
        }

        return idleFrame.empty() ? firstFrame : idleFrame;
    };

    auto loadFrame = [&](const std::filesystem::path& frame) -> bool {
        if (frame.empty()) {
            return false;
        }
        const std::filesystem::path framePath = frame.is_absolute() ? frame : projectRoot_ / frame;
        return loadTexture(framePath, playerTexture_, nullptr);
    };

    if (!chapter_.playableCharacterId.empty()) {
        if (loadFrame(frameForCharacter(characterDir / (chapter_.playableCharacterId + ".adcharacter"), nullptr))) {
            return;
        }
    }

    GameProject project;
    if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr) &&
        !project.playableCharacterId.empty()) {
        if (loadFrame(frameForCharacter(characterDir / (project.playableCharacterId + ".adcharacter"), nullptr))) {
            return;
        }
    }

    std::filesystem::path fallbackFrame;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(characterDir, ec)) {
        if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".adcharacter") {
            continue;
        }

        bool playable = false;
        const std::filesystem::path selectedFrame = frameForCharacter(entry.path(), &playable);
        if (!selectedFrame.empty() && fallbackFrame.empty()) {
            fallbackFrame = selectedFrame;
        }
        if (playable && loadFrame(selectedFrame)) {
            return;
        }
    }

    (void)loadFrame(fallbackFrame);
}

void Engine::update(float dt)
{
    runtimeSeconds_ += dt;
    hazardCooldownSeconds_ = std::max(0.0f, hazardCooldownSeconds_ - dt);
    playerInvulnerableSeconds_ = std::max(0.0f, playerInvulnerableSeconds_ - dt);

    if (transitionState_ == TransitionState::Sliding) {
        transitionTime_ += dt;
        if (transitionTime_ >= transitionDuration_) {
            transitionState_ = TransitionState::None;
        }
        return;
    }

    updatePlayer(dt);
    updateHazards(dt);
    updateEnemyCombat(dt);
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

    const float half = kPlayerSizePx * 0.5f;
    const float exitDistance = kPlayerSizePx * kScreenTransitionExitRatio;
    const bool crossedWest = playerX_ - half <= -exitDistance;
    const bool crossedEast = playerX_ + half >= width + exitDistance;
    const bool crossedNorth = playerY_ - half <= -exitDistance;
    const bool crossedSouth = playerY_ + half >= height + exitDistance;

    bool transitioned = false;
    if (crossedWest && !activeScreen_->links.west.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.west, width - half, playerY_, -width, 0.0f);
    } else if (crossedEast && !activeScreen_->links.east.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.east, half, playerY_, width, 0.0f);
    } else if (crossedNorth && !activeScreen_->links.north.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.north, playerX_, height - half, 0.0f, -height);
    } else if (crossedSouth && !activeScreen_->links.south.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.south, playerX_, half, 0.0f, height);
    }

    if (transitioned) {
        return;
    }

    const bool canLeaveWest = !activeScreen_->links.west.empty();
    const bool canLeaveEast = !activeScreen_->links.east.empty();
    const bool canLeaveNorth = !activeScreen_->links.north.empty();
    const bool canLeaveSouth = !activeScreen_->links.south.empty();
    if (!canLeaveWest || crossedWest) {
        playerX_ = std::max(playerX_, half);
    }
    if (!canLeaveEast || crossedEast) {
        playerX_ = std::min(playerX_, width - half);
    }
    if (!canLeaveNorth || crossedNorth) {
        playerY_ = std::max(playerY_, half);
    }
    if (!canLeaveSouth || crossedSouth) {
        playerY_ = std::min(playerY_, height - half);
    }
}

void Engine::updatePaths(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.path.behavior == PathBehavior::Idle || entity.path.waypoints.empty()) {
            continue;
        }
        if (entity.path.curveMode == PathCurveMode::Spline && entity.path.waypoints.size() >= 3) {
            const float length = approximatePathLength(entity.path);
            if (length <= 0.0f) {
                continue;
            }
            entity.pathDistance += entity.path.speed * dt;
            if (entity.path.loop) {
                entity.pathDistance = std::fmod(entity.pathDistance, length);
            } else {
                entity.pathDistance = std::min(entity.pathDistance, length);
            }
            const PathWaypoint point = pointAtDistance(entity.path, entity.pathDistance);
            entity.x = point.x;
            entity.y = point.y;
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

void Engine::updateHazards(float)
{
    if (hazardCooldownSeconds_ > 0.0f) {
        return;
    }
    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        if (!obstacleIsActive(obstacle) || !playerOverlapsObstacle(obstacle)) {
            continue;
        }
        damagePlayer(1);
        hazardCooldownSeconds_ = kHazardRespawnCooldownSeconds;
        break;
    }
}

void Engine::updateEnemyCombat(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        entity.contactCooldownSeconds = std::max(0.0f, entity.contactCooldownSeconds - dt);
        if (entity.health <= 0 || entity.path.combat.contactDamage <= 0 ||
            entity.contactCooldownSeconds > 0.0f || !playerOverlapsEnemy(entity)) {
            continue;
        }
        damagePlayer(entity.path.combat.contactDamage);
        entity.contactCooldownSeconds = entity.path.combat.attackCooldownSeconds;
    }
}

bool Engine::beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY)
{
    const ChapterScreen* targetScreen = findScreen(chapter_, targetScreenId);
    if (targetScreen == nullptr) {
        std::cerr << "Screen not found: " << targetScreenId << "\n";
        return false;
    }

    TileMap targetMap;
    std::string error;
    const std::filesystem::path mapPath = assetPath(projectRoot_, "assets/game/maps") / (targetScreen->mapId + ".admap");
    if (!loadTileMap(mapPath, targetMap, &error)) {
        std::cerr << "Failed to load map for screen " << targetScreenId << ": " << error << "\n";
        return false;
    }

    const float half = kPlayerSizePx * 0.5f;
    const float targetWidth = static_cast<float>(targetMap.width * kTileSize);
    const float targetHeight = static_cast<float>(targetMap.height * kTileSize);
    spawnX = std::clamp(spawnX, half, targetWidth - half);
    spawnY = std::clamp(spawnY, half, targetHeight - half);
    if (!playerCanOccupyInMap(targetMap, spawnX, spawnY)) {
        return false;
    }

    if (!loadScreen(targetScreenId, &error)) {
        std::cerr << error << "\n";
        return false;
    }
    playerX_ = spawnX;
    playerY_ = spawnY;
    transitionState_ = TransitionState::Sliding;
    transitionTime_ = 0.0f;
    transitionFromX_ = fromX;
    transitionFromY_ = fromY;
    transitionToX_ = 0.0f;
    transitionToY_ = 0.0f;
    return true;
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
    return solidAtPixelInMap(activeMap_, x, y);
}

bool Engine::playerCanOccupyInMap(const TileMap& map, float x, float y) const
{
    const float half = kPlayerSizePx * 0.5f;
    return !solidAtPixelInMap(map, x - half, y - half) &&
        !solidAtPixelInMap(map, x + half, y - half) &&
        !solidAtPixelInMap(map, x - half, y + half) &&
        !solidAtPixelInMap(map, x + half, y + half);
}

bool Engine::solidAtPixelInMap(const TileMap& map, float x, float y) const
{
    const float width = static_cast<float>(map.width * kTileSize);
    const float height = static_cast<float>(map.height * kTileSize);
    if (x < 0.0f || y < 0.0f || x >= width || y >= height) {
        return false;
    }
    const int tileX = std::clamp(static_cast<int>(x) / kTileSize, 0, map.width - 1);
    const int tileY = std::clamp(static_cast<int>(y) / kTileSize, 0, map.height - 1);
    const std::size_t index = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(tileX);
    return map.layers[1][index] != 0u;
}

bool Engine::obstacleIsActive(const MapObstacle& obstacle) const
{
    if (obstacle.type != ObstacleType::TimedSpike) {
        return true;
    }
    const float cycle = std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds);
    const float t = std::fmod(runtimeSeconds_ + obstacle.phaseSeconds, cycle);
    return t < obstacle.activeSeconds;
}

bool Engine::playerOverlapsObstacle(const MapObstacle& obstacle) const
{
    const float half = kPlayerSizePx * 0.5f;
    const float playerMinX = playerX_ - half;
    const float playerMinY = playerY_ - half;
    const float playerMaxX = playerX_ + half;
    const float playerMaxY = playerY_ + half;
    const float obstacleMinX = static_cast<float>(obstacle.x * kTileSize);
    const float obstacleMinY = static_cast<float>(obstacle.y * kTileSize);
    const float obstacleMaxX = static_cast<float>((obstacle.x + obstacle.width) * kTileSize);
    const float obstacleMaxY = static_cast<float>((obstacle.y + obstacle.height) * kTileSize);
    return playerMaxX > obstacleMinX && playerMaxY > obstacleMinY &&
        playerMinX < obstacleMaxX && playerMinY < obstacleMaxY;
}

bool Engine::playerOverlapsEnemy(const RuntimePathEntity& entity) const
{
    const float half = kPlayerSizePx * 0.5f;
    const float playerMinX = playerX_ - half;
    const float playerMinY = playerY_ - half;
    const float playerMaxX = playerX_ + half;
    const float playerMaxY = playerY_ + half;
    const float enemyHalfW = entity.path.combat.hitboxWidth * 0.5f;
    const float enemyHalfH = entity.path.combat.hitboxHeight * 0.5f;
    const float enemyMinX = entity.x - enemyHalfW;
    const float enemyMinY = entity.y - enemyHalfH;
    const float enemyMaxX = entity.x + enemyHalfW;
    const float enemyMaxY = entity.y + enemyHalfH;
    return playerMaxX > enemyMinX && playerMaxY > enemyMinY &&
        playerMinX < enemyMaxX && playerMinY < enemyMaxY;
}

void Engine::damagePlayer(int amount)
{
    if (amount <= 0 || playerInvulnerableSeconds_ > 0.0f) {
        return;
    }
    playerHealth_ = std::max(0, playerHealth_ - amount);
    playerInvulnerableSeconds_ = kPlayerDamageInvulnerableSeconds;
    if (playerHealth_ <= 0) {
        respawnPlayerAtMapSpawn();
    }
}

void Engine::respawnPlayerAtMapSpawn()
{
    playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
    playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
    playerHealth_ = playerMaxHealth_;
    playerInvulnerableSeconds_ = kPlayerDamageInvulnerableSeconds;
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
        auto spriteIt = obstacleSprites_.find(entity.path.spriteId);
        if (spriteIt != obstacleSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = obstacleSpriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                renderTextureRegion(spriteIt->second.texture, entity.x - drawW * 0.5f, entity.y - drawH * 0.5f, drawW, drawH, u0, v0, u1, v1);
                continue;
            }
        }
        renderFilledRect(entity.x - 4.0f, entity.y - 4.0f, 8.0f, 8.0f, 0.90f, 0.18f, 0.14f, 1.0f);
    }

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        const bool active = obstacleIsActive(obstacle);
        const float x = static_cast<float>(obstacle.x * kTileSize);
        const float y = static_cast<float>(obstacle.y * kTileSize);
        const float w = static_cast<float>(obstacle.width * kTileSize);
        const float h = static_cast<float>(obstacle.height * kTileSize);

        auto spriteIt = obstacleSprites_.find(obstacle.spriteId);
        if (spriteIt != obstacleSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = obstacleSpriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, x, y, w, h, u0, v0, u1, v1);
                if (!active) {
                    renderFilledRect(x, y, w, h, 0.05f, 0.08f, 0.10f, 0.45f);
                }
                continue;
            }
        }

        float r = 0.90f;
        float g = 0.12f;
        float b = 0.16f;
        float a = active ? 0.42f : 0.18f;
        if (obstacle.type == ObstacleType::Pit) {
            r = 0.02f; g = 0.02f; b = 0.03f; a = 0.70f;
        } else if (obstacle.type == ObstacleType::TimedSpike) {
            r = active ? 1.0f : 0.20f;
            g = active ? 0.62f : 0.50f;
            b = active ? 0.10f : 0.80f;
        }
        renderFilledRect(x, y, w, h, r, g, b, a);
    }

    if (playerTexture_.id != 0) {
        const float drawW = static_cast<float>(playerTexture_.width);
        const float drawH = static_cast<float>(playerTexture_.height);
        renderTexture(playerTexture_, playerX_ - drawW * 0.5f, playerY_ - drawH * 0.5f, drawW, drawH);
    } else {
        renderFilledRect(playerX_ - kPlayerSizePx * 0.5f, playerY_ - kPlayerSizePx * 0.5f,
            kPlayerSizePx, kPlayerSizePx, 0.20f, 0.62f, 1.0f, 1.0f);
    }

    if (wallTexture_.id != 0) {
        renderTexture(wallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    }

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (entity.path.combat.maxHealth <= 1) {
            continue;
        }
        const float barW = std::max(8.0f, entity.path.combat.hitboxWidth);
        const float barH = 2.0f;
        const float pct = std::clamp(static_cast<float>(entity.health) / static_cast<float>(entity.path.combat.maxHealth), 0.0f, 1.0f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW, barH, 0.08f, 0.08f, 0.08f, 0.85f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW * pct, barH, 0.95f, 0.20f, 0.16f, 0.95f);
    }

    const float heartW = 8.0f;
    for (int i = 0; i < playerMaxHealth_; ++i) {
        const bool filled = i < playerHealth_;
        renderFilledRect(8.0f + static_cast<float>(i) * (heartW + 2.0f), 8.0f, heartW, 6.0f,
            filled ? 0.90f : 0.16f, filled ? 0.08f : 0.08f, filled ? 0.12f : 0.09f, 0.95f);
    }

    glPopMatrix();
}

void Engine::renderTexture(const Texture& texture, float x, float y, float width, float height) const
{
    renderTextureRegion(texture, x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f);
}

void Engine::renderTextureRegion(const Texture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1) const
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + width, y);
    glTexCoord2f(u1, v1); glVertex2f(x + width, y + height);
    glTexCoord2f(u0, v1); glVertex2f(x, y + height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

const SpriteFrameDef* Engine::obstacleSpriteFrame(const RuntimeSprite& sprite) const
{
    if (sprite.metadata.frames.empty()) {
        return nullptr;
    }

    const int totalMs = std::accumulate(sprite.metadata.frames.begin(), sprite.metadata.frames.end(), 0, [](int total, const SpriteFrameDef& frame) {
        return total + std::max(1, frame.durationMs);
    });
    if (totalMs <= 0) {
        return &sprite.metadata.frames.front();
    }

    int t = static_cast<int>(std::fmod(runtimeSeconds_ * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef& frame : sprite.metadata.frames) {
        t -= std::max(1, frame.durationMs);
        if (t < 0) {
            return &frame;
        }
    }
    return &sprite.metadata.frames.back();
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
