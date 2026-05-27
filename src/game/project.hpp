#pragma once

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

struct NpcTypeDef {
    std::string id = "npc_1";
    std::string spriteId;
    std::string characterId;
    NpcMovementMode defaultMovement = NpcMovementMode::Stationary;
    NpcInteractionMode defaultInteraction = NpcInteractionMode::Talk;
    float defaultSpeed = 32.0f;
    std::string defaultGraphId;
    std::vector<DialogueLine> defaultDialogue;
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
    std::vector<StateVariableDef> stateVariables;
    std::vector<GameEffectDef> effectDefs;
    std::vector<NpcTypeDef> npcTypes;
};

[[nodiscard]] bool saveGameProject(const std::filesystem::path& path, const GameProject& project, std::string* errorMessage = nullptr);
[[nodiscard]] bool loadGameProject(const std::filesystem::path& path, GameProject& project, std::string* errorMessage = nullptr);

} // namespace adventure::game
