#include <BECore/UI/UIThemes.h>
#include <Common/Printers.h>
#include <Common/UIThemeTestSupport.h>
#include <Mahjong/Table/UiPalette.h>
#include <cmath>
#include <gtest/gtest.h>
#include <tests/ShippedThemes.h>
#include <tests/UiStyleTestSupport.h>

// The shipped `config/UIThemes.xml`: whole and legal, with a token for every role of `UiPalette.h`.
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kClassic = "classic";

        /// The themes the game ships: a theme taken out of the file fails here by name.
        constexpr Array<const char*, 2> kShippedThemeIds{{"classic", "navy"}};

        /// The least Euclidean RGB distance (0..441) between a mark laid over the felt, or over its rim, and the felt itself. The weakest mark of `classic` (`kTurnMark`, a thin
        /// gold over the green) is about 93 from the felt and 106 from the rim, so 60 leaves a margin and still finds a felt that has drifted to the mark's colour.
        constexpr float kMinMarkDistance = 60.0f;

        /// WCAG AA for normal text.
        constexpr float kMinTextContrast = 4.5f;

        struct Mark {
            const char* name;
            Color color;
        };

        /// Every mark of the table that lies over the felt, the rim or the plates (`UiPalette.h`): the gold family, the wind bars, the danger marks and the dealer's red.
        Vector<Mark> TableMarks() {
            return {{"kTurnMark", kTurnMark},
                    {"kPendingFrame", kPendingFrame},
                    {"kLastDiscardMark", kLastDiscardMark},
                    {"kDrawnTileMark", kDrawnTileMark},
                    {"kClaimedEdge", kClaimedEdge},
                    {"kClaimHighlight", kClaimHighlight},
                    {"kClaimLostFrame", kClaimLostFrame},
                    {"kCallingFrame", kCallingFrame},
                    {"kKeepsWaitingMark", kKeepsWaitingMark},
                    {"kSeatWindMark", kSeatWindMark},
                    {"kRoundWindMark", kRoundWindMark},
                    {"kSafeMark", kSafeMark},
                    {"kRiskyMark", kRiskyMark},
                    {"kDangerMark", kDangerMark},
                    {"kDeclareMark", kDeclareMark},
                    {"kAdvisedFrame", kAdvisedFrame},
                    {"kSeatTurnFrame", kSeatTurnFrame},
                    {"kDealerMark", kDealerMark},
                    {"kPanelTurnLight", kPanelTurnLight},
                    {"kPanelDealerMark", kPanelDealerMark}};
        }

        /// `top` laid over the opaque `bottom`, as an opaque colour.
        Color Over(const Color& top, const Color& bottom) {
            const auto channel = [&](unsigned char a, unsigned char b) {
                const float alpha = static_cast<float>(top.a) / 255.0f;
                return static_cast<unsigned char>(std::lround(static_cast<float>(a) * alpha + static_cast<float>(b) * (1.0f - alpha)));
            };
            return Color(channel(top.r, bottom.r), channel(top.g, bottom.g), channel(top.b, bottom.b), 255);
        }

        float Distance(const Color& a, const Color& b) {
            const float dr = static_cast<float>(a.r) - static_cast<float>(b.r);
            const float dg = static_cast<float>(a.g) - static_cast<float>(b.g);
            const float db = static_cast<float>(a.b) - static_cast<float>(b.b);
            return std::sqrt(dr * dr + dg * dg + db * db);
        }

        float Luminance(const Color& color) {
            const auto linear = [](unsigned char channel) {
                const float value = static_cast<float>(channel) / 255.0f;
                return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
        }

        /// The WCAG contrast ratio of two colours, 1 (the same) to 21 (black on white).
        float ContrastRatio(const Color& a, const Color& b) {
            const float first = Luminance(a);
            const float second = Luminance(b);
            return (std::max(first, second) + 0.05f) / (std::min(first, second) + 0.05f);
        }

        /// The colour of a token in the current theme; a token nobody defines is `kMissingTokenColor`, which the completeness check reports.
        Color Token(const UIThemes& themes, const char* name) {
            return themes.Find(name).value_or(kMissingTokenColor);
        }

        /// A pair of `classic` that was never held to the contrast ratio and falls under it. `classic` is not changed here (the owner decides); the pair is named so that it
        /// is found again if it is ever fixed, and no other theme has an exception.
        struct KnownPair {
            const char* theme;
            const char* pair;
        };
        constexpr Array<KnownPair, 2> kKnownLowContrastPairs{{
            {"classic", "textMuted on primary"},     // 3.49
            {"classic", "textMuted on placeBadge"},  // 4.16
        }};

        bool IsKnownLowContrastPair(const UIThemes& themes, const String& pair) {
            const String theme(themes.GetTheme().ToStringView());
            return std::any_of(kKnownLowContrastPairs.begin(), kKnownLowContrastPairs.end(), [&](const KnownPair& known) { return theme == known.theme && pair == known.pair; });
        }

        /// The plates a text sits on, by the name of the role; a role with a `Deep` end (the bottom of its gradient) is judged at both ends, and the translucent HUD panel
        /// over the felt of the theme.
        struct Plate {
            String name;
            Color color;
        };

        Vector<Plate> TextPlates(const UIThemes& themes) {
            static constexpr Array<const char*, 8> kWithDeepEnd{{"surface", "surfaceRaised", "header", "hudPanel", "primary", "secondary", "neutral", "danger"}};
            static constexpr Array<const char*, 2> kSingle{{"winnerRow", "placeBadge"}};
            const Color felt = Token(themes, "felt");
            const auto laidOn = [&](const char* role) {
                const Color color = Token(themes, role);
                return String(role).rfind("hudPanel", 0) == 0 ? Over(color, felt) : color;
            };
            Vector<Plate> plates;
            for (const char* role : kWithDeepEnd) {
                plates.push_back({String(role), laidOn(role)});
                const String deep = String(role) + "Deep";
                plates.push_back({deep, laidOn(deep.c_str())});
            }
            for (const char* role : kSingle) {
                plates.push_back({String(role), laidOn(role)});
            }
            return plates;
        }

        /// One line per pair of the current theme where a text role is under `kMinTextContrast` on a plate it sits on (and is no known pair): `text` and `textMuted` on every
        /// plate, `accent` on the plates of a modal and of the HUD.
        Vector<String> TextProblems(const UIThemes& themes) {
            Vector<String> problems;
            const auto judge = [&](const char* role, const Plate& plate) {
                const String pair = String(role) + " on " + plate.name;
                const float ratio = ContrastRatio(Token(themes, role), plate.color);
                if (ratio < kMinTextContrast && !IsKnownLowContrastPair(themes, pair)) {
                    problems.push_back(Format("theme '{}': {} has the contrast {:.2f}, under {:.1f}", themes.GetTheme().ToStringView(), pair, ratio, kMinTextContrast));
                }
            };
            for (const Plate& plate : TextPlates(themes)) {
                judge("text", plate);
                judge("textMuted", plate);
                if (plate.name == "surface" || plate.name == "hudPanel") {
                    judge("accent", plate);
                }
            }
            return problems;
        }

        /// One line per mark that is too near the felt or its rim, laid over each of them.
        Vector<String> MarkProblems(const UIThemes& themes) {
            Vector<String> problems;
            for (const char* ground : {"felt", "feltRim"}) {
                const Color base = Token(themes, ground);
                for (const Mark& mark : TableMarks()) {
                    const float distance = Distance(Over(mark.color, base), base);
                    if (distance < kMinMarkDistance) {
                        problems.push_back(Format("theme '{}': {} over {} is only {:.0f} from it, under {:.0f}", themes.GetTheme().ToStringView(), mark.name, ground, distance, kMinMarkDistance));
                    }
                }
            }
            return problems;
        }

        /// One line per colour the table lays its tiles on that is not opaque.
        Vector<String> OpacityProblems(const UIThemes& themes) {
            Vector<String> problems;
            for (const char* role : {"felt", "feltRim", "feltLip", "handRack", "handRackLip"}) {
                if (Token(themes, role).a != 255) {
                    problems.push_back(Format("theme '{}': {} is not opaque", themes.GetTheme().ToStringView(), role));
                }
            }
            return problems;
        }

        /// A table with one theme `broken` that is `source` (the current theme of it) but for `token`.
        UIThemes BrokenCopy(const UIThemes& source, const char* token, const Color& value) {
            UIThemes copy;
            const PoolString id = PoolString::Intern("broken");
            for (const PoolString name : source.GetTokens()) {
                copy.Set(id, name.ToStringView(), source.Find(name.ToStringView()).value_or(kMissingTokenColor));
            }
            copy.Set(id, token, value);
            copy.SetTheme(id);
            return copy;
        }

        String Joined(const Vector<String>& lines) {
            String text;
            for (const String& line : lines) {
                text += line + "\n";
            }
            return text;
        }

        Set<String> RoleNames() {
            Set<String> roles;
            for (const char* name : kUiPaletteNames) {
                roles.insert(String(name));
            }
            for (const char* name : kTablePaletteNames) {
                roles.insert(String(name));
            }
            return roles;
        }

        class MahjongThemesTest : public ::testing::Test {
        protected:
            void SetUp() override {
                problems = LoadShippedThemes(themes);
            }

            UIThemes themes;
            Vector<String> problems;
        };

    }  // namespace

    TEST_F(MahjongThemesTest, TheShippedFileLoadsWithNoProblem) {
        for (const String& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
        EXPECT_FALSE(themes.GetThemes().empty());
    }

    TEST_F(MahjongThemesTest, TheDefaultThemeIsClassic) {
        EXPECT_EQ(themes.GetDefaultTheme(), PoolString::Intern(kClassic));
        EXPECT_EQ(themes.GetTheme(), PoolString::Intern(kClassic));
        EXPECT_TRUE(themes.HasTheme(PoolString::Intern(kClassic)));
        ASSERT_FALSE(themes.GetThemes().empty());
        EXPECT_EQ(themes.GetThemes().front(), PoolString::Intern(kClassic)) << "the first theme of the file is the one a new player sees";
    }

    TEST_F(MahjongThemesTest, EveryShippedThemeIsInTheFile) {
        for (const char* id : kShippedThemeIds) {
            EXPECT_TRUE(themes.HasTheme(PoolString::Intern(id))) << id;
        }
        EXPECT_EQ(themes.GetThemes().size(), kShippedThemeIds.size()) << "a theme of the file is missing from kShippedThemeIds";
    }

    TEST_F(MahjongThemesTest, TheTokensAreTheRolesOfThePaletteAndOfTheTableExactly) {
        const Set<String> roles = RoleNames();
        EXPECT_EQ(roles.size(), static_cast<size_t>(kUiPaletteSize + kTablePaletteSize)) << "a role is named twice";

        Set<String> tokens;
        for (const PoolString token : themes.GetTokens()) {
            tokens.insert(String(token.ToStringView()));
        }
        for (const String& role : roles) {
            EXPECT_TRUE(tokens.count(role) > 0) << "the role '" << role.c_str() << "' has no token in config/UIThemes.xml";
        }
        for (const String& token : tokens) {
            EXPECT_TRUE(roles.count(token) > 0) << "the token '" << token.c_str() << "' is no role of UiPalette.h";
        }
    }

    TEST_F(MahjongThemesTest, EveryRoleIsALegalTokenName) {
        for (const String& role : RoleNames()) {
            EXPECT_TRUE(UIThemes::IsValidToken(role)) << role.c_str();
        }
    }

    TEST_F(MahjongThemesTest, EveryThemeDefinesEveryToken) {
        ::BECore::Tests::ExpectThemesComplete(themes);
    }

    TEST_F(MahjongThemesTest, TheColoursTheTilesLieOnAreOpaqueInEveryTheme) {
        ::BECore::Tests::ForEachTheme(themes, [&](PoolString) {
            const Vector<String> problems = OpacityProblems(themes);
            EXPECT_TRUE(problems.empty()) << Joined(problems).c_str();
        });
    }

    TEST_F(MahjongThemesTest, TheMarksOfTheTableStandApartFromTheFeltInEveryTheme) {
        ::BECore::Tests::ForEachTheme(themes, [&](PoolString) {
            const Vector<String> problems = MarkProblems(themes);
            EXPECT_TRUE(problems.empty()) << Joined(problems).c_str();
        });
    }

    TEST_F(MahjongThemesTest, TheTextRolesAreLegibleOnTheirPlatesInEveryTheme) {
        ::BECore::Tests::ForEachTheme(themes, [&](PoolString) {
            const Vector<String> problems = TextProblems(themes);
            EXPECT_TRUE(problems.empty()) << Joined(problems).c_str();
        });
    }

    TEST_F(MahjongThemesTest, OnlyClassicHasPairsUnderTheContrastRatioAndEachStillFalls) {
        for (const KnownPair& known : kKnownLowContrastPairs) {
            EXPECT_STREQ(known.theme, kClassic) << "an exception for '" << known.pair << "' outside classic: fix the colour of that theme instead";
        }
        // A pair on the table that passes now has been fixed: it leaves the table.
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern(kClassic)));
        for (const KnownPair& known : kKnownLowContrastPairs) {
            const String pair = known.pair;
            const size_t on = pair.find(" on ");
            ASSERT_NE(on, String::npos) << known.pair;
            const String text = pair.substr(0, on);
            const String plate = pair.substr(on + 4);
            bool found = false;
            for (const Plate& candidate : TextPlates(themes)) {
                if (candidate.name == plate) {
                    found = true;
                    EXPECT_LT(ContrastRatio(Token(themes, text.c_str()), candidate.color), kMinTextContrast) << known.pair << " is no longer under the ratio: remove it from the table";
                }
            }
            EXPECT_TRUE(found) << known.pair;
        }
    }

    TEST_F(MahjongThemesTest, TheChecksFindABrokenTheme) {
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern("navy")));
        const Color plate = Token(themes, "surface");

        const UIThemes text = BrokenCopy(themes, "text", plate);
        EXPECT_FALSE(TextProblems(text).empty()) << "a text colour equal to its plate";

        const UIThemes muted = BrokenCopy(themes, "textMuted", Token(themes, "primary"));
        EXPECT_FALSE(TextProblems(muted).empty()) << "a muted text equal to a button";

        EXPECT_TRUE(MarkProblems(BrokenCopy(themes, "felt", Token(themes, "felt"))).empty()) << "the unchanged copy is sound";
        EXPECT_TRUE(TextProblems(BrokenCopy(themes, "felt", Token(themes, "felt"))).empty());
        EXPECT_FALSE(MarkProblems(BrokenCopy(themes, "felt", Color(kDeclareMark.r, kDeclareMark.g, kDeclareMark.b, 255))).empty()) << "a felt equal to a mark";

        EXPECT_FALSE(OpacityProblems(BrokenCopy(themes, "feltRim", Color(8, 16, 36, 200))).empty());
    }

    TEST_F(MahjongThemesTest, PaletteColorReadsTheClassicTheme) {
        const Optional<Color> surface = themes.Find("surface");
        ASSERT_TRUE(surface.has_value());
        EXPECT_TRUE(SameColor(PaletteColor("surface"), *surface));
    }

}  // namespace BECore::Mahjong::Tests
