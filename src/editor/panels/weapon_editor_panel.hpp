#pragma once

#include "editor/editor_context.hpp"
#include "game/weapon.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace adventure::editor {

class WeaponEditorPanel {
public:
    std::optional<std::filesystem::path> draw(EditorContext& context);
    void loadWeapons(EditorContext& context);
    void saveWeapons(EditorContext& context);

private:
    int selectedWeapon_ = -1;
    bool projectLoaded_ = false;
    std::string lastLoadedProjectRoot_;

    // Inspector buffers for the selected weapon
    std::array<char, 64> weaponId_{'s', 'w', 'o', 'r', 'd', '_', '1', '\0'};
    int weaponType_ = 0;
    int damage_ = 1;
    float range_ = 24.0f;
    float attackCooldown_ = 0.5f;
    float projectileSpeed_ = 200.0f;
    std::array<char, 64> spriteId_{'\0'};
    std::array<char, 64> ammoTypeId_{'\0'};
    std::array<char, 64> ammoSpriteId_{'\0'};
    int ammoPerShot_ = 1;

    void syncInspectorFromSelected(const EditorContext& context);
    void writeInspectorToSelected(EditorContext& context);
};

} // namespace adventure::editor
