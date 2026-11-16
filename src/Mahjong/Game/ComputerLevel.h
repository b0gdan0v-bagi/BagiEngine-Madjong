#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// How well a computer seat plays: a whole number from `kMinLevel` (1, the weakest) to `kMaxLevel` (10). Two real players exist,
    /// `Easy` (1, `kEasyProfile`) and `Normal` (5, the default); 2..4 lie between them, and 6..10 are Normal placeholders because no
    /// stronger player fits the time rule (`Game/CLAUDE.md`, "Computer player"). A number outside 1..10 is a defect; `LevelOf` is the
    /// checked way in.
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
    [[nodiscard]] constexpr Optional<ComputerLevel> LevelOf(int64_t number) noexcept {
        if (number < kMinLevel || number > kMaxLevel) {
            return std::nullopt;
        }
        return ComputerLevel{static_cast<uint8_t>(number)};
    }

    /// Every level, weakest first.
    inline constexpr ComputerLevel kAllLevels[kLevelCount] = {{1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}};

    /// The text of a level: its number, `1` .. `10` (the value of the `Options` dropdown and of the saved `Setup` choices). A value
    /// outside 1..10 asserts in debug and answers the text of the default.
    [[nodiscard]] constexpr StringView LevelText(ComputerLevel level) noexcept {
        constexpr StringView kTexts[kLevelCount] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
        const int number = LevelNumber(level);
        if (number < kMinLevel || number > kMaxLevel) {
            ASSERT(false, "a value outside ComputerLevel");
            return kTexts[LevelNumber(kDefaultLevel) - kMinLevel];
        }
        return kTexts[number - kMinLevel];
    }

    /// The level whose `LevelText` is `text`: exactly `1` .. `10` (no sign, no leading zero, no spaces), nothing else.
    [[nodiscard]] constexpr Optional<ComputerLevel> TryParseLevel(StringView text) noexcept {
        for (const ComputerLevel level : kAllLevels) {
            if (text == LevelText(level)) {
                return level;
            }
        }
        return std::nullopt;
    }

    /// The level of the old name `easy` (1) or `normal` (5), lowercase. Read where such names were written (the saved `Setup` choices,
    /// a version 2 match log) and accepted by the arena; never written.
    [[nodiscard]] constexpr Optional<ComputerLevel> TryParseLevelName(StringView text) noexcept {
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
    using SeatLevels = Array<ComputerLevel, kSeatCount>;

    /// Every seat at `level`.
    [[nodiscard]] constexpr SeatLevels UniformLevels(ComputerLevel level) noexcept {
        return {level, level, level, level};
    }

    /// The base of a percentage: a roll is one `MahjongRng::Below(kPercentBase)` against the percent.
    inline constexpr int kPercentBase = 100;

    /// The search knobs of `LevelProfile` (`ComputerSearch.h`; **no level turns the search on**): the default candidate count (neutral
    /// while `searchWorlds` is 0) and the largest value of each knob, which the arena accepts from the command line.
    inline constexpr int kSearchDefaultCandidates = 4;
    inline constexpr int kSearchMaxWorlds = 4096;
    inline constexpr int kSearchMaxCandidates = 12;
    inline constexpr int kSearchMaxMargin = 100;

    /// The budget of the search player that fits the time rule: the slowest decision (the first discard of 14 tiles with every candidate)
    /// takes at most 0.3 s in a **Debug** build, so the answer is there within the pause `kComputerStepSeconds`. **No level uses it**
    /// (`docs/bot-search.md`, "Levels"); the arena sets the same numbers from the command line, `normal:searchWorlds=2`. The margin is
    /// not set by time but by the arena's rows of that document.
    inline constexpr int kSearchWorlds = 2;
    inline constexpr int kSearchCandidates = 4;
    inline constexpr int kSearchMargin = 30;

    /// The weighted player (`LevelProfile::weighted`, `ComputerWeights.h`): the largest value of a weight, which the arena accepts from
    /// the command line (the smallest is 0).
    inline constexpr int kWeightMax = 65535;

    /// The shanten of a hand of 13 tiles is at most this (no set, no pair, no part: four sets and a pair away), so `kShantenCeiling -
    /// shanten` is the feature "nearness to a win", never below 0 for a hand of 13 tiles.
    inline constexpr int kShantenCeiling = 8;

    /// The worst grade of `Danger` (`High`, `DiscardDanger.h` asserts it) and the span of `KeepValue` (`kKeepValueTile - kKeepWorthless`,
    /// `ComputerTurn.h` asserts it): the features `kDangerWorst - danger` and `kKeepValueTile - keep` run from 0 to these.
    inline constexpr int kDangerWorst = 3;
    inline constexpr int kKeepSpread = 3;

    /// The most unseen copies the acceptance of a hand can count: every kind, every copy.
    inline constexpr int kAcceptanceMax = kKindCount * kCopiesPerTile;

    /// The largest `ClaimRank` (a kong; `ComputerClaim.h` asserts it).
    inline constexpr int kClaimRankMax = 2;

    /// The tiles left in the wall at which the wall slope of a claim (`wClaimWall`) is one whole weight: `wClaimWall * liveCount / kWallWeightUnit`.
    inline constexpr int kWallWeightUnit = 100;

    /// The weights with which the weighted player is Normal exactly (`docs/bot-weights.md`, "Policy"). Normal ranks by shanten, then
    /// acceptance, then (with `readDanger`) danger, then keep value, so each weight exceeds the largest spread of everything below it:
    /// `kNormalWeightSafeStep` is the weight `wSafe` that stands for `readDanger`, and is itself above the span of keep.
    inline constexpr int kNormalWeightKeep = 1;
    inline constexpr int kNormalWeightSafe = 0;
    inline constexpr int kNormalWeightSafeStep = 4;
    inline constexpr int kNormalWeightAcceptance = 16;
    inline constexpr int kNormalWeightShanten = 4400;

    /// The claim weights with which a claim is acceptable exactly when Normal's rule says so: a gain of acceptance alone is never
    /// enough (the margin is the whole span of acceptance and of rank), a lowered shanten always is, a value claim at an equal shanten is.
    inline constexpr int kNormalWeightClaimRank = 1;
    inline constexpr int kNormalWeightClaimWall = 0;
    inline constexpr int kNormalWeightClaimMargin = kNormalWeightAcceptance * kAcceptanceMax + kNormalWeightClaimRank * kClaimRankMax;
    inline constexpr int kNormalWeightClaimValue = kNormalWeightShanten;

    /// The weights of the two value features (`wValueDiscard`, `wValueClaim`): 0, the feature is off and costs nothing, which is what
    /// keeps the Normal-equivalent weights Normal's. `kWeightedValueDepth` is the horizon (draws) of both when a weight is above 0.
    inline constexpr int kNormalWeightValueDiscard = 0;
    inline constexpr int kNormalWeightValueClaim = 0;
    inline constexpr int kWeightedValueDepth = 1;

    static_assert(kNormalWeightClaimRank * kClaimRankMax < kNormalWeightAcceptance, "acceptance outranks the claim rank");
    static_assert(kNormalWeightKeep * kKeepSpread < kNormalWeightSafeStep, "danger outranks keep value");
    static_assert(kKeepSpread * kNormalWeightKeep + kDangerWorst * kNormalWeightSafeStep < kNormalWeightAcceptance, "acceptance outranks danger and keep value");
    static_assert(kAcceptanceMax * kNormalWeightAcceptance < kNormalWeightShanten, "shanten outranks acceptance");
    static_assert(kNormalWeightShanten - kNormalWeightAcceptance * kAcceptanceMax > kNormalWeightClaimMargin, "a lowered shanten passes the claim gate whatever the acceptance does");
    static_assert(kNormalWeightClaimValue - kNormalWeightAcceptance * kAcceptanceMax > kNormalWeightClaimMargin, "a value claim at an equal shanten passes the gate");
    static_assert(kNormalWeightClaimMargin >= kNormalWeightAcceptance * kAcceptanceMax + kNormalWeightClaimRank * kClaimRankMax, "a gain of acceptance and rank alone never passes the gate");
    static_assert(kNormalWeightShanten <= kWeightMax && kNormalWeightClaimMargin <= kWeightMax, "the defaults are in the range of the knobs");

    /**
     * @brief The numbers a level plays by: what the choosers of `ComputerTurn.h` and `ComputerClaim.h` do differently from Normal.
     * `kNormalProfile` is the defaults, and **a knob that is off draws nothing from the rng**, so Normal's stream does not depend on
     * the knobs that exist. A percentage is 0..100; each roll is one `MahjongRng::Below(kPercentBase)` of the player's own seeded rng,
     * so one seed and one setup is one match.
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
        /// The search (`ComputerSearch.h`): the number of sampled worlds each candidate is played out in, 0 = no search at all (nothing
        /// drawn). Off in every profile of `kLevelProfiles`; only the arena and the tests turn it on.
        int searchWorlds = 0;
        /// At most this many candidates are looked at in a search decision, Normal's own choice among them (below 2 there is nothing to
        /// compare). Unused while `searchWorlds` is 0.
        int searchCandidates = kSearchDefaultCandidates;
        /// Normal's choice is replaced only when the best candidate's mean advantage in points is above this many tenths of the standard
        /// error of the paired difference (0: the best mean wins, if it is above 0). Unused while `searchWorlds` is 0.
        int searchMargin = 0;
        /// Step 5 of the own turn: the number of the seat's own next draws the discard is rated over (`ValueDiscards`, `OwnDraws.h`, at most
        /// `kOwnDrawsMaxDepth`), in expected points; 0 = Normal's step 5, nothing of the new code runs. A decision with no rated discard
        /// falls back to Normal's. Draws nothing from the rng (ties go to Normal's order). Off in every level: no variant beat Normal inside the time rule (`docs/own-draws-ev.md`).
        int ownDrawsDepth = 0;
        /// Step 2 of the own turn: the discard that declares the calling hand is the one with the best expected value (`ValueWaits`), not
        /// the one with the most unseen winning copies; false = Normal's step 2. Draws nothing from the rng when on. Off in every level.
        bool ownDrawsPickWait = false;
        /// The own-draws value (`ownDrawsDepth`, `ownDrawsPickWait`, `ownDrawsClaimDepth`, `ownDrawsDeclareDepth`) also counts the win on a discard of another
        /// seat: in each round the three others discard before the seat draws, and a discard of a winning kind wins
        /// (`OwnDrawsModel::discardWins`, `OwnDraws.h`). Alone it changes nothing. Draws nothing from the rng. Off in every level.
        bool ownDrawsDiscardWins = false;
        /// Step 3 of a claim window (after a mahjong claim and the pass of a waiting hand): the number of the seat's own next draws that
        /// each answer, the pass and every listed pung, kong and chow, is rated over (`ValueClaims`, `OwnDraws.h`, at most
        /// `kOwnDrawsMaxDepth`), in expected points; a claim is taken only when worth strictly more than the pass. 0 = Normal's steps 3
        /// and 4, nothing of the new code runs; a window with nothing rated falls back to them. Draws nothing from the rng, and
        /// `skipClaimPercent` and `takeValueClaimsAtEqualShanten` do not apply (the value decides). Off in every level.
        int ownDrawsClaimDepth = 0;
        /// Step 2 of the own turn: the number of draws the seat's choice between declaring now and playing on is rated over
        /// (`ValueWaits` against `ValueLater`, `OwnDraws.h`, at most `kOwnDrawsMaxDepth`); it declares when that is worth no less, else
        /// it plays the best discard that does not declare. 0 = Normal's step 2, nothing of the new code runs. Draws nothing from the
        /// rng (`skipDeclarationPercent` is rolled first, as always). Off in every level.
        int ownDrawsDeclareDepth = 0;
        /// Steps 2, 4 and 5 of the own turn: among discards equal in what each step ranks by (unseen winning copies; shanten and
        /// acceptance), the one with the lowest `Danger` against the declared opponents (`DiscardDanger.h`) is chosen, then the lowest
        /// `KeepValue`, then the rng. A tie-break only: it never changes the shanten or the acceptance of the choice. Draws nothing from
        /// the rng, and with no declared opponent the play is Normal's exactly. Off in every level: it gave no gain in points a hand in
        /// the arena's paired rows (`docs/bot-arena.md`).
        bool readDanger = false;
        /// Step 2 of a claim window: a waiting hand offered a kong that keeps its wait (a concealed pung of the discard) takes it or
        /// passes by the expected points of each (`RateWaitingKong`, `ComputerClaim.h`); false = it passes every claim but mahjong.
        /// Draws nothing from the rng. On in every level (the owner, 2026-11-12).
        bool waitingKongByPoints = true;
        /// The weighted player (`ComputerWeights.h`): step 5 of the own turn and steps 3 and 4 of a claim window choose by the weights
        /// below instead of `BestDiscard` and `BestClaims`. Off in every level; **with the default weights the play is Normal's, rng
        /// stream included**, and off the code path is the unweighted one. Steps 1 to 4 of the turn and 1 and 2 of a window ignore it.
        bool weighted = false;
        /// Weighted discard rating: per step of the shanten of the 13 tiles left below `kShantenCeiling`.
        int wShanten = kNormalWeightShanten;
        /// Weighted discard rating: per unseen copy that improves the 13 tiles left (`AcceptanceOf`).
        int wAcceptance = kNormalWeightAcceptance;
        /// Weighted discard rating: per step of `KeepValue` below `kKeepValueTile` (a lower keep value is discarded sooner).
        int wKeep = kNormalWeightKeep;
        /// Weighted discard rating: per grade of `Danger` below `kDangerWorst`; read only with a declared opponent. 0 = danger not read.
        int wSafe = kNormalWeightSafe;
        /// Weighted claim rating: per `ClaimRank` of the claim (kong above pung above chow), the order of claims that rate the same.
        int wClaimRank = kNormalWeightClaimRank;
        /// Weighted claim gate: the bonus of a pung or kong of a value tile at an equal shanten.
        int wClaimValue = kNormalWeightClaimValue;
        /// Weighted claim gate: the claim's rating must exceed the pass's by more than this.
        int wClaimMargin = kNormalWeightClaimMargin;
        /// Weighted claim gate: added to the margin per `kWallWeightUnit` tiles in the live wall (the fewer tiles are left, the less margin a claim needs).
        int wClaimWall = kNormalWeightClaimWall;
        /// Weighted discard rating: per milli-point by which the discard's own-draws value (`ValueDiscards`) is above the lowest rated one.
        /// Read (and the value computed) only when above 0 and `weightedValueDepth` is above 0.
        int wValueDiscard = kNormalWeightValueDiscard;
        /// Weighted claim rating: per milli-point by which the claim's own-draws value (`ValueClaims`) is above the pass's (signed).
        /// Read (and the value computed) only when above 0 and `weightedValueDepth` is above 0.
        int wValueClaim = kNormalWeightValueClaim;
        /// The draws both value terms look ahead (`OwnDrawsModel::depth`, at most `kOwnDrawsMaxDepth`); 0 = no value term. Read only when a
        /// value weight is above 0.
        int weightedValueDepth = kWeightedValueDepth;
    };

    /// The Normal player: the defaults of `LevelProfile`.
    inline constexpr LevelProfile kNormalProfile{};

    /// What Easy (level 1) plays by.
    inline constexpr LevelProfile kEasyProfile{
        .skipAcceptancePercent = 60,
        .skipDeclarationPercent = 85,
        .skipClaimPercent = 35,
        .takeValueClaimsAtEqualShanten = false,
    };

    /// Levels 2..4 take Easy's percentages by 3/4, 1/2 and 1/4 (rounded) and pass value claims up to level 2; the arena orders
    /// 1 < 2 < 3 < 4 < 5 (`docs/bot-search.md`, "Levels").
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
    [[nodiscard]] inline String LevelRangeText() {
        return Format("a whole number {}..{}", kMinLevel, kMaxLevel);
    }

}  // namespace BECore::Mahjong
