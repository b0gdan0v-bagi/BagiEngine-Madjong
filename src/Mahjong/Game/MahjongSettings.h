#pragma once

namespace BECore::Mahjong {

    /// Hands in a full match and in a short one (a match is played over `handsPerMatch` hands).
    inline constexpr int kFullMatchHands = 16;
    inline constexpr int kShortMatchHands = 8;
    /// The default limit of a hand in points.
    inline constexpr int kDefaultLimit = 500;
    /// The points each seat starts a match with.
    inline constexpr int kStartingScore = 2000;

    /// The bounds of a playable match (`IsPlayable`, `Match::Start`): what a seat's points can reach is then below the largest `int`
    /// whatever the settings are (the proof is a `static_assert` in `Match.cpp`), so the arithmetic of a settlement never saturates.
    /// No reader of a save, a log or a request needs a copy of them: all reach `Match::Start`.
    inline constexpr int kMaxHandsPerMatch = kFullMatchHands;
    inline constexpr int kMaxLimit = 1'000'000;
    inline constexpr int kMaxScore = 100'000'000;

    /**
     * @brief The options of a game. The rules of one hand use `flowers` (and the scoring of a hand `limit`); the match (`Match.h`) uses
     *        `handsPerMatch`, `replayDrawnHand` and `startingScore`.
     */
    struct MahjongSettings {
        /// A positive multiple of 4 up to `kMaxHandsPerMatch` (the game offers `kFullMatchHands` or `kShortMatchHands`).
        int handsPerMatch = kFullMatchHands;
        /// Play with the flower and season tiles.
        bool flowers = false;
        /// A drawn hand is replayed by the same East instead of counting towards handsPerMatch.
        bool replayDrawnHand = true;
        /// The most a hand can pay, in points (scoring uses it rounded down to a multiple of 10, `LimitCap`); `0..kMaxLimit`.
        int limit = kDefaultLimit;
        /// The points of each seat when the match starts; within `+-kMaxScore`.
        int startingScore = kStartingScore;
    };

}  // namespace BECore::Mahjong
