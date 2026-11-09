// BagiMahjongArena: plays the computer players with no window and prints a table with intervals (docs/bot-arena.md). All the work is in
// src/Mahjong/Arena/ArenaCli; this file only hands over the arguments and the streams.
#include <Mahjong/Arena/ArenaCli.h>
#include <cstdio>

int main(int argc, char** argv) {
    eastl::vector<std::string_view> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(argv[index]);
    }
    BECore::Mahjong::Arena::ArenaCliOutput output;
    const int code = BECore::Mahjong::Arena::RunArenaCli(args, output);
    std::fwrite(output.out.data(), 1, output.out.size(), stdout);
    std::fwrite(output.err.data(), 1, output.err.size(), stderr);
    return code;
}
