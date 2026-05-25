#include "game/project.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path);
    if (!output) {
        setError(errorMessage, "Could not open game project for writing.");
        return false;
    }

    output << "ADGAME 3\n";
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
    if (magic != "ADGAME" || version < 1 || version > 3) {
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
