#include "MahjongHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongHudComponent.gen.hpp>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/CenterPanel.h>
#include <Mahjong/Table/MoveText.h>
#include <Mahjong/Table/RulesText.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TileAtlas.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetSeatPrefix = "MahjongSetSeat";
        constexpr eastl::string_view kSetSeatLevelPrefix = "MahjongSetSeatLevel";
        constexpr eastl::string_view kSetHandsAction = "MahjongSetHands";
        // The ids of the two segments of `Hands` (the values of the buttons in the scene file): the numbers of `MahjongSettings.h` as text
        constexpr eastl::string_view kFullHandsId = "16";
        constexpr eastl::string_view kShortHandsId = "8";
        static_assert(kFullMatchHands == 16 && kShortMatchHands == 8, "the ids and the buttons `Hands16` / `Hands8` of the scene follow these numbers");
        constexpr eastl::string_view kPersonId = "person";
        constexpr eastl::string_view kComputerId = "computer";
        constexpr eastl::string_view kNewGameAction = "MahjongNewGame";
        constexpr eastl::string_view kStartAction = "MahjongStart";
        constexpr eastl::string_view kContinueAction = "MahjongContinue";
        constexpr eastl::string_view kNextHandAction = "MahjongNextHand";
        constexpr eastl::string_view kSetLanguageAction = "MahjongSetLanguage";
        constexpr eastl::string_view kOptionsAction = "MahjongOptions";
        constexpr eastl::string_view kOptionsBackAction = "MahjongOptionsBack";
        constexpr eastl::string_view kSetupCloseAction = "MahjongSetupClose";
        constexpr eastl::string_view kSetSpeedAction = "MahjongSetSpeed";
        constexpr eastl::string_view kRulesAction = "MahjongRules";
        constexpr eastl::string_view kRulesNextAction = "MahjongRulesNext";
        constexpr eastl::string_view kRulesBackAction = "MahjongRulesBack";
        constexpr eastl::string_view kRulesCloseAction = "MahjongRulesClose";

        /// "RulesLine3": the element or variable `base` of number `index`.
        PoolString NumberedName(const char* base, int index) {
            return PoolString::Intern(Format("{}{}", base, index));
        }

        /// The canvas of the node `nodeName`; null when there is none.
        IntrusivePtr<UICanvasComponent> CanvasOf(Scene& scene, PoolString nodeName) {
            for (const auto& child : scene.GetRootNode().GetChildren()) {
                if (child->GetName() == nodeName) {
                    return child->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// "seatKind2": the variable of a seat, "plateName2": that of the plate at a screen position.
        PoolString SeatVar(const char* base, int index) {
            return PoolString::Intern(Format("{}{}", base, index));
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

        /// The seat that `MahjongSetSeatLevelN` is about; nullopt for another action.
        std::optional<Seat> SeatOfLevelAction(eastl::string_view action) {
            if (action.size() != kSetSeatLevelPrefix.size() + 1 || action.substr(0, kSetSeatLevelPrefix.size()) != kSetSeatLevelPrefix) {
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
        SubscribeInScene<SDLEvents::KeyDownEvent, &MahjongHudComponent::OnKeyDown>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &MahjongHudComponent::OnUpdate>(this);
    }

    void MahjongHudComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& /*event*/) {
        BE_PROFILE_SCOPE("MahjongHud::OnUpdate");
        if (MahjongGame* game = MahjongGame::GetActive(); game != nullptr) {
            game->FlushSave();
            _leaveSent = _leaveSent && game->IsOnlineTable();  // the room's table ended: a leave is over
        }
    }

    void MahjongHudComponent::PublishNewGameWord(const MahjongGame* game) {
        const bool room = game != nullptr && game->IsOnlineTable();
        const uint64_t stringsVersion = CoreManager::GetUIStrings().GetVersion();
        if (_newGameWordPublished && _newGameWordRoom == room && _newGameWordStrings == stringsVersion) {
            return;
        }
        _newGameWordPublished = true;
        _newGameWordRoom = room;
        _newGameWordStrings = stringsVersion;
        // The button of the HUD menu and of the standings: in a room it leaves the room instead of dealing a new match
        const eastl::string word = Tr(CoreManager::GetUIStrings(), room ? "hud.leaveRoom" : "hud.newGame");
        CoreManager::GetUIVars().Set("newGameWord"_intern, eastl::string_view(word));
    }

    eastl::string MahjongHudComponent::RoomLine(const MahjongGame& game, const UIStrings& strings) {
        const OnlineClient* client = game.FindOnline();
        if (client == nullptr) {
            return {};
        }
        // A word of the client that comes while the table is up (a view that could not be read) stays until the next view moves the table
        const OnlineNotice& notice = client->Notice();
        if (!_roomLineSeen) {
            _roomLineSeen = true;
            _noticeSeen = notice.serial;
            _noteKey = {};
        } else if (notice.serial != _noticeSeen) {
            _noticeSeen = notice.serial;
            _noteKey = notice.key;
            _noteArg = notice.arg;
            _noteTable = client->Table().version;
        }
        if (!_noteKey.empty() && client->Table().version != _noteTable) {
            _noteKey = {};
        }
        if (client->Stage() == OnlineStage::Reconnecting) {
            return OnlineNoticeText(strings, kOnlineReconnectingKey);
        }
        return _noteKey.empty() ? eastl::string() : OnlineNoticeText(strings, _noteKey, _noteArg);
    }

    void MahjongHudComponent::OnDraw() {
        BE_PROFILE_SCOPE("MahjongHud::OnDraw");
        const MahjongGame* game = MahjongGame::GetActive();
        PublishNewGameWord(game);
        PublishNumbers(game);
        PublishSetup(game);
        PublishOptions(game);
        PublishEndScreens(game);
        PublishRules(game);
    }

    void MahjongHudComponent::PublishNumbers(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongHud::PublishNumbers");
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        if (!started) {
            _numbersOf = nullptr;
            _numbersPublished = false;
            _roomLineSeen = false;
            ShowMoveLine(false);
            return;
        }
        const TableSession& session = game->GetSession();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (_numbersPublished && _numbersOf == &session && _numbersVersion == session.Version() && _numbersStrings == strings.GetVersion()) {
            return;
        }
        _numbersOf = &session;
        _numbersVersion = session.Version();
        _numbersStrings = strings.GetVersion();
        _numbersPublished = true;

        const Match& match = game->GetMatch();
        const HandModel& hand = match.Hand();
        UIVars& vars = CoreManager::GetUIVars();
        // The line under the discards: in a room the connection or a word of the client first; else the last move, in words, only while a hand is
        // played and has a move (the end screens speak after it)
        eastl::string lastMove;
        if (game->IsOnlineTable()) {
            lastMove = RoomLine(*game, strings);
        } else {
            _roomLineSeen = false;
        }
        if (lastMove.empty() && session.GetPhase() == SessionPhase::Playing && !hand.Moves().empty()) {
            const HandMove& move = hand.Moves().back();
            lastMove = MoveText(strings, move, hand.SeatWind(move.seat));
        }
        vars.Set("lastMove"_intern, eastl::string_view(lastMove));
        ShowMoveLine(!lastMove.empty());

        // The plates' names and the centre panel, by screen position: the seat there, its wind letter and its score
        const auto plates = MakeSeatPlates(strings, session, match);
        const CenterPanelText panel = MakeCenterPanel(strings, session, match);
        vars.Set("panelRound"_intern, eastl::string_view(panel.round));
        vars.Set("panelWall"_intern, eastl::string_view(panel.wallText));
        for (int position = 0; position < kSeatCount; ++position) {
            const CenterPanelSeat& side = panel.seats[position];
            vars.Set(SeatVar("plateName", position), eastl::string_view(plates[position].name));
            vars.Set(SeatVar("panelWind", position), eastl::string_view(side.wind));
            vars.Set(SeatVar("panelScore", position), side.score);
        }
    }

    void MahjongHudComponent::ShowMoveLine(bool show) {
        if (show != _moveLineShown) {
            _moveLineShown = show;
            UICanvasComponent::SetElementVisibleByName(GetScene(), "MoveLine"_intern, show);
        }
    }

    void MahjongHudComponent::PublishSeatKinds() {
        UIVars& vars = CoreManager::GetUIVars();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            vars.Set(SeatVar("seatKind", seat), KindId(_pending[seat]));
        }
        PublishSeatLevels();
    }

    void MahjongHudComponent::PublishSeatLevels() {
        // The dropdown of a seat is there exactly while the seat is a computer; the level of a person's seat is kept, only not shown
        UIVars& vars = CoreManager::GetUIVars();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const std::string_view text = LevelText(_pendingLevels[seat]);
            vars.Set(SeatVar("seatLevel", seat), eastl::string_view(text.data(), text.size()));
            UICanvasComponent::SetElementVisibleByName(GetScene(), SeatVar("SeatLevel", seat), _pending[seat] == SeatKind::Computer);
        }
    }

    void MahjongHudComponent::PublishHandsChoice() const {
        // The number as text, also a number the panel does not offer: then no segment is selected
        CoreManager::GetUIVars().Set("handsChoice"_intern, _pendingHands);
    }

    void MahjongHudComponent::PublishLanguage(const MahjongGame& game) const {
        CoreManager::GetUIVars().Set("language"_intern, game.GetLanguage().ToStringView());
    }

    void MahjongHudComponent::PublishSpeedChoice(const MahjongGame& game) const {
        const std::string_view text = SpeedText(game.GetAnimationSpeed());
        CoreManager::GetUIVars().Set("speedChoice"_intern, eastl::string_view(text.data(), text.size()));
    }

    void MahjongHudComponent::PublishSetup(const MahjongGame* game) {
        const bool open = game != nullptr && game->IsSetupOpen();
        if (open && !_setupShown) {
            _pending = game->GetSeats();  // the panel opens on the seats of the last match
            _pendingLevels = game->GetLevels();
            PublishSeatKinds();
            _pendingHands = game->GetSettings().handsPerMatch;
            PublishHandsChoice();
            PublishLanguage(*game);
        }
        if (open != _setupShown) {
            _setupShown = open;
            UICanvasComponent::SetVisibleByName(GetScene(), "Setup"_intern, open);
        }
        const bool offerContinue = open && game->CanContinue();
        if (offerContinue != _continueShown) {
            _continueShown = offerContinue;
            UICanvasComponent::SetElementVisibleByName(GetScene(), "Continue"_intern, offerContinue);
        }
    }

    void MahjongHudComponent::PublishOptions(const MahjongGame* game) {
        const bool open = game != nullptr && game->IsOptionsOpen();
        if (open == _optionsShown) {
            return;
        }
        _optionsShown = open;
        if (open) {
            // The rows show what the game has now; the hints row is published by the hints component while the page is open
            PublishLanguage(*game);
            PublishSpeedChoice(*game);
        }
        UICanvasComponent::SetVisibleByName(GetScene(), "Options"_intern, open);
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
        const OnlineClient* room = game.IsOnlineTable() ? game.FindOnline() : nullptr;
        HandSettlement settlement = SettleHand(match.Hand());
        if (room != nullptr && room->Table().result.has_value()) {
            // A display hand does not know everything the room scored with (`Online/CLAUDE.md`): the points are the server's
            settlement.scores = room->Table().result->scores;
            settlement.deltas = room->Table().result->deltas;
        }
        SeatScores totals = match.Scores();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            totals[seat] += settlement.deltas[seat];
        }
        const UIStrings& strings = CoreManager::GetUIStrings();
        const ScoreTable table = FormatScoreTable(strings, settlement, match.Hand().Result().value_or(HandResult{}), totals, SeatWho(session));

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
        // The row under the lines: in a room how many have read the plate (the coach is off there), else the coach's tally
        eastl::string tally;
        if (room != nullptr) {
            tally = ReadyText(strings, room->Table());
            SetNextHandDisabled(!CanSendNextHand(*room));
        } else {
            const CoachLedger& coach = game.GetCoach();
            tally = game.CoachActive() ? FormatCoachTally(strings, coach.HandTallyOf(HandKeyOf(session)), coach.MatchTallyOf(session.StartCount())) : eastl::string();
            SetNextHandDisabled(false);
        }
        vars.Set("handOverCoach"_intern, eastl::string_view(tally));
        UICanvasComponent::SetElementVisibleByName(GetScene(), "HandCoachRow"_intern, !tally.empty());
    }

    eastl::string MahjongHudComponent::ReadyText(const UIStrings& strings, const OnlineTable& table) {
        int ready = 0;
        for (const bool seat : table.ready) {
            ready += seat ? 1 : 0;
        }
        if (ready == 0) {
            return {};
        }
        return Tr(strings, "online.table.ready", {{"n", Format("{}", ready)}, {"total", Format("{}", kSeatCount)}});
    }

    bool MahjongHudComponent::CanSendNextHand(const OnlineClient& client) {
        if (client.Stage() != OnlineStage::Game) {
            return false;
        }
        const auto& actions = client.Table().actions;
        return eastl::any_of(actions.begin(), actions.end(), [](const ControlAction& listed) { return listed.nextHand; });
    }

    void MahjongHudComponent::SetNextHandDisabled(bool disabled) {
        const IntrusivePtr<UICanvasComponent> canvas = CanvasOf(GetScene(), "HandOverAction"_intern);
        UIElement* button = canvas ? canvas->GetRoot().FindByName("NextHand"_intern) : nullptr;
        if (UIInputClicker* clicker = button != nullptr ? button->GetComponent<UIInputClicker>() : nullptr) {
            clicker->disabled = disabled;
        }
    }

    void MahjongHudComponent::ShowHandOver(bool shown) {
        Scene& scene = GetScene();
        UICanvasComponent::SetVisibleByName(scene, "HandOver"_intern, shown);
        UICanvasComponent::SetVisibleByName(scene, "HandOverAction"_intern, shown);
        if (!shown) {
            SetNextHandDisabled(false);  // a room's ready check is over with the plate
        }
        // The score table is a screen of its own (no dim, so the hands stay visible): HudMenu (New game, Options) goes while it is up.
        // The standings (MatchOver) are a Screen canvas whose dim covers the whole window, so they need nothing.
        ShowHudMenu();
    }

    void MahjongHudComponent::ShowHudMenu() {
        UICanvasComponent::SetVisibleByName(GetScene(), "HudMenu"_intern, !_handOverShown && !_rulesShown);
    }

    void MahjongHudComponent::PublishRulesPage() {
        Scene& scene = GetScene();
        const UIStrings& strings = CoreManager::GetUIStrings();
        const RulesPage page = FormatRulesPage(strings, _rulesPage);
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("rulesTitle"_intern, eastl::string_view(page.title));
        vars.Set("rulesPager"_intern, eastl::string_view(RulesPagerText(strings, _rulesPage)));
        for (int i = 0; i < kRulesMaxLines; ++i) {
            const bool has = i < static_cast<int>(page.lines.size());
            vars.Set(NumberedName("rulesLine", i), has ? eastl::string_view(page.lines[static_cast<size_t>(i)]) : eastl::string_view());
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesLine", i), has);
        }
        for (int i = 0; i < kRulesMaxItems; ++i) {
            const bool has = i < static_cast<int>(page.items.size());
            vars.Set(NumberedName("rulesItem", i), has ? eastl::string_view(page.items[static_cast<size_t>(i)]) : eastl::string_view());
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesItem", i), has);
        }
        for (int row = 0; row < kRulesMaxItems / kRulesItemColumns; ++row) {
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesItemRow", row), row * kRulesItemColumns < static_cast<int>(page.items.size()));
        }
        const IntrusivePtr<UICanvasComponent> rulesCanvas = CanvasOf(scene, "Rules"_intern);
        ASSERT(rulesCanvas != nullptr, "PublishRulesPage: the node Rules has no canvas");
        for (int row = 0; row < kRulesMaxTileRows; ++row) {
            const bool hasRow = row < static_cast<int>(page.tileRows.size());
            const int count = hasRow ? static_cast<int>(page.tileRows[static_cast<size_t>(row)].size()) : 0;
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesTileRow", row), hasRow);
            for (int column = 0; column < kRulesMaxRowTiles; ++column) {
                const PoolString name = NumberedName("RulesTile", row * kRulesMaxRowTiles + column);
                const bool has = column < count;
                if (has) {
                    UIElement* element = rulesCanvas ? rulesCanvas->GetRoot().FindByName(name) : nullptr;
                    UIImageComponent* image = element != nullptr ? element->GetComponent<UIImageComponent>() : nullptr;
                    ASSERT(image != nullptr, "PublishRulesPage: the scene file has no UIImageComponent on RulesTile<N> for a tile of the page");
                    if (image != nullptr) {
                        image->sprite = PoolString::Intern(SpriteName(page.tileRows[static_cast<size_t>(row)][static_cast<size_t>(column)]));
                    }
                }
                UICanvasComponent::SetElementVisibleByName(scene, name, has);
            }
        }
        UICanvasComponent::SetElementVisibleByName(scene, "RulesBack"_intern, _rulesPage > 0);
        UICanvasComponent::SetElementVisibleByName(scene, "RulesNext"_intern, _rulesPage + 1 < RulesPageCount());
    }

    void MahjongHudComponent::PublishRules(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongHud::PublishRules");
        const bool open = game != nullptr && game->IsRulesOpen();
        if (!open) {
            _rulesPage = 0;
        }
        if (open) {
            const uint64_t stringsVersion = CoreManager::GetUIStrings().GetVersion();
            if (!_rulesShown || _rulesPublishedPage != _rulesPage || _rulesStrings != stringsVersion) {
                _rulesPublishedPage = _rulesPage;
                _rulesStrings = stringsVersion;
                PublishRulesPage();
            }
        }
        if (open != _rulesShown) {
            _rulesShown = open;
            Scene& scene = GetScene();
            UICanvasComponent::SetVisibleByName(scene, "Rules"_intern, open);
            UICanvasComponent::SetVisibleByName(scene, "RulesAction"_intern, open);
            ShowHudMenu();
        }
    }

    void MahjongHudComponent::PublishMatchOver(const MahjongGame& game) {
        const StandingsTable table = FormatStandings(CoreManager::GetUIStrings(), game.GetMatch(), SeatWho(game.GetSession()));
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("matchOverHeadline"_intern, eastl::string_view(table.headline));
        for (Seat row = 0; row < kSeatCount; ++row) {
            vars.Set(SeatVar("standing", row), eastl::string_view(table.rows[row]));
        }
        const UIStrings& strings = CoreManager::GetUIStrings();
        const eastl::string tally =
            game.CoachActive() && !game.IsOnlineTable() ? FormatCoachTally(strings, CoachTally{}, game.GetCoach().MatchTallyOf(game.GetSession().StartCount())) : eastl::string();
        vars.Set("matchOverCoach"_intern, eastl::string_view(tally));
        UICanvasComponent::SetElementVisibleByName(GetScene(), "MatchCoachRow"_intern, !tally.empty());
    }

    void MahjongHudComponent::PublishEndScreens(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongHud::PublishEndScreens");
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        const bool handOver = started && game->GetSession().GetPhase() == SessionPhase::HandOver;
        const bool matchOver = started && game->GetMatch().GetPhase() == MatchPhase::Finished;
        if (handOver || matchOver) {
            const uint64_t stringsVersion = CoreManager::GetUIStrings().GetVersion();
            const bool coachOn = game->CoachActive();
            if (!_endScreenPublished || _endScreenOf != &game->GetSession() || _endScreenVersion != game->GetSession().Version() || _endScreenStrings != stringsVersion || _endScreenCoach != coachOn) {
                _endScreenOf = &game->GetSession();
                _endScreenCoach = coachOn;
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
        // The score table waits while the table holds it back (a person's claim lost to a mahjong and the flight is on screen); the words
        // above are published at once
        const MahjongTableComponent* table = MahjongTableComponent::GetActive();
        const bool handOverShown = handOver && (table == nullptr || !table->HandOverHeld());
        if (handOverShown != _handOverShown) {
            _handOverShown = handOverShown;
            ShowHandOver(handOverShown);
        }
        if (matchOver != _matchOverShown) {
            _matchOverShown = matchOver;
            UICanvasComponent::SetVisibleByName(GetScene(), "MatchOver"_intern, matchOver);
        }
    }

    void MahjongHudComponent::OnKeyDown(const SDLEvents::KeyDownEvent& event) {
        if (event.key != SDLK_ESCAPE || event.repeat) {
            return;
        }
        // Escape closes the page `Options` or `Setup`, whichever is open; with neither open it is not ours
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr || game->IsOnlineOpen()) {
            return;  // the online panel owns Escape while it is up (`MahjongOnlineComponent`)
        }
        if (game->IsOptionsOpen()) {
            game->CloseOptions();
        } else if (game->IsSetupOpen()) {
            game->CloseSetup();
        }
    }

    void MahjongHudComponent::LeaveRoom(MahjongGame& game) {
        if (_leaveSent) {
            return;  // the server has not answered yet: a second leave would only be refused
        }
        _leaveSent = game.GetOnline().Leave();
    }

    void MahjongHudComponent::SendNextHand(MahjongGame& game) {
        // The server's own `next_hand` of the list now; none listed (already sent, the hand moved on, the link is down) and the click is ignored
        OnlineClient& client = game.GetOnline();
        const auto& actions = client.Table().actions;
        for (size_t i = 0; i < actions.size(); ++i) {
            if (actions[i].nextHand) {
                client.Act(i);
                return;
            }
        }
    }

    void MahjongHudComponent::OnAction(const InputEvents::ActionEvent& event) {
        BE_PROFILE_SCOPE("MahjongHud::OnAction");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kNewGameAction) {
            if (game->IsOnlineTable()) {
                LeaveRoom(*game);  // the room's table has no new game: the button leaves, and the end of the table opens `Setup`
            } else {
                game->OpenSetup();
            }
            return;
        }
        if (action == kOptionsAction) {
            // The HUD button: under the dim of the seat choice or behind the rules it cannot be pressed, an event that says otherwise is
            // out of date
            if (!game->IsSetupOpen() && !game->IsRulesOpen()) {
                game->OpenOptions();
            }
            return;
        }
        if (action == kOptionsBackAction) {
            game->CloseOptions();  // a click while the page is closed finds it closed
            return;
        }
        if (action == kRulesAction) {
            // The button of the page `Options`: the page gives way to the rules. A click while the page is closed is out of date
            if (game->IsOptionsOpen()) {
                game->CloseOptions();
                if (!game->IsRulesOpen()) {
                    _rulesPage = 0;  // the screen opens on its first page, whatever the order of the events
                }
                game->OpenRules();
            }
            return;
        }
        if (action == kSetSpeedAction) {
            // The page is where the speed is set; client text is parsed by `TryParseSpeed`, anything else is ignored
            const eastl::string_view text = event.value.ToStringView();
            const std::optional<AnimationSpeed> speed = TryParseSpeed(std::string_view(text.data(), text.size()));
            if (game->IsOptionsOpen() && speed) {
                game->SetAnimationSpeed(*speed);
                PublishSpeedChoice(*game);
            }
            return;
        }
        if (action == kSetLanguageAction) {
            // The language row is on the page: a click that arrives while the page is closed is out of date. The game keeps the language
            // and applies it to the table; an id the table does not have changes nothing, the variable keeps the truth
            if (game->IsOptionsOpen()) {
                game->SetLanguage(event.value);
                PublishLanguage(*game);
            }
            return;
        }
        if (action == kRulesNextAction || action == kRulesBackAction || action == kRulesCloseAction) {
            // The pages turn only while they are open: a click that arrives after a control action closed them is out of date
            if (!game->IsRulesOpen()) {
                return;
            }
            if (action == kRulesCloseAction) {
                game->CloseRules();
            } else {
                _rulesPage = std::clamp(_rulesPage + (action == kRulesNextAction ? 1 : -1), 0, RulesPageCount() - 1);
            }
            return;
        }
        if (action == kNextHandAction) {
            if (game->IsOnlineTable()) {
                SendNextHand(*game);
                return;
            }
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
        if (action == kSetupCloseAction) {
            // The cross only goes back to where the player was: nothing of the pending choice is applied, nothing starts
            game->CloseSetup();
            return;
        }
        if (action == kContinueAction) {
            game->ContinueMatch();  // ignored by the game when there is nothing to continue
            return;
        }
        if (action == kStartAction) {
            // The seats and the number of hands are the game's only when the match began with them (every other setting stays as it
            // is); a refusal leaves the choice open on the old ones
            const SeatSetup seatsBefore = game->GetSeats();
            const SeatLevels levelsBefore = game->GetLevels();
            const MahjongSettings settingsBefore = game->GetSettings();
            MahjongSettings settings = settingsBefore;
            settings.handsPerMatch = _pendingHands;
            game->SetSeats(_pending);
            game->SetLevels(_pendingLevels);
            game->SetSettings(settings);
            if (!game->StartRandomMatch().has_value()) {
                game->SetSeats(seatsBefore);
                game->SetLevels(levelsBefore);
                game->SetSettings(settingsBefore);
                LOG_WARNING("MahjongHudComponent: the match did not start, the seat choice stays open");
            }
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
        if (const std::optional<Seat> levelSeat = SeatOfLevelAction(action)) {
            // Only `1` .. `10` (`TryParseLevel`); the level is the panel's until `Start`
            const eastl::string_view text = event.value.ToStringView();
            if (const std::optional<ComputerLevel> level = TryParseLevel(std::string_view(text.data(), text.size()))) {
                _pendingLevels[*levelSeat] = *level;
                PublishSeatLevels();
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
