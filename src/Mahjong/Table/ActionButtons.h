#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>

namespace BECore::Mahjong {

    /**
     * The action buttons of the table (the node and canvas `Actions` of `config/SceneConfig.xml`): what the person to act can do besides
     * clicking a tile to discard it. Plain C++, no scene: `MakeActionButtons` turns `HandModel::LegalActions` into the buttons that are
     * shown, the `HandAction` each one performs, its label, the tiles to picture on it and the sentence that explains it.
     */
    CORE_ENUM(ButtonKind, uint8_t, Win, ClaimMahjong, ClaimPung, ClaimKong, ClaimChow0, ClaimChow1, ClaimChow2, Pass, ConcealedKong0, ConcealedKong1, ConcealedKong2, UpgradeKong, Declare)

    inline constexpr int kButtonCount = static_cast<int>(EnumUtils<ButtonKind>::Count());
    /// A tile has at most three sequences, so a claim window lists at most three chows.
    inline constexpr int kChowButtons = 3;
    /// Fourteen concealed tiles hold at most three sets of four equal tiles.
    inline constexpr int kConcealedKongButtons = 3;

    /// The most tiles pictured on one button: the three tiles of a chow.
    inline constexpr int kMaxButtonTiles = 3;
    /// The longest sentence of `HintOf` in characters: two lines of the hint label of the scene.
    inline constexpr size_t kMaxHintLength = 90;

    /// How the scene file names a button: its element, the action and value it emits, the `UIVars` variable that is its label (null: the
    /// label is fixed text in the file), the plain word the file shows on it and the most tiles pictured on it (3 for a chow, 1 for a
    /// claim of the pending tile and for a kong, 0 for none).
    struct ButtonSpec {
        const char* element;
        const char* action;
        const char* value;
        const char* labelVar;
        const char* word;
        int maxTiles;
    };

    [[nodiscard]] const ButtonSpec& SpecOf(ButtonKind kind) noexcept;

    /// The values of the toggle `Declare`: it emits the state it asks for (`SpecOf(Declare).value` is "on", this is the other one).
    inline constexpr eastl::string_view kDeclareOn = "on";
    inline constexpr eastl::string_view kDeclareOff = "off";

    /// The button that emits `action` with `value`, nullopt for an action that no button emits.
    [[nodiscard]] std::optional<ButtonKind> ButtonOf(eastl::string_view action, eastl::string_view value) noexcept;

    /// One sentence that tells the player what the button does (a fixed text, never empty).
    [[nodiscard]] eastl::string_view HintOf(ButtonKind kind) noexcept;

    /// One button: shown or not, the action it performs (`Declare` performs none: it only sets the flag of the next discard), its label,
    /// the tiles to picture on it (`tileCount` of `tiles`, in the order to draw; at most `SpecOf(kind).maxTiles`, fewer when the tile is
    /// not known) and its hint (`HintOf`; empty while the button is not shown).
    struct ButtonState {
        bool shown = false;
        HandAction action;
        eastl::string label;
        eastl::array<Tile, kMaxButtonTiles> tiles;
        int tileCount = 0;
        eastl::string_view hint;

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
     * sequence in the order listed ("Chow 4-6p"); a concealed kong, one button per listed tile ("Kong 5p"); the upgraded kong ("Add 5p").
     * `Win` and `Pass` as they are. The tiles to picture: the three of a chow, the `pendingTile` (the discard or the robbed kong's tile;
     * none when unknown) for the claims of mahjong, pung and kong, the tile of a concealed or upgraded kong.
     */
    [[nodiscard]] ActionButtons MakeActionButtons(const eastl::vector<HandAction>& legal, std::optional<Tile> pendingTile = std::nullopt);

}  // namespace BECore::Mahjong
