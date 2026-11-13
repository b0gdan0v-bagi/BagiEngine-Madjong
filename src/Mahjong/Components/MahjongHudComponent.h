#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Online/OnlineClient.h>

namespace BECore {

    class UIStrings;

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SDLEvents {
        struct KeyDownEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The words and numbers of the HUD, and the seat choice of a new game: the canvases `Hud` and `Setup` (`config/nodes/MahjongHud.xml`,
     *        `MahjongSetup.xml`) show what this component publishes to `CoreManager::GetUIVars()` and emit the actions it answers.
     *
     * **Variables** (set when `TableSession::Version()` moved, or the game changed, or the version of `UIStrings` moved, i.e. the language;
     * nothing is set again otherwise; every word is in the current language of the string table, `config/UIStrings.xml`): per screen position of the four seats (0 bottom, 1 right, 2 top,
     * 3 left; the seat there is the viewer's, `BottomSeat`) `plateName` (`Seat 2`, with ` You` for the person when exactly one person plays, the player's name in a room; the node `SeatPlates`),
     * `panelWind` (the wind letter) and `panelScore`; and `panelRound` ("East 1": the round wind and the hand in it) and `panelWall` ("Wall 83"), all of `MakeCenterPanel` (the labels of the node
     * `CenterPanel`; the table paints the plates, the panel's ground, the turn and the dealer, `MahjongTableComponent`; the plates also change with the viewer, which moves `Version()`), and
     * `lastMove` (the last accepted move of the hand in words, `MoveText` of `HandModel::Moves().back()`; empty before the first move and once the hand is over; the element `MoveLine` is visible
     * exactly while it is not empty). They carry no tile a seat could not see (the last move is public: a concealed kong names no tile).
     *
     * **Setup.** `MahjongGame::IsSetupOpen()` decides whether the modal canvas `Setup` is shown (this component shows and hides it every
     * frame). Its four seat `UISegmented` groups are bound to `seatKind0` .. `seatKind3` (`person` | `computer`) and emit
     * `MahjongSetSeat0` .. `MahjongSetSeat3` with the value; the fifth, `Hands`, is bound to `handsChoice` (`16` | `8`) and emits
     * `MahjongSetHands` with the value. Each seat row also has a `UIDropdown` `SeatLevel0` .. `3` of `1` .. `10`, bound to `seatLevel0` ..
     * `3` (the number as text; set when `Setup` opens, from `GetLevels()`) and emitting `MahjongSetSeatLevel0` .. `3` with the number
     * (`TryParseLevel`: any other text is ignored); the element is visible exactly while that seat is a computer in the pending setup, and a
     * person's seat keeps its pending level for when it turns computer again. This component changes the pending `SeatSetup`, the pending
     * `SeatLevels` or the pending number of hands and the variable, and `MahjongStart` gives the pending seats, levels and number of hands (`handsPerMatch` only: every other setting stays as the game
     * has it) to the game and begins a match with a random seed (`StartRandomMatch`), which closes the panel; a start the game refuses
     * leaves it open and the seats, levels and settings as they were. The cross `SetupClose` (`MahjongSetupClose`) closes the panel with
     * none of the pending choices applied and no match begun, and Escape does the same on the keyboard;
     * either way the player is where they were (the table behind the panel). `MahjongNewGame` (the HUD button) opens it again, with the pending setup of the
     * last match. The language row of the page `Options` is a
     * `UISegmented` bound to `language` (the id of the language) and emits `MahjongSetLanguage` with the id, which is accepted only while the page is open;
     * `MahjongGame::SetLanguage` applies it to the string table (an id the table does not have is refused and
     * the variable is set to the language the game really has), and the labels and the variables follow at the next draw. Nothing is
     * played while it is open (`MahjongTableComponent` takes no click and steps no computer). The button `Continue` (`MahjongContinue`, beside `Start` in
     * the stack `ButtonRow`) is shown exactly while the panel is open and `MahjongGame::CanContinue()` (a saved match is the table behind it);
     * `MahjongContinue` calls `ContinueMatch()` and is ignored while the panel is closed or there is nothing to continue.
     *
     * **Options.** `MahjongGame::IsOptionsOpen()` is the page over the table (paused meanwhile). `MahjongOptions` (the HUD button) opens
     * it while neither `Setup` nor the rules are open; `MahjongOptionsBack` and Escape close it; `MahjongRules` (the HUD button `Rules`, and `OptionsRules`
     * on the page) closes it and opens the rules, and is ignored under `Setup`, the online panel and the rules.
     * `MahjongSetSpeed` with `slow` | `normal` | `fast` (`TryParseSpeed`; any other text is ignored, and so is the action while the page is closed)
     * calls `SetAnimationSpeed` and sets `speedChoice`; it takes effect at once (`Table/AnimationSpeed.h`). The canvas `Options` is
     * shown exactly while the page is open (rows: language, hints, speed, and the buttons `Rules` and `Back`); `language` and `speedChoice` are published when it opens. The level is not set here: it
     * is a choice of each computer seat on `Setup`.
     *
     * **Saving.** Every `SceneUpdateEvent` (once per frame, only for the active game) calls `MahjongGame::FlushSave()`, which writes the match and the
     * choices of `Setup` when they changed, so a burst of computer steps is one write.
     *
     * **The end of a hand and of a match.** While the session is in `HandOver` the modal canvas `HandOver` is shown with the rows of
     * `FormatScoreTable` (`Table/ScoreText.h`): `handOverHeadline`, `handOverCell<r>_<c>` (seat row 0..3, column 0..4: name, wind, hand, change, total), `scoreLine0` .. `15` (the element
     * `ScoreLineN` is visible exactly when there is a line N; `WinnerRow<seat>` exactly for the winner's row; the pictures `WinTile<N>` of the winning hand with `WinTileMark<N>` on the last, in the
     * row `WinTiles`, none for a drawn hand), `handOverSummary` and `handOverButton` (`handover.next`, `handover.finish` on the last hand of the match, resolved, on the button `NextHand` of
     * `HandOverAction`, in the middle of the table, in the gap of the plate, shown and hidden with the score table, while `HudMenu` is hidden; the two wait while
     * `MahjongTableComponent::HandOverHeld()`, i.e. while the table shows a person's claim that lost to a mahjong, for `kClaimLostSeconds`; the variables are published at once); `MahjongNextHand`
     * settles the hand (`TableSession::NextHand`) and deals on. While the match is finished the canvas `MatchOver` is shown with `matchOverHeadline` and `standing0` .. `3` (`FormatStandings`); its
     * button `MahjongNewGame` opens `Setup`. In Coach mode, once the match tally has a move, each also has `handOverCoach` / `matchOverCoach`
     * (`FormatCoachTally`; the labels `HandCoachRow` / `MatchCoachRow` are visible exactly while it has text). Both are published when the version of the session or of the string table moved and read
     * only public data (the hands are open once the hand is over).
     *
     * **A room** (`MahjongGame::IsOnlineTable()`, `Components/CLAUDE.md`): the plates and sentences name the players (`SeatWho`); `newGameWord` (the caption of `NewGame` of `HudMenu` and `MatchOver`)
     * is `hud.leaveRoom` and `MahjongNewGame` leaves the room (`OnlineClient::Leave`, once); `MahjongNextHand` sends the server's `next_hand` (`OnlineClient::Act`) and the button is disabled while
     * none is listed, `handOverCoach` says how many have read the plate; `lastMove` is the lost connection or the client's own word while there is one.
     *
     * **The rules screen.** `MahjongGame::IsRulesOpen()` (the buttons `Rules` of the HUD and of the page `Options`, action `MahjongRules`) decides whether the modal canvas
     * `Rules` (text and tile pictures only) and its action bar `RulesAction` are shown; `HudMenu` is hidden meanwhile. The page number is
     * this component's (`CurrentRulesPage()`, 0 when the screen opens); `MahjongRulesNext` / `MahjongRulesBack` turn the pages (clamped) and
     * `MahjongRulesClose` closes the screen, the last three ignored while it is not open. The page is `FormatRulesPage`
     * (`Table/RulesText.h`): `rulesTitle`, `rulesPager`, `rulesLine0` .. `5`, `rulesItem0` .. `11` and the pictured tiles `RulesTile0` ..
     * (`kRulesMaxRowTiles` per row); the elements `RulesLineN`, `RulesItemRowR`, `RulesItemN`, `RulesTileRowR`, `RulesTileN` are visible exactly
     * when the page has them, `RulesBack` is hidden on the first page and `RulesNext` on the last. It is set when the page, the open state or
     * the version of `UIStrings` moved (a language switch rewords the open page); it reads and changes no state of the table.
     */
    class MahjongHudComponent : public IComponent {
        BE_CLASS(MahjongHudComponent)
    public:
        MahjongHudComponent() = default;
        ~MahjongHudComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The page of the rules the screen shows (0 while the rules are closed).
        [[nodiscard]] int CurrentRulesPage() const noexcept {
            return _rulesPage;
        }

        /// The seats the panel shows and `MahjongStart` would begin a match with.
        [[nodiscard]] const SeatSetup& PendingSeats() const noexcept {
            return _pending;
        }

        /// The level of each seat the panel shows and `MahjongStart` would begin a match with (a person's seat keeps its entry).
        [[nodiscard]] const SeatLevels& PendingLevels() const noexcept {
            return _pendingLevels;
        }

        /// The number of hands the panel shows and `MahjongStart` would begin a match with.
        [[nodiscard]] int PendingHands() const noexcept {
            return _pendingHands;
        }

        /// The attached HUD, if any; the game assumes a single one.
        static MahjongHudComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        /// Escape closes the page `Options` while it is open, else `Setup` (the same as `MahjongSetupClose`).
        void OnKeyDown(const SDLEvents::KeyDownEvent& event);
        /// The frame hook of the save: writes what changed since the last frame.
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        /// Sets the variables of the HUD when the version of the session (or the game) moved since the last time.
        void PublishNumbers(const MahjongGame* game);
        /// Sets `newGameWord`, the caption of the HUD button and of the standings' button: "New game", in a room "Leave room".
        void PublishNewGameWord(const MahjongGame* game);
        /// What the line under the discards says in a room instead of the last move: the lost connection, else the client's latest word while the table has not moved; empty if neither.
        eastl::string RoomLine(const MahjongGame& game, const UIStrings& strings);
        /// "Ready for the next hand: 2 of 4"; empty while nobody has sent `next_hand`.
        static eastl::string ReadyText(const UIStrings& strings, const OnlineTable& table);
        /// The client is in the game and the server lists `next_hand` for it now.
        static bool CanSendNextHand(const OnlineClient& client);
        /// Disables (or enables) the button `NextHand`: in a room after the own `next_hand`, while the link is down.
        void SetNextHandDisabled(bool disabled);
        /// The button of the HUD in a room: leaves it (the end of the table opens `Setup`).
        void LeaveRoom(MahjongGame& game);
        /// `MahjongNextHand` in a room: the server's `next_hand` through the client.
        void SendNextHand(MahjongGame& game);
        /// Shows or hides `Setup` after the game, and takes the setup of the game when the panel opens.
        void PublishSetup(const MahjongGame* game);
        /// Shows or hides `Options` after `MahjongGame::IsOptionsOpen()`, and publishes `language` and `speedChoice` when it opens.
        void PublishOptions(const MahjongGame* game);
        /// Shows or hides the plate `MoveLine` (the variable `lastMove` holds its text).
        void ShowMoveLine(bool show);
        /// Sets `seatKind0` .. `3` and, through `PublishSeatLevels`, the level variables and dropdowns after the pending seats.
        void PublishSeatKinds();
        /// Sets `seatLevel0` .. `3` (the number as text) and shows `SeatLevel0` .. `3` exactly for the pending computer seats.
        void PublishSeatLevels();
        void PublishHandsChoice() const;
        /// Sets the variable `language` to the language of the game.
        void PublishLanguage(const MahjongGame& game) const;
        /// Sets the variable `speedChoice` to the id of the speed of the game (`SpeedText`).
        void PublishSpeedChoice(const MahjongGame& game) const;
        /// Shows or hides `HandOver` and `MatchOver` after the session, and sets their variables when the session moved.
        void PublishEndScreens(const MahjongGame* game);
        void PublishHandOver(const MahjongGame& game);
        /// Sets the pictures `WinTile0` .. of the winning hand and shows exactly those (the row `WinTiles` with them), the mark of the winning tile on the last.
        void PublishWinningTiles(const eastl::vector<Tile>& tiles);
        void PublishMatchOver(const MahjongGame& game);
        /// Shows or hides the score table `HandOver` with its button `HandOverAction`; `HudMenu` (New game) is hidden while it is shown.
        void ShowHandOver(bool shown);
        /// Makes exactly the first `count` line labels of the score table visible.
        void ShowScoreLines(int count);
        /// Shows or hides the rules screen (`Rules` and its bar `RulesAction`) after `MahjongGame::IsRulesOpen()`, and puts the page into
        /// variables and elements when the page, the open state or the language changed.
        void PublishRules(const MahjongGame* game);
        void PublishRulesPage();
        /// `HudMenu` (New game, Options) is shown exactly while neither the score table nor the rules are up.
        void ShowHudMenu();

        SeatSetup _pending = kDefaultSeatSetup;
        SeatLevels _pendingLevels = UniformLevels(kDefaultLevel);
        int _pendingHands = kFullMatchHands;
        bool _setupShown = false;
        bool _optionsShown = false;                // the canvas `Options`
        bool _continueShown = false;               // the button `Continue` of the panel
        const TableSession* _numbersOf = nullptr;  // the session (the local one or a room's display copy) and the version the variables show
        uint64_t _numbersVersion = 0;
        uint64_t _numbersStrings = 0;  // the version of the string table the variables are worded in
        bool _numbersPublished = false;
        bool _moveLineShown = false;
        bool _handOverShown = false;
        bool _matchOverShown = false;
        int _shownLines = 0;                         // the labels `ScoreLine0` .. that are visible
        const TableSession* _endScreenOf = nullptr;  // the session and the version the variables of the end screens show
        uint64_t _endScreenVersion = 0;
        uint64_t _endScreenStrings = 0;
        bool _endScreenCoach = false;  // the mode the tally line was published for
        bool _endScreenPublished = false;
        int _rulesPage = 0;  // the page the rules show; 0 whenever they are closed
        bool _rulesShown = false;
        int _rulesPublishedPage = -1;  // the page and the language the elements show
        uint64_t _rulesStrings = 0;
        bool _newGameWordPublished = false;  // `newGameWord` is for this state: a room or not, this language
        bool _newGameWordRoom = false;
        uint64_t _newGameWordStrings = 0;
        bool _roomLineSeen = false;  // the word of the client at the table's start is not new
        uint32_t _noticeSeen = 0;
        eastl::string_view _noteKey;  // the word of the client that came while the table is up, and the version of the table it came at
        eastl::string _noteArg;
        uint64_t _noteTable = 0;
        bool _leaveSent = false;
        static MahjongHudComponent* s_active;
    };

}  // namespace BECore::Mahjong
