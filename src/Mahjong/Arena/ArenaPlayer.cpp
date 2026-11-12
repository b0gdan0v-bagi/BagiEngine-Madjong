#include "ArenaPlayer.h"

namespace BECore::Mahjong::Arena {

    namespace {

        // A new field of `LevelProfile` needs a row of `kProfileKnobs`: the binding below names every field, so it stops compiling
        // when the count changes, and `ArenaPlayerTest` checks that each row sets its own field.
        constexpr bool ProfileHasKnownFields() {
            [[maybe_unused]] const auto& [skipAcceptance, skipDeclaration, skipClaim, takeValue, searchWorlds, searchCandidates, searchMargin, ownDrawsDepth, ownDrawsPickWait, readDanger,
                                          waitingKongByPoints] = kNormalProfile;
            return true;
        }
        static_assert(ProfileHasKnownFields());

        /// The value of `text` as a whole number (an optional minus sign), nullopt when anything else is in it.
        std::optional<int> ParseNumber(std::string_view text) {
            int value = 0;
            const char* const end = text.data() + text.size();
            const auto parsed = std::from_chars(text.data(), end, value);
            if (text.empty() || parsed.ec != std::errc() || parsed.ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        const ProfileKnob* FindKnob(std::string_view name) {
            for (const ProfileKnob& knob : kProfileKnobs) {
                if (name == std::string_view(knob.name.data(), knob.name.size())) {
                    return &knob;
                }
            }
            return nullptr;
        }

        eastl::string KnobNamesText() {
            eastl::string names;
            for (const ProfileKnob& knob : kProfileKnobs) {
                names += names.empty() ? "" : ", ";
                names.append(knob.name.data(), knob.name.size());
            }
            return names;
        }

        eastl::string LevelAndRandomNamesText() {
            return Format("a level {}..{}, easy (1), normal ({}) or random", kMinLevel, kMaxLevel, LevelNumber(ComputerLevel::Normal));
        }

        /// Applies `knob=value` (`part`) to `profile`; the error names the part.
        std::optional<eastl::string> ApplyKnob(std::string_view part, LevelProfile& profile, eastl::array<bool, kProfileFieldCount>& seen) {
            const size_t equals = part.find('=');
            if (equals == std::string_view::npos) {
                return Format("\"{}\" is no knob=value (a knob is set as name=number)", part);
            }
            const std::string_view name = part.substr(0, equals);
            const std::string_view valueText = part.substr(equals + 1);
            const ProfileKnob* const knob = FindKnob(name);
            if (knob == nullptr) {
                return Format("unknown knob \"{}\" (the knobs are {})", name, KnobNamesText());
            }
            const size_t index = static_cast<size_t>(knob - kProfileKnobs.data());
            if (seen[index]) {
                return Format("knob \"{}\" is set twice", name);
            }
            const std::optional<int> value = ParseNumber(valueText);
            if (!value.has_value()) {
                return Format("the value \"{}\" of \"{}\" is no whole number", valueText, name);
            }
            if (*value < 0 || *value > knob->maxValue) {
                return Format("the value {} of \"{}\" is outside 0..{}", valueText, name, knob->maxValue);
            }
            seen[index] = true;
            if (knob->percent != nullptr) {
                profile.*(knob->percent) = *value;
            } else {
                profile.*(knob->flag) = *value != 0;
            }
            return std::nullopt;
        }

    }  // namespace

    std::expected<ArenaPlayer, eastl::string> ParseArenaPlayer(std::string_view spec) {
        if (spec.empty()) {
            return std::unexpected(Format("empty player spec (use {}, or a level with knobs: easy:skipClaimPercent=20)", LevelAndRandomNamesText()));
        }
        const size_t colon = spec.find(':');
        const std::string_view head = spec.substr(0, colon);
        ArenaPlayer player;
        player.name = Format("{}", spec);
        if (head == "random") {
            if (colon != std::string_view::npos) {
                return std::unexpected(Format("\"random\" takes no knobs: \"{}\"", spec));
            }
            player.random = true;
            player.profile = kNormalProfile;
            return player;
        }
        std::optional<ComputerLevel> level = TryParseLevel(head);
        if (!level.has_value()) {
            level = TryParseLevelName(head);
        }
        if (!level.has_value()) {
            return std::unexpected(Format("unknown player \"{}\" in \"{}\" (use {}, all lowercase)", head, spec, LevelAndRandomNamesText()));
        }
        player.profile = ProfileOf(*level);
        if (colon == std::string_view::npos) {
            return player;
        }
        const std::string_view knobs = spec.substr(colon + 1);
        if (knobs.empty()) {
            return std::unexpected(Format("no knobs after the colon in \"{}\"", spec));
        }
        eastl::array<bool, kProfileFieldCount> seen{};
        size_t begin = 0;
        while (begin <= knobs.size()) {
            const size_t comma = knobs.find(',', begin);
            const size_t end = comma == std::string_view::npos ? knobs.size() : comma;
            if (const std::optional<eastl::string> error = ApplyKnob(knobs.substr(begin, end - begin), player.profile, seen)) {
                return std::unexpected(*error);
            }
            begin = end + 1;
        }
        return player;
    }

    HandAction ChooseSeekWin(const eastl::vector<HandAction>& actions, MahjongRng& rng) {
        eastl::vector<HandAction> wanted;
        for (const HandAction& action : actions) {
            const bool wins = action.kind == ActionKind::DeclareWin || (action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong);
            if (wins) {
                return action;
            }
            const bool declares = action.kind == ActionKind::Discard && action.declareCalling;
            const bool buildsSet = action.kind == ActionKind::Claim || action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong;
            if (declares || buildsSet) {
                wanted.push_back(action);
            }
        }
        const eastl::vector<HandAction>& from = wanted.empty() ? actions : wanted;
        return from[rng.Below(static_cast<uint32_t>(from.size()))];
    }

}  // namespace BECore::Mahjong::Arena
