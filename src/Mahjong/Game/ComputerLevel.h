#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// How well a computer seat plays: a whole number from `kMinLevel` (1, the weakest) to `kMaxLevel` (10), chosen by the owner
    /// (2026-11-10) so the levels can grow without a new name each. The two named points are the players that exist: `Easy` (1) is the
    /// player of `kEasyProfile`, `Normal` (5, the default) the player described in `CLAUDE.md` ("Computer player"). Levels 2..4 lie between
    /// them (`ProfileOf`); levels 6..10 play exactly like Normal, because no player stronger than Normal exists inside the time rule (no
    /// search profile beat it on two ranges, `docs/bot-search.md`, "Levels", "Above Normal"): they are placeholders, not a stronger
    /// player. A number outside 1..10 is a defect; `LevelOf` is the checked way in.
    inline constexpr uint8_t kEasyLevelNumber = 1;
    inline constexpr uint8_t kNormalLevelNumber = 5;

    struct ComputerLevel {
        /// 1..10; a level made with no number is Normal
        uint8_t number = kNormalLevelNumber;

        static const ComputerLevel Easy;
        static const ComputerLevel Normal;

        constexpr bool operator==(const ComputerLevel&) const = default;
    };

    inline constexpr ComputerLevel ComputerLevel::Easy{kEasyLevelNumber};
    inline constexpr ComputerLevel ComputerLevel::Normal{kNormalLevelNumber};

    inline constexpr int kMinLevel = 1;
    inline constexpr int kMaxLevel = 10;

    /// The number of levels.
    inline constexpr int kLevelCount = kMaxLevel - kMinLevel + 1;

    /// The level of a table nobody chose a level for.
    inline constexpr ComputerLevel kDefaultLevel = ComputerLevel::Normal;

    /// The number of a level, 1..10.
    [[nodiscard]] constexpr int LevelNumber(ComputerLevel level) noexcept {
        return level.number;
    }

    /// The level of `number`, or nothing outside 1..10.
    [[nodiscard]] constexpr std::optional<ComputerLevel> LevelOf(int64_t number) noexcept {
        if (number < kMinLevel || number > kMaxLevel) {
            return std::nullopt;
        }
        return ComputerLevel{static_cast<uint8_t>(number)};
    }

    /// Every level, weakest first.
    inline constexpr ComputerLevel kAllLevels[kLevelCount] = {{1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}};

    /// The text of a level: its number, `1` .. `10` (the value of the `Options` dropdown and of the saved `Setup` choices). A value
    /// outside 1..10 asserts in debug and answers the text of the default.
    [[nodiscard]] constexpr std::string_view LevelText(ComputerLevel level) noexcept {
        constexpr std::string_view kTexts[kLevelCount] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
        const int number = LevelNumber(level);
        if (number < kMinLevel || number > kMaxLevel) {
            ASSERT(false, "a value outside ComputerLevel");
            return kTexts[LevelNumber(kDefaultLevel) - kMinLevel];
        }
        return kTexts[number - kMinLevel];
    }

    /// The level whose `LevelText` is `text`: exactly `1` .. `10` (no sign, no leading zero, no spaces), nothing else.
    [[nodiscard]] constexpr std::optional<ComputerLevel> TryParseLevel(std::string_view text) noexcept {
        for (const ComputerLevel level : kAllLevels) {
            if (text == LevelText(level)) {
                return level;
            }
        }
        return std::nullopt;
    }

    /// The level of a name of the two named levels before the scale of numbers: `easy` (1) and `normal` (5), lowercase. Read where such
    /// names were written (the saved `Setup` choices, a version 2 match log) and accepted by the arena; never written.
    [[nodiscard]] constexpr std::optional<ComputerLevel> TryParseLevelName(std::string_view text) noexcept {
        if (text == "easy") {
            return ComputerLevel::Easy;
        }
        if (text == "normal") {
            return ComputerLevel::Normal;
        }
        return std::nullopt;
    }

    /// The level of each seat of a table, indexed by seat. All four entries are kept, also those of seats a person plays: a seat that
    /// becomes a computer again plays at the level it had. Only the entries of computer seats play.
    using SeatLevels = eastl::array<ComputerLevel, kSeatCount>;

    /// Every seat at `level`.
    [[nodiscard]] constexpr SeatLevels UniformLevels(ComputerLevel level) noexcept {
        return {level, level, level, level};
    }

    /// The base of a percentage: a roll is one `MahjongRng::Below(kPercentBase)` against the percent.
    inline constexpr int kPercentBase = 100;

    /// The search knobs of `LevelProfile` (the search player of `ComputerSearch.h`; **no level turns it on**, `docs/bot-search.md`,
    /// "Levels"): the candidates a search decision looks at when nothing else is said (neutral while `searchWorlds` is 0), and the
    /// largest value of each knob (what the arena accepts from the command line).
    inline constexpr int kSearchDefaultCandidates = 4;
    inline constexpr int kSearchMaxWorlds = 4096;
    inline constexpr int kSearchMaxCandidates = 12;
    inline constexpr int kSearchMaxMargin = 100;

    /// The budget of the search player that fitted the time rule (**used by no level**: the search is a weakening of Normal at best and
    /// beats it at no budget inside the time rule, so it is neither a stronger level nor a better ladder for levels 2..4 than the skip
    /// knobs, `docs/bot-search.md`, "Levels"; the arena sets the same numbers from the command line, `normal:searchWorlds=2`). Found by
    /// time, as `docs/bot-search.md` records: the rule is that the slowest decision (the first discard of 14 tiles with the full candidate
    /// count) takes at most 0.3 s in a **Debug** build, so that the answer is there within the 1.0 s pause (`kComputerStepSeconds`; the table thinks in the background, so a slower one only waits). A
    /// decision is `kSearchWorlds` x `kSearchCandidates` whole hands played out by four Normal players, 8 of them here; on 16 seeds (727 decisions, `--jobs 1`, the smaller of two runs of each
    /// decision) the worst decision took 248 ms, while 3 x 3, 4 x 2 and 5 x 2 took 314 to 335 ms and 2 x 5 over a second. The margin is not set by time: on the exploration range (seeds 4000 ..) every
    /// margin from 20 to 100 tenths lost 2 to 3 points a hand to Normal (within each other's noise), 10 lost 7 and 0 lost 35, so a margin of 30 is used; with two worlds the choice is mostly noise,
    /// and the 4x and 16x rows of `docs/bot-search.md` say whether more worlds would change that (they did not: about level with Normal at 4x and 16x, within wide intervals).
    inline constexpr int kSearchWorlds = 2;
    inline constexpr int kSearchCandidates = 4;
    inline constexpr int kSearchMargin = 30;

    /**
     * @brief The numbers a level plays by: what the choosers of `ComputerTurn.h` and `ComputerClaim.h` do differently from the Normal
     * player. Every knob of `kNormalProfile` is off, and **a knob that is off draws nothing from the rng**, so Normal's stream is the
     * stream of the player before levels existed. A percentage is 0..100; each roll is one `MahjongRng::Below(kPercentBase)` of the player's own
     * seeded rng, so one seed and one setup is one match.
     */
    struct LevelProfile {
        /// Step 5 of the own turn only: the share of discards chosen among those that leave the lowest shanten by `KeepValue` and the
        /// rng, ignoring the acceptance (the tile that gives the most improving copies). The hand never gets further from a win by it.
        int skipAcceptancePercent = 0;
        /// Step 2 of the own turn: the share of turns on which a discard that could declare the calling hand does not declare (the
        /// hand plays on by the later steps and is asked again next turn). `DeclareWin` (step 1) is never skipped.
        int skipDeclarationPercent = 0;
        /// The share of acceptable pung, kong and chow claims that are passed. A mahjong claim is never passed.
        int skipClaimPercent = 0;
        /// Whether a pung or kong of a value tile (a dragon, the own wind, the round wind) is taken at an equal shanten, for the double.
        bool takeValueClaimsAtEqualShanten = true;
        /// The search (`ComputerSearch.h`): the number of sampled worlds each candidate is played out in, 0 = no search at all (no number
        /// drawn, the choosers answer as before). It is off in every profile of `kLevelProfiles`; only the arena and the tests turn it on.
        int searchWorlds = 0;
        /// At most this many candidates are looked at in a search decision, Normal's own choice among them (below 2 there is nothing to
        /// compare). Unused while `searchWorlds` is 0.
        int searchCandidates = kSearchDefaultCandidates;
        /// Normal's choice is replaced only when the best candidate's mean advantage in points is above this many tenths of the standard
        /// error of the paired difference (0: the best mean wins, if it is above 0). Unused while `searchWorlds` is 0.
        int searchMargin = 0;
        /// Step 5 of the own turn: the number of the seat's own next draws the discard is rated over (`ValueDiscards`, `OwnDraws.h`, at most
        /// `kOwnDrawsMaxDepth`), in expected points; 0 = Normal's step 5, nothing of the new code runs. A decision with no rated discard
        /// falls back to Normal's. Draws nothing from the rng (ties go to Normal's order). Off in every level.
        int ownDrawsDepth = 0;
        /// Step 2 of the own turn: the discard that declares the calling hand is the one with the best expected value (`ValueWaits`), not
        /// the one with the most unseen winning copies; false = Normal's step 2. Draws nothing from the rng when on. Off in every level.
        bool ownDrawsPickWait = false;
        /// Steps 2, 4 and 5 of the own turn: among discards equal in what each step ranks by (unseen winning copies; shanten and
        /// acceptance), the one with the lowest `Danger` against the declared opponents (`DiscardDanger.h`) is chosen, then the lowest
        /// `KeepValue`, then the rng. A tie-break only: it never changes the shanten or the acceptance of the choice. Draws nothing from
        /// the rng, and with no declared opponent the play is Normal's exactly. Off in every level: paired with Normal against three
        /// Normal seats (1000 seeds, `docs/bot-arena.md`) it gives the win on 1.7 percent fewer hands (`significant`) but wins and points
        /// a hand stay level (seeds 4000..: -1.09 +- 1.30 points, +0.05 +- 0.39 wins %; seeds 12000..: +0.03 +- 1.52, +0.33 +- 0.36), the
        /// pooled points are -0.53, so the rule of the entry (pooled points not below 0) kept it off.
        bool readDanger = false;
    };

    /// The Normal player: every knob off.
    inline constexpr LevelProfile kNormalProfile{};

    /// What Easy (level 1) plays by (measured in `ComputerStrengthTest`, whose comment has the numbers): about half of Normal's wins,
    /// still several times a random seat's.
    inline constexpr LevelProfile kEasyProfile{
        .skipAcceptancePercent = 60,
        .skipDeclarationPercent = 85,
        .skipClaimPercent = 35,
        .takeValueClaimsAtEqualShanten = false,
    };

    /// Levels 2..4 take Easy's percentages by 3/4, 1/2 and 1/4 (rounded) and pass value claims up to level 2. Against three Normal seats
    /// the arena orders 1 < 2 < 3 < 4 < 5, each step significant on 1000 seeds of two ranges (`docs/bot-search.md`, "Levels"). The
    /// search dials (worlds, margin) were tried as a ladder for these levels and kept out: the margin moves the strength from -35 to
    /// -7 to -3 points in three steps, a steeper and less even ladder than this one, and nothing lies at level 3.
    inline constexpr LevelProfile kLevel2Profile{
        .skipAcceptancePercent = 45,
        .skipDeclarationPercent = 64,
        .skipClaimPercent = 26,
        .takeValueClaimsAtEqualShanten = false,
    };
    inline constexpr LevelProfile kLevel3Profile{.skipAcceptancePercent = 30, .skipDeclarationPercent = 43, .skipClaimPercent = 18};
    inline constexpr LevelProfile kLevel4Profile{.skipAcceptancePercent = 15, .skipDeclarationPercent = 21, .skipClaimPercent = 9};

    /// The profile of each level 1..10, the order of strength; 5..10 are Normal's (6..10 are placeholders: nothing stronger exists).
    inline constexpr const LevelProfile* kLevelProfiles[kLevelCount] = {
        &kEasyProfile, &kLevel2Profile, &kLevel3Profile, &kLevel4Profile, &kNormalProfile, &kNormalProfile, &kNormalProfile, &kNormalProfile, &kNormalProfile, &kNormalProfile,
    };

    /// The profile of `level`. A value outside 1..10 gives Normal's.
    [[nodiscard]] constexpr const LevelProfile& ProfileOf(ComputerLevel level) noexcept {
        const int number = LevelNumber(level);
        if (number < kMinLevel || number > kMaxLevel) {
            return kNormalProfile;
        }
        return *kLevelProfiles[number - kMinLevel];
    }

    /// The levels for an error text: "a whole number 1..10".
    [[nodiscard]] inline eastl::string LevelRangeText() {
        return Format("a whole number {}..{}", kMinLevel, kMaxLevel);
    }

}  // namespace BECore::Mahjong
