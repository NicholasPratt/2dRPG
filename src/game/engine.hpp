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

    // Axis-aligned world-space rectangle (pixels) used for per-frame collision and
    // hit testing.
    struct WorldRect {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
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
        // Attack telegraph: index of the attack winding up (-1 = none) and the
        // time left before it lands. The enemy flashes and holds during windup.
        int windupAttackIndex = -1;
        float windupRemainingSeconds = 0.0f;
        // Charger AI state machine: 0 idle, 1 winding up, 2 dashing, 3 recovering.
        int chargeState = 0;
        float chargeTimerSeconds = 0.0f;
        float chargeCooldownSeconds = 0.0f;
        float chargeDirX = 1.0f;
        float chargeDirY = 0.0f;
        bool enrageAnnounced = false;  // boss roar fired when dropping below half health
        float resumePathDistance = 0.0f;  // arc-distance of the nearest path point to resume at
        bool pathHidden = false;
        bool pathFinished = false;
        std::string speechText;
        float speechRemainingSeconds = 0.0f;
        // Actual movement velocity (px/s) measured across the last update,
        // used to lead the target when firing ranged shots.
        float prevX = 0.0f;
        float prevY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
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
        // Distance damage falloff: full damage to falloffStartPx, scaling
        // linearly down to damage * falloffMinScale at falloffEndPx (shotgun).
        float falloffStartPx = 0.0f;  // <= 0: no falloff
        float falloffEndPx = 0.0f;
        float falloffMinScale = 1.0f;
        // Rebound recovery: when set, a settled projectile turns into an ammo
        // pickup for this inventory item instead of despawning.
        std::string recoverAmmoItemId;
    };

    struct RuntimeItemEntity {
        MapItemPlacement placement;
        bool collected = false;
    };

    // Short-lived visual effect quad (hit sparks, death bursts, pickup sparkle).
    struct Particle {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float life = 0.0f;      // seconds remaining
        float maxLife = 0.3f;
        float size = 2.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float gravity = 0.0f;   // px/s^2 pulled downward
    };

    // Floating combat text (damage numbers, pickup counts) drifting upward.
    struct FloatingText {
        float x = 0.0f;
        float y = 0.0f;
        std::string text;
        float life = 0.0f;
        float maxLife = 0.8f;
        float scale = 0.9f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
    };

    // Procedurally synthesized retro sound effects (no asset files needed).
    enum class SfxId {
        MeleeSwing,
        MeleeHit,
        EnemyDeath,
        PlayerHurt,
        PlayerDeath,
        Shoot,
        Misfire,
        Pickup,
        Coin,
        Heal,
        Roll,
        MenuMove,
        MenuSelect,
        QuestComplete,
        BossRoar,
        Telegraph,
    };

    struct RuntimeNpcEntity {
        NpcPlacement placement;
        std::string spriteId;
        std::vector<DialogueLine> dialogue;
        std::string graphId;
        std::string baseGraphId;
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
        float speechRemainingSeconds = 0.0f;
        std::string speechText;
        bool pathHidden = false;
        bool pathFinished = false;
        std::vector<std::string> talkEffectIds;
        std::vector<NpcStateRule> stateRules;
        int activeStateRule = -1;
        NpcMovementMode baseMovement = NpcMovementMode::Stationary;
        std::string ruleAnimation;
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
        FadingOut,
        FadingIn,
    };

    enum class PlayerActionState {
        Idle,
        Walk,
        MeleeAttack,
        RangedAttack,
        Roll,
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
        Roll,
        AimCycleNext,
        AimCyclePrev,
        Inventory,
        Exit,
    };

    enum class DisplayMode {
        Windowed1x,
        Windowed2x,
        Fullscreen2x,
    };

    std::filesystem::path projectRoot_;
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<MusicPlayer> musicPlayer_;
    std::string pendingDoorCloseSoundPath_;
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
    std::vector<WeaponDef> weaponDefs_;
    std::vector<ItemDef> itemDefs_;
    std::vector<GameEffectDef> effectDefs_;
    std::vector<QuestDef> questDefs_;
    std::unordered_set<std::string> questCompleteNotified_;  // quests whose completion toast fired
    std::unordered_map<std::string, int> inventory_;
    bool inventoryVisible_ = false;
    bool inventoryInputWasDown_ = false;
    int inventorySelection_ = 0;
    int inventoryScroll_ = 0;
    bool inventoryUpWasDown_ = false;
    bool inventoryDownWasDown_ = false;
    bool inventoryUseWasDown_ = false;
    bool inventoryMeleeWasDown_ = false;
    bool inventoryRangedWasDown_ = false;
    bool displayMenuVisible_ = false;
    int displayMenuSelection_ = 2;
    bool displayMenuUpWasDown_ = false;
    bool displayMenuDownWasDown_ = false;
    bool displayMenuUseWasDown_ = false;
    bool displayMenuEscapeWasDown_ = false;
    bool fullscreenShortcutWasDown_ = false;
    DisplayMode displayMode_ = DisplayMode::Windowed1x;
    int windowedX_ = 0;
    int windowedY_ = 0;
    int windowedWidth_ = 768;
    int windowedHeight_ = 540;
    float playerX_ = 0.0f;
    float playerY_ = 0.0f;
    float playerFacingX_ = 1.0f;
    float playerFacingY_ = 0.0f;
    float playerAnimSeconds_ = 0.0f;
    std::string playerActionType_ = "idle";
    PlayerActionState playerActionState_ = PlayerActionState::Idle;
    // Animation action of the weapon used for the current attack (e.g.
    // "attack_20"); empty falls back to "attack_1" (melee) / "cast" (ranged).
    std::string playerAttackAnimOverride_;
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
    float playerHitFlashSeconds_ = 0.0f;
    bool meleeInputWasDown_ = false;
    bool rangedInputWasDown_ = false;
    // Ranged hold-to-draw / auto-aim state.
    bool rangedAiming_ = false;          // ranged button held on a hold-to-draw weapon
    float rangedHoldSeconds_ = 0.0f;     // how long the current draw has been held
    bool rangedMisfireLatched_ = false;  // overheld a Misfire weapon: shot fizzles on release
    std::string aimTargetId_;            // enemy instance id of the locked target
    std::vector<std::size_t> aimCandidates_;  // pathEntities_ indexes in the aim cone, sorted left-to-right
    bool aimCycleNextWasDown_ = false;
    bool aimCyclePrevWasDown_ = false;
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
    std::string pendingChapterId_;
    std::string pendingChapterScreenId_;
    float pendingChapterSpawnX_ = 0.0f;
    float pendingChapterSpawnY_ = 0.0f;
    float chapterExitArrivalLockSeconds_ = 0.0f;
    std::vector<bool> chapterExitConditionWasTrue_;
    std::vector<bool> chapterExitPlayerWasInside_;
    InteractionState interactionState_ = InteractionState::None;
    int interactingNpcIndex_ = -1;
    int interactingDoorIndex_ = -1;
    int interactingChapterExitIndex_ = -1;
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

    // --- Game-feel state ---
    std::vector<Particle> particles_;
    std::vector<FloatingText> floatingTexts_;
    float shakeTrauma_ = 0.0f;     // 0..1, squared into a pixel offset each frame
    float hitstopSeconds_ = 0.0f;  // sim freeze on meaty hits
    // Dodge roll.
    float rollSeconds_ = 0.0f;         // time left in the current roll
    float rollCooldownSeconds_ = 0.0f;
    float rollDirX_ = 1.0f;
    float rollDirY_ = 0.0f;
    bool rollInputWasDown_ = false;
    // Death sequence: dead player fades to a "YOU DIED" screen, then respawns.
    bool playerDead_ = false;
    float deathSeconds_ = 0.0f;
    // Audio settings (0..100, persisted in the save state).
    int musicVolumePct_ = 80;
    int sfxVolumePct_ = 100;
    // Door opening-animation playback (openingAnimation frames play once).
    std::string animatingDoorId_;
    float doorAnimSeconds_ = -1.0f;

    [[nodiscard]] bool loadScreen(const std::string& screenId, std::string* errorMessage);
    [[nodiscard]] bool loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage);
    void destroyTexture(Texture& texture);
    void loadPlayableCharacter();
    void loadProjectFont();
    void loadWeapons();
    void seedChapterStateDefaults();
    void syncInventoryFromGameState();
    [[nodiscard]] std::string scopedStateId(StateVariableScope scope, const std::string& id) const;
    [[nodiscard]] int scopedInt(StateVariableScope scope, const std::string& id, int fallback = 0) const;
    [[nodiscard]] bool scopedBool(StateVariableScope scope, const std::string& id, bool fallback = false) const;
    [[nodiscard]] std::string interpolateText(const std::string& text) const;
    void applyEffect(const GameEffectDef& effect);
    void applyEffects(const std::vector<std::string>& effectIds);
    void loadPathEntities();
    void loadNpcEntities();
    void loadAllSprites();
    void loadSpriteById(const std::string& spriteId);
    void loadItemEntities();
    [[nodiscard]] std::string itemStateId(const MapItemPlacement& item) const;
    void updateScreenMusic();
    void updateWalkingSfx();
    [[nodiscard]] bool inputDown(InputAction action) const;
    [[nodiscard]] bool gamepadButtonDown(int button) const;
    [[nodiscard]] float gamepadAxis(int axis) const;
    void update(float dt);
    void updateDisplayMenu();
    void applyDisplayMode(DisplayMode mode);
    void updateInventoryInput();
    [[nodiscard]] std::vector<std::string> sortedInventoryIds() const;
    [[nodiscard]] std::vector<std::string> sortedShopInventoryIds() const;
    [[nodiscard]] std::vector<std::string> ammoInventoryIds() const;
    [[nodiscard]] bool isAmmoItemId(const std::string& id) const;
    [[nodiscard]] bool isCurrencyItemId(const std::string& id) const;
    [[nodiscard]] bool hasInventoryItem(const std::string& itemId) const;
    [[nodiscard]] const WeaponDef* weaponForInventoryItem(const std::string& itemId) const;
    void equipWeapon(const WeaponDef& weapon);
    void addInventoryItem(const std::string& itemId, int quantity);
    [[nodiscard]] bool removeInventoryItem(const std::string& itemId, int quantity);
    void useInventoryItem(const std::string& itemId);
    void updatePlayer(float dt);
    void updateAttack(float dt);
    void updateRangedAttack(float dt, bool pressed, bool released);
    void updateAimTargets();
    void cycleAimTarget(int direction);
    [[nodiscard]] const RuntimePathEntity* aimTargetEntity() const;
    void cancelRangedAim();
    void fireRangedWeapon(bool misfire);
    [[nodiscard]] float rangedFullDrawSeconds() const;
    [[nodiscard]] float rangedSpreadDegrees() const;
    void setPlayerActionState(PlayerActionState state, float durationSeconds = 0.0f);
    [[nodiscard]] bool playerActionLocksBaseMotion() const;
    [[nodiscard]] std::string playerActionName() const;
    void updateProjectiles(float dt);
    void updateHazards(float dt);
    void updateEnemyCombat(float dt);
    void updateEnemyDeaths(float dt);
    void updatePaths(float dt);
    void updateNpcs(float dt);
    void updateNpcStateRules(RuntimeNpcEntity& npc);
    void loadNpcGraph(RuntimeNpcEntity& npc, const std::string& graphId);
    void updateDoors();
    void updateChapterExits();
    void applyEnemyWaypointAction(RuntimePathEntity& entity, const PathWaypoint& waypoint);
    void applyNpcWaypointAction(RuntimeNpcEntity& npc, const PathWaypoint& waypoint, std::size_t waypointIndex);
    [[nodiscard]] std::string npcWaypointActionStateId(const RuntimeNpcEntity& npc, std::size_t waypointIndex) const;
    [[nodiscard]] bool npcWaypointActionExhausted(const RuntimeNpcEntity& npc, const PathWaypoint& waypoint,
        std::size_t waypointIndex) const;
    void markNpcWaypointActionUsed(const RuntimeNpcEntity& npc, const PathWaypoint& waypoint,
        std::size_t waypointIndex);
    void updateNpcAwareness();
    void updateInteraction();
    [[nodiscard]] int nearestInteractableDoor() const;
    [[nodiscard]] bool playerCanInteractWithDoor(const MapDoorPlacement& door) const;
    [[nodiscard]] bool activateDoor(const MapDoorPlacement& door);
    [[nodiscard]] std::string doorStateId(const MapDoorPlacement& door, const char* state) const;
    [[nodiscard]] bool doorIsUnlocked(const MapDoorPlacement& door) const;
    [[nodiscard]] bool doorIsHidden(const MapDoorPlacement& door) const;
    void playSoundEffect(const std::string& configuredPath);
    void playSfx(SfxId id);
    void applyAudioVolumes();
    void addScreenShake(float trauma);
    void spawnParticles(float x, float y, int count, float r, float g, float b,
        float speed, float life, float size, float gravity = 0.0f);
    void spawnFloatingText(float x, float y, const std::string& text,
        float r, float g, float b, float scale = 0.9f);
    void updateParticles(float dt);
    void updateQuests();
    [[nodiscard]] bool questStarted(const QuestDef& quest) const;
    [[nodiscard]] bool questComplete(const QuestDef& quest) const;
    void spawnGroundPickup(float x, float y, ItemPickupType type, const std::string& targetId,
        int quantity, const std::string& spriteId);
    void spawnEnemyDrop(const RuntimePathEntity& entity);
    void beginPlayerDeath();
    void finishPlayerDeath();
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
    [[nodiscard]] bool gameConditionPasses(const GameCondition& condition) const;
    void executeDialogueActions(const std::vector<DialogueAction>& actions, RuntimeNpcEntity& npc);
    [[nodiscard]] const DialogueNode* dialogueNodeById(const RuntimeNpcEntity& npc, const std::string& nodeId) const;
    void endInteraction();
    void updateItemPickups();
    void checkMeleeHits();
    [[nodiscard]] bool beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY);
    [[nodiscard]] bool beginChapterTransition(const MapChapterExitPlacement& exit);
    [[nodiscard]] bool completeChapterTransition();
    [[nodiscard]] bool activateChapterExit(const MapChapterExitPlacement& exit);
    [[nodiscard]] bool playerOverlapsChapterExit(const MapChapterExitPlacement& exit) const;
    [[nodiscard]] bool playerCanInteractWithChapterExit(const MapChapterExitPlacement& exit) const;
    [[nodiscard]] int nearestInteractableChapterExit() const;
    [[nodiscard]] std::string chapterExitStateId(const MapChapterExitPlacement& exit) const;
    [[nodiscard]] bool playerCanOccupy(float x, float y) const;
    [[nodiscard]] bool solidAtPixel(float x, float y) const;
    [[nodiscard]] bool playerCanOccupyInMap(const TileMap& map, float x, float y) const;
    [[nodiscard]] bool solidAtPixelInMap(const TileMap& map, float x, float y) const;
    // Per-frame collision/hit box resolution. A frame-local box [x,y,w,h] is mapped
    // to a world rect using the same offset the sprite is drawn at (entity centered
    // on its frame), mirrored on X when the frame is horizontally flipped. An unset
    // box (w<=0 or h<=0) falls back to a centered box of the given half-extents.
    [[nodiscard]] static WorldRect frameBoxRect(const SpriteFrameDef& frame, const std::array<int, 4>& box,
        float worldX, float worldY, bool flipHorizontal, float fallbackHalfW, float fallbackHalfH);
    [[nodiscard]] static bool rectsOverlap(const WorldRect& a, const WorldRect& b);
    [[nodiscard]] bool mapRectClear(const TileMap& map, const WorldRect& rect) const;
    [[nodiscard]] WorldRect playerWallRectAt(float x, float y) const;
    [[nodiscard]] WorldRect playerHitRect() const;
    [[nodiscard]] WorldRect enemyWallRectAt(const RuntimePathEntity& entity, float x, float y) const;
    [[nodiscard]] WorldRect enemyHitRect(const RuntimePathEntity& entity) const;
    [[nodiscard]] WorldRect npcWallRectAt(const RuntimeNpcEntity& npc, float x, float y) const;
    [[nodiscard]] bool entityCanOccupy(const RuntimePathEntity& entity, float x, float y) const;
    [[nodiscard]] bool npcCanOccupy(const RuntimeNpcEntity& npc, float x, float y) const;
    [[nodiscard]] bool obstacleIsActive(const MapObstacle& obstacle) const;
    [[nodiscard]] bool playerOverlapsObstacle(const MapObstacle& obstacle) const;
    [[nodiscard]] bool playerOverlapsEnemy(const RuntimePathEntity& entity) const;
    [[nodiscard]] bool playerOverlapsItem(const RuntimeItemEntity& item) const;
    [[nodiscard]] bool playerOverlapsDoor(const MapDoorPlacement& door) const;
    [[nodiscard]] bool playerOverlapsDoorAt(const MapDoorPlacement& door, float x, float y) const;
    [[nodiscard]] bool doorBlocksPlayerAt(const MapDoorPlacement& door, float x, float y) const;
    void collectItem(RuntimeItemEntity& item);
    void recordEnemyDefeated(const std::string& screenId, const RuntimePathEntity& entity);
    void applyEnemyHit(RuntimePathEntity& entity, int damage, float dirX, float dirY);
    void damagePlayer(int amount, float sourceX, float sourceY);
    void damagePlayer(int amount);
    void saveRuntimeState();
    void writeCheckpoint(const std::string& screenId, float x, float y) const;
    void respawnPlayerAtMapSpawn();
    [[nodiscard]] float screenWidthPx() const;
    [[nodiscard]] float screenHeightPx() const;
    void render();
    void renderTexture(const Texture& texture, float x, float y, float width, float height) const;
    void renderTextureRegion(const Texture& texture, float x, float y, float width, float height,
        float u0, float v0, float u1, float v1,
        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) const;
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
    void renderArc(float cx, float cy, float radius, float thickness,
        float startRadians, float spanRadians, float r, float g, float b, float a) const;
    void renderDoors(bool aboveWalls) const;
    void renderNpcs() const;
    void renderInteractionPrompt() const;
    void renderNotice() const;
    void renderSpeechBubble() const;
    void renderPathSpeechBubbles() const;
    void renderDialogueBox() const;
    void renderShopMenu() const;
    void renderItems() const;
    void renderProjectiles() const;
    void renderMeleeFlash() const;
    void renderAimTargets() const;
    void renderChargeMeter() const;
    void renderParticles() const;
    void renderFloatingTexts() const;
    void renderBossBar() const;
    void renderQuestHud() const;
    void renderDeathOverlay() const;
    void renderHeart(float x, float y, float scale, float fill01) const;
    void renderHud() const;
    void renderInventory() const;
    void renderDisplayMenu() const;
    void renderText(const std::string& text, float x, float y, float scale, float r, float g, float b, float a) const;
    [[nodiscard]] float textWidth(const std::string& text, float scale) const;
};

} // namespace adventure::game
