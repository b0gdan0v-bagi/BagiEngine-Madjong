#pragma once

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /// The default match-log folder: empty (logging off) in the browser build, else `logs/matches`. Pure, so a test can drive both builds.
    [[nodiscard]] std::filesystem::path DefaultMatchLogDirectory(bool browser);

    /// The folder the match logs are written to, relative to the working directory unless absolute (`DefaultMatchLogDirectory`: `logs/matches`,
    /// beside `engine.log`; none in the browser). An empty path switches logging off. Process-wide: `MatchLogWriter` reads it when a match starts.
    [[nodiscard]] const std::filesystem::path& MatchLogDirectory();
    void SetMatchLogDirectory(std::filesystem::path directory);

    /**
     * @brief The sink that writes a match down: one JSON line per event (`MatchLogJson.h`) into a file of its own.
     *
     * The file is opened at the `Start` event as `<MatchLogDirectory()>/YYYY-MM-DDTHH-MM-SS_seed<seed>.jsonl` (local time; if the name
     * exists, `-2`, `-3` ... goes before the extension) and one info line of the engine log names it. Every line is flushed at once, so a
     * crash or a match given up keeps the log up to its last event. The file is closed at `End`, at the next `Start` and by the
     * destructor.
     *
     * The writer never fails the game: a folder or a file that cannot be made, or a write that fails, is one warning, the file is closed
     * and the match goes on unlogged. Nothing throws.
     */
    class MatchLogWriter final : public IMatchLogSink {
    public:
        MatchLogWriter() = default;
        ~MatchLogWriter() override;
        MatchLogWriter(const MatchLogWriter&) = delete;
        MatchLogWriter& operator=(const MatchLogWriter&) = delete;

        void OnEvent(const MatchLogEvent& event) override;

        /// The file of the match that is being written, or of the last one (empty when none was opened or logging is off).
        [[nodiscard]] const std::filesystem::path& Path() const noexcept {
            return _path;
        }
        /// Whether the file is open, i.e. the match in progress is being written.
        [[nodiscard]] bool IsOpen() const noexcept {
            return _file.is_open();
        }

    private:
        void Open(const MatchLogEvent& start);
        void Close();
        void Write(const MatchLogEvent& event);

        std::ofstream _file;
        std::filesystem::path _path;
    };

}  // namespace BECore::Mahjong
