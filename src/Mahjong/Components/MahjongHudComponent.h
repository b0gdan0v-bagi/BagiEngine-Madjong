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
     *        `MahjongSetup.xml`) show what this component publishes to `CoreManager::GetUIVars()` and emit the actions it answers. What each
     *        screen holds: `src/Mahjong/Components/CLAUDE.md`, "The HUD (`MahjongHudComponent`)".
     *
     * **Variables** are set only when `TableSession::Version()`, the game or the version of `UIStrings` (the language) moved, in the current
     * language of the string table. Per screen position of the four seats (0 bottom; the seat there is the viewer's, `BottomSeat`) they are
     * the plate and panel words of `MakeCenterPanel`, and `lastMove`. They carry no tile a seat could not see (the last move is public: a
     * concealed kong names no tile).
     *
     * **Setup.** The component shows and hides the modal `Setup` every frame after `MahjongGame::IsSetupOpen()`. It changes the pending seats,
     * levels and hands and their variables; `MahjongStart` gives them to the game (`handsPerMatch` only: every other setting stays) and begins
     * a match from a random seed (`StartRandomMatch`); a start the game refuses leaves the panel open and everything as it was. A person's
     * seat keeps its pending level for when it turns computer again. `SetupClose` and Escape apply no pending choice and begin no match.
     * `MahjongContinue` is ignored while the panel is closed or `CanContinue()` is false. The language action is accepted only while
     * `Options` is open, and a language the table does not have is refused (the variable shows the language the game really has).
     *
     * **Options.** `MahjongSetSpeed` takes `slow` | `normal` | `fast` (`TryParseSpeed`); any other text, and the action while the page is
     * closed, is ignored. `MahjongSetTheme` takes a theme id of the table of themes (`MahjongGame::SetTheme`): an id it lacks, and the action
     * while the page is closed, changes nothing (the variable `themeChoice` shows the theme the game really has).
     * The level is not set here: it is a choice of each computer seat on `Setup`.
     *
     * **Saving.** Every `SceneUpdateEvent` of the active game calls `MahjongGame::FlushSave()`, so a burst of computer steps is one write.
     *
     * **The end of a hand and of a match.** `HandOver` and `HandOverAction` wait while `MahjongTableComponent::HandOverHeld()`, but their
     * variables are published at once. `MahjongNextHand` settles the hand (`TableSession::NextHand`) and deals on. Both end screens read only
     * public data (the hands are open once the hand is over) and are published when the session's or the string table's version moved.
     *
     * **A room** (`MahjongGame::IsOnlineTable()`): the plates and sentences name the players (`SeatWho`); `newGameWord`, the caption of
     * `NewGame` in `HudMenu` and `MatchOver`, is `hud.leaveRoom` and `MahjongNewGame` leaves the room (`OnlineClient::Leave`, once);
     * `MahjongNextHand` sends the server's `next_hand` (`OnlineClient::Act`) and the button is disabled while none is listed; `handOverCoach`
     * says how many have read the plate; `lastMove` is the lost connection or the client's own word while there is one.
     *
     * **The rules screen.** The page number is this component's (`CurrentRulesPage()`, 0 when the screen opens); `MahjongRulesNext` /
     * `MahjongRulesBack` turn the pages (clamped) and the three rules actions are ignored while the screen is not open. The page is set when
     * the page, the open state or the version of `UIStrings` moved; it reads and changes no state of the table.
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

        static MahjongHudComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        /// Escape closes the page `Options` while it is open, else `Setup` (the same as `MahjongSetupClose`).
        void OnKeyDown(const SDLEvents::KeyDownEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void PublishNumbers(const MahjongGame* game);
        /// Sets `newGameWord`, the caption of the HUD button and of the standings' button: "New game", in a room "Leave room".
        void PublishNewGameWord(const MahjongGame* game);
        /// What the line under the discards says in a room instead of the last move: the lost connection, else the client's latest word while the table has not moved; empty if neither.
        String RoomLine(const MahjongGame& game, const UIStrings& strings);
        /// "Ready for the next hand: 2 of 4"; empty while nobody has sent `next_hand`.
        static String ReadyText(const UIStrings& strings, const OnlineTable& table);
        /// The client is in the game and the server lists `next_hand` for it now.
        static bool CanSendNextHand(const OnlineClient& client);
        /// Disables (or enables) the button `NextHand`: in a room after the own `next_hand`, while the link is down.
        void SetNextHandDisabled(bool disabled);
        void LeaveRoom(MahjongGame& game);
        void SendNextHand(MahjongGame& game);
        /// Takes the setup of the game into the pending choices when the panel opens.
        void PublishSetup(const MahjongGame* game);
        void PublishOptions(const MahjongGame* game);
        void ShowMoveLine(bool show);
        void PublishSeatKinds();
        /// `SeatLevel<n>` is shown exactly for the pending computer seats.
        void PublishSeatLevels();
        void PublishHandsChoice() const;
        void PublishLanguage(const MahjongGame& game) const;
        void PublishSpeedChoice(const MahjongGame& game) const;
        void PublishThemeChoice(const MahjongGame& game) const;
        void PublishEndScreens(const MahjongGame* game);
        void PublishHandOver(const MahjongGame& game);
        void PublishWinningTiles(const Vector<Tile>& tiles);
        void PublishMatchOver(const MahjongGame& game);
        void ShowHandOver(bool shown);
        void ShowScoreLines(int count);
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
        StringView _noteKey;  // the word of the client that came while the table is up, and the version of the table it came at
        String _noteArg;
        uint64_t _noteTable = 0;
        bool _leaveSent = false;
        static MahjongHudComponent* s_active;
    };

}  // namespace BECore::Mahjong
