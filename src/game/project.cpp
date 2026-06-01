#include "game/project.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace adventure::game {
namespace {

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validToken(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    });
}

} // namespace

bool saveGameProject(const std::filesystem::path& path, const GameProject& project, std::string* errorMessage)
{
    if (!validToken(project.id)) {
        setError(errorMessage, "Project id is invalid.");
        return false;
    }
    for (const StateVariableDef& variable : project.stateVariables) {
        if (!validToken(variable.id)) {
            setError(errorMessage, "State variable id is invalid: " + variable.id);
            return false;
        }
    }
    for (const GameEffectDef& effect : project.effectDefs) {
        if (!validToken(effect.id) || effect.targetId.empty() || !validToken(effect.targetId)) {
            setError(errorMessage, "Effect definition is invalid: " + effect.id);
            return false;
        }
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open game project for writing.");
        return false;
    }

    output << "ADGAME 13\n";
    output << "id " << project.id << "\n";
    output << "playable " << (project.playableCharacterId.empty() ? "-" : project.playableCharacterId) << "\n";
    output << "characters " << project.characterIds.size() << "\n";
    for (const std::string& id : project.characterIds) {
        output << "character " << id << "\n";
    }
    output << "chapters " << project.chapterIds.size() << "\n";
    for (const std::string& id : project.chapterIds) {
        output << "chapter " << id << "\n";
    }
    output << "enemy_types " << project.enemyTypes.size() << "\n";
    for (const EnemyType& type : project.enemyTypes) {
        output << "enemy_type " << type.id << ' ' << (type.spriteId.empty() ? "-" : type.spriteId) << ' '
               << type.maxHealth << ' ' << type.contactDamage << ' '
               << type.hitboxWidth << ' ' << type.hitboxHeight << ' '
               << type.attackCooldownSeconds << ' ' << type.speed
               << ' ' << type.knockbackResistance << ' ' << type.hitstunSeconds
               << ' ' << type.aggroRange
               << ' ' << (type.killVariable.empty() ? "-" : type.killVariable)
               << ' ' << type.killAmount
               << ' ' << type.attacks.size() << "\n";
        for (const EnemyAttackDef& atk : type.attacks) {
            output << "enemy_attack"
                   << ' ' << static_cast<int>(atk.type)
                   << ' ' << atk.damage
                   << ' ' << atk.range
                   << ' ' << atk.cooldown
                   << ' ' << atk.projectileSpeed
                   << ' ' << (atk.animState.empty() ? "-" : atk.animState)
                   << ' ' << (atk.ammoSpriteId.empty() ? "-" : atk.ammoSpriteId)
                   << "\n";
        }
    }
    output << "weapon_defs " << project.weaponDefs.size() << "\n";
    for (const WeaponDef& w : project.weaponDefs) {
        output << "weapon_def " << w.id
               << ' ' << static_cast<int>(w.type)
               << ' ' << w.damage
               << ' ' << w.range
               << ' ' << w.attackCooldown
               << ' ' << w.projectileSpeed
               << ' ' << (w.spriteId.empty() ? "-" : w.spriteId)
               << ' ' << (w.ammoTypeId.empty() ? "-" : w.ammoTypeId)
               << ' ' << (w.ammoSpriteId.empty() ? "-" : w.ammoSpriteId)
               << ' ' << w.ammoPerShot
               << ' ' << static_cast<int>(w.wallBehavior) << "\n";
    }
    output << "item_defs " << project.itemDefs.size() << "\n";
    for (const ItemDef& item : project.itemDefs) {
        output << "item_def " << item.id
               << ' ' << std::quoted(item.name)
               << ' ' << static_cast<int>(item.type)
               << ' ' << (item.spriteId.empty() ? "-" : item.spriteId)
               << ' ' << (item.targetId.empty() ? "-" : item.targetId)
               << ' ' << item.value
               << ' ' << (item.stackable ? 1 : 0)
               << ' ' << (item.customType.empty() ? "-" : item.customType)
               << "\n";
    }
    output << "starting_weapon " << (project.startingWeaponId.empty() ? "-" : project.startingWeaponId) << "\n";
    output << "font " << std::quoted(project.fontPath.empty() ? std::string{"-"} : project.fontPath) << "\n";
    output << "state_defs " << project.stateVariables.size() << "\n";
    for (const StateVariableDef& variable : project.stateVariables) {
        output << "state_def " << variable.id
               << ' ' << static_cast<int>(variable.type)
               << ' ' << variable.defaultInt
               << ' ' << (variable.defaultBool ? 1 : 0) << "\n";
    }
    output << "effect_defs " << project.effectDefs.size() << "\n";
    for (const GameEffectDef& effect : project.effectDefs) {
        output << "effect_def " << effect.id
               << ' ' << static_cast<int>(effect.type)
               << ' ' << effect.targetId
               << ' ' << effect.intValue
               << ' ' << (effect.boolValue ? 1 : 0) << "\n";
    }
    output << "npc_types " << project.npcTypes.size() << "\n";
    for (const NpcTypeDef& npc : project.npcTypes) {
        output << "npc_type " << npc.id
               << ' ' << (npc.spriteId.empty() ? "-" : npc.spriteId)
               << ' ' << (npc.characterId.empty() ? "-" : npc.characterId)
               << ' ' << static_cast<int>(npc.defaultMovement)
               << ' ' << static_cast<int>(npc.defaultInteraction)
               << ' ' << npc.defaultSpeed
               << ' ' << (npc.defaultGraphId.empty() ? "-" : npc.defaultGraphId)
               << ' ' << npc.defaultDialogue.size()
               << ' ' << npc.shopInventory.size() << "\n";
        for (const DialogueLine& dl : npc.defaultDialogue) {
            output << "dl " << std::quoted(dl.speaker) << ' ' << std::quoted(dl.text) << "\n";
        }
        for (const ShopItemDef& item : npc.shopInventory) {
            output << "shop_item " << (item.itemId.empty() ? "-" : item.itemId)
                   << ' ' << item.buyPrice
                   << ' ' << item.sellPrice
                   << ' ' << item.quantity
                   << ' ' << (item.unlimited ? 1 : 0)
                   << "\n";
        }
    }
    output << "end\n";
    return static_cast<bool>(output);
}

bool loadGameProject(const std::filesystem::path& path, GameProject& project, std::string* errorMessage)
{
    std::ifstream input(path);
    if (!input) {
        setError(errorMessage, "Could not open game project for reading.");
        return false;
    }

    std::string magic;
    int version = 0;
    input >> magic >> version;
    if (magic != "ADGAME" || version < 1 || version > 13) {
        setError(errorMessage, "Unsupported game project file.");
        return false;
    }

    GameProject loaded;
    std::string key;
    while (input >> key) {
        if (key == "id") {
            input >> loaded.id;
        } else if (key == "playable") {
            input >> loaded.playableCharacterId;
            if (loaded.playableCharacterId == "-") {
                loaded.playableCharacterId.clear();
            }
        } else if (key == "characters") {
            std::size_t count = 0;
            input >> count;
            loaded.characterIds.reserve(count);
        } else if (key == "character") {
            std::string id;
            input >> id;
            if (!id.empty()) {
                loaded.characterIds.push_back(id);
            }
        } else if (version >= 2 && key == "chapters") {
            std::size_t count = 0;
            input >> count;
            loaded.chapterIds.reserve(count);
        } else if (version >= 2 && key == "chapter") {
            std::string id;
            input >> id;
            if (!id.empty()) {
                loaded.chapterIds.push_back(id);
            }
        } else if (version >= 2 && key == "enemy_types") {
            std::size_t count = 0;
            input >> count;
            loaded.enemyTypes.reserve(count);
        } else if (version >= 2 && key == "enemy_type") {
            EnemyType type;
            input >> type.id >> type.spriteId >> type.maxHealth >> type.contactDamage
                  >> type.hitboxWidth >> type.hitboxHeight >> type.attackCooldownSeconds >> type.speed;
            if (type.spriteId == "-") {
                type.spriteId.clear();
            }
            if (version >= 12) {
                std::string killVar;
                input >> type.knockbackResistance >> type.hitstunSeconds >> type.aggroRange
                      >> killVar >> type.killAmount;
                type.killVariable = (killVar == "-") ? "" : killVar;
            }
            int numAttacks = 0;
            if (version >= 8) {
                input >> numAttacks;
                for (int ai = 0; ai < numAttacks && input; ++ai) {
                    std::string atkKey;
                    input >> atkKey;
                    if (atkKey == "enemy_attack") {
                        EnemyAttackDef atk;
                        int typeInt = 0;
                        std::string animState, ammoSprite;
                        input >> typeInt >> atk.damage >> atk.range >> atk.cooldown
                              >> atk.projectileSpeed >> animState >> ammoSprite;
                        atk.type = static_cast<EnemyAttackType>(std::clamp(typeInt, 0, 2));
                        atk.animState    = (animState  == "-") ? "" : animState;
                        atk.ammoSpriteId = (ammoSprite == "-") ? "" : ammoSprite;
                        type.attacks.push_back(std::move(atk));
                    }
                }
            }
            if (!type.id.empty()) {
                loaded.enemyTypes.push_back(std::move(type));
            }
        } else if (version >= 3 && key == "weapon_defs") {
            std::size_t count = 0;
            input >> count;
            loaded.weaponDefs.reserve(count);
        } else if (version >= 3 && key == "weapon_def") {
            WeaponDef w;
            int weaponType = 0;
            std::string spriteId;
            std::string ammoTypeId;
            std::string ammoSpriteId;
            input >> w.id >> weaponType >> w.damage >> w.range >> w.attackCooldown
                  >> w.projectileSpeed >> spriteId >> ammoTypeId;
            if (version >= 9) {
                input >> ammoSpriteId;
            }
            input >> w.ammoPerShot;
            if (version >= 13) {
                int wallBehavior = 0;
                input >> wallBehavior;
                w.wallBehavior = static_cast<ProjectileWallBehavior>(std::clamp(wallBehavior, 0, 1));
            }
            w.type = (weaponType == 1) ? WeaponType::Ranged : WeaponType::Melee;
            w.spriteId = (spriteId == "-") ? std::string{} : spriteId;
            w.ammoTypeId = (ammoTypeId == "-") ? std::string{} : ammoTypeId;
            w.ammoSpriteId = (ammoSpriteId == "-") ? std::string{} : ammoSpriteId;
            if (!w.id.empty()) {
                loaded.weaponDefs.push_back(std::move(w));
            }
        } else if (version >= 3 && key == "starting_weapon") {
            input >> loaded.startingWeaponId;
            if (loaded.startingWeaponId == "-") {
                loaded.startingWeaponId.clear();
            }
        } else if (version >= 10 && key == "item_defs") {
            std::size_t count = 0;
            input >> count;
            loaded.itemDefs.reserve(count);
        } else if (version >= 10 && key == "item_def") {
            ItemDef item;
            int type = 0;
            std::string spriteId;
            std::string targetId;
            std::string customType;
            int stackable = 1;
            input >> item.id >> std::quoted(item.name) >> type >> spriteId >> targetId >> item.value >> stackable >> customType;
            item.type = static_cast<ItemDefType>(std::clamp(type, 0, 10));
            item.spriteId = (spriteId == "-") ? std::string{} : spriteId;
            item.targetId = (targetId == "-") ? std::string{} : targetId;
            item.stackable = stackable != 0;
            item.customType = (customType == "-") ? std::string{} : customType;
            if (!item.id.empty()) {
                loaded.itemDefs.push_back(std::move(item));
            }
        } else if (version >= 7 && key == "font") {
            input >> std::quoted(loaded.fontPath);
            if (loaded.fontPath == "-") {
                loaded.fontPath.clear();
            }
        } else if (version >= 4 && key == "state_defs") {
            std::size_t count = 0;
            input >> count;
            loaded.stateVariables.reserve(count);
        } else if (version >= 4 && key == "state_def") {
            StateVariableDef variable;
            int type = 0;
            int defaultBool = 0;
            input >> variable.id >> type >> variable.defaultInt >> defaultBool;
            variable.type = static_cast<StateVariableType>(std::clamp(type, 0, 2));
            variable.defaultBool = defaultBool != 0;
            if (!variable.id.empty()) {
                loaded.stateVariables.push_back(std::move(variable));
            }
        } else if (version >= 4 && key == "effect_defs") {
            std::size_t count = 0;
            input >> count;
            loaded.effectDefs.reserve(count);
        } else if (version >= 4 && key == "effect_def") {
            GameEffectDef effect;
            int type = 0;
            int boolValue = 0;
            input >> effect.id >> type >> effect.targetId >> effect.intValue >> boolValue;
            effect.type = static_cast<GameEffectType>(std::clamp(type, 0, 4));
            effect.boolValue = boolValue != 0;
            if (!effect.id.empty()) {
                loaded.effectDefs.push_back(std::move(effect));
            }
        } else if (version >= 5 && key == "npc_types") {
            std::size_t count = 0;
            input >> count;
            loaded.npcTypes.reserve(count);
        } else if (version >= 5 && key == "npc_type") {
            NpcTypeDef npc;
            int movement = 0;
            int interaction = 1;
            std::string spriteId;
            std::string characterId;
            std::string graphId;
            input >> npc.id >> spriteId >> characterId >> movement >> interaction >> npc.defaultSpeed >> graphId;
            npc.spriteId = (spriteId == "-") ? std::string{} : spriteId;
            npc.characterId = (characterId == "-") ? std::string{} : characterId;
            npc.defaultGraphId = (graphId == "-") ? std::string{} : graphId;
            npc.defaultMovement = static_cast<NpcMovementMode>(std::clamp(movement, 0, 2));
            npc.defaultInteraction = static_cast<NpcInteractionMode>(std::clamp(interaction, 0, 3));
            if (version >= 6) {
                int dlCount = 0;
                input >> dlCount;
                int shopCount = 0;
                if (version >= 11) {
                    input >> shopCount;
                }
                for (int di = 0; di < dlCount && input; ++di) {
                    std::string dlKey;
                    DialogueLine dl;
                    input >> dlKey >> std::quoted(dl.speaker) >> std::quoted(dl.text);
                    if (dlKey == "dl") {
                        npc.defaultDialogue.push_back(std::move(dl));
                    }
                }
                for (int si = 0; si < shopCount && input; ++si) {
                    std::string shopKey;
                    std::string itemId;
                    ShopItemDef shopItem;
                    int unlimited = 1;
                    input >> shopKey >> itemId >> shopItem.buyPrice >> shopItem.sellPrice >> shopItem.quantity >> unlimited;
                    if (shopKey == "shop_item") {
                        shopItem.itemId = (itemId == "-") ? std::string{} : itemId;
                        shopItem.unlimited = unlimited != 0;
                        shopItem.quantity = std::max(0, shopItem.quantity);
                        shopItem.buyPrice = std::max(0, shopItem.buyPrice);
                        shopItem.sellPrice = std::max(0, shopItem.sellPrice);
                        npc.shopInventory.push_back(std::move(shopItem));
                    }
                }
            }
            if (!npc.id.empty()) {
                loaded.npcTypes.push_back(std::move(npc));
            }
        } else if (key == "end") {
            break;
        }
    }

    if (!validToken(loaded.id)) {
        setError(errorMessage, "Loaded game project is invalid.");
        return false;
    }

    project = std::move(loaded);
    return true;
}

} // namespace adventure::game
