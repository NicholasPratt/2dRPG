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
    src/game/path.cpp
    src/game/map.cpp
    src/game/sprite.cpp
    src/game/tileset.cpp
)

target_include_directories(adventure_game PUBLIC
    src
)

add_library(adventure_editor STATIC
    src/editor/asset_directories.cpp
    src/editor/editor_app.cpp
    src/editor/stb_image_impl.cpp
    src/editor/panels/character_editor_panel.cpp
    src/editor/panels/enemy_path_editor_panel.cpp
    src/editor/panels/layout_editor_panel.cpp
    src/editor/panels/map_editor_panel.cpp
    src/editor/panels/sprite_editor_panel.cpp
    src/editor/panels/tileset_editor_panel.cpp
    src/editor/panels/wall_floor_paint_panel.cpp
)

target_include_directories(adventure_editor PUBLIC
    src
    external/stb
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
endif()

if(OpenGL_FOUND AND GLFW3_FOUND)
    add_executable(adventure_game_window
        src/app/main_game.cpp
        src/game/engine.cpp
        src/editor/stb_image_impl.cpp
    )

    target_include_directories(adventure_game_window PRIVATE
        external/stb
        ${GLFW3_INCLUDE_DIRS}
    )

    target_compile_options(adventure_game_window PRIVATE
        ${GLFW3_CFLAGS_OTHER}
    )

    target_link_directories(adventure_game_window PRIVATE
        ${GLFW3_LIBRARY_DIRS}
    )

    target_link_libraries(adventure_game_window PRIVATE
        adventure_game
        OpenGL::GL
        ${GLFW3_LIBRARIES}
    )

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
