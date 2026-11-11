// BagiMahjongArena: plays the computer players with no window and prints a table with intervals (docs/bot-arena.md), or reads a folder of
// match logs (--logs). All the work is in src/Mahjong/Arena/ArenaCli; this file only hands over the arguments, reads the log files and
// writes the streams.
#include <Mahjong/Arena/ArenaCli.h>
#include <cstdio>
#include <iterator>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    constexpr std::string_view kLogExtension = ".jsonl";

    eastl::string Quoted(const fs::path& path) {
        const std::string text = path.generic_string();
        return BECore::Format("\"{}\"", text);
    }

    std::expected<BECore::Mahjong::Arena::LogFile, eastl::string> ReadLogFile(const fs::path& file, const std::string& name) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            return std::unexpected(BECore::Format("cannot read the file {}", Quoted(file)));
        }
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return BECore::Mahjong::Arena::LogFile{eastl::string(name.data(), name.size()), eastl::string(text.data(), text.size())};
    }

    /// The `*.jsonl` files under `path` (names relative to it, `/` separated; the caller sorts), or the one file `path` names.
    std::expected<eastl::vector<BECore::Mahjong::Arena::LogFile>, eastl::string> ReadLogFiles(std::string_view path) {
        const fs::path root{std::string(path)};
        std::error_code error;
        eastl::vector<BECore::Mahjong::Arena::LogFile> files;
        if (fs::is_regular_file(root, error)) {
            std::expected<BECore::Mahjong::Arena::LogFile, eastl::string> file = ReadLogFile(root, root.filename().generic_string());
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
            if (!iterator->is_regular_file(error) || iterator->path().extension() != kLogExtension) {
                continue;
            }
            const fs::path relative = fs::relative(iterator->path(), root, error);
            if (error) {
                break;
            }
            std::expected<BECore::Mahjong::Arena::LogFile, eastl::string> file = ReadLogFile(iterator->path(), relative.generic_string());
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
    eastl::vector<std::string_view> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(argv[index]);
    }
    BECore::Mahjong::Arena::ArenaCliOutput output;
    const int code = BECore::Mahjong::Arena::RunArenaCli(args, ReadLogFiles, output);
    std::fwrite(output.out.data(), 1, output.out.size(), stdout);
    std::fwrite(output.err.data(), 1, output.err.size(), stderr);
    return code;
}
