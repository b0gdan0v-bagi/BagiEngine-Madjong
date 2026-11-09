#include "MatchLogWriter.h"

#include <Mahjong/MatchLogJson.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace BECore::Mahjong {

    namespace {

        constexpr const char* kDefaultDirectory = "logs/matches";
        constexpr const char* kFileExtension = ".jsonl";
        constexpr const char* kTimeFormat = "%Y-%m-%dT%H-%M-%S";

        std::filesystem::path& Directory() {
            static std::filesystem::path directory = kDefaultDirectory;
            return directory;
        }

        /// The local time as `2026-11-09T14-03-55` (no colons: the name is a file name on every platform).
        std::string TimeStamp() {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            std::ostringstream text;
            text << std::put_time(&local, kTimeFormat);
            return text.str();
        }

        /// `<directory>/<stamp>_seed<seed>.jsonl`, or with `-2`, `-3` ... before the extension while that file exists.
        std::filesystem::path FreeFileName(const std::filesystem::path& directory, uint32_t seed) {
            const std::string stem = TimeStamp() + "_seed" + std::to_string(seed);
            std::filesystem::path path = directory / (stem + kFileExtension);
            std::error_code error;
            for (int copy = 2; std::filesystem::exists(path, error) && copy < 100000; ++copy) {
                path = directory / (stem + "-" + std::to_string(copy) + kFileExtension);
            }
            return path;
        }

    }  // namespace

    const std::filesystem::path& MatchLogDirectory() {
        return Directory();
    }

    void SetMatchLogDirectory(std::filesystem::path directory) {
        Directory() = std::move(directory);
    }

    MatchLogWriter::~MatchLogWriter() {
        Close();
    }

    void MatchLogWriter::OnEvent(const MatchLogEvent& event) {
        if (event.kind == MatchLogKind::Start) {
            Close();
            Open(event);
        }
        if (_file.is_open()) {
            Write(event);
        }
        if (event.kind == MatchLogKind::End) {
            Close();
        }
    }

    void MatchLogWriter::Open(const MatchLogEvent& start) {
        _path.clear();
        const std::filesystem::path& directory = MatchLogDirectory();
        if (directory.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            LOG_WARNING(Format("MatchLogWriter: the folder '{}' could not be made ({}), this match is not logged", directory.string(), error.message()));
            return;
        }
        const std::filesystem::path path = FreeFileName(directory, start.seed);
        _file.open(path, std::ios::out | std::ios::trunc);
        if (!_file.is_open()) {
            _file.clear();
            LOG_WARNING(Format("MatchLogWriter: the file '{}' could not be opened, this match is not logged", path.string()));
            return;
        }
        _path = path;
        LOG_INFO(Format("MatchLogWriter: the match is logged to {}", path.string()));
    }

    void MatchLogWriter::Close() {
        if (_file.is_open()) {
            _file.close();
        }
        _file.clear();
    }

    void MatchLogWriter::Write(const MatchLogEvent& event) {
        const std::string line = EventToJson(event).dump();
        _file << line << '\n';
        _file.flush();
        if (!_file) {
            LOG_WARNING(Format("MatchLogWriter: writing to '{}' failed, the rest of this match is not logged", _path.string()));
            Close();
        }
    }

}  // namespace BECore::Mahjong
