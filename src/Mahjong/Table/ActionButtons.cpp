#include "ActionButtons.h"

namespace BECore::Mahjong {

    namespace {

        /// The hint keys that more than one button shares: the three chows, the three concealed kongs.
        constexpr const char* kChowHint = "hint.ClaimChow";
        constexpr const char* kConcealedKongHint = "hint.ConcealedKong";

        constexpr eastl::array<ButtonSpec, kButtonCount> kSpecs{{
            {"Win", "MahjongWin", "", "button.Win", "hint.Win", 0},
            {"Mahjong", "MahjongClaimMahjong", "", "button.ClaimMahjong", "hint.ClaimMahjong", 1},
            {"Pung", "MahjongClaimPung", "", "button.ClaimPung", "hint.ClaimPung", 1},
            {"Kong", "MahjongClaimKong", "", "button.ClaimKong", "hint.ClaimKong", 1},
            {"Chow0", "MahjongClaimChow", "0", "button.ClaimChow0", kChowHint, kMaxButtonTiles},
            {"Chow1", "MahjongClaimChow", "1", "button.ClaimChow1", kChowHint, kMaxButtonTiles},
            {"Chow2", "MahjongClaimChow", "2", "button.ClaimChow2", kChowHint, kMaxButtonTiles},
            {"Pass", "MahjongPass", "", "button.Pass", "hint.Pass", 0},
            {"ConcealedKong0", "MahjongConcealedKong", "0", "button.ConcealedKong0", kConcealedKongHint, 1},
            {"ConcealedKong1", "MahjongConcealedKong", "1", "button.ConcealedKong1", kConcealedKongHint, 1},
            {"ConcealedKong2", "MahjongConcealedKong", "2", "button.ConcealedKong2", kConcealedKongHint, 1},
            {"UpgradeKong", "MahjongUpgradeKong", "", "button.UpgradeKong", "hint.UpgradeKong", 1},
            {"Declare", "MahjongDeclare", kDeclareOn.data(), "button.Declare", "hint.Declare", 0},
        }};

        static_assert(kChowButtons + static_cast<int>(ButtonKind::ClaimChow0) == static_cast<int>(ButtonKind::Pass));
        static_assert(kConcealedKongButtons + static_cast<int>(ButtonKind::ConcealedKong0) == static_cast<int>(ButtonKind::UpgradeKong));

        /// Shows `kind` with its action and hint and no tile yet.
        ButtonState& Show(ActionButtons& buttons, ButtonKind kind, const HandAction& action) {
            ButtonState& button = buttons[kind];
            button.shown = true;
            button.action = action;
            button.hintKey = HintKey(kind);
            return button;
        }

        /// Adds a tile to picture on the button, up to the `maxTiles` of its kind; an invalid tile ("not known") is not added.
        void AddTile(ButtonState& button, ButtonKind kind, Tile tile) {
            if (tile.IsValid() && button.tileCount < SpecOf(kind).maxTiles) {
                button.tiles[static_cast<size_t>(button.tileCount++)] = tile;
            }
        }

        ButtonKind Offset(ButtonKind first, int index) noexcept {
            return static_cast<ButtonKind>(static_cast<int>(first) + index);
        }

    }  // namespace

    const ButtonSpec& SpecOf(ButtonKind kind) noexcept {
        return kSpecs[static_cast<size_t>(kind)];
    }

    std::optional<ButtonKind> ButtonOf(eastl::string_view action, eastl::string_view value) noexcept {
        for (size_t i = 0; i < kSpecs.size(); ++i) {
            if (action == kSpecs[i].action && value == kSpecs[i].value) {
                return static_cast<ButtonKind>(i);
            }
        }
        // The toggle of the declaration emits the state it asks for: "on" while the flag is off and "off" while it is on
        if (action == SpecOf(ButtonKind::Declare).action && value == kDeclareOff) {
            return ButtonKind::Declare;
        }
        return std::nullopt;
    }

    eastl::string_view HintKey(ButtonKind kind) noexcept {
        return SpecOf(kind).hintKey;
    }

    eastl::string WordOf(const UIStrings& strings, ButtonKind kind) {
        return Tr(strings, SpecOf(kind).wordKey);
    }

    eastl::string HintOf(const UIStrings& strings, ButtonKind kind) {
        return Tr(strings, SpecOf(kind).hintKey);
    }

    ActionButtons MakeActionButtons(const eastl::vector<HandAction>& legal, std::optional<Tile> pendingTile) {
        const Tile pending = pendingTile.value_or(Tile{});
        ActionButtons buttons;
        int chows = 0;
        int concealedKongs = 0;
        for (const HandAction& action : legal) {
            switch (action.kind) {
                case ActionKind::Discard:
                    if (action.declareCalling) {
                        // The flag is set by the toggle: no action of its own, only the hint
                        buttons[ButtonKind::Declare].shown = true;
                        buttons[ButtonKind::Declare].hintKey = HintKey(ButtonKind::Declare);
                    }
                    break;
                case ActionKind::DeclareWin:
                    Show(buttons, ButtonKind::Win, action);
                    break;
                case ActionKind::ConcealedKong:
                    if (concealedKongs < kConcealedKongButtons) {
                        const ButtonKind kind = Offset(ButtonKind::ConcealedKong0, concealedKongs++);
                        AddTile(Show(buttons, kind, action), kind, action.tile);
                    }
                    break;
                case ActionKind::UpgradeKong:
                    AddTile(Show(buttons, ButtonKind::UpgradeKong, action), ButtonKind::UpgradeKong, action.tile);
                    break;
                case ActionKind::Pass:
                    Show(buttons, ButtonKind::Pass, action);
                    break;
                case ActionKind::Claim:
                    switch (action.claim) {
                        case ClaimKind::Chow:
                            if (chows < kChowButtons) {
                                const ButtonKind kind = Offset(ButtonKind::ClaimChow0, chows++);
                                ButtonState& button = Show(buttons, kind, action);
                                for (int k = 0; k < kMaxButtonTiles; ++k) {
                                    AddTile(button, kind, Tile::Suited(action.chowFirst.Suit(), action.chowFirst.Rank() + k));
                                }
                            }
                            break;
                        case ClaimKind::Pung:
                            AddTile(Show(buttons, ButtonKind::ClaimPung, action), ButtonKind::ClaimPung, pending);
                            break;
                        case ClaimKind::Kong:
                            AddTile(Show(buttons, ButtonKind::ClaimKong, action), ButtonKind::ClaimKong, pending);
                            break;
                        case ClaimKind::Mahjong:
                            AddTile(Show(buttons, ButtonKind::ClaimMahjong, action), ButtonKind::ClaimMahjong, pending);
                            break;
                    }
                    break;
            }
        }
        return buttons;
    }

}  // namespace BECore::Mahjong
