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
    RoomClientModule
    SDLModule
)
be_add_game_reflection(MahjongModule)

# The build id that every match log carries (src/Mahjong/BuildInfo.h): the short git hash, read now, at configure time, and given to
# BuildInfo.cpp alone so that nothing else recompiles. A commit made after this step shows only after the next `cmake --preset`.
set(MAHJONG_BUILD_ID unknown)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE MAHJONG_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE MAHJONG_GIT_RESULT
    )
    if(MAHJONG_GIT_RESULT EQUAL 0 AND MAHJONG_GIT_HASH MATCHES "^[0-9a-f]+$")
        set(MAHJONG_BUILD_ID ${MAHJONG_GIT_HASH})
    endif()
endif()
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Mahjong/BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS "MAHJONG_BUILD_ID=\"${MAHJONG_BUILD_ID}\"")

be_add_game_executable(BagiMahjong
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES MahjongModule
)

# The command-line tools: plain programs with no window, no runtime and no content package. The rules, the arena and the analyzer
# are plain C++ in MahjongModule; a program needs the engine core (BECoreModule) and nothing else.
function(mahjong_add_tool target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES FOLDER "Tools")
    target_precompile_headers(${target} REUSE_FROM BECoreModule)
    be_force_include_pch(${target})
    target_compile_definitions(${target} PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
    target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR})
    be_link_engine(${target})
    target_link_libraries(${target} PRIVATE MahjongModule)
    be_deploy_runtime_libs(${target})
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
    endif()
endfunction()

# The bot arena (docs/bot-arena.md) and the EV analyzer of one position (docs/ev-analyzer.md).
# Not built for the web: a plain add_executable would link a second wasm without the content package.
if(NOT ENGINE_TARGET_WEB)
    mahjong_add_tool(BagiMahjongArena tools/arena/main.cpp)
    mahjong_add_tool(BagiMahjongAnalyzer tools/analyzer/main.cpp)
endif()

# The room server of online play (README.md, "Online play, the server"): the headless program of the engine's server main with MahjongSeatedGame
# registered as `mahjong`; no window, no renderer, no Application. RoomServerModule exists only with ENGINE_WITH_REMOTE_CONTROL (sockets), so
# not for the web.
if(NOT ENGINE_TARGET_WEB AND TARGET RoomServerModule)
    be_add_headless_executable(BagiMahjongServer
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tools/server/main.cpp
        LINK RoomServerModule RoomsModule RemoteControlModule MahjongModule
    )
    set_target_properties(BagiMahjongServer PROPERTIES FOLDER "Tools")
endif()

if(ENGINE_BUILD_TESTS)
    file(GLOB MAHJONG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: MahjongGameTest starts the game through GameManager, which finds MahjongGame by its BE_CLASS registration only
    be_add_game_tests(MahjongTests
        SOURCES ${MAHJONG_TEST_SOURCES}
        LINK RoomsModule
        WHOLE_ARCHIVE MahjongModule
    )

    # Starts BagiMahjong with the control API and plays a hand and a match over WebSocket (ctest entry BagiMahjongControlSmoke);
    # without the control server (ENGINE_WITH_REMOTE_CONTROL=OFF) the smoke could never connect
    if(ENGINE_WITH_REMOTE_CONTROL)
        be_add_game_control_tests(BagiMahjong SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)
    endif()

    # Starts BagiMahjongServer on a free port and plays hands from Python over real sockets (ctest entry BagiMahjongRoomSmoke), the model is
    # BagiRoomServerSmoke of the engine. Does not build the executable (a missing one is a skip, exit code 77)
    if(TARGET BagiMahjongServer AND Python3_FOUND)
        add_test(NAME BagiMahjongRoomSmoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rooms_smoke.py --exe $<TARGET_FILE:BagiMahjongServer>)
        set_tests_properties(BagiMahjongRoomSmoke PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 120
            RESOURCE_LOCK bagi_room_server_smoke
            ENVIRONMENT "BAGI_ENGINE_DIR=${BAGI_ENGINE_DIR}"
        )
    endif()

    # tools/collect_logs.py with a fake verifier and temp folders (no build needed); Python 3 standard library only
    if(Python3_FOUND)
        add_test(NAME MahjongCollectLogs COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/collect_logs_test.py)
        set_tests_properties(MahjongCollectLogs PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    else()
        message(WARNING "Python3 not found, the test MahjongCollectLogs is not registered")
    endif()
endif()
