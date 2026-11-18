#include "MahjongHudComponent.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongHudComponent.gen.hpp>
#include <Mahjong/Components/MahjongOnlineComponent.h>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/CenterPanel.h>
#include <Mahjong/Table/HudFacts.h>
#include <Mahjong/Table/MoveText.h>
#include <Mahjong/Table/RulesText.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TileAtlas.h>

namespace BECore::Mahjong {

    namespace {

        /// The names this file asks for: nodes and elements of the scene files, UI variables, theme tokens, config names.
        struct Ids {
            static constexpr auto newGameWord = "newGameWord"_intern;
            static constexpr auto lastMove = "lastMove"_intern;
            static constexpr auto panelRound = "panelRound"_intern;
            static constexpr auto panelWall = "panelWall"_intern;
            static constexpr auto MoveLine = "MoveLine"_intern;
            static constexpr auto MatchOver = "MatchOver"_intern;
            static constexpr auto handsChoice = "handsChoice"_intern;
            static constexpr auto language = "language"_intern;
            static constexpr auto speedChoice = "speedChoice"_intern;
            static constexpr auto themeChoice = "themeChoice"_intern;
            static constexpr auto Setup = "Setup"_intern;
            static constexpr auto Continue = "Continue"_intern;
            static constexpr auto Options = "Options"_intern;
            static constexpr auto HandOver = "HandOver"_intern;
            static constexpr auto WinTiles = "WinTiles"_intern;
            static constexpr auto handOverHeadline = "handOverHeadline"_intern;
            static constexpr auto handOverSummary = "handOverSummary"_intern;
            static constexpr auto handOverButton = "handOverButton"_intern;
            static constexpr auto handOverCoach = "handOverCoach"_intern;
            static constexpr auto HandCoachRow = "HandCoachRow"_intern;
            static constexpr auto HandOverAction = "HandOverAction"_intern;
            static constexpr auto NextHand = "NextHand"_intern;
            static constexpr auto HudMenu = "HudMenu"_intern;
            static constexpr auto rulesTitle = "rulesTitle"_intern;
            static constexpr auto rulesPager = "rulesPager"_intern;
            static constexpr auto Rules = "Rules"_intern;
            static constexpr auto RulesBack = "RulesBack"_intern;
            static constexpr auto RulesNext = "RulesNext"_intern;
            static constexpr auto RulesAction = "RulesAction"_intern;
            static constexpr auto matchOverHeadline = "matchOverHeadline"_intern;
            static constexpr auto matchOverCoach = "matchOverCoach"_intern;
            static constexpr auto MatchCoachRow = "MatchCoachRow"_intern;
        };

        constexpr StringView kSetSeatPrefix = "MahjongSetSeat";
        constexpr StringView kSetSeatLevelPrefix = "MahjongSetSeatLevel";
        constexpr StringView kSetHandsAction = "MahjongSetHands";
        // The values of the two `Hands` buttons of the scene file
        constexpr StringView kFullHandsId = "16";
        constexpr StringView kShortHandsId = "8";
        static_assert(kFullMatchHands == 16 && kShortMatchHands == 8, "the ids and the buttons `Hands16` / `Hands8` of the scene follow these numbers");
        constexpr StringView kPersonId = "person";
        constexpr StringView kComputerId = "computer";
        constexpr StringView kNewGameAction = "MahjongNewGame";
        constexpr StringView kStartAction = "MahjongStart";
        constexpr StringView kContinueAction = "MahjongContinue";
        constexpr StringView kNextHandAction = "MahjongNextHand";
        constexpr StringView kSetLanguageAction = "MahjongSetLanguage";
        constexpr StringView kOptionsAction = "MahjongOptions";
        constexpr StringView kOptionsBackAction = "MahjongOptionsBack";
        constexpr StringView kSetupCloseAction = "MahjongSetupClose";
        constexpr StringView kSetSpeedAction = "MahjongSetSpeed";
        constexpr StringView kSetThemeAction = "MahjongSetTheme";
        constexpr StringView kRulesAction = "MahjongRules";
        constexpr StringView kRulesNextAction = "MahjongRulesNext";
        constexpr StringView kRulesBackAction = "MahjongRulesBack";
        constexpr StringView kRulesCloseAction = "MahjongRulesClose";
        constexpr StringView kHudValuesName = "MahjongHudValues";
        constexpr LevelIndex kViewLevel = 0;

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

        StringView KindId(SeatKind kind) {
            return kind == SeatKind::Person ? kPersonId : kComputerId;
        }

        Optional<Seat> SeatOfAction(StringView action) {
            if (action.size() != kSetSeatPrefix.size() + 1 || action.substr(0, kSetSeatPrefix.size()) != kSetSeatPrefix) {
                return std::nullopt;
            }
            const int seat = action.back() - '0';
            return IsSeat(seat) ? Optional<Seat>(seat) : std::nullopt;
        }

        Optional<Seat> SeatOfLevelAction(StringView action) {
            if (action.size() != kSetSeatLevelPrefix.size() + 1 || action.substr(0, kSetSeatLevelPrefix.size()) != kSetSeatLevelPrefix) {
                return std::nullopt;
            }
            const int seat = action.back() - '0';
            return IsSeat(seat) ? Optional<Seat>(seat) : std::nullopt;
        }

        Optional<SeatKind> KindOfValue(StringView value) {
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
        ConfigManager& configs = CoreManager::GetConfigManager();
        const PoolString name = PoolString::Intern(kHudValuesName);
        if (configs.HasConfig(name)) {
            OfferHudValues(configs.GetConfig(name).GetPugiNode());
        }
    }

    bool MahjongHudComponent::OfferHudValues(const pugi::xml_node& root) {
        HudValuesParse parse = ParseHudValues(root, kHudValuesName);
        // The context holds `_facts` at its level 0, so the document may declare nothing else
        const ContextDecl& declaration = parse.values.context;
        if (parse.Ok() && (declaration.Size() != 1 || declaration.Levels()[kViewLevel].type->Hash() != HudFacts::GetStaticTypeMeta().typeHash)) {
            parse.errors.push_back(Format("Config '{}': context: the HUD's data declares exactly one level, of the type HudFacts", kHudValuesName));
        }
        _valuesKeyRead = false;
        if (!parse.Ok()) {
            for (const String& error : parse.errors) {
                LOG_ERROR(Format("MahjongHudComponent: the HUD's data is refused, MoveLine and MatchOver stay hidden: {}", error));
            }
            _values = HudValues{};
            _context = Context();
            _valuesMask = 0;
            return false;
        }
        _values = std::move(parse.values);
        _context = Context(_values.context);
        _context.Set(kViewLevel, _facts, &_factsVersion);
        _valuesMask = ValueDependencies().Add(_values.moveLineShown).Add(_values.matchOverShown).Mask();
        return true;
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
        const String word = Tr(CoreManager::GetUIStrings(), room ? "hud.leaveRoom" : "hud.newGame");
        CoreManager::GetUIVars().Set(Ids::newGameWord, StringView(word));
    }

    String MahjongHudComponent::RoomLine(const MahjongGame& game, const UIStrings& strings) {
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
        return _noteKey.empty() ? String() : OnlineNoticeText(strings, _noteKey, _noteArg);
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
        ApplyValues();
    }

    void MahjongHudComponent::PublishNumbers(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongHud::PublishNumbers");
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        if (!started) {
            _numbersOf = nullptr;
            _numbersPublished = false;
            _roomLineSeen = false;
            SetFacts(HudFacts{});
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
        // In a room the connection or a word of the client comes first; the end screens speak after the last move, so it shows only while a hand is played
        String lastMove;
        if (game->IsOnlineTable()) {
            lastMove = RoomLine(*game, strings);
        } else {
            _roomLineSeen = false;
        }
        const bool roomLineUp = !lastMove.empty();
        if (lastMove.empty() && session.GetPhase() == SessionPhase::Playing && !hand.Moves().empty()) {
            const HandMove& move = hand.Moves().back();
            lastMove = MoveText(strings, move, hand.SeatWind(move.seat));
        }
        vars.Set(Ids::lastMove, StringView(lastMove));
        SetFacts(MakeHudFacts(match, session, roomLineUp));

        const auto plates = MakeSeatPlates(strings, session, match);
        const CenterPanelText panel = MakeCenterPanel(strings, session, match);
        vars.Set(Ids::panelRound, StringView(panel.round));
        vars.Set(Ids::panelWall, StringView(panel.wallText));
        for (int position = 0; position < kSeatCount; ++position) {
            const CenterPanelSeat& side = panel.seats[position];
            vars.Set(SeatVar("plateName", position), StringView(plates[position].name));
            vars.Set(SeatVar("panelWind", position), StringView(side.wind));
            vars.Set(SeatVar("panelScore", position), side.score);
        }
    }

    void MahjongHudComponent::SetFacts(const HudFacts& facts) {
        if (facts != _facts) {
            _facts = facts;
            ++_factsVersion;
        }
    }

    void MahjongHudComponent::ApplyValues() {
        const uint64_t key = _context.VersionKey(_valuesMask);
        if (_valuesKeyRead && key == _valuesKey) {
            return;
        }
        _valuesKey = key;
        _valuesKeyRead = true;
        ++_valueReads;
        ShowMoveLine(_values.moveLineShown.Get(_context));
        ShowMatchOver(_values.matchOverShown.Get(_context));
    }

    void MahjongHudComponent::ShowMoveLine(bool show) {
        if (show != _moveLineShown) {
            _moveLineShown = show;
            UICanvasComponent::SetElementVisibleByName(GetScene(), Ids::MoveLine, show);
        }
    }

    void MahjongHudComponent::ShowMatchOver(bool show) {
        if (show != _matchOverShown) {
            _matchOverShown = show;
            UICanvasComponent::SetVisibleByName(GetScene(), Ids::MatchOver, show);
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
        // A person's seat keeps its level, only not shown
        UIVars& vars = CoreManager::GetUIVars();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const StringView text = LevelText(_pendingLevels[seat]);
            vars.Set(SeatVar("seatLevel", seat), text);
            UICanvasComponent::SetElementVisibleByName(GetScene(), SeatVar("SeatLevel", seat), _pending[seat] == SeatKind::Computer);
        }
    }

    void MahjongHudComponent::PublishHandsChoice() const {
        // A number the panel does not offer selects no segment
        CoreManager::GetUIVars().Set(Ids::handsChoice, _pendingHands);
    }

    void MahjongHudComponent::PublishLanguage(const MahjongGame& game) const {
        CoreManager::GetUIVars().Set(Ids::language, game.GetLanguage().ToStringView());
    }

    void MahjongHudComponent::PublishSpeedChoice(const MahjongGame& game) const {
        const StringView text = SpeedText(game.GetAnimationSpeed());
        CoreManager::GetUIVars().Set(Ids::speedChoice, text);
    }

    void MahjongHudComponent::PublishThemeChoice(const MahjongGame& game) const {
        CoreManager::GetUIVars().Set(Ids::themeChoice, game.GetTheme().ToStringView());
    }

    void MahjongHudComponent::PublishSetup(const MahjongGame* game) {
        const bool open = game != nullptr && game->IsSetupOpen();
        if (open && !_setupShown) {
            _pending = game->GetSeats();
            _pendingLevels = game->GetLevels();
            PublishSeatKinds();
            _pendingHands = game->GetSettings().handsPerMatch;
            PublishHandsChoice();
            PublishLanguage(*game);
        }
        if (open != _setupShown) {
            _setupShown = open;
            UICanvasComponent::SetVisibleByName(GetScene(), Ids::Setup, open);
        }
        const bool offerContinue = open && game->CanContinue();
        if (offerContinue != _continueShown) {
            _continueShown = offerContinue;
            UICanvasComponent::SetElementVisibleByName(GetScene(), Ids::Continue, offerContinue);
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
            PublishThemeChoice(*game);
        }
        UICanvasComponent::SetVisibleByName(GetScene(), Ids::Options, open);
    }

    void MahjongHudComponent::ShowScoreLines(int count) {
        Scene& scene = GetScene();
        for (int i = std::min(count, _shownLines); i < std::max(count, _shownLines); ++i) {
            UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern(Format("ScoreLine{}", i)), i < count);
        }
        _shownLines = count;
    }

    void MahjongHudComponent::PublishWinningTiles(const Vector<Tile>& tiles) {
        Scene& scene = GetScene();
        const IntrusivePtr<UICanvasComponent> canvas = CanvasOf(scene, Ids::HandOver);
        ASSERT(canvas != nullptr, "PublishWinningTiles: the node HandOver has no canvas");
        const int count = static_cast<int>(tiles.size());
        for (int i = 0; i < kMaxWinningTiles; ++i) {
            const bool has = i < count;
            if (has) {
                UIElement* element = canvas ? canvas->GetRoot().FindByName(NumberedName("WinTile", i)) : nullptr;
                UIImageComponent* image = element != nullptr ? element->GetComponent<UIImageComponent>() : nullptr;
                ASSERT(image != nullptr, "PublishWinningTiles: the scene file has no UIImageComponent on WinTile<N> for a tile of the winning hand");
                if (image != nullptr) {
                    image->sprite = PoolString::Intern(SpriteName(tiles[static_cast<size_t>(i)]));
                }
            }
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("WinTile", i), has);
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("WinTileMark", i), has && i + 1 == count);
        }
        UICanvasComponent::SetElementVisibleByName(scene, Ids::WinTiles, count > 0);
    }

    void MahjongHudComponent::PublishHandOver(const MahjongGame& game) {
        const Match& match = game.GetMatch();
        const TableSession& session = game.GetSession();
        const OnlineClient* room = game.IsOnlineTable() ? game.FindOnline() : nullptr;
        HandSettlement settlement = SettleHand(match.Hand());
        if (room != nullptr && room->Table().result.has_value()) {
            // A display hand does not know everything the room scored with (`src/Mahjong/Online/CLAUDE.md`): the points are the server's
            settlement.scores = room->Table().result->scores;
            settlement.deltas = room->Table().result->deltas;
        }
        SeatScores totals = match.Scores();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            totals[seat] += settlement.deltas[seat];
        }
        const UIStrings& strings = CoreManager::GetUIStrings();
        const ScoreTable table = FormatScoreTable(strings, settlement, match.Hand().Result().value_or(HandResult{}), totals, SeatWho(session),
                                                  IsSeat(settlement.winner) ? &match.Hand().GetHand(settlement.winner) : nullptr);

        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::handOverHeadline, StringView(table.headline));
        vars.Set(Ids::handOverSummary, StringView(table.summary));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const ScoreRow& row = table.rows[seat];
            const StringView cells[] = {row.name, row.wind, row.hand, row.change, row.total};
            for (int column = 0; column < static_cast<int>(std::size(cells)); ++column) {
                vars.Set(PoolString::Intern(Format("handOverCell{}_{}", seat, column)), cells[column]);
            }
            UICanvasComponent::SetElementVisibleByName(GetScene(), SeatVar("WinnerRow", seat), seat == table.winner);
        }
        PublishWinningTiles(table.winningTiles);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(table.lines.size());
            vars.Set(PoolString::Intern(Format("scoreLine{}", i)), has ? StringView(table.lines[i]) : StringView());
        }
        ShowScoreLines(static_cast<int>(table.lines.size()));
        const MatchProgress after = NextProgress(MatchProgress{match.HandsPlayed(), match.East()}, settlement.kind, settlement.winner, match.GetSettings());
        vars.Set(Ids::handOverButton, StringView(Tr(strings, IsMatchOver(after.handsPlayed, match.GetSettings()) ? "handover.finish" : "handover.next")));
        // In a room how many have read the plate (the coach is off there), else the coach's tally
        String tally;
        if (room != nullptr) {
            tally = ReadyText(strings, room->Table());
            SetNextHandDisabled(!CanSendNextHand(*room));
        } else {
            const CoachLedger& coach = game.GetCoach();
            const MoveContext context = game.CoachContext();
            tally = game.CoachActive() ? FormatCoachTally(strings, coach.HandTallyOf(HandKeyOf(context)), coach.MatchTallyOf(context.startCount)) : String();
            SetNextHandDisabled(false);
        }
        vars.Set(Ids::handOverCoach, StringView(tally));
        UICanvasComponent::SetElementVisibleByName(GetScene(), Ids::HandCoachRow, !tally.empty());
    }

    String MahjongHudComponent::ReadyText(const UIStrings& strings, const OnlineTable& table) {
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
        const IntrusivePtr<UICanvasComponent> canvas = CanvasOf(GetScene(), Ids::HandOverAction);
        UIElement* button = canvas ? canvas->GetRoot().FindByName(Ids::NextHand) : nullptr;
        if (UIInputClicker* clicker = button != nullptr ? button->GetComponent<UIInputClicker>() : nullptr) {
            clicker->disabled = disabled;
        }
    }

    void MahjongHudComponent::ShowHandOver(bool shown) {
        Scene& scene = GetScene();
        UICanvasComponent::SetVisibleByName(scene, Ids::HandOver, shown);
        UICanvasComponent::SetVisibleByName(scene, Ids::HandOverAction, shown);
        if (!shown) {
            SetNextHandDisabled(false);  // a room's ready check is over with the plate
        }
        // The score table is a screen of its own (no dim, so the hands stay visible): HudMenu (New game, Options) goes while it is up.
        // The standings (MatchOver) are a Screen canvas whose dim covers the whole window, so they need nothing.
        ShowHudMenu();
    }

    void MahjongHudComponent::ShowHudMenu() {
        UICanvasComponent::SetVisibleByName(GetScene(), Ids::HudMenu, !_handOverShown && !_rulesShown);
    }

    void MahjongHudComponent::PublishRulesPage() {
        Scene& scene = GetScene();
        const UIStrings& strings = CoreManager::GetUIStrings();
        const RulesPage page = FormatRulesPage(strings, _rulesPage);
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::rulesTitle, StringView(page.title));
        vars.Set(Ids::rulesPager, StringView(RulesPagerText(strings, _rulesPage)));
        for (int i = 0; i < kRulesMaxLines; ++i) {
            const bool has = i < static_cast<int>(page.lines.size());
            vars.Set(NumberedName("rulesLine", i), has ? StringView(page.lines[static_cast<size_t>(i)]) : StringView());
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesLine", i), has);
        }
        for (int i = 0; i < kRulesMaxItems; ++i) {
            const bool has = i < static_cast<int>(page.items.size());
            vars.Set(NumberedName("rulesItem", i), has ? StringView(page.items[static_cast<size_t>(i)]) : StringView());
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesItem", i), has);
        }
        for (int row = 0; row < kRulesMaxItems / kRulesItemColumns; ++row) {
            UICanvasComponent::SetElementVisibleByName(scene, NumberedName("RulesItemRow", row), row * kRulesItemColumns < static_cast<int>(page.items.size()));
        }
        const IntrusivePtr<UICanvasComponent> rulesCanvas = CanvasOf(scene, Ids::Rules);
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
        UICanvasComponent::SetElementVisibleByName(scene, Ids::RulesBack, _rulesPage > 0);
        UICanvasComponent::SetElementVisibleByName(scene, Ids::RulesNext, _rulesPage + 1 < RulesPageCount());
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
            UICanvasComponent::SetVisibleByName(scene, Ids::Rules, open);
            UICanvasComponent::SetVisibleByName(scene, Ids::RulesAction, open);
            ShowHudMenu();
        }
    }

    void MahjongHudComponent::PublishMatchOver(const MahjongGame& game) {
        const UIStrings& strings = CoreManager::GetUIStrings();
        const StandingsTable table = FormatStandings(strings, game.GetMatch(), SeatWho(game.GetSession()));
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::matchOverHeadline, StringView(table.headline));
        for (Seat row = 0; row < kSeatCount; ++row) {
            const StandingRow& standing = table.rows[row];
            vars.Set(SeatVar("standingPlace", row), StringView(Format("{}", standing.place)));
            vars.Set(SeatVar("standingName", row), StringView(standing.name));
            vars.Set(SeatVar("standingScore", row), StringView(standing.score));
            UICanvasComponent::SetElementVisibleByName(GetScene(), SeatVar("StandingFirst", row), standing.first);
        }
        const String tally = game.CoachActive() ? FormatCoachTally(strings, CoachTally{}, game.GetCoach().MatchTallyOf(game.CoachContext().startCount)) : String();
        vars.Set(Ids::matchOverCoach, StringView(tally));
        UICanvasComponent::SetElementVisibleByName(GetScene(), Ids::MatchCoachRow, !tally.empty());
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
        if (matchOver != _facts.matchOver) {
            HudFacts facts = _facts;
            facts.matchOver = matchOver;
            SetFacts(facts);
        }
    }

    void MahjongHudComponent::OnKeyDown(const SDLEvents::KeyDownEvent& event) {
        if (event.key != SDLK_ESCAPE || event.repeat) {
            return;
        }
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr || game->IsOnlineOpen()) {
            return;  // the online panel owns Escape while it is up (`MahjongOnlineComponent`)
        }
        if (const MahjongOnlineComponent* online = MahjongOnlineComponent::GetActive(); online != nullptr && online->IsNicknameEntryOpen()) {
            return;  // so does the nickname pad over `Options`: its Escape goes back to the page
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
        // None listed (already sent, the hand moved on, the link is down) and the click is ignored
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
        const StringView action = event.action.ToStringView();
        if (action == kNewGameAction) {
            if (game->IsOnlineTable()) {
                LeaveRoom(*game);  // the end of the table, not the click, opens `Setup`
            } else {
                game->OpenSetup();
            }
            return;
        }
        if (action == kOptionsAction) {
            // Under `Setup` or behind the rules the button cannot be pressed: such an event is out of date
            if (!game->IsSetupOpen() && !game->IsRulesOpen()) {
                game->OpenOptions();
            }
            return;
        }
        if (action == kOptionsBackAction) {
            game->CloseOptions();
            return;
        }
        if (action == kRulesAction) {
            // Under `Setup`, the online panel or the rules the button cannot be pressed: such an event is out of date. Over the score table
            // and the standings it is hidden, but an event of a control client is taken: the rules open over them
            if (game->IsOptionsOpen() || !game->IsTablePaused()) {
                game->CloseOptions();
                if (!game->IsRulesOpen()) {
                    _rulesPage = 0;  // the screen opens on its first page, whatever the order of the events
                }
                game->OpenRules();
            }
            return;
        }
        if (action == kSetSpeedAction) {
            const StringView text = event.value.ToStringView();
            const Optional<AnimationSpeed> speed = TryParseSpeed(text);
            if (game->IsOptionsOpen() && speed) {
                game->SetAnimationSpeed(*speed);
                PublishSpeedChoice(*game);
            }
            return;
        }
        if (action == kSetThemeAction) {
            // An id the table does not have changes nothing, and `PublishThemeChoice` shows the theme the game really has
            if (game->IsOptionsOpen()) {
                game->SetTheme(event.value);
                PublishThemeChoice(*game);
            }
            return;
        }
        if (action == kSetLanguageAction) {
            // An id the table does not have changes nothing, and `PublishLanguage` shows the language the game really has
            if (game->IsOptionsOpen()) {
                game->SetLanguage(event.value);
                PublishLanguage(*game);
            }
            return;
        }
        if (action == kRulesNextAction || action == kRulesBackAction || action == kRulesCloseAction) {
            // A click after a control action closed the screen is out of date
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
            game->CloseSetup();
            return;
        }
        if (action == kContinueAction) {
            game->ContinueMatch();
            return;
        }
        if (action == kStartAction) {
            // The pending choices become the game's only when the match began with them; a refusal puts the old ones back
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
            // Client text is never parsed into a number the panel does not offer
            const StringView value = event.value.ToStringView();
            if (value == kFullHandsId || value == kShortHandsId) {
                _pendingHands = value == kFullHandsId ? kFullMatchHands : kShortMatchHands;
                PublishHandsChoice();
            }
            return;
        }
        if (const Optional<Seat> levelSeat = SeatOfLevelAction(action)) {
            const StringView text = event.value.ToStringView();
            if (const Optional<ComputerLevel> level = TryParseLevel(text)) {
                _pendingLevels[*levelSeat] = *level;
                PublishSeatLevels();
            }
            return;
        }
        const Optional<Seat> seat = SeatOfAction(action);
        const Optional<SeatKind> kind = KindOfValue(event.value.ToStringView());
        if (seat && kind) {
            _pending[*seat] = *kind;
            PublishSeatKinds();
        }
    }

}  // namespace BECore::Mahjong
