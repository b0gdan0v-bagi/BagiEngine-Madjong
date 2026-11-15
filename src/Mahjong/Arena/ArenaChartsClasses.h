#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong::Arena {

    /// A class of fewer rows than this is shown and marked `few`, never `significant` (`docs/charts-claims.md`, "The classes").
    inline constexpr int kFewRows = 30;

    /// The live wall at the deal: the ring without the loose reserve, the dead wall and the 13 tiles of each seat (flowers off, as
    /// the arena plays). East's first draw comes after it, so the first window sees at most one tile less.
    inline constexpr int kLiveWallAtDeal = RingSize(kStacksPerSide) - static_cast<int>(kLooseTiles) - static_cast<int>(kDeadWallTiles) - kSeatCount * kDealtTiles;
    /// The stages of the wall are thirds of `kLiveWallAtDeal` (rounded down; the early third takes the remainder).
    inline constexpr int kLiveStageThird = kLiveWallAtDeal / 3;

    /// What the seat claims; an open kong of the discard only (a concealed or upgraded kong is not a claim).
    CORE_ENUM(ClaimChartKind, uint8_t, Chow, Pung, Kong)
    /// The claimed tile: ranks 2..8 of a suit, rank 1 or 9, a dragon or the seat's or the round wind, any other wind.
    CORE_ENUM(TileGroup, uint8_t, Simple, Terminal, ValueHonour, OtherHonour)
    /// The shanten of the hand before the claim; `Zero` is a waiting hand.
    CORE_ENUM(ShantenBefore, uint8_t, Zero, One, Two, ThreeOrMore)
    /// What the claim does to the shanten (`RateClaim` against the hand as it is).
    CORE_ENUM(ShantenChange, uint8_t, Lower, Equal, Higher)
    /// How much of the live wall is left, in thirds of `kLiveWallAtDeal`.
    CORE_ENUM(WallStage, uint8_t, Early, Middle, Late)
    /// The seat's open melds before the claim (a concealed kong is not open).
    CORE_ENUM(OpenSets, uint8_t, None, One, TwoOrMore)

    /// The class of one claim of a window: the seven fields of the charts, a pure function of a `TableView` and the claim.
    struct ClaimClass {
        ClaimChartKind kind = ClaimChartKind::Chow;
        TileGroup tile = TileGroup::Simple;
        ShantenBefore shantenBefore = ShantenBefore::Zero;
        ShantenChange shantenChange = ShantenChange::Equal;
        WallStage stage = WallStage::Early;
        bool east = false;
        OpenSets openSets = OpenSets::None;

        bool operator==(const ClaimClass&) const = default;
    };

    /// Whether `action` is a claim the charts measure: a chow, a pung or a kong (a mahjong claim is taken at once by every level).
    [[nodiscard]] bool IsChartedClaim(const HandAction& action) noexcept;

    [[nodiscard]] ClaimChartKind KindOfClaim(ClaimKind kind) noexcept;
    [[nodiscard]] TileGroup TileGroupOf(Tile tile, Wind seatWind, Wind roundWind) noexcept;
    /// `shanten` at or below 0 is `Zero` (a hand to claim into is never below it).
    [[nodiscard]] ShantenBefore ShantenBin(int shanten) noexcept;
    [[nodiscard]] ShantenChange ShantenChangeOf(int before, int after) noexcept;
    /// Early above two thirds, late up to the first third (`kLiveStageThird`).
    [[nodiscard]] WallStage StageOfLiveCount(int liveCount) noexcept;
    [[nodiscard]] OpenSets OpenSetsBin(int open) noexcept;

    /**
     * @brief The class of `claim` in the claim window of `view`.
     *
     * Reads nothing but the view (the seat's own hand and the public table), so two worlds that differ only in the hidden tiles give
     * one class. `claim` must satisfy `IsChartedClaim` and be listed in `view.legal` of a claim window.
     */
    [[nodiscard]] ClaimClass ClassifyClaim(const TableView& view, const HandAction& claim);

    /// The words the report prints, lower case.
    [[nodiscard]] StringView KindName(ClaimChartKind kind) noexcept;
    [[nodiscard]] StringView TileGroupName(TileGroup group) noexcept;
    [[nodiscard]] StringView ShantenBeforeName(ShantenBefore band) noexcept;
    [[nodiscard]] StringView ShantenChangeName(ShantenChange change) noexcept;
    [[nodiscard]] StringView WallStageName(WallStage stage) noexcept;
    [[nodiscard]] StringView OpenSetsName(OpenSets sets) noexcept;
    [[nodiscard]] StringView EastName(bool east) noexcept;

}  // namespace BECore::Mahjong::Arena
