#pragma once

#include <Mahjong/Analyzer/AnalyzerRun.h>
#include <Mahjong/Arena/ArenaChartsClasses.h>
#include <Mahjong/Arena/ArenaHand.h>

namespace BECore::Mahjong::Arena {

    /// How far the hand is from waiting after Normal's discard; a charted turn is never at 0 (`SelectDiscardPair`).
    CORE_ENUM(DiscardShanten, uint8_t, One, Two, ThreeOrMore)
    /// What the valued discard costs in shanten against Normal's (it is never lower: Normal's discard leaves the lowest).
    CORE_ENUM(ValueCost, uint8_t, Same, Higher)
    /// The most copies of one value tile the seat has, its melds counted.
    CORE_ENUM(ValueHeld, uint8_t, None, Single, PairOrSet)

    /// The class of one measured own turn: the six fields of the discard charts, a pure function of a `TableView` and the pair.
    struct DiscardClass {
        ValueHeld valueHeld = ValueHeld::None;
        DiscardShanten shanten = DiscardShanten::One;
        ValueCost cost = ValueCost::Same;
        WallStage stage = WallStage::Early;
        OpenSets openSets = OpenSets::None;
        bool east = false;

        bool operator==(const DiscardClass&) const = default;
    };

    /// The two discards of a measured turn: what Normal discards and what `ValueDiscards` at depth 1 ranks first.
    struct DiscardPair {
        Tile fast;
        Tile valued;

        bool operator==(const DiscardPair&) const = default;
    };

    /// What `SelectDiscardPair` made of a turn.
    enum class DiscardOutcome {
        /// The pair is measured.
        Measured,
        /// Not a step-5 turn of Normal (a win, a declaration, a kong, a kept wait), or not an own discard turn: nothing to chart.
        OtherStep,
        /// `ValueDiscards` rates no discard.
        NoValue,
        /// The valued tile is one of Normal's own ties.
        SameAsNormal,
    };

    struct DiscardSelection {
        DiscardOutcome outcome = DiscardOutcome::OtherStep;
        /// Set when `outcome` is `Measured`.
        DiscardPair pair;
    };

    [[nodiscard]] DiscardShanten DiscardShantenBin(int shanten) noexcept;
    [[nodiscard]] ValueCost ValueCostOf(int normalShanten, int valuedShanten) noexcept;
    /// 0 copies none, 1 a single, 2 or more a pair or a set.
    [[nodiscard]] ValueHeld ValueHeldBin(int copies) noexcept;
    /// The most copies of one value tile in the seat's concealed tiles and melds (a pung is 3, a kong 4).
    [[nodiscard]] int MostValueCopies(const TableView& view) noexcept;

    /**
     * @brief The two discards of an own turn and whether the charts measure it.
     *
     * Fast is Normal's step-5 discard, valued the first of `ValueDiscards(view, 1)`; the turn is measured only when valued is not one of
     * Normal's own ties (`docs/charts-discards.md`, "The pair"). A pure function of the view: no model, so no hidden tile can enter. A
     * view that is not the discard turn of its seat is `OtherStep`.
     */
    [[nodiscard]] DiscardSelection SelectDiscardPair(const TableView& view);

    /**
     * @brief The class of the measured turn `view` with `pair`.
     *
     * The shanten of each tile is its entry of `RateDiscards`, so the class and the selection rate a discard the same way. `pair` must
     * be a `Measured` selection of `view`.
     */
    [[nodiscard]] DiscardClass ClassifyDiscard(const TableView& view, const DiscardPair& pair);

    /// The words the report prints, lower case.
    [[nodiscard]] StringView DiscardShantenName(DiscardShanten band) noexcept;
    [[nodiscard]] StringView ValueCostName(ValueCost cost) noexcept;
    [[nodiscard]] StringView ValueHeldName(ValueHeld held) noexcept;

}  // namespace BECore::Mahjong::Arena
