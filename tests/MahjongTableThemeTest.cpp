#include <Mahjong/Table/TableLook.h>
#include <Mahjong/Table/TileAtlas.h>
#include <tests/MahjongTableTestSupport.h>
#include <tests/ShippedThemes.h>
#include <tests/UiStyleTestSupport.h>

/**
 * The table follows the theme (`MahjongTableComponent::RefreshPalette`, `FeltTexture`): the felt texture, the rim, the lip and the rack take the colours of the current theme, the
 * backs and the wall stacks take its cell of the atlas, the faces stay as they are.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kClassic = "classic";
        constexpr const char* kNavy = "navy";
        /// The middle pixel of the felt is the felt colour times `kFeltCentreGain`, give or take the grain and a rounding.
        constexpr int kFeltPixelTolerance = kFeltGrainLevels + 2;
        constexpr size_t kRimSides = 4;
        constexpr size_t kLipSides = 4;

    }  // namespace

    class MahjongTableThemeTest : public MahjongTableTestBase {
    protected:
        void ChooseTheme(const char* id) {
            ASSERT_TRUE(_game->SetTheme(PoolString::Intern(id))) << id;
        }

        /// The felt of the last draw: the first texture call that is neither the atlas nor the emblems.
        const DrawCall* FeltCall() const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture != _atlasTexture && call.texture != _emblemTexture) {
                    return &call;
                }
            }
            return nullptr;
        }

        /// The pixel in the middle of the texture of the felt call, as the renderer was given it.
        Color FeltMiddle() const {
            const DrawCall* call = FeltCall();
            EXPECT_NE(call, nullptr);
            const auto* texture = dynamic_cast<const RecordedTexture*>(call ? call->texture : nullptr);
            EXPECT_NE(texture, nullptr);
            if (!texture) {
                return {};
            }
            const size_t width = static_cast<size_t>(texture->GetWidth());
            const size_t height = static_cast<size_t>(texture->GetHeight());
            const size_t offset = ((height / 2) * width + width / 2) * 4;
            const Vector<uint8_t>& pixels = texture->GetPixels();
            return Color(pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
        }

        static int GainedChannel(unsigned char channel) {
            return std::min(255, static_cast<int>(std::lround(static_cast<float>(channel) * kFeltCentreGain)));
        }

        void ExpectFeltIs(const Color& felt) const {
            const Color middle = FeltMiddle();
            EXPECT_NEAR(middle.r, GainedChannel(felt.r), kFeltPixelTolerance);
            EXPECT_NEAR(middle.g, GainedChannel(felt.g), kFeltPixelTolerance);
            EXPECT_NEAR(middle.b, GainedChannel(felt.b), kFeltPixelTolerance);
        }

        /// The source rectangle of every tile call, in the order of the slots.
        Vector<Rect> TileSources() const {
            Vector<Rect> sources;
            for (const DrawCall& call : TileCalls()) {
                sources.push_back(call.source.value_or(Rect{}));
            }
            return sources;
        }
    };

    TEST_F(MahjongTableThemeTest, TheFeltIsMadeAgainOncePerChangeOfTheFeltColourAndNeverInAFrameAtRest) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        ExpectFeltIs(Palette().felt);
        const uint32_t made = _renderer->texturesCreated;

        Draw();
        Draw();
        EXPECT_EQ(_renderer->texturesCreated, made) << "a frame at rest makes no texture";

        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kNavy));
        Draw();
        EXPECT_EQ(_renderer->texturesCreated, made + 1) << "one texture for the change";
        ExpectFeltIs(Palette().felt);
        const ITexture* navyFelt = FeltCall()->texture;
        Draw();
        Draw();
        EXPECT_EQ(_renderer->texturesCreated, made + 1) << "and none at the next frames";
        EXPECT_EQ(FeltCall()->texture, navyFelt);

        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kClassic));
        Draw();
        EXPECT_EQ(_renderer->texturesCreated, made + 2);
        ExpectFeltIs(Palette().felt);
        Draw();
        EXPECT_EQ(_renderer->texturesCreated, made + 2);
    }

    TEST_F(MahjongTableThemeTest, TheFeltOfTheThemesAreNotTheSameColour) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const Color classic = FeltMiddle();
        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kNavy));
        Draw();
        const Color navy = FeltMiddle();
        EXPECT_GT(navy.b, navy.g) << "navy is blue";
        EXPECT_GT(classic.g, classic.b) << "classic is green";
    }

    TEST_F(MahjongTableThemeTest, TheRimTheLipAndTheRackCarryTheColoursOfTheCurrentTheme) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const TablePalette classic = Palette();
        EXPECT_EQ(MarksOf(_renderer->calls, classic.feltRim).size(), kRimSides);
        EXPECT_EQ(MarksOf(_renderer->calls, classic.feltLip).size(), kLipSides);

        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kNavy));
        Draw();
        const TablePalette navy = Palette();
        ASSERT_FALSE(SameColor(navy.feltRim, classic.feltRim));
        ASSERT_FALSE(SameColor(navy.feltLip, classic.feltLip));
        EXPECT_EQ(MarksOf(_renderer->calls, navy.feltRim).size(), kRimSides);
        EXPECT_EQ(MarksOf(_renderer->calls, navy.feltLip).size(), kLipSides);
        EXPECT_TRUE(MarksOf(_renderer->calls, classic.feltRim).empty()) << "nothing of the old theme is left";
        EXPECT_TRUE(MarksOf(_renderer->calls, classic.feltLip).empty());
        EXPECT_EQ(MarksOf(_renderer->calls, navy.handRackLip).size(), 1u);
        EXPECT_EQ(MarksOf(_renderer->calls, navy.handRack).size(), 1u);
        EXPECT_EQ(PanelCountAt(_renderer->calls, _table->GetLayout().zones.seatPlates[static_cast<size_t>(kSeat1)], navy), 1) << "the seat plates are painted in the palette of the theme";
    }

    TEST_F(MahjongTableThemeTest, TheBacksAndTheWallStacksFollowTheThemeAndEveryFaceStaysTheSame) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        const TileBackSet& classicSet = BackSetOf(kClassic);
        const TileBackSet& navySet = BackSetOf(kNavy);
        ASSERT_NE(classicSet.backCell, navySet.backCell);

        Draw();
        const Vector<Rect> classic = TileSources();
        const TableLayout& layout = _table->GetLayout();
        ASSERT_EQ(classic.size(), layout.slots.size());

        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kNavy));
        Draw();
        const Vector<Rect> navy = TileSources();
        ASSERT_EQ(navy.size(), classic.size());

        int backs = 0;
        int stacks = 0;
        int faces = 0;
        for (size_t i = 0; i < classic.size(); ++i) {
            if (layout.slots[i].role == SlotRole::WallStack) {
                ++stacks;
                EXPECT_TRUE(SameRect(classic[i], StackRect(classicSet))) << "slot " << i;
                EXPECT_TRUE(SameRect(navy[i], StackRect(navySet))) << "slot " << i;
            } else if (SameRect(classic[i], BackRect(classicSet))) {
                ++backs;
                EXPECT_TRUE(SameRect(navy[i], BackRect(navySet))) << "slot " << i;
            } else {
                ++faces;
                EXPECT_TRUE(SameRect(navy[i], classic[i])) << "a face changed with the theme at slot " << i;
                EXPECT_FALSE(SameRect(navy[i], BackRect(navySet))) << "slot " << i;
            }
        }
        EXPECT_GT(stacks, 0);
        EXPECT_GT(backs, 0);
        EXPECT_GT(faces, 0);

        ASSERT_NO_FATAL_FAILURE(ChooseTheme(kClassic));
        Draw();
        const Vector<Rect> back = TileSources();
        ASSERT_EQ(back.size(), classic.size());
        for (size_t i = 0; i < classic.size(); ++i) {
            EXPECT_TRUE(SameRect(back[i], classic[i])) << "slot " << i;
        }
    }

}  // namespace BECore::Mahjong::Tests
