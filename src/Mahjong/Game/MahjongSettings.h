#pragma once

namespace BECore::Mahjong {

    /// Hands in a full match and in a short one (a match is played over `handsPerMatch` hands).
    inline constexpr int kFullMatchHands = 16;
    inline constexpr int kShortMatchHands = 8;
    /// The default limit of a hand in points.
    inline constexpr int kDefaultLimit = 500;

    /**
     * @brief The options of a game. Only `flowers` is used by the rules of one hand; the rest is carried for the match (a later slice).
     */
    struct MahjongSettings {
        /// 16 (kFullMatchHands) or 8 (kShortMatchHands).
        int handsPerMatch = kFullMatchHands;
        /// Play with the eight flower and season tiles (144 tiles instead of 136).
        bool flowers = false;
        /// A drawn hand is replayed instead of counting towards handsPerMatch (the match is a later slice).
        bool replayDrawnHand = true;
        /// The most a hand can pay, in points.
        int limit = kDefaultLimit;
    };

}  // namespace BECore::Mahjong
