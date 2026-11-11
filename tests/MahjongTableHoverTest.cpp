#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UILabel.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/TileHover.h>

/**
 * The name on hover in the scene (`Components/MahjongTableComponent.h`, "The name on hover"; the node `TileHover`): the mouse pointer over a
 * face-up tile shows its `TileName` in a plate, over anything else shows nothing. The pure rules are `TileHoverTest`; here the pointer goes
 * through `UIInput` as the platform layer sends it, so a canvas that consumes a motion keeps it from the table as it does in the game.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// Computer steps played before the draws, so that the piles and the melds of every seat hold tiles.
        constexpr int kStepsIntoTheHand = 40;

        /// Seat 1 (a person in `kPersonAtSeat1`) may take the 5p that East just discarded: a claim window with the buttons of a claim.
        HandModel ClaimWindowModel() {
            FixedWallSpec spec = SpecOf(kPlainHand, "5p");
            spec.hands[kSeat1] = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            return model;
        }

        Rect ViewRect() {
            return Rect(0.0f, 0.0f, kViewWidth, kViewHeight);
        }

    }  // namespace

    class MahjongTableHoverTest : public MahjongTableTestBase {
    protected:
        /// A match with `seats`, then `model` laid in place of the hand in progress.
        void StartWith(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
        }

        /// A match of computers played `kStepsIntoTheHand` steps in: nobody is the person, so every hand is face down.
        void StartSpectating() {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
                ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
            }
            Draw();
        }

        /// The slot as the last layout has it (a copy: the next draw replaces the layout); a failure and an empty slot when there is none.
        TileSlot SlotWhere(SlotRole role, Seat seat, int index, int part = 0) const {
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == role && slot.seat == seat && slot.index == index && slot.part == part) {
                    return slot;
                }
            }
            ADD_FAILURE() << "no slot of role " << static_cast<int>(role) << " seat " << seat << " index " << index << " part " << part;
            return TileSlot{};
        }

        /// The first slot of a role in the last layout.
        TileSlot FirstSlotOf(SlotRole role) const {
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == role) {
                    return slot;
                }
            }
            ADD_FAILURE() << "no slot of role " << static_cast<int>(role);
            return TileSlot{};
        }

        /// The pointer moves to the middle of a rectangle through the engine's input path, and the table is drawn.
        void PointAt(const Rect& rect, SDL_MouseID which = 0) {
            HoverAt(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, which);
            Draw();
        }

        /// The name as the player sees it: the text of the label of the plate when the plate is on screen, else empty. The variable has to agree.
        std::string PlateText() const {
            const bool shown = OnScreen("TileHover", "TileHoverPlate");
            const std::string variable = Var("tileHoverText");
            EXPECT_EQ(shown, !variable.empty()) << "the plate is shown exactly while there is a name";
            const UIElement* element = Element("TileHover", "TileHoverLabel");
            const auto* label = element != nullptr ? element->Cast<UILabel>() : nullptr;
            EXPECT_NE(label, nullptr);
            if (!shown || label == nullptr) {
                return {};
            }
            EXPECT_EQ(std::string(label->GetDisplayText().data(), label->GetDisplayText().size()), variable);
            return variable;
        }

        /// The name of a tile in the current language, as the plate words it.
        static std::string NameOf(Tile tile) {
            const eastl::string name = TileName(CoreManager::GetUIStrings(), tile);
            return std::string(name.data(), name.size());
        }

        static std::string NameOf(const char* tile) {
            return NameOf(T(tile));
        }

        /// The plate as the last draw laid it out, in view units.
        Rect PlateRect() const {
            const UIElement* plate = Element("TileHover", "TileHoverPlate");
            return plate != nullptr ? plate->rect : Rect{};
        }

        /// Moves the person's hand to the end: the hand is over, the score table (a modal canvas) is up, and the plate of the table would be hidden by it.
        void PlayTheHandOut() {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            _game->GetSession().RunComputers();
            ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
            Draw();
        }
    };

    // ----- which tile -----

    TEST_F(MahjongTableHoverTest, ATileOfTheViewersHandShowsItsNameAndTheLanguageChangesIt) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        const int index = IndexOf(kSeat0, T("9p"));
        ASSERT_GE(index, 0);
        ASSERT_EQ(PlateText(), "") << "no pointer yet";

        PointAt(SlotWhere(SlotRole::HandTile, kSeat0, index).rect);

        EXPECT_EQ(PlateText(), "9 Dots");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Draw();
        const std::string russian = PlateText();
        EXPECT_FALSE(russian.empty()) << "the plate stays while the pointer rests, in the new language";
        EXPECT_EQ(russian, NameOf("9p"));
        EXPECT_NE(russian, "9 Dots");
    }

    TEST_F(MahjongTableHoverTest, ADiscardAnOpenSetAndABonusTileShowTheirNames) {
        // A discard: seat 0 discards the 9p it drew
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("9p"), false)).has_value());
        Draw();
        PointAt(SlotWhere(SlotRole::DiscardTile, kSeat0, 0).rect);
        EXPECT_EQ(PlateText(), "9 Dots");

        // An open set of another seat: every tile of the pung of 5p
        Lay(UpgradeModel());
        Draw();
        ASSERT_FALSE(Hand().GetHand(kSeat1).melds.empty());
        for (int part = 0; part < 3; ++part) {
            PointAt(SlotWhere(SlotRole::MeldTile, kSeat1, 0, part).rect);
            EXPECT_EQ(PlateText(), "5 Dots") << "part " << part;
        }

        // A bonus tile of the viewer
        FixedWallSpec spec = SpecOf("F1 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p");
        Lay(MakeModel(spec, kSeat0, Wind::East, true));
        Draw();
        ASSERT_FALSE(Hand().GetHand(kSeat0).bonus.empty());
        PointAt(SlotWhere(SlotRole::BonusTile, kSeat0, 0).rect);
        EXPECT_EQ(PlateText(), NameOf(Hand().GetHand(kSeat0).bonus[0]));
        EXPECT_FALSE(PlateText().empty());
    }

    // ----- no name over a back, off the tiles -----

    TEST_F(MahjongTableHoverTest, AnOpponentsConcealedTileShowsNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        for (Seat seat = kSeat1; seat < kSeatCount; ++seat) {
            for (const TileSlot& slot : std::vector<TileSlot>(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end())) {
                if (slot.role != SlotRole::HandTile || slot.seat != seat) {
                    continue;
                }
                PointAt(slot.rect);
                EXPECT_EQ(PlateText(), "") << "seat " << seat << " tile " << slot.index;
            }
        }
    }

    TEST_F(MahjongTableHoverTest, ASpectatorSeesTheNamesOfTheOpenHandsAndTheDiscards) {
        ASSERT_NO_FATAL_FAILURE(StartSpectating());  // a watched match: every hand is face up
        const std::vector<TileSlot> slots(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end());
        int discards = 0;
        for (const TileSlot& slot : slots) {
            if (slot.role == SlotRole::HandTile) {
                PointAt(slot.rect);
                EXPECT_EQ(PlateText(), NameOf(Hand().GetHand(slot.seat).concealed[slot.index])) << "seat " << slot.seat << " tile " << slot.index;
            } else if (slot.role == SlotRole::DiscardTile && discards++ == 0) {
                PointAt(slot.rect);
                EXPECT_EQ(PlateText(), NameOf(Hand().Discards()[slot.index].tile));
            }
        }
        EXPECT_GT(discards, 0);
    }

    TEST_F(MahjongTableHoverTest, UnderTheHotSeatCoverTheViewersTilesShowNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, MakeModel(SpecOf(kPlainHand, "9p"))));
        _game->GetSession().Reveal();
        Draw();
        const int index = IndexOf(kSeat0, T("9p"));
        PointAt(SlotWhere(SlotRole::HandTile, kSeat0, index).rect);
        ASSERT_EQ(PlateText(), "9 Dots") << "uncovered, the person's own tiles show";

        ClickHandTile(index);  // the discard hands the table to the next person: the cover is up again
        Draw();
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_EQ(PlateText(), "") << "the pointer rests where it was and the cover is up";

        // The cover is a modal canvas: a motion over the tiles under it never reaches the table
        EXPECT_TRUE(HoverAt(100.0f, 650.0f));
        Draw();
        EXPECT_EQ(PlateText(), "");
    }

    TEST_F(MahjongTableHoverTest, AtTheEndOfTheHandAllFourHandsShowTheirNamesOnceTheScoreTableIsOutOfTheWay) {
        ASSERT_NO_FATAL_FAILURE(PlayTheHandOut());
        const int seat1Tile = 0;
        ASSERT_FALSE(Hand().GetHand(kSeat1).concealed.empty());
        const TileSlot slot = SlotWhere(SlotRole::HandTile, kSeat1, seat1Tile);

        // The score table is a modal canvas: it takes every motion, so over the tiles nothing reaches the table (the hold that
        // keeps it back for a moment after a lost claim is the one time the names are seen)
        EXPECT_TRUE(HoverAt(slot.rect.x + slot.rect.w * 0.5f, slot.rect.y + slot.rect.h * 0.5f));
        Draw();
        EXPECT_EQ(PlateText(), "");

        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "HandOver"_intern, false);
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "HandOverAction"_intern, false);
        for (Seat seat = kSeat0; seat < kSeatCount; ++seat) {
            const TileSlot tile = SlotWhere(SlotRole::HandTile, seat, seat1Tile);
            ASSERT_FALSE(HoverAt(tile.rect.x + tile.rect.w * 0.5f, tile.rect.y + tile.rect.h * 0.5f)) << "no canvas takes it";
            Draw();
            EXPECT_EQ(PlateText(), NameOf(Hand().GetHand(seat).concealed[seat1Tile])) << "seat " << seat;
        }
    }

    TEST_F(MahjongTableHoverTest, NothingIsShownOffTheTilesOrOnceThePointerLeavesATile) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        const TileSlot tile = SlotWhere(SlotRole::HandTile, kSeat0, 0);
        PointAt(tile.rect);
        ASSERT_NE(PlateText(), "");

        const TileSlot stack = FirstSlotOf(SlotRole::WallStack);
        const Rect plate = _table->GetLayout().zones.seatPlates[kSeat1];
        const Rect spots[] = {Rect(5.0f, 5.0f, 2.0f, 2.0f), stack.rect, plate, _table->GetLayout().zones.centerPanel};
        for (const Rect& spot : spots) {
            PointAt(tile.rect);
            ASSERT_NE(PlateText(), "");
            PointAt(spot);
            EXPECT_EQ(PlateText(), "") << "off the tiles at " << spot.x << "," << spot.y;
        }
    }

    // ----- what else takes the pointer -----

    TEST_F(MahjongTableHoverTest, ThePointerThatMovesOntoAButtonOfTheActionsCanvasTakesTheNameAway) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        const TileSlot tile = SlotWhere(SlotRole::HandTile, kSeat1, 0);
        PointAt(tile.rect);
        ASSERT_EQ(PlateText(), NameOf(Hand().GetHand(kSeat1).concealed[0]));

        const UIElement* pass = Element("Actions", "Pass");
        ASSERT_NE(pass, nullptr);
        ASSERT_TRUE(OnScreen("Actions", "Pass"));
        EXPECT_TRUE(HoverAt(pass->rect.x + pass->rect.w * 0.5f, pass->rect.y + pass->rect.h * 0.5f)) << "the canvas takes the motion: the table never hears of it";
        Draw();
        EXPECT_EQ(PlateText(), "");

        PointAt(tile.rect);
        EXPECT_EQ(PlateText(), NameOf(Hand().GetHand(kSeat1).concealed[0])) << "back on the tile";
    }

    TEST_F(MahjongTableHoverTest, WhileTheTableIsPausedThereIsNoNameAndAPointerThatWasOverTheOpenPanelIsForgotten) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        const TileSlot tile = SlotWhere(SlotRole::HandTile, kSeat0, 0);
        PointAt(tile.rect);
        ASSERT_NE(PlateText(), "");

        _game->OpenSetup();
        Draw();
        ASSERT_TRUE(_game->IsTablePaused());
        EXPECT_EQ(PlateText(), "");

        _game->CloseSetup();
        Draw();
        EXPECT_EQ(PlateText(), "") << "the pointer may have moved while the panel took it: wait for the next motion";

        PointAt(tile.rect);
        EXPECT_NE(PlateText(), "");
    }

    TEST_F(MahjongTableHoverTest, ATouchMotionIsNoHover) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        const TileSlot tile = SlotWhere(SlotRole::HandTile, kSeat0, 0);

        PointAt(tile.rect, SDL_TOUCH_MOUSEID);
        EXPECT_EQ(PlateText(), "");

        PointAt(tile.rect);
        ASSERT_NE(PlateText(), "");
        PointAt(tile.rect, SDL_PEN_MOUSEID);
        EXPECT_EQ(PlateText(), "") << "a finger or a pen after the mouse: there is no pointer to rest on the tile";
    }

    // ----- the plate -----

    TEST_F(MahjongTableHoverTest, ThePlateLiesInsideTheViewAndDoesNotCoverTheTileItNames) {
        ASSERT_NO_FATAL_FAILURE(StartSpectating());
        int checked = 0;
        const std::vector<TileSlot> slots(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end());
        for (const TileSlot& slot : slots) {
            if (slot.role == SlotRole::HandTile || slot.role == SlotRole::WallStack) {
                continue;
            }
            const float x = slot.rect.x + slot.rect.w * 0.5f;
            const float y = slot.rect.y + slot.rect.h * 0.5f;
            const TileSlot* top = SlotAt(_table->GetLayout(), x, y);
            ASSERT_NE(top, nullptr);
            if (top->role == SlotRole::HandTile) {
                continue;  // covered by a back
            }
            const TileSlot named = *top;
            PointAt(slot.rect);
            ASSERT_NE(PlateText(), "") << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            const Rect plate = PlateRect();
            EXPECT_GE(plate.x, 0.0f);
            EXPECT_GE(plate.y, 0.0f);
            EXPECT_LE(plate.x + plate.w, kViewWidth);
            EXPECT_LE(plate.y + plate.h, kViewHeight);
            EXPECT_TRUE(Apart(plate, named.rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
            ++checked;
        }
        EXPECT_GT(checked, 0);
    }

    TEST_F(MahjongTableHoverTest, ThePlateOfTheFirstAndTheLastTileOfTheViewersHandStaysInsideTheView) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        const int last = static_cast<int>(Hand().GetHand(kSeat0).concealed.size()) - 1;
        for (const int index : {0, last}) {
            const TileSlot tile = SlotWhere(SlotRole::HandTile, kSeat0, index);
            PointAt(tile.rect);
            ASSERT_NE(PlateText(), "") << index;
            const Rect plate = PlateRect();
            EXPECT_TRUE(Apart(plate, tile.rect)) << index;
            EXPECT_GE(plate.x, 0.0f) << index;
            EXPECT_LE(plate.x + plate.w, kViewWidth) << index;
            EXPECT_LE(plate.y + plate.h, kViewHeight) << index;
            EXPECT_LT(plate.y, tile.rect.y) << "the hand lies at the foot of the view: the plate goes above it";
        }
    }

    TEST_F(MahjongTableHoverTest, TheLongestNameOfEachLanguageFitsThePlate) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> candidates;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                candidates.push_back(TileName(CoreManager::GetUIStrings(), Tile{id}));
            }
            const eastl::string longest = LongestText(candidates);
            ASSERT_FALSE(longest.empty());
            CoreManager::GetUIVars().Set("tileHoverText"_intern, eastl::string_view(longest));
            UICanvasComponent::SetElementVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "TileHoverPlate"_intern, true);

            ExpectWordsFit("TileHover", kViewWidth, kViewHeight, 0.0f);
            ExpectWordsFit("TileHover", kViewWidth / 2.0f, kViewHeight / 2.0f, 0.0f);
            const UIElement* element = Element("TileHover", "TileHoverLabel");
            ASSERT_NE(element, nullptr);
            const auto* label = element->Cast<UILabel>();
            ASSERT_NE(label, nullptr);
            EXPECT_EQ(std::string(label->GetDisplayText().data(), label->GetDisplayText().size()), std::string(longest.data(), longest.size()));
            UICanvasComponent::SetElementVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "TileHoverPlate"_intern, false);
        });
    }

    // ----- hover changes nothing -----

    TEST_F(MahjongTableHoverTest, AFrameWithThePointerAtRestOrOffTheTableSetsNoVariable) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        PointAt(SlotWhere(SlotRole::HandTile, kSeat0, 0).rect);
        ASSERT_NE(PlateText(), "");

        uint64_t version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << "a pointer at rest on a tile";

        PointAt(Rect(5.0f, 5.0f, 2.0f, 2.0f));
        version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << "a pointer off the tiles";
    }

    TEST_F(MahjongTableHoverTest, HoveringChangesNothingInTheGame) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(SpecOf(kPlainHand, "9p"))));
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        const uint64_t version = _game->GetSession().Version();
        const std::string snapshot = Snapshot(Hand());
        const auto before = control->GetState();
        ASSERT_TRUE(before.has_value());

        const std::vector<TileSlot> slots(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end());
        for (const TileSlot& slot : slots) {
            PointAt(slot.rect);
        }
        Draw();

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(Snapshot(Hand()), snapshot);
        const auto after = control->GetState();
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(*before, *after);
    }

}  // namespace BECore::Mahjong::Tests
