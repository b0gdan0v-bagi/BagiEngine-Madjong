#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * The action buttons of the table (the node and canvas `Actions`, `config/nodes/MahjongActions.xml`): what the person to act can do besides
     * clicking a tile to discard it. Plain C++, no scene: `MakeActionButtons` turns `HandModel::LegalActions` into the buttons that are
     * shown, the `HandAction` each one performs, the tiles to picture on it and the key of the sentence that explains it. The words themselves
     * (the label of a button, its hint) are keys of the string table (`config/UIStrings.xml`): this file holds no English.
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

    /// How the scene file names a button: its element, the action and value it emits, the key of the plain word the file shows on it
    /// (`text="{#button.Win}"`; `button.<ButtonKind>`), the key of the sentence that explains it (`HintOf`; `hint.<ButtonKind>`, the three
    /// chows share `hint.ClaimChow` and the three concealed kongs `hint.ConcealedKong`) and the most tiles pictured on it (3 for a chow,
    /// 1 for a claim of the pending tile and for a kong, 0 for none). The keys are literals that live as long as the program.
    struct ButtonSpec {
        const char* element;
        const char* action;
        const char* value;
        const char* wordKey;
        const char* hintKey;
        int maxTiles;
    };

    [[nodiscard]] const ButtonSpec& SpecOf(ButtonKind kind) noexcept;

    /// The name of the `UIImage` that pictures tile `t` (0..`maxTiles` - 1) on the button, as the scene file has it: the element of
    /// the button, `Tile` and the number ("Chow1Tile2").
    [[nodiscard]] inline eastl::string TileImageName(ButtonKind kind, int t) {
        return Format("{}Tile{}", SpecOf(kind).element, t);
    }

    /// The values of the toggle `Declare`: it emits the state it asks for (`SpecOf(Declare).value` is "on", this is the other one).
    inline constexpr eastl::string_view kDeclareOn = "on";
    inline constexpr eastl::string_view kDeclareOff = "off";

    /// The button that emits `action` with `value`, nullopt for an action that no button emits.
    [[nodiscard]] std::optional<ButtonKind> ButtonOf(eastl::string_view action, eastl::string_view value) noexcept;

    /// The key of the sentence that tells the player what the button does (`SpecOf(kind).hintKey`, never empty).
    [[nodiscard]] eastl::string_view HintKey(ButtonKind kind) noexcept;

    /// The word on the button in the current language of `strings`.
    [[nodiscard]] eastl::string WordOf(const UIStrings& strings, ButtonKind kind);

    /// The sentence that tells the player what the button does, in the current language of `strings` (never empty).
    [[nodiscard]] eastl::string HintOf(const UIStrings& strings, ButtonKind kind);

    /// One button: shown or not, the action it performs (`Declare` performs none: it only sets the flag of the next discard), the
    /// tiles to picture on it (`tileCount` of `tiles`, in the order to draw; at most `SpecOf(kind).maxTiles`, fewer when the tile is
    /// not known) and the key of its hint (`HintKey`; empty while the button is not shown). The state holds no language, so it stays
    /// equal by value when the language changes.
    struct ButtonState {
        bool shown = false;
        HandAction action;
        eastl::array<Tile, kMaxButtonTiles> tiles;
        int tileCount = 0;
        eastl::string_view hintKey;

        bool operator==(const ButtonState&) const = default;
    };

    struct ActionButtons {
        eastl::array<ButtonState, kButtonCount> buttons;

        [[nodiscard]] const ButtonState& operator[](ButtonKind kind) const noexcept {
            return buttons[static_cast<size_t>(kind)];
        }
        [[nodiscard]] ButtonState& operator[](ButtonKind kind) noexcept {
            return buttons[static_cast<size_t>(kind)];
        }

        bool operator==(const ActionButtons&) const = default;
    };

    /**
     * The buttons for the actions a seat can take now (`HandModel::LegalActions`). A discard is no button (the tile is clicked); `Declare`
     * is shown when some listed discard declares the calling hand. Claims: `Mahjong`, `Pung`, `Kong`, one `Chow` button per listed
     * sequence in the order listed; a concealed kong, one button per listed tile; the upgraded kong.
     * `Win` and `Pass` as they are. The tiles to picture: the three of a chow, the `pendingTile` (the discard or the robbed kong's tile;
     * none when unknown) for the claims of mahjong, pung and kong, the tile of a concealed or upgraded kong.
     */
    [[nodiscard]] ActionButtons MakeActionButtons(const eastl::vector<HandAction>& legal, std::optional<Tile> pendingTile = std::nullopt);

}  // namespace BECore::Mahjong
