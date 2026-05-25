#pragma once

#include <string>

namespace adventure::game {

enum class WeaponType { Melee = 0, Ranged = 1 };

struct WeaponDef {
    std::string id = "sword_1";
    WeaponType type = WeaponType::Melee;
    int damage = 1;
    float range = 24.0f;            // melee: reach px from player centre; ranged: max flight px
    float attackCooldown = 0.5f;    // seconds between attacks
    float projectileSpeed = 200.0f; // ranged only
    std::string spriteId;           // visual sprite for weapon/projectile
    std::string ammoTypeId;         // ranged only: key into ammo pool (defaults to id if empty)
    int ammoPerShot = 1;
};

} // namespace adventure::game
