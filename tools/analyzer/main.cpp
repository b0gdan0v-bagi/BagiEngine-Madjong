// BagiMahjongAnalyzer: the expected points of every legal action of one position (docs/ev-analyzer.md). All the work is in
// src/Mahjong/Analyzer/AnalyzerCli; this file only hands over the arguments, reads the files and writes the streams.
#include <Mahjong/Analyzer/AnalyzerCli.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {

    std::expected<eastl::string, eastl::string> ReadFile(std::string_view path) {
        std::istream* stream = &std::cin;
        std::ifstream file;
        if (path != BECore::Mahjong::Analyzer::kStandardInputName) {
            file.open(std::string(path), std::ios::binary);
            if (!file) {
                return std::unexpected(eastl::string("cannot read the file \"") + eastl::string(path.data(), path.size()) + "\"");
            }
            stream = &file;
        }
        const std::string text((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        return eastl::string(text.data(), text.size());
    }

}  // namespace

int main(int argc, char** argv) {
    eastl::vector<std::string_view> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(argv[index]);
    }
    BECore::Mahjong::Analyzer::AnalyzerCliOutput output;
    const int code = BECore::Mahjong::Analyzer::RunAnalyzerCli(args, ReadFile, output);
    std::fwrite(output.out.data(), 1, output.out.size(), stdout);
    std::fwrite(output.err.data(), 1, output.err.size(), stderr);
    return code;
}
