#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * The action buttons of the table (the node and canvas `Actions`): what the person to act can do besides clicking a tile to discard
     * it. Plain C++, no scene: `MakeActionButtons` turns `HandModel::LegalActions` into the shown buttons, the `HandAction` each performs,
     * the tiles pictured on it and the key of its hint. The words are keys of `config/UIStrings.xml`: this file holds no English.
     */
    CORE_ENUM(ButtonKind, uint8_t, Win, ClaimMahjong, ClaimPung, ClaimKong, ClaimChow0, ClaimChow1, ClaimChow2, Pass, ConcealedKong0, ConcealedKong1, ConcealedKong2, UpgradeKong, Declare)

    inline constexpr int kButtonCount = static_cast<int>(EnumUtils<ButtonKind>::Count());
    /// A tile has at most three sequences, so a claim window lists at most three chows.
    inline constexpr int kChowButtons = 3;
    /// Fourteen concealed tiles hold at most three sets of four equal tiles.
    inline constexpr int kConcealedKongButtons = 3;

    /// The most tiles pictured on one button: the three tiles of a chow.
    inline constexpr int kMaxButtonTiles = 3;
    /// The longest sentence of `HintOf` in characters (code points, in any language of the table): two lines of the hint label of the scene.
    inline constexpr size_t kMaxHintLength = 90;

    /// How the scene file names a button: its element, the action and value it emits, the key of the word the file shows on it
    /// (`button.<ButtonKind>`), the key of its hint (`hint.<ButtonKind>`; the three chows share `hint.ClaimChow`, the three concealed
    /// kongs `hint.ConcealedKong`) and the most tiles pictured on it. The keys are literals that live as long as the program.
    struct ButtonSpec {
        const char* element;
        const char* action;
        const char* value;
        const char* wordKey;
        const char* hintKey;
        int maxTiles;
    };

    [[nodiscard]] const ButtonSpec& SpecOf(ButtonKind kind) noexcept;

    /// The name of the element with a `UIImageComponent` that pictures tile `t` (0..`maxTiles` - 1) on the button, as the scene file has
    /// it ("Chow1Tile2").
    [[nodiscard]] inline String TileImageName(ButtonKind kind, int t) {
        return Format("{}Tile{}", SpecOf(kind).element, t);
    }

    /// The values of the toggle `Declare`: it emits the state it asks for (`SpecOf(Declare).value` is "on", this is the other one).
    inline constexpr StringView kDeclareOn = "on";
    inline constexpr StringView kDeclareOff = "off";

    /// The button that emits `action` with `value`, nullopt for an action that no button emits.
    [[nodiscard]] Optional<ButtonKind> ButtonOf(StringView action, StringView value) noexcept;

    /// `SpecOf(kind).hintKey`, never empty.
    [[nodiscard]] StringView HintKey(ButtonKind kind) noexcept;

    [[nodiscard]] String WordOf(const UIStrings& strings, ButtonKind kind);

    /// The sentence that tells the player what the button does (never empty).
    [[nodiscard]] String HintOf(const UIStrings& strings, ButtonKind kind);

    /// One button. `Declare` performs no action: it only sets the flag of the next discard. `tiles` has fewer than `maxTiles` when a
    /// tile is not known; `hintKey` is empty while the button is not shown. The state holds no language, so it stays equal by value when
    /// the language changes.
    struct ButtonState {
        bool shown = false;
        HandAction action;
        Array<Tile, kMaxButtonTiles> tiles;
        int tileCount = 0;
        StringView hintKey;

        bool operator==(const ButtonState&) const = default;
    };

    struct ActionButtons {
        Array<ButtonState, kButtonCount> buttons;

        [[nodiscard]] const ButtonState& operator[](ButtonKind kind) const noexcept {
            return buttons[static_cast<size_t>(kind)];
        }
        [[nodiscard]] ButtonState& operator[](ButtonKind kind) noexcept {
            return buttons[static_cast<size_t>(kind)];
        }

        bool operator==(const ActionButtons&) const = default;
    };

    /**
     * The buttons for the actions a seat can take now (`HandModel::LegalActions`). A discard is no button (the tile is clicked);
     * `Declare` is shown when some listed discard declares the calling hand. One `Chow` button per listed sequence and one concealed
     * kong button per listed tile, in the order listed. Pictured tiles: the three of a chow, the `pendingTile` (the discard or the
     * robbed kong's tile; none when unknown) for a claim of mahjong, pung or kong, the tile of a concealed or upgraded kong.
     */
    [[nodiscard]] ActionButtons MakeActionButtons(const Vector<HandAction>& legal, Optional<Tile> pendingTile = std::nullopt);

    /// The tiles of the hand a chow takes: the two of its sequence besides the pending discard.
    struct ChowTakes {
        Array<Tile, 2> tiles;
        int count = 0;
    };

    /**
     * The tiles of the hand to mark while chows are offered: those of the `hovered` chow button, else of the one chow on offer; none when
     * several are offered and none is hovered. The sequence is the three tiles pictured on the button less the `pending` discard (the
     * first equal one).
     */
    [[nodiscard]] ChowTakes ChowTilesTaken(const ActionButtons& buttons, Tile pending, Optional<ButtonKind> hovered);

}  // namespace BECore::Mahjong
