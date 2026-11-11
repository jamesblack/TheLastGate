cmake_minimum_required(VERSION 4.1.0)

# vcpkg integration - must be set before project()
# Set CMAKE_TOOLCHAIN_FILE if not already set (allows command-line override)
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if (DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "vcpkg toolchain file")
    else ()
        message(WARNING "VCPKG_ROOT environment variable not set. "
                "Please set it or specify CMAKE_TOOLCHAIN_FILE manually. "
                "Example: cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..")
    endif ()
endif ()

# Set default triplet based on platform
# The compiler is not known until project() runs, so MINGW/MSVC can't be tested here.
# The client only builds with GCC/Clang (-mwindows etc.), so Windows always means MinGW.
if (NOT DEFINED VCPKG_TARGET_TRIPLET)
    if (CMAKE_HOST_WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg target triplet")
        message(STATUS "Windows host, using vcpkg triplet: x64-mingw-dynamic")
    elseif (UNIX AND NOT APPLE)
        set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg target triplet")
        message(STATUS "Detected Linux, using vcpkg triplet: x64-linux")
    else ()
        set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg target triplet")
        message(STATUS "Using vcpkg triplet: x64-windows")
    endif ()
endif ()

# Build vcpkg's host tools with MinGW too, so Visual Studio isn't required
if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_HOST_TRIPLET)
    set(VCPKG_HOST_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg host triplet")
endif ()

project(TheLastGate LANGUAGES C CXX)

option(HOME_COPY "Connect to Home Server" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_options.cmake)

# Platform-specific source files
if (WIN32)
    set(PLATFORM_SOURCES TheLastGate.rc)
else ()
    set(PLATFORM_SOURCES)
endif ()

add_executable(TheLastGate
        ${PLATFORM_SOURCES}
        engine.c
        inter.c
        main.c
        options.c
        src/audio/sound.c
        src/config/config.c
        src/config/keybindings.c
        src/game/attributes.c
        src/game/buff_focus.c
        src/game/buff_model.c
        src/game/combat_model.c
        src/game/game_input.c
        src/game/planner_choices.c
        src/game/minimap.c
        src/game/minimap_store.c
        src/game/game_ui.c
        src/game/stat_display.c
        src/game/skill_focus.c
        src/game/skill_preview.c
        src/game/stat_raise.c
        src/graphics/atlas.c
        src/graphics/item_display.c
        src/graphics/loader.c
        src/graphics/render.c
        src/graphics/scaling.c
        src/graphics/sdl.c
        src/graphics/sdl_magic.c
        src/graphics/shaders/effect_shader.c
        src/graphics/shaders/magic_shader.c
        src/graphics/shaders/shaders.c
        src/graphics/shaders/solid_shader.c
        src/input/input.c
        src/launcher/character_manager.c
        src/launcher/launcher.c
        src/launcher/launcher_input.c
        src/log/log.c
        src/mods/base_cost_warning.c
        src/mods/buff_list.c
        src/mods/buff_watch.c
        src/mods/give_more.c
        src/mods/item_details.c
        src/mods/item_tooltip.c
        src/mods/stubborn_actions.c
        src/mods/use_queue.c
        src/net/connection.c
        src/net/socket.c
        src/net/tick_pacer.c
        src/security/security.c
        src/security/single_instance.c
        src/security/unique_id.c
        src/ui/hotbar.cpp
        src/ui/enhanced_skill_window.cpp
        src/ui/inventory.cpp
        src/ui/item_tooltip_ui.cpp
        src/ui/launcher_ui.cpp
        src/ui/option_window.cpp
        src/ui/perf_window.cpp
        src/ui/quick_stats.cpp
        src/ui/ui.cpp
        src/ui/ui_utils/imgui_helpers.cpp
        src/ui/ui_utils/imgui_style_builder.cpp
        src/ui/widgets.cpp
        src/util/perf.c
        src/util/nearest_to_mouse.c
)

target_precompile_headers(TheLastGate PRIVATE
        src/ui/ui_ffi.h
)

target_include_directories(TheLastGate PRIVATE
        ${CMAKE_BINARY_DIR}/_generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/
        ${CMAKE_CURRENT_SOURCE_DIR}/
)

# Use C++ linker for mixed C/C++ project (C++ linker handles C code correctly)
# This is REQUIRED because we link against imgui_wrapper (C++ library)
# The C++ linker can link both C and C++ object files without issues
set_target_properties(TheLastGate PROPERTIES LINKER_LANGUAGE CXX)

# Disable warnings for old code compatibility
# TODO: These warnings indicate real issues that should be fixed during modernization
target_compile_options(TheLastGate PRIVATE
        -Wno-incompatible-pointer-types  # Disable pointer type mismatch warnings
        -Wno-int-conversion              # Disable int/pointer conversion warnings (often related)
)

option(TLG_BUILD_TESTS "Build client unit tests" ON)
if (TLG_BUILD_TESTS)
    enable_testing()
    add_executable(item_details_test
            tests/item_details_test.c
            src/mods/item_details.c
    )
    target_include_directories(item_details_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME item_details COMMAND item_details_test)

    add_executable(buff_list_test
            tests/buff_list_test.c
            src/mods/buff_list.c
    )
    target_include_directories(buff_list_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME buff_list COMMAND buff_list_test)

    add_executable(buff_watch_test
            tests/buff_watch_test.c
            src/mods/buff_watch.c
            src/mods/buff_list.c
    )
    target_include_directories(buff_watch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ ${CMAKE_CURRENT_SOURCE_DIR}/)
    target_precompile_headers(buff_watch_test PRIVATE src/ui/ui_ffi.h)
    add_test(NAME buff_watch COMMAND buff_watch_test)

    add_executable(buff_model_test
            tests/buff_model_test.c
            src/game/buff_model.c
    )
    target_include_directories(buff_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME buff_model COMMAND buff_model_test)

    add_executable(combat_model_test
            tests/combat_model_test.c
            src/game/combat_model.c
    )
    target_include_directories(combat_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME combat_model COMMAND combat_model_test)

    add_executable(planner_choices_test
            tests/planner_choices_test.c
            src/game/planner_choices.c
    )
    target_include_directories(planner_choices_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME planner_choices COMMAND planner_choices_test)

    find_package(ZLIB REQUIRED)
    add_executable(minimap_store_test
            tests/minimap_store_test.c
            src/game/minimap_store.c
    )
    target_include_directories(minimap_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    target_link_libraries(minimap_store_test PRIVATE ZLIB::ZLIB)
    add_test(NAME minimap_store COMMAND minimap_store_test)

    add_executable(buff_focus_test
            tests/buff_focus_test.c
            src/game/buff_focus.c
            src/game/buff_model.c
    )
    target_include_directories(buff_focus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME buff_focus COMMAND buff_focus_test)

    add_executable(skill_preview_test
            tests/skill_preview_test.c
            src/game/skill_preview.c
    )
    target_include_directories(skill_preview_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME skill_preview COMMAND skill_preview_test)

    add_executable(skill_focus_test
            tests/skill_focus_test.c
            src/game/skill_focus.c
    )
    target_include_directories(skill_focus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME skill_focus COMMAND skill_focus_test)

    add_executable(stat_raise_test
            tests/stat_raise_test.c
            src/game/stat_raise.c
    )
    target_include_directories(stat_raise_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ ${CMAKE_CURRENT_SOURCE_DIR}/)
    target_precompile_headers(stat_raise_test PRIVATE src/ui/ui_ffi.h)
    add_test(NAME stat_raise COMMAND stat_raise_test)

    add_executable(tick_pacer_test
            tests/tick_pacer_test.c
            src/net/tick_pacer.c
    )
    target_include_directories(tick_pacer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    add_test(NAME tick_pacer COMMAND tick_pacer_test)
endif ()

include(cmake/dependencies/sdl2.cmake)

include(cmake/dependencies/pnglib.cmake)

include(cmake/dependencies/imgui.cmake)

include(cmake/dependencies/opengl.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_setup.cmake)

if (HOME_COPY)
    target_compile_options(TheLastGate PRIVATE
            -DHOMECOPY
    )
endif ()

# Release build optimizations
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(TheLastGate PRIVATE
            -O3                    # Maximum optimization
            -march=x86-64          # Optimize for x86-64 compatibility
            -ffast-math            # Fast floating-point math (safe for game logic)
            -DNDEBUG               # Disable assertions (enables the printf macro suppression)
    )

    target_link_options(TheLastGate PRIVATE
            -s                     # Strip debug symbols
    )
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tinyfiledialogs.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_runtime_dlls.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/include_packaged_files.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/appimage.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_target.cmake)