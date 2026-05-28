#pragma once

#include "game/chapter.hpp"
#include "game/map.hpp"
#include "game/path.hpp"
#include "game/project.hpp"
#include "game/sprite.hpp"
#include "game/state.hpp"
#include "game/weapon.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    class MusicPlayer;

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
        float deathSeconds = -1.0f;
        std::size_t waypointIndex = 0;
        float waitRemainingSeconds = 0.0f;
        bool atWaypoint = false;
        float animSeconds = 0.0f;
        std::string animState = "idle";
        std::vector<float> attackCooldowns;   // one entry per combat.attacks element
        float facingX = 1.0f;  // unit vector — direction the entity is facing
        float facingY = 0.0f;
    };

    struct RuntimeSprite {
        SpriteMetadata metadata;
        Texture texture;
        bool loaded = false;
    };

    struct RuntimeFont {
        Texture texture;
        bool loaded = false;
        float pixelHeight = 16.0f;
        struct BakedChar {
            unsigned short x0 = 0;
            unsigned short y0 = 0;
            unsigned short x1 = 0;
            unsigned short y1 = 0;
            float xoff = 0.0f;
            float yoff = 0.0f;
            float xadvance = 0.0f;
        };
        std::vector<BakedChar> chars;
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

    struct RuntimeNpcEntity {
        NpcPlacement placement;
        std::string spriteId;
        std::vector<DialogueLine> dialogue;
        float x = 0.0f;
        float y = 0.0f;
        float animSeconds = 0.0f;
        std::string actionType = "idle";
        std::size_t waypointIndex = 0;
        float pathDistance = 0.0f;
        bool playerInAwareness = false;
        float waitRemainingSeconds = 0.0f;
        bool atWaypoint = false;
        float facingX = 1.0f;  // unit vector — direction the NPC is facing
        float facingY = 0.0f;
    };

    enum class InteractionState {
        None,
        PromptVisible,
        InDialogue,
    };

    enum class TransitionState {
        None,
        Sliding,
    };

    enum class PlayerActionState {
        Idle,
        Walk,
        MeleeAttack,
        RangedAttack,
        Hurt,
        Dead,
    };

    std::filesystem::path projectRoot_;
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<MusicPlayer> musicPlayer_;
    Chapter chapter_;
    const ChapterScreen* activeScreen_ = nullptr;
    TileMap activeMap_;
    Texture floorTexture_;
    Texture wallTexture_;
    Texture prevFloorTexture_;
    Texture prevWallTexture_;
    RuntimeSprite playerSprite_;
    RuntimeFont font_;
    std::vector<RuntimePathEntity> pathEntities_;
    std::vector<RuntimeNpcEntity> npcEntities_;
    std::unordered_map<std::string, RuntimeSprite> loadedSprites_;
    std::vector<RuntimeProjectile> projectiles_;
    std::vector<RuntimeItemEntity> itemEntities_;
    GameState gameState_;
    std::unordered_set<std::string> defeatedEnemies_;
    std::optional<WeaponDef> meleeWeapon_;
    std::optional<WeaponDef> rangedWeapon_;
    std::unordered_map<std::string, int> ammo_;
    float playerX_ = 0.0f;
    float playerY_ = 0.0f;
    float playerFacingX_ = 1.0f;
    float playerFacingY_ = 0.0f;
    float playerAnimSeconds_ = 0.0f;
    std::string playerActionType_ = "idle";
    PlayerActionState playerActionState_ = PlayerActionState::Idle;
    float playerActionSeconds_ = 0.0f;
    bool playerIsMoving_ = false;
    int playerMaxHealth_ = 6;
    int playerHealth_ = 6;
    float meleeCooldownSeconds_ = 0.0f;
    float rangedCooldownSeconds_ = 0.0f;
    float meleeActiveSeconds_ = 0.0f;  // brief visual flash duration
    bool meleeHitApplied_ = false;
    bool meleeInputWasDown_ = false;
    bool rangedInputWasDown_ = false;
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
    InteractionState interactionState_ = InteractionState::None;
    int interactingNpcIndex_ = -1;
    int dialogueLineIndex_ = 0;
    bool interactInputWasDown_ = false;

    [[nodiscard]] bool loadScreen(const std::string& screenId, std::string* errorMessage);
    [[nodiscard]] bool loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage);
    void destroyTexture(Texture& texture);
    void loadPlayableCharacter();
    void loadProjectFont();
    void loadWeapons();
    void loadPathEntities();
    void loadNpcEntities();
    void loadAllSprites();
    void loadSpriteById(const std::string& spriteId);
    void loadItemEntities();
    void updateScreenMusic();
    void update(float dt);
    void updatePlayer(float dt);
    void updateAttack(float dt);
    void setPlayerActionState(PlayerActionState state, float durationSeconds = 0.0f);
    [[nodiscard]] bool playerActionLocksBaseMotion() const;
    [[nodiscard]] std::string playerActionName() const;
    void updateProjectiles(float dt);
    void updateHazards(float dt);
    void updateEnemyCombat(float dt);
    void updateEnemyDeaths(float dt);
    void updatePaths(float dt);
    void updateNpcs(float dt);
    void updateNpcAwareness();
    void updateInteraction();
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
    void recordEnemyDefeated(const std::string& screenId, const RuntimePathEntity& entity);
    void damagePlayer(int amount);
    void respawnPlayerAtMapSpawn();
    [[nodiscard]] float screenWidthPx() const;
    [[nodiscard]] float screenHeightPx() const;
    void render();
    void renderTexture(const Texture& texture, float x, float y, float width, float height) const;
    void renderTextureRegion(const Texture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1) const;
    void renderEnemyEntity(const RuntimePathEntity& entity) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrame(const RuntimeSprite& sprite) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrameForEntity(const RuntimeSprite& sprite, const RuntimePathEntity& entity, bool& flipHorizontal) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrameForNpc(const RuntimeSprite& sprite, const RuntimeNpcEntity& npc, bool& flipHorizontal) const;
    [[nodiscard]] const SpriteFrameDef* playerSpriteFrame(bool& flipHorizontal) const;
    static std::string directionFromFacing(float fx, float fy);
    void renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const;
    void renderNpcs() const;
    void renderInteractionPrompt() const;
    void renderSpeechBubble() const;
    void renderDialogueBox() const;
    void renderItems() const;
    void renderProjectiles() const;
    void renderMeleeFlash() const;
    void renderHud() const;
    void renderText(const std::string& text, float x, float y, float scale, float r, float g, float b, float a) const;
    [[nodiscard]] float textWidth(const std::string& text, float scale) const;
};

} // namespace adventure::game
