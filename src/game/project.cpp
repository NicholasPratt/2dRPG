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

    output << "ADGAME 7\n";
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
               << type.attackCooldownSeconds << ' ' << type.speed << "\n";
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
               << ' ' << w.ammoPerShot << "\n";
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
               << ' ' << npc.defaultDialogue.size() << "\n";
        for (const DialogueLine& dl : npc.defaultDialogue) {
            output << "dl " << std::quoted(dl.speaker) << ' ' << std::quoted(dl.text) << "\n";
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
    if (magic != "ADGAME" || version < 1 || version > 7) {
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
            input >> w.id >> weaponType >> w.damage >> w.range >> w.attackCooldown
                  >> w.projectileSpeed >> spriteId >> ammoTypeId >> w.ammoPerShot;
            w.type = (weaponType == 1) ? WeaponType::Ranged : WeaponType::Melee;
            w.spriteId = (spriteId == "-") ? std::string{} : spriteId;
            w.ammoTypeId = (ammoTypeId == "-") ? std::string{} : ammoTypeId;
            if (!w.id.empty()) {
                loaded.weaponDefs.push_back(std::move(w));
            }
        } else if (version >= 3 && key == "starting_weapon") {
            input >> loaded.startingWeaponId;
            if (loaded.startingWeaponId == "-") {
                loaded.startingWeaponId.clear();
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
                for (int di = 0; di < dlCount && input; ++di) {
                    std::string dlKey;
                    DialogueLine dl;
                    input >> dlKey >> std::quoted(dl.speaker) >> std::quoted(dl.text);
                    if (dlKey == "dl") {
                        npc.defaultDialogue.push_back(std::move(dl));
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
