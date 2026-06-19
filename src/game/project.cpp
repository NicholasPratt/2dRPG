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

std::string tokenOrDash(const std::string& value)
{
    return value.empty() ? "-" : value;
}

void writeCondition(std::ostream& output, const DialogueCondition& condition)
{
    output << static_cast<int>(condition.type) << ' '
           << static_cast<int>(condition.op) << ' '
           << tokenOrDash(condition.variableId) << ' '
           << condition.intValue << ' '
           << (condition.boolValue ? 1 : 0) << ' '
           << static_cast<int>(condition.scope);
}

bool readCondition(std::istream& input, DialogueCondition& condition)
{
    int type = 0;
    int op = 0;
    int boolValue = 0;
    int scope = 0;
    std::string variableId;
    input >> type >> op >> variableId >> condition.intValue >> boolValue >> scope;
    condition.type = static_cast<DialogueConditionType>(std::clamp(type, 0, 4));
    condition.op = static_cast<DialogueCompareOp>(std::clamp(op, 0, 5));
    condition.variableId = variableId == "-" ? std::string{} : variableId;
    condition.boolValue = boolValue != 0;
    condition.scope = static_cast<StateVariableScope>(std::clamp(scope, 0, 1));
    return static_cast<bool>(input);
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
    for (const ChapterSynopsisDef& synopsis : project.chapterSynopses) {
        if (!validToken(synopsis.chapterId)) {
            setError(errorMessage, "Chapter synopsis id is invalid: " + synopsis.chapterId);
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

    output << "ADGAME 17\n";
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
    output << "chapter_synopses " << project.chapterSynopses.size() << "\n";
    for (const ChapterSynopsisDef& synopsis : project.chapterSynopses) {
        output << "chapter_synopsis " << synopsis.chapterId << ' ' << std::quoted(synopsis.text) << "\n";
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
               << ' ' << static_cast<int>(type.killVariableScope)
               << ' ' << type.defeatEffectIds.size()
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
        for (const std::string& effectId : type.defeatEffectIds) {
            output << "enemy_defeat_effect " << effectId << "\n";
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
               << ' ' << static_cast<int>(w.wallBehavior)
               << ' ' << w.chargeTimeSeconds
               << ' ' << w.chargeDamageScaleMin
               << ' ' << w.chargeDamageScaleMax
               << ' ' << w.overchargeTimeSeconds
               << ' ' << static_cast<int>(w.overchargeEffect)
               << ' ' << w.spreadStartDegrees
               << ' ' << w.spreadEndDegrees
               << ' ' << w.steadyTimeSeconds
               << ' ' << w.pelletCount
               << ' ' << w.falloffStartPx
               << ' ' << w.falloffEndPx
               << ' ' << w.falloffMinDamageScale
               << ' ' << w.aimConeDegrees
               << ' ' << (w.attackAnimState.empty() ? "-" : w.attackAnimState) << "\n";
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
               << ' ' << item.acquireEffectIds.size()
               << "\n";
        for (const std::string& effectId : item.acquireEffectIds) {
            output << "item_acquire_effect " << effectId << "\n";
        }
    }
    output << "starting_weapon " << (project.startingWeaponId.empty() ? "-" : project.startingWeaponId) << "\n";
    output << "font " << std::quoted(project.fontPath.empty() ? std::string{"-"} : project.fontPath) << "\n";
    output << "state_defs " << project.stateVariables.size() << "\n";
    for (const StateVariableDef& variable : project.stateVariables) {
        output << "state_def " << variable.id
               << ' ' << static_cast<int>(variable.type)
               << ' ' << variable.defaultInt
               << ' ' << (variable.defaultBool ? 1 : 0)
               << ' ' << static_cast<int>(variable.scope)
               << ' ' << tokenOrDash(variable.chapterId) << "\n";
    }
    output << "effect_defs " << project.effectDefs.size() << "\n";
    for (const GameEffectDef& effect : project.effectDefs) {
        output << "effect_def " << effect.id
               << ' ' << static_cast<int>(effect.type)
               << ' ' << effect.targetId
               << ' ' << effect.intValue
               << ' ' << (effect.boolValue ? 1 : 0)
               << ' ' << static_cast<int>(effect.scope) << "\n";
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
               << ' ' << npc.shopInventory.size()
               << ' ' << npc.talkEffectIds.size()
               << ' ' << npc.stateRules.size() << "\n";
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
        for (const std::string& effectId : npc.talkEffectIds) {
            output << "npc_talk_effect " << effectId << "\n";
        }
        for (const NpcStateRule& rule : npc.stateRules) {
            output << "npc_rule ";
            writeCondition(output, rule.condition);
            output << ' ' << tokenOrDash(rule.graphId)
                   << ' ' << rule.movementOverride
                   << ' ' << tokenOrDash(rule.animation)
                   << ' ' << rule.visibility
                   << ' ' << rule.following
                   << ' ' << rule.activateEffectIds.size() << "\n";
            for (const std::string& effectId : rule.activateEffectIds) {
                output << "npc_rule_effect " << effectId << "\n";
            }
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
    if (magic != "ADGAME" || version < 1 || version > 17) {
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
        } else if (version >= 17 && key == "chapter_synopses") {
            std::size_t count = 0;
            input >> count;
            loaded.chapterSynopses.reserve(count);
        } else if (version >= 17 && key == "chapter_synopsis") {
            ChapterSynopsisDef synopsis;
            input >> synopsis.chapterId >> std::quoted(synopsis.text);
            if (!synopsis.chapterId.empty()) {
                loaded.chapterSynopses.push_back(std::move(synopsis));
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
            int defeatEffectCount = 0;
            if (version >= 16) {
                int scope = 0;
                input >> scope >> defeatEffectCount;
                type.killVariableScope = static_cast<StateVariableScope>(std::clamp(scope, 0, 1));
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
            for (int ei = 0; ei < defeatEffectCount && input; ++ei) {
                std::string effectKey;
                std::string effectId;
                input >> effectKey >> effectId;
                if (effectKey == "enemy_defeat_effect") {
                    type.defeatEffectIds.push_back(effectId);
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
            if (version >= 14) {
                int overchargeEffect = 0;
                input >> w.chargeTimeSeconds >> w.chargeDamageScaleMin >> w.chargeDamageScaleMax
                      >> w.overchargeTimeSeconds >> overchargeEffect
                      >> w.spreadStartDegrees >> w.spreadEndDegrees >> w.steadyTimeSeconds
                      >> w.pelletCount
                      >> w.falloffStartPx >> w.falloffEndPx >> w.falloffMinDamageScale
                      >> w.aimConeDegrees;
                w.overchargeEffect = static_cast<OverchargeEffect>(std::clamp(overchargeEffect, 0, 3));
            }
            if (version >= 15) {
                std::string attackAnim;
                input >> attackAnim;
                w.attackAnimState = (attackAnim == "-") ? "" : attackAnim;
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
            int acquireEffectCount = 0;
            if (version >= 16) {
                input >> acquireEffectCount;
            }
            item.type = static_cast<ItemDefType>(std::clamp(type, 0, 10));
            item.spriteId = (spriteId == "-") ? std::string{} : spriteId;
            item.targetId = (targetId == "-") ? std::string{} : targetId;
            item.stackable = stackable != 0;
            item.customType = (customType == "-") ? std::string{} : customType;
            for (int ei = 0; ei < acquireEffectCount && input; ++ei) {
                std::string effectKey;
                std::string effectId;
                input >> effectKey >> effectId;
                if (effectKey == "item_acquire_effect") {
                    item.acquireEffectIds.push_back(effectId);
                }
            }
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
            if (version >= 16) {
                int scope = 0;
                std::string chapterId;
                input >> scope >> chapterId;
                variable.scope = static_cast<StateVariableScope>(std::clamp(scope, 0, 1));
                variable.chapterId = chapterId == "-" ? std::string{} : chapterId;
            }
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
            if (version >= 16) {
                int scope = 0;
                input >> scope;
                effect.scope = static_cast<StateVariableScope>(std::clamp(scope, 0, 1));
            }
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
                int talkEffectCount = 0;
                int ruleCount = 0;
                if (version >= 16) {
                    input >> talkEffectCount >> ruleCount;
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
                for (int ei = 0; ei < talkEffectCount && input; ++ei) {
                    std::string effectKey;
                    std::string effectId;
                    input >> effectKey >> effectId;
                    if (effectKey == "npc_talk_effect") {
                        npc.talkEffectIds.push_back(effectId);
                    }
                }
                for (int ri = 0; ri < ruleCount && input; ++ri) {
                    std::string ruleKey;
                    NpcStateRule rule;
                    std::string graphId;
                    std::string animation;
                    int effectCount = 0;
                    input >> ruleKey;
                    if (ruleKey != "npc_rule" || !readCondition(input, rule.condition)) {
                        break;
                    }
                    input >> graphId >> rule.movementOverride >> animation
                          >> rule.visibility >> rule.following >> effectCount;
                    rule.graphId = graphId == "-" ? std::string{} : graphId;
                    rule.animation = animation == "-" ? std::string{} : animation;
                    for (int ei = 0; ei < effectCount && input; ++ei) {
                        std::string effectKey;
                        std::string effectId;
                        input >> effectKey >> effectId;
                        if (effectKey == "npc_rule_effect") {
                            rule.activateEffectIds.push_back(effectId);
                        }
                    }
                    npc.stateRules.push_back(std::move(rule));
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
