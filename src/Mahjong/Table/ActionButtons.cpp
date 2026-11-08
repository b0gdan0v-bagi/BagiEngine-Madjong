#include "ActionButtons.h"

namespace BECore::Mahjong {

    namespace {

        /// The sentences of the hint label, one per kind (the chows, the concealed kongs share theirs).
        constexpr const char* kChowHint = "Take the discard to complete a run of three in a row (only from the player before you).";
        constexpr const char* kConcealedKongHint = "Set four equal tiles from your hand aside and draw a spare tile.";

        constexpr eastl::array<ButtonSpec, kButtonCount> kSpecs{{
            {"Win", "MahjongWin", "", nullptr, "Mahjong!", "Your hand is complete: win the hand.", 0},
            {"Mahjong", "MahjongClaimMahjong", "", nullptr, "Mahjong!", "The tile completes your hand: take it and win.", 1},
            {"Pung", "MahjongClaimPung", "", nullptr, "Pung", "Take the discard to make a set of three equal tiles.", 1},
            {"Kong", "MahjongClaimKong", "", nullptr, "Kong", "Take the discard to make a set of four equal tiles; you then draw a spare tile.", 1},
            {"Chow0", "MahjongClaimChow", "0", "chow0", "Chow", kChowHint, kMaxButtonTiles},
            {"Chow1", "MahjongClaimChow", "1", "chow1", "Chow", kChowHint, kMaxButtonTiles},
            {"Chow2", "MahjongClaimChow", "2", "chow2", "Chow", kChowHint, kMaxButtonTiles},
            {"Pass", "MahjongPass", "", nullptr, "Pass", "Let the discard go.", 0},
            {"ConcealedKong0", "MahjongConcealedKong", "0", "kong0", "Kong", kConcealedKongHint, 1},
            {"ConcealedKong1", "MahjongConcealedKong", "1", "kong1", "Kong", kConcealedKongHint, 1},
            {"ConcealedKong2", "MahjongConcealedKong", "2", "kong2", "Kong", kConcealedKongHint, 1},
            {"UpgradeKong", "MahjongUpgradeKong", "", "upgrade", "Add to pung", "Add the fourth tile to your open pung to make a kong; you draw a spare tile.", 1},
            {"Declare", "MahjongDeclare", kDeclareOn.data(), nullptr, "Ready hand", "Announce you are one tile from mahjong (needed to win), then click the tile to discard.", 0},
        }};

        static_assert(kChowButtons + static_cast<int>(ButtonKind::ClaimChow0) == static_cast<int>(ButtonKind::Pass));
        static_assert(kConcealedKongButtons + static_cast<int>(ButtonKind::ConcealedKong0) == static_cast<int>(ButtonKind::UpgradeKong));

        /// Shows `kind` with its action, label and hint and no tile yet.
        ButtonState& Show(ActionButtons& buttons, ButtonKind kind, const HandAction& action, eastl::string label = {}) {
            ButtonState& button = buttons[kind];
            button.shown = true;
            button.action = action;
            button.label = eastl::move(label);
            button.hint = HintOf(kind);
            return button;
        }

        /// Adds a tile to picture on the button, up to the `maxTiles` of its kind; an invalid tile ("not known") is not added.
        void AddTile(ButtonState& button, ButtonKind kind, Tile tile) {
            if (tile.IsValid() && button.tileCount < SpecOf(kind).maxTiles) {
                button.tiles[static_cast<size_t>(button.tileCount++)] = tile;
            }
        }

        /// "4-6p" for the chow that starts at 4p.
        eastl::string ChowLabel(Tile first) {
            const std::string_view start = ToString(first);
            const std::string_view end = ToString(Tile::Suited(first.Suit(), first.Rank() + 2));
            return Format("Chow {}-{}", start.substr(0, 1), end);
        }

        eastl::string TileLabel(const char* word, Tile tile) {
            return Format("{} {}", word, ToString(tile));
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

    eastl::string_view HintOf(ButtonKind kind) noexcept {
        return SpecOf(kind).hint;
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
                        buttons[ButtonKind::Declare].hint = HintOf(ButtonKind::Declare);
                    }
                    break;
                case ActionKind::DeclareWin:
                    Show(buttons, ButtonKind::Win, action);
                    break;
                case ActionKind::ConcealedKong:
                    if (concealedKongs < kConcealedKongButtons) {
                        const ButtonKind kind = Offset(ButtonKind::ConcealedKong0, concealedKongs++);
                        AddTile(Show(buttons, kind, action, TileLabel("Kong", action.tile)), kind, action.tile);
                    }
                    break;
                case ActionKind::UpgradeKong:
                    AddTile(Show(buttons, ButtonKind::UpgradeKong, action, TileLabel("Add", action.tile)), ButtonKind::UpgradeKong, action.tile);
                    break;
                case ActionKind::Pass:
                    Show(buttons, ButtonKind::Pass, action);
                    break;
                case ActionKind::Claim:
                    switch (action.claim) {
                        case ClaimKind::Chow:
                            if (chows < kChowButtons) {
                                const ButtonKind kind = Offset(ButtonKind::ClaimChow0, chows++);
                                ButtonState& button = Show(buttons, kind, action, ChowLabel(action.chowFirst));
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
