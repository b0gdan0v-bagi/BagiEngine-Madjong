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

if(ENGINE_BUILD_TESTS)
    file(GLOB MAHJONG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: MahjongGameTest starts the game through GameManager, which finds MahjongGame by its BE_CLASS registration only
    be_add_game_tests(MahjongTests
        SOURCES ${MAHJONG_TEST_SOURCES}
        WHOLE_ARCHIVE MahjongModule
    )

    # Starts BagiMahjong with the control API and plays a hand and a match over WebSocket (ctest entry BagiMahjongControlSmoke)
    be_add_game_control_tests(BagiMahjong SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)
endif()
