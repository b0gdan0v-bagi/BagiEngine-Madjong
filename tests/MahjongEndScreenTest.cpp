#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/Text/Utf8.h>
#include <BECore/UI/UIImageComponent.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>

/**
 * The screens that end a hand and a match in the shipped scene (`Components/MahjongHudComponent.h`): the score table `HandOver` is shown
 * exactly while the hand is over and says what `FormatScoreTable` says, `Next hand` settles it, the last hand leads to the standings
 * `MatchOver`, `New game` opens the seat choice again, and both panels pass the size matrix with the longest rows.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The worst numbers a screen can show: a limit-sized score, a big change and total, a long chain of doubles.
        constexpr int kWorstScore = 500;
        constexpr int kWorstDelta = -4000;
        constexpr int kWorstTotal = 12000;
        constexpr int kWorstPoints = 64;
        constexpr int kWorstDoubles = 12;
        constexpr int kWorstLinePoints = 32;  // the most one line can give: a concealed kong of terminals (2 x 2 x 2 x 4)
        constexpr float kButtonAir = 8.0f;
        /// The shape of the score table (see `config/nodes/MahjongHandOver.xml`): the plate lies in the ground the hands leave free, so all
        /// four hands stay readable under it. The edges are those of the hands, melds and bonus tiles of the worst-case table.
        constexpr float kScoreTableLeft = 119.0f;
        constexpr float kScoreTableRight = 1161.0f;
        constexpr float kScoreTableTop = 96.0f;
        constexpr float kScoreTableBottom = 612.0f;
        constexpr float kScoreTableHeight = 508.0f;     // the phone profile's large hand starts at y 609.6, so the plate ends at 606
        constexpr float kScoreCoachRowHeight = 24.0f;   // HandCoachRow, the coach's tally: hidden unless the HUD has a line for it
        constexpr float kScoreButtonGapHeight = 64.0f;  // the gap of the plate that the Next hand button sits in
        /// The table of the plate: a caption row and four seat rows 4 apart (`ScoreTable`), and under the gap `Details` (the winning tiles, the lines,
        /// the coach's row and the summary, 4 apart: the plate is exactly full with the tally row shown).
        constexpr float kScoreCaptionHeight = 24.0f;
        constexpr float kScoreRowHeight = 28.0f;
        constexpr float kScoreTableGap = 4.0f;
        constexpr float kScoreTilesHeight = 32.0f;
        constexpr float kScoreTileWidth = 24.0f;
        constexpr float kScoreLineRowHeight = 20.0f;
        constexpr float kScoreSummaryHeight = 24.0f;
        constexpr int kScoreColumns = 5;  // seat, wind, hand, change, total
        /// The same in the portrait view: the plate lies between the bonus columns of the side seats (x 94..118 and 602..626, y 308..452) and above the
        /// viewer's bonus tiles (from y 1112); two columns of lines and a taller gap, for the ratio of the window scale to the table's fit of 1.6
        constexpr float kPortraitScoreTableLeft = 124.0f;
        constexpr float kPortraitScoreTableRight = 596.0f;
        constexpr float kPortraitScoreTableTop = 92.0f;
        constexpr float kPortraitScoreTableBottom = 1108.0f;
        constexpr float kPortraitScoreTableHeight = 760.0f;
        constexpr float kPortraitScoreRowHeight = 32.0f;
        constexpr float kPortraitScoreCoachRowHeight = 48.0f;  // two lines of 18
        constexpr float kPortraitScoreBandHeight = 72.0f;      // the headline and the summary: two lines of the narrow plate
        constexpr float kPortraitScoreButtonGapHeight = 112.0f;
        constexpr int kPortraitScoreLineColumns = 2;
        /// The same in the narrow view: between the columns of the side seats (x 8..112 and 968..1072, their bonus tiles the inner ones), under the
        /// top seat's rows (to y 90) and above the viewer's melds and bonus tiles (from y 636); the plate is 840 wide, 508 high
        constexpr float kNarrowScoreTableLeft = 120.0f;
        constexpr float kNarrowScoreTableRight = 960.0f;
        constexpr float kNarrowScoreTableTop = 92.0f;
        constexpr float kNarrowScoreTableBottom = 630.0f;  // 6 above the viewer's row
        constexpr float kNarrowScoreTableWidth = 840.0f;
        /// The edges the plate of the score table keeps to in a view of this shape (the wide view is the landscape table a wing in from the
        /// side, and the plate is centred on the view, so its edges move by the wing).
        Rect ScoreTableBounds(TableShape shape) {
            if (shape == TableShape::Portrait) {
                return Rect(kPortraitScoreTableLeft, kPortraitScoreTableTop, kPortraitScoreTableRight - kPortraitScoreTableLeft, kPortraitScoreTableBottom - kPortraitScoreTableTop);
            }
            if (shape == TableShape::Narrow) {
                return Rect(kNarrowScoreTableLeft, kNarrowScoreTableTop, kNarrowScoreTableRight - kNarrowScoreTableLeft, kNarrowScoreTableBottom - kNarrowScoreTableTop);
            }
            const float wing = shape == TableShape::Wide ? kWideWingWidth : 0.0f;
            return Rect(kScoreTableLeft + wing, kScoreTableTop, kScoreTableRight - kScoreTableLeft, kScoreTableBottom - kScoreTableTop);
        }
        constexpr int kScoreLineColumns = 4;
        constexpr float kScoreHeadlineHeight = 52.0f;
        constexpr float kScoreNextHeight = 56.0f;
        constexpr float kScoreNextWidth = 176.0f;
        constexpr float kMinControlHeight = 48.0f;
        /// The standings lie on a Screen canvas that a 480 px window at the user scale 1.5 makes 320 units high (296 with a notch), so
        /// the plate keeps its rows 4 apart; the button is still 16 below them (a spacer of 8 between two gaps of 4).
        constexpr float kStandingsGap = 4.0f;
        constexpr float kStandingsPlateHeight = 296.0f;
        constexpr float kMatchCoachRowHeight = 28.0f;  // MatchCoachRow: the plate (296) pays for it inside, so its rows are 24 high at 18
        constexpr int kHandLimit = 60;                 // hands of a 4-hand match with replayed draws
        /// Output sizes the words are asked in: the design size, a small window, the narrowest windows and a wide short one.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        /// The worst case of the score table's shape: sixteen lines, the tally row and the eighteen tiles of a hand of four kongs, all shown.
        void ShowTheWorstScoreTable(UICanvasComponent& canvas) {
            for (int i = 0; i < kMaxScoreLines; ++i) {
                ASSERT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
            }
            ASSERT_TRUE(canvas.SetElementVisible("HandCoachRow"_intern, true));
            ASSERT_TRUE(canvas.SetElementVisible("WinTiles"_intern, true));
            for (int i = 0; i < kMaxWinningTiles; ++i) {
                ASSERT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("WinTile{}", i)), true));
            }
        }

        /// The longest texts of the screens in the current language of `strings`: the real formatters over every winner, East, kind of win, seat of the
        /// discarder, seat of the one person (the mark of "you") and winning tile, the longest found by code points.
        /// The tiles a line of this kind can name: a set or a pair any tile of the hand, a bonus line a flower or a season, a dragon set a dragon, a wind set a wind.
        bool CanNameTile(ScoreKind kind, Tile tile) {
            switch (kind) {
                case ScoreKind::Pung:
                case ScoreKind::Kong:
                case ScoreKind::Pair:
                    return !tile.IsBonus();
                case ScoreKind::Bonus:
                case ScoreKind::OwnBonus:
                case ScoreKind::RoundBonus:
                    return tile.IsBonus();
                case ScoreKind::DragonSet:
                    return tile.IsDragon();
                case ScoreKind::OwnWindSet:
                case ScoreKind::RoundWindSet:
                    return tile.IsWind();
                default:
                    return false;
            }
        }

        /// The tile of the longest name among those `candidate` allows (by code points); `Tile{}` when none.
        Tile LongestTile(const UIStrings& strings, const std::function<bool(Tile)>& candidate) {
            Tile longest;
            size_t most = 0;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const size_t length = candidate(Tile(id)) ? Utf8::CountCodePoints(TileName(strings, Tile(id))) : 0;
                if (length > most) {
                    most = length;
                    longest = Tile(id);
                }
            }
            return longest;
        }

        /// The cell of a row of the score table by column: name, wind, hand, change, total.
        const eastl::string& CellOf(const ScoreRow& row, int column) {
            const eastl::string* const cells[kScoreColumns] = {&row.name, &row.wind, &row.hand, &row.change, &row.total};
            return *cells[column];
        }

        struct WorstTexts {
            eastl::string headline;
            eastl::array<eastl::string, kScoreColumns> cells;  // the longest text of each column of the score table
            eastl::string summary;
            eastl::string standing;
            eastl::string standingsHeadline;
            eastl::string handTally;   // the longest line of the coach's tally under the score table
            eastl::string matchTally;  // and under the standings
            Tile tile;                 // the winning tile with the longest name (not a flower)
        };

        WorstTexts FindWorstTexts(const UIStrings& strings) {
            WorstTexts worst;
            worst.tile = LongestTile(strings, [](Tile tile) { return !tile.IsBonus(); });  // a winning tile is never a flower

            std::vector<eastl::string> headlines;
            std::array<std::vector<eastl::string>, kScoreColumns> columns;
            std::vector<eastl::string> summaries;
            for (Seat winner = 0; winner < kSeatCount; ++winner) {
                for (Seat east = 0; east < kSeatCount; ++east) {
                    HandSettlement settlement;
                    settlement.kind = ResultKind::Win;
                    settlement.winner = winner;
                    settlement.east = east;
                    settlement.scores.fill(kWorstScore);
                    settlement.deltas.fill(kWorstDelta);
                    ScoreBreakdown& breakdown = settlement.breakdowns[winner];
                    breakdown.won = true;
                    breakdown.points = kWorstPoints;
                    breakdown.doubles = kWorstDoubles;
                    breakdown.raw = kWorstPoints << kWorstDoubles;
                    breakdown.score = kWorstScore;
                    SeatScores totals;
                    totals.fill(kWorstTotal);
                    for (const WinSource source : {WinSource::SelfDraw, WinSource::Discard, WinSource::RobbedKong}) {
                        for (Seat from = 0; from < kSeatCount; ++from) {
                            for (Seat person = 0; person < kSeatCount; ++person) {
                                SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                                seats[person] = SeatKind::Person;
                                HandResult result;
                                result.kind = ResultKind::Win;
                                result.winner = winner;
                                result.winningTile = worst.tile;
                                result.source = source;
                                result.from = from;
                                const ScoreTable table = FormatScoreTable(strings, settlement, result, totals, seats);
                                headlines.push_back(table.headline);
                                summaries.push_back(table.summary);
                                for (const ScoreRow& row : table.rows) {
                                    for (int column = 0; column < kScoreColumns; ++column) {
                                        columns[static_cast<size_t>(column)].push_back(CellOf(row, column));
                                    }
                                }
                            }
                        }
                    }
                }
            }
            worst.headline = LongestText(headlines);
            for (int column = 0; column < kScoreColumns; ++column) {
                worst.cells[static_cast<size_t>(column)] = LongestText(columns[static_cast<size_t>(column)]);
            }
            worst.summary = LongestText(summaries);

            // The standings: a row with the last place and the biggest score, the headline of three tied seats and of a single winner
            std::vector<eastl::string> standings;
            std::vector<eastl::string> standingsHeadlines;
            for (Seat person = 0; person < kSeatCount; ++person) {
                SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                seats[person] = SeatKind::Person;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    standings.push_back(FormatStandingSentence(strings, StandingRow{4, SeatLabel(strings, seat, seats), Format("{}", kWorstTotal), false}));
                }
                for (const SeatScores& deltas : {SeatScores{-300, 100, 100, 100}, SeatScores{100, -300, 100, 100}, SeatScores{100, 100, -300, 100}, SeatScores{100, 100, 100, -300},
                                                 SeatScores{200, 100, 0, -300}, SeatScores{-300, 0, 100, 200}}) {
                    Match match;
                    EXPECT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
                    HandSettlement settlement;
                    settlement.kind = ResultKind::Drawn;
                    settlement.deltas = deltas;
                    EXPECT_TRUE(match.ApplySettlement(settlement).has_value());
                    standingsHeadlines.push_back(FormatStandings(strings, match, seats).headline);
                }
            }
            worst.standing = LongestText(standings);
            worst.standingsHeadline = LongestText(standingsHeadlines);
            constexpr CoachTally kBig{999, 999};
            // The row also says how many of the room have read the plate
            const eastl::string ready = Tr(strings, "online.table.ready", {{"n", Format("{}", kSeatCount)}, {"total", Format("{}", kSeatCount)}});
            worst.handTally = LongestText({FormatCoachTally(strings, kBig, kBig), ready});
            worst.matchTally = FormatCoachTally(strings, CoachTally{}, kBig);
            return worst;
        }

    }  // namespace

    class MahjongEndScreenTest : public MahjongTableTestBase {
    protected:
        /// Expects the rows of the table and the tiles to lie inside the plate's inner area and the cells inside their row.
        void ExpectTheTableInsideThePlate(const Rect& plate, const char* label) const {
            const Rect inner(plate.x + kModalPadding, plate.y + kModalPadding, plate.w - 2.0f * kModalPadding, plate.h - 2.0f * kModalPadding);
            auto inside = [&](const Rect& rect, const Rect& frame, const eastl::string& what) {
                EXPECT_GE(rect.x, frame.x - 0.01f) << label << ": " << what.c_str();
                EXPECT_LE(rect.x + rect.w, frame.x + frame.w + 0.01f) << label << ": " << what.c_str();
                EXPECT_GE(rect.y, frame.y - 0.01f) << label << ": " << what.c_str();
                EXPECT_LE(rect.y + rect.h, frame.y + frame.h + 0.01f) << label << ": " << what.c_str();
            };
            for (const char* name : {"ScoreTable", "ScoreHeader", "ButtonGap", "Details", "WinTiles", "Lines", "HandCoachRow", "Summary"}) {
                const UIElement* element = Element("HandOver", name);
                ASSERT_NE(element, nullptr) << name;
                inside(element->rect, inner, name);
            }
            for (int row = 0; row < kSeatCount; ++row) {
                const UIElement* seatRow = Element("HandOver", Format("SeatRow{}", row).c_str());
                ASSERT_NE(seatRow, nullptr) << row;
                inside(seatRow->rect, inner, Format("SeatRow{}", row));
                for (int column = 0; column < kScoreColumns; ++column) {
                    const UIElement* cell = Element("HandOver", Format("ScoreCell{}_{}", row, column).c_str());
                    ASSERT_NE(cell, nullptr);
                    inside(cell->rect, seatRow->rect, Format("ScoreCell{}_{}", row, column));
                }
            }
            for (int i = 0; i < kMaxWinningTiles; ++i) {
                const UIElement* tile = Element("HandOver", Format("WinTile{}", i).c_str());
                ASSERT_NE(tile, nullptr) << i;
                inside(tile->rect, Element("HandOver", "WinTiles")->rect, Format("WinTile{}", i));
            }
        }

        void StartShortMatch(const SeatSetup& seats, uint32_t seed = kSeed) {
            MahjongSettings settings = _game->GetSettings();
            settings.handsPerMatch = kRoundsPerMatch;
            _game->SetSettings(settings);
            ASSERT_NO_FATAL_FAILURE(Start(seats, seed));
        }

        /// Plays the hand to its end between computers and draws.
        void PlayHandOver() {
            _game->GetSession().RunComputers();
            ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
            Draw();
        }

        std::string Text(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            return DisplayText(found);
        }

        bool Shown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        /// The settlement the screen of the hand in progress must show, worked out from the model here.
        ScoreTable ExpectedTable() const {
            const HandSettlement settlement = SettleHand(Hand());
            SeatScores totals = _game->GetMatch().Scores();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                totals[seat] += settlement.deltas[seat];
            }
            const PlayerHand* winnerHand = IsSeat(settlement.winner) ? &Hand().GetHand(settlement.winner) : nullptr;
            return FormatScoreTable(CoreManager::GetUIStrings(), settlement, *Hand().Result(), totals, _game->GetSession().Seats(), winnerHand);
        }

        /// The person at seat 0 plays the hand on: the computers run, and each turn of the person takes the first action on offer. Returns how
        /// many actions of the person were accepted (each is a move the coach counts, whether it agrees or not).
        int PlayHandAsPerson() {
            TableSession& session = _game->GetSession();
            int moves = 0;
            for (int guard = 0; guard < 600 && session.GetPhase() == SessionPhase::Playing; ++guard) {
                session.RunComputers();
                if (session.GetPhase() != SessionPhase::Playing) {
                    break;
                }
                const TableView view = MakeTableView(Hand(), session.Actor());
                EXPECT_FALSE(view.legal.empty());
                if (view.legal.empty()) {
                    break;
                }
                moves += session.PerformPerson(view.legal.front()).has_value() ? 1 : 0;
            }
            return moves;
        }

        /// The tally line a hand of the person, shown now, must carry: from the ledger, in the current language.
        eastl::string ExpectedHandTally() const {
            const CoachLedger& coach = _game->GetCoach();
            const TableSession& session = _game->GetSession();
            return FormatCoachTally(CoreManager::GetUIStrings(), coach.HandTallyOf(HandKeyOf(session)), coach.MatchTallyOf(session.StartCount()));
        }

        /// Plays whole hands (the person clicking `Next hand` each time) until the match is over.
        void PlayMatchToTheEnd() {
            for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                _game->GetSession().RunComputers();
                ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            }
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        }
    };

    // ----- the score table of a hand -----

    TEST_F(MahjongEndScreenTest, TheScoreTableIsShownExactlyWhileTheHandIsOverAndHiddenAfterNextHand) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_FALSE(Shown("HandOver")) << "a hand in play";
        EXPECT_FALSE(Shown("HandOverAction")) << "a hand in play";
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_TRUE(Shown("HandOverAction"));
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
    }

    TEST_F(MahjongEndScreenTest, TheVariablesAndTheLabelsSayWhatTheScoreTableSays) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());

        const ScoreTable expected = ExpectedTable();
        EXPECT_EQ(Var("handOverHeadline"), expected.headline.c_str());
        EXPECT_EQ(Var("handOverSummary"), expected.summary.c_str());
        EXPECT_EQ(Text("HandOver", "Headline"), expected.headline.c_str());
        EXPECT_EQ(Text("HandOver", "Summary"), expected.summary.c_str());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (int column = 0; column < kScoreColumns; ++column) {
                const std::string name = Format("handOverCell{}_{}", seat, column).c_str();
                EXPECT_EQ(Var(name.c_str()), CellOf(expected.rows[seat], column).c_str());
                EXPECT_EQ(Text("HandOver", Format("ScoreCell{}_{}", seat, column).c_str()), CellOf(expected.rows[seat], column).c_str());
            }
        }
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(expected.lines.size());
            EXPECT_EQ(Var(Format("scoreLine{}", i).c_str()), has ? expected.lines[i].c_str() : "") << "line " << i;
            EXPECT_EQ(OnScreen("HandOver", Format("ScoreLine{}", i).c_str()), has) << "line " << i << " is visible exactly when it has text";
        }
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Next hand");
    }

    TEST_F(MahjongEndScreenTest, TheWinnersRowIsLitAndTheWinningTilesAreShownAsPicturesWithTheWinningTileMarked) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        ASSERT_EQ(Hand().Result()->kind, ResultKind::Win) << "this seed ends in a mahjong";
        const ScoreTable expected = ExpectedTable();
        ASSERT_EQ(expected.winner, Hand().Result()->winner);

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(OnScreen("HandOver", Format("WinnerRow{}", seat).c_str()), seat == expected.winner) << "exactly the winner's row is lit: seat " << seat;
        }
        ASSERT_FALSE(expected.winningTiles.empty());
        EXPECT_EQ(expected.winningTiles.back(), Hand().Result()->winningTile) << "the winning tile is the last";
        EXPECT_TRUE(OnScreen("HandOver", "WinTiles"));
        for (int i = 0; i < kMaxWinningTiles; ++i) {
            const bool has = i < static_cast<int>(expected.winningTiles.size());
            EXPECT_EQ(OnScreen("HandOver", Format("WinTile{}", i).c_str()), has) << "tile " << i << " is shown exactly when the hand has it";
            EXPECT_EQ(OnScreen("HandOver", Format("WinTileMark{}", i).c_str()), has && i + 1 == static_cast<int>(expected.winningTiles.size())) << "only the winning tile is marked: " << i;
            if (has) {
                const UIElement* tile = Element("HandOver", Format("WinTile{}", i).c_str());
                ASSERT_NE(tile, nullptr);
                const UIImageComponent* image = tile->GetComponent<UIImageComponent>();
                ASSERT_NE(image, nullptr);
                EXPECT_EQ(image->sprite, PoolString::Intern(SpriteName(expected.winningTiles[static_cast<size_t>(i)]))) << "tile " << i;
            }
        }
        EXPECT_EQ(
            expected.winningTiles.size(), Hand().GetHand(expected.winner).concealed.size() + 3 * Hand().GetHand(expected.winner).melds.size() +
                                              [&] {
                                                  size_t kongs = 0;
                                                  for (const Meld& meld : Hand().GetHand(expected.winner).melds) {
                                                      kongs += meld.kind == MeldKind::Kong ? 1 : 0;
                                                  }
                                                  return kongs;
                                              }())
            << "all the tiles of the hand: its melds as laid, the concealed tiles with the winning tile in them";
    }

    TEST_F(MahjongEndScreenTest, ADrawnHandLightsNoRowShowsNoTilesAndHasDashesInItsCells) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        HandState state = Hand().Capture();
        state.result = HandResult{};
        Lay(HandModel::ForDisplay(state));
        Draw();
        ASSERT_TRUE(Shown("HandOver"));
        ASSERT_EQ(Hand().Result()->kind, ResultKind::Drawn);

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(OnScreen("HandOver", Format("WinnerRow{}", seat).c_str())) << seat;
            EXPECT_EQ(Var(Format("handOverCell{}_2", seat).c_str()), "-");
            EXPECT_EQ(Var(Format("handOverCell{}_3", seat).c_str()), "-");
            EXPECT_EQ(Text("HandOver", Format("ScoreCell{}_2", seat).c_str()), "-");
        }
        EXPECT_FALSE(OnScreen("HandOver", "WinTiles")) << "no winning hand, no row of tiles";
        for (int i = 0; i < kMaxWinningTiles; ++i) {
            EXPECT_FALSE(OnScreen("HandOver", Format("WinTile{}", i).c_str())) << i;
            EXPECT_FALSE(OnScreen("HandOver", Format("WinTileMark{}", i).c_str())) << i;
        }
    }

    TEST_F(MahjongEndScreenTest, AnUnchangedFrameOfTheScoreTableMovesNoVariable) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << "the cells are published when the session or the language moved, not per frame";
    }

    TEST_F(MahjongEndScreenTest, ALanguageSwitchWhileTheScoreTableIsUpChangesItsTextAtTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const ScoreTable english = ExpectedTable();
        ASSERT_EQ(Text("HandOver", "Headline"), english.headline.c_str());
        const uint64_t version = _game->GetSession().Version();

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();

        const ScoreTable russian = ExpectedTable();
        EXPECT_NE(russian.headline, english.headline);
        EXPECT_EQ(Text("HandOver", "Headline"), russian.headline.c_str());
        EXPECT_EQ(Text("HandOver", "Summary"), russian.summary.c_str());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (int column = 0; column < kScoreColumns; ++column) {
                EXPECT_EQ(Text("HandOver", Format("ScoreCell{}_{}", seat, column).c_str()), CellOf(russian.rows[seat], column).c_str());
            }
        }
        for (int column = 0; column < kScoreColumns; ++column) {
            EXPECT_EQ(Text("HandOver", Format("ScoreCol{}", column).c_str()), russian.columns[static_cast<size_t>(column)].c_str()) << "the captions follow the language";
        }
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(russian.lines.size());
            EXPECT_EQ(Var(Format("scoreLine{}", i).c_str()), has ? russian.lines[i].c_str() : "") << "line " << i;
            EXPECT_EQ(OnScreen("HandOver", Format("ScoreLine{}", i).c_str()), has) << "the same lines are visible in the new language";
        }
        EXPECT_NE(Text("HandOverAction", "NextHand"), "Next hand");
        EXPECT_EQ(Text("HandOverAction", "NextHand").size(), Tr(CoreManager::GetUIStrings(), "handover.next").size());
        EXPECT_EQ(_game->GetSession().Version(), version) << "the game did not move";
        EXPECT_TRUE(Shown("HandOver"));

        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Text("HandOver", "Headline"), english.headline.c_str());
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Next hand");
    }

    TEST_F(MahjongEndScreenTest, AWonHandShowsTheWinnersLinesAndADrawnHandShowsNone) {
        bool sawWin = false;
        bool sawDraw = false;
        for (uint32_t seed = 1; seed <= 40 && !(sawWin && sawDraw); ++seed) {
            ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, seed));
            ASSERT_NO_FATAL_FAILURE(PlayHandOver());
            const bool win = Hand().Result()->kind == ResultKind::Win;
            (win ? sawWin : sawDraw) = true;
            const ScoreTable expected = ExpectedTable();
            if (win) {
                EXPECT_FALSE(expected.lines.empty());
                EXPECT_TRUE(OnScreen("HandOver", "ScoreLine0")) << "seed " << seed;
                EXPECT_EQ(Text("HandOver", "ScoreLine0"), expected.lines[0].c_str());
            } else {
                EXPECT_EQ(Text("HandOver", "Headline"), "Drawn hand");
                EXPECT_FALSE(OnScreen("HandOver", "ScoreLine0")) << "seed " << seed;
            }
        }
        EXPECT_TRUE(sawWin);
        // A drawn hand is rare among computers; the pure test of the text covers it, this one only looks when a seed gives it
    }

    TEST_F(MahjongEndScreenTest, ClickingNextHandSettlesTheHandAndDealsTheNext) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const SeatScores before = _game->GetMatch().Scores();
        const SeatScores deltas = SettleHand(Hand()).deltas;
        const size_t history = _game->GetMatch().History().size();

        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        Draw();

        EXPECT_EQ(_game->GetMatch().History().size(), history + 1);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(_game->GetMatch().Scores()[seat], before[seat] + deltas[seat]);
        }
        EXPECT_FALSE(Shown("HandOver"));
    }

    TEST_F(MahjongEndScreenTest, ANextHandThatIsOutOfDateChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        ASSERT_TRUE(_game->GetSession().NextHand().has_value());  // a control client settled it before the click arrived
        const uint64_t version = _game->GetSession().Version();
        const size_t history = _game->GetMatch().History().size();

        EmitAction("MahjongNextHand");

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetMatch().History().size(), history);
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
    }

    TEST_F(MahjongEndScreenTest, ANextHandFromAControlClientClosesTheScoreTable) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
        // The person plays the hand to its end through the control API (the computers answer at once)
        _game->GetSession().RunComputers();
        IControllableGame* control = _game->AsControllable();
        for (int step = 0; step < kActionLimit && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            const auto actions = control->GetActions();
            ASSERT_TRUE(actions.has_value());
            ASSERT_FALSE(actions->empty());
            ASSERT_TRUE(control->PerformAction(actions->back()).has_value());
        }
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        ASSERT_TRUE(Shown("HandOver"));
        ASSERT_TRUE(Shown("HandOverAction"));

        Json action = Json::object();
        action["type"] = "next_hand";
        ASSERT_TRUE(control->PerformAction(action).has_value());
        Draw();

        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
    }

    TEST_F(MahjongEndScreenTest, TheButtonOfTheLastHandSaysFinishAndLeadsToTheStandings) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        int finishing = 0;
        for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
            ASSERT_NO_FATAL_FAILURE(PlayHandOver());
            const bool says = Text("HandOverAction", "NextHand") == "Finish match";
            ASSERT_TRUE(says || Text("HandOverAction", "NextHand") == "Next hand");
            ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
            Draw();
            EXPECT_EQ(says, _game->GetMatch().GetPhase() == MatchPhase::Finished) << "hand " << hand << ": the button promises what the click does";
            finishing += says ? 1 : 0;
        }
        EXPECT_EQ(finishing, 1);
        ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_TRUE(Shown("MatchOver"));
        const StandingsTable table = FormatStandings(CoreManager::GetUIStrings(), _game->GetMatch(), _game->GetSession().Seats());
        EXPECT_EQ(Var("matchOverHeadline"), table.headline.c_str());
        EXPECT_EQ(Text("MatchOver", "Headline"), table.headline.c_str());
        for (int row = 0; row < kSeatCount; ++row) {
            EXPECT_EQ(Var(Format("standing{}", row).c_str()), FormatStandingSentence(CoreManager::GetUIStrings(), table.rows[row]).c_str());
            EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), FormatStandingSentence(CoreManager::GetUIStrings(), table.rows[row]).c_str());
        }
    }

    TEST_F(MahjongEndScreenTest, TheButtonOfTheLastHandSaysFinishInEveryLanguageAndTheStandingsFollowALanguageSwitch) {
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            ASSERT_TRUE(_game->SetLanguage(language));  // a start applies the language of the game, not the one the walk set on the table
            ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
            int finishing = 0;
            for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                ASSERT_NO_FATAL_FAILURE(PlayHandOver());
                const std::string label = Text("HandOverAction", "NextHand");
                const bool says = label == Tr(strings, "handover.finish").c_str();
                ASSERT_TRUE(says || label == Tr(strings, "handover.next").c_str()) << label;
                ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
                Draw();
                EXPECT_EQ(says, _game->GetMatch().GetPhase() == MatchPhase::Finished) << "hand " << hand << ": the button promises what the click does";
                finishing += says ? 1 : 0;
            }
            EXPECT_EQ(finishing, 1);
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);

            // The standings are in this language, and in the other after a switch while they are up
            const StandingsTable table = FormatStandings(strings, _game->GetMatch(), _game->GetSession().Seats());
            EXPECT_EQ(Text("MatchOver", "Headline"), table.headline.c_str());
            EXPECT_EQ(Text("MatchOver", "NewGame"), Tr(strings, "hud.newGame").c_str());
            const PoolString other = language == "en"_intern ? PoolString("ru"_intern) : PoolString("en"_intern);
            ASSERT_TRUE(_game->SetLanguage(other));
            Draw();
            const StandingsTable switched = FormatStandings(CoreManager::GetUIStrings(), _game->GetMatch(), _game->GetSession().Seats());
            EXPECT_NE(switched.headline, table.headline);
            EXPECT_EQ(Text("MatchOver", "Headline"), switched.headline.c_str());
            for (int row = 0; row < kSeatCount; ++row) {
                EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), FormatStandingSentence(CoreManager::GetUIStrings(), switched.rows[row]).c_str());
            }
            ASSERT_TRUE(_game->SetLanguage(language));  // the walk goes on from this language
        });
    }

    TEST_F(MahjongEndScreenTest, NewGameOnTheStandingsOpensTheSeatChoiceAndAStartHidesThem) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));

        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        Draw();
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(Shown("Setup"));

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(Shown("MatchOver")) << "a new match is in play";
        EXPECT_EQ(_game->GetMatch().GetPhase(), MatchPhase::InHand);
    }

    TEST_F(MahjongEndScreenTest, NothingMovesBehindTheScoreTableUntilNextHand) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const uint64_t version = _game->GetSession().Version();

        for (int i = 0; i < 4; ++i) {
            SceneEvents::SceneUpdateEvent::Emit(kComputerStepSeconds * 2.0f);
        }

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
    }

    // ----- the look of the two screens -----

    TEST_F(MahjongEndScreenTest, TheScoreTableFollowsTheModalAnatomyAndTheGridGaps) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            CoreManager::GetUIVars().Set("handOverHeadline"_intern, eastl::string_view(FindWorstTexts(CoreManager::GetUIStrings()).headline));
            canvas->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plateElement = Element("HandOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            EXPECT_TRUE(SameColor(plateElement->color, PaletteColor("surface")));
            EXPECT_GE(plate.y, kScoreTableTop) << "the plate keeps clear of the top seat's sets";
            EXPECT_LE(plate.y + plate.h, kScoreTableBottom) << "the plate keeps clear of the viewer's bonus tiles";
            EXPECT_GE(plate.x, kScoreTableLeft) << "the plate keeps clear of the left seat's columns";
            EXPECT_LE(plate.x + plate.w, kScoreTableRight) << "the plate keeps clear of the right seat's columns";
            EXPECT_EQ(plate.h, kScoreTableHeight);

            const UIElement* headline = Element("HandOver", "Headline");
            ASSERT_NE(headline, nullptr);
            EXPECT_TRUE(SameColor(headline->color, PaletteColor("header")));
            EXPECT_EQ(headline->rect.x, plate.x + kModalPadding);
            EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);
            EXPECT_EQ(headline->rect.w, plate.w - 2.0f * kModalPadding);
            EXPECT_EQ(headline->rect.h, kScoreHeadlineHeight) << "one line of 28";

            // The table: the caption row and the four seat rows, 4 apart, in one stack 8 under the headline
            const UIElement* table = Element("HandOver", "ScoreTable");
            const UIElement* caption = Element("HandOver", "ScoreHeader");
            ASSERT_TRUE(table != nullptr && caption != nullptr);
            EXPECT_EQ(GapBelow(headline->rect, table->rect), kModalGap);
            EXPECT_EQ(caption->rect.y, table->rect.y);
            EXPECT_EQ(caption->rect.h, kScoreCaptionHeight);
            EXPECT_EQ(caption->rect.w, headline->rect.w);
            const Rect* above = &caption->rect;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const UIElement* row = Element("HandOver", Format("SeatRow{}", seat).c_str());
                ASSERT_NE(row, nullptr);
                EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised"))) << seat;
                EXPECT_EQ(GapBelow(*above, row->rect), kScoreTableGap) << "row " << seat;
                EXPECT_EQ(row->rect.h, kScoreRowHeight) << seat;
                EXPECT_EQ(row->rect.x, headline->rect.x) << seat;
                EXPECT_EQ(row->rect.w, headline->rect.w) << seat;
                above = &row->rect;
            }
            EXPECT_EQ(table->rect.y + table->rect.h, above->y + above->h) << "the table ends with its last row";

            // The gap for the Next hand button: an empty element of the stack between the table and the details
            const UIElement* buttonGap = Element("HandOver", "ButtonGap");
            ASSERT_NE(buttonGap, nullptr);
            EXPECT_EQ(GapBelow(table->rect, buttonGap->rect), kModalGap);
            EXPECT_EQ(buttonGap->rect.h, kScoreButtonGapHeight);
            EXPECT_EQ(buttonGap->rect.x, headline->rect.x);
            EXPECT_EQ(buttonGap->rect.w, headline->rect.w);

            // Under the gap the details, 4 apart: the winning tiles, the lines, the tally row, the summary
            const UIElement* details = Element("HandOver", "Details");
            const UIElement* tiles = Element("HandOver", "WinTiles");
            const UIElement* lines = Element("HandOver", "Lines");
            ASSERT_TRUE(details != nullptr && tiles != nullptr && lines != nullptr);
            EXPECT_EQ(GapBelow(buttonGap->rect, details->rect), kModalGap);
            EXPECT_EQ(tiles->rect.y, details->rect.y);
            EXPECT_EQ(tiles->rect.h, kScoreTilesHeight);
            EXPECT_EQ(tiles->rect.w, headline->rect.w);
            for (int i = 0; i < kMaxWinningTiles; ++i) {
                const UIElement* tile = Element("HandOver", Format("WinTile{}", i).c_str());
                ASSERT_NE(tile, nullptr) << i;
                EXPECT_EQ(tile->rect.w, kScoreTileWidth) << i;
                EXPECT_EQ(tile->rect.h, kScoreTilesHeight) << i;
            }
            EXPECT_EQ(GapBelow(tiles->rect, lines->rect), kScoreTableGap);
            EXPECT_EQ(lines->rect.w, headline->rect.w);
            std::vector<float> columns;  // the distinct left edges of the lines: four columns
            for (int i = 0; i < kMaxScoreLines; ++i) {
                const UIElement* line = Element("HandOver", Format("ScoreLine{}", i).c_str());
                ASSERT_NE(line, nullptr) << i;
                if (std::find(columns.begin(), columns.end(), line->rect.x) == columns.end()) {
                    columns.push_back(line->rect.x);
                }
                EXPECT_EQ(line->rect.h, kScoreLineRowHeight) << i;
                EXPECT_GE(line->rect.x, lines->rect.x) << i;
                EXPECT_LE(line->rect.x + line->rect.w, lines->rect.x + lines->rect.w) << i;
                EXPECT_GE(line->rect.y, lines->rect.y) << i;
                EXPECT_LE(line->rect.y + line->rect.h, lines->rect.y + lines->rect.h) << "line " << i << " lies inside its area, all sixteen shown";
            }
            EXPECT_EQ(columns.size(), static_cast<size_t>(kScoreLineColumns)) << "the sixteen lines lie in four columns";
            above = &lines->rect;

            const UIElement* coachRow = Element("HandOver", "HandCoachRow");
            ASSERT_NE(coachRow, nullptr);
            EXPECT_EQ(coachRow->rect.h, kScoreCoachRowHeight);
            EXPECT_EQ(GapBelow(*above, coachRow->rect), kScoreTableGap) << "the tally row lies under the lines";
            EXPECT_EQ(coachRow->rect.w, headline->rect.w);
            above = &coachRow->rect;

            const UIElement* summary = Element("HandOver", "Summary");
            ASSERT_NE(summary, nullptr);
            EXPECT_TRUE(SameColor(summary->color, PaletteColor("surfaceRaised")));
            EXPECT_TRUE(SameColor(summary->GetComponent<UITextComponent>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(summary->rect.h, kScoreSummaryHeight);
            EXPECT_EQ(GapBelow(*above, summary->rect), kScoreTableGap);

            EXPECT_EQ(summary->rect.y + summary->rect.h, plate.y + plate.h - kModalPadding) << "the plate ends with the summary band: the button is in the gap above the details";
            EXPECT_EQ(details->rect.y + details->rect.h, summary->rect.y + summary->rect.h) << "the details stack is exactly full with the tally row shown";
            ExpectTheTableInsideThePlate(plate, "landscape");

            // The score table leaves the hands face up under it: no dim, and the plate is the only thing that blocks the clicks
            EXPECT_EQ(Element("HandOver", "Dim"), nullptr) << "a dim would cover the hands";
            EXPECT_TRUE(plateElement->blocksInput);
            EXPECT_TRUE(canvas->_modal) << "the canvas is still modal: it takes every click";

            // The main button is not a child of the plate: it is in a Screen canvas whose floor keeps it a touch target, in the middle of the window,
            // in the gap of the plate. 56 high at 22, because a button of 64 / 28 would not leave room for the gaps of 8 around it when the window
            // scale outgrows the fit of the table (32:9: 1.41 times).
            auto action = Canvas("HandOverAction");
            ASSERT_TRUE(action);
            action->UpdateLayout(kViewWidth, kViewHeight);
            EXPECT_EQ(Element("HandOverAction", "Bar"), nullptr) << "no bar: it would cover the viewer's hand";
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_NE(next, nullptr);
            EXPECT_TRUE(SameColor(next->color, PaletteColor("primary")));
            EXPECT_EQ(next->rect.h, kScoreNextHeight);
            EXPECT_EQ(next->rect.w, kScoreNextWidth);
            EXPECT_EQ(next->rect.x + next->rect.w * 0.5f, kViewWidth * 0.5f) << "in the middle of the table";
            EXPECT_GE(next->rect.x, plate.x + kModalPadding);
            EXPECT_LE(next->rect.x + next->rect.w, plate.x + plate.w - kModalPadding);
            // In the gap at the design size, with room to spare on both sides (a grid unit at least); the gap is 64 and the button 56
            EXPECT_GE(next->rect.y, buttonGap->rect.y) << "inside the gap, not on the seat rows";
            EXPECT_LE(next->rect.y + next->rect.h, buttonGap->rect.y + buttonGap->rect.h) << "inside the gap, not on the lines";
            for (const char* name : {"Headline", "ScoreHeader", "SeatRow0", "SeatRow1", "SeatRow2", "SeatRow3", "WinTiles", "Lines", "HandCoachRow", "Summary"}) {
                EXPECT_FALSE(Intersects(next->rect, Element("HandOver", name)->rect)) << name;
            }
            EXPECT_TRUE(Intersects(next->rect, plate)) << "the button lies on the plate";
        });
    }

    TEST_F(MahjongEndScreenTest, ThePortraitScoreTableLiesBetweenTheSideColumnsInTwoColumnsAndHoldsTheNextHandButtonInItsGap) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        auto action = Canvas("HandOverAction");
        ASSERT_TRUE(canvas && action);
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown
        const MatrixWindow window = DesignWindowOf(TableShape::Portrait);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            CoreManager::GetUIVars().Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            LayOutInWindow(*canvas, window);
            LayOutInWindow(*action, window, CanvasScaleMode::ScaleWithScreen, 1.0f);

            const UIElement* plateElement = Element("HandOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            const Rect bounds = ScoreTableBounds(TableShape::Portrait);
            EXPECT_GE(plate.x, bounds.x);
            EXPECT_LE(plate.x + plate.w, bounds.x + bounds.w);
            EXPECT_GE(plate.y, bounds.y);
            EXPECT_LE(plate.y + plate.h, bounds.y + bounds.h);
            EXPECT_EQ(plate.h, kPortraitScoreTableHeight);
            EXPECT_EQ(plate.x + plate.w * 0.5f, kPortraitViewWidth * 0.5f) << "in the middle of the table";

            // The anatomy of the landscape plate: padding, the headline, the table, the gap, the details; the gap and the two bands are taller,
            // the rows of the table one step taller and the type of the headline one step smaller
            const UIElement* headline = Element("HandOver", "Headline");
            const UIElement* table = Element("HandOver", "ScoreTable");
            const UIElement* gap = Element("HandOver", "ButtonGap");
            const UIElement* details = Element("HandOver", "Details");
            const UIElement* tiles = Element("HandOver", "WinTiles");
            const UIElement* lines = Element("HandOver", "Lines");
            const UIElement* summary = Element("HandOver", "Summary");
            const UIElement* coachRow = Element("HandOver", "HandCoachRow");
            ASSERT_TRUE(headline != nullptr && table != nullptr && gap != nullptr && details != nullptr && tiles != nullptr && lines != nullptr && summary != nullptr && coachRow != nullptr);
            EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);
            EXPECT_EQ(headline->rect.w, plate.w - 2.0f * kModalPadding);
            EXPECT_EQ(headline->rect.h, kPortraitScoreBandHeight);
            EXPECT_EQ(GapBelow(headline->rect, table->rect), kModalGap);
            EXPECT_EQ(Element("HandOver", "ScoreHeader")->rect.h, kScoreCaptionHeight);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const UIElement* row = Element("HandOver", Format("SeatRow{}", seat).c_str());
                ASSERT_NE(row, nullptr);
                EXPECT_EQ(row->rect.h, kPortraitScoreRowHeight) << seat;
            }
            EXPECT_EQ(GapBelow(table->rect, gap->rect), kModalGap);
            EXPECT_EQ(gap->rect.h, kPortraitScoreButtonGapHeight);
            EXPECT_EQ(GapBelow(gap->rect, details->rect), kModalGap);
            EXPECT_EQ(tiles->rect.y, details->rect.y);
            EXPECT_EQ(tiles->rect.h, kScoreTilesHeight);
            for (int i = 0; i < kMaxWinningTiles; ++i) {
                const UIElement* tile = Element("HandOver", Format("WinTile{}", i).c_str());
                ASSERT_NE(tile, nullptr) << i;
                EXPECT_EQ(tile->rect.w, kScoreTileWidth) << "the narrow plate holds all eighteen tiles at the size of the landscape one: " << i;
            }
            EXPECT_EQ(GapBelow(tiles->rect, lines->rect), kScoreTableGap);
            EXPECT_EQ(GapBelow(lines->rect, coachRow->rect), kScoreTableGap);
            EXPECT_EQ(coachRow->rect.h, kPortraitScoreCoachRowHeight);
            EXPECT_EQ(GapBelow(coachRow->rect, summary->rect), kScoreTableGap);
            EXPECT_EQ(summary->rect.h, kPortraitScoreBandHeight);
            EXPECT_EQ(summary->rect.y + summary->rect.h, plate.y + plate.h - kModalPadding) << "the plate ends with the summary band";
            EXPECT_EQ(details->rect.y + details->rect.h, summary->rect.y + summary->rect.h) << "the details stack is exactly full";
            ExpectTheTableInsideThePlate(plate, "portrait");
            std::vector<float> columns;
            for (int i = 0; i < kMaxScoreLines; ++i) {
                const UIElement* line = Element("HandOver", Format("ScoreLine{}", i).c_str());
                ASSERT_NE(line, nullptr) << i;
                if (std::find(columns.begin(), columns.end(), line->rect.x) == columns.end()) {
                    columns.push_back(line->rect.x);
                }
                EXPECT_GE(line->rect.x, lines->rect.x) << i;
                EXPECT_LE(line->rect.x + line->rect.w, lines->rect.x + lines->rect.w) << i;
                EXPECT_LE(line->rect.y + line->rect.h, lines->rect.y + lines->rect.h) << "line " << i << " lies inside its area, all sixteen shown";
            }
            EXPECT_EQ(columns.size(), static_cast<size_t>(kPortraitScoreLineColumns)) << "the sixteen lines lie in two columns";

            // The button, in a window the size of the view, is in the gap and in the middle of the table, 6 units under the middle of the view
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_NE(next, nullptr);
            EXPECT_EQ(next->rect.h, kScoreNextHeight);
            EXPECT_EQ(next->rect.w, kScoreNextWidth);
            EXPECT_EQ(next->rect.x + next->rect.w * 0.5f, kPortraitViewWidth * 0.5f);
            EXPECT_GE(next->rect.y, gap->rect.y) << "inside the gap, not on the seat rows";
            EXPECT_LE(next->rect.y + next->rect.h, gap->rect.y + gap->rect.h) << "inside the gap, not on the lines";
        });
    }

    namespace {

        /// Whether a slot is part of a hand: its concealed tiles, its sets and its bonus tiles. The discard blocks and the wall are the piles and
        /// the ring in the middle, which the score table covers on purpose (the owner's call is about the hands).
        bool IsHandSlot(const TileSlot& slot) {
            return slot.role == SlotRole::HandTile || slot.role == SlotRole::MeldTile || slot.role == SlotRole::BonusTile;
        }

        /// Expects that no hand, meld or bonus slot of the layout (view units) intersects `rect`, and that the layout has such slots of all four
        /// seats, so the check is not an empty one. `exceptViewersSets` leaves the melds and bonus tiles of the viewer out: in the phone profile
        /// they lie in a row above the large hand, under the foot of the plate (a known gap, "Left for the owner" of the entry
        /// `mahjong-phone-hand-size`); the viewer's concealed tiles are held in every profile.
        void ExpectNoHandUnder(const Rect& rect, const TableLayout& layout, const char* label, bool exceptViewersSets = false) {
            std::array<size_t, kSeatCount> slotsOfSeat{};
            for (const TileSlot& slot : layout.slots) {
                if (!IsHandSlot(slot)) {
                    continue;
                }
                ++slotsOfSeat[slot.seat];
                if (exceptViewersSets && slot.seat == layout.viewer && slot.role != SlotRole::HandTile) {
                    continue;
                }
                EXPECT_FALSE(Intersects(rect, slot.rect)) << label << ": covers the slot of role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index << " part "
                                                          << slot.part << " at (" << slot.rect.x << ", " << slot.rect.y << ", " << slot.rect.w << " x " << slot.rect.h << ")";
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_GT(slotsOfSeat[seat], 0u) << label << ": seat " << seat;
            }
        }

    }  // namespace

    TEST_F(MahjongEndScreenTest, TheNarrowScoreTableLiesBetweenTheSideColumnsInFourColumnsAndHoldsTheNextHandButtonInItsGap) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        auto action = Canvas("HandOverAction");
        ASSERT_TRUE(canvas && action);
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown
        const MatrixWindow window = DesignWindowOf(TableShape::Narrow);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            CoreManager::GetUIVars().Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            LayOutInWindow(*canvas, window);
            LayOutInWindow(*action, window, CanvasScaleMode::ScaleWithScreen, 1.0f);

            const UIElement* plateElement = Element("HandOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            const Rect bounds = ScoreTableBounds(TableShape::Narrow);
            EXPECT_GE(plate.x, bounds.x);
            EXPECT_LE(plate.x + plate.w, bounds.x + bounds.w);
            EXPECT_GE(plate.y, bounds.y);
            EXPECT_LE(plate.y + plate.h, bounds.y + bounds.h);
            EXPECT_EQ(plate.w, kNarrowScoreTableWidth);
            EXPECT_EQ(plate.h, kScoreTableHeight);
            EXPECT_EQ(plate.x + plate.w * 0.5f, kNarrowViewWidth * 0.5f) << "in the middle of the table";

            // The anatomy of the landscape plate in a narrower one: the same bands, the gap and the four columns of lines
            const UIElement* headline = Element("HandOver", "Headline");
            const UIElement* gap = Element("HandOver", "ButtonGap");
            const UIElement* lines = Element("HandOver", "Lines");
            const UIElement* coachRow = Element("HandOver", "HandCoachRow");
            const UIElement* summary = Element("HandOver", "Summary");
            const UIElement* details = Element("HandOver", "Details");
            ASSERT_TRUE(headline != nullptr && gap != nullptr && lines != nullptr && coachRow != nullptr && summary != nullptr && details != nullptr);
            EXPECT_EQ(headline->rect.w, plate.w - 2.0f * kModalPadding);
            EXPECT_EQ(headline->rect.h, kScoreHeadlineHeight);
            EXPECT_EQ(coachRow->rect.h, kScoreCoachRowHeight);
            EXPECT_EQ(summary->rect.y + summary->rect.h, plate.y + plate.h - kModalPadding) << "the plate ends with the summary band";
            EXPECT_EQ(details->rect.y + details->rect.h, summary->rect.y + summary->rect.h) << "the details stack is exactly full";
            EXPECT_EQ(gap->rect.h, kScoreButtonGapHeight);
            ExpectTheTableInsideThePlate(plate, "narrow");
            std::vector<float> columns;
            for (int i = 0; i < kMaxScoreLines; ++i) {
                const UIElement* line = Element("HandOver", Format("ScoreLine{}", i).c_str());
                ASSERT_NE(line, nullptr) << i;
                if (std::find(columns.begin(), columns.end(), line->rect.x) == columns.end()) {
                    columns.push_back(line->rect.x);
                }
                EXPECT_GE(line->rect.x, lines->rect.x) << i;
                EXPECT_LE(line->rect.x + line->rect.w, lines->rect.x + lines->rect.w) << i;
                EXPECT_LE(line->rect.y + line->rect.h, lines->rect.y + lines->rect.h) << "line " << i << " lies inside its area, all sixteen shown";
            }
            EXPECT_EQ(columns.size(), static_cast<size_t>(kScoreLineColumns)) << "the sixteen lines lie in four columns";

            // The button (a Screen canvas: its own scale, 0.95 in this window) is in the gap and in the middle of the table, in view units
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_NE(next, nullptr);
            EXPECT_EQ(next->rect.w, kScoreNextWidth);
            const Rect button = ToView(next->rect, *action, window);
            EXPECT_NEAR(button.x + button.w * 0.5f, kNarrowViewWidth * 0.5f, 0.01f);
            EXPECT_GE(button.y, gap->rect.y) << "inside the gap, not on the seat rows";
            EXPECT_LE(button.y + button.h, gap->rect.y + gap->rect.h) << "inside the gap, not on the lines";

            // No hand, meld or bonus tile of the worst-case table, from any seat, lies under the plate or the button
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout seen = MakeWorstCaseTableLayout(viewer, window);
                ASSERT_EQ(seen.shape, TableShape::Narrow);
                ExpectNoHandUnder(plate, seen, Format("narrow plate, viewer {}", viewer).c_str());
                ExpectNoHandUnder(button, seen, Format("narrow Next hand, viewer {}", viewer).c_str());
            }
        });
    }

    TEST_F(MahjongEndScreenTest, TheScoreTableCoversNoHandMeldOrBonusTileAtTheDesignSize) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        auto action = Canvas("HandOverAction");
        ASSERT_TRUE(canvas && action);
        ASSERT_TRUE(Shown("HandOver") && Shown("HandOverAction"));
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown

        // The score table promises that the four hands stay readable under it: there is no dim, it is the plate that is modal
        EXPECT_EQ(Element("HandOver", "Dim"), nullptr);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(Element("HandOverAction", "Bar"), nullptr);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            vars.Set("handOverSummary"_intern, eastl::string_view(worst.summary));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int column = 0; column < kScoreColumns; ++column) {
                    vars.Set(PoolString::Intern(Format("handOverCell{}_{}", seat, column)), eastl::string_view(worst.cells[static_cast<size_t>(column)]));
                }
            }
            canvas->UpdateLayout(kViewWidth, kViewHeight);
            action->UpdateLayout(kViewWidth, kViewHeight);
            const UIElement* plate = Element("HandOver", "Plate");
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_TRUE(plate != nullptr && next != nullptr);

            // The worst-case table, seen from each seat: 14 tiles, four kongs and 8 bonus tiles a seat
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeWorstCaseTableLayout(viewer);
                ExpectNoHandUnder(plate->rect, layout, Format("plate, viewer {}", viewer).c_str());
                ExpectNoHandUnder(next->rect, layout, Format("Next hand, viewer {}", viewer).c_str());
            }
        });

        // The table of the finished hand itself: all four hands are face up and no slot of them is under the plate or the button
        const TableLayout& layout = _table->GetLayout();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), static_cast<int>(Hand().GetHand(seat).concealed.size())) << "seat " << seat << ": the hand is face up under the score table";
        }
        ExpectNoHandUnder(Element("HandOver", "Plate")->rect, layout, "plate, the table of the hand");
        ExpectNoHandUnder(Element("HandOverAction", "NextHand")->rect, layout, "Next hand, the table of the hand");
    }

    TEST_F(MahjongEndScreenTest, ThePlateOfTheScoreTableCoversNoHandMeldOrBonusTileInAnyWindowOfTheMatrix) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown
        // The plate is a SceneView canvas and follows the table (it has ignoreSafeArea like the table), so no window brings it onto a hand,
        // not even one with insets. The layout is the one the table draws in the window (the phone windows included)
        size_t windows = 0;
        size_t phoneWindows = 0;
        ForEachTableWindow([&](const MatrixWindow& window) {
            LayOutInWindow(*canvas, window);
            const UIElement* plate = Element("HandOver", "Plate");
            ASSERT_NE(plate, nullptr);
            ASSERT_FALSE(plate->rect.IsEmpty()) << window.label.c_str();
            const Rect plateRect = ToView(plate->rect, *canvas, window);
            ExpectTheTableInsideThePlate(plate->rect, window.label.c_str());
            const Rect bounds = ScoreTableBounds(ShapeOf(window));
            EXPECT_GE(plateRect.x, bounds.x - 0.01f) << window.label.c_str();
            EXPECT_LE(plateRect.x + plateRect.w, bounds.x + bounds.w + 0.01f) << window.label.c_str();
            EXPECT_GE(plateRect.y, bounds.y - 0.01f) << window.label.c_str();
            EXPECT_LE(plateRect.y + plateRect.h, bounds.y + bounds.h + 0.01f) << window.label.c_str();
            const bool phone = TableProfileOf(window).phone;
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                ExpectNoHandUnder(plateRect, MakeWorstCaseTableLayout(viewer, window), Format("{} viewer {}", window.label.c_str(), viewer).c_str(), phone);
            }
            ++windows;
            phoneWindows += phone ? 1 : 0;
        });
        EXPECT_GT(windows, 0u);
        EXPECT_GT(phoneWindows, 0u) << "the phone windows are visited";
    }

    TEST_F(MahjongEndScreenTest, TheStandingsFollowTheModalAnatomyAndTheFirstPlaceIsGold) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        auto canvas = Canvas("MatchOver");
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(canvas->SetElementVisible("MatchCoachRow"_intern, true));  // shown: the plate is laid out with the row in it
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            CoreManager::GetUIVars().Set("matchOverHeadline"_intern, eastl::string_view(FindWorstTexts(CoreManager::GetUIStrings()).standingsHeadline));
            canvas->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plateElement = Element("MatchOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            EXPECT_TRUE(SameColor(plateElement->color, PaletteColor("surface")));

            const UIElement* headline = Element("MatchOver", "Headline");
            ASSERT_NE(headline, nullptr);
            EXPECT_TRUE(SameColor(headline->color, PaletteColor("header")));
            EXPECT_EQ(headline->rect.x, plate.x + kModalPadding);
            EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);

            const Rect* above = &headline->rect;
            for (int row = 0; row < kSeatCount; ++row) {
                const UIElement* standing = Element("MatchOver", Format("Standing{}", row).c_str());
                ASSERT_NE(standing, nullptr);
                EXPECT_TRUE(SameColor(standing->color, PaletteColor("surfaceRaised"))) << row;
                EXPECT_EQ(GapBelow(*above, standing->rect), kStandingsGap) << "row " << row;
                above = &standing->rect;
                // The first place is the accent (gold), the others the plain text colour
                const Color& text = standing->GetComponent<UITextComponent>()->textColor;
                EXPECT_EQ(SameColor(text, PaletteColor("accent")), row == 0) << "row " << row;
                if (row != 0) {
                    EXPECT_TRUE(SameColor(text, PaletteColor("text"))) << "row " << row;
                }
            }

            const UIElement* coachRow = Element("MatchOver", "MatchCoachRow");
            ASSERT_NE(coachRow, nullptr);
            EXPECT_EQ(GapBelow(*above, coachRow->rect), kStandingsGap) << "the tally row lies under the standings";
            EXPECT_EQ(coachRow->rect.h, kMatchCoachRowHeight);
            above = &coachRow->rect;

            const UIElement* button = Element("MatchOver", "NewGame");
            ASSERT_NE(button, nullptr);
            EXPECT_TRUE(SameColor(button->color, PaletteColor("primary")));
            EXPECT_EQ(button->rect.h, kMainButtonHeight);
            EXPECT_EQ(GapBelow(*above, button->rect), kModalSectionGap) << "the button is set apart from the rows by 16";
            EXPECT_EQ(button->rect.y + button->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds the button and nothing more";
        });
    }

    // ----- the coach's tally -----

    TEST_F(MahjongEndScreenTest, TheScoreTableSaysTheTallyOfTheCoachInCoachModeAfterACountedMove) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
        const int moves = PlayHandAsPerson();
        ASSERT_GT(moves, 0);
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();

        const CoachTally hand = _game->GetCoach().HandTally();
        EXPECT_EQ(hand.moves, moves) << "every accepted action of the person is a counted move";
        EXPECT_LE(hand.agreed, hand.moves);
        EXPECT_EQ(_game->GetCoach().MatchTally(), hand) << "the first hand of the match";
        const eastl::string expected = ExpectedHandTally();
        ASSERT_FALSE(expected.empty());
        EXPECT_STREQ(Var("handOverCoach").c_str(), expected.c_str());
        EXPECT_TRUE(OnScreen("HandOver", "HandCoachRow"));
        EXPECT_EQ(Text("HandOver", "HandCoachRow"), std::string(expected.c_str()));
        EXPECT_NE(expected.find(Format("{}", moves).c_str()), eastl::string::npos);

        // The row is paid for inside the plate: the button, the gap and the plate are where they are without it
        const UIElement* plate = Element("HandOver", "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_EQ(plate->rect.h, kScoreTableHeight);
        EXPECT_EQ(Element("HandOver", "ButtonGap")->rect.h, kScoreButtonGapHeight);

        // A language switch rewords it at the next draw
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            EXPECT_STREQ(Var("handOverCoach").c_str(), ExpectedHandTally().c_str());
            EXPECT_TRUE(OnScreen("HandOver", "HandCoachRow"));
        });
    }

    TEST_F(MahjongEndScreenTest, TheScoreTableHasNoTallyInHintsOffOrWithNoMoveOfAPerson) {
        for (const HintsMode mode : {HintsMode::Hints, HintsMode::Off}) {
            _game->SetHintsMode(mode);
            ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
            ASSERT_GT(PlayHandAsPerson(), 0);
            Draw();
            ASSERT_TRUE(Shown("HandOver"));
            EXPECT_TRUE(Var("handOverCoach").empty()) << EnumUtils<HintsMode>::ToString(mode).data();
            EXPECT_FALSE(OnScreen("HandOver", "HandCoachRow")) << EnumUtils<HintsMode>::ToString(mode).data();
        }

        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        EXPECT_TRUE(Var("handOverCoach").empty()) << "no person, no move";
        EXPECT_FALSE(OnScreen("HandOver", "HandCoachRow"));
    }

    TEST_F(MahjongEndScreenTest, TheTallyRowFollowsTheModeWhileTheScoreTableIsUp) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
        ASSERT_GT(PlayHandAsPerson(), 0);
        Draw();
        ASSERT_TRUE(OnScreen("HandOver", "HandCoachRow"));
        _game->SetHintsMode(HintsMode::Hints);
        Draw();
        EXPECT_FALSE(OnScreen("HandOver", "HandCoachRow"));
        EXPECT_TRUE(Var("handOverCoach").empty());
    }

    TEST_F(MahjongEndScreenTest, TheStandingsSayTheMatchTallyInCoachModeAndNothingElse) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
        int moves = 0;
        for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
            moves += PlayHandAsPerson();
            ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        }
        ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));

        EXPECT_EQ(_game->GetCoach().MatchTally().moves, moves);
        const eastl::string expected = FormatCoachTally(CoreManager::GetUIStrings(), CoachTally{}, _game->GetCoach().MatchTally());
        ASSERT_FALSE(expected.empty());
        EXPECT_STREQ(Var("matchOverCoach").c_str(), expected.c_str());
        EXPECT_TRUE(OnScreen("MatchOver", "MatchCoachRow"));
        const UIElement* plate = Element("MatchOver", "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_EQ(plate->rect.h, kStandingsPlateHeight);
        EXPECT_EQ(plate->rect.y + plate->rect.h - kModalPadding, Element("MatchOver", "NewGame")->rect.y + Element("MatchOver", "NewGame")->rect.h) << "the button still ends the plate";

        _game->SetHintsMode(HintsMode::Hints);
        Draw();
        EXPECT_TRUE(Var("matchOverCoach").empty());
        EXPECT_FALSE(OnScreen("MatchOver", "MatchCoachRow"));
    }

    TEST_F(MahjongEndScreenTest, TheStandingsOfAMatchOfComputersHaveNoTallyRow) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));
        EXPECT_TRUE(Var("matchOverCoach").empty());
        EXPECT_FALSE(OnScreen("MatchOver", "MatchCoachRow"));
    }

    // ----- the size matrix -----

    TEST_F(MahjongEndScreenTest, TheScoreTablePassesTheSizeMatrixInEveryLanguageWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        ASSERT_NO_FATAL_FAILURE(ShowTheWorstScoreTable(*canvas));  // the worst case: the sixteen lines, the tally row and eighteen tiles shown

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            const WorstTexts worst = FindWorstTexts(strings);
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            vars.Set("handOverSummary"_intern, eastl::string_view(worst.summary));
            vars.Set("handOverCoach"_intern, eastl::string_view(worst.handTally));
            vars.Set("handOverButton"_intern, eastl::string_view(LongestText({Tr(strings, "handover.next"), Tr(strings, "handover.finish")})));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int column = 0; column < kScoreColumns; ++column) {
                    vars.Set(PoolString::Intern(Format("handOverCell{}_{}", seat, column)), eastl::string_view(worst.cells[static_cast<size_t>(column)]));
                }
            }

            // The longest line the rules can print, of every kind and in both forms (points and doubles), in all sixteen places at once
            for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
                for (const int doubles : {0, 3}) {
                    ScoreLine line;
                    line.kind = kind;
                    line.tile = LongestTile(strings, [&](Tile tile) { return CanNameTile(kind, tile); });  // the longest tile this kind can name
                    line.points = kWorstLinePoints;
                    line.doubles = doubles;
                    const eastl::string text = ScoreLineText(strings, line);
                    for (int i = 0; i < kMaxScoreLines; ++i) {
                        vars.Set(PoolString::Intern(Format("scoreLine{}", i)), eastl::string_view(text));
                    }
                    SCOPED_TRACE(EnumUtils<ScoreKind>::ToString(kind).data());
                    for (const Output& output : kOutputs) {
                        ExpectWordsFit("HandOver", output.width, output.height, kButtonAir);
                    }
                }
            }
            // The plate is text only; the button is in the canvas HandOverAction, and the longer of its two words fits there
            for (const Output& output : kOutputs) {
                ExpectWordsFit("HandOverAction", output.width, output.height, kButtonAir);
            }
            ExpectPassesTheMatrix("HandOver", EngineTouchMinimum());
            ExpectPassesTheMatrix("HandOverAction", EngineTouchMinimum());
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("HandOver"), kButtonAir);  // each window against the view of its shape
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
            EXPECT_GE(Element("HandOverAction", "NextHand")->rect.h, kMinControlHeight);
        });
    }

    TEST_F(MahjongEndScreenTest, TheStandingsPassTheSizeMatrixInEveryLanguageWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Canvas("MatchOver")->SetElementVisible("MatchCoachRow"_intern, true));  // the worst case: the tally row shown

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("matchOverHeadline"_intern, eastl::string_view(worst.standingsHeadline));
            vars.Set("matchOverCoach"_intern, eastl::string_view(worst.matchTally));
            for (int row = 0; row < kSeatCount; ++row) {
                vars.Set(PoolString::Intern(Format("standing{}", row)), eastl::string_view(worst.standing));
            }

            ExpectPassesTheMatrix("MatchOver", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("MatchOver", output.width, output.height, kButtonAir);
            }
            EXPECT_GE(Element("MatchOver", "NewGame")->rect.h, kMinControlHeight);
        });
    }

    TEST_F(MahjongEndScreenTest, TheButtonsOfBothPanelsCarryTheirActions) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_EQ(ButtonAction(Element("HandOverAction", "NextHand")), "MahjongNextHand"_intern);
        EXPECT_EQ(ButtonAction(Element("MatchOver", "NewGame")), "MahjongNewGame"_intern);
    }

    TEST_F(MahjongEndScreenTest, TheScoreTableHasNoControlAndTheNewGameButtonOfTheHudIsHiddenWhileItIsShown) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_TRUE(Shown("HudMenu")) << "a hand in play";

        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        // The score table is a screen of its own: the HUD buttons (New game, Rules) are hidden while it is up
        EXPECT_FALSE(Shown("HudMenu"));
        auto plate = Canvas("HandOver");
        ASSERT_TRUE(plate);
        std::vector<const UIElement*> controls;
        CollectControls(plate->GetRoot(), controls);
        for (const UIElement* control : controls) {
            EXPECT_FALSE(control->IsInteractive()) << control->name.ToStringView().data() << ": the plate is text, the button is in HandOverAction";
        }

        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        Draw();
        EXPECT_TRUE(Shown("HudMenu"));
    }

    TEST_F(MahjongEndScreenTest, TheStandingsCoverTheWholeWindowSoTheNewGameButtonOfTheHudStaysAsItIs) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));
        EXPECT_TRUE(Shown("HudMenu"));
        const UIElement* dim = Element("MatchOver", "Dim");
        ASSERT_NE(dim, nullptr);
        EXPECT_TRUE(dim->blocksInput);
        EXPECT_EQ(Canvas("MatchOver")->GetLayout().params.space, CanvasSpace::Screen) << "a Screen canvas: its dim covers the bars too";
        // The button of the standings is the one that answers, and the click opens the seat choice
        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        EXPECT_TRUE(_game->IsSetupOpen());
    }

}  // namespace BECore::Mahjong::Tests
