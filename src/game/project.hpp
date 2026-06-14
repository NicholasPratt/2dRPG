#pragma once

#include "game/dialogue_graph.hpp"
#include "game/state_types.hpp"
#include "game/weapon.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace adventure::game {

enum class StateVariableType {
    Integer = 0,
    Boolean = 1,
    Item = 2,
};

struct StateVariableDef {
    std::string id = "Variable_1";
    StateVariableType type = StateVariableType::Integer;
    int defaultInt = 0;
    bool defaultBool = false;
    StateVariableScope scope = StateVariableScope::Universal;
    std::string chapterId;
};

enum class GameEffectType {
    SetInt = 0,
    AddInt = 1,
    SetBool = 2,
    GiveItem = 3,
    TakeItem = 4,
};

struct GameEffectDef {
    std::string id = "effect_1";
    GameEffectType type = GameEffectType::AddInt;
    std::string targetId;
    int intValue = 1;
    bool boolValue = true;
    StateVariableScope scope = StateVariableScope::Universal;
};

enum class ItemDefType {
    Weapon = 0,
    Ammo = 1,
    Health = 2,
    Mana = 3,
    Currency = 4,
    Key = 5,
    Quest = 6,
    Consumable = 7,
    Material = 8,
    Equipment = 9,
    Custom = 10,
};

struct ItemDef {
    std::string id = "item_1";
    std::string name = "Item";
    ItemDefType type = ItemDefType::Consumable;
    std::string spriteId;
    std::string targetId;
    int value = 1;
    bool stackable = true;
    std::string customType;
    std::vector<std::string> acquireEffectIds;
};

struct ShopItemDef {
    std::string itemId;
    int buyPrice = 1;
    int sellPrice = 1;
    int quantity = 1;
    bool unlimited = true;
};

enum class EnemyAttackType {
    Contact = 0,  // damage when player overlaps enemy (legacy behaviour)
    Melee   = 1,  // swing attack when player is within range
    Ranged  = 2,  // fire a projectile toward player
};

struct EnemyAttackDef {
    EnemyAttackType type            = EnemyAttackType::Contact;
    int             damage          = 1;
    float           range           = 32.0f;      // px: melee reach or ranged trigger distance
    float           cooldown        = 1.0f;       // seconds between activations
    float           projectileSpeed = 120.0f;     // ranged only
    std::string     ammoSpriteId;                 // ranged only: projectile sprite id
    std::string     animState;                    // anim state name to play, e.g. "attack_1"
};

struct EnemyType {
    std::string id = "enemy_1";
    std::string spriteId = "enemy_1";
    int maxHealth = 1;
    int contactDamage = 1;
    float hitboxWidth = 12.0f;
    float hitboxHeight = 12.0f;
    float attackCooldownSeconds = 1.0f;
    float speed = 64.0f;
    std::vector<EnemyAttackDef> attacks;
    // Action-RPG hit reaction & AI tuning (ADGAME v12+)
    float knockbackResistance = 0.0f;   // 0 = full knockback, 1 = immovable
    float hitstunSeconds = 0.18f;       // stun/hurt window when damaged
    float aggroRange = 0.0f;            // 0 = never chase; >0 = chase player within this radius (px)
    std::string killVariable;           // GameState int incremented on death (quest hook)
    int killAmount = 1;                 // amount added to killVariable on death
    StateVariableScope killVariableScope = StateVariableScope::Universal;
    std::vector<std::string> defeatEffectIds;
};

enum class NpcMovementMode {
    Stationary = 0,
    Patrol = 1,
    Wander = 2,
};

enum class NpcInteractionMode {
    None = 0,
    Talk = 1,
    Shop = 2,
    Quest = 3,
};

struct DialogueLine {
    std::string speaker;
    std::string text;
};

struct NpcStateRule {
    DialogueCondition condition;
    std::string graphId;
    int movementOverride = -1; // -1 keeps the NPC type/placement movement
    std::string animation;
    int visibility = -1;       // -1 unchanged, 0 hidden, 1 visible
    int following = -1;        // -1 unchanged, 0 stop, 1 follow player
    std::vector<std::string> activateEffectIds;
};

struct NpcTypeDef {
    std::string id = "npc_1";
    std::string spriteId;
    std::string characterId;
    NpcMovementMode defaultMovement = NpcMovementMode::Stationary;
    NpcInteractionMode defaultInteraction = NpcInteractionMode::Talk;
    float defaultSpeed = 32.0f;
    std::string defaultGraphId;
    std::vector<DialogueLine> defaultDialogue;
    std::vector<ShopItemDef> shopInventory;
    std::vector<std::string> talkEffectIds;
    std::vector<NpcStateRule> stateRules;
};

struct GameProject {
    std::string id = "game";
    std::string playableCharacterId;
    std::string startingWeaponId;
    std::string fontPath;
    std::vector<std::string> characterIds;
    std::vector<std::string> chapterIds;
    std::vector<EnemyType> enemyTypes;
    std::vector<WeaponDef> weaponDefs;
    std::vector<ItemDef> itemDefs;
    std::vector<StateVariableDef> stateVariables;
    std::vector<GameEffectDef> effectDefs;
    std::vector<NpcTypeDef> npcTypes;
};

[[nodiscard]] bool saveGameProject(const std::filesystem::path& path, const GameProject& project, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadGameProject(const std::filesystem::path& path, GameProject& project, std::string* errorMessage = nullptr);

} // namespace adventure::game
