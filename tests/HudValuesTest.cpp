#include <Mahjong/Table/HudValues.h>
#include <gtest/gtest.h>
#include <tests/ShippedHudValues.h>

namespace BECore::Mahjong::Tests {

    namespace {

        constexpr StringView kDocumentName = "HudValuesTestFile";
        constexpr LevelIndex kViewLevel = 0;

        bool Has(const String& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != String::npos;
        }

        /// All the errors of a parse in one text, for the failure message and for `Has`
        String AllErrors(const HudValuesParse& parse) {
            String text;
            for (const String& error : parse.errors) {
                text += error;
                text += '\n';
            }
            return text;
        }

        /// A document with the given children under the root element
        HudValuesParse ParseText(StringView children) {
            pugi::xml_document document;
            const String xml = Format("<MahjongHudValues>{}</MahjongHudValues>", children);
            EXPECT_TRUE(document.load_string(xml.c_str())) << xml.c_str();
            return ParseHudValues(document.document_element(), kDocumentName);
        }

    }  // namespace

    TEST(HudValuesTest, TheShippedFileLoadsWithNoErrorAndBothValuesReadTheViewLevel) {
        const HudValuesParse parse = ReadShippedHudValues();
        EXPECT_TRUE(parse.Ok()) << AllErrors(parse).c_str();
        ASSERT_EQ(parse.values.context.Size(), 1U);
        EXPECT_EQ(parse.values.context.Levels()[kViewLevel].name, "view");
        for (const Value<bool>* value : {&parse.values.moveLineShown, &parse.values.matchOverShown}) {
            EXPECT_TRUE(value->IsSlot());
            EXPECT_EQ(value->Reads(), LevelBit(kViewLevel));
        }
        EXPECT_EQ(ValueDependencies().Add(parse.values.moveLineShown).Add(parse.values.matchOverShown).Mask(), LevelBit(kViewLevel));
    }

    TEST(HudValuesTest, TheValuesReadTheFactsWhereTheyAre) {
        const HudValuesParse parse = ReadShippedHudValues();
        ASSERT_TRUE(parse.Ok());
        HudFacts facts;
        Context context(parse.values.context);
        context.Set(kViewLevel, facts);
        EXPECT_FALSE(parse.values.moveLineShown.Get(context));
        EXPECT_FALSE(parse.values.matchOverShown.Get(context));
        facts.hasLastMove = true;
        EXPECT_TRUE(parse.values.moveLineShown.Get(context));
        EXPECT_FALSE(parse.values.matchOverShown.Get(context));
        facts.hasLastMove = false;
        facts.matchOver = true;
        EXPECT_FALSE(parse.values.moveLineShown.Get(context));
        EXPECT_TRUE(parse.values.matchOverShown.Get(context));
    }

    TEST(HudValuesTest, ADocumentThatNamesNoValueLeavesBothFalseConstants) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts"/>)");
        EXPECT_TRUE(parse.Ok()) << AllErrors(parse).c_str();
        EXPECT_TRUE(parse.values.moveLineShown.IsConst());
        EXPECT_FALSE(parse.values.moveLineShown.GetConst());
        EXPECT_TRUE(parse.values.matchOverShown.IsConst());
        EXPECT_FALSE(parse.values.matchOverShown.GetConst());
    }

    TEST(HudValuesTest, AMistypedFieldNamesTheDocumentTheValueAndThePath) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts"/><moveLineShown from="view.hasLastMov"/>)");
        ASSERT_FALSE(parse.Ok());
        const String text = AllErrors(parse);
        EXPECT_TRUE(Has(text, kDocumentName)) << text.c_str();
        EXPECT_TRUE(Has(text, "moveLineShown")) << text.c_str();
        EXPECT_TRUE(Has(text, "view.hasLastMov")) << text.c_str();
        EXPECT_TRUE(parse.values.moveLineShown.IsConst());
    }

    TEST(HudValuesTest, AnUnknownLevelNamesTheDocumentTheValueAndThePath) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts"/><matchOverShown from="seat.hasLastMove"/>)");
        ASSERT_FALSE(parse.Ok());
        const String text = AllErrors(parse);
        EXPECT_TRUE(Has(text, kDocumentName)) << text.c_str();
        EXPECT_TRUE(Has(text, "matchOverShown")) << text.c_str();
        EXPECT_TRUE(Has(text, "seat.hasLastMove")) << text.c_str();
    }

    TEST(HudValuesTest, AFieldOfAnotherTypeIsRefused) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts" prefs="MahjongPrefsSave"/><moveLineShown from="prefs.hands"/>)");
        ASSERT_FALSE(parse.Ok());
        const String text = AllErrors(parse);
        EXPECT_TRUE(Has(text, kDocumentName)) << text.c_str();
        EXPECT_TRUE(Has(text, "moveLineShown")) << text.c_str();
        EXPECT_TRUE(Has(text, "prefs.hands")) << text.c_str();
    }

    TEST(HudValuesTest, AnUnknownTypeOfALevelIsRefusedAndTheValuesWithIt) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFact"/><moveLineShown from="view.hasLastMove"/>)");
        ASSERT_FALSE(parse.Ok());
        const String text = AllErrors(parse);
        EXPECT_TRUE(Has(text, kDocumentName)) << text.c_str();
        EXPECT_TRUE(Has(text, "HudFact")) << text.c_str();
    }

    TEST(HudValuesTest, ASecondContextIsRefused) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts"/><context other="HudFacts"/>)");
        ASSERT_FALSE(parse.Ok());
        EXPECT_TRUE(Has(AllErrors(parse), kDocumentName)) << AllErrors(parse).c_str();
    }

    TEST(HudValuesTest, AFromWithNoContextIsAnErrorNotACrash) {
        const HudValuesParse parse = ParseText(R"(<moveLineShown from="view.hasLastMove"/>)");
        ASSERT_FALSE(parse.Ok());
        const String text = AllErrors(parse);
        EXPECT_TRUE(Has(text, kDocumentName)) << text.c_str();
        EXPECT_TRUE(Has(text, "moveLineShown")) << text.c_str();
        EXPECT_TRUE(parse.values.moveLineShown.IsConst());
    }

    TEST(HudValuesTest, AMistypedValueNameIsAnErrorNotAHiddenElement) {
        const HudValuesParse parse = ParseText(R"(<context view="HudFacts"/><moveLineShow from="view.hasLastMove"/>)");
        ASSERT_FALSE(parse.Ok());
        EXPECT_TRUE(Has(AllErrors(parse), "moveLineShow")) << AllErrors(parse).c_str();
    }

    TEST(HudValuesTest, ADocumentWithNoRootIsAnError) {
        const HudValuesParse parse = ParseHudValues(pugi::xml_node(), kDocumentName);
        EXPECT_FALSE(parse.Ok());
    }

}  // namespace BECore::Mahjong::Tests
