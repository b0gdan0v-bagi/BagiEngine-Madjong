// BagiMahjongArena: plays the computer players with no window and prints a table with intervals (docs/bot-arena.md), or reads a folder of
// match logs (--logs). All the work is in src/Mahjong/Arena/ArenaCli; this file only hands over the arguments, reads the log files and
// writes the streams.
#include <Mahjong/Arena/ArenaCli.h>
#include <cstdio>
#include <iterator>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    constexpr BECore::StringView kLogExtension = ".jsonl";

    BECore::String Quoted(const fs::path& path) {
        const auto text = path.generic_string();
        return BECore::Format("\"{}\"", text);
    }

    std::expected<BECore::Mahjong::Arena::LogFile, BECore::String> ReadLogFile(const fs::path& file, BECore::StringView name) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            return std::unexpected(BECore::Format("cannot read the file {}", Quoted(file)));
        }
        //-NoStd: the bytes of the stream are read through its iterator into a std::string (EASTL would measure an input range by consuming it)
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return BECore::Mahjong::Arena::LogFile{BECore::String(name), BECore::FromStdString(text)};
    }

    /// The `*.jsonl` files under `path` (names relative to it, `/` separated; the caller sorts), or the one file `path` names.
    std::expected<eastl::vector<BECore::Mahjong::Arena::LogFile>, BECore::String> ReadLogFiles(BECore::StringView path) {
        const fs::path root{BECore::ToStdString(path)};
        std::error_code error;
        eastl::vector<BECore::Mahjong::Arena::LogFile> files;
        if (fs::is_regular_file(root, error)) {
            std::expected<BECore::Mahjong::Arena::LogFile, BECore::String> file = ReadLogFile(root, BECore::FromStd(root.filename().generic_string()));
            if (!file.has_value()) {
                return std::unexpected(file.error());
            }
            files.push_back(std::move(*file));
            return files;
        }
        if (!fs::is_directory(root, error)) {
            return std::unexpected(BECore::Format("cannot read {}: it is no file or folder", Quoted(root)));
        }
        fs::recursive_directory_iterator iterator(root, error);
        for (const fs::recursive_directory_iterator end; !error && iterator != end; iterator.increment(error)) {
            if (!iterator->is_regular_file(error) || iterator->path().extension() != BECore::ToStd(kLogExtension)) {
                continue;
            }
            const fs::path relative = fs::relative(iterator->path(), root, error);
            if (error) {
                break;
            }
            std::expected<BECore::Mahjong::Arena::LogFile, BECore::String> file = ReadLogFile(iterator->path(), BECore::FromStd(relative.generic_string()));
            if (!file.has_value()) {
                return std::unexpected(file.error());
            }
            files.push_back(std::move(*file));
        }
        if (error) {
            return std::unexpected(BECore::Format("cannot read {}: {}", Quoted(root), error.message()));
        }
        return files;
    }

}  // namespace

int main(int argc, char** argv) {
    eastl::vector<BECore::StringView> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(argv[index]);
    }
    BECore::Mahjong::Arena::ArenaCliOutput output;
    const int code = BECore::Mahjong::Arena::RunArenaCli(args, ReadLogFiles, output);
    std::fwrite(output.out.data(), 1, output.out.size(), stdout);
    std::fwrite(output.err.data(), 1, output.err.size(), stderr);
    return code;
}
