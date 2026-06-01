#pragma once

#include <string>

namespace adventure::game {

enum class WeaponType { Melee = 0, Ranged = 1 };

// What a ranged projectile does when it strikes a solid wall.
enum class ProjectileWallBehavior {
    Break = 0,    // vanish on impact (arrow, bullet)
    Rebound = 1,  // bounce off the wall, lose energy, then settle on the ground (slingshot stone)
};

struct WeaponDef {
    std::string id = "sword_1";
    WeaponType type = WeaponType::Melee;
    int damage = 1;
    float range = 24.0f;            // melee: reach px from player centre; ranged: max flight px
    float attackCooldown = 0.5f;    // seconds between attacks
    float projectileSpeed = 200.0f; // ranged only
    std::string spriteId;           // visual sprite for the weapon itself
    std::string ammoTypeId;         // ranged only: key into ammo pool (defaults to id if empty)
    std::string ammoSpriteId;       // ranged only: projectile/ammo sprite (defaults to spriteId if empty)
    int ammoPerShot = 1;
    ProjectileWallBehavior wallBehavior = ProjectileWallBehavior::Break;  // ranged only (ADGAME v13+)
};

} // namespace adventure::game
