#include "OwnDraws.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong {

    namespace {

        /// The memo key: the concealed counts, the copies drawn on the line per kind, the draws used.
        using StateKey = Array<uint8_t, 2 * kKindCount + 1>;

        /// FNV-1a over the bytes of a key. The tables below are only looked up, never walked, so no answer depends on the hash.
        struct BytesHash {
            template <size_t N>
            size_t operator()(const Array<uint8_t, N>& key) const noexcept {
                constexpr size_t kOffsetBasis = 14695981039346656037ull;
                constexpr size_t kPrime = 1099511628211ull;
                size_t hash = kOffsetBasis;
                for (const uint8_t byte : key) {
                    hash = (hash ^ byte) * kPrime;
                }
                return hash;
            }
        };

        /// What the search asks about one concealed hand again and again, made on first use. A hand meets itself on lines that differ
        /// only in what was drawn, so each part is a pure function of the counts and the evaluator's melds.
        ///
        /// Worth it (depth 2, seeds 4000..4003, sampled in Debug and in Release): `Shanten` was two thirds of a decision, from the
        /// probes of `ImprovingTiles` and the discards of the draws, and the same hands come back; `WinningTiles` and the standing score
        /// of a hand likewise. Not worth it: the score of a win (`WinScore`, 3 % of a decision) depends on the line too. Not done: skipping
        /// `WinningTiles` for a hand whose shanten is above 0, because seven pairs and thirteen orphans wait with a high shanten.
        struct HandFacts {
            /// `Shanten` of the hand.
            Optional<int> shanten;
            /// `NotWinningValue` of the hand as it stands.
            Optional<int64_t> notWinning;
            /// `WinningTiles` of the hand (3n + 1 tiles).
            Optional<Vector<Tile>> winning;
            /// `ImprovingTiles` of the hand (3n + 1 tiles).
            Optional<Vector<Tile>> improving;
            /// For a hand of 3n + 2 tiles: the kinds whose discard leaves the lowest shanten, in the order of the ids.
            Optional<Vector<uint8_t>> lowestDiscards;
        };

        /// The value of a state per unit of the probability of reaching it. All sums, so two values add and a value scales by a chance.
        struct Value {
            /// The chance (over `kOwnDrawsProbBase`) that the seat wins, and the same weighted by the score of the win.
            int64_t winMass = 0;
            int64_t winScoreMass = 0;
            /// Milli-points from the wins, and from every other outcome (another seat wins, the hand is drawn).
            int64_t winPay = 0;
            int64_t rest = 0;

            [[nodiscard]] int64_t Expected() const noexcept {
                return winPay + rest;
            }
        };

        /// `prob` (over `kOwnDrawsProbBase`) of `value`.
        int64_t ProbTimes(int64_t prob, int64_t value) noexcept {
            return RoundedDivSigned(prob * value, kOwnDrawsProbBase);
        }

        Value Scaled(const Value& value, int64_t prob) noexcept {
            return {ProbTimes(prob, value.winMass), ProbTimes(prob, value.winScoreMass), ProbTimes(prob, value.winPay), ProbTimes(prob, value.rest)};
        }

        void Add(Value& total, const Value& part) noexcept {
            total.winMass += part.winMass;
            total.winScoreMass += part.winScoreMass;
            total.winPay += part.winPay;
            total.rest += part.rest;
        }

        /// The copies drawn on the line so far, per kind and together (kept, so no node sums 34 kinds).
        struct Line {
            TileCounts drawn{};
            int drawnTotal = 0;
        };

        /// The search of one call. Holds the view and the memo; nothing outlives the call.
        class Evaluator {
        public:
            Evaluator(const TableView& view, int depth, bool discardWins, int extraDraws = 0)
                : _view(view), _depth(depth), _discardWins(discardWins), _meldCount(MeldCountOf(view)), _meldTiles(CountMeldTiles(view.own.melds)), _root(CountTiles(view.own.concealed)),
                  _east(view.seatWind == Wind::East), _draws(view.liveCount / kSeatCount + extraDraws) {
                int declared = 0;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    declared += (seat != view.seat && view.seats[seat].callingDeclared) ? 1 : 0;
                }
                // `_alive[i]` for 0..draws + 1, so a closed form never calls `Alive` (which walks i steps).
                for (int i = 0; i <= _draws + 1; ++i) {
                    _alive.push_back(Alive(i, declared));
                }
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    _unseenTotal += UnseenCopies(view, Tile{id});
                    _openCopies[id] = view.visible[id] - _root[id];
                }
                for (const Meld& meld : view.own.melds) {
                    if (meld.concealed) {
                        _openCopies[meld.first.Id()] -= kCopiesPerTile;
                    }
                }
            }

            /// The value of the hand `counts` (3n + 1 tiles, after a discard) with nothing drawn yet, as declared when it waits.
            Value Start(const TileCounts& counts) {
                return Evaluate(counts, Line{}, 0);
            }

        private:
            /// The copies of kind `id` still unseen once the draws of the line are taken out.
            [[nodiscard]] int Unseen(const Line& line, uint8_t id) const {
                const int unseen = UnseenCopies(_view, Tile{id}) - line.drawn[id];
                return unseen > 0 ? unseen : 0;
            }

            [[nodiscard]] int UnseenTotal(const Line& line) const noexcept {
                return _unseenTotal - line.drawnTotal;
            }

            [[nodiscard]] int ShantenOf(const TileCounts& counts) {
                Optional<int>& slot = _facts[counts].shanten;
                if (!slot.has_value()) {
                    slot = Shanten(counts, _meldCount);
                }
                return *slot;
            }

            [[nodiscard]] int64_t NotWinningOf(const TileCounts& counts) {
                Optional<int64_t>& slot = _facts[counts].notWinning;
                if (!slot.has_value()) {
                    slot = NotWinningValue(StandingScore(counts), _east);
                }
                return *slot;
            }

            [[nodiscard]] const Vector<Tile>& WinningOf(const TileCounts& counts) {
                Optional<Vector<Tile>>& slot = _facts[counts].winning;
                if (!slot.has_value()) {
                    slot = WinningTiles(counts, _meldCount, _meldTiles);
                }
                return *slot;
            }

            /// The answer of `ImprovingTiles` (its rule, written out), with the shanten of each probe taken from the same table as the
            /// discards' (a probe is often the hand a line reaches by a draw); a change of that rule is a change here too.
            [[nodiscard]] const Vector<Tile>& ImprovingOf(const TileCounts& counts) {
                Optional<Vector<Tile>>& slot = _facts[counts].improving;
                if (slot.has_value()) {
                    return *slot;
                }
                Vector<Tile> improving;
                const int current = ShantenOf(counts);
                if (current > kShantenComplete) {
                    TileCounts probe = counts;
                    for (uint8_t id = 0; id < kKindCount; ++id) {
                        if (!HasTileNearby(counts, id)) {
                            continue;
                        }
                        ++probe[id];
                        if (ShantenOf(probe) < current) {
                            improving.push_back(Tile{id});
                        }
                        --probe[id];
                    }
                }
                slot = eastl::move(improving);
                return *slot;
            }

            /// The kinds whose discard from `hand` (3n + 2 tiles) leaves the lowest shanten.
            [[nodiscard]] const Vector<uint8_t>& LowestDiscardsOf(TileCounts& hand) {
                Optional<Vector<uint8_t>>& slot = _facts[hand].lowestDiscards;
                if (slot.has_value()) {
                    return *slot;
                }
                // A node table: the slot stays where it is while `ShantenOf` adds hands below.
                Vector<uint8_t> lowest;
                int lowestShanten = 0;
                for (uint8_t discard = 0; discard < kKindCount; ++discard) {
                    if (hand[discard] == 0) {
                        continue;
                    }
                    --hand[discard];
                    const int shanten = ShantenOf(hand);
                    ++hand[discard];
                    if (lowest.empty() || shanten < lowestShanten) {
                        lowestShanten = shanten;
                        lowest.clear();
                    }
                    if (shanten == lowestShanten) {
                        lowest.push_back(discard);
                    }
                }
                slot = eastl::move(lowest);
                return *slot;
            }

            [[nodiscard]] PlayerHand HandOf(const TileCounts& counts) const {
                PlayerHand hand;
                hand.melds = _view.own.melds;
                hand.bonus = _view.own.bonus;
                hand.callingDeclared = _view.own.callingDeclared;
                hand.callingAtDiscardCount = _view.own.callingAtDiscardCount;
                hand.discardCount = _view.own.discardCount;
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    for (int copy = 0; copy < counts[id]; ++copy) {
                        hand.concealed.push_back(Tile{id});
                    }
                }
                return hand;
            }

            [[nodiscard]] ScoreInput InputOf(PlayerHand hand) const {
                ScoreInput input;
                input.hand = eastl::move(hand);
                input.seatWind = _view.seatWind;
                input.roundWind = _view.roundWind;
                return input;
            }

            /// The score of the hand `counts` as it stands (it does not win).
            [[nodiscard]] int StandingScore(const TileCounts& counts) const {
                return ScoreSeat(InputOf(HandOf(counts))).score;
            }

            /// The score of the hand `counts` that wins with `tile` (a self-draw, or the discard of another seat), declared with the
            /// discard number `used` of the line.
            [[nodiscard]] int WinScore(const TileCounts& counts, const TileCounts& drawn, int used, Tile tile, WinSource source) const {
                const uint8_t id = tile.Id();
                PlayerHand hand = HandOf(counts);
                hand.AddConcealed(tile);
                hand.callingDeclared = true;
                if (!_view.own.callingDeclared) {
                    hand.callingAtDiscardCount = _view.own.discardCount + used;
                }
                ScoreInput input = InputOf(eastl::move(hand));
                input.won = true;
                input.result.kind = ResultKind::Win;
                input.result.winner = _view.seat;
                input.result.winningTile = tile;
                input.result.source = source;
                // The score does not depend on whose discard it is; any other seat will do.
                input.result.from = source == WinSource::SelfDraw ? _view.seat : NextSeat(_view.seat);
                // The copies lying open: the pile and the open melds as the view shows them, and what the line has discarded (the root
                // hand and the draws of the line, less the hand now).
                input.visibleWinningTileCopies = _openCopies[id] + _root[id] + drawn[id] - counts[id];
                return ScoreSeat(input).score;
            }

            /// The unseen winning copies of a hand that waits and the mean score and payoff of a win with one of them, by what the win is
            /// (a self-draw or a discard of another seat), weighted by the copies.
            struct WinKinds {
                int64_t copies = 0;
                int64_t meanScore = 0;
                int64_t meanPay = 0;
            };

            [[nodiscard]] WinKinds MeanWin(const TileCounts& counts, const Line& line, int used, const Vector<Tile>& winning, WinSource source) const {
                int64_t scoreSum = 0;
                int64_t paySum = 0;
                WinKinds kinds;
                for (const Tile tile : winning) {
                    const int unseen = Unseen(line, tile.Id());
                    if (unseen > 0) {
                        const int score = WinScore(counts, line.drawn, used, tile, source);
                        kinds.copies += unseen;
                        scoreSum += unseen * static_cast<int64_t>(score);
                        paySum += unseen * WinPayoff(score, _east);
                    }
                }
                kinds.meanScore = kinds.copies > 0 ? RoundedDiv(scoreSum, kinds.copies) : 0;
                kinds.meanPay = kinds.copies > 0 ? RoundedDiv(paySum, kinds.copies) : 0;
                return kinds;
            }

            /// The closed form of a hand that waits, after `used` draws, over the draws the wall leaves: a draw wins with the chance `p`
            /// of the unseen winning copies, the hand does not change, and what is left of the chance is another seat's win (the standing
            /// hand, worth `notWinning`) or a drawn hand. With `discardWins` each round also starts with the three discards of the
            /// others, which win with `DiscardRoundChance(p)` (scored as a win on a discard) before the seat draws.
            [[nodiscard]] Value Waiting(const TileCounts& counts, const Line& line, int used, const Vector<Tile>& winning, int64_t notWinning) const {
                const WinKinds drawKinds = MeanWin(counts, line, used, winning, WinSource::SelfDraw);
                const int64_t total = UnseenTotal(line);
                const int64_t chance = (drawKinds.copies > 0 && total > 0) ? RoundedDiv(drawKinds.copies * kOwnDrawsProbBase, total) : 0;
                WinKinds discardKinds;
                int64_t discardChance = 0;
                if (_discardWins) {
                    discardKinds = MeanWin(counts, line, used, winning, WinSource::Discard);
                    discardChance = DiscardRoundChance(chance);
                }

                int64_t drawMass = 0;
                int64_t discardMass = 0;
                int64_t stay = kOwnDrawsProbBase;  // the chance that the hand has not won after the rounds so far
                for (int draw = used + 1; draw <= _draws; ++draw) {
                    if (_discardWins) {
                        discardMass += ProbTimes(ProbTimes(_alive[draw - 1], stay), discardChance);
                        stay = ProbTimes(stay, kOwnDrawsProbBase - discardChance);
                    }
                    drawMass += ProbTimes(ProbTimes(_alive[draw - 1], stay), chance);
                    stay = ProbTimes(stay, kOwnDrawsProbBase - chance);
                }
                const int64_t winMass = drawMass + discardMass;
                const int64_t reachedEnd = ProbTimes(_alive[_draws], stay);
                const int64_t others = eastl::max<int64_t>(0, _alive[used] - winMass - reachedEnd);

                Value value;
                value.winMass = winMass;
                value.winScoreMass = drawMass * drawKinds.meanScore + discardMass * discardKinds.meanScore;
                value.winPay = ProbTimes(drawMass, drawKinds.meanPay) + ProbTimes(discardMass, discardKinds.meanPay);
                value.rest = ProbTimes(others, notWinning);
                return value;
            }

            /// The value after the draw of `id`: the best discard among those that leave the lowest shanten.
            [[nodiscard]] Value AfterDraw(const TileCounts& counts, const Line& line, int used, uint8_t id) {
                TileCounts hand = counts;
                ++hand[id];
                Line nowDrawn = line;
                ++nowDrawn.drawn[id];
                ++nowDrawn.drawnTotal;
                Value best;
                bool any = false;
                for (const uint8_t discard : LowestDiscardsOf(hand)) {
                    --hand[discard];
                    const Value value = Evaluate(hand, nowDrawn, used + 1);
                    ++hand[discard];
                    if (!any || value.Expected() > best.Expected()) {
                        best = value;
                        any = true;
                    }
                }
                return best;
            }

            /// The value of the hand `counts` (after a discard) once `used` draws have been made, per unit of the chance of getting here.
            Value Evaluate(const TileCounts& counts, const Line& line, int used) {
                StateKey key{};
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    key[id] = counts[id];
                    key[kKindCount + id] = line.drawn[id];
                }
                key[2 * kKindCount] = static_cast<uint8_t>(used);
                const auto found = _memo.find(key);
                if (found != _memo.end()) {
                    return found->second;
                }

                const int64_t notWinning = NotWinningOf(counts);
                Value value;
                // Another seat won between the last draw and this one: the standing hand pays.
                value.rest = used > 0 ? ProbTimes(_alive[used - 1] - _alive[used], notWinning) : 0;
                if (used < _draws) {
                    const Vector<Tile>& winning = WinningOf(counts);
                    if (!winning.empty()) {
                        Add(value, Waiting(counts, line, used, winning, notWinning));
                    } else if (used < _depth && UnseenTotal(line) > 0) {
                        Add(value, Expand(counts, line, used));
                    } else {
                        // The search stops here: no win from now on, another seat wins or the hand is drawn.
                        value.rest += ProbTimes(_alive[used] - _alive[_draws], notWinning);
                    }
                }
                _memo[key] = value;
                return value;
            }

            /// The draw after `used` draws of a hand that does not wait: the improving kinds branch, the rest leaves the hand as it is.
            [[nodiscard]] Value Expand(const TileCounts& counts, const Line& line, int used) {
                const int64_t total = UnseenTotal(line);
                Value value;
                int64_t branched = 0;
                for (const Tile tile : ImprovingOf(counts)) {
                    const int unseen = Unseen(line, tile.Id());
                    if (unseen > 0) {
                        const int64_t chance = RoundedDiv(unseen * kOwnDrawsProbBase, total);
                        Add(value, Scaled(AfterDraw(counts, line, used, tile.Id()), chance));
                        branched += chance;
                    }
                }
                const int64_t unchanged = eastl::max<int64_t>(0, kOwnDrawsProbBase - branched);
                Add(value, Scaled(Evaluate(counts, line, used + 1), unchanged));
                return value;
            }

            const TableView& _view;
            int _depth = 0;
            bool _discardWins = false;
            int _meldCount = 0;
            TileCounts _meldTiles{};
            TileCounts _root{};
            bool _east = false;
            /// The draws the seat has left at the start: the wall's hard limit.
            int _draws = 0;
            Vector<int64_t> _alive;
            int _unseenTotal = 0;
            Array<int, kKindCount> _openCopies{};
            HashMap<StateKey, Value, BytesHash> _memo;
            HashMap<TileCounts, HandFacts, BytesHash> _facts;
        };

        DiscardValue MakeDiscardValue(Tile tile, const Value& value) {
            DiscardValue result;
            result.tile = tile;
            result.winChance = static_cast<int>(eastl::clamp<int64_t>(value.winMass, 0, kOwnDrawsProbBase));
            result.meanWinScore = value.winMass > 0 ? static_cast<int>(RoundedDiv(value.winScoreMass, value.winMass)) : 0;
            const int64_t notWinning = kOwnDrawsProbBase - result.winChance;
            result.valueWhenNotWinning = notWinning > 0 ? RoundedDivSigned(value.rest * kOwnDrawsProbBase, notWinning) : 0;
            result.expected = value.Expected();
            return result;
        }

        bool IsOwnDiscardTurn(const TableView& view) noexcept {
            return view.phase == Phase::Discard && IsSeat(view.seat) && view.currentSeat == view.seat;
        }

        /// A rated discard with what breaks the ties of its expected value.
        struct Ranked {
            DiscardValue value;
            int order = 0;  // the position in the order the ties are kept in: lower first
        };

        Vector<DiscardValue> BestFirst(Vector<Ranked> ranked) {
            eastl::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.value.expected != b.value.expected ? a.value.expected > b.value.expected : a.order < b.order; });
            Vector<DiscardValue> values;
            for (const Ranked& entry : ranked) {
                values.push_back(entry.value);
            }
            return values;
        }

    }  // namespace

    Vector<DiscardValue> ValueDiscards(const TableView& view, int depth) {
        return ValueDiscards(view, OwnDrawsModel{.depth = depth});
    }

    Vector<DiscardValue> ValueDiscards(const TableView& view, const OwnDrawsModel& model) {
        if (model.depth < 1 || !IsOwnDiscardTurn(view)) {
            return {};
        }
        Array<bool, kKindCount> listed{};
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Discard && !action.declareCalling && action.tile.IsPlayable()) {
                listed[action.tile.Id()] = true;
            }
        }
        TileCounts counts = CountTiles(view.own.concealed);
        // The chooser's order; the best shanten and the slack are those of the listed discards.
        Vector<DiscardOption> options;
        for (const DiscardOption& option : RateDiscards(counts, MeldCountOf(view), view)) {
            if (listed[option.tile.Id()]) {
                options.push_back(option);
            }
        }
        if (options.empty()) {
            return {};
        }
        const int bestShanten = options.front().shanten;
        Evaluator evaluator(view, eastl::min(model.depth, kOwnDrawsMaxDepth), model.discardWins);
        Vector<Ranked> ranked;
        for (const DiscardOption& option : options) {
            if (option.shanten > bestShanten + kSearchShantenSlack || static_cast<int>(ranked.size()) >= kOwnDrawsMaxCandidates) {
                continue;
            }
            --counts[option.tile.Id()];
            ranked.push_back({MakeDiscardValue(option.tile, evaluator.Start(counts)), static_cast<int>(ranked.size())});
            ++counts[option.tile.Id()];
        }
        return BestFirst(eastl::move(ranked));
    }

    Vector<DiscardValue> ValueWaits(const TableView& view, const OwnDrawsModel& model) {
        if (!IsOwnDiscardTurn(view)) {
            return {};
        }
        // The order of the ties: the most unseen winning copies, the lowest `KeepValue`, the lowest tile id.
        Vector<WaitingDiscard> waiting = WaitingDiscards(view, true);
        eastl::sort(waiting.begin(), waiting.end(), [](const WaitingDiscard& a, const WaitingDiscard& b) {
            if (a.copies != b.copies) {
                return a.copies > b.copies;
            }
            return a.keep != b.keep ? a.keep < b.keep : a.tile.Id() < b.tile.Id();
        });
        TileCounts counts = CountTiles(view.own.concealed);
        Evaluator evaluator(view, 0, model.discardWins);
        Vector<Ranked> ranked;
        for (const WaitingDiscard& discard : waiting) {
            --counts[discard.tile.Id()];
            ranked.push_back({MakeDiscardValue(discard.tile, evaluator.Start(counts)), static_cast<int>(ranked.size())});
            ++counts[discard.tile.Id()];
        }
        return BestFirst(eastl::move(ranked));
    }

    int64_t ValueOfHeldHand(const TableView& view, int extraDraws, const OwnDrawsModel& model) {
        if (!IsSeat(view.seat)) {
            return 0;
        }
        Evaluator evaluator(view, 0, model.discardWins, eastl::max(0, extraDraws));
        return evaluator.Start(CountTiles(view.own.concealed)).Expected();
    }

}  // namespace BECore::Mahjong
