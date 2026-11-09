#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The words and numbers of the HUD, and the seat choice of a new game: the canvases `Hud` and `Setup` of
     *        `config/SceneConfig.xml` show what this component publishes to `CoreManager::GetUIVars()` and emit the actions it answers.
     *
     * **Variables** (set when `TableSession::Version()` moved, or the game changed, or the version of `UIStrings` moved, i.e. the language;
     * nothing is set again otherwise; every word is in the current language of the string table, `config/UIStrings.xml`): `round` (the name
     * of the round wind), `hand` (the number of the hand in progress, at most `handsTotal`), `handsTotal`, `wall` (live tiles left), `turn` (the seat
     * whose turn it is, never the seat that is asked to claim: that would tell a person that this computer holds a claim; `hud.handOver`
     * and `hud.matchOver` when the hand or the match is over), and per seat `seatName` (`Seat 2 (South)`, with ` You` for the person when
     * exactly one person plays), `seatWind` (the wind's name), `seatScore`, `seatMark` (`>` on the seat whose turn it is) and `seatDealer` (`*` on East),
     * and `lastMove` (the last accepted move of the hand in words, `MoveText` of `HandModel::Moves().back()`; empty before the first move and
     * once the hand is over; the element `MoveLine` is visible exactly while it is not empty). They carry no tile a seat could not see
     * (the last move is public: a concealed kong names no tile).
     *
     * **Setup.** `MahjongGame::IsSetupOpen()` decides whether the modal canvas `Setup` is shown (this component shows and hides it every
     * frame). Its four seat `UISegmented` groups are bound to `seatKind0` .. `seatKind3` (`person` | `computer`) and emit
     * `MahjongSetSeat0` .. `MahjongSetSeat3` with the value; the fifth, `Hands`, is bound to `handsChoice` (`16` | `8`) and emits
     * `MahjongSetHands` with the value. This component changes the pending `SeatSetup` or the pending number of hands and the variable,
     * and `MahjongStart` gives the pending seats and the number of hands (`handsPerMatch` only: every other setting stays as the game
     * has it) to the game and begins a match with a random seed (`StartRandomMatch`), which closes the panel; a start the game refuses
     * leaves it open and the seats and settings as they were. `MahjongNewGame` (the HUD button) opens it again, with the pending setup of the
     * last match. The first row of the panel is the language: a `UISegmented` bound to `language` (the id of the language) emits
     * `MahjongSetLanguage` with the id; `MahjongGame::SetLanguage` applies it to the string table (an id the table does not have is refused and
     * the variable is set to the language the game really has), and the labels and the variables follow at the next draw. Nothing is
     * played while it is open (`MahjongTableComponent` takes no click and steps no computer).
     *
     * **The end of a hand and of a match.** While the session is in `HandOver` the modal canvas `HandOver` is shown with the rows of
     * `FormatScoreTable` (`Table/ScoreText.h`): `handOverHeadline`, `handOverSeat0` .. `3`, `scoreLine0` .. `15` (the element
     * `ScoreLineN` is visible exactly when there is a line N), `handOverSummary` and `handOverButton` (`handover.next`, `handover.finish` on
     * the last hand of the match, resolved, on the button in the action bar `HandOverAction`, shown and hidden with the score table, while
     * the New game button of `HudMenu` is hidden); `MahjongNextHand` settles the hand (`TableSession::NextHand`) and deals on. While the match is
     * finished the canvas `MatchOver` is shown with `matchOverHeadline` and `standing0` .. `3` (`FormatStandings`); its button
     * `MahjongNewGame` opens `Setup`. Both are published when the version of the session or of the string table moved and read only public data (the hands
     * are open once the hand is over).
     *
     * **The rules screen.** `MahjongGame::IsRulesOpen()` (the HUD button `Rules`, action `MahjongRules`) decides whether the modal canvas
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
        /// Sets the variables of the HUD when the version of the session (or the game) moved since the last time.
        void PublishNumbers(const MahjongGame* game);
        /// Shows or hides `Setup` after the game, and takes the setup of the game when the panel opens.
        void PublishSetup(const MahjongGame* game);
        /// Shows or hides the plate `MoveLine` (the variable `lastMove` holds its text).
        void ShowMoveLine(bool show);
        void PublishSeatKinds() const;
        void PublishHandsChoice() const;
        /// Sets the variable `language` to the language of the game.
        void PublishLanguage(const MahjongGame& game) const;
        /// Shows or hides `HandOver` and `MatchOver` after the session, and sets their variables when the session moved.
        void PublishEndScreens(const MahjongGame* game);
        void PublishHandOver(const MahjongGame& game);
        void PublishMatchOver(const MahjongGame& game);
        /// Shows or hides the score table `HandOver` with its action bar `HandOverAction`; `HudMenu` (New game) is hidden while it is shown.
        void ShowHandOver(bool shown);
        /// Makes exactly the first `count` line labels of the score table visible.
        void ShowScoreLines(int count);
        /// Shows or hides the rules screen (`Rules` and its bar `RulesAction`) after `MahjongGame::IsRulesOpen()`, and puts the page into
        /// variables and elements when the page, the open state or the language changed.
        void PublishRules(const MahjongGame* game);
        void PublishRulesPage();
        /// `HudMenu` (New game, Rules) is shown exactly while neither the score table nor the rules are up.
        void ShowHudMenu();

        SeatSetup _pending = kDefaultSeatSetup;
        int _pendingHands = kFullMatchHands;
        bool _setupShown = false;
        const MahjongGame* _numbersOf = nullptr;  // the game and the version the variables show
        uint64_t _numbersVersion = 0;
        uint64_t _numbersStrings = 0;  // the version of the string table the variables are worded in
        bool _numbersPublished = false;
        bool _moveLineShown = false;
        bool _handOverShown = false;
        bool _matchOverShown = false;
        int _shownLines = 0;                        // the labels `ScoreLine0` .. that are visible
        const MahjongGame* _endScreenOf = nullptr;  // the game and the version the variables of the end screens show
        uint64_t _endScreenVersion = 0;
        uint64_t _endScreenStrings = 0;
        bool _endScreenPublished = false;
        int _rulesPage = 0;  // the page the rules show; 0 whenever they are closed
        bool _rulesShown = false;
        int _rulesPublishedPage = -1;  // the page and the language the elements show
        uint64_t _rulesStrings = 0;
        static MahjongHudComponent* s_active;
    };

}  // namespace BECore::Mahjong
