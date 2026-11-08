#include "ActionButtons.h"

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::array<ButtonSpec, kButtonCount> kSpecs{{
            {"Win", "MahjongWin", "", nullptr},
            {"Mahjong", "MahjongClaimMahjong", "", nullptr},
            {"Pung", "MahjongClaimPung", "", nullptr},
            {"Kong", "MahjongClaimKong", "", nullptr},
            {"Chow0", "MahjongClaimChow", "0", "chow0"},
            {"Chow1", "MahjongClaimChow", "1", "chow1"},
            {"Chow2", "MahjongClaimChow", "2", "chow2"},
            {"Pass", "MahjongPass", "", nullptr},
            {"ConcealedKong0", "MahjongConcealedKong", "0", "kong0"},
            {"ConcealedKong1", "MahjongConcealedKong", "1", "kong1"},
            {"ConcealedKong2", "MahjongConcealedKong", "2", "kong2"},
            {"UpgradeKong", "MahjongUpgradeKong", "", "upgrade"},
            {"Declare", "MahjongDeclare", "on", nullptr},
        }};

        static_assert(kChowButtons + static_cast<int>(ButtonKind::ClaimChow0) == static_cast<int>(ButtonKind::Pass));
        static_assert(kConcealedKongButtons + static_cast<int>(ButtonKind::ConcealedKong0) == static_cast<int>(ButtonKind::UpgradeKong));

        void Show(ActionButtons& buttons, ButtonKind kind, const HandAction& action, eastl::string label = {}) {
            ButtonState& button = buttons[kind];
            button.shown = true;
            button.action = action;
            button.label = eastl::move(label);
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
        return std::nullopt;
    }

    ActionButtons MakeActionButtons(const eastl::vector<HandAction>& legal) {
        ActionButtons buttons;
        int chows = 0;
        int concealedKongs = 0;
        for (const HandAction& action : legal) {
            switch (action.kind) {
                case ActionKind::Discard:
                    if (action.declareCalling) {
                        buttons[ButtonKind::Declare].shown = true;
                    }
                    break;
                case ActionKind::DeclareWin:
                    Show(buttons, ButtonKind::Win, action);
                    break;
                case ActionKind::ConcealedKong:
                    if (concealedKongs < kConcealedKongButtons) {
                        Show(buttons, Offset(ButtonKind::ConcealedKong0, concealedKongs++), action, TileLabel("Kong", action.tile));
                    }
                    break;
                case ActionKind::UpgradeKong:
                    Show(buttons, ButtonKind::UpgradeKong, action, TileLabel("Add", action.tile));
                    break;
                case ActionKind::Pass:
                    Show(buttons, ButtonKind::Pass, action);
                    break;
                case ActionKind::Claim:
                    switch (action.claim) {
                        case ClaimKind::Chow:
                            if (chows < kChowButtons) {
                                Show(buttons, Offset(ButtonKind::ClaimChow0, chows++), action, ChowLabel(action.chowFirst));
                            }
                            break;
                        case ClaimKind::Pung:
                            Show(buttons, ButtonKind::ClaimPung, action);
                            break;
                        case ClaimKind::Kong:
                            Show(buttons, ButtonKind::ClaimKong, action);
                            break;
                        case ClaimKind::Mahjong:
                            Show(buttons, ButtonKind::ClaimMahjong, action);
                            break;
                    }
                    break;
            }
        }
        return buttons;
    }

}  // namespace BECore::Mahjong
