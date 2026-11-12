#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "StringsTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Table/DangerMarks.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>
#include <Mahjong/Table/Words.h>

/**
 * The danger marks the hints draw on the viewer's hand (`Components/MahjongHintsComponent.h`, `Table/DangerMarks.h`): a bar inside the left
 * edge of a hand tile, green, amber or red, while an opponent has declared. The shipped scene is drawn into a `RecordingRenderer`; the table
 * is a played one (`FixedWall.h`) laid into the hand in progress. The mapping from the grades has its own test (`DangerMarksTest`).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // Seat 0 (East, the person) waits on the Ew with 123s 456s 789m 234p and draws the Rd (so the Ew and the Rd declare); seat 1 declares
        // with its discard of the Wd in the second lap, after which the 9m (seat 2) and the Ew (seat 3) were discarded.
        constexpr const char* kViewerHand = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";
        constexpr const char* kWaitingHand = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kSeat2Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kSeat3Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";
        // What those tiles are worth against seat 1, worked out by hand with the rules of `DiscardDanger.h`
        constexpr const char* kSafeTiles[] = {"Ew", "9m"};
        constexpr const char* kRiskyTiles[] = {"2s", "8m", "2p"};
        constexpr const char* kDangerousTiles[] = {"3s", "4s", "5s", "6s", "7m", "3p", "4p"};
        constexpr const char* kUnmarkedTiles[] = {"1s", "Rd"};
        constexpr size_t kSafeCount = std::size(kSafeTiles);
        constexpr size_t kRiskyCount = std::size(kRiskyTiles);
        constexpr size_t kDangerousCount = std::size(kDangerousTiles);

        constexpr SeatSetup kPersonsAt0And2{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};

        /// The rects share ground (touching edges, and the float noise of a scaled draw, do not count).
        bool Overlap(const Rect& a, const Rect& b) {
            constexpr float kEpsilon = 0.01f;
            return a.x + kEpsilon < b.x + b.w && b.x + kEpsilon < a.x + a.w && a.y + kEpsilon < b.y + b.h && b.y + kEpsilon < a.y + a.h;
        }

        bool Inside(const Rect& inner, const Rect& outer) {
            constexpr float kEpsilon = 0.01f;
            return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.x + inner.w <= outer.x + outer.w + kEpsilon && inner.y + inner.h <= outer.y + outer.h + kEpsilon;
        }

    }  // namespace

    class MahjongHintsDangerTest : public MahjongTableTestBase {
    protected:
        /// The table of the notes above; `declared` is false for a table where seat 1 has not declared, `viewerDeclares` makes seat 0 declare
        /// with its first discard. Seat 0 is to act with the Rd drawn.
        static HandModel PlayedModel(bool declared = true, bool viewerDeclares = false) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kViewerHand;
            spec.hands[kSeat1] = kWaitingHand;
            spec.hands[kSeat2] = kSeat2Hand;
            spec.hands[kSeat3] = kSeat3Hand;
            // A declared viewer would win on an Ew discarded by seat 3, which ends the lap: seat 3 discards a 2m instead
            const char* const last = viewerDeclares ? "2m" : "Ew";
            spec.live = std::string("Ew Rd Nw Ww Gd Wd 9m ") + last + " Rd 1p 2p";
            HandModel model = MakeModel(spec);
            if (viewerDeclares) {
                Lap(model, "Ew Rd Nw Ww", {kSeat0});
            } else {
                Lap(model, "Ew Rd Nw Ww");
            }
            const std::string second = std::string("Gd Wd 9m ") + last;
            if (declared) {
                Lap(model, second, {kSeat1});
            } else {
                Lap(model, second);
            }
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

        void StartPlayed(const HandModel& model, const SeatSetup& seats = kDefaultSeatSetup) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
        }

        /// The model was changed behind the session's back (`Lay`), which the component cannot see: a flip of the flag makes it look again.
        void Refresh() {
            const bool flag = _game->HintsOn();
            _game->SetHintsOn(!flag);
            Draw();
            _game->SetHintsOn(flag);
            Draw();
        }

        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        /// Every bar of the three danger colours, in draw order.
        std::vector<Rect> DangerBars() const {
            std::vector<Rect> bars;
            for (const Color& color : {kSafeMark, kRiskyMark, kDangerMark}) {
                for (const Rect& bar : RectsOf(color)) {
                    bars.push_back(bar);
                }
            }
            return bars;
        }

        /// The tiles of the viewer's hand: slot (the last layout) and tile.
        std::vector<std::pair<TileSlot, Tile>> HandTiles() const {
            std::vector<std::pair<TileSlot, Tile>> tiles;
            const PlayerHand& held = Hand().GetHand(_table->GetLayout().viewer);
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == _table->GetLayout().viewer) {
                    tiles.emplace_back(slot, held.concealed[slot.index]);
                }
            }
            return tiles;
        }

        /// The bars of `color` that lie inside the left half of `slot` (its danger bar; there is at most one).
        std::vector<Rect> BarsOnSlot(const Color& color, const Rect& slot) const {
            std::vector<Rect> found;
            for (const Rect& bar : RectsOf(color)) {
                if (Inside(bar, Rect(slot.x, slot.y, slot.w * 0.5f, slot.h))) {
                    found.push_back(bar);
                }
            }
            return found;
        }

        /// What the bar on every hand tile should be (a colour, none for an unmarked tile), from the lists above.
        static const Color* ColorOfTile(Tile tile) {
            for (const char* text : kSafeTiles) {
                if (tile == T(text)) {
                    return &kSafeMark;
                }
            }
            for (const char* text : kRiskyTiles) {
                if (tile == T(text)) {
                    return &kRiskyMark;
                }
            }
            for (const char* text : kDangerousTiles) {
                if (tile == T(text)) {
                    return &kDangerMark;
                }
            }
            return nullptr;
        }

        /// Every bar of a danger colour clear of every other bar of the table that lies on a hand tile.
        void ExpectClearOfTheOtherMarks(const std::string& where) const {
            const std::vector<Rect> bars = DangerBars();
            for (size_t i = 0; i < bars.size(); ++i) {
                for (size_t j = i + 1; j < bars.size(); ++j) {
                    EXPECT_FALSE(Overlap(bars[i], bars[j])) << where << ": two danger bars";
                }
                for (const Color& other : {kDeclareMark, kAdvisedFrame, kKeepsWaitingMark, kSeatWindMark, kRoundWindMark, kCallingFrame}) {
                    for (const Rect& mark : RectsOf(other)) {
                        EXPECT_FALSE(Overlap(bars[i], mark)) << where << ": a danger bar " << bars[i].x << "," << bars[i].y << " " << bars[i].w << "x" << bars[i].h << " over a mark of the hand "
                                                             << mark.x << "," << mark.y << " " << mark.w << "x" << mark.h << " rgba " << static_cast<int>(other.r) << "," << static_cast<int>(other.g)
                                                             << "," << static_cast<int>(other.b) << "," << static_cast<int>(other.a);
                    }
                }
            }
        }
    };

    TEST_F(MahjongHintsDangerTest, TheBarsAreExactlyOnTheMarkedTilesInTheColourOfTheirGrade) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        ASSERT_TRUE(HintsApply(_game->HintsOn(), _game->IsTablePaused(), _game->GetSession()));

        EXPECT_EQ(RectsOf(kSafeMark).size(), kSafeCount);
        EXPECT_EQ(RectsOf(kRiskyMark).size(), kRiskyCount);
        EXPECT_EQ(RectsOf(kDangerMark).size(), kDangerousCount);
        EXPECT_EQ(HandTiles().size(), 14u);
        for (const auto& [slot, tile] : HandTiles()) {
            const Color* expected = ColorOfTile(tile);
            size_t found = 0;
            for (const Color& color : {kSafeMark, kRiskyMark, kDangerMark}) {
                const std::vector<Rect> bars = BarsOnSlot(color, slot.rect);
                if (&color == expected) {
                    ASSERT_EQ(bars.size(), 1u) << ToString(tile);
                    EXPECT_FLOAT_EQ(bars[0].w, kDangerMarkWidth);
                    EXPECT_GT(bars[0].x, slot.rect.x) << ToString(tile) << ": inside the tile";
                    EXPECT_GT(bars[0].y, slot.rect.y + kWindMarkBarHeight - 0.01f) << ToString(tile) << ": under the wind bar";
                    EXPECT_LT(bars[0].y + bars[0].h, slot.rect.y + slot.rect.h - kKeepsWaitingBarHeight + 0.01f) << ToString(tile) << ": over the keeps-waiting bar";
                    EXPECT_GT(bars[0].h, 0.0f);
                    EXPECT_LE(bars[0].y + bars[0].h, kViewHeight);
                }
                found += bars.size();
            }
            EXPECT_EQ(found, expected != nullptr ? 1u : 0u) << ToString(tile);
        }
        for (const char* text : kUnmarkedTiles) {
            const Tile tile = T(text);
            for (const auto& [slot, held] : HandTiles()) {
                if (held == tile) {
                    EXPECT_EQ(ColorOfTile(held), nullptr);
                }
            }
        }
    }

    TEST_F(MahjongHintsDangerTest, TheBarsAreTheMarksOfTheViewersViewAndNothingElse) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        const auto marks = DangerMarks(MakeTableView(Hand(), kSeat0));
        size_t safe = 0;
        size_t risky = 0;
        size_t dangerous = 0;
        for (const auto& [slot, tile] : HandTiles()) {
            safe += marks[tile.Id()] == DangerMark::Safe ? 1 : 0;
            risky += marks[tile.Id()] == DangerMark::Risky ? 1 : 0;
            dangerous += marks[tile.Id()] == DangerMark::Dangerous ? 1 : 0;
        }
        EXPECT_EQ(RectsOf(kSafeMark).size(), safe);
        EXPECT_EQ(RectsOf(kRiskyMark).size(), risky);
        EXPECT_EQ(RectsOf(kDangerMark).size(), dangerous);
        EXPECT_GT(safe + risky + dangerous, 0u);
    }

    TEST_F(MahjongHintsDangerTest, NothingShowsWithTheHintsOffTheSetupOpenOrNobodyDeclared) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        ASSERT_FALSE(DangerBars().empty());

        _game->SetHintsOn(false);
        Draw();
        EXPECT_TRUE(DangerBars().empty()) << "hints off";
        _game->SetHintsOn(true);
        Draw();
        EXPECT_FALSE(DangerBars().empty());

        _game->OpenSetup();
        Draw();
        EXPECT_TRUE(DangerBars().empty()) << "Setup is open";
        _game->CloseSetup();
        Draw();
        EXPECT_FALSE(DangerBars().empty());

        Lay(PlayedModel(false));
        Refresh();
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        EXPECT_TRUE(DangerBars().empty()) << "nobody has declared";
    }

    TEST_F(MahjongHintsDangerTest, NothingShowsOffTheViewersTurn) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        ASSERT_FALSE(DangerBars().empty());

        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("Rd"))).has_value());
        Draw();
        ASSERT_FALSE(_game->GetSession().ActorIsPerson()) << "a computer acts now";
        EXPECT_TRUE(DangerBars().empty());
    }

    TEST_F(MahjongHintsDangerTest, UnderTheCoverNothingShowsAndRevealBringsTheBars) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel(), kPersonsAt0And2));
        ASSERT_TRUE(_game->GetSession().HandoffPending());
        EXPECT_TRUE(DangerBars().empty());

        _game->GetSession().Reveal();
        Refresh();
        EXPECT_EQ(DangerBars().size(), kSafeCount + kRiskyCount + kDangerousCount);
    }

    TEST_F(MahjongHintsDangerTest, TheRaisedTilesBarFollowsItAndTheOthersStay) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        int index = -1;
        for (const auto& [slot, tile] : HandTiles()) {
            index = tile == T("4p") ? slot.index : index;
        }
        ASSERT_GE(index, 0);
        const Rect slotRect = HandSlot(index)->rect;
        const std::vector<Rect> before = BarsOnSlot(kDangerMark, slotRect);
        ASSERT_EQ(before.size(), 1u);
        const size_t bars = DangerBars().size();

        TapHandTile(index);
        Draw();

        ASSERT_EQ(_table->SelectedHandIndex(), index);
        EXPECT_EQ(DangerBars().size(), bars) << "a selection adds and drops no bar";
        const Rect raised = RaisedRect(slotRect);
        const float rise = raised.y - slotRect.y;
        ASSERT_LT(rise, 0.0f);
        bool follows = false;
        bool stayed = false;
        for (const Rect& bar : RectsOf(kDangerMark)) {
            follows = follows || SameRect(bar, Rect(before[0].x, before[0].y + rise, before[0].w, before[0].h));
            stayed = stayed || SameRect(bar, before[0]);
        }
        EXPECT_TRUE(follows) << "the bar of the 4p rises with the tile";
        EXPECT_FALSE(stayed);
        ASSERT_NO_FATAL_FAILURE(ExpectClearOfTheOtherMarks("raised"));
    }

    TEST_F(MahjongHintsDangerTest, TheBarsAreClearOfTheDeclareBarTheWindBarsAndTheAdvisedFrameInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        EmitAction("MahjongHintAdvice");
        Draw();
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());

        size_t windows = 0;
        ForEachTableWindow([&](const MatrixWindow& window) {
            SetWindow(*_renderer, window);
            Draw();
            ++windows;
            EXPECT_EQ(DangerBars().size(), kSafeCount + kRiskyCount + kDangerousCount) << window.label.c_str();
            EXPECT_FALSE(RectsOf(kDeclareMark).empty()) << window.label.c_str() << ": the Ew and the Rd declare";
            EXPECT_EQ(RectsOf(kAdvisedFrame).size(), 4u) << window.label.c_str();
            EXPECT_FALSE(RectsOf(kSeatWindMark).empty()) << window.label.c_str() << ": the Ew is the own wind";
            for (const auto& [slot, tile] : HandTiles()) {
                for (const Color& color : {kSafeMark, kRiskyMark, kDangerMark}) {
                    for (const Rect& bar : BarsOnSlot(color, slot.rect)) {
                        EXPECT_TRUE(Inside(bar, slot.rect)) << window.label.c_str() << " " << ToString(tile);
                        EXPECT_GT(bar.h, 0.0f) << window.label.c_str();
                    }
                }
            }
            ExpectClearOfTheOtherMarks(window.label.c_str());
        });
        EXPECT_GT(windows, 0u);
    }

    TEST_F(MahjongHintsDangerTest, ADeclaredViewerKeepsItsKeepsWaitingBarClearOfTheDangerBars) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel(true, true)));
        ASSERT_TRUE(Hand().GetHand(kSeat0).callingDeclared);

        EXPECT_FALSE(RectsOf(kKeepsWaitingMark).empty());
        EXPECT_FALSE(DangerBars().empty());
        ASSERT_NO_FATAL_FAILURE(ExpectClearOfTheOtherMarks("declared viewer"));
        // The Rd and the Ew keep the declared hand waiting; the danger bars of the other tiles stay clear of those bars
        EXPECT_EQ(RectsOf(kKeepsWaitingMark).size(), 2u) << "the Rd and the Ew keep the hand waiting";
        EXPECT_EQ(RectsOf(kDangerMark).size(), kDangerousCount);
    }

    TEST_F(MahjongHintsDangerTest, TheNameOfAHoveredHandTileSaysWhatItsBarMeansInTheBarsColour) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        const auto marks = DangerMarks(MakeTableView(Hand(), kSeat0));
        size_t worded = 0;
        for (const auto& [slot, tile] : HandTiles()) {
            SCOPED_TRACE(ToString(tile));
            HoverAt(slot.rect.x + slot.rect.w * 0.5f, slot.rect.y + slot.rect.h * 0.5f);
            Draw();
            Draw();  // the hints draw after the table: the word follows the bar within a frame
            ASSERT_TRUE(OnScreen("TileHover", "TileHoverPlate"));
            const DangerMark mark = marks[tile.Id()];
            if (mark == DangerMark::None) {
                EXPECT_FALSE(OnScreen("TileHover", "TileHoverDanger")) << "no bar, no line";
                EXPECT_EQ(Var("tileHoverDanger"), "");
                continue;
            }
            ++worded;
            ASSERT_TRUE(OnScreen("TileHover", "TileHoverDanger"));
            const eastl::string expected = Tr(CoreManager::GetUIStrings(), DangerHoverKey(mark));
            EXPECT_EQ(Var("tileHoverDanger"), std::string(expected.data(), expected.size()));
            const auto* label = Element("TileHover", "TileHoverDanger")->GetComponent<UITextComponent>();
            ASSERT_NE(label, nullptr);
            EXPECT_TRUE(SameColor(label->textColor, DangerMarkColor(mark)));
            EXPECT_TRUE(Inside(Element("TileHover", "TileHoverDanger")->rect, Element("TileHover", "TileHoverPlate")->rect));
        }
        EXPECT_EQ(worded, kSafeCount + kRiskyCount + kDangerousCount);

        // Off the tiles the plate goes, and with it the line
        HoverAt(4.0f, 4.0f);
        Draw();
        EXPECT_FALSE(OnScreen("TileHover", "TileHoverDanger"));
        _game->SetHintsOn(false);
        const auto [slot, tile] = HandTiles().front();
        HoverAt(slot.rect.x + slot.rect.w * 0.5f, slot.rect.y + slot.rect.h * 0.5f);
        Draw();
        Draw();
        EXPECT_TRUE(OnScreen("TileHover", "TileHoverPlate"));
        EXPECT_FALSE(OnScreen("TileHover", "TileHoverDanger")) << "hints off: no bar, no line";
    }

    TEST_F(MahjongHintsDangerTest, TheLongestLineOfTheBarsFitsTheHoverPlateInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(StartPlayed(PlayedModel()));
        Scene& scene = *_manager->GetSceneByName("MahjongScene"_intern);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> lines;
            for (const DangerMark mark : {DangerMark::Safe, DangerMark::Risky, DangerMark::Dangerous}) {
                lines.push_back(Tr(CoreManager::GetUIStrings(), DangerHoverKey(mark)));
            }
            const eastl::string longest = LongestText(lines);
            CoreManager::GetUIVars().Set("tileHoverText"_intern, eastl::string_view("9 Dots"));
            CoreManager::GetUIVars().Set("tileHoverDanger"_intern, eastl::string_view(longest));
            UIElement* plate = Element("TileHover", "TileHoverPlate");
            plate->style.rect.height = UILength::Px(92.0f);
            UICanvasComponent::SetElementVisibleByName(scene, "TileHoverPlate"_intern, true);
            UICanvasComponent::SetElementVisibleByName(scene, "TileHoverDanger"_intern, true);
            ExpectWordsFit("TileHover", kViewWidth, kViewHeight, 0.0f);
            ExpectWordsFit("TileHover", kViewWidth / 2.0f, kViewHeight / 2.0f, 0.0f);
            UICanvasComponent::SetElementVisibleByName(scene, "TileHoverPlate"_intern, false);
        });
    }

    TEST_F(MahjongHintsDangerTest, TheThreeColoursAreRolesOfTheirOwnApartFromEveryOtherMark) {
        const Color danger[] = {kSafeMark, kRiskyMark, kDangerMark};
        for (size_t i = 0; i < std::size(danger); ++i) {
            EXPECT_FALSE(danger[i].r == kTurnMark.r && danger[i].g == kTurnMark.g && danger[i].b == kTurnMark.b) << i << ": not in the gold family";
            for (size_t j = i + 1; j < std::size(danger); ++j) {
                EXPECT_FALSE(SameColor(danger[i], danger[j]));
            }
            for (const Color& other : {kTurnMark, kPendingFrame, kLastDiscardMark, kDrawnTileMark, kCallingFrame, kClaimedEdge, kClaimHighlight, kClaimLostFrame, kDeclareMark, kAdvisedFrame,
                                       kKeepsWaitingMark, kSeatTurnFrame, kSeatWindMark, kRoundWindMark, kDealerMark, kPanelDealerMark, kPanelTurnLight, kVeil}) {
                EXPECT_FALSE(SameColor(danger[i], other)) << i;
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
