#pragma once

namespace BECore::Mahjong {

    /// Hands in a full match and in a short one (a match is played over `handsPerMatch` hands).
    inline constexpr int kFullMatchHands = 16;
    inline constexpr int kShortMatchHands = 8;
    /// The default limit of a hand in points.
    inline constexpr int kDefaultLimit = 500;
    /// The points each seat starts a match with.
    inline constexpr int kStartingScore = 2000;

    /**
     * @brief The options of a game. The rules of one hand use `flowers` (and the scoring of a hand `limit`); the match (`Match.h`) uses
     *        `handsPerMatch`, `replayDrawnHand` and `startingScore`.
     */
    struct MahjongSettings {
        /// 16 (kFullMatchHands) or 8 (kShortMatchHands).
        int handsPerMatch = kFullMatchHands;
        /// Play with the eight flower and season tiles (144 tiles instead of 136).
        bool flowers = false;
        /// A drawn hand is replayed by the same East instead of counting towards handsPerMatch.
        bool replayDrawnHand = true;
        /// The most a hand can pay, in points (scoring uses it rounded down to a multiple of 10, `LimitCap`).
        int limit = kDefaultLimit;
        /// The points of each seat when the match starts.
        int startingScore = kStartingScore;
    };

}  // namespace BECore::Mahjong
