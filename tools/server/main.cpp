// BagiMahjongServer: the room server of Mahjong, a headless program with no window and no Application. It registers MahjongSeatedGame as
// `mahjong` and hands the rest to the engine's RunRoomServer (RoomServer/CLAUDE.md, "A server main"); flags and environment are the engine's.
#include <Games/SeatedGameRegistry.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <RoomServer/RoomServerMain.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    using namespace BECore;

    // --test-games is accepted and ignored: the engine's smoke harness passes it, and this server has no demo game to switch on
    const auto args = LoadRoomServerArgs(argc, argv);
    if (!args) {
        std::fprintf(stderr, "BagiMahjongServer: %s\n", args.error().c_str());
        return 1;
    }
    if (args->help) {
        std::printf("%s", GetRoomServerUsage().c_str());
        return 0;
    }

    SeatedGameRegistry registry;
    registry.Register(Mahjong::MahjongSeatedGame::Info(), [] { return eastl::make_unique<Mahjong::MahjongSeatedGame>(); });

    return RunRoomServer(registry, *args);
}
