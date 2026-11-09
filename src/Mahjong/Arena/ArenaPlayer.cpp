#include "ArenaPlayer.h"

namespace BECore::Mahjong::Arena {

    namespace {

        /// A number as text (`eastl::to_string` needs a library the arena does not link).
        eastl::string NumberText(long long value) {
            const std::string text = std::to_string(value);
            return eastl::string(text.data(), text.size());
        }

        // A new field of `LevelProfile` needs a row of `kProfileKnobs`: the binding below names every field, so it stops compiling
        // when the count changes, and `ArenaPlayerTest` checks that each row sets its own field.
        constexpr bool ProfileHasFourFields() {
            [[maybe_unused]] const auto& [skipAcceptance, skipDeclaration, skipClaim, takeValue] = kNormalProfile;
            return true;
        }
        static_assert(ProfileHasFourFields());
        static_assert(kProfileKnobs.size() == 4, "one row of kProfileKnobs per field of LevelProfile");

        eastl::string Text(std::string_view text) {
            return eastl::string(text.data(), text.size());
        }

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
            eastl::string names = LevelNamesText();
            names += " or random";
            return names;
        }

        /// Applies `knob=value` (`part`) to `profile`; the error names the part.
        std::optional<eastl::string> ApplyKnob(std::string_view part, LevelProfile& profile, eastl::array<bool, kProfileKnobs.size()>& seen) {
            const size_t equals = part.find('=');
            if (equals == std::string_view::npos) {
                return "\"" + Text(part) + "\" is no knob=value (a knob is set as name=number)";
            }
            const std::string_view name = part.substr(0, equals);
            const std::string_view valueText = part.substr(equals + 1);
            const ProfileKnob* const knob = FindKnob(name);
            if (knob == nullptr) {
                return "unknown knob \"" + Text(name) + "\" (the knobs are " + KnobNamesText() + ")";
            }
            const size_t index = static_cast<size_t>(knob - kProfileKnobs.data());
            if (seen[index]) {
                return "knob \"" + Text(name) + "\" is set twice";
            }
            const std::optional<int> value = ParseNumber(valueText);
            if (!value.has_value()) {
                return "the value \"" + Text(valueText) + "\" of \"" + Text(name) + "\" is no whole number";
            }
            if (*value < 0 || *value > knob->maxValue) {
                return "the value " + Text(valueText) + " of \"" + Text(name) + "\" is outside 0.." + NumberText(knob->maxValue);
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
            return std::unexpected("empty player spec (use " + LevelAndRandomNamesText() + ", or a level with knobs: easy:skipClaimPercent=20)");
        }
        const size_t colon = spec.find(':');
        const std::string_view head = spec.substr(0, colon);
        ArenaPlayer player;
        player.name = Text(spec);
        if (head == "random") {
            if (colon != std::string_view::npos) {
                return std::unexpected("\"random\" takes no knobs: \"" + Text(spec) + "\"");
            }
            player.random = true;
            player.profile = kNormalProfile;
            return player;
        }
        const std::optional<ComputerLevel> level = TryParseLevel(head);
        if (!level.has_value()) {
            return std::unexpected("unknown player \"" + Text(head) + "\" in \"" + Text(spec) + "\" (use " + LevelAndRandomNamesText() + ", all lowercase)");
        }
        player.profile = ProfileOf(*level);
        if (colon == std::string_view::npos) {
            return player;
        }
        const std::string_view knobs = spec.substr(colon + 1);
        if (knobs.empty()) {
            return std::unexpected("no knobs after the colon in \"" + Text(spec) + "\"");
        }
        eastl::array<bool, kProfileKnobs.size()> seen{};
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
