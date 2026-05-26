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
constexpr float kPlayerCollisionSizePx = 14.0f;
constexpr float kPlayerFallbackDrawSizePx = 32.0f;
constexpr float kScreenTransitionExitRatio = 0.30f;
constexpr float kHazardRespawnCooldownSeconds = 0.75f;
constexpr float kPlayerDamageInvulnerableSeconds = 0.65f;
constexpr float kMeleeActiveSeconds = 0.15f;
constexpr float kMeleeAttackSeconds = 0.28f;
constexpr float kRangedAttackSeconds = 0.22f;
constexpr float kAttackMoveSpeedScale = 0.55f;
constexpr float kItemPickupRadius = 12.0f;
constexpr float kProjectileHalfSize = 4.0f;
constexpr float kEnemyDeathVisualSeconds = 0.35f;

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
    destroyTexture(prevFloorTexture_);
    destroyTexture(prevWallTexture_);
    destroyTexture(playerSprite_.texture);
    for (auto& [id, sprite] : loadedSprites_) {
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
    loadPlayableCharacter();
    loadWeapons();
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
    loadNpcEntities();
    loadItemEntities();
    projectiles_.clear();
    interactionState_ = InteractionState::None;
    interactingNpcIndex_ = -1;
    dialogueLineIndex_ = 0;
    loadAllSprites();
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

void Engine::loadWeapons()
{
    meleeWeapon_.reset();
    rangedWeapon_.reset();
    ammo_.clear();

    GameProject project;
    if (!loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
        return;
    }

    if (project.startingWeaponId.empty()) {
        return;
    }

    for (const WeaponDef& w : project.weaponDefs) {
        if (w.id == project.startingWeaponId) {
            if (w.type == WeaponType::Melee) {
                meleeWeapon_ = w;
                loadSpriteById(w.spriteId);
            } else {
                rangedWeapon_ = w;
                const std::string ammoKey = w.ammoTypeId.empty() ? w.id : w.ammoTypeId;
                ammo_[ammoKey] = 0;
                loadSpriteById(w.spriteId);
            }
            break;
        }
    }
}

void Engine::loadItemEntities()
{
    itemEntities_.clear();
    for (const MapItemPlacement& placement : activeMap_.items) {
        RuntimeItemEntity entity;
        entity.placement = placement;
        itemEntities_.push_back(entity);
    }
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
            if (activeScreen_ != nullptr &&
                defeatedEnemies_.count(activeScreen_->id + "/" + entity.path.id) > 0) {
                continue;
            }
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
        if (activeScreen_ != nullptr &&
            defeatedEnemies_.count(activeScreen_->id + "/" + entity.path.id) > 0) {
            continue;
        }
        pathEntities_.push_back(std::move(entity));
    }
}

void Engine::loadSpriteById(const std::string& spriteId)
{
    if (spriteId.empty() || loadedSprites_.find(spriteId) != loadedSprites_.end()) {
        return;
    }
    const std::filesystem::path spriteDir = assetPath(projectRoot_, "assets/game/sprites");
    RuntimeSprite runtime;
    std::string error;
    const std::filesystem::path metadataPath = spriteDir / (spriteId + ".sprite.json");
    if (!loadSpriteMetadata(metadataPath, runtime.metadata, &error)) {
        loadedSprites_[spriteId] = std::move(runtime);
        return;
    }
    std::filesystem::path sourcePath = runtime.metadata.source;
    if (sourcePath.empty()) {
        sourcePath = std::filesystem::path("assets/raw/sprites") / (spriteId + "_sheet.png");
    }
    sourcePath = sourcePath.is_absolute() ? sourcePath : projectRoot_ / sourcePath;
    runtime.loaded = loadTexture(sourcePath, runtime.texture, nullptr);
    loadedSprites_[spriteId] = std::move(runtime);
}

void Engine::loadAllSprites()
{
    for (auto& [id, sprite] : loadedSprites_) {
        destroyTexture(sprite.texture);
    }
    loadedSprites_.clear();

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        loadSpriteById(obstacle.spriteId);
    }
    for (const RuntimePathEntity& entity : pathEntities_) {
        loadSpriteById(entity.path.spriteId);
    }
    for (const RuntimeNpcEntity& npc : npcEntities_) {
        loadSpriteById(npc.spriteId);
    }
    for (const RuntimeItemEntity& item : itemEntities_) {
        loadSpriteById(item.placement.spriteId);
    }
    if (meleeWeapon_.has_value()) {
        loadSpriteById(meleeWeapon_->spriteId);
    }
    if (rangedWeapon_.has_value()) {
        loadSpriteById(rangedWeapon_->spriteId);
    }
}

void Engine::loadPlayableCharacter()
{
    destroyTexture(playerSprite_.texture);
    playerSprite_ = RuntimeSprite{};

    const std::filesystem::path characterDir = assetPath(projectRoot_, "assets/game/characters");
    std::error_code ec;
    if (!std::filesystem::exists(characterDir, ec)) {
        return;
    }

    auto loadCharacterSprite = [&](const std::filesystem::path& charPath) -> bool {
        std::ifstream input(charPath);
        if (!input) return false;

        std::string key;
        input >> key;
        if (key != "ADCHARACTER") return false;
        int version = 0;
        input >> version;

        bool playable = false;
        std::filesystem::path spritePath;
        while (input >> key) {
            if (key == "playable") {
                int v = 0;
                input >> v;
                playable = v != 0;
            } else if (key == "sprite") {
                std::string p;
                input >> std::quoted(p);
                spritePath = p;
            } else if (key == "end") {
                break;
            } else if (key == "name" || key == "bio") {
                std::string ignored;
                input >> std::quoted(ignored);
            } else if (key == "anim") {
                std::string a, b;
                input >> std::quoted(a) >> std::quoted(b);
            } else if (key == "frame") {
                int idx = 0;
                std::string st, img;
                input >> idx >> std::quoted(st) >> std::quoted(img);
            } else if (key == "animations" || key == "frames") {
                std::size_t ignored = 0;
                input >> ignored;
            }
        }

        if (!playable || spritePath.empty()) return false;

        const std::filesystem::path fullPath = spritePath.is_absolute() ? spritePath : projectRoot_ / spritePath;
        std::string error;
        if (!loadSpriteMetadata(fullPath, playerSprite_.metadata, &error)) return false;

        std::filesystem::path sourcePath = playerSprite_.metadata.source;
        if (sourcePath.empty()) {
            sourcePath = std::filesystem::path("assets/raw/sprites") / (playerSprite_.metadata.id + "_sheet.png");
        }
        sourcePath = sourcePath.is_absolute() ? sourcePath : projectRoot_ / sourcePath;
        playerSprite_.loaded = loadTexture(sourcePath, playerSprite_.texture, nullptr);
        return playerSprite_.loaded;
    };

    if (!chapter_.playableCharacterId.empty()) {
        if (loadCharacterSprite(characterDir / (chapter_.playableCharacterId + ".adcharacter"))) return;
    }

    GameProject project;
    if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr) &&
        !project.playableCharacterId.empty()) {
        if (loadCharacterSprite(characterDir / (project.playableCharacterId + ".adcharacter"))) return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(characterDir, ec)) {
        if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".adcharacter") continue;
        if (loadCharacterSprite(entry.path())) return;
    }
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
            destroyTexture(prevFloorTexture_);
            destroyTexture(prevWallTexture_);
        }
        return;
    }

    updatePlayer(dt);
    updateAttack(dt);
    updateProjectiles(dt);
    updateHazards(dt);
    updateEnemyCombat(dt);
    updateEnemyDeaths(dt);
    updatePaths(dt);
    updateNpcs(dt);
    updateInteraction();
    updateItemPickups();
}

void Engine::updatePlayer(float dt)
{
    if (interactionState_ == InteractionState::InDialogue) {
        if (playerActionType_ != "idle") {
            playerActionType_ = "idle";
            playerAnimSeconds_ = 0.0f;
        }
        playerIsMoving_ = false;
        return;
    }

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
        playerFacingX_ = dx;
        playerFacingY_ = dy;
    }

    if (!playerActionLocksBaseMotion()) {
        setPlayerActionState(len > 0.0f ? PlayerActionState::Walk : PlayerActionState::Idle);
    }
    const std::string newAction = playerActionName();
    if (newAction != playerActionType_) {
        playerActionType_ = newAction;
        playerAnimSeconds_ = 0.0f;
    }

    playerAnimSeconds_ += dt;
    playerIsMoving_ = (len > 0.0f);

    const float speedScale = playerActionLocksBaseMotion() ? kAttackMoveSpeedScale : 1.0f;
    const float newX = playerX_ + dx * kPlayerSpeedPxPerSecond * speedScale * dt;
    const float newY = playerY_ + dy * kPlayerSpeedPxPerSecond * speedScale * dt;
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

    const float half = kPlayerCollisionSizePx * 0.5f;
    const float exitDistance = kPlayerCollisionSizePx * kScreenTransitionExitRatio;
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

void Engine::updateAttack(float dt)
{
    if (interactionState_ == InteractionState::InDialogue) {
        return;
    }
    meleeCooldownSeconds_ = std::max(0.0f, meleeCooldownSeconds_ - dt);
    rangedCooldownSeconds_ = std::max(0.0f, rangedCooldownSeconds_ - dt);
    meleeActiveSeconds_ = std::max(0.0f, meleeActiveSeconds_ - dt);

    if (playerActionSeconds_ > 0.0f) {
        playerActionSeconds_ = std::max(0.0f, playerActionSeconds_ - dt);
        if (playerActionSeconds_ <= 0.0f &&
            (playerActionState_ == PlayerActionState::MeleeAttack ||
                playerActionState_ == PlayerActionState::RangedAttack ||
                playerActionState_ == PlayerActionState::Hurt)) {
            setPlayerActionState(playerIsMoving_ ? PlayerActionState::Walk : PlayerActionState::Idle);
        }
    }

    const bool meleeInputDown = glfwGetKey(window_, GLFW_KEY_Z) == GLFW_PRESS;
    const bool rangedInputDown = glfwGetKey(window_, GLFW_KEY_X) == GLFW_PRESS;
    const bool meleePressed = meleeInputDown && !meleeInputWasDown_;
    const bool rangedPressed = rangedInputDown && !rangedInputWasDown_;
    meleeInputWasDown_ = meleeInputDown;
    rangedInputWasDown_ = rangedInputDown;

    if (meleeWeapon_.has_value() && meleePressed && meleeCooldownSeconds_ <= 0.0f &&
        !playerActionLocksBaseMotion()) {
        setPlayerActionState(PlayerActionState::MeleeAttack, kMeleeAttackSeconds);
        meleeCooldownSeconds_ = meleeWeapon_->attackCooldown;
        meleeActiveSeconds_ = kMeleeActiveSeconds;
        meleeHitApplied_ = false;
    }

    if (playerActionState_ == PlayerActionState::MeleeAttack && !meleeHitApplied_) {
        checkMeleeHits();
        meleeHitApplied_ = true;
    }

    if (rangedWeapon_.has_value() && rangedPressed && rangedCooldownSeconds_ <= 0.0f &&
        !playerActionLocksBaseMotion()) {
        const std::string ammoKey = rangedWeapon_->ammoTypeId.empty() ? rangedWeapon_->id : rangedWeapon_->ammoTypeId;
        const int available = ammo_.count(ammoKey) ? ammo_.at(ammoKey) : 0;
        if (available >= rangedWeapon_->ammoPerShot) {
            setPlayerActionState(PlayerActionState::RangedAttack, kRangedAttackSeconds);
            ammo_[ammoKey] -= rangedWeapon_->ammoPerShot;
            rangedCooldownSeconds_ = rangedWeapon_->attackCooldown;
            RuntimeProjectile proj;
            const float offset = kPlayerCollisionSizePx * 0.5f + 2.0f;
            proj.x = playerX_ + playerFacingX_ * offset;
            proj.y = playerY_ + playerFacingY_ * offset;
            proj.vx = playerFacingX_ * rangedWeapon_->projectileSpeed;
            proj.vy = playerFacingY_ * rangedWeapon_->projectileSpeed;
            proj.maxDistance = rangedWeapon_->range;
            proj.damage = rangedWeapon_->damage;
            proj.spriteId = rangedWeapon_->spriteId;
            projectiles_.push_back(proj);
        }
    }
}

void Engine::setPlayerActionState(PlayerActionState state, float durationSeconds)
{
    if (playerActionState_ == state && playerActionSeconds_ == durationSeconds) {
        return;
    }
    playerActionState_ = state;
    playerActionSeconds_ = std::max(0.0f, durationSeconds);

    const std::string actionName = playerActionName();
    if (playerActionType_ != actionName) {
        playerActionType_ = actionName;
        playerAnimSeconds_ = 0.0f;
    }
}

bool Engine::playerActionLocksBaseMotion() const
{
    return playerActionState_ == PlayerActionState::MeleeAttack ||
        playerActionState_ == PlayerActionState::RangedAttack ||
        playerActionState_ == PlayerActionState::Hurt ||
        playerActionState_ == PlayerActionState::Dead;
}

std::string Engine::playerActionName() const
{
    switch (playerActionState_) {
        case PlayerActionState::Walk:
            return "walk";
        case PlayerActionState::MeleeAttack:
            return "attack_1";
        case PlayerActionState::RangedAttack:
            return "cast";
        case PlayerActionState::Hurt:
            return "hit_react";
        case PlayerActionState::Dead:
            return "death";
        case PlayerActionState::Idle:
        default:
            return "idle";
    }
}

void Engine::checkMeleeHits()
{
    if (!meleeWeapon_.has_value()) {
        return;
    }
    const float reach = meleeWeapon_->range;
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.health <= 0) {
            continue;
        }
        const float ex = entity.x - playerX_;
        const float ey = entity.y - playerY_;
        const float dist = std::sqrt(ex * ex + ey * ey);
        if (dist > reach) {
            continue;
        }
        const float dot = (dist > 0.0f) ? (ex * playerFacingX_ + ey * playerFacingY_) / dist : 1.0f;
        if (dot < 0.3f) {
            continue;
        }
        entity.health = std::max(0, entity.health - meleeWeapon_->damage);
        if (entity.health <= 0 && entity.deathSeconds < 0.0f) {
            entity.deathSeconds = 0.0f;
        }
    }
}

void Engine::updateProjectiles(float dt)
{
    for (RuntimeProjectile& proj : projectiles_) {
        if (proj.dead) {
            continue;
        }
        const float step = std::sqrt(proj.vx * proj.vx + proj.vy * proj.vy) * dt;
        proj.x += proj.vx * dt;
        proj.y += proj.vy * dt;
        proj.distanceTraveled += step;

        if (proj.distanceTraveled >= proj.maxDistance) {
            proj.dead = true;
            continue;
        }

        if (solidAtPixel(proj.x, proj.y)) {
            proj.dead = true;
            continue;
        }

        for (RuntimePathEntity& entity : pathEntities_) {
            if (entity.health <= 0) {
                continue;
            }
            const float halfW = entity.path.combat.hitboxWidth * 0.5f;
            const float halfH = entity.path.combat.hitboxHeight * 0.5f;
            if (proj.x + kProjectileHalfSize > entity.x - halfW &&
                proj.x - kProjectileHalfSize < entity.x + halfW &&
                proj.y + kProjectileHalfSize > entity.y - halfH &&
                proj.y - kProjectileHalfSize < entity.y + halfH) {
                entity.health = std::max(0, entity.health - proj.damage);
                if (entity.health <= 0 && entity.deathSeconds < 0.0f) {
                    entity.deathSeconds = 0.0f;
                }
                proj.dead = true;
                break;
            }
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(), [](const RuntimeProjectile& p) { return p.dead; }),
        projectiles_.end());
}

void Engine::updateItemPickups()
{
    for (RuntimeItemEntity& item : itemEntities_) {
        if (item.collected) {
            continue;
        }
        if (playerOverlapsItem(item)) {
            collectItem(item);
        }
    }
}

bool Engine::playerOverlapsItem(const RuntimeItemEntity& item) const
{
    const float dx = item.placement.x - playerX_;
    const float dy = item.placement.y - playerY_;
    return std::sqrt(dx * dx + dy * dy) <= kItemPickupRadius;
}

void Engine::collectItem(RuntimeItemEntity& item)
{
    item.collected = true;
    switch (item.placement.pickupType) {
        case ItemPickupType::Weapon: {
            GameProject project;
            if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
                for (const WeaponDef& w : project.weaponDefs) {
                    if (w.id == item.placement.targetId) {
                        if (w.type == WeaponType::Melee) {
                            meleeWeapon_ = w;
                        } else {
                            rangedWeapon_ = w;
                            const std::string key = w.ammoTypeId.empty() ? w.id : w.ammoTypeId;
                            if (ammo_.find(key) == ammo_.end()) {
                                ammo_[key] = 0;
                            }
                        }
                        gameState_.giveItem(w.id);
                        loadSpriteById(w.spriteId);
                        break;
                    }
                }
            }
            break;
        }
        case ItemPickupType::Ammo: {
            const std::string& key = item.placement.targetId;
            if (!key.empty()) {
                ammo_[key] += item.placement.quantity;
            }
            break;
        }
        case ItemPickupType::Health:
            playerHealth_ = std::min(playerMaxHealth_, playerHealth_ + item.placement.quantity);
            break;
    }
}

void Engine::loadNpcEntities()
{
    npcEntities_.clear();
    if (activeScreen_ == nullptr) {
        return;
    }

    GameProject project;
    (void)loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr);

    auto typeForId = [&project](const std::string& id) -> const NpcTypeDef* {
        for (const NpcTypeDef& npc : project.npcTypes) {
            if (npc.id == id) {
                return &npc;
            }
        }
        return nullptr;
    };

    for (const NpcPlacement& placement : activeScreen_->npcs) {
        RuntimeNpcEntity entity;
        entity.placement = placement;
        entity.x = placement.x;
        entity.y = placement.y;
        if (const NpcTypeDef* type = typeForId(placement.typeId)) {
            entity.spriteId = type->spriteId;
            if (placement.speedOverride <= 0.0f) {
                entity.placement.speedOverride = type->defaultSpeed;
            }
            if (placement.movementOverride == NpcMovementMode::Stationary &&
                type->defaultMovement != NpcMovementMode::Stationary) {
                entity.placement.movementOverride = type->defaultMovement;
            }
            if (placement.dialogueOverride.empty()) {
                entity.dialogue = type->defaultDialogue;
            } else {
                entity.dialogue = placement.dialogueOverride;
            }
        } else if (!placement.dialogueOverride.empty()) {
            entity.dialogue = placement.dialogueOverride;
        }
        if (!placement.waypoints.empty()) {
            entity.x = placement.waypoints.front().x;
            entity.y = placement.waypoints.front().y;
            entity.waypointIndex = placement.waypoints.size() > 1 ? 1u : 0u;
        }
        npcEntities_.push_back(std::move(entity));
    }
}

void Engine::updateNpcs(float dt)
{
    updateNpcAwareness();
    for (RuntimeNpcEntity& npc : npcEntities_) {
        npc.animSeconds += dt;
        if (npc.playerInAwareness) {
            npc.actionType = "idle";
            continue;
        }
        if (npc.placement.movementOverride != NpcMovementMode::Patrol || npc.placement.waypoints.size() < 2) {
            npc.actionType = "idle";
            continue;
        }

        if (npc.atWaypoint) {
            if (npc.waitRemainingSeconds > 0.0f) {
                npc.waitRemainingSeconds -= dt;
                continue;
            }
            npc.atWaypoint = false;
            if (npc.waypointIndex + 1 < npc.placement.waypoints.size()) {
                ++npc.waypointIndex;
            } else if (npc.placement.loop) {
                npc.waypointIndex = 0;
            } else {
                npc.actionType = "idle";
                continue;
            }
        }

        npc.actionType = "walk";
        const PathWaypoint& target = npc.placement.waypoints[npc.waypointIndex];
        const float dx = target.x - npc.x;
        const float dy = target.y - npc.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 1.0f) {
            npc.x = target.x;
            npc.y = target.y;
            npc.atWaypoint = true;
            npc.waitRemainingSeconds = target.waitSeconds;
            npc.actionType = target.animState.empty() ? "idle" : target.animState;
            continue;
        }
        const float segSpeed = target.speedOverride > 0.0f ? target.speedOverride
            : (npc.placement.speedOverride > 0.0f ? npc.placement.speedOverride : 32.0f);
        const float step = std::min(dist, segSpeed * dt);
        npc.x += dx / dist * step;
        npc.y += dy / dist * step;
    }
}

void Engine::updateNpcAwareness()
{
    for (RuntimeNpcEntity& npc : npcEntities_) {
        const float dx = playerX_ - npc.x;
        const float dy = playerY_ - npc.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        npc.playerInAwareness = dist <= npc.placement.awarenessRadius;
    }
}

void Engine::updateInteraction()
{
    const bool interactDown = glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS;
    const bool interactPressed = interactDown && !interactInputWasDown_;
    interactInputWasDown_ = interactDown;

    switch (interactionState_) {
        case InteractionState::None: {
            int nearest = -1;
            float nearestDist = 99999.0f;
            for (int i = 0; i < static_cast<int>(npcEntities_.size()); ++i) {
                const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(i)];
                if (npc.dialogue.empty()) {
                    continue;
                }
                const float dx = playerX_ - npc.x;
                const float dy = playerY_ - npc.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= npc.placement.interactionRadius && dist < nearestDist) {
                    nearest = i;
                    nearestDist = dist;
                }
            }
            if (nearest >= 0) {
                interactionState_ = InteractionState::PromptVisible;
                interactingNpcIndex_ = nearest;
            }
            break;
        }
        case InteractionState::PromptVisible: {
            if (interactingNpcIndex_ < 0 ||
                interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
                interactionState_ = InteractionState::None;
                break;
            }
            const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
            const float dx = playerX_ - npc.x;
            const float dy = playerY_ - npc.y;
            if (std::sqrt(dx * dx + dy * dy) > npc.placement.interactionRadius) {
                interactionState_ = InteractionState::None;
                interactingNpcIndex_ = -1;
                break;
            }
            if (interactPressed) {
                interactionState_ = InteractionState::InDialogue;
                dialogueLineIndex_ = 0;
            }
            break;
        }
        case InteractionState::InDialogue: {
            if (interactingNpcIndex_ < 0 ||
                interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
                interactionState_ = InteractionState::None;
                break;
            }
            if (interactPressed) {
                ++dialogueLineIndex_;
                const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
                if (dialogueLineIndex_ >= static_cast<int>(npc.dialogue.size())) {
                    interactionState_ = InteractionState::None;
                    interactingNpcIndex_ = -1;
                    dialogueLineIndex_ = 0;
                }
            }
            break;
        }
    }
}

void Engine::renderNpcs() const
{
    for (const RuntimeNpcEntity& npc : npcEntities_) {
        auto spriteIt = loadedSprites_.find(npc.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                renderTextureRegion(spriteIt->second.texture, npc.x - drawW * 0.5f, npc.y - drawH * 0.5f, drawW, drawH, u0, v0, u1, v1);
                continue;
            }
        }
        renderFilledRect(npc.x - 6.0f, npc.y - 6.0f, 12.0f, 12.0f, 0.10f, 0.75f, 0.72f, 0.90f);
    }
}

void Engine::renderInteractionPrompt() const
{
    if (interactionState_ != InteractionState::PromptVisible || interactingNpcIndex_ < 0 ||
        interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const float pulse = 0.55f + 0.45f * std::sin(runtimeSeconds_ * 5.0f);
    const float size = 6.0f;
    renderFilledRect(npc.x - size * 0.5f, npc.y - 22.0f, size, size, 0.25f, 0.90f, 0.35f, pulse);
}

void Engine::renderDialogueBox() const
{
    if (interactionState_ != InteractionState::InDialogue || interactingNpcIndex_ < 0 ||
        interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    if (dialogueLineIndex_ < 0 || dialogueLineIndex_ >= static_cast<int>(npc.dialogue.size())) {
        return;
    }

    const float sw = screenWidthPx();
    const float sh = screenHeightPx();
    const float boxH = 56.0f;
    const float margin = 10.0f;
    const float boxY = sh - boxH - margin;

    renderFilledRect(margin, boxY, sw - margin * 2.0f, boxH, 0.04f, 0.05f, 0.07f, 0.92f);
    renderFilledRect(margin, boxY, sw - margin * 2.0f, 2.0f, 0.30f, 0.70f, 0.90f, 0.80f);

    if (!npc.dialogue[static_cast<std::size_t>(dialogueLineIndex_)].speaker.empty()) {
        renderFilledRect(margin + 6.0f, boxY + 6.0f, 48.0f, 6.0f, 0.30f, 0.70f, 0.90f, 0.85f);
    }

    const float textY = boxY + 18.0f;
    const int textLen = static_cast<int>(npc.dialogue[static_cast<std::size_t>(dialogueLineIndex_)].text.size());
    const float barW = std::min(static_cast<float>(textLen) * 3.0f, sw - margin * 4.0f);
    renderFilledRect(margin + 6.0f, textY, barW, 4.0f, 0.75f, 0.78f, 0.82f, 0.65f);

    const int total = static_cast<int>(npc.dialogue.size());
    for (int i = 0; i < total; ++i) {
        const float dotX = sw - margin - 8.0f - static_cast<float>(total - 1 - i) * 8.0f;
        const float dotY = boxY + boxH - 12.0f;
        const bool current = i == dialogueLineIndex_;
        renderFilledRect(dotX, dotY, 4.0f, 4.0f,
            current ? 0.90f : 0.40f, current ? 0.90f : 0.40f, current ? 0.90f : 0.40f,
            current ? 0.95f : 0.50f);
    }
}

void Engine::updatePaths(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.deathSeconds >= 0.0f || entity.path.behavior == PathBehavior::Idle || entity.path.waypoints.empty()) {
            continue;
        }
        if (entity.atWaypoint) {
            if (entity.waitRemainingSeconds > 0.0f) {
                entity.waitRemainingSeconds -= dt;
                continue;
            }
            entity.atWaypoint = false;
            if (entity.waypointIndex + 1 < entity.path.waypoints.size()) {
                ++entity.waypointIndex;
            } else if (entity.path.loop) {
                entity.waypointIndex = 0;
            } else {
                continue;
            }
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
        const PathWaypoint& target = entity.path.waypoints[entity.waypointIndex];
        const float dx = target.x - entity.x;
        const float dy = target.y - entity.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 1.0f) {
            entity.x = target.x;
            entity.y = target.y;
            entity.atWaypoint = true;
            entity.waitRemainingSeconds = target.waitSeconds;
            continue;
        }
        const float segSpeed = target.speedOverride > 0.0f ? target.speedOverride : entity.path.speed;
        const float step = std::min(dist, segSpeed * dt);
        entity.x += dx / dist * step;
        entity.y += dy / dist * step;
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

void Engine::updateEnemyDeaths(float dt)
{
    bool anyRemoved = false;
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.deathSeconds < 0.0f) {
            continue;
        }
        entity.deathSeconds += dt;
        if (entity.deathSeconds >= kEnemyDeathVisualSeconds && activeScreen_ != nullptr) {
            if (!entity.path.respawn && !activeScreen_->respawnEnemies) {
                recordEnemyDefeated(activeScreen_->id, entity);
            }
            anyRemoved = true;
        }
    }
    if (anyRemoved) {
        pathEntities_.erase(
            std::remove_if(pathEntities_.begin(), pathEntities_.end(),
                [](const RuntimePathEntity& e) {
                    return e.deathSeconds >= kEnemyDeathVisualSeconds;
                }),
            pathEntities_.end());
    }
}

void Engine::recordEnemyDefeated(const std::string& screenId, const RuntimePathEntity& entity)
{
    defeatedEnemies_.insert(screenId + "/" + entity.path.id);
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

    const float half = kPlayerCollisionSizePx * 0.5f;
    const float targetWidth = static_cast<float>(targetMap.width * kTileSize);
    const float targetHeight = static_cast<float>(targetMap.height * kTileSize);
    spawnX = std::clamp(spawnX, half, targetWidth - half);
    spawnY = std::clamp(spawnY, half, targetHeight - half);
    if (!playerCanOccupyInMap(targetMap, spawnX, spawnY)) {
        return false;
    }

    destroyTexture(prevFloorTexture_);
    destroyTexture(prevWallTexture_);
    prevFloorTexture_ = floorTexture_;
    prevWallTexture_ = wallTexture_;
    floorTexture_ = {};
    wallTexture_ = {};

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
    const float half = kPlayerCollisionSizePx * 0.5f;
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
    const float half = kPlayerCollisionSizePx * 0.5f;
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
    const float half = kPlayerCollisionSizePx * 0.5f;
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
    const float half = kPlayerCollisionSizePx * 0.5f;
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
        setPlayerActionState(PlayerActionState::Dead);
        respawnPlayerAtMapSpawn();
        return;
    }
    setPlayerActionState(PlayerActionState::Hurt, 0.22f);
}

void Engine::respawnPlayerAtMapSpawn()
{
    playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
    playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
    playerHealth_ = playerMaxHealth_;
    playerInvulnerableSeconds_ = kPlayerDamageInvulnerableSeconds;
    setPlayerActionState(PlayerActionState::Idle);
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
    float transitionT = 0.0f;
    if (transitionState_ == TransitionState::Sliding) {
        transitionT = std::clamp(transitionTime_ / transitionDuration_, 0.0f, 1.0f);
        tx = transitionFromX_ + (transitionToX_ - transitionFromX_) * transitionT;
        ty = transitionFromY_ + (transitionToY_ - transitionFromY_) * transitionT;
    }

    if (transitionState_ == TransitionState::Sliding) {
        const float prevTx = -transitionFromX_ * transitionT;
        const float prevTy = -transitionFromY_ * transitionT;
        glPushMatrix();
        glTranslatef(prevTx, prevTy, 0.0f);
        if (prevFloorTexture_.id != 0) {
            renderTexture(prevFloorTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
        } else {
            renderFilledRect(0.0f, 0.0f, screenWidthPx(), screenHeightPx(), 0.12f, 0.18f, 0.20f, 1.0f);
        }
        if (prevWallTexture_.id != 0) {
            renderTexture(prevWallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
        }
        glPopMatrix();
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
        const bool dying = entity.deathSeconds >= 0.0f;
        const float deathAlpha = dying
            ? 1.0f - std::clamp(entity.deathSeconds / kEnemyDeathVisualSeconds, 0.0f, 1.0f)
            : 1.0f;

        auto spriteIt = loadedSprites_.find(entity.path.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                renderTextureRegion(spriteIt->second.texture, entity.x - drawW * 0.5f, entity.y - drawH * 0.5f, drawW, drawH, u0, v0, u1, v1);
                if (dying) {
                    renderFilledRect(entity.x - drawW * 0.5f, entity.y - drawH * 0.5f,
                        drawW, drawH, 1.0f, 1.0f, 1.0f, deathAlpha * 0.6f);
                }
                continue;
            }
        }
        renderFilledRect(entity.x - 4.0f, entity.y - 4.0f, 8.0f, 8.0f, 0.90f, 0.18f, 0.14f, dying ? deathAlpha : 1.0f);
    }

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        const bool active = obstacleIsActive(obstacle);
        const float x = static_cast<float>(obstacle.x * kTileSize);
        const float y = static_cast<float>(obstacle.y * kTileSize);
        const float w = static_cast<float>(obstacle.width * kTileSize);
        const float h = static_cast<float>(obstacle.height * kTileSize);

        auto spriteIt = loadedSprites_.find(obstacle.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
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

    renderItems();
    renderNpcs();
    renderInteractionPrompt();

    bool flipH = false;
    const SpriteFrameDef* pf = playerSpriteFrame(flipH);
    if (pf != nullptr && playerSprite_.texture.id != 0 &&
        playerSprite_.texture.width > 0 && playerSprite_.texture.height > 0) {
        const float tw = static_cast<float>(playerSprite_.texture.width);
        const float th = static_cast<float>(playerSprite_.texture.height);
        const float u0 = static_cast<float>(pf->x) / tw;
        const float v0 = static_cast<float>(pf->y) / th;
        const float u1 = static_cast<float>(pf->x + pf->width) / tw;
        const float v1 = static_cast<float>(pf->y + pf->height) / th;
        const float drawW = static_cast<float>(pf->width);
        const float drawH = static_cast<float>(pf->height);
        renderTextureRegion(playerSprite_.texture,
            playerX_ - drawW * 0.5f, playerY_ - drawH * 0.5f, drawW, drawH,
            flipH ? u1 : u0, v0, flipH ? u0 : u1, v1);
    } else {
        renderFilledRect(playerX_ - kPlayerFallbackDrawSizePx * 0.5f, playerY_ - kPlayerFallbackDrawSizePx * 0.5f,
            kPlayerFallbackDrawSizePx, kPlayerFallbackDrawSizePx, 0.20f, 0.62f, 1.0f, 1.0f);
    }

    renderMeleeFlash();
    renderProjectiles();

    if (wallTexture_.id != 0) {
        renderTexture(wallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    }

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (entity.path.combat.maxHealth <= 1 || entity.deathSeconds >= 0.0f) {
            continue;
        }
        const float barW = std::max(8.0f, entity.path.combat.hitboxWidth);
        const float barH = 2.0f;
        const float pct = std::clamp(static_cast<float>(entity.health) / static_cast<float>(entity.path.combat.maxHealth), 0.0f, 1.0f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW, barH, 0.08f, 0.08f, 0.08f, 0.85f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW * pct, barH, 0.95f, 0.20f, 0.16f, 0.95f);
    }

    renderHud();

    glPopMatrix();

    renderDialogueBox();
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

std::string Engine::directionFromFacing(float fx, float fy)
{
    const float angle = std::atan2(fy, fx) * (180.0f / 3.14159265358979f);
    if (angle > -22.5f  && angle <=  22.5f) return "E";
    if (angle >  22.5f  && angle <=  67.5f) return "SE";
    if (angle >  67.5f  && angle <= 112.5f) return "S";
    if (angle > 112.5f  && angle <= 157.5f) return "SW";
    if (angle >  157.5f || angle <= -157.5f) return "W";
    if (angle > -157.5f && angle <= -112.5f) return "NW";
    if (angle > -112.5f && angle <=  -67.5f) return "N";
    return "NE";
}

const SpriteFrameDef* Engine::playerSpriteFrame(bool& flipHorizontal) const
{
    flipHorizontal = false;
    if (playerSprite_.metadata.frames.empty()) return nullptr;

    const std::string& action = playerActionType_;
    const std::string dir = directionFromFacing(playerFacingX_, playerFacingY_);

    // Mirror pair: flip East frames when facing these directions
    const std::string mirrorOf = (dir == "W") ? "E" : (dir == "NW") ? "NE" : (dir == "SW") ? "SE" : "";

    auto collectByDir = [&](const std::string& actionName, const std::string& targetDir) -> std::vector<const SpriteFrameDef*> {
        std::vector<const SpriteFrameDef*> out;
        for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
            if (frame.type == actionName && (frame.direction.empty() || frame.direction == targetDir)) {
                out.push_back(&frame);
            }
        }
        return out;
    };

    std::vector<const SpriteFrameDef*> candidates = collectByDir(action, dir);

    if (candidates.empty() && !mirrorOf.empty()) {
        candidates = collectByDir(action, mirrorOf);
        if (!candidates.empty()) flipHorizontal = true;
    }

    // Fall back to any frame of this action type
    if (candidates.empty()) {
        for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
            if (frame.type == action) candidates.push_back(&frame);
        }
    }

    if (candidates.empty() && action != "idle") {
        candidates = collectByDir("idle", dir);
        if (candidates.empty() && !mirrorOf.empty()) {
            candidates = collectByDir("idle", mirrorOf);
            if (!candidates.empty()) flipHorizontal = true;
        }
        if (candidates.empty()) {
            for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
                if (frame.type == "idle") candidates.push_back(&frame);
            }
        }
    }

    if (candidates.empty()) return &playerSprite_.metadata.frames.front();

    int totalMs = 0;
    for (const SpriteFrameDef* f : candidates) totalMs += std::max(1, f->durationMs);

    int t = static_cast<int>(std::fmod(playerAnimSeconds_ * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef* f : candidates) {
        t -= std::max(1, f->durationMs);
        if (t < 0) return f;
    }
    return candidates.back();
}

const SpriteFrameDef* Engine::spriteFrame(const RuntimeSprite& sprite) const
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

void Engine::renderItems() const
{
    for (const RuntimeItemEntity& item : itemEntities_) {
        if (item.collected) {
            continue;
        }
        const float x = item.placement.x;
        const float y = item.placement.y;
        const float r = 7.0f;
        float cr = 1.0f, cg = 1.0f, cb = 0.2f;
        if (item.placement.pickupType == ItemPickupType::Ammo) { cr = 0.2f; cg = 0.8f; cb = 1.0f; }
        else if (item.placement.pickupType == ItemPickupType::Health) { cr = 0.2f; cg = 0.9f; cb = 0.3f; }

        auto spriteIt = loadedSprites_.find(item.placement.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, x - r, y - r, r * 2.0f, r * 2.0f, u0, v0, u1, v1);
                continue;
            }
        }

        // Diamond fallback
        glDisable(GL_TEXTURE_2D);
        glColor4f(cr, cg, cb, 0.92f);
        glBegin(GL_QUADS);
        glVertex2f(x,       y - r);
        glVertex2f(x + r,   y);
        glVertex2f(x,       y + r);
        glVertex2f(x - r,   y);
        glEnd();
    }
}

void Engine::renderProjectiles() const
{
    for (const RuntimeProjectile& proj : projectiles_) {
        auto spriteIt = loadedSprites_.find(proj.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr) {
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, proj.x - drawW * 0.5f, proj.y - drawH * 0.5f, drawW, drawH, u0, v0, u1, v1);
                continue;
            }
        }
        renderFilledRect(proj.x - kProjectileHalfSize, proj.y - kProjectileHalfSize,
            kProjectileHalfSize * 2.0f, kProjectileHalfSize * 2.0f, 1.0f, 0.85f, 0.1f, 0.95f);
    }
}

void Engine::renderMeleeFlash() const
{
    if (meleeActiveSeconds_ <= 0.0f || !meleeWeapon_.has_value()) {
        return;
    }
    const float reach = meleeWeapon_->range;
    const float hw = reach * 0.5f;
    const float cx = playerX_ + playerFacingX_ * (kPlayerCollisionSizePx * 0.5f + hw);
    const float cy = playerY_ + playerFacingY_ * (kPlayerCollisionSizePx * 0.5f + hw);
    const float alpha = std::clamp(meleeActiveSeconds_ / kMeleeActiveSeconds, 0.0f, 1.0f) * 0.55f;
    renderFilledRect(cx - hw, cy - hw, reach, reach, 1.0f, 0.95f, 0.2f, alpha);
}

void Engine::renderHud() const
{
    const float heartW = 8.0f;
    for (int i = 0; i < playerMaxHealth_; ++i) {
        const bool filled = i < playerHealth_;
        renderFilledRect(8.0f + static_cast<float>(i) * (heartW + 2.0f), 8.0f, heartW, 6.0f,
            filled ? 0.90f : 0.16f, filled ? 0.08f : 0.08f, filled ? 0.12f : 0.09f, 0.95f);
    }

    float hudY = 18.0f;

    if (meleeWeapon_.has_value()) {
        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 0.9f, 0.3f, 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(8.0f,  hudY);
        glVertex2f(18.0f, hudY);
        glVertex2f(18.0f, hudY + 6.0f);
        glVertex2f(8.0f,  hudY + 6.0f);
        glEnd();
        hudY += 9.0f;
    }

    if (rangedWeapon_.has_value()) {
        const std::string ammoKey = rangedWeapon_->ammoTypeId.empty() ? rangedWeapon_->id : rangedWeapon_->ammoTypeId;
        const int ammoCount = ammo_.count(ammoKey) ? ammo_.at(ammoKey) : 0;
        const float barMax = 20.0f;
        const float barW = std::min(static_cast<float>(ammoCount), barMax) * 2.0f;
        renderFilledRect(8.0f, hudY, 40.0f, 4.0f, 0.08f, 0.08f, 0.08f, 0.6f);
        renderFilledRect(8.0f, hudY, barW, 4.0f, 0.2f, 0.8f, 1.0f, 0.9f);
    }
}

} // namespace adventure::game
