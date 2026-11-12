#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "StringsTestSupport.h"

#include <BECore/UI/UIDropdown.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Online/TextEntry.h>
#include <Mahjong/Table/Words.h>
#include <initializer_list>
#include <vector>

/**
 * The node files `Online` and `OnlineEntry` of the shipped scene (`config/nodes/MahjongOnline.xml`, `MahjongOnlineEntry.xml`) with no component behind them: the
 * canvases are modal `Screen` canvases with a floor, hidden at the start; every page is a set of elements shown by name (what the online component does later), and each
 * page passes the size matrix in both languages with the longest texts, every word whole and every control a touch target.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kOnline = "Online";
        constexpr const char* kEntry = "OnlineEntry";
        constexpr float kButtonAir = 8.0f;
        /// The air kept each side of the caption of a pad key, one character in a key of 25 units in the narrowest canvas: the 8 of a word's button would leave a letter no room.
        constexpr float kKeyAir = 4.0f;
        constexpr float kMinControlHeight = 48.0f;
        constexpr size_t kSeats = 4;
        /// The longest a name can be: 24 bytes, which the value of a field shows whole. The widest letter of the pad of the language repeats up to it.
        constexpr size_t kNameBytes = 24;
        /// A name on a seat row shares the row with the seat's number and `Sit here`, and in a canvas of 262 units it has about 120 of them: a name longer than this many bytes
        /// of wide letters is cut at the smallest text size (it is the player's text, not a word of the game).
        constexpr size_t kSeatNameBytes = 10;

        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        /// The elements the pages of the panel show and hide among themselves (the rest of the canvas is always there).
        const char* const kPageElements[] = {
            "OnlineTitleMenu",  "OnlineTitleHost", "OnlineTitleJoin", "OnlineTitleLobby", "OnlineNotice",   "OnlineStatus",   "OnlineWaiting",  "OnlineHostLine", "OnlineMenuHost", "OnlineMenuJoin",
            "OnlineMenuReturn", "OnlineNameRow",   "OnlineCodeRow",   "OnlineHandsRow",   "OnlineLevelRow", "OnlineHintsRow", "OnlineSeatRow0", "OnlineSeatRow1", "OnlineSeatRow2", "OnlineSeatRow3",
            "OnlineSit0",       "OnlineSit1",      "OnlineSit2",      "OnlineSit3",       "OnlineGap",      "OnlineButtons",  "OnlineCreate",   "OnlineEnter",    "OnlineStart",    "OnlineLeave",
        };

        /// The page `Menu` (with `Return` offered), `Host`, `Join` and the `Lobby` (a seat that can be taken on every row, the host's `Start`): the elements each shows.
        const std::vector<const char*> kMenuPage = {"OnlineTitleMenu", "OnlineMenuHost", "OnlineMenuJoin", "OnlineMenuReturn"};
        const std::vector<const char*> kHostPage = {"OnlineTitleHost", "OnlineNameRow", "OnlineHandsRow", "OnlineLevelRow", "OnlineHintsRow", "OnlineGap", "OnlineButtons", "OnlineCreate"};
        const std::vector<const char*> kJoinPage = {"OnlineTitleJoin", "OnlineNameRow", "OnlineCodeRow", "OnlineGap", "OnlineButtons", "OnlineEnter"};
        const std::vector<const char*> kLobbyPage = {"OnlineTitleLobby", "OnlineSeatRow0", "OnlineSeatRow1", "OnlineSeatRow2", "OnlineSeatRow3", "OnlineSit0", "OnlineSit1",
                                                     "OnlineSit2",       "OnlineSit3",     "OnlineGap",      "OnlineButtons",  "OnlineStart",    "OnlineLeave"};
        /// The lines that take the place under the title, one at a time.
        const char* const kInfoLines[] = {"OnlineNotice", "OnlineStatus", "OnlineWaiting", "OnlineHostLine"};

        /// The widest character of @p candidates by the number of code points of a text of it, which is how a line is measured; here the longest text in a language is wanted,
        /// so the first of the pad.
        eastl::string RepeatedLetter(const UIStrings& strings, size_t bytes) {
            const eastl::vector<eastl::string> pad = TextEntry(TextEntry::Kind::Name).Pad(strings);
            const eastl::string& letter = pad.front();
            eastl::string name;
            while (name.size() + letter.size() <= bytes) {
                name += letter;
            }
            return name;
        }

    }  // namespace

    class MahjongHudOnlineTest : public MahjongTableTestBase {
    protected:
        void Prepare() {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            _game->CloseSetup();
            Draw();
        }

        Scene& TheScene() const {
            return *_manager->GetSceneByName("MahjongScene"_intern);
        }

        void SetCanvasShown(const char* node, bool shown) {
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(TheScene(), PoolString::Intern(node), shown));
        }

        /// Shows exactly these elements of @p kPageElements (the rest of them hidden).
        void ShowOnly(const std::vector<const char*>& shown, const char* infoLine = nullptr) {
            const auto canvas = Canvas(kOnline);
            ASSERT_TRUE(canvas);
            for (const char* name : kPageElements) {
                const bool visible =
                    std::find_if(shown.begin(), shown.end(), [&](const char* s) { return std::string(s) == name; }) != shown.end() || (infoLine != nullptr && std::string(infoLine) == name);
                ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(name), visible)) << name;
            }
        }

        /// The longest text each variable of the panel can have in the current language: the notices by code points among every known code, the seat texts among every
        /// occupant, a name of the full 24 bytes, a room code of the widest letters, the keys of the widest pad.
        static void SetWorstVars() {
            const UIStrings& strings = CoreManager::GetUIStrings();
            UIVars& vars = CoreManager::GetUIVars();
            std::vector<eastl::string> notices;
            for (const OnlineCodeWord& word : kKnownOnlineCodes) {
                notices.push_back(OnlineNoticeText(strings, word.key));
            }
            notices.push_back(OnlineNoticeText(strings, kOnlineUnknownKey, "too_many_failed_joins"));
            notices.push_back(OnlineNoticeText(strings, kOnlineBadViewKey));
            vars.Set("onlineNotice"_intern, eastl::string_view(LongestText(notices)));
            vars.Set("onlineStatus"_intern, eastl::string_view(LongestText({Tr(strings, kOnlineConnectingKey), Tr(strings, kOnlineReconnectingKey)})));

            const eastl::string name = RepeatedLetter(strings, kNameBytes);
            const eastl::string seatName = RepeatedLetter(strings, kSeatNameBytes);
            vars.Set("onlineHostLine"_intern, eastl::string_view(LongestText({Tr(strings, kOnlineLobbyHintsAllowedKey), Tr(strings, kOnlineLobbyHintsOffKey)})));
            vars.Set("onlineWaiting"_intern, eastl::string_view(LongestText({Tr(strings, kOnlineWaitHostKey), Tr(strings, kOnlineWaitHostHintsAllowedKey), Tr(strings, kOnlineWaitHostHintsOffKey)})));
            std::vector<eastl::string> seats;
            for (const bool mine : {false, true}) {
                for (const bool connected : {false, true}) {
                    seats.push_back(OnlineSeatText(strings, BECore::SeatKind::Human, seatName, mine, connected));
                }
            }
            seats.push_back(OnlineSeatText(strings, BECore::SeatKind::Computer, {}, false, true));
            seats.push_back(OnlineSeatText(strings, BECore::SeatKind::Empty, {}, false, true));
            for (size_t seat = 0; seat < kSeats; ++seat) {
                vars.Set(PoolString::Intern(Format("onlineSeat{}", seat)), eastl::string_view(LongestText(seats)));
            }
            vars.Set("onlineName"_intern, eastl::string_view(name));
            vars.Set("onlineEntryValue"_intern, eastl::string_view(name));
            vars.Set("onlineCode"_intern, "WWWWW");
            vars.Set("onlineRoom"_intern, "WWWWW");

            const eastl::vector<eastl::string> pad = TextEntry(TextEntry::Kind::Name).Pad(strings);
            for (size_t key = 0; key < TextEntry::kPadMaxKeys; ++key) {
                vars.Set(PoolString::Intern(Format("padKey{}", key)), eastl::string_view(pad[key % pad.size()]));
            }
        }

        /// The canvas as it is passes the matrix, every word fits in the outputs and on the whole matrix.
        void ExpectTheCanvasFits(const char* node, float air = kButtonAir) {
            Draw();
            ExpectPassesTheMatrix(node, EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit(node, output.width, output.height, air);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas(node), air);
            EXPECT_TRUE(cut.empty()) << node << ": " << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        }

        std::string Text(const char* node, const char* element) const {
            return DisplayText(Element(node, element));
        }
    };

    TEST_F(MahjongHudOnlineTest, BothCanvasesAreModalScreenCanvasesWithAFloorAndHiddenAtTheStart) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        for (const char* node : {kOnline, kEntry}) {
            const auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            canvas->UpdateLayout(kViewWidth, kViewHeight);  // the reflected fields of the component reach the layout on its first update
            const UICanvasScaleParams& params = canvas->GetLayout().params;
            EXPECT_EQ(params.space, CanvasSpace::Screen) << node;
            EXPECT_EQ(params.scaleMode, CanvasScaleMode::ScaleWithScreen) << node;
            EXPECT_GE(params.minScale, 0.6f) << node << ": a 56 unit control stays above 32 px";
            EXPECT_FALSE(canvas->TakesInput()) << node << " is hidden until the component shows it";
        }
        ASSERT_NO_FATAL_FAILURE(SetCanvasShown(kOnline, true));
        Draw();
        EXPECT_TRUE(Canvas(kOnline)->TakesInput());
        EXPECT_NE(Element(kOnline, "OnlineDim"), nullptr);
    }

    TEST_F(MahjongHudOnlineTest, EveryControlCarriesItsActionAndItsValue) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        struct Control {
            const char* node;
            std::string element;
            const char* action;
            std::string value;
        };
        std::vector<Control> controls = {
            {kOnline, "OnlineClose", "MahjongOnlineClose", ""},       {kOnline, "OnlineMenuHost", "MahjongOnlineHost", ""},
            {kOnline, "OnlineMenuJoin", "MahjongOnlineJoin", ""},     {kOnline, "OnlineMenuReturn", "MahjongOnlineResume", ""},
            {kOnline, "OnlineName", "MahjongOnlineField", "name"},    {kOnline, "OnlineCode", "MahjongOnlineField", "code"},
            {kOnline, "OnlineHands16", "MahjongOnlineHands", "16"},   {kOnline, "OnlineHands8", "MahjongOnlineHands", "8"},
            {kOnline, "OnlineCreate", "MahjongOnlineCreate", ""},     {kOnline, "OnlineHintsAllowed", "MahjongOnlineHints", "allowed"},
            {kOnline, "OnlineHintsOff", "MahjongOnlineHints", "off"}, {kOnline, "OnlineEnter", "MahjongOnlineEnter", ""},
            {kOnline, "OnlineStart", "MahjongOnlineStart", ""},       {kOnline, "OnlineLeave", "MahjongOnlineLeave", ""},
            {kEntry, "EntryDelete", "MahjongOnlineBackspace", ""},    {kEntry, "EntryDone", "MahjongOnlineDone", ""},
        };
        for (size_t seat = 0; seat < kSeats; ++seat) {
            controls.push_back({kOnline, Format("OnlineSit{}", seat).c_str(), "MahjongOnlineSit", Format("{}", seat).c_str()});
        }
        for (const Control& control : controls) {
            const UIElement* element = Element(control.node, control.element.c_str());
            ASSERT_NE(element, nullptr) << control.element;
            EXPECT_EQ(ButtonAction(element), PoolString::Intern(control.action)) << control.element;
            EXPECT_EQ(ButtonValue(element), PoolString::Intern(control.value.c_str())) << control.element;
        }
        // The pad: key i sends the index i, in rows of eight
        for (size_t key = 0; key < TextEntry::kPadMaxKeys; ++key) {
            const UIElement* element = Element(kEntry, Format("PadKey{}", key).c_str());
            ASSERT_NE(element, nullptr) << key;
            EXPECT_EQ(ButtonAction(element), "MahjongOnlineKey"_intern) << key;
            EXPECT_EQ(ButtonValue(element), PoolString::Intern(Format("{}", key).c_str())) << key;
        }
        const auto* level = Element(kOnline, "OnlineLevel");
        ASSERT_NE(level, nullptr);
        ASSERT_NE(level->Cast<UIDropdown>(), nullptr);
        EXPECT_EQ(level->Cast<UIDropdown>()->action, "MahjongOnlineLevel"_intern);
        for (int number = 1; number <= 10; ++number) {
            const UIElement* option = Element(kOnline, Format("OnlineLevel{}", number).c_str());
            ASSERT_NE(option, nullptr) << number;
            EXPECT_EQ(ButtonValue(option), PoolString::Intern(Format("{}", number).c_str())) << number;
        }
    }

    TEST_F(MahjongHudOnlineTest, TheCanvasesAsShippedPassTheMatrixWithNoVariablesSet) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        for (const char* node : {kOnline, kEntry}) {
            ASSERT_NO_FATAL_FAILURE(SetCanvasShown(node, true));
            ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
                SCOPED_TRACE(language.ToStringView().data());
                Draw();
                ExpectPassesTheMatrix(node, EngineTouchMinimum());
            });
        }
    }

    TEST_F(MahjongHudOnlineTest, EveryPageOfThePanelPassesTheSizeMatrixInEveryLanguageWithTheLongestTexts) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        ASSERT_NO_FATAL_FAILURE(SetCanvasShown(kOnline, true));
        struct Page {
            const char* label;
            const std::vector<const char*>* shown;
        };
        const Page pages[] = {{"Menu", &kMenuPage}, {"Host", &kHostPage}, {"Join", &kJoinPage}, {"Lobby", &kLobbyPage}};
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            SetWorstVars();
            for (const Page& page : pages) {
                // every line that can stand under the title, then none
                std::vector<const char*> lines(std::begin(kInfoLines), std::end(kInfoLines));
                lines.push_back(nullptr);
                for (const char* line : lines) {
                    SCOPED_TRACE(Format("{} {}", page.label, line != nullptr ? line : "no line").c_str());
                    ASSERT_NO_FATAL_FAILURE(ShowOnly(*page.shown, line));
                    ExpectTheCanvasFits(kOnline);
                }
            }
        });
    }

    TEST_F(MahjongHudOnlineTest, TheEntryPageHoldsAllFortyEightKeysInEveryLanguageAndEveryKeyIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        ASSERT_NO_FATAL_FAILURE(SetCanvasShown(kEntry, true));
        const auto canvas = Canvas(kEntry);
        ASSERT_TRUE(canvas);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            SetWorstVars();
            for (const bool code : {false, true}) {
                SCOPED_TRACE(code ? "code" : "name");
                ASSERT_TRUE(canvas->SetElementVisible("EntryCaptionName"_intern, !code));
                ASSERT_TRUE(canvas->SetElementVisible("EntryCaptionCode"_intern, code));
                Draw();
                canvas->UpdateLayout(kViewWidth, kViewHeight);
                for (size_t key = 0; key < TextEntry::kPadMaxKeys; ++key) {
                    const UIElement* element = Element(kEntry, Format("PadKey{}", key).c_str());
                    ASSERT_NE(element, nullptr) << key;
                    EXPECT_GE(element->rect.h, kMinControlHeight) << "PadKey" << key << " at the design size";
                }
                ExpectTheCanvasFits(kEntry, kKeyAir);
            }
        });
    }

    TEST_F(MahjongHudOnlineTest, ThePagesSayTheirWordsInTheLanguageOfTheGame) {
        ASSERT_NO_FATAL_FAILURE(Prepare());
        ASSERT_NO_FATAL_FAILURE(SetCanvasShown(kOnline, true));
        ASSERT_NO_FATAL_FAILURE(ShowOnly(std::vector<const char*>{}));
        CoreManager::GetUIVars().Set("onlineRoom"_intern, "K7M2Q");
        Draw();
        EXPECT_EQ(Text(kOnline, "OnlineTitleMenu"), "Play online");
        EXPECT_EQ(Text(kOnline, "OnlineMenuReturn"), "Return to room K7M2Q");
        EXPECT_EQ(Text(kOnline, "OnlineTitleLobby"), "Room K7M2Q");
        EXPECT_EQ(Text(kOnline, "OnlineSit2"), "Sit here");
        EXPECT_EQ(Text(kOnline, "OnlineHandsCaption"), "Hands");
        EXPECT_EQ(Text(kOnline, "OnlineHintsCaption"), "Hints");
        EXPECT_EQ(Text(kOnline, "OnlineHintsAllowed"), "Allowed");
        EXPECT_EQ(Text(kOnline, "OnlineHintsOff"), "Not allowed");
        EXPECT_EQ(Text(kOnline, "OnlineSeatCaption3"), "Seat 4");
        EXPECT_EQ(Text(kOnline, "OnlineStart"), "Start");
        EXPECT_EQ(Text(kOnline, "OnlineLeave"), "Leave");

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();
        EXPECT_EQ(Text(kOnline, "OnlineMenuHost"), "Создать комнату");
        EXPECT_EQ(Text(kOnline, "OnlineMenuReturn"), "Вернуться в комнату K7M2Q");
        EXPECT_EQ(Text(kOnline, "OnlineTitleLobby"), "Комната K7M2Q");
        EXPECT_EQ(Text(kOnline, "OnlineSit0"), "Сесть здесь");
        EXPECT_EQ(Text(kOnline, "OnlineLevelCaption"), "Уровень");
        EXPECT_EQ(Text(kOnline, "OnlineHintsCaption"), "Помощь");
        EXPECT_EQ(Text(kOnline, "OnlineHintsAllowed"), "Можно");
        EXPECT_EQ(Text(kOnline, "OnlineHintsOff"), "Нельзя");
        EXPECT_EQ(Text(kOnline, "OnlineCreate"), "Создать");
        EXPECT_EQ(Text(kOnline, "OnlineEnter"), "Войти");
        EXPECT_EQ(Text(kEntry, "EntryDone"), "Готово");
        EXPECT_EQ(Text(kEntry, "EntryDelete"), "Стереть");
        CoreManager::GetUIVars().Set("onlineRoom"_intern, "");
    }

}  // namespace BECore::Mahjong::Tests
