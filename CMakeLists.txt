cmake_minimum_required(VERSION 3.20)

project(Adventure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)

target_include_directories(imgui PUBLIC
    external/imgui
)

add_library(adventure_game STATIC
    src/game/chapter.cpp
    src/game/dialogue_graph.cpp
    src/game/path.cpp
    src/game/project.cpp
    src/game/map.cpp
    src/game/sprite.cpp
    src/game/state.cpp
    src/game/tileset.cpp
    # weapon.hpp is header-only; no .cpp needed
)

target_include_directories(adventure_game PUBLIC
    src
)

add_library(adventure_editor STATIC
    src/editor/asset_directories.cpp
    src/editor/editor_app.cpp
    src/editor/editor_state.cpp
    src/editor/stb_image_impl.cpp
    src/editor/panels/character_editor_panel.cpp
    src/editor/panels/dialogue_graph_editor_panel.cpp
    src/editor/panels/enemy_path_editor_panel.cpp
    src/editor/panels/item_placement_panel.cpp
    src/editor/panels/layout_editor_panel.cpp
    src/editor/panels/map_editor_panel.cpp
    src/editor/panels/npc_editor_panel.cpp
    src/editor/panels/sprite_editor_panel.cpp
    src/editor/panels/tileset_editor_panel.cpp
    src/editor/panels/wall_floor_paint_panel.cpp
    src/editor/panels/weapon_editor_panel.cpp
)

target_include_directories(adventure_editor PUBLIC
    src
    external/stb
)

target_compile_definitions(adventure_editor PRIVATE
    ADVENTURE_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(adventure_editor PUBLIC
    imgui
    adventure_game
)

add_executable(adventure_editor_smoke
    src/app/main_editor_smoke.cpp
)

target_link_libraries(adventure_editor_smoke PRIVATE
    adventure_editor
)

add_executable(adventure_game_smoke
    src/app/main_game_smoke.cpp
)

target_link_libraries(adventure_game_smoke PRIVATE
    adventure_game
)

find_package(OpenGL)
find_package(PkgConfig)

if(OpenGL_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(GLFW3 glfw3)
    pkg_check_modules(SDL2 sdl2)
    pkg_check_modules(VORBISFILE vorbisfile)
endif()

if(OpenGL_FOUND AND GLFW3_FOUND)
    if(SDL2_FOUND AND VORBISFILE_FOUND)
        add_executable(adventure_game_window
            src/app/main_game.cpp
            src/game/engine.cpp
            src/editor/stb_image_impl.cpp
            external/imgui/backends/imgui_impl_glfw.cpp
            external/imgui/backends/imgui_impl_opengl2.cpp
        )

        target_include_directories(adventure_game_window PRIVATE
            external/imgui
            external/imgui/backends
            external/stb
            ${GLFW3_INCLUDE_DIRS}
            ${SDL2_INCLUDE_DIRS}
            ${VORBISFILE_INCLUDE_DIRS}
        )

        target_compile_definitions(adventure_game_window PRIVATE
            ADVENTURE_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
            SDL_MAIN_HANDLED
        )

        target_compile_options(adventure_game_window PRIVATE
            ${GLFW3_CFLAGS_OTHER}
            ${SDL2_CFLAGS_OTHER}
            ${VORBISFILE_CFLAGS_OTHER}
        )

        target_link_directories(adventure_game_window PRIVATE
            ${GLFW3_LIBRARY_DIRS}
            ${SDL2_LIBRARY_DIRS}
            ${VORBISFILE_LIBRARY_DIRS}
        )

        target_link_libraries(adventure_game_window PRIVATE
            imgui
            adventure_game
            OpenGL::GL
            ${GLFW3_LIBRARIES}
            ${SDL2_LIBRARIES}
            ${VORBISFILE_LIBRARIES}
        )
    endif()

    add_executable(adventure_editor_window
        src/app/main_editor.cpp
        external/imgui/backends/imgui_impl_glfw.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(adventure_editor_window PRIVATE
        external/imgui/backends
        ${GLFW3_INCLUDE_DIRS}
    )

    target_compile_options(adventure_editor_window PRIVATE
        ${GLFW3_CFLAGS_OTHER}
    )

    target_link_directories(adventure_editor_window PRIVATE
        ${GLFW3_LIBRARY_DIRS}
    )

    target_link_libraries(adventure_editor_window PRIVATE
        adventure_editor
        OpenGL::GL
        ${GLFW3_LIBRARIES}
    )
endif()
