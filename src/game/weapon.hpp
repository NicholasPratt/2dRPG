#pragma once

#include <string>

namespace adventure::game {

enum class WeaponType { Melee = 0, Ranged = 1 };

// What a ranged projectile does when it strikes a solid wall.
enum class ProjectileWallBehavior {
    Break = 0,    // vanish on impact (arrow, bullet)
    Rebound = 1,  // bounce off the wall, lose energy, then settle on the ground (slingshot stone)
};

// What happens when a hold-to-draw weapon is held past full draw + its overcharge window.
enum class OverchargeEffect {
    None = 0,      // can be held at full draw indefinitely
    WildShot = 1,  // accuracy degrades the longer it is overheld (spread keeps growing)
    Misfire = 2,   // the shot fizzles: ammo is spent, projectile does no damage
    Break = 3,     // the weapon breaks and is unequipped (shot lost, ammo kept)
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
    // Player animation action played when attacking with this weapon (matches
    // sprite frame action types, e.g. "attack_1".."attack_20", "cast"). Empty =
    // default: "attack_1" for melee, "cast" for ranged. (ADGAME v15+)
    std::string attackAnimState;
    ProjectileWallBehavior wallBehavior = ProjectileWallBehavior::Break;  // ranged only (ADGAME v13+)

    // --- Ranged feel stats (ADGAME v14+). A weapon is hold-to-draw when
    // chargeTimeSeconds > 0 or steadyTimeSeconds > 0: the ranged button charges
    // while held and fires on release. Otherwise it fires instantly on press.

    // Draw / charge: damage ramps from chargeDamageScaleMin (tap) to
    // chargeDamageScaleMax (full draw) over chargeTimeSeconds (slingshot).
    float chargeTimeSeconds = 0.0f;
    float chargeDamageScaleMin = 1.0f;
    float chargeDamageScaleMax = 1.0f;
    // Holding past full draw for longer than overchargeTimeSeconds triggers
    // overchargeEffect (0 seconds with effect None = hold forever).
    float overchargeTimeSeconds = 0.0f;
    OverchargeEffect overchargeEffect = OverchargeEffect::None;

    // Accuracy: shot direction is perturbed by up to ±spread/2 degrees. Spread
    // interpolates from spreadStartDegrees to spreadEndDegrees over
    // steadyTimeSeconds of holding (sniper: large -> 0). 0 steady time = fixed.
    float spreadStartDegrees = 0.0f;
    float spreadEndDegrees = 0.0f;
    float steadyTimeSeconds = 0.0f;
    int pelletCount = 1;            // projectiles per shot, each with its own spread roll (shotgun)

    // Damage falloff over flight distance: full damage up to falloffStartPx,
    // scaling linearly down to falloffMinScale at falloffEndPx (shotgun).
    // falloffStartPx <= 0 disables falloff.
    float falloffStartPx = 0.0f;
    float falloffEndPx = 0.0f;
    float falloffMinDamageScale = 1.0f;

    // Auto-aim: enemies within range and inside this cone (full angle, centred
    // on player facing) become selectable targets. <= 0 disables auto-aim.
    float aimConeDegrees = 45.0f;
};

} // namespace adventure::game
