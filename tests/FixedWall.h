#pragma once

#include <Common/Printers.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/FixedWall.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * A wall laid out tile by tile for a test, so that it knows what every seat holds and what comes next: `Game/FixedWall.h` (the
 * layout, the padding, the clamping) with the tiles written in the notation of `ParseTiles` ("1p 2p 3p Ew Rd F1 T4"). Header
 * only; the rules tests of the hand (deal, turn, claims, kongs) build their walls with it.
 *
 * Whatever is wrong with a spec (a fifth copy, a bonus tile without `flowers`, a hand that is not 13 tiles, a text that does not
 * read, a `liveAfterDeal` out of range) fails the test (`ADD_FAILURE`) and the wall is built as well as it can be.
 */
namespace BECore::Mahjong::Tests {

    /// What a fixed wall holds, as texts: the members of `FixedDeal` (`hands[seat]`, `live`, `loose`, `liveAfterDeal`) in notation.
    struct FixedWallSpec {
        Array<String, kSeatCount> hands;
        String live;
        String loose;
        Optional<int> liveAfterDeal;
    };

    namespace FixedWallDetail {

        /// The tiles of a text; a bad text fails the test and gives no tiles.
        inline Vector<Tile> Parse(const String& text, const char* what) {
            const Optional<Vector<Tile>> tiles = ParseTiles(text);
            if (!tiles.has_value()) {
                ADD_FAILURE() << "FixedWallSpec." << what << ": cannot read the tiles \"" << text << "\"";
                return {};
            }
            return *tiles;
        }

    }  // namespace FixedWallDetail

    /// The deal of `spec`, dealt from `east`.
    inline FixedDeal MakeFixedDeal(const FixedWallSpec& spec, Seat east = 0) {
        FixedDeal deal;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            deal.hands[seat] = FixedWallDetail::Parse(spec.hands[seat], "hands");
        }
        deal.live = FixedWallDetail::Parse(spec.live, "live");
        deal.loose = FixedWallDetail::Parse(spec.loose, "loose");
        deal.east = east;
        deal.liveAfterDeal = spec.liveAfterDeal;
        return deal;
    }

    /// A position of a match under way (`handsPlayed` hands counted, uneven scores): four hands, the tiles that come next are
    /// `Gd Gd 9m`, the loose reserve starts `4s 5s`.
    inline FixedStart MakeSamplePosition(Seat east = 0, int handsPlayed = 0) {
        FixedWallSpec spec;
        spec.hands[0] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s";
        spec.hands[1] = "1m 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw";
        spec.hands[2] = "1p 2p 3p 4p 5p 6p 7p 8p 9p Rd Wd Gd Ew";
        spec.hands[3] = "5s 6s 7s 8s 9s 1m 2m 3m 4m 5m 6m 7m 8m";
        spec.live = "Gd Gd 9m";
        spec.loose = "4s 5s";
        FixedStart state;
        state.deal = MakeFixedDeal(spec, east);
        state.handsPlayed = handsPlayed;
        state.scores = {250, 120, -40, 900};
        return state;
    }

    /// The wall of `spec` (`BuildFixedWall`); each problem fails the test.
    inline Wall MakeFixedWall(const FixedWallSpec& spec, bool flowers, Seat east = 0) {
        FixedWallBuild build = BuildFixedWall(MakeFixedDeal(spec, east), flowers);
        for (const FixedDealProblem& problem : build.problems) {
            if (problem.member.empty()) {
                ADD_FAILURE() << "FixedWallSpec: " << ToStd(problem.what);
            } else {
                ADD_FAILURE() << "FixedWallSpec." << ToStd(problem.member) << " " << ToStd(problem.what);
            }
        }
        return eastl::move(build.wall);
    }

    /**
     * @brief A started hand on the fixed wall of `spec`: the winds follow counter-clockwise from `east` (that seat is the dealer and
     *        takes the first tile), `flowers` is the setting of the hand (it also decides the size of the wall).
     */
    inline HandModel MakeModel(const FixedWallSpec& spec, Seat east = 0, Wind roundWind = Wind::East, bool flowers = false) {
        MahjongSettings settings;
        settings.flowers = flowers;
        HandModel model;
        const HandStatus begun = model.Begin(settings, MakeFixedWall(spec, flowers, east), MakeSeatWinds(east), roundWind);
        EXPECT_TRUE(begun.has_value()) << "MakeModel: Begin refused the hand";
        return model;
    }

}  // namespace BECore::Mahjong::Tests
