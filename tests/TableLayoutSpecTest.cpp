#include "HandTestSupport.h"
#include "ShippedTableLayout.h"

#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TableLayoutSpecXml.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `config/TableLayout.xml` and its reader (`Table/TableLayoutSpecXml.h`): the shipped file parses with no error, a small text gives the spec
 * it says, and every kind of mistake in a file is one message that names the place and the attribute; `CheckTableLayoutSpec`
 * refuses a spec whose table is not sound. Also the global environment that reads
 * the shipped spec once and makes it the active one for the whole test binary: the game's `config/` is not mounted here.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Reads the shipped file before the first test, so a broken file fails at once and the same way for every test, and makes it the
        /// active spec of the whole binary (`MakeTableLayout` without a spec reads it).
        class ShippedTableLayoutEnvironment final : public ::testing::Environment {
        public:
            void SetUp() override {
                SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
            }

            void TearDown() override {
                SetActiveTableLayoutSpec(nullptr);
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

        /// One message of `problems` has every one of `parts`.
        void ExpectProblem(const eastl::vector<eastl::string>& problems, std::initializer_list<const char*> parts) {
            std::string all;
            for (const eastl::string& problem : problems) {
                bool every = true;
                for (const char* part : parts) {
                    every = every && problem.find(part) != eastl::string::npos;
                }
                if (every) {
                    return;
                }
                all += std::string(problem.c_str()) + "\n";
            }
            ADD_FAILURE() << "no message has all of the parts; the messages are:\n" << all;
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

        constexpr float kSameRectTolerance = 0.01f;

        Meld MakeMeld(MeldKind kind, Seat from, bool fromDiscard, const char* first, const char* claimed) {
            Meld meld;
            meld.kind = kind;
            meld.from = from;
            meld.fromDiscard = fromDiscard;
            meld.first = T(first);
            meld.claimed = T(claimed);
            return meld;
        }

        TableInput InputWith(Seat viewer, const TableProfile& profile) {
            TableInput input;
            input.viewer = viewer;
            input.profile = profile;
            return input;
        }

        /// Melds of every kind, claimed from every seat (the claim edges), bonus tiles and a pile spread over the seats.
        TableInput MixedTable(Seat viewer, const TableProfile& profile) {
            TableInput input = InputWith(viewer, profile);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const Seat next = (seat + 1) % kSeatCount;
                const Seat across = (seat + 2) % kSeatCount;
                const Seat previous = (seat + 3) % kSeatCount;
                input.concealedCount[seat] = 5 + seat;
                input.melds[seat] = {MakeMeld(MeldKind::Chow, previous, true, "3s", "4s"), MakeMeld(MeldKind::Pung, next, true, "5p", "5p"), MakeMeld(MeldKind::Kong, across, true, "7m", "7m"),
                                     MakeMeld(MeldKind::Pung, seat, false, "Rd", "Rd")};
                input.melds[seat].resize(static_cast<size_t>(1 + seat));
                input.bonusCount[seat] = seat * 2;
            }
            for (int entry = 0; entry < 31; ++entry) {
                input.discardSeats.push_back(static_cast<Seat>((entry * 3) % kSeatCount));
            }
            input.wallRemaining = 61;
            return input;
        }

        /// Every zone at what the rules allow, the pile spread over the seats or all of it by one seat.
        TableInput WorstTable(Seat viewer, const TableProfile& profile, bool pileOfOneSeat) {
            TableInput input = InputWith(viewer, profile);
            input.concealedCount.fill(kMaxConcealedTiles);
            input.bonusCount.fill(kMaxBonusTiles);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                input.melds[seat].assign(static_cast<size_t>(kMaxMelds), MakeMeld(MeldKind::Kong, (seat + 1) % kSeatCount, true, "1p", "1p"));
            }
            for (int entry = 0; entry < 100; ++entry) {
                input.discardSeats.push_back(pileOfOneSeat ? 2 : static_cast<Seat>(entry % kSeatCount));
            }
            input.wallRemaining = 2 * kWallRingCells;
            return input;
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return std::fabs(a.x - b.x) <= kSameRectTolerance && std::fabs(a.y - b.y) <= kSameRectTolerance && std::fabs(a.w - b.w) <= kSameRectTolerance && std::fabs(a.h - b.h) <= kSameRectTolerance;
        }

        void ExpectSameLayouts(const TableLayout& data, const TableLayout& other, const std::string& where) {
            ASSERT_EQ(data.slots.size(), other.slots.size()) << where;
            EXPECT_EQ(data.viewer, other.viewer) << where;
            EXPECT_EQ(data.shape, other.shape) << where;
            EXPECT_EQ(data.profile.phone, other.profile.phone) << where;
            EXPECT_EQ(data.profile.handTileW, other.profile.handTileW) << where;
            EXPECT_EQ(data.viewWidth, other.viewWidth) << where;
            EXPECT_EQ(data.viewHeight, other.viewHeight) << where;
            for (size_t index = 0; index < data.slots.size(); ++index) {
                const TileSlot& a = data.slots[index];
                const TileSlot& b = other.slots[index];
                ASSERT_TRUE(SameRect(a.rect, b.rect) && a.role == b.role && a.seat == b.seat && a.index == b.index && a.part == b.part && a.claimEdge == b.claimEdge && a.rotation == b.rotation)
                    << where << ": slot " << index << " role " << static_cast<int>(a.role) << " seat " << a.seat << " index " << a.index << " at " << a.rect.x << "," << a.rect.y << " " << a.rect.w
                    << "x" << a.rect.h << " against " << b.rect.x << "," << b.rect.y << " " << b.rect.w << "x" << b.rect.h;
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(SameRect(data.zones.discards[seat], other.zones.discards[seat])) << where << ": the discard block of seat " << seat;
                EXPECT_TRUE(SameRect(data.zones.seatPlates[seat], other.zones.seatPlates[seat])) << where << ": the plate of seat " << seat;
            }
            EXPECT_TRUE(SameRect(data.zones.wall, other.zones.wall)) << where << ": the wall ring";
            EXPECT_TRUE(SameRect(data.zones.centerPanel, other.zones.centerPanel)) << where << ": the centre panel";
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

    namespace {

        constexpr const char* kFreeXml = "<Free name=\"table.hint\" x=\"0.0625\" y=\"0.5\" w=\"0.125\" h=\"0.25\"/>";

        std::string FreeXml(const std::string& name, const std::string& rect = "x=\"0.0625\" y=\"0.5\" w=\"0.125\" h=\"0.25\"") {
            return "<Free name=\"" + name + "\" " + rect + "/>";
        }

        /// A region on ground that no tile of the shipped Landscape table reaches: the corner above the left seat's hand and left of the top seat's.
        FreeRegion CornerRegion() {
            return FreeRegion{"table.corner", ZoneRect{0.01f, 0.01f, 0.2f, 0.1f}};
        }

    }  // namespace

    TEST(TableLayoutSpecTest, AFreeRegionIsReadWithItsNameAndFractions) {
        const std::string text = Replaced(FileXml(), ShapeXml("Portrait"), ShapeXml("Portrait", kFreeXml + FreeXml("table.moveLine", "x=\"0\" y=\"0\" w=\"1\" h=\"0.5\"")));
        const TableLayoutSpecParse parse = ParseText(text);
        EXPECT_TRUE(parse.Ok());
        const eastl::vector<FreeRegion>& regions = parse.spec.Shape(TableShape::Portrait).freeRegions;
        ASSERT_EQ(regions.size(), 2U);
        EXPECT_STREQ(regions[0].name.c_str(), "table.hint");
        EXPECT_TRUE((regions[0].rect == ZoneRect{0.0625f, 0.5f, 0.125f, 0.25f}));
        EXPECT_STREQ(regions[1].name.c_str(), "table.moveLine");
        EXPECT_TRUE((regions[1].rect == ZoneRect{0.0f, 0.0f, 1.0f, 0.5f}));
        EXPECT_TRUE(parse.spec.Shape(TableShape::Landscape).freeRegions.empty());
        EXPECT_FALSE(parse.spec == ParseText(FileXml()).spec);
    }

    TEST(TableLayoutSpecTest, ABadFreeRegionIsRefusedAndNamedByShapeAndRegion) {
        const auto withFree = [&](const std::string& regions) { return Replaced(FileXml(), ShapeXml("Narrow"), ShapeXml("Narrow", regions)); };
        ExpectError(withFree(kFreeXml + FreeXml("table.hint")), {"shape 'Narrow'", "Free 'table.hint'", "given twice"});
        ExpectError(withFree(FreeXml("")), {"shape 'Narrow'", "Free", "attribute 'name' is empty"});
        ExpectError(withFree("<Free x=\"0.1\" y=\"0.1\" w=\"0.1\" h=\"0.1\"/>"), {"shape 'Narrow'", "Free", "missing attribute 'name'"});
        ExpectError(withFree(FreeXml("table.aVeryLongNameThatNoLayoutCouldKeepWithoutTheHeap")), {"shape 'Narrow'", "Free", "longer than 31"});
        ExpectError(withFree(FreeXml("table.hint", "x=\"1.5\" y=\"0.5\" w=\"0.1\" h=\"0.1\"")), {"shape 'Narrow'", "Free 'table.hint'", "attribute 'x'", "outside 0..1"});
        ExpectError(withFree(FreeXml("table.hint", "x=\"0.5\" y=\"0.5\" w=\"0.1\"")), {"Free 'table.hint'", "missing attribute 'h'"});
        ExpectError(withFree(FreeXml("table.hint", "x=\"0.5\" y=\"0.5\" w=\"0\" h=\"0.1\"")), {"Free 'table.hint'", "no area"});
        ExpectError(withFree(FreeXml("table.hint", "x=\"0.5\" y=\"0.5\" w=\"0.1\" h=\"0.1\" size=\"3\"")), {"Free 'table.hint'", "unknown attribute 'size'"});
        ExpectError(Replaced(FileXml(), "inset=\"0.125\"/>", "inset=\"0.125\">" + std::string(kFreeXml) + "</Shape>"), {"shape 'Wide'", "no zones of its own"});
    }

    TEST(TableLayoutSpecTest, TheLayoutWithoutASpecReadsTheActiveOne) {
        ASSERT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
        const TableInput input = MixedTable(1, TableProfile{});
        ExpectSameLayouts(MakeTableLayout(input), MakeTableLayout(input, ShippedTableLayoutSpec()), "active spec");
    }

    TEST(TableLayoutSpecTest, ASpecThatDoesNotDescribeTheShapeGivesNoSlots) {
        const TableLayoutSpec empty;
        const TableLayout layout = MakeTableLayout(WorstTable(2, TableProfile{}, false), empty);
        EXPECT_TRUE(layout.slots.empty());
        EXPECT_EQ(layout.viewer, 2);
        EXPECT_EQ(layout.shape, TableShape::Landscape);
    }

    TEST(TableLayoutSpecTest, TheShippedSpecPassesTheCheck) {
        const eastl::vector<eastl::string> problems = CheckTableLayoutSpec(ShippedTableLayoutSpec());
        for (const eastl::string& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
    }

    TEST(TableLayoutSpecTest, AnEmptySpecIsRefusedForEveryShape) {
        EXPECT_EQ(CheckTableLayoutSpec(TableLayoutSpec{}).size(), kTableShapeCount);
    }

    TEST(TableLayoutSpecTest, AZoneMovedOntoAnotherIsRefusedAndNamed) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        SeatSpec& top = spec.Shape(TableShape::Landscape).seats[2];
        spec.Shape(TableShape::Landscape).seats[0].discards.rect = top.discards.rect;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape'", "the discard block of position 0", "overlaps", "the discard block of position 2"});
    }

    TEST(TableLayoutSpecTest, ARowMovedOntoABlockIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Portrait).seats[1].bonus.rect = spec.Shape(TableShape::Portrait).seats[1].discards.rect;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Portrait'", "the bonus row of position 1", "the discard block of position 1"});
    }

    TEST(TableLayoutSpecTest, APlatePushedAgainstABlockIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        ShapeSpec& landscape = spec.Shape(TableShape::Landscape);
        // the right plate ends 24 units above its block; push it to 2 units
        landscape.seats[1].plate.y = (landscape.seats[1].discards.rect.y - 2.0f / kViewHeight) - landscape.seats[1].plate.h;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape'", "the plate of position 1", "nearer than", "the discard block of position 1"});
    }

    TEST(TableLayoutSpecTest, APlateOverARowIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        ShapeSpec& narrow = spec.Shape(TableShape::Narrow);
        narrow.seats[0].plate = narrow.seats[0].bonus.rect;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Narrow'", "the plate of position 0 overlaps", "the bonus row of position 0"});
    }

    TEST(TableLayoutSpecTest, AZoneTooSmallForItsTilesIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Landscape).seats[3].melds.rect.h = 0.0f;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape'", "the melds zone of position 3 is too small"});
        spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Portrait).wall.w = 0.0f;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Portrait'", "the wall zone is too small"});
        spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Narrow).seats[2].discards.rect.w = 0.001f;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Narrow'", "the discard zone of position 2 is too small"});
    }

    TEST(TableLayoutSpecTest, AMinimumAboveTheSolvedTileIsRefusedWithTheTileAndTheMinimum) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Landscape).seats[2].hand.minTileW = 36;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape'", "the hand tile of position 2 is 35 wide", "minTileW 36"});
    }

    TEST(TableLayoutSpecTest, AViewersHandUnderTheTouchTileIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        // one tile narrower than the least a hand may have: the zone is 14 tiles of 47 wide
        spec.Shape(TableShape::Narrow).seats[0].hand.rect.w = 14.0f * 47.0f / kNarrowViewWidth;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Narrow'", "the viewer's hand tile is 47 wide", "kViewerHandTileW"});
    }

    TEST(TableLayoutSpecTest, ATableThatLeavesTheViewIsRefused) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Wide).inset = 0.2f;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Wide'", "lies outside the view"});
    }

    TEST(TableLayoutSpecTest, ThePhoneProfileMustReachTheGreatestHandTile) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Landscape).phone.hand.rect.h = 90.0f / kViewHeight;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape', phone profile", "the widest hand tile is 68", "kPhoneHandTileMaxW"});
    }

    TEST(TableLayoutSpecTest, AFreeRegionOnGroundNoTileReachesPassesTheCheck) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Landscape).freeRegions.push_back(CornerRegion());
        const eastl::vector<eastl::string> problems = CheckTableLayoutSpec(spec);
        for (const eastl::string& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
    }

    TEST(TableLayoutSpecTest, AFreeRegionOnATileIsRefusedAndNamed) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        ShapeSpec& landscape = spec.Shape(TableShape::Landscape);
        landscape.freeRegions.push_back(FreeRegion{"table.onHand", landscape.seats[0].hand.rect});
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape'", "free region 'table.onHand'", "hand tile of position 0"});
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Landscape', phone profile", "free region 'table.onHand'"});
    }

    TEST(TableLayoutSpecTest, AFreeRegionOnABlockAPlateOrTheRingIsRefusedAndNamed) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        ShapeSpec& portrait = spec.Shape(TableShape::Portrait);
        portrait.freeRegions.push_back(FreeRegion{"table.onBlock", portrait.seats[2].discards.rect});
        portrait.freeRegions.push_back(FreeRegion{"table.onPlate", portrait.seats[1].plate});
        portrait.freeRegions.push_back(FreeRegion{"table.inRing", ZoneRect{portrait.wall.x + portrait.wall.w / 2.0f - 0.01f, portrait.wall.y + portrait.wall.h / 2.0f - 0.01f, 0.02f, 0.02f}});
        const eastl::vector<eastl::string> problems = CheckTableLayoutSpec(spec);
        ExpectProblem(problems, {"shape 'Portrait'", "free region 'table.onBlock'", "the discard block of position 2"});
        ExpectProblem(problems, {"shape 'Portrait'", "free region 'table.onPlate'", "the plate of position 1"});
        ExpectProblem(problems, {"shape 'Portrait'", "free region 'table.inRing'", "the wall ring"});
    }

    TEST(TableLayoutSpecTest, AFreeRegionOutsideTheViewIsRefusedAndNamed) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Narrow).freeRegions.push_back(FreeRegion{"table.off", ZoneRect{0.9f, 0.0f, 0.3f, 0.05f}});
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Narrow'", "free region 'table.off'", "outside the view"});
    }

    TEST(TableLayoutSpecTest, ALikeShapeHoldsItsBorrowedFreeRegionsToItsOwnView) {
        // Wide borrows Landscape's regions and lies a wing in: a region at the right edge of Landscape leaves the wide view at the wing's inset
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Landscape).freeRegions.push_back(FreeRegion{"table.edge", ZoneRect{0.99f, 0.0f, 0.01f, 0.01f}});
        const eastl::vector<eastl::string> problems = CheckTableLayoutSpec(spec);
        for (const eastl::string& problem : problems) {
            EXPECT_EQ(problem.find("outside the view"), eastl::string::npos) << problem.c_str();
        }
        spec.Shape(TableShape::Wide).inset = 0.2f;
        ExpectProblem(CheckTableLayoutSpec(spec), {"shape 'Wide'", "free region 'table.edge'", "outside the view"});
    }

    TEST(TableLayoutSpecTest, AProblemIsReportedOnceWhateverTheViewer) {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        spec.Shape(TableShape::Portrait).seats[0].discards.rect = spec.Shape(TableShape::Portrait).seats[2].discards.rect;
        const eastl::vector<eastl::string> problems = CheckTableLayoutSpec(spec);
        for (size_t first = 0; first < problems.size(); ++first) {
            for (size_t second = first + 1; second < problems.size(); ++second) {
                EXPECT_NE(problems[first], problems[second]);
            }
        }
        EXPECT_FALSE(problems.empty());
    }

    /// `OfferTableLayoutSpec` sets a process-wide spec: each test puts the shipped one back.
    class TableLayoutOfferTest : public ::testing::Test {
    protected:
        void SetUp() override {
            ASSERT_TRUE(LoadShippedTableLayoutDocument(_document));
        }

        void TearDown() override {
            SetActiveTableLayoutSpec(&ShippedTableLayoutSpec());
        }

        /// The `attribute` of the `zone` of seat `position` in the shape `shape` of the document.
        pugi::xml_attribute Attribute(const char* shape, int position, const char* zone, const char* attribute) {
            pugi::xml_node shapeNode = _document.document_element().find_child_by_attribute("Shape", "name", shape);
            pugi::xml_node seat = shapeNode.find_child_by_attribute("Seat", "position", std::to_string(position).c_str());
            return seat.child(zone).attribute(attribute);
        }

        pugi::xml_document _document;
    };

    TEST_F(TableLayoutOfferTest, ASoundFileThatIsTheActiveSpecIsUnchanged) {
        const TableLayoutOfferResult offer = OfferTableLayoutSpec(_document.document_element());
        EXPECT_EQ(offer.outcome, TableLayoutOffer::Unchanged);
        EXPECT_TRUE(offer.errors.empty());
        EXPECT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
    }

    TEST_F(TableLayoutOfferTest, ASoundFileThatDiffersBecomesTheActiveSpecOnce) {
        ASSERT_TRUE(Attribute("Landscape", 2, "Hand", "x").set_value("0.35"));
        const TableLayoutOfferResult offer = OfferTableLayoutSpec(_document.document_element());
        ASSERT_EQ(offer.outcome, TableLayoutOffer::Applied) << (offer.errors.empty() ? "" : offer.errors.front().c_str());
        ASSERT_NE(ActiveTableLayoutSpec(), nullptr);
        EXPECT_NE(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
        EXPECT_FLOAT_EQ(ActiveTableLayoutSpec()->Shape(TableShape::Landscape).seats[2].hand.rect.x, 0.35f);
        EXPECT_EQ(OfferTableLayoutSpec(_document.document_element()).outcome, TableLayoutOffer::Unchanged);
        // and back to what the file said
        ASSERT_TRUE(Attribute("Landscape", 2, "Hand", "x").set_value("0.346875"));
        EXPECT_EQ(OfferTableLayoutSpec(_document.document_element()).outcome, TableLayoutOffer::Applied);
        EXPECT_EQ(*ActiveTableLayoutSpec(), ShippedTableLayoutSpec());
    }

    TEST_F(TableLayoutOfferTest, AFileThatDoesNotReadIsRefusedAndTheLastGoodSpecStays) {
        ASSERT_TRUE(Attribute("Portrait", 1, "Bonus", "w").set_value("wide"));
        const TableLayoutOfferResult offer = OfferTableLayoutSpec(_document.document_element());
        EXPECT_EQ(offer.outcome, TableLayoutOffer::Rejected);
        ExpectProblem(offer.errors, {"shape 'Portrait'", "seat 1", "Bonus", "attribute 'w'", "'wide' is not a number"});
        EXPECT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
    }

    TEST_F(TableLayoutOfferTest, AFileThatReadsButIsNotASoundTableIsRefusedAndTheLastGoodSpecStays) {
        // the discards of the viewer on the discards of the top seat
        for (const char* attribute : {"x", "y", "w", "h"}) {
            ASSERT_TRUE(Attribute("Landscape", 0, "Discards", attribute).set_value(Attribute("Landscape", 2, "Discards", attribute).value()));
        }
        const TableLayoutOfferResult offer = OfferTableLayoutSpec(_document.document_element());
        EXPECT_EQ(offer.outcome, TableLayoutOffer::Rejected);
        ExpectProblem(offer.errors, {"shape 'Landscape'", "the discard block of position 0", "overlaps", "the discard block of position 2"});
        EXPECT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());
    }

    TEST_F(TableLayoutOfferTest, AFreeRegionOnATileIsRefusedAndTheLastGoodSpecStaysAFreeOneIsApplied) {
        pugi::xml_node landscape = _document.document_element().find_child_by_attribute("Shape", "name", "Landscape");
        pugi::xml_node region = landscape.append_child("Free");
        region.append_attribute("name") = "table.test";
        region.append_attribute("x") = "0.05625";
        region.append_attribute("y") = "0.9";
        region.append_attribute("w") = "0.1";
        region.append_attribute("h") = "0.08";
        const TableLayoutOfferResult refused = OfferTableLayoutSpec(_document.document_element());
        EXPECT_EQ(refused.outcome, TableLayoutOffer::Rejected);
        ExpectProblem(refused.errors, {"shape 'Landscape'", "free region 'table.test'", "hand tile of position 0"});
        EXPECT_EQ(ActiveTableLayoutSpec(), &ShippedTableLayoutSpec());

        region.attribute("x").set_value("0.01");
        region.attribute("y").set_value("0.01");
        region.attribute("w").set_value("0.2");
        region.attribute("h").set_value("0.1");
        const TableLayoutOfferResult applied = OfferTableLayoutSpec(_document.document_element());
        ASSERT_EQ(applied.outcome, TableLayoutOffer::Applied) << (applied.errors.empty() ? "" : applied.errors.front().c_str());
        ASSERT_NE(ActiveTableLayoutSpec(), nullptr);
        ASSERT_EQ(ActiveTableLayoutSpec()->Shape(TableShape::Landscape).freeRegions.size(), ShippedTableLayoutSpec().Shape(TableShape::Landscape).freeRegions.size() + 1);
        EXPECT_STREQ(ActiveTableLayoutSpec()->Shape(TableShape::Landscape).freeRegions.back().name.c_str(), "table.test");
        EXPECT_EQ(OfferTableLayoutSpec(_document.document_element()).outcome, TableLayoutOffer::Unchanged);
    }

    TEST_F(TableLayoutOfferTest, AnOfferWithNoActiveSpecAppliesAndARefusalAfterAnApplyKeepsTheApplied) {
        SetActiveTableLayoutSpec(nullptr);
        ASSERT_EQ(OfferTableLayoutSpec(_document.document_element()).outcome, TableLayoutOffer::Applied);
        const TableLayoutSpec* applied = ActiveTableLayoutSpec();
        ASSERT_NE(applied, nullptr);
        EXPECT_EQ(*applied, ShippedTableLayoutSpec());
        ASSERT_TRUE(Attribute("Narrow", 3, "Hand", "x").set_value("2"));
        EXPECT_EQ(OfferTableLayoutSpec(_document.document_element()).outcome, TableLayoutOffer::Rejected);
        EXPECT_EQ(ActiveTableLayoutSpec(), applied);
        EXPECT_EQ(*applied, ShippedTableLayoutSpec());
    }

}  // namespace BECore::Mahjong::Tests
