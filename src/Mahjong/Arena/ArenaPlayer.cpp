#include "ArenaPlayer.h"

#include <Mahjong/Game/ComputerSearch.h>

namespace BECore::Mahjong::Arena {

    namespace {

        // The binding below names every field, so it stops compiling when a field of `LevelProfile` has no row in `kProfileKnobs`.
        constexpr bool ProfileHasKnownFields() {
            [[maybe_unused]] const auto& [skipAcceptance, skipDeclaration, skipClaim, takeValue, searchWorlds, searchCandidates, searchMargin, ownDrawsDepth, ownDrawsPickWait, ownDrawsDiscardWins,
                                          ownDrawsClaimDepth, readDanger, waitingKongByPoints] = kNormalProfile;
            return true;
        }
        static_assert(ProfileHasKnownFields());

        Optional<int> ParseNumber(StringView text) {
            int value = 0;
            const char* const end = text.data() + text.size();
            const auto parsed = std::from_chars(text.data(), end, value);
            if (text.empty() || parsed.ec != std::errc() || parsed.ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        const ProfileKnob* FindKnob(StringView name) {
            for (const ProfileKnob& knob : kProfileKnobs) {
                if (name == knob.name) {
                    return &knob;
                }
            }
            return nullptr;
        }

        String KnobNamesText() {
            String names;
            for (const ProfileKnob& knob : kProfileKnobs) {
                names += names.empty() ? "" : ", ";
                names.append(knob.name.data(), knob.name.size());
            }
            return names;
        }

        String LevelAndRandomNamesText() {
            return Format("a level {}..{}, easy (1), normal ({}) or random", kMinLevel, kMaxLevel, LevelNumber(ComputerLevel::Normal));
        }

        struct OracleSpec {
            StringView name;
            OracleKind kind;
        };
        constexpr Array<OracleSpec, 4> kOracleSpecs{{
            {"oracle-acceptance", OracleKind::Acceptance},
            {"oracle-hands", OracleKind::Hands},
            {"oracle-whole", OracleKind::WholeHand},
            {"oracle-defence", OracleKind::Defence},
        }};
        constexpr StringView kOracleHeadPrefix = "oracle-";
        constexpr StringView kOracleNamesText = "oracle-acceptance, oracle-hands, oracle-whole, oracle-defence";

        /// The knobs of the search: `oracle-acceptance` plays no search, so a spec that sets one is refused.
        constexpr Array<StringView, 3> kSearchKnobNames = {{"searchWorlds", "searchCandidates", "searchMargin"}};

        /// The oracles that are a search player: their profile starts from the shipped search budget.
        bool IsSearchOracle(OracleKind kind) {
            return kind == OracleKind::Hands || kind == OracleKind::WholeHand;
        }

        const OracleSpec* FindOracle(StringView head) {
            for (const OracleSpec& oracle : kOracleSpecs) {
                if (head == oracle.name) {
                    return &oracle;
                }
            }
            return nullptr;
        }

        Optional<String> ApplyKnob(StringView part, LevelProfile& profile, Array<bool, kProfileFieldCount>& seen) {
            const size_t equals = part.find('=');
            if (equals == StringView::npos) {
                return Format("\"{}\" is no knob=value (a knob is set as name=number)", part);
            }
            const StringView name = part.substr(0, equals);
            const StringView valueText = part.substr(equals + 1);
            const ProfileKnob* const knob = FindKnob(name);
            if (knob == nullptr) {
                return Format("unknown knob \"{}\" (the knobs are {})", name, KnobNamesText());
            }
            const size_t index = static_cast<size_t>(knob - kProfileKnobs.data());
            if (seen[index]) {
                return Format("knob \"{}\" is set twice", name);
            }
            const Optional<int> value = ParseNumber(valueText);
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

    std::expected<ArenaPlayer, String> ParseArenaPlayer(StringView spec) {
        if (spec.empty()) {
            return std::unexpected(Format("empty player spec (use {}, or a level with knobs: easy:skipClaimPercent=20)", LevelAndRandomNamesText()));
        }
        const size_t colon = spec.find(':');
        const StringView head = spec.substr(0, colon);
        ArenaPlayer player;
        player.name = Format("{}", spec);
        if (head == "random") {
            if (colon != StringView::npos) {
                return std::unexpected(Format("\"random\" takes no knobs: \"{}\"", spec));
            }
            player.random = true;
            player.profile = kNormalProfile;
            return player;
        }
        if (head.starts_with(kOracleHeadPrefix)) {
            const OracleSpec* const oracle = FindOracle(head);
            if (oracle == nullptr) {
                return std::unexpected(Format("unknown oracle \"{}\" in \"{}\" (the oracles are {})", head, spec, kOracleNamesText));
            }
            player.oracle = oracle->kind;
            player.profile = kNormalProfile;
            if (IsSearchOracle(player.oracle)) {
                player.profile.searchWorlds = kSearchWorlds;
                player.profile.searchCandidates = kSearchCandidates;
                player.profile.searchMargin = kSearchMargin;
            }
        } else {
            Optional<ComputerLevel> level = TryParseLevel(head);
            if (!level.has_value()) {
                level = TryParseLevelName(head);
            }
            if (!level.has_value()) {
                return std::unexpected(Format("unknown player \"{}\" in \"{}\" (use {}, all lowercase, or an oracle: {})", head, spec, LevelAndRandomNamesText(), kOracleNamesText));
            }
            player.profile = ProfileOf(*level);
        }
        if (colon == StringView::npos) {
            return player;
        }
        const StringView knobs = spec.substr(colon + 1);
        if (knobs.empty()) {
            return std::unexpected(Format("no knobs after the colon in \"{}\"", spec));
        }
        Array<bool, kProfileFieldCount> seen{};
        size_t begin = 0;
        while (begin <= knobs.size()) {
            const size_t comma = knobs.find(',', begin);
            const size_t end = comma == StringView::npos ? knobs.size() : comma;
            if (const Optional<String> error = ApplyKnob(knobs.substr(begin, end - begin), player.profile, seen)) {
                return std::unexpected(*error);
            }
            begin = end + 1;
        }
        if (player.oracle == OracleKind::Acceptance) {
            for (const StringView searchKnob : kSearchKnobNames) {
                const ProfileKnob* const knob = FindKnob(searchKnob);
                if (knob != nullptr && seen[static_cast<size_t>(knob - kProfileKnobs.data())]) {
                    return std::unexpected(Format("\"{}\": oracle-acceptance plays no search, so \"{}\" would do nothing there (the search oracles are oracle-hands and "
                                                  "oracle-whole)",
                                                  spec, searchKnob));
                }
            }
        }
        if (IsSearchOracle(player.oracle) && (player.profile.searchWorlds < 1 || player.profile.searchCandidates < kSearchMinCandidates)) {
            return std::unexpected(Format("\"{}\" would be plain Normal: a search oracle needs searchWorlds of at least 1 and searchCandidates of at least {}", spec, kSearchMinCandidates));
        }
        return player;
    }

    HandAction ChooseSeekWin(const Vector<HandAction>& actions, MahjongRng& rng) {
        Vector<HandAction> wanted;
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
        const Vector<HandAction>& from = wanted.empty() ? actions : wanted;
        return from[rng.Below(static_cast<uint32_t>(from.size()))];
    }

}  // namespace BECore::Mahjong::Arena
