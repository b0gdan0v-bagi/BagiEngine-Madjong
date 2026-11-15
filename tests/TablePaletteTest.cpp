#include <BECore/UI/UIThemes.h>
#include <Mahjong/Table/TablePalette.h>
#include <gtest/gtest.h>
#include <tests/UiStyleTestSupport.h>

// What the table paints in is read from a theme table by token: each field of `TablePalette` is the colour of the token it names.
namespace BECore::Mahjong::Tests {

    namespace {

        /// The token of each field of the palette, in the order of the struct.
        struct FieldToken {
            const char* token;
            Color TablePalette::* field;
        };

        const FieldToken kFields[] = {
            {"felt", &TablePalette::felt},
            {"feltRim", &TablePalette::feltRim},
            {"feltLip", &TablePalette::feltLip},
            {"handRack", &TablePalette::handRack},
            {"handRackLip", &TablePalette::handRackLip},
            {"hudPanel", &TablePalette::seatPlate},
            {"hudPanelDeep", &TablePalette::seatPlateDeep},
            {"plateFrame", &TablePalette::seatPlateFrame},
            {"plateShadow", &TablePalette::seatPlateShadow},
            {"plateHighlight", &TablePalette::seatPlateHighlight},
        };

        /// A table whose theme `id` holds every token of the palette in a colour made from `seed`, each field its own.
        void FillTheme(UIThemes& themes, const char* id, uint8_t seed) {
            uint8_t step = 0;
            for (const FieldToken& field : kFields) {
                const uint8_t channel = static_cast<uint8_t>(seed + 7 * step++);
                ASSERT_TRUE(themes.Set(PoolString::Intern(id), field.token, Color{channel, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(channel + 2), 255}));
            }
        }

    }  // namespace

    TEST(TablePaletteTest, EveryFieldIsTheColourOfItsTokenInTheShippedClassicTheme) {
        UIThemes themes;
        ASSERT_TRUE(LoadShippedThemes(themes).empty());

        const TablePalette palette = MakeTablePalette(themes);
        for (const FieldToken& field : kFields) {
            const Optional<Color> expected = themes.Find(field.token);
            ASSERT_TRUE(expected.has_value()) << field.token;
            EXPECT_TRUE(SameColor(palette.*field.field, *expected)) << field.token;
        }
    }

    TEST(TablePaletteTest, AnEmptyTableGivesTheLoudColourInEveryField) {
        const UIThemes themes;

        const TablePalette palette = MakeTablePalette(themes);
        for (const FieldToken& field : kFields) {
            EXPECT_TRUE(SameColor(palette.*field.field, kMissingTokenColor)) << field.token;
        }
    }

    TEST(TablePaletteTest, ATokenNoThemeHasIsTheLoudColourAndTheOthersAreKept) {
        UIThemes themes;
        FillTheme(themes, "one", 10);
        UIThemes without;  // the same colours but for the felt
        for (const FieldToken& field : kFields) {
            if (StringView(field.token) != "felt") {
                ASSERT_TRUE(without.Set(PoolString::Intern("one"), field.token, *themes.Find(field.token)));
            }
        }

        const TablePalette palette = MakeTablePalette(without);
        EXPECT_TRUE(SameColor(palette.felt, kMissingTokenColor));
        EXPECT_TRUE(SameColor(palette.feltRim, *themes.Find("feltRim")));
    }

    TEST(TablePaletteTest, ASwitchToAThemeWithOtherColoursChangesEveryField) {
        UIThemes themes;
        FillTheme(themes, "one", 10);
        FillTheme(themes, "two", 100);
        const TablePalette first = MakeTablePalette(themes);

        ASSERT_TRUE(themes.SetTheme(PoolString::Intern("two")));
        const TablePalette second = MakeTablePalette(themes);
        for (const FieldToken& field : kFields) {
            EXPECT_FALSE(SameColor(first.*field.field, second.*field.field)) << field.token;
        }
    }

}  // namespace BECore::Mahjong::Tests
