# Mahjong
# Concrete IGame implementation (MahjongGame, the rules in Game/ and the scene glue in Components/ to come) and the BagiMahjong executable.

# Standalone build: the game in its own repository, the engine as the git submodule engine/.
# Inside the engine repository the engine is already set up by the root CMakeLists.txt and this block is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.31)
    project(BagiMahjong)
    include(${CMAKE_CURRENT_SOURCE_DIR}/engine/cmake/BagiEngine.cmake)
endif()

be_add_module(Mahjong LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
be_add_game_reflection(MahjongModule)

be_add_game_executable(BagiMahjong
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES MahjongModule
)

# The bot arena: a command-line program that plays many hands of the computer players with no window (docs/bot-arena.md). The rules
# and the arena are plain C++ in MahjongModule; the program needs the engine core (BECoreModule) and nothing of the runtime.
# Not built for the web: a plain add_executable would link a second wasm without the content package.
if(NOT ENGINE_TARGET_WEB)
    add_executable(BagiMahjongArena tools/arena/main.cpp)
    set_target_properties(BagiMahjongArena PROPERTIES FOLDER "Tools")
    target_precompile_headers(BagiMahjongArena REUSE_FROM BECoreModule)
    be_force_include_pch(BagiMahjongArena)
    target_compile_definitions(BagiMahjongArena PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
    target_include_directories(BagiMahjongArena PRIVATE ${CMAKE_BINARY_DIR})
    be_link_engine(BagiMahjongArena)
    target_link_libraries(BagiMahjongArena PRIVATE MahjongModule)
    be_deploy_runtime_libs(BagiMahjongArena)
    if(APPLE)
        set_target_properties(BagiMahjongArena PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
    endif()
endif()

if(ENGINE_BUILD_TESTS)
    file(GLOB MAHJONG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: MahjongGameTest starts the game through GameManager, which finds MahjongGame by its BE_CLASS registration only
    be_add_game_tests(MahjongTests
        SOURCES ${MAHJONG_TEST_SOURCES}
        WHOLE_ARCHIVE MahjongModule
    )

    # Starts BagiMahjong with the control API and plays a hand and a match over WebSocket (ctest entry BagiMahjongControlSmoke);
    # without the control server (ENGINE_WITH_REMOTE_CONTROL=OFF) the smoke could never connect
    if(ENGINE_WITH_REMOTE_CONTROL)
        be_add_game_control_tests(BagiMahjong SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)
    endif()
endif()
