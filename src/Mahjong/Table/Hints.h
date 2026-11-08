#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * What the hints of the table say (the node `Hints` of `config/SceneConfig.xml`): which tiles the viewer's hand waits for, which
     * discards declare the calling hand, and what the computer player would do in the viewer's place. Plain C++, no scene: everything
     * is computed from the `TableView` of the viewer (`MakeTableView`), so a hint never knows more than the viewer sees. The words are
     * keys of the string table (`config/UIStrings.xml`), this file holds no English.
     */

    /// The tiles of the waiting row: the 13 orphans are the most a hand can wait for.
    inline constexpr int kMaxWaitingTiles = 13;

    /// The seed of the computer player that gives the advice: fixed, so the same view always gives the same advice and no rng of the game is touched.
    inline constexpr uint32_t kAdviceSeed = 1;

    /// The keys of the words the hint canvas shows (the scene says `{#key}`; the names are here so that code and tests can ask for them).
    inline constexpr const char* kWaitCaptionKey = "hints.waitCaption";
    inline constexpr const char* kAdviseButtonKey = "hints.adviseButton";

    /// One tile the hand waits for and the copies of it the viewer cannot see (it may be 0: a dead wait is still shown).
    struct WaitEntry {
        Tile tile;
        int unseen = 0;
    };

    /// The tiles that would complete the viewer's hand, in the order of the tile ids, each with its `UnseenCopies`. Empty unless the
    /// concealed tiles are `3n + 1` and the hand is waiting (`WinningTiles(view.own)`: the copies in the melds counted). It does not
    /// depend on whose turn it is or on the calling-hand declaration.
    [[nodiscard]] eastl::vector<WaitEntry> WaitingTiles(const TableView& view);

    /// `true` for the id of each tile whose discard declares the calling hand: the `Discard` actions of `view.legal` with
    /// `declareCalling` (exactly what the `Declare` switch of the table allows). All false when the viewer has no turn.
    [[nodiscard]] eastl::array<bool, kTileIdCount> DeclaringDiscards(const TableView& view);

    /// What the computer player would do in the viewer's place: `ComputerPlayer(kAdviceSeed).Choose(view)`. An element of `view.legal`;
    /// empty when `view.legal` is empty (nothing to do).
    [[nodiscard]] std::optional<HandAction> AdviceFor(const TableView& view);

    /// One sentence that says `advice` in the language of `strings` ("Discard 5 Dots.", "Take the discard to make a pung of 5 Dots."):
    /// one key `hints.advice.*` per kind with placeholders. `view` names the pending discard of a claim. Never empty.
    [[nodiscard]] eastl::string AdviceText(const UIStrings& strings, const TableView& view, const HandAction& advice);

    /**
     * @brief Whether the hints are on screen now: `hintsOn` (`MahjongGame::HintsOn()`), the seat choice is closed (`setupOpen` false,
     * `MahjongGame::IsSetupOpen()`), the session is `Playing`, it has a viewer who is a person (never a spectator), and no hot-seat
     * cover is up. In `HandOver` and `MatchOver` there are no hints. The one place of the rule; the components ask it every frame.
     */
    [[nodiscard]] bool HintsApply(bool hintsOn, bool setupOpen, const TableSession& session);

}  // namespace BECore::Mahjong
