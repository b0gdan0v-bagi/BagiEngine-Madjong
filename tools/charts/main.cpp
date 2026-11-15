// BagiMahjongCharts: the tables of when a claim gains points over passing (docs/charts-claims.md). All the work is in
// src/Mahjong/Arena/ArenaChartsCli; this file only hands over the arguments and writes the streams.
#include <Mahjong/Arena/ArenaChartsCli.h>
#include <cstdio>

int main(int argc, char** argv) {
    BECore::Vector<BECore::StringView> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(argv[index]);
    }
    BECore::Mahjong::Arena::ArenaCliOutput output;
    const int code = BECore::Mahjong::Arena::RunChartsCli(args, output);
    std::fwrite(output.out.data(), 1, output.out.size(), stdout);
    std::fwrite(output.err.data(), 1, output.err.size(), stderr);
    return code;
}
