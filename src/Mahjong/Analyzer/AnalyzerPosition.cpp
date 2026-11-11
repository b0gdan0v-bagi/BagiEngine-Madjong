#include "AnalyzerPosition.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong::Analyzer {

    namespace {

        using Failure = std::unexpected<eastl::string>;

        // ---------------------------------------------------------------------------------------------------------------------------
        // Positions from a log
        // ---------------------------------------------------------------------------------------------------------------------------

        eastl::string ReplayText(const ReplayError& error) {
            return Format("the log does not replay: event {} ({})", error.index, EnumUtils<ReplayProblem>::ToString(error.problem));
        }

        /// The indexes of the `Action` events of hand number `hand`, in log order. Hand K is what follows K-1 `Settlement` events.
        std::expected<eastl::vector<size_t>, eastl::string> ActionEventsOfHand(const eastl::vector<MatchLogEvent>& events, int hand) {
            int settlements = 0;
            for (const MatchLogEvent& event : events) {
                settlements += event.kind == MatchLogKind::Settlement ? 1 : 0;
            }
            if (hand < 1 || hand > settlements + 1) {
                return Failure(Format("hand {} is out of range: the hands of this log are numbered 1..{} (hand K is what follows K-1 settlements)", hand, settlements + 1));
            }
            eastl::vector<size_t> actions;
            int current = 1;
            for (size_t index = 0; index < events.size(); ++index) {
                if (events[index].kind == MatchLogKind::Settlement) {
                    ++current;
                } else if (events[index].kind == MatchLogKind::Action && current == hand) {
                    actions.push_back(index);
                }
            }
            if (actions.empty()) {
                return Failure(Format("hand {} has no action in this log", hand));
            }
            return actions;
        }

        /// A fresh `Match` that has replayed the events before `end`.
        std::expected<Match, eastl::string> ReplayedBefore(const eastl::vector<MatchLogEvent>& events, size_t end) {
            const eastl::vector<MatchLogEvent> prefix(events.begin(), events.begin() + static_cast<ptrdiff_t>(end));
            Match match;
            if (const ReplayStatus replayed = ReplayMatchLog(prefix, match); !replayed) {
                return Failure(ReplayText(replayed.error()));
            }
            return match;
        }

        std::expected<LogPosition, eastl::string> MakeLogPosition(const HandModel& model, const MatchLogEvent& event, size_t eventIndex, int hand, int actionIndex) {
            if (!IsSeat(event.seat)) {
                return Failure(Format("event {} names seat {}, which is no seat", eventIndex, event.seat));
            }
            LogPosition position;
            position.view = MakeTableView(model, event.seat);
            if (position.view.legal.empty()) {
                return Failure(Format("seat {} has nothing to decide before event {} (the log does not match the rules here)", event.seat, eventIndex));
            }
            position.seat = event.seat;
            position.logged = event.action;
            position.hand = hand;
            position.actionIndex = actionIndex;
            return position;
        }

        // ---------------------------------------------------------------------------------------------------------------------------
        // Positions from the tile notation
        // ---------------------------------------------------------------------------------------------------------------------------

        using Words = eastl::vector<std::string_view>;
        using Problem = std::optional<eastl::string>;

        constexpr char kCommentMark = '#';
        constexpr std::string_view kBlanks = " \t\r";
        constexpr std::string_view kFromWord = "from";
        constexpr std::string_view kFirstWord = "first";
        constexpr std::string_view kNoneWord = "none";
        constexpr int kMeldLimit = kSetsInHand;
        constexpr int kBonusCopies = 1;

        eastl::string LineError(int line, const eastl::string& text) {
            return Format("line {}: {}", line, text.c_str());
        }

        std::optional<int> ParseNumber(std::string_view word) {
            int value = 0;
            const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
            if (error != std::errc{} || end != word.data() + word.size() || value < 0) {
                return std::nullopt;
            }
            return value;
        }

        std::optional<Seat> ParseSeat(std::string_view word) {
            const std::optional<int> number = ParseNumber(word);
            return number.has_value() && IsSeat(*number) ? number : std::nullopt;
        }

        /// What a seat is given in the text.
        struct SeatDraft {
            eastl::vector<Meld> melds;
            eastl::vector<Tile> discards;
            eastl::vector<Tile> bonus;
            bool hasDiscards = false;
            bool hasBonus = false;
            bool calling = false;
            bool callingFirst = false;
            int callingLine = 0;
        };

        /// Everything the text says, before it is checked as a whole.
        struct Draft {
            Seat seat = 0;
            std::optional<Seat> east;
            std::optional<Wind> round;
            std::optional<Phase> phase;
            eastl::vector<Tile> hand;
            int handLine = 0;
            Tile drew;
            bool drewNone = false;
            bool hasDrew = false;
            int drewLine = 0;
            Tile pending;
            Seat pendingFrom = kNoSeat;
            int pendingLine = 0;
            std::optional<int> wall;
            int wallLine = 0;
            eastl::array<SeatDraft, kSeatCount> seats;
            /// The copies named so far, per tile id; a kind has four, a bonus tile one.
            eastl::array<int, kTileIdCount> used{};
            bool seenSeat = false;
            bool seenEast = false;
        };

        /// Notes a copy of `tile` as named; false when that is more than the set has.
        bool Take(Draft& draft, Tile tile) {
            const int limit = tile.IsBonus() ? kBonusCopies : kCopiesPerTile;
            return ++draft.used[tile.Id()] <= limit;
        }

        Problem TooMany(int line, Tile tile) {
            return LineError(line, Format("more than {} copies of {}", tile.IsBonus() ? kBonusCopies : kCopiesPerTile, ToString(tile)));
        }

        /// Reads `words[from..)` as playable tiles (or bonus tiles) and notes them; the problem names the line.
        Problem TakeTiles(Draft& draft, int line, const Words& words, size_t from, bool bonus, eastl::vector<Tile>& out) {
            for (size_t i = from; i < words.size(); ++i) {
                const std::optional<Tile> tile = TryParse(words[i]);
                if (!tile.has_value() || tile->IsBonus() != bonus) {
                    return LineError(line, Format("'{}' is not {} tile in the notation of Tile.h", words[i], bonus ? "a flower or season" : "a playable"));
                }
                if (!Take(draft, *tile)) {
                    return TooMany(line, *tile);
                }
                out.push_back(*tile);
            }
            return std::nullopt;
        }

        Problem ParseMeld(Draft& draft, int line, const Words& words) {
            constexpr size_t kMinWords = 4;
            constexpr size_t kFromWords = 6;
            if (words.size() != kMinWords && words.size() != kFromWords) {
                return LineError(line, Format("meld needs a seat, chow|pung|kong|ckong, a tile and optionally 'from <seat>'"));
            }
            const std::optional<Seat> owner = ParseSeat(words[1]);
            if (!owner.has_value()) {
                return LineError(line, Format("'{}' is not a seat (0..3)", words[1]));
            }
            Meld meld;
            const std::string_view kind = words[2];
            if (kind == "chow") {
                meld.kind = MeldKind::Chow;
            } else if (kind == "pung") {
                meld.kind = MeldKind::Pung;
            } else if (kind == "kong" || kind == "ckong") {
                meld.kind = MeldKind::Kong;
                meld.concealed = kind == "ckong";
            } else {
                return LineError(line, Format("'{}' is not a meld kind (chow, pung, kong, ckong)", kind));
            }
            const std::optional<Tile> tile = TryParse(words[3]);
            if (!tile.has_value() || !tile->IsPlayable()) {
                return LineError(line, Format("'{}' is not a playable tile", words[3]));
            }
            meld.first = *tile;
            meld.claimed = *tile;
            std::optional<Seat> from;
            if (words.size() == kFromWords) {
                from = words[4] == kFromWord ? ParseSeat(words[5]) : std::nullopt;
                if (!from.has_value() || *from == *owner || meld.concealed) {
                    return LineError(line, Format("'{} {}' is not 'from' and another seat (0..3), or the meld is a concealed kong", words[4], words[5]));
                }
                meld.from = *from;
                meld.fromDiscard = true;
            } else {
                meld.from = *owner;
            }
            if (meld.kind == MeldKind::Chow) {
                if (!tile->IsSuit() || tile->Rank() > kLastChowStartRank) {
                    return LineError(line, Format("a chow starts at a suit tile of rank 1..{}, not {}", kLastChowStartRank, words[3]));
                }
                if (from.has_value() && NextSeat(*from) != *owner) {
                    return LineError(line, Format("a chow is only claimed from the seat before: seat {} cannot take it from seat {}", *owner, *from));
                }
                for (const Tile member : ChowTiles(*tile)) {
                    if (!Take(draft, member)) {
                        return TooMany(line, member);
                    }
                }
            } else {
                const int copies = meld.kind == MeldKind::Kong ? kCopiesPerTile : kTilesPerSet;
                for (int copy = 0; copy < copies; ++copy) {
                    if (!Take(draft, *tile)) {
                        return TooMany(line, *tile);
                    }
                }
            }
            SeatDraft& seat = draft.seats[*owner];
            if (static_cast<int>(seat.melds.size()) >= kMeldLimit) {
                return LineError(line, Format("seat {} has more than {} melds", *owner, kMeldLimit));
            }
            seat.melds.push_back(meld);
            return std::nullopt;
        }

        /// One line of the text. `words[0]` is the key.
        Problem ApplyLine(Draft& draft, int line, const Words& words) {
            const std::string_view key = words[0];
            const auto needSeat = [&](size_t index) -> std::optional<Seat> { return index < words.size() ? ParseSeat(words[index]) : std::nullopt; };
            const auto once = [&](bool& seen) -> Problem {
                if (seen) {
                    return LineError(line, Format("'{}' is given twice", key));
                }
                seen = true;
                return std::nullopt;
            };

            if (key == "seat" || key == "east") {
                bool& seen = key == "seat" ? draft.seenSeat : draft.seenEast;
                if (const Problem again = once(seen)) {
                    return again;
                }
                const std::optional<Seat> seat = words.size() == 2 ? needSeat(1) : std::nullopt;
                if (!seat.has_value()) {
                    return LineError(line, Format("{} needs one seat number 0..3", key));
                }
                if (key == "seat") {
                    draft.seat = *seat;
                } else {
                    draft.east = *seat;
                }
                return std::nullopt;
            }
            if (key == "round") {
                const std::optional<Wind> wind = words.size() == 2 ? EnumUtils<Wind>::Cast(words[1]) : std::nullopt;
                if (!wind.has_value() || draft.round.has_value()) {
                    return LineError(line, Format("round needs one wind (East, South, West, North), once"));
                }
                draft.round = wind;
                return std::nullopt;
            }
            if (key == "phase") {
                if (draft.phase.has_value() || words.size() != 2) {
                    return LineError(line, Format("phase needs one word (discard or claim), once"));
                }
                if (words[1] == "discard") {
                    draft.phase = Phase::Discard;
                } else if (words[1] == "claim") {
                    draft.phase = Phase::Claim;
                } else {
                    return LineError(line, Format("phase '{}' cannot be written: use discard or claim (a position in a robbed-kong window comes from a log)", words[1]));
                }
                return std::nullopt;
            }
            if (key == "hand") {
                if (draft.handLine != 0) {
                    return LineError(line, Format("'hand' is given twice"));
                }
                draft.handLine = line;
                if (words.size() < 2) {
                    return LineError(line, Format("hand needs tiles"));
                }
                return TakeTiles(draft, line, words, 1, false, draft.hand);
            }
            if (key == "drew") {
                if (draft.hasDrew || words.size() != 2) {
                    return LineError(line, Format("drew needs one tile or 'none', once"));
                }
                draft.hasDrew = true;
                draft.drewLine = line;
                if (words[1] == kNoneWord) {
                    draft.drewNone = true;
                    return std::nullopt;
                }
                const std::optional<Tile> tile = TryParse(words[1]);
                if (!tile.has_value() || !tile->IsPlayable()) {
                    return LineError(line, Format("'{}' is not a playable tile", words[1]));
                }
                draft.drew = *tile;
                return std::nullopt;
            }
            if (key == "meld") {
                return ParseMeld(draft, line, words);
            }
            if (key == "discards" || key == "bonus") {
                const std::optional<Seat> seat = needSeat(1);
                if (!seat.has_value()) {
                    return LineError(line, Format("{} needs a seat 0..3 first", key));
                }
                SeatDraft& target = draft.seats[*seat];
                const bool bonus = key == "bonus";
                if (bonus && words.size() < 3) {
                    return LineError(line, Format("bonus needs a seat and tiles"));
                }
                bool& seen = bonus ? target.hasBonus : target.hasDiscards;
                if (const Problem again = once(seen)) {
                    return LineError(line, Format("{} of seat {} is given twice", key, *seat));
                }
                return TakeTiles(draft, line, words, 2, bonus, bonus ? target.bonus : target.discards);
            }
            if (key == "pending") {
                const std::optional<Tile> tile = words.size() == 4 ? TryParse(words[1]) : std::nullopt;
                const std::optional<Seat> from = words.size() == 4 && words[2] == kFromWord ? needSeat(3) : std::nullopt;
                if (draft.pendingLine != 0 || !tile.has_value() || !tile->IsPlayable() || !from.has_value()) {
                    return LineError(line, Format("pending needs 'pending <tile> from <seat>', once"));
                }
                if (!Take(draft, *tile)) {
                    return TooMany(line, *tile);
                }
                draft.pending = *tile;
                draft.pendingFrom = *from;
                draft.pendingLine = line;
                return std::nullopt;
            }
            if (key == "calling") {
                const std::optional<Seat> seat = needSeat(1);
                const bool first = words.size() == 3 && words[2] == kFirstWord;
                if (!seat.has_value() || (words.size() != 2 && !first)) {
                    return LineError(line, Format("calling needs a seat and optionally 'first'"));
                }
                SeatDraft& target = draft.seats[*seat];
                if (target.calling) {
                    return LineError(line, Format("calling of seat {} is given twice", *seat));
                }
                target.calling = true;
                target.callingFirst = first;
                target.callingLine = line;
                return std::nullopt;
            }
            if (key == "wall") {
                const std::optional<int> count = words.size() == 2 ? ParseNumber(words[1]) : std::nullopt;
                if (draft.wall.has_value() || !count.has_value()) {
                    return LineError(line, Format("wall needs one number of live tiles, once"));
                }
                draft.wall = count;
                draft.wallLine = line;
                return std::nullopt;
            }
            return LineError(line, Format("unknown key '{}'", key));
        }

        /// The text split into lines of words; empty lines and comments are gone. `numbers` holds the 1-based number of each kept line.
        void SplitLines(std::string_view text, eastl::vector<Words>& lines, eastl::vector<int>& numbers) {
            int number = 0;
            size_t start = 0;
            while (start <= text.size()) {
                const size_t end = text.find('\n', start);
                std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
                ++number;
                start = end == std::string_view::npos ? text.size() + 1 : end + 1;
                if (const size_t comment = line.find(kCommentMark); comment != std::string_view::npos) {
                    line = line.substr(0, comment);
                }
                Words words;
                size_t at = line.find_first_not_of(kBlanks);
                while (at != std::string_view::npos) {
                    const size_t stop = line.find_first_of(kBlanks, at);
                    words.push_back(line.substr(at, stop == std::string_view::npos ? std::string_view::npos : stop - at));
                    at = stop == std::string_view::npos ? stop : line.find_first_not_of(kBlanks, stop);
                }
                if (!words.empty()) {
                    lines.push_back(eastl::move(words));
                    numbers.push_back(number);
                }
            }
        }

        /// The state of the position as the text describes it, with the unseen tiles dealt in a fixed order.
        std::expected<HandState, eastl::string> BuildState(Draft& draft) {
            if (draft.handLine == 0) {
                return Failure(eastl::string("the position has no 'hand' line"));
            }
            const Phase phase = draft.phase.value_or(Phase::Discard);
            const bool claim = phase == Phase::Claim;
            if (claim && draft.pendingLine == 0) {
                return Failure(eastl::string("phase claim needs a 'pending <tile> from <seat>' line"));
            }
            if (!claim && draft.pendingLine != 0) {
                return Failure(LineError(draft.pendingLine, eastl::string("'pending' belongs to phase claim")));
            }
            if (claim && draft.hasDrew) {
                return Failure(LineError(draft.drewLine, eastl::string("'drew' belongs to phase discard")));
            }
            if (claim && draft.pendingFrom == draft.seat) {
                return Failure(LineError(draft.pendingLine, Format("the seat cannot claim its own discard (seat {})", draft.seat)));
            }
            const Seat east = draft.east.value_or(draft.seat);

            // The seat's own hand: its size follows the phase.
            const SeatDraft& own = draft.seats[draft.seat];
            const int ownTiles = static_cast<int>(draft.hand.size()) + kTilesPerSet * static_cast<int>(own.melds.size());
            const int expectedTiles = claim ? kDealtTiles : kDealtTiles + 1;
            if (ownTiles != expectedTiles) {
                return Failure(LineError(draft.handLine, Format("the hand has {} concealed tiles and {} melds, which makes {} tiles; a {} position holds {}", draft.hand.size(), own.melds.size(),
                                                                ownTiles, claim ? "claim" : "discard", expectedTiles)));
            }

            HandState state;
            state.settings.flowers = false;
            for (const SeatDraft& seat : draft.seats) {
                state.settings.flowers = state.settings.flowers || seat.hasBonus;
            }
            state.seatWinds = MakeSeatWinds(east);
            state.roundWind = draft.round.value_or(Wind::East);
            state.east = east;
            state.phase = phase;
            state.current = claim ? draft.pendingFrom : draft.seat;

            // The pile: seat by seat in turn from East, as the table is played; the pending discard is the last.
            eastl::array<size_t, kSeatCount> next{};
            for (bool placed = true; placed;) {
                placed = false;
                for (int step = 0; step < kSeatCount; ++step) {
                    const Seat seat = (east + step) % kSeatCount;
                    if (next[seat] < draft.seats[seat].discards.size()) {
                        state.discards.push_back({draft.seats[seat].discards[next[seat]++], seat});
                        placed = true;
                    }
                }
            }
            if (claim) {
                state.discards.push_back({draft.pending, draft.pendingFrom});
                state.pendingTile = draft.pending;
            }

            // The discards a seat has made: its pile tiles, the pending one, and the ones other seats' melds took from it.
            eastl::array<int, kSeatCount> discardCount{};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                discardCount[seat] += static_cast<int>(draft.seats[seat].discards.size());
                for (const Meld& meld : draft.seats[seat].melds) {
                    if (meld.fromDiscard) {
                        ++discardCount[meld.from];
                    }
                }
            }
            if (claim) {
                ++discardCount[draft.pendingFrom];
            }

            // The unseen tiles in the order of the set; the other hands take their share first, the wall the rest.
            eastl::vector<Tile> pool;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const Tile tile{id};
                if (tile.IsBonus() && !state.settings.flowers) {
                    continue;
                }
                const int limit = tile.IsBonus() ? kBonusCopies : kCopiesPerTile;
                pool.insert(pool.end(), static_cast<size_t>(limit - draft.used[id]), tile);
            }
            size_t hiddenTotal = 0;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != draft.seat) {
                    hiddenTotal += static_cast<size_t>(kDealtTiles - kTilesPerSet * static_cast<int>(draft.seats[seat].melds.size()));
                }
            }
            if (pool.size() < hiddenTotal + kDeadWallTiles) {
                return Failure(Format("the tiles named leave {} unseen: the other hands need {} and the dead wall {}", pool.size(), hiddenTotal, kDeadWallTiles));
            }
            const size_t wallTotal = pool.size() - hiddenTotal;
            const size_t liveAndLoose = wallTotal - kDeadWallTiles;
            size_t looseCount = 0;
            size_t liveCount = 0;
            if (draft.wall.has_value()) {
                liveCount = static_cast<size_t>(*draft.wall);
                if (liveCount > liveAndLoose || liveAndLoose - liveCount > kLooseTiles) {
                    const size_t lowest = liveAndLoose > kLooseTiles ? liveAndLoose - kLooseTiles : 0;
                    return Failure(LineError(
                        draft.wallLine, Format("the tiles named leave {} live tiles with a loose reserve of at most {}: the live wall holds {}..{}", liveAndLoose, kLooseTiles, lowest, liveAndLoose)));
                }
                looseCount = liveAndLoose - liveCount;
            } else {
                looseCount = eastl::min(kLooseTiles, liveAndLoose);
                liveCount = liveAndLoose - looseCount;
            }

            size_t cursor = 0;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                PlayerHand& hand = state.hands[seat];
                const SeatDraft& draftSeat = draft.seats[seat];
                if (seat == draft.seat) {
                    hand.concealed = draft.hand;
                } else {
                    const size_t count = static_cast<size_t>(kDealtTiles - kTilesPerSet * static_cast<int>(draftSeat.melds.size()));
                    hand.concealed.assign(pool.begin() + static_cast<ptrdiff_t>(cursor), pool.begin() + static_cast<ptrdiff_t>(cursor + count));
                    cursor += count;
                }
                hand.melds = draftSeat.melds;
                hand.bonus = draftSeat.bonus;
                hand.discardCount = discardCount[seat];
                if (draftSeat.calling) {
                    if (draftSeat.callingFirst && hand.discardCount == 0) {
                        return Failure(LineError(draftSeat.callingLine, Format("seat {} declared with its first discard but has no discard", seat)));
                    }
                    hand.callingDeclared = true;
                    hand.callingAtDiscardCount = draftSeat.callingFirst ? 0 : kWorldLateDeclarationDiscards;
                }
            }
            state.loose.assign(pool.begin() + static_cast<ptrdiff_t>(cursor), pool.begin() + static_cast<ptrdiff_t>(cursor + looseCount));
            state.live.assign(pool.begin() + static_cast<ptrdiff_t>(cursor + looseCount), pool.begin() + static_cast<ptrdiff_t>(cursor + looseCount + liveCount));
            state.dead.assign(pool.begin() + static_cast<ptrdiff_t>(cursor + looseCount + liveCount), pool.end());

            // The tile just drawn: by default the last one written.
            if (!claim && !draft.drewNone) {
                const Tile drawn = draft.hasDrew ? draft.drew : draft.hand.back();
                if (eastl::find(draft.hand.begin(), draft.hand.end(), drawn) == draft.hand.end()) {
                    return Failure(LineError(draft.drewLine, Format("the drawn tile {} is not in the hand", ToString(drawn))));
                }
                state.drawnTile = drawn;
            }
            return state;
        }

        /// The model of `state`; a claim window needs to know which of the other seats could claim, which the hands decide and `Restore`
        /// checks, so the guesses are tried from "nobody else" up.
        std::expected<HandModel, eastl::string> BuildModel(HandState state, Seat viewer) {
            if (state.phase != Phase::Claim) {
                std::expected<HandModel, eastl::string> model = HandModel::Restore(eastl::move(state));
                if (!model.has_value()) {
                    return Failure(Format("the position cannot exist: {}", model.error().c_str()));
                }
                return model;
            }
            eastl::vector<Seat> others;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != viewer && seat != state.current) {
                    others.push_back(seat);
                }
            }
            eastl::string firstProblem;
            for (unsigned mask = 0; mask < (1U << others.size()); ++mask) {
                HandState trial = state;
                trial.claimStatus[viewer] = ClaimStatus::Waiting;
                for (size_t i = 0; i < others.size(); ++i) {
                    trial.claimStatus[others[i]] = ((mask >> i) & 1U) != 0 ? ClaimStatus::Waiting : ClaimStatus::NotInWindow;
                }
                std::expected<HandModel, eastl::string> model = HandModel::Restore(eastl::move(trial));
                if (model.has_value()) {
                    return model;
                }
                if (mask == 0) {
                    firstProblem = model.error();
                }
            }
            return Failure(Format("the position cannot exist (seat {} must be able to claim the pending tile): {}", viewer, firstProblem.c_str()));
        }

    }  // namespace

    std::expected<LogPosition, eastl::string> PositionFromLog(const eastl::vector<MatchLogEvent>& events, int hand, int actionIndex) {
        const std::expected<eastl::vector<size_t>, eastl::string> actions = ActionEventsOfHand(events, hand);
        if (!actions.has_value()) {
            return Failure(actions.error());
        }
        if (actionIndex < 0 || static_cast<size_t>(actionIndex) >= actions->size()) {
            return Failure(Format("action {} is out of range: hand {} has {} actions (numbered from 0)", actionIndex, hand, actions->size()));
        }
        const size_t target = (*actions)[static_cast<size_t>(actionIndex)];
        const std::expected<Match, eastl::string> match = ReplayedBefore(events, target);
        if (!match.has_value()) {
            return Failure(match.error());
        }
        return MakeLogPosition(match->Hand(), events[target], target, hand, actionIndex);
    }

    std::expected<eastl::vector<LogPosition>, eastl::string> PositionsOfSeat(const eastl::vector<MatchLogEvent>& events, int hand, Seat seat) {
        if (!IsSeat(seat)) {
            return Failure(Format("{} is no seat (0..3)", seat));
        }
        const std::expected<eastl::vector<size_t>, eastl::string> actions = ActionEventsOfHand(events, hand);
        if (!actions.has_value()) {
            return Failure(actions.error());
        }
        // The actions of one hand follow each other in the log, so one replay up to the first reaches every decision.
        for (size_t i = 0; i < actions->size(); ++i) {
            if ((*actions)[i] != actions->front() + i) {
                return Failure(Format("the actions of hand {} are not contiguous in the log (event {})", hand, (*actions)[i]));
            }
        }
        std::expected<Match, eastl::string> match = ReplayedBefore(events, actions->front());
        if (!match.has_value()) {
            return Failure(match.error());
        }
        eastl::vector<LogPosition> positions;
        for (size_t i = 0; i < actions->size(); ++i) {
            const size_t eventIndex = (*actions)[i];
            const MatchLogEvent& event = events[eventIndex];
            if (event.seat == seat) {
                std::expected<LogPosition, eastl::string> position = MakeLogPosition(match->Hand(), event, eventIndex, hand, static_cast<int>(i));
                if (!position.has_value()) {
                    return Failure(position.error());
                }
                positions.push_back(eastl::move(*position));
            }
            if (!match->Hand().Apply(event.seat, event.action)) {
                return Failure(ReplayText(ReplayError{eventIndex, ReplayProblem::ActionRefused}));
            }
        }
        if (positions.empty()) {
            return Failure(Format("seat {} has no decision in hand {}", seat, hand));
        }
        return positions;
    }

    std::expected<TableView, eastl::string> ParsePosition(std::string_view text) {
        eastl::vector<Words> lines;
        eastl::vector<int> numbers;
        SplitLines(text, lines, numbers);
        Draft draft;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (const Problem problem = ApplyLine(draft, numbers[i], lines[i])) {
                return Failure(*problem);
            }
        }
        std::expected<HandState, eastl::string> state = BuildState(draft);
        if (!state.has_value()) {
            return Failure(state.error());
        }
        std::expected<HandModel, eastl::string> model = BuildModel(eastl::move(*state), draft.seat);
        if (!model.has_value()) {
            return Failure(model.error());
        }
        return MakeTableView(*model, draft.seat);
    }

}  // namespace BECore::Mahjong::Analyzer
