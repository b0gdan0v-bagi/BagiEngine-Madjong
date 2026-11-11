#include "ShippedTableLayout.h"

#include <Mahjong/Table/TableLayoutSpecXml.h>
#include <gtest/gtest.h>

/**
 * `config/TableLayout.xml` and its reader (`Table/TableLayoutSpecXml.h`): the shipped file parses with no error, a small text gives the spec
 * it says, and every kind of mistake in a file is one message that names the place and the attribute. Also the global environment that reads
 * the shipped spec once for the whole test binary: the game's `config/` is not mounted here.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Reads the shipped file before the first test, so a broken file fails at once and the same way for every test.
        class ShippedTableLayoutEnvironment final : public ::testing::Environment {
        public:
            void SetUp() override {
                static_cast<void>(ShippedTableLayoutSpec());
            }
        };

        [[maybe_unused]] ::testing::Environment* const kShippedTableLayoutEnvironment = ::testing::AddGlobalTestEnvironment(new ShippedTableLayoutEnvironment);

        constexpr const char* kSeatZones = "<Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"
                                           "<Melds x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"
                                           "<Bonus x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"
                                           "<Discards x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"
                                           "<Plate x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>";

        std::string SeatXml(int position) {
            return "<Seat position=\"" + std::to_string(position) + "\">" + kSeatZones + "</Seat>";
        }

        std::string ShapeXml(const std::string& name, const std::string& extra = {}) {
            std::string text = "<Shape name=\"" + name + "\">";
            for (int position = 0; position < kSeatCount; ++position) {
                text += SeatXml(position);
            }
            return text + "<Wall x=\"0.25\" y=\"0.5\" w=\"0.5\" h=\"0.25\"/>" + extra + "</Shape>";
        }

        /// A complete file: the four shapes, Wide like Landscape. A test breaks it by replacing a piece.
        std::string FileXml() {
            return "<TableLayout>" + ShapeXml("Landscape") + ShapeXml("Portrait") + ShapeXml("Narrow") + "<Shape name=\"Wide\" like=\"Landscape\" inset=\"0.125\"/></TableLayout>";
        }

        std::string Replaced(std::string text, const std::string& from, const std::string& to) {
            const size_t at = text.find(from);
            EXPECT_NE(at, std::string::npos) << "the base text has no '" << from << "'";
            if (at != std::string::npos) {
                text.replace(at, from.size(), to);
            }
            return text;
        }

        TableLayoutSpecParse ParseText(const std::string& text) {
            pugi::xml_document document;
            const pugi::xml_parse_result result = document.load_string(text.c_str(), pugi::parse_default | pugi::parse_comments);
            EXPECT_TRUE(result) << result.description();
            return ParseTableLayoutSpec(document.document_element());
        }

        /// Whether one message of the parse has every one of `parts`.
        bool HasError(const TableLayoutSpecParse& parse, std::initializer_list<const char*> parts) {
            for (const eastl::string& error : parse.errors) {
                bool all = true;
                for (const char* part : parts) {
                    all = all && error.find(part) != eastl::string::npos;
                }
                if (all) {
                    return true;
                }
            }
            return false;
        }

        void ExpectError(const std::string& text, std::initializer_list<const char*> parts) {
            const TableLayoutSpecParse parse = ParseText(text);
            EXPECT_FALSE(parse.Ok()) << text;
            std::string all;
            for (const eastl::string& error : parse.errors) {
                all += std::string(error.c_str()) + "\n";
            }
            EXPECT_TRUE(HasError(parse, parts)) << "no message has all of the parts; the messages are:\n" << all;
        }

        std::string ShapeName(TableShape shape) {
            const auto name = EnumUtils<TableShape>::ToString(shape);
            return std::string(name.data(), name.size());
        }

        SeatSpec PlainSeat() {
            SeatSpec seat;
            const ZoneRect rect{0.25f, 0.5f, 0.125f, 0.0625f};
            seat.hand.rect = rect;
            seat.melds.rect = rect;
            seat.bonus.rect = rect;
            seat.discards.rect = rect;
            seat.plate = rect;
            return seat;
        }

        ShapeSpec PlainShape() {
            ShapeSpec shape;
            shape.present = true;
            shape.seats.fill(PlainSeat());
            shape.wall = ZoneRect{0.25f, 0.5f, 0.5f, 0.25f};
            return shape;
        }

        void ExpectInUnitRange(const ZoneRect& zone, const std::string& where) {
            EXPECT_GE(zone.x, 0.0f) << where;
            EXPECT_GE(zone.y, 0.0f) << where;
            EXPECT_GT(zone.w, 0.0f) << where;
            EXPECT_GT(zone.h, 0.0f) << where;
            EXPECT_LE(zone.x + zone.w, 1.0f + 1e-5f) << where;
            EXPECT_LE(zone.y + zone.h, 1.0f + 1e-5f) << where;
        }

    }  // namespace

    TEST(TableLayoutSpecTest, TheShippedFileParsesWithNoError) {
        const TableLayoutSpecParse parse = ReadShippedTableLayout();
        EXPECT_TRUE(parse.Ok());
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            EXPECT_TRUE(parse.spec.Shape(shape).present) << ShapeName(shape);
            EXPECT_NE(ZonesOf(parse.spec, shape), nullptr) << ShapeName(shape);
        }
    }

    TEST(TableLayoutSpecTest, WideBorrowsLandscapeAndLiesAWingFromTheLeftEdge) {
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        const ShapeSpec& wide = spec.Shape(TableShape::Wide);
        EXPECT_TRUE(wide.like);
        EXPECT_EQ(wide.likeShape, TableShape::Landscape);
        EXPECT_NEAR(wide.inset * kWideViewWidth, kWideWingWidth, 0.01f);
        EXPECT_EQ(ZonesOf(spec, TableShape::Wide), &spec.Shape(TableShape::Landscape));
    }

    TEST(TableLayoutSpecTest, OnlyTheLandscapeShapeHasAPhoneOverride) {
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        EXPECT_TRUE(spec.Shape(TableShape::Landscape).hasPhone);
        EXPECT_FALSE(spec.Shape(TableShape::Portrait).hasPhone);
        EXPECT_FALSE(spec.Shape(TableShape::Narrow).hasPhone);
        const SeatSpec& phone = spec.Shape(TableShape::Landscape).phone;
        EXPECT_TRUE(phone.hand.profileTile);
        EXPECT_EQ(phone.hand.align, ZoneAlign::End);
        EXPECT_FALSE(spec.Shape(TableShape::Landscape).seats[0].hand.profileTile);
        EXPECT_LT(phone.discards.rows, kDiscardRowCapacity);
    }

    TEST(TableLayoutSpecTest, EveryZoneOfTheShippedFileLiesInsideItsView) {
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            const ShapeSpec* zones = ZonesOf(spec, shape);
            ASSERT_NE(zones, nullptr);
            const std::string name = ShapeName(shape);
            for (int position = 0; position < kSeatCount; ++position) {
                const SeatSpec& seat = zones->seats[static_cast<size_t>(position)];
                const std::string where = name + " seat " + std::to_string(position);
                ExpectInUnitRange(seat.hand.rect, where + " hand");
                ExpectInUnitRange(seat.melds.rect, where + " melds");
                ExpectInUnitRange(seat.bonus.rect, where + " bonus");
                ExpectInUnitRange(seat.discards.rect, where + " discards");
                ExpectInUnitRange(seat.plate, where + " plate");
            }
            ExpectInUnitRange(zones->wall, name + " wall");
        }
    }

    TEST(TableLayoutSpecTest, ASmallTextGivesTheSpecItSays) {
        ShapeSpec landscape = PlainShape();
        landscape.seats[0].hand.align = ZoneAlign::Center;
        landscape.seats[0].hand.spread = true;
        landscape.seats[0].hand.minTileW = 7;
        landscape.seats[1].discards.rows = 3;
        landscape.hasPhone = true;
        landscape.phone = PlainSeat();
        landscape.phone.hand.profileTile = true;
        landscape.phone.hand.align = ZoneAlign::End;
        ShapeSpec wide;
        wide.present = true;
        wide.like = true;
        wide.likeShape = TableShape::Landscape;
        wide.inset = 0.125f;
        TableLayoutSpec expected;
        expected.Shape(TableShape::Landscape) = landscape;
        expected.Shape(TableShape::Portrait) = PlainShape();
        expected.Shape(TableShape::Narrow) = PlainShape();
        expected.Shape(TableShape::Wide) = wide;

        const std::string phone = "<Phone><Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\" align=\"end\" tile=\"profile\"/>"
                                  "<Melds x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/><Bonus x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"
                                  "<Discards x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/><Plate x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/></Phone>";
        std::string text = Replaced(FileXml(), "<Wall x=\"0.25\" y=\"0.5\" w=\"0.5\" h=\"0.25\"/></Shape>", "<Wall x=\"0.25\" y=\"0.5\" w=\"0.5\" h=\"0.25\"/>" + phone + "</Shape>");
        text = Replaced(text, "<Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>", "<Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\" align=\"center\" spread=\"true\" minTileW=\"7\"/>");
        const size_t discards = text.find("<Discards", text.find("<Seat position=\"1\">"));
        ASSERT_NE(discards, std::string::npos);
        text.insert(text.find("/>", discards), " rows=\"3\"");

        const TableLayoutSpecParse parse = ParseText(text);
        EXPECT_TRUE(parse.Ok());
        EXPECT_TRUE(parse.spec == expected);
    }

    TEST(TableLayoutSpecTest, ADefaultedZoneAsksNothing) {
        const TableLayoutSpecParse parse = ParseText(FileXml());
        ASSERT_TRUE(parse.Ok());
        const StripZone& hand = parse.spec.Shape(TableShape::Landscape).seats[0].hand;
        EXPECT_EQ(hand.align, ZoneAlign::Start);
        EXPECT_FALSE(hand.spread);
        EXPECT_FALSE(hand.profileTile);
        EXPECT_EQ(hand.minTileW, 0);
        EXPECT_EQ(parse.spec.Shape(TableShape::Landscape).seats[0].discards.rows, kDiscardRowCapacity);
        EXPECT_FALSE(parse.spec.Shape(TableShape::Landscape).hasPhone);
    }

    TEST(TableLayoutSpecTest, CommentsAreNotElements) {
        std::string text = Replaced(FileXml(), "<TableLayout>", "<TableLayout><!-- a -->");
        text = Replaced(text, "<Shape name=\"Landscape\">", "<Shape name=\"Landscape\"><!-- b -->");
        text = Replaced(text, "<Seat position=\"2\">", "<Seat position=\"2\"><!-- c -->");
        text = Replaced(text, "inset=\"0.125\"/>", "inset=\"0.125\"><!-- d --></Shape>");
        const TableLayoutSpecParse parse = ParseText(text);
        EXPECT_TRUE(parse.Ok());
        EXPECT_TRUE(parse.spec == ParseText(FileXml()).spec);
    }

    TEST(TableLayoutSpecTest, ANumberThatIsNoNumberNamesTheShapeTheSeatTheZoneAndTheAttribute) {
        ExpectError(Replaced(FileXml(), "<Seat position=\"1\"><Hand x=\"0.25\"", "<Seat position=\"1\"><Hand x=\"abc\""),
                    {"shape 'Landscape'", "seat 1", "Hand", "attribute 'x'", "'abc' is not a number"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"nan\""), {"shape 'Landscape'", "Wall", "attribute 'x'", "not a number"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"inf\""), {"Wall", "not a number"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"\""), {"Wall", "not a number"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"0.2x\""), {"Wall", "'0.2x' is not a number"});
    }

    TEST(TableLayoutSpecTest, AFractionOutsideZeroToOneIsRefused) {
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"1.5\""), {"Wall", "attribute 'x'", "outside 0..1"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\"", "<Wall x=\"-0.1\""), {"Wall", "attribute 'x'", "outside 0..1"});
        ExpectError(Replaced(FileXml(), "w=\"0.5\" h=\"0.25\"/>", "w=\"0.5\" h=\"2\"/>"), {"Wall", "attribute 'h'", "outside 0..1"});
    }

    TEST(TableLayoutSpecTest, AMissingAttributeAZoneASeatOrAShapeIsAnError) {
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\" y=\"0.5\"", "<Wall x=\"0.25\""), {"Wall", "missing attribute 'y'"});
        ExpectError(Replaced(FileXml(), "<Plate x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>", ""), {"seat 0", "'Plate' is missing"});
        ExpectError(Replaced(FileXml(), "<Wall x=\"0.25\" y=\"0.5\" w=\"0.5\" h=\"0.25\"/>", ""), {"shape 'Landscape'", "'Wall' is missing"});
        ExpectError(Replaced(FileXml(), SeatXml(2), ""), {"shape 'Landscape'", "seat 2 is missing"});
        ExpectError(Replaced(FileXml(), ShapeXml("Narrow"), ""), {"shape 'Narrow' is missing"});
        ExpectError("<Other/>", {"no 'TableLayout' element"});
    }

    TEST(TableLayoutSpecTest, ARepeatedThingIsAnError) {
        ExpectError(Replaced(FileXml(), SeatXml(1), SeatXml(1) + SeatXml(1)), {"seat 1", "given twice"});
        ExpectError(Replaced(FileXml(), "<Seat position=\"3\">", "<Seat position=\"3\"><Plate x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>"), {"seat 3", "'Plate' is given twice"});
        ExpectError(Replaced(FileXml(), "<Shape name=\"Wide\"", ShapeXml("Narrow") + "<Shape name=\"Wide\""), {"shape 'Narrow' is given twice"});
    }

    TEST(TableLayoutSpecTest, AnUnknownNameIsAnError) {
        ExpectError(Replaced(FileXml(), "<Shape name=\"Narrow\">", "<Shape name=\"Square\">"), {"shape 'Square' is not a shape"});
        ExpectError(Replaced(FileXml(), "<Wall ", "<Floor "), {"shape 'Landscape'", "unknown element 'Floor'"});
        ExpectError(Replaced(FileXml(), "<Hand x", "<Hand size=\"3\" x"), {"Hand", "unknown attribute 'size'"});
        ExpectError(Replaced(FileXml(), "<TableLayout>", "<TableLayout><Surprise/>"), {"unknown element 'Surprise'"});
    }

    TEST(TableLayoutSpecTest, ABadValueOfAKnownAttributeIsAnError) {
        const std::string hand = "<Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>";
        const auto withHand = [&](const std::string& attributes) { return Replaced(FileXml(), hand, "<Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\" " + attributes + "/>"); };
        ExpectError(withHand("align=\"middle\""), {"Hand", "attribute 'align'", "'middle'"});
        ExpectError(withHand("tile=\"big\""), {"Hand", "attribute 'tile'", "'big'"});
        ExpectError(withHand("spread=\"yes\""), {"Hand", "attribute 'spread'", "true or false"});
        ExpectError(withHand("minTileW=\"-3\""), {"Hand", "attribute 'minTileW'"});
        ExpectError(withHand("minTileW=\"2.5\""), {"Hand", "attribute 'minTileW'", "whole number"});
        const std::string discards = "<Discards x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/>";
        ExpectError(Replaced(FileXml(), discards, "<Discards x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\" rows=\"0\"/>"), {"Discards", "attribute 'rows'"});
        ExpectError(Replaced(FileXml(), "<Seat position=\"1\">", "<Seat position=\"4\">"), {"attribute 'position'"});
        ExpectError(Replaced(FileXml(), "<Seat position=\"1\">", "<Seat>"), {"no attribute 'position'"});
    }

    TEST(TableLayoutSpecTest, ALikeShapeNamesAShapeThatHasZonesOfItsOwn) {
        const std::string wide = "<Shape name=\"Wide\" like=\"Landscape\" inset=\"0.125\"/>";
        ExpectError(Replaced(FileXml(), wide, "<Shape name=\"Wide\" like=\"Round\" inset=\"0.125\"/>"), {"shape 'Wide'", "attribute 'like'", "'Round' is not a shape"});
        ExpectError(Replaced(FileXml(), wide, "<Shape name=\"Wide\" like=\"Wide\"/>"), {"shape 'Wide'", "'like' itself"});
        ExpectError(Replaced(FileXml(), ShapeXml("Narrow"), "<Shape name=\"Narrow\" like=\"Wide\"/>"), {"shape 'Narrow'", "itself 'like'"});
        ExpectError(Replaced(FileXml(), wide, "<Shape name=\"Wide\" like=\"Landscape\" inset=\"3\"/>"), {"shape 'Wide'", "attribute 'inset'", "outside 0..1"});
        ExpectError(Replaced(FileXml(), wide, "<Shape name=\"Wide\" like=\"Landscape\">" + SeatXml(0) + "</Shape>"), {"shape 'Wide'", "no zones of its own"});
        ExpectError(Replaced(FileXml(), "<Shape name=\"Portrait\">", "<Shape name=\"Portrait\" inset=\"0.1\">"), {"shape 'Portrait'", "'inset'"});
    }

    TEST(TableLayoutSpecTest, ThePhoneOverrideIsForTheLandscapeShapesAndIsComplete) {
        const std::string phone = "<Phone>" + std::string(kSeatZones) + "</Phone>";
        ExpectError(Replaced(FileXml(), ShapeXml("Portrait"), ShapeXml("Portrait", phone)), {"shape 'Portrait'", "Phone", "landscape shapes only"});
        ExpectError(Replaced(FileXml(), ShapeXml("Landscape"), ShapeXml("Landscape", "<Phone><Hand x=\"0.25\" y=\"0.5\" w=\"0.125\" h=\"0.0625\"/></Phone>")), {"Phone", "'Melds' is missing"});
        ExpectError(Replaced(FileXml(), ShapeXml("Landscape"), ShapeXml("Landscape", phone + phone)), {"shape 'Landscape'", "'Phone' is given twice"});
    }

}  // namespace BECore::Mahjong::Tests
