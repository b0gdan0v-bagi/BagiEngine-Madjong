#include "MahjongHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHudComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/MoveText.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetSeatPrefix = "MahjongSetSeat";
        constexpr eastl::string_view kSetHandsAction = "MahjongSetHands";
        // The ids of the two segments of `Hands` (the values of the buttons in the scene file): the numbers of `MahjongSettings.h` as text
        constexpr eastl::string_view kFullHandsId = "16";
        constexpr eastl::string_view kShortHandsId = "8";
        static_assert(kFullMatchHands == 16 && kShortMatchHands == 8, "the ids and the buttons `Hands16` / `Hands8` of the scene follow these numbers");
        constexpr eastl::string_view kPersonId = "person";
        constexpr eastl::string_view kComputerId = "computer";
        constexpr eastl::string_view kTurnMark = ">";
        constexpr eastl::string_view kDealerMark = "*";
        constexpr eastl::string_view kNewGameAction = "MahjongNewGame";
        constexpr eastl::string_view kStartAction = "MahjongStart";
        constexpr eastl::string_view kNextHandAction = "MahjongNextHand";
        constexpr eastl::string_view kSetLanguageAction = "MahjongSetLanguage";

        /// "seatName2": the variable of a seat.
        PoolString SeatVar(const char* base, Seat seat) {
            return PoolString::Intern(Format("{}{}", base, seat));
        }

        eastl::string_view KindId(SeatKind kind) {
            return kind == SeatKind::Person ? kPersonId : kComputerId;
        }

        /// The seat that `MahjongSetSeatN` is about; nullopt for another action.
        std::optional<Seat> SeatOfAction(eastl::string_view action) {
            if (action.size() != kSetSeatPrefix.size() + 1 || action.substr(0, kSetSeatPrefix.size()) != kSetSeatPrefix) {
                return std::nullopt;
            }
            const int seat = action.back() - '0';
            return IsSeat(seat) ? std::optional<Seat>(seat) : std::nullopt;
        }

        std::optional<SeatKind> KindOfValue(eastl::string_view value) {
            if (value == kPersonId) {
                return SeatKind::Person;
            }
            if (value == kComputerId) {
                return SeatKind::Computer;
            }
            return std::nullopt;
        }

        /// Who is whose turn on the table, for the label `Turn:`, in the language of `strings`.
        eastl::string TurnText(const UIStrings& strings, const TableSession& session, const Match& match) {
            switch (session.GetPhase()) {
                case SessionPhase::HandOver:
                    return Tr(strings, "hud.handOver");
                case SessionPhase::MatchOver:
                    return Tr(strings, "hud.matchOver");
                case SessionPhase::Playing:
                    break;
            }
            const HandModel& hand = match.Hand();
            return SeatName(strings, hand.CurrentSeat(), hand.SeatWind(hand.CurrentSeat()));
        }

    }  // namespace

    MahjongHudComponent* MahjongHudComponent::s_active = nullptr;

    MahjongHudComponent::~MahjongHudComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongHudComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &MahjongHudComponent::OnAction>(this);
    }

    void MahjongHudComponent::OnDraw() {
        const MahjongGame* game = MahjongGame::GetActive();
        PublishNumbers(game);
        PublishSetup(game);
        PublishEndScreens(game);
    }

    void MahjongHudComponent::PublishNumbers(const MahjongGame* game) {
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        if (!started) {
            _numbersOf = nullptr;
            _numbersPublished = false;
            ShowMoveLine(false);
            return;
        }
        const TableSession& session = game->GetSession();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (_numbersPublished && _numbersOf == game && _numbersVersion == session.Version() && _numbersStrings == strings.GetVersion()) {
            return;
        }
        _numbersOf = game;
        _numbersVersion = session.Version();
        _numbersStrings = strings.GetVersion();
        _numbersPublished = true;

        const Match& match = game->GetMatch();
        const HandModel& hand = match.Hand();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("round"_intern, eastl::string_view(WindName(strings, match.RoundWind())));
        vars.Set("hand"_intern, std::min(match.HandsPlayed() + 1, match.HandsTotal()));
        vars.Set("handsTotal"_intern, match.HandsTotal());
        vars.Set("wall"_intern, static_cast<int>(hand.GetWall().LiveCount()));
        vars.Set("turn"_intern, eastl::string_view(TurnText(strings, session, match)));

        // The last move, in words: only while a hand is played and has a move (the end screens speak after it)
        eastl::string lastMove;
        if (session.GetPhase() == SessionPhase::Playing && !hand.Moves().empty()) {
            const HandMove& move = hand.Moves().back();
            lastMove = MoveText(strings, move, hand.SeatWind(move.seat));
        }
        vars.Set("lastMove"_intern, eastl::string_view(lastMove));
        ShowMoveLine(!lastMove.empty());

        const bool playing = session.GetPhase() == SessionPhase::Playing;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Wind wind = hand.SeatWind(seat);
            vars.Set(SeatVar("seatName", seat), eastl::string_view(SeatLabel(strings, seat, wind, session.Seats())));
            vars.Set(SeatVar("seatWind", seat), eastl::string_view(WindName(strings, wind)));
            vars.Set(SeatVar("seatScore", seat), match.Scores()[seat]);
            vars.Set(SeatVar("seatMark", seat), playing && hand.CurrentSeat() == seat ? kTurnMark : eastl::string_view());
            vars.Set(SeatVar("seatDealer", seat), match.East() == seat ? kDealerMark : eastl::string_view());
        }
    }

    void MahjongHudComponent::ShowMoveLine(bool show) {
        if (show != _moveLineShown) {
            _moveLineShown = show;
            UICanvasComponent::SetElementVisibleByName(GetScene(), "MoveLine"_intern, show);
        }
    }

    void MahjongHudComponent::PublishSeatKinds() const {
        UIVars& vars = CoreManager::GetUIVars();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            vars.Set(SeatVar("seatKind", seat), KindId(_pending[seat]));
        }
    }

    void MahjongHudComponent::PublishHandsChoice() const {
        // The number as text, also a number the panel does not offer: then no segment is selected
        CoreManager::GetUIVars().Set("handsChoice"_intern, _pendingHands);
    }

    void MahjongHudComponent::PublishLanguage(const MahjongGame& game) const {
        CoreManager::GetUIVars().Set("language"_intern, game.GetLanguage().ToStringView());
    }

    void MahjongHudComponent::PublishSetup(const MahjongGame* game) {
        const bool open = game != nullptr && game->IsSetupOpen();
        if (open && !_setupShown) {
            _pending = game->GetSeats();  // the panel opens on the seats of the last match
            PublishSeatKinds();
            _pendingHands = game->GetSettings().handsPerMatch;
            PublishHandsChoice();
            PublishLanguage(*game);
        }
        if (open != _setupShown) {
            _setupShown = open;
            UICanvasComponent::SetVisibleByName(GetScene(), "Setup"_intern, open);
        }
    }

    void MahjongHudComponent::ShowScoreLines(int count) {
        Scene& scene = GetScene();
        for (int i = std::min(count, _shownLines); i < std::max(count, _shownLines); ++i) {
            UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern(Format("ScoreLine{}", i)), i < count);
        }
        _shownLines = count;
    }

    void MahjongHudComponent::PublishHandOver(const MahjongGame& game) {
        const Match& match = game.GetMatch();
        const TableSession& session = game.GetSession();
        const HandSettlement settlement = SettleHand(match.Hand());
        SeatScores totals = match.Scores();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            totals[seat] += settlement.deltas[seat];
        }
        const UIStrings& strings = CoreManager::GetUIStrings();
        const ScoreTable table = FormatScoreTable(strings, settlement, match.Hand().Result().value_or(HandResult{}), totals, session.Seats());

        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("handOverHeadline"_intern, eastl::string_view(table.headline));
        vars.Set("handOverSummary"_intern, eastl::string_view(table.summary));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            vars.Set(SeatVar("handOverSeat", seat), eastl::string_view(table.seats[seat]));
        }
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(table.lines.size());
            vars.Set(PoolString::Intern(Format("scoreLine{}", i)), has ? eastl::string_view(table.lines[i]) : eastl::string_view());
        }
        ShowScoreLines(static_cast<int>(table.lines.size()));
        // The last hand of the match ends it: the button says so
        const MatchProgress after = NextProgress(MatchProgress{match.HandsPlayed(), match.East()}, settlement.kind, settlement.winner, match.GetSettings());
        vars.Set("handOverButton"_intern, eastl::string_view(Tr(strings, IsMatchOver(after.handsPlayed, match.GetSettings()) ? "handover.finish" : "handover.next")));
    }

    void MahjongHudComponent::PublishMatchOver(const MahjongGame& game) {
        const StandingsTable table = FormatStandings(CoreManager::GetUIStrings(), game.GetMatch(), game.GetSession().Seats());
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("matchOverHeadline"_intern, eastl::string_view(table.headline));
        for (Seat row = 0; row < kSeatCount; ++row) {
            vars.Set(SeatVar("standing", row), eastl::string_view(table.rows[row]));
        }
    }

    void MahjongHudComponent::PublishEndScreens(const MahjongGame* game) {
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        const bool handOver = started && game->GetSession().GetPhase() == SessionPhase::HandOver;
        const bool matchOver = started && game->GetMatch().GetPhase() == MatchPhase::Finished;
        if (handOver || matchOver) {
            const uint64_t stringsVersion = CoreManager::GetUIStrings().GetVersion();
            if (!_endScreenPublished || _endScreenOf != game || _endScreenVersion != game->GetSession().Version() || _endScreenStrings != stringsVersion) {
                _endScreenOf = game;
                _endScreenVersion = game->GetSession().Version();
                _endScreenStrings = stringsVersion;
                _endScreenPublished = true;
                if (handOver) {
                    PublishHandOver(*game);
                } else {
                    PublishMatchOver(*game);
                }
            }
        } else {
            _endScreenPublished = false;
        }
        if (handOver != _handOverShown) {
            _handOverShown = handOver;
            UICanvasComponent::SetVisibleByName(GetScene(), "HandOver"_intern, handOver);
        }
        if (matchOver != _matchOverShown) {
            _matchOverShown = matchOver;
            UICanvasComponent::SetVisibleByName(GetScene(), "MatchOver"_intern, matchOver);
        }
    }

    void MahjongHudComponent::OnAction(const InputEvents::ActionEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kNewGameAction) {
            game->OpenSetup();
            return;
        }
        if (action == kNextHandAction) {
            // Out of date when the hand is no longer over (a control client played on, or a second click)
            if (game->GetSession().GetPhase() == SessionPhase::HandOver && !game->GetSession().NextHand().has_value()) {
                LOG_WARNING("MahjongHudComponent: the finished hand could not be settled");
            }
            return;
        }
        // The rest belongs to the panel: a click that reached it while it is not open is out of date
        if (!game->IsSetupOpen()) {
            return;
        }
        if (action == kStartAction) {
            // The seats and the number of hands are the game's only when the match began with them (every other setting stays as it
            // is); a refusal leaves the choice open on the old ones
            const SeatSetup seatsBefore = game->GetSeats();
            const MahjongSettings settingsBefore = game->GetSettings();
            MahjongSettings settings = settingsBefore;
            settings.handsPerMatch = _pendingHands;
            game->SetSeats(_pending);
            game->SetSettings(settings);
            if (!game->StartRandomMatch().has_value()) {
                game->SetSeats(seatsBefore);
                game->SetSettings(settingsBefore);
                LOG_WARNING("MahjongHudComponent: the match did not start, the seat choice stays open");
            }
            return;
        }
        if (action == kSetLanguageAction) {
            // The game keeps the language and applies it to the table; an id the table does not have changes nothing, the variable keeps the truth
            game->SetLanguage(event.value);
            PublishLanguage(*game);
            return;
        }
        if (action == kSetHandsAction) {
            // Only the two numbers the panel offers; client text is never parsed into another number
            const eastl::string_view value = event.value.ToStringView();
            if (value == kFullHandsId || value == kShortHandsId) {
                _pendingHands = value == kFullHandsId ? kFullMatchHands : kShortMatchHands;
                PublishHandsChoice();
            }
            return;
        }
        const std::optional<Seat> seat = SeatOfAction(action);
        const std::optional<SeatKind> kind = KindOfValue(event.value.ToStringView());
        if (seat && kind) {
            _pending[*seat] = *kind;
            PublishSeatKinds();
        }
    }

}  // namespace BECore::Mahjong
