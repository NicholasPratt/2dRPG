#pragma once

#include "game/chapter.hpp"
#include "game/dialogue_graph.hpp"
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

    // Editor test-launch options (set before initialize()).
    void setFreshStart(bool fresh) { freshStart_ = fresh; }          // ignore + don't overwrite save.adstate
    // Continue a previous playthrough by loading save.adstate at boot. Off by
    // default: a normal launch starts a new game (state initialized from defaults).
    void setContinueSave(bool cont) { continueSave_ = cont; }
    void setStartScreen(const std::string& screenId) { startScreenOverride_ = screenId; }
    void setStartPosition(float x, float y) { startPosX_ = x; startPosY_ = y; }

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
        float hitstunSeconds = 0.0f;   // >0: staggered, cannot move/attack
        float hitFlashSeconds = 0.0f;  // >0: render white hit flash
        float knockbackVx = 0.0f;      // decaying knockback velocity (px/s)
        float knockbackVy = 0.0f;
        bool aggroActive = false;      // currently chasing the player
        bool returningToPath = false;  // walking back to its path after losing aggro
        float resumePathDistance = 0.0f;  // arc-distance of the nearest path point to resume at
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
        bool fromEnemy = false;       // true: damages the player; false: damages enemies
        ProjectileWallBehavior wallBehavior = ProjectileWallBehavior::Break;
        int bouncesRemaining = 0;     // rebound only
        float settleSeconds = -1.0f;  // >=0: grounded/settling (inert), counts up to fade-out
    };

    struct RuntimeItemEntity {
        MapItemPlacement placement;
        bool collected = false;
    };

    struct RuntimeNpcEntity {
        NpcPlacement placement;
        std::string spriteId;
        std::vector<DialogueLine> dialogue;
        std::string graphId;
        DialogueGraph graph;
        bool hasGraph = false;
        bool hidden = false;
        bool followingPlayer = false;
        NpcInteractionMode interactionMode = NpcInteractionMode::Talk;
        std::vector<ShopItemDef> shopInventory;
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
        InShop,
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

    enum class InputAction {
        Up,
        Down,
        Left,
        Right,
        Interact,
        Melee,
        Ranged,
        Inventory,
        Exit,
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
    bool freshStart_ = false;            // test launch: ignore + preserve save.adstate
    bool continueSave_ = false;          // load save.adstate at boot (explicit continue only)
    std::string startScreenOverride_;    // empty = chapter start screen
    float startPosX_ = -1.0f;            // <0 = use screen default spawn/center
    float startPosY_ = -1.0f;
    std::optional<WeaponDef> meleeWeapon_;
    std::optional<WeaponDef> rangedWeapon_;
    std::vector<ItemDef> itemDefs_;
    std::unordered_map<std::string, int> inventory_;
    bool inventoryVisible_ = false;
    bool inventoryInputWasDown_ = false;
    int inventorySelection_ = 0;
    int inventoryScroll_ = 0;
    bool inventoryUpWasDown_ = false;
    bool inventoryDownWasDown_ = false;
    bool inventoryUseWasDown_ = false;
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
    float meleeElapsedSeconds_ = 0.0f; // time since current swing started (active-window timing)
    std::unordered_set<std::string> meleeHitEnemies_;  // enemy instance ids hit by current swing
    float playerKnockbackVx_ = 0.0f;   // decaying player knockback velocity (px/s)
    float playerKnockbackVy_ = 0.0f;
    bool meleeInputWasDown_ = false;
    bool rangedInputWasDown_ = false;
    float runtimeSeconds_ = 0.0f;
    std::unordered_map<std::string, float> hazardCooldowns_;  // per-obstacle damage-tick timer, keyed by obstacle id
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
    int interactingDoorIndex_ = -1;
    int dialogueLineIndex_ = 0;
    int dialogueScrollLine_ = 0;
    std::string dialogueGraphNodeId_;
    DialogueLine dialogueGraphLine_;
    std::vector<DialogueChoice> dialogueGraphChoices_;
    int dialogueChoiceIndex_ = 0;
    bool interactInputWasDown_ = false;
    bool dialogueScrollUpWasDown_ = false;
    bool dialogueScrollDownWasDown_ = false;
    int shopPanel_ = 0;
    int shopSelection_ = 0;
    int shopScroll_[2] = {0, 0};
    bool shopBuyActive_ = false;
    int shopBuyIndex_ = -1;
    int shopBuyQuantity_ = 0;
    bool shopUpWasDown_ = false;
    bool shopDownWasDown_ = false;
    bool shopLeftWasDown_ = false;
    bool shopRightWasDown_ = false;
    bool shopUseWasDown_ = false;
    bool shopExitWasDown_ = false;
    std::string noticeText_;
    float noticeSeconds_ = 0.0f;

    [[nodiscard]] bool loadScreen(const std::string& screenId, std::string* errorMessage);
    [[nodiscard]] bool loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage);
    void destroyTexture(Texture& texture);
    void loadPlayableCharacter();
    void loadProjectFont();
    void loadWeapons();
    void syncInventoryFromGameState();
    void loadPathEntities();
    void loadNpcEntities();
    void loadAllSprites();
    void loadSpriteById(const std::string& spriteId);
    void loadItemEntities();
    void updateScreenMusic();
    [[nodiscard]] bool inputDown(InputAction action) const;
    [[nodiscard]] bool gamepadButtonDown(int button) const;
    [[nodiscard]] float gamepadAxis(int axis) const;
    void update(float dt);
    void updateInventoryInput();
    [[nodiscard]] std::vector<std::string> sortedInventoryIds() const;
    [[nodiscard]] std::vector<std::string> sortedShopInventoryIds() const;
    [[nodiscard]] std::vector<std::string> ammoInventoryIds() const;
    [[nodiscard]] bool isAmmoItemId(const std::string& id) const;
    [[nodiscard]] bool isCurrencyItemId(const std::string& id) const;
    [[nodiscard]] bool hasInventoryItem(const std::string& itemId) const;
    void addInventoryItem(const std::string& itemId, int quantity);
    [[nodiscard]] bool removeInventoryItem(const std::string& itemId, int quantity);
    void useInventoryItem(const std::string& itemId);
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
    [[nodiscard]] int nearestInteractableDoor() const;
    [[nodiscard]] bool playerCanInteractWithDoor(const MapDoorPlacement& door) const;
    [[nodiscard]] bool activateDoor(const MapDoorPlacement& door);
    void showNotice(const std::string& text, float seconds = 1.6f);
    void updateShopInput();
    [[nodiscard]] int maxShopBuyQuantity(const RuntimeNpcEntity& npc, int index) const;
    void beginShopPurchase(const RuntimeNpcEntity& npc, int index);
    void buyShopItem(RuntimeNpcEntity& npc, int index, int quantity);
    void sellInventoryItem(const std::string& itemId);
    void startDialogueGraph(const RuntimeNpcEntity& npc);
    void advanceDialogueGraph();
    void confirmDialogueGraph();
    [[nodiscard]] bool dialogueConditionPasses(const DialogueCondition& condition) const;
    void executeDialogueActions(const std::vector<DialogueAction>& actions, RuntimeNpcEntity& npc);
    [[nodiscard]] const DialogueNode* dialogueNodeById(const RuntimeNpcEntity& npc, const std::string& nodeId) const;
    void endInteraction();
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
    [[nodiscard]] bool playerOverlapsDoor(const MapDoorPlacement& door) const;
    void collectItem(RuntimeItemEntity& item);
    void recordEnemyDefeated(const std::string& screenId, const RuntimePathEntity& entity);
    void applyEnemyHit(RuntimePathEntity& entity, int damage, float dirX, float dirY);
    void damagePlayer(int amount, float sourceX, float sourceY);
    void damagePlayer(int amount);
    void saveRuntimeState() const;
    void writeCheckpoint(const std::string& screenId, float x, float y) const;
    void respawnPlayerAtMapSpawn();
    [[nodiscard]] float screenWidthPx() const;
    [[nodiscard]] float screenHeightPx() const;
    void render();
    void renderTexture(const Texture& texture, float x, float y, float width, float height) const;
    void renderTextureRegion(const Texture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1) const;
    void renderEnemyEntity(const RuntimePathEntity& entity) const;
    void renderAnimatedTiles(int layer) const;
    // Ammo for ranged weapons is drawn from the inventory. These resolve the
    // inventory item that backs a weapon's ammo, count it, and consume it.
    [[nodiscard]] std::string ammoItemIdForWeapon(const WeaponDef& weapon) const;
    [[nodiscard]] int ammoCountForWeapon(const WeaponDef& weapon) const;
    void consumeAmmoForWeapon(const WeaponDef& weapon, int amount);
    [[nodiscard]] const SpriteFrameDef* spriteFrame(const RuntimeSprite& sprite) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrameForEntity(const RuntimeSprite& sprite, const RuntimePathEntity& entity, bool& flipHorizontal) const;
    [[nodiscard]] const SpriteFrameDef* spriteFrameForNpc(const RuntimeSprite& sprite, const RuntimeNpcEntity& npc, bool& flipHorizontal) const;
    [[nodiscard]] const SpriteFrameDef* playerSpriteFrame(bool& flipHorizontal) const;
    static std::string directionFromFacing(float fx, float fy);
    void renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const;
    void renderDoors() const;
    void renderNpcs() const;
    void renderInteractionPrompt() const;
    void renderNotice() const;
    void renderSpeechBubble() const;
    void renderDialogueBox() const;
    void renderShopMenu() const;
    void renderItems() const;
    void renderProjectiles() const;
    void renderMeleeFlash() const;
    void renderHud() const;
    void renderInventory() const;
    void renderText(const std::string& text, float x, float y, float scale, float r, float g, float b, float a) const;
    [[nodiscard]] float textWidth(const std::string& text, float scale) const;
};

} // namespace adventure::game
