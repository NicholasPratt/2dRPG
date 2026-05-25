#pragma once

#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "game/weapon.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
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
        float pathDistance = 0.0f;
        int health = 1;
        float contactCooldownSeconds = 0.0f;
        std::size_t waypointIndex = 0;
    };

    struct RuntimeSprite {
        SpriteMetadata metadata;
        Texture texture;
        bool loaded = false;
    };

    struct RuntimeProjectile {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float distanceTraveled = 0.0f;
        float maxDistance = 300.0f;
        int damage = 1;
        std::string spriteId;
        bool dead = false;
    };

    struct RuntimeItemEntity {
        MapItemPlacement placement;
        bool collected = false;
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
    Texture playerTexture_;
    std::vector<RuntimePathEntity> pathEntities_;
    std::unordered_map<std::string, RuntimeSprite> loadedSprites_;
    std::vector<RuntimeProjectile> projectiles_;
    std::vector<RuntimeItemEntity> itemEntities_;
    std::optional<WeaponDef> meleeWeapon_;
    std::optional<WeaponDef> rangedWeapon_;
    std::unordered_map<std::string, int> ammo_;
    float playerX_ = 0.0f;
    float playerY_ = 0.0f;
    float playerFacingX_ = 1.0f;
    float playerFacingY_ = 0.0f;
    int playerMaxHealth_ = 6;
    int playerHealth_ = 6;
    float meleeCooldownSeconds_ = 0.0f;
    float rangedCooldownSeconds_ = 0.0f;
    float meleeActiveSeconds_ = 0.0f;  // brief visual flash duration
    float runtimeSeconds_ = 0.0f;
    float hazardCooldownSeconds_ = 0.0f;
    float playerInvulnerableSeconds_ = 0.0f;
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
    void loadPlayableCharacter();
    void loadWeapons();
    void loadPathEntities();
    void loadAllSprites();
    void loadSpriteById(const std::string& spriteId);
    void loadItemEntities();
    void update(float dt);
    void updatePlayer(float dt);
    void updateAttack(float dt);
    void updateProjectiles(float dt);
    void updateHazards(float dt);
    void updateEnemyCombat(float dt);
    void updatePaths(float dt);
    void updateItemPickups();
    void checkMeleeHits();
    [[nodiscard]] bool beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY);
    [[nodiscard]] bool playerCanOccupy(float x, float y) const;
    [[nodiscard]] bool solidAtPixel(float x, float y) const;
    [[nodiscard]] bool playerCanOccupyInMap(const TileMap& map, float x, float y) const;
    [[nodiscard]] bool solidAtPixelInMap(const TileMap& map, float x, float y) const;
    [[nodiscard]] bool obstacleIsActive(const MapObstacle& obstacle) const;
    [[nodiscard]] bool playerOverlapsObstacle(const MapObstacle& obstacle) const;
    [[nodiscard]] bool playerOverlapsEnemy(const RuntimePathEntity& entity) const;
    [[nodiscard]] bool playerOverlapsItem(const RuntimeItemEntity& item) const;
    void collectItem(RuntimeItemEntity& item);
    void damagePlayer(int amount);
    void respawnPlayerAtMapSpawn();
    [[nodiscard]] float screenWidthPx() const;
    [[nodiscard]] float screenHeightPx() const;
    void render();
    void renderTexture(const Texture& texture, float x, float y, float width, float height) const;
    void renderTextureRegion(const Texture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrame(const RuntimeSprite& sprite) const;
    void renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const;
    void renderItems() const;
    void renderProjectiles() const;
    void renderMeleeFlash() const;
    void renderHud() const;
};

} // namespace adventure::game
