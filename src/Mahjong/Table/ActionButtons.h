#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>

namespace BECore::Mahjong {

    /**
     * The action buttons of the table (the node and canvas `Actions` of `config/SceneConfig.xml`): what the person to act can do besides
     * clicking a tile to discard it. Plain C++, no scene: `MakeActionButtons` turns `HandModel::LegalActions` into the buttons that are
     * shown, the `HandAction` each one performs and its label.
     */
    CORE_ENUM(ButtonKind, uint8_t, Win, ClaimMahjong, ClaimPung, ClaimKong, ClaimChow0, ClaimChow1, ClaimChow2, Pass, ConcealedKong0, ConcealedKong1, ConcealedKong2, UpgradeKong, Declare)

    inline constexpr int kButtonCount = static_cast<int>(EnumUtils<ButtonKind>::Count());
    /// A tile has at most three sequences, so a claim window lists at most three chows.
    inline constexpr int kChowButtons = 3;
    /// Fourteen concealed tiles hold at most three sets of four equal tiles.
    inline constexpr int kConcealedKongButtons = 3;

    /// How the scene file names a button: its element, the action and value it emits and the `UIVars` variable that is its label (null:
    /// the label is fixed text in the file).
    struct ButtonSpec {
        const char* element;
        const char* action;
        const char* value;
        const char* labelVar;
    };

    [[nodiscard]] const ButtonSpec& SpecOf(ButtonKind kind) noexcept;

    /// The button that emits `action` with `value`, nullopt for an action that no button emits.
    [[nodiscard]] std::optional<ButtonKind> ButtonOf(eastl::string_view action, eastl::string_view value) noexcept;

    /// One button: shown or not, the action it performs (`Declare` performs none: it only sets the flag of the next discard) and its label.
    struct ButtonState {
        bool shown = false;
        HandAction action;
        eastl::string label;

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
     * `Win` and `Pass` as they are.
     */
    [[nodiscard]] ActionButtons MakeActionButtons(const eastl::vector<HandAction>& legal);

}  // namespace BECore::Mahjong
