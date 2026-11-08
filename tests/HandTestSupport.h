#pragma once

#include "FixedWall.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * What the tests of a `HandModel` share: the seat names, `kExpectedStartingScore`, a sum of a number per seat, the name of an enum value
 * (`Name`), a tile from text, the error of a status, a snapshot of everything an action could change, a lap of discards, the checks of
 * `LegalActions` against `Apply` (`EveryCanonicalAction`, `AcceptedExactlyWhenListed`) and the chooser that plays a hand with random
 * legal actions. Header only. Included directly by `ComputerClaimTest`, `ComputerMatchTest`, `ComputerPlayerTest`,
 * `ComputerTestSupport.h`, `ComputerTurnTest`, `ComputerViewTest`, `HandModelActionsTest`, `HandModelClaimTest`, `HandModelKongTest`,
 * `HandModelPlayoutTest`, `HandModelTurnTest`, `MahjongGameTest`, `MatchTest` and `PaymentsTest`, and through `ScoreTestSupport.h`
 * by `MatchTestSupport.h` (so `MatchPlayoutTest` and `ComputerMatchTest`) and the `Score*Test` files. What differs
 * between the files (the quiet hands, the specs) stays in them.
 */
namespace BECore::Mahjong::Tests {

    inline constexpr Seat kSeat0 = 0;
    inline constexpr Seat kSeat1 = 1;
    inline constexpr Seat kSeat2 = 2;
    inline constexpr Seat kSeat3 = 3;

    /// The starting score of a seat, written out and not taken from the header (2000), for the tests of a match.
    inline constexpr int kExpectedStartingScore = 2000;

    /// The total of a number per seat (the points of the four seats, or the change of a hand: zero).
    inline int Sum(const SeatScores& scores) {
        int total = 0;
        for (const int value : scores) {
            total += value;
        }
        return total;
    }

    /// The name of an enum value, for a failure message.
    template <typename E>
    std::string Name(E value) {
        const auto name = EnumUtils<E>::ToString(value);
        return std::string(name.data(), name.size());
    }

    /// The tile of a text such as "5p" or "Rd"; the text is the test's own, so a bad one aborts at once.
    inline Tile T(const char* text) {
        return TryParse(text).value();
    }

    /// The error of a status, nullopt for a success (to compare with `EXPECT_EQ`).
    inline std::optional<HandError> Error(const HandStatus& status) {
        return status.has_value() ? std::nullopt : std::optional<HandError>(status.error());
    }

    /// Whether `actions` holds `wanted`.
    inline bool Has(const eastl::vector<HandAction>& actions, const HandAction& wanted) {
        return eastl::find(actions.begin(), actions.end(), wanted) != actions.end();
    }

    /// Everything an action could change, as text: phase and seat to act, per seat the concealed tiles, the melds (kind, first
    /// tile, concealed), the bonus tiles, the calling fields and the claim status, then the pile, the live and loose counts and
    /// whether a result exists.
    inline std::string Snapshot(const HandModel& model) {
        std::string text = std::to_string(static_cast<int>(model.GetPhase())) + " seat " + std::to_string(model.CurrentSeat());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const PlayerHand& hand = model.GetHand(seat);
            text += " |";
            for (const Tile tile : hand.concealed) {
                text += " ";
                text += ToString(tile);
            }
            text += " melds";
            for (const Meld& meld : hand.melds) {
                text += " " + std::to_string(static_cast<int>(meld.kind)) + std::string(ToString(meld.first)) + std::to_string(meld.concealed);
            }
            text += " bonus " + std::to_string(hand.bonus.size());
            text += " calling " + std::to_string(hand.callingDeclared) + "/" + std::to_string(hand.callingAtDiscardCount) + "/" + std::to_string(hand.discardCount);
            text += " claim " + std::to_string(static_cast<int>(model.GetClaimStatus(seat)));
        }
        text += " | pile";
        for (const DiscardEntry& entry : model.Discards()) {
            text += " ";
            text += ToString(entry.tile);
        }
        text += " | live " + std::to_string(model.GetWall().LiveCount()) + " loose " + std::to_string(model.GetWall().LooseCount());
        text += model.Result().has_value() ? " result" : " open";
        return text;
    }

    /// Each seat in turn discards the next tile of `tiles`; the seats in `declaring` declare the calling hand with their discard.
    /// No discard of a lap may open a claim window.
    inline void Lap(HandModel& model, const std::string& tiles, std::initializer_list<Seat> declaring = {}) {
        const eastl::vector<Tile> parsed = ParseTiles(tiles).value();
        for (const Tile tile : parsed) {
            const Seat seat = model.CurrentSeat();
            const bool declare = eastl::find(declaring.begin(), declaring.end(), seat) != declaring.end();
            const HandStatus status = model.Discard(seat, tile, declare);
            ASSERT_TRUE(status.has_value()) << "seat " << seat << " cannot discard " << ToString(tile);
            ASSERT_EQ(model.GetPhase(), Phase::Discard) << "the discard of " << ToString(tile) << " by seat " << seat << " opened a window";
        }
    }

    /// Every action of every seat's list is accepted by `Apply` (on a copy, so that each is tried from the same state).
    inline void ExpectEveryLegalActionIsAccepted(const HandModel& model) {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (const HandAction& action : model.LegalActions(seat)) {
                HandModel copy = model;
                EXPECT_TRUE(copy.Apply(seat, action).has_value()) << "seat " << seat << " action " << static_cast<int>(action.kind) << " " << ToString(action.tile);
            }
        }
    }

    /// Every canonical action a seat could try, listed or not: a discard of each playable kind with and without the declaration, the
    /// win, both kongs of each kind, a claim of each kind (a chow with every possible first tile, and with none), the pass.
    inline const eastl::vector<HandAction>& EveryCanonicalAction() {
        static const eastl::vector<HandAction> candidates = [] {
            eastl::vector<HandAction> list;
            list.push_back(HandAction::MakeDeclareWin());
            list.push_back(HandAction::MakePass());
            list.push_back(HandAction::MakeClaim(ClaimKind::Chow));
            list.push_back(HandAction::MakeClaim(ClaimKind::Pung));
            list.push_back(HandAction::MakeClaim(ClaimKind::Kong));
            list.push_back(HandAction::MakeClaim(ClaimKind::Mahjong));
            for (uint8_t id = 0; id < kKindCount; ++id) {
                const Tile tile{id};
                list.push_back(HandAction::MakeDiscard(tile));
                list.push_back(HandAction::MakeDiscard(tile, true));
                list.push_back(HandAction::MakeConcealedKong(tile));
                list.push_back(HandAction::MakeUpgradeKong(tile));
                list.push_back(HandAction::MakeClaim(ClaimKind::Chow, tile));
            }
            return list;
        }();
        return candidates;
    }

    /// `Apply` accepts a canonical action of `seat` exactly when `LegalActions(seat)` lists it (each tried on a copy). The failure
    /// names the first action that disagrees.
    inline ::testing::AssertionResult AcceptedExactlyWhenListed(const HandModel& model, Seat seat) {
        const eastl::vector<HandAction> legal = model.LegalActions(seat);
        for (const HandAction& candidate : EveryCanonicalAction()) {
            HandModel copy = model;
            const bool accepted = copy.Apply(seat, candidate).has_value();
            const bool listed = Has(legal, candidate);
            if (accepted != listed) {
                return ::testing::AssertionFailure() << "seat " << seat << " action " << Name(candidate.kind) << " tile " << ToString(candidate.tile) << " declare " << candidate.declareCalling
                                                     << " claim " << Name(candidate.claim) << " chowFirst " << ToString(candidate.chowFirst)
                                                     << (accepted ? " is accepted but not listed" : " is listed but refused");
            }
        }
        return ::testing::AssertionSuccess();
    }

    /// `AcceptedExactlyWhenListed` for every seat.
    inline void ExpectAcceptedExactlyWhenListed(const HandModel& model) {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_TRUE(AcceptedExactlyWhenListed(model, seat));
        }
    }

    // ----- playing a hand with random legal actions -----

    /// Which player draws the hand: one that takes every win and declares whenever it can, or one that never wins.
    CORE_ENUM(Mode, uint8_t, SeekWin, NeverWin)

    /// The seat that acts next: the one to discard, or a random one of the seats a window still waits for (`kNoSeat` when none).
    inline Seat NextActor(const HandModel& model, MahjongRng& rng) {
        if (model.GetPhase() == Phase::Discard) {
            return model.CurrentSeat();
        }
        const eastl::vector<Seat> pending = model.PendingClaimSeats();
        return pending.empty() ? kNoSeat : pending[rng.Below(static_cast<uint32_t>(pending.size()))];
    }

    /// Whether the action ends the hand with a win for its seat.
    inline bool WinsTheHand(const HandAction& action) {
        return action.kind == ActionKind::DeclareWin || (action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong);
    }

    inline HandAction PickRandom(const eastl::vector<HandAction>& actions, MahjongRng& rng) {
        return actions[rng.Below(static_cast<uint32_t>(actions.size()))];
    }

    /// The action a player of `mode` takes from a non-empty list.
    inline HandAction Choose(const eastl::vector<HandAction>& actions, Mode mode, MahjongRng& rng) {
        eastl::vector<HandAction> wanted;
        for (const HandAction& action : actions) {
            if (mode == Mode::NeverWin) {
                if (!WinsTheHand(action)) {
                    wanted.push_back(action);
                }
                continue;
            }
            // Win when it can, declare when it can, build sets when it can, otherwise anything.
            if (WinsTheHand(action)) {
                return action;
            }
            const bool declares = action.kind == ActionKind::Discard && action.declareCalling;
            const bool buildsSet = action.kind == ActionKind::Claim || action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong;
            if (declares || buildsSet) {
                wanted.push_back(action);
            }
        }
        return PickRandom(wanted.empty() ? actions : wanted, rng);
    }

    /// The hand in progress of `match` is played with random legal actions until it is finished.
    inline void PlayHand(Match& match, Mode mode, MahjongRng& rng) {
        HandModel& hand = match.Hand();
        while (hand.GetPhase() != Phase::Finished) {
            const Seat actor = NextActor(hand, rng);
            ASSERT_NE(actor, kNoSeat);
            const eastl::vector<HandAction> actions = hand.LegalActions(actor);
            ASSERT_FALSE(actions.empty());
            ASSERT_TRUE(hand.Apply(actor, Choose(actions, mode, rng)).has_value());
        }
    }

}  // namespace BECore::Mahjong::Tests
