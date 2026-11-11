#include "AnalyzerReport.h"

#include <EASTL/sort.h>
#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong::Analyzer {

    namespace {

        constexpr int kActionMinWidth = 22;
        /// "-1234.56", "+1234.56".
        constexpr int kNumberWidth = 9;
        constexpr int kHalfWidth = 8;
        constexpr const char* kColumnGap = "  ";
        constexpr eastl::string_view kNoteBest = "best";
        constexpr eastl::string_view kNoteNormal = "Normal";
        constexpr eastl::string_view kNoteLogged = "logged";
        constexpr eastl::string_view kNoteWorseThanBest = "worse than best";
        constexpr eastl::string_view kNoteBetterThanNormal = "better than Normal";
        constexpr eastl::string_view kNoteWorseThanNormal = "worse than Normal";
        constexpr eastl::string_view kNoteSeparator = ", ";
        constexpr eastl::string_view kOpponentsKind = "normal";
        constexpr eastl::string_view kHiddenKind = "uniform";
        constexpr eastl::string_view kPointsKind = "this hand";

        eastl::string Text(std::string_view view) {
            return eastl::string(view.data(), view.size());
        }

        eastl::string Text(eastl::string_view view) {
            return eastl::string(view.data(), view.size());
        }

        /// A number as it is printed and stored: `kReportPrecision` digits, and never a negative zero.
        double Rounded(double value) {
            constexpr double kScale = 100.0;
            static_assert(kReportPrecision == 2, "Rounded() scales by 10^kReportPrecision");
            const double rounded = std::round(value * kScale) / kScale;
            return rounded == 0.0 ? 0.0 : rounded;
        }

        eastl::string TilesText(const eastl::vector<Tile>& tiles) {
            eastl::string text;
            for (const Tile tile : tiles) {
                text.append(Format("{}{}", text.empty() ? "" : " ", ToString(tile)));
            }
            return text;
        }

        /// The lowest tile of a chow and the next two of its suit.
        eastl::string ChowText(Tile first) {
            if (!first.IsSuit()) {
                return Text(ToString(first));
            }
            constexpr int kChowLength = 3;
            eastl::string text;
            for (int step = 0; step < kChowLength; ++step) {
                text.append(Format("{}{}", step == 0 ? "" : "-", ToString(Tile::Suited(first.Suit(), first.Rank() + step))));
            }
            return text;
        }

        eastl::string MeldText(const Meld& meld) {
            eastl::string text;
            switch (meld.kind) {
                case MeldKind::Chow:
                    text = Format("chow {}", ChowText(meld.first));
                    break;
                case MeldKind::Pung:
                    text = Format("pung {}", ToString(meld.first));
                    break;
                case MeldKind::Kong:
                    text = Format("kong {}", ToString(meld.first));
                    break;
            }
            if (meld.concealed) {
                text.append(" concealed");
            } else if (meld.fromDiscard) {
                text.append(Format(" from seat {}", static_cast<int>(meld.from)));
            }
            return text;
        }

        eastl::string MeldsText(const eastl::vector<Meld>& melds) {
            eastl::string text;
            for (const Meld& meld : melds) {
                text.append(Format("{}{}", text.empty() ? "" : "; ", MeldText(meld)));
            }
            return text;
        }

        /// The rows in the order of the report: highest mean first, the earlier row first among equal means.
        eastl::vector<size_t> ReportOrder(const PositionAnalysis& analysis) {
            eastl::vector<size_t> order;
            order.reserve(analysis.rows.size());
            for (size_t index = 0; index < analysis.rows.size(); ++index) {
                order.push_back(index);
            }
            eastl::sort(order.begin(), order.end(), [&analysis](size_t a, size_t b) {
                const double meanA = analysis.rows[a].ev.mean;
                const double meanB = analysis.rows[b].ev.mean;
                return meanA != meanB ? meanA > meanB : a < b;
            });
            return order;
        }

        eastl::string ClaimText(const HandAction& action) {
            switch (action.claim) {
                case ClaimKind::Chow:
                    return Format("claim chow {}", ChowText(action.chowFirst));
                case ClaimKind::Pung:
                    return "claim pung";
                case ClaimKind::Kong:
                    return "claim kong";
                case ClaimKind::Mahjong:
                    return "claim mahjong";
            }
            return "claim pung";
        }

        bool IsLogged(const ActionRow& row, const PositionLabel& label) {
            return label.hasLogged && row.action == label.logged;
        }

        /// The notes of a row, in a fixed order.
        eastl::string NoteText(const ActionRow& row, const PositionLabel& label) {
            eastl::vector<eastl::string_view> notes;
            if (row.isBest) {
                notes.push_back(kNoteBest);
            }
            if (row.isNormal) {
                notes.push_back(kNoteNormal);
            }
            if (IsLogged(row, label)) {
                notes.push_back(kNoteLogged);
            }
            if (row.vsBest.significant) {
                notes.push_back(kNoteWorseThanBest);
            }
            if (row.vsNormal.significant) {
                notes.push_back(row.vsNormal.interval.mean > 0.0 ? kNoteBetterThanNormal : kNoteWorseThanNormal);
            }
            eastl::string text;
            for (const eastl::string_view note : notes) {
                text.append(Format("{}{}", text.empty() ? "" : Text(kNoteSeparator), Text(note)));
            }
            return text;
        }

        // ----- text -----

        void AddLeft(eastl::string& text, const eastl::string& cell, int width) {
            text.append(Format("{:<{}}", cell, width));
        }

        void AddRight(eastl::string& text, const eastl::string& cell, int width) {
            text.append(Format("{}{:>{}}", kColumnGap, cell, width));
        }

        /// Ends the line: the spaces at its right end go, a newline comes.
        void EndLine(eastl::string& text, size_t lineStart) {
            size_t end = text.size();
            while (end > lineStart && text[end - 1] == ' ') {
                --end;
            }
            text.resize(end);
            text.push_back('\n');
        }

        eastl::string PlainCell(const Arena::MeanInterval& interval) {
            return interval.n > 0 ? Format("{:.{}f}", Rounded(interval.mean), kReportPrecision) : eastl::string();
        }

        eastl::string SignedCell(const Arena::MeanInterval& interval) {
            return interval.n > 0 ? Format("{:+.{}f}", Rounded(interval.mean), kReportPrecision) : eastl::string();
        }

        eastl::string HalfCell(const Arena::MeanInterval& interval) {
            return interval.n > 0 && interval.hasInterval ? Format("{:.{}f}", Rounded(interval.half), kReportPrecision) : eastl::string();
        }

        eastl::string LabelLine(const PositionLabel& label) {
            if (label.hand > 0) {
                return Format("position: {}, hand {}, action {}\n", label.source.empty() ? eastl::string("(log)") : label.source, label.hand, label.actionIndex);
            }
            return label.source.empty() ? eastl::string() : Format("position: {}\n", label.source);
        }

        void AddHeader(eastl::string& text, const PositionAnalysis& analysis) {
            text.append("EV of every legal action, in points at the end of the hand\n");
            text.append(Format("The opponents are Normal players (level {}) and the seat itself plays Normal after the chosen action.\n", LevelNumber(ComputerLevel::Normal)));
            text.append("Hidden tiles are sampled uniformly from the tiles the seat cannot see (the approximations of ComputerWorld.h).\n");
            text.append("The points are those of this hand only (a drawn hand pays 0), against Normal bots: like a poker chart against a known pool, not an equilibrium.\n");
            text.append(Format("worlds: asked {}, played {}, dropped {}; seed {}; Normal's choice seed {}\n", analysis.worldsAsked, analysis.worldsPlayed, analysis.worldsDropped, analysis.config.seed,
                               analysis.config.normalSeed));
            text.append(Format("+- is {} standard errors over the worlds (a 95 percent interval); a world is one sample of the hidden tiles with its rollout seed.\n", Arena::kIntervalZ));
            text.append("vs best and vs Normal are paired by world (the same worlds and rollout seeds), so their intervals are tighter than those of two means;\n");
            text.append("vs best has a small bias: the best row was picked by the same worlds.\n");
        }

        void AddPosition(eastl::string& text, const TableView& view, const PositionLabel& label) {
            text.push_back('\n');
            text.append(LabelLine(label));
            text.append(Format("seat {} ({} wind), round {}, phase {}, live tiles {}\n", static_cast<int>(view.seat), Text(EnumUtils<Wind>::ToString(view.seatWind)),
                               Text(EnumUtils<Wind>::ToString(view.roundWind)), Text(EnumUtils<Phase>::ToString(view.phase)), view.liveCount));
            text.append(Format("hand: {}\n", TilesText(view.own.concealed)));
            if (!view.own.melds.empty()) {
                text.append(Format("melds: {}\n", MeldsText(view.own.melds)));
            }
            if (!view.own.bonus.empty()) {
                text.append(Format("bonus: {}\n", TilesText(view.own.bonus)));
            }
            if (view.pendingTile.IsValid()) {
                text.append(Format("pending: {} from seat {}\n", ToString(view.pendingTile), static_cast<int>(view.currentSeat)));
            }
            for (size_t seat = 0; seat < kSeatCount; ++seat) {
                if (static_cast<Seat>(seat) == view.seat) {
                    continue;
                }
                const PublicSeat& other = view.seats[seat];
                text.append(Format("seat {}: discards {}", seat, other.discardCount));
                if (!other.openMelds.empty()) {
                    text.append(Format(", melds {}", MeldsText(other.openMelds)));
                }
                if (other.concealedKongs > 0) {
                    text.append(Format(", concealed kongs {}", other.concealedKongs));
                }
                if (!other.bonus.empty()) {
                    text.append(Format(", bonus {}", TilesText(other.bonus)));
                }
                if (other.callingDeclared) {
                    text.append(", calling");
                }
                text.push_back('\n');
            }
        }

        void AddTable(eastl::string& text, const PositionAnalysis& analysis, const PositionLabel& label) {
            int actionWidth = kActionMinWidth;
            for (const ActionRow& row : analysis.rows) {
                actionWidth = eastl::max(actionWidth, static_cast<int>(ActionNotation(row.action).size()));
            }
            text.push_back('\n');
            size_t start = text.size();
            AddLeft(text, "action", actionWidth);
            AddRight(text, "EV", kNumberWidth);
            AddRight(text, "+-", kHalfWidth);
            AddRight(text, "vs best", kNumberWidth);
            AddRight(text, "+-", kHalfWidth);
            AddRight(text, "vs Normal", kNumberWidth);
            AddRight(text, "+-", kHalfWidth);
            text.append(Format("{}note", kColumnGap));
            EndLine(text, start);
            for (const size_t index : ReportOrder(analysis)) {
                const ActionRow& row = analysis.rows[index];
                start = text.size();
                AddLeft(text, ActionNotation(row.action), actionWidth);
                AddRight(text, PlainCell(row.ev), kNumberWidth);
                AddRight(text, HalfCell(row.ev), kHalfWidth);
                AddRight(text, SignedCell(row.vsBest.interval), kNumberWidth);
                AddRight(text, HalfCell(row.vsBest.interval), kHalfWidth);
                AddRight(text, SignedCell(row.vsNormal.interval), kNumberWidth);
                AddRight(text, HalfCell(row.vsNormal.interval), kHalfWidth);
                text.append(Format("{}{}", kColumnGap, NoteText(row, label)));
                EndLine(text, start);
            }
        }

        // ----- JSON -----

        Json NumberOrNull(double value, bool present) {
            return present ? Json(Rounded(value)) : Json(nullptr);
        }

        Json DifferenceToJson(const ActionDifference& difference) {
            if (difference.interval.n == 0) {
                return Json(nullptr);
            }
            Json json = Json::object();
            json["mean"] = Rounded(difference.interval.mean);
            json["half"] = NumberOrNull(difference.interval.half, difference.interval.hasInterval);
            json["significant"] = difference.significant;
            return json;
        }

        Json RowToJson(const ActionRow& row, const PositionLabel& label) {
            Json json = Json::object();
            json["action"] = ActionToJson(row.action);
            json["text"] = MakeJsonString(ActionNotation(row.action).c_str());
            json["ev"] = NumberOrNull(row.ev.mean, row.ev.n > 0);
            json["half"] = NumberOrNull(row.ev.half, row.ev.n > 0 && row.ev.hasInterval);
            json["vsBest"] = DifferenceToJson(row.vsBest);
            json["vsNormal"] = DifferenceToJson(row.vsNormal);
            json["best"] = row.isBest;
            json["normal"] = row.isNormal;
            json["logged"] = IsLogged(row, label);
            return json;
        }

    }  // namespace

    PositionLabel LabelOfLogPosition(eastl::string_view file, const LogPosition& position) {
        PositionLabel label;
        label.source = Text(file);
        label.hand = position.hand;
        label.actionIndex = position.actionIndex;
        label.hasLogged = true;
        label.logged = position.logged;
        return label;
    }

    eastl::string ActionNotation(const HandAction& action) {
        switch (action.kind) {
            case ActionKind::Discard:
                return Format("discard {}{}", ToString(action.tile), action.declareCalling ? " declare" : "");
            case ActionKind::DeclareWin:
                return "win";
            case ActionKind::ConcealedKong:
                return Format("kong {} concealed", ToString(action.tile));
            case ActionKind::UpgradeKong:
                return Format("upgrade {}", ToString(action.tile));
            case ActionKind::Claim:
                return ClaimText(action);
            case ActionKind::Pass:
                return "pass";
        }
        return "pass";
    }

    eastl::string FormatAnalysisReport(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label) {
        eastl::string text;
        AddHeader(text, analysis);
        AddPosition(text, view, label);
        AddTable(text, analysis, label);
        return text;
    }

    Json AnalysisToJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label) {
        Json json = Json::object();
        json["version"] = kAnalysisJsonVersion;
        json["source"] = label.source.empty() ? Json(nullptr) : MakeJsonString(label.source.c_str());
        json["hand"] = label.hand > 0 ? Json(label.hand) : Json(nullptr);
        json["actionIndex"] = label.hand > 0 ? Json(label.actionIndex) : Json(nullptr);
        json["logged"] = label.hasLogged ? ActionToJson(label.logged) : Json(nullptr);
        json["seat"] = SeatOrNull(view.seat);
        json["seatWind"] = MakeJsonString(EnumUtils<Wind>::ToString(view.seatWind));
        json["roundWind"] = MakeJsonString(EnumUtils<Wind>::ToString(view.roundWind));
        json["phase"] = MakeJsonString(EnumUtils<Phase>::ToString(view.phase));
        json["own"] = MakeJsonString(TilesText(view.own.concealed).c_str());
        json["pending"] = view.pendingTile.IsValid() ? TileToJson(view.pendingTile) : Json(nullptr);
        json["liveTiles"] = view.liveCount;
        json["worldsAsked"] = analysis.worldsAsked;
        json["worldsPlayed"] = analysis.worldsPlayed;
        json["worldsDropped"] = analysis.worldsDropped;
        json["seed"] = analysis.config.seed;
        json["normalSeed"] = analysis.config.normalSeed;
        json["intervalZ"] = Arena::kIntervalZ;
        json["opponents"] = MakeJsonString(kOpponentsKind);
        json["opponentLevel"] = LevelNumber(ComputerLevel::Normal);
        json["seatAfterAction"] = MakeJsonString(kOpponentsKind);
        json["hiddenTiles"] = MakeJsonString(kHiddenKind);
        json["points"] = MakeJsonString(kPointsKind);
        Json actions = Json::array();
        for (const size_t index : ReportOrder(analysis)) {
            actions.push_back(RowToJson(analysis.rows[index], label));
        }
        json["actions"] = actions;
        return json;
    }

    eastl::string FormatAnalysisJson(const TableView& view, const PositionAnalysis& analysis, const PositionLabel& label) {
        return DumpJson(AnalysisToJson(view, analysis, label));
    }

}  // namespace BECore::Mahjong::Analyzer
