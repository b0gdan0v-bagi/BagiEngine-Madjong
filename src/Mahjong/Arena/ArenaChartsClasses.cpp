#include "ArenaChartsClasses.h"

#include <Mahjong/Game/ComputerClaim.h>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kShantenOne = 1;
        constexpr int kShantenTwo = 2;
        constexpr int kOpenOne = 1;
        constexpr int kOpenTwo = 2;
        constexpr int kEarlyAbove = 2 * kLiveStageThird;

        int OpenMeldCount(const PlayerHand& hand) noexcept {
            int open = 0;
            for (const Meld& meld : hand.melds) {
                open += meld.concealed ? 0 : 1;
            }
            return open;
        }

    }  // namespace

    bool IsChartedClaim(const HandAction& action) noexcept {
        return action.kind == ActionKind::Claim && action.claim != ClaimKind::Mahjong;
    }

    ClaimChartKind KindOfClaim(ClaimKind kind) noexcept {
        switch (kind) {
            case ClaimKind::Pung:
                return ClaimChartKind::Pung;
            case ClaimKind::Kong:
                return ClaimChartKind::Kong;
            case ClaimKind::Chow:
            case ClaimKind::Mahjong:
                break;
        }
        return ClaimChartKind::Chow;
    }

    TileGroup TileGroupOf(Tile tile, Wind seatWind, Wind roundWind) noexcept {
        if (IsValueTile(tile, seatWind, roundWind)) {
            return TileGroup::ValueHonour;
        }
        if (tile.IsHonour()) {
            return TileGroup::OtherHonour;
        }
        return tile.IsTerminal() ? TileGroup::Terminal : TileGroup::Simple;
    }

    ShantenBefore ShantenBin(int shanten) noexcept {
        if (shanten <= kWaitingShanten) {
            return ShantenBefore::Zero;
        }
        if (shanten == kShantenOne) {
            return ShantenBefore::One;
        }
        return shanten == kShantenTwo ? ShantenBefore::Two : ShantenBefore::ThreeOrMore;
    }

    ShantenChange ShantenChangeOf(int before, int after) noexcept {
        if (after < before) {
            return ShantenChange::Lower;
        }
        return after == before ? ShantenChange::Equal : ShantenChange::Higher;
    }

    WallStage StageOfLiveCount(int liveCount) noexcept {
        if (liveCount > kEarlyAbove) {
            return WallStage::Early;
        }
        return liveCount > kLiveStageThird ? WallStage::Middle : WallStage::Late;
    }

    OpenSets OpenSetsBin(int open) noexcept {
        if (open < kOpenOne) {
            return OpenSets::None;
        }
        return open < kOpenTwo ? OpenSets::One : OpenSets::TwoOrMore;
    }

    ClaimClass ClassifyClaim(const TableView& view, const HandAction& claim) {
        const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
        ClaimClass cls;
        cls.kind = KindOfClaim(claim.claim);
        cls.tile = TileGroupOf(view.pendingTile, view.seatWind, view.roundWind);
        cls.shantenBefore = ShantenBin(before);
        cls.shantenChange = ShantenChangeOf(before, RateClaim(view, claim).shanten);
        cls.stage = StageOfLiveCount(view.liveCount);
        cls.east = view.seatWind == Wind::East;
        cls.openSets = OpenSetsBin(OpenMeldCount(view.own));
        return cls;
    }

    StringView KindName(ClaimChartKind kind) noexcept {
        switch (kind) {
            case ClaimChartKind::Chow:
                return "chow";
            case ClaimChartKind::Pung:
                return "pung";
            case ClaimChartKind::Kong:
                return "kong";
        }
        return "";
    }

    StringView TileGroupName(TileGroup group) noexcept {
        switch (group) {
            case TileGroup::Simple:
                return "simple";
            case TileGroup::Terminal:
                return "terminal";
            case TileGroup::ValueHonour:
                return "value honour";
            case TileGroup::OtherHonour:
                return "other honour";
        }
        return "";
    }

    StringView ShantenBeforeName(ShantenBefore band) noexcept {
        switch (band) {
            case ShantenBefore::Zero:
                return "0";
            case ShantenBefore::One:
                return "1";
            case ShantenBefore::Two:
                return "2";
            case ShantenBefore::ThreeOrMore:
                return "3+";
        }
        return "";
    }

    StringView ShantenChangeName(ShantenChange change) noexcept {
        switch (change) {
            case ShantenChange::Lower:
                return "lower";
            case ShantenChange::Equal:
                return "equal";
            case ShantenChange::Higher:
                return "higher";
        }
        return "";
    }

    StringView WallStageName(WallStage stage) noexcept {
        switch (stage) {
            case WallStage::Early:
                return "early";
            case WallStage::Middle:
                return "middle";
            case WallStage::Late:
                return "late";
        }
        return "";
    }

    StringView OpenSetsName(OpenSets sets) noexcept {
        switch (sets) {
            case OpenSets::None:
                return "0";
            case OpenSets::One:
                return "1";
            case OpenSets::TwoOrMore:
                return "2+";
        }
        return "";
    }

    StringView EastName(bool east) noexcept {
        return east ? "east" : "not east";
    }

}  // namespace BECore::Mahjong::Arena
