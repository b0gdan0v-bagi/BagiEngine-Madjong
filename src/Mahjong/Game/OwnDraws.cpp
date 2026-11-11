#include "OwnDraws.h"

#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong {

    namespace {

        /// The memo key: the concealed counts, the copies drawn on the line per kind, the draws used.
        using StateKey = eastl::array<uint8_t, 2 * kKindCount + 1>;

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

        /// The search of one call. Holds the view and the memo; nothing outlives the call.
        class Evaluator {
        public:
            Evaluator(const TableView& view, int depth)
                : _view(view), _depth(depth), _meldCount(MeldCountOf(view)), _meldTiles(CountMeldTiles(view.own.melds)), _root(CountTiles(view.own.concealed)), _east(view.seatWind == Wind::East),
                  _draws(view.liveCount / kSeatCount) {
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
                return Evaluate(counts, TileCounts{}, 0);
            }

        private:
            /// The copies of kind `id` still unseen once the draws of the line (`drawn`) are taken out.
            [[nodiscard]] int Unseen(const TileCounts& drawn, uint8_t id) const {
                const int unseen = UnseenCopies(_view, Tile{id}) - drawn[id];
                return unseen > 0 ? unseen : 0;
            }

            [[nodiscard]] int UnseenTotal(const TileCounts& drawn) const {
                int total = _unseenTotal;
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    total -= drawn[id];
                }
                return total;
            }

            /// The seat's hand with the concealed tiles `counts`.
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

            /// The score of the hand `counts` that wins with a self-draw of `tile`, declared with the discard number `used` of the line.
            [[nodiscard]] int WinScore(const TileCounts& counts, const TileCounts& drawn, int used, Tile tile) const {
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
                input.result.source = WinSource::SelfDraw;
                // The copies lying open: the pile and the open melds as the view shows them, and what the line has discarded (the root
                // hand and the draws of the line, less the hand now).
                input.visibleWinningTileCopies = _openCopies[id] + _root[id] + drawn[id] - counts[id];
                return ScoreSeat(input).score;
            }

            /// The closed form of a hand that waits, after `used` draws, over the draws the wall leaves: a draw wins with the chance `p`
            /// of the unseen winning copies, the hand does not change, and what is left of the chance is another seat's win (the standing
            /// hand, worth `notWinning`) or a drawn hand.
            [[nodiscard]] Value Waiting(const TileCounts& counts, const TileCounts& drawn, int used, const eastl::vector<Tile>& winning, int64_t notWinning) const {
                int64_t copies = 0;
                int64_t scoreSum = 0;
                int64_t paySum = 0;
                for (const Tile tile : winning) {
                    const int unseen = Unseen(drawn, tile.Id());
                    if (unseen > 0) {
                        const int score = WinScore(counts, drawn, used, tile);
                        copies += unseen;
                        scoreSum += unseen * static_cast<int64_t>(score);
                        paySum += unseen * WinPayoff(score, _east);
                    }
                }
                const int64_t total = UnseenTotal(drawn);
                const int64_t chance = (copies > 0 && total > 0) ? RoundedDiv(copies * kOwnDrawsProbBase, total) : 0;
                const int64_t meanScore = copies > 0 ? RoundedDiv(scoreSum, copies) : 0;
                const int64_t meanPay = copies > 0 ? RoundedDiv(paySum, copies) : 0;

                int64_t winMass = 0;
                int64_t stay = kOwnDrawsProbBase;  // the chance that the hand has not won after the draws so far
                for (int draw = used + 1; draw <= _draws; ++draw) {
                    winMass += ProbTimes(ProbTimes(_alive[draw - 1], stay), chance);
                    stay = ProbTimes(stay, kOwnDrawsProbBase - chance);
                }
                const int64_t reachedEnd = ProbTimes(_alive[_draws], stay);
                const int64_t others = eastl::max<int64_t>(0, _alive[used] - winMass - reachedEnd);

                Value value;
                value.winMass = winMass;
                value.winScoreMass = winMass * meanScore;
                value.winPay = ProbTimes(winMass, meanPay);
                value.rest = ProbTimes(others, notWinning);
                return value;
            }

            /// The value after the draw of `id`: the best discard among those that leave the lowest shanten.
            [[nodiscard]] Value AfterDraw(const TileCounts& counts, const TileCounts& drawn, int used, uint8_t id) {
                TileCounts hand = counts;
                ++hand[id];
                TileCounts nowDrawn = drawn;
                ++nowDrawn[id];
                eastl::vector<uint8_t> lowest;
                int lowestShanten = 0;
                for (uint8_t discard = 0; discard < kKindCount; ++discard) {
                    if (hand[discard] == 0) {
                        continue;
                    }
                    --hand[discard];
                    const int shanten = Shanten(hand, _meldCount);
                    ++hand[discard];
                    if (lowest.empty() || shanten < lowestShanten) {
                        lowestShanten = shanten;
                        lowest.clear();
                    }
                    if (shanten == lowestShanten) {
                        lowest.push_back(discard);
                    }
                }
                Value best;
                bool any = false;
                for (const uint8_t discard : lowest) {
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
            Value Evaluate(const TileCounts& counts, const TileCounts& drawn, int used) {
                StateKey key{};
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    key[id] = counts[id];
                    key[kKindCount + id] = drawn[id];
                }
                key[2 * kKindCount] = static_cast<uint8_t>(used);
                const auto found = _memo.find(key);
                if (found != _memo.end()) {
                    return found->second;
                }

                const int64_t notWinning = NotWinningValue(StandingScore(counts), _east);
                Value value;
                // Another seat won between the last draw and this one: the standing hand pays.
                value.rest = used > 0 ? ProbTimes(_alive[used - 1] - _alive[used], notWinning) : 0;
                if (used < _draws) {
                    const eastl::vector<Tile> winning = WinningTiles(counts, _meldCount, _meldTiles);
                    if (!winning.empty()) {
                        Add(value, Waiting(counts, drawn, used, winning, notWinning));
                    } else if (used < _depth && UnseenTotal(drawn) > 0) {
                        Add(value, Expand(counts, drawn, used));
                    } else {
                        // The search stops here: no win from now on, another seat wins or the hand is drawn.
                        value.rest += ProbTimes(_alive[used] - _alive[_draws], notWinning);
                    }
                }
                _memo[key] = value;
                return value;
            }

            /// The draw after `used` draws of a hand that does not wait: the improving kinds branch, the rest leaves the hand as it is.
            [[nodiscard]] Value Expand(const TileCounts& counts, const TileCounts& drawn, int used) {
                const int64_t total = UnseenTotal(drawn);
                Value value;
                int64_t branched = 0;
                for (const Tile tile : ImprovingTiles(counts, _meldCount)) {
                    const int unseen = Unseen(drawn, tile.Id());
                    if (unseen > 0) {
                        const int64_t chance = RoundedDiv(unseen * kOwnDrawsProbBase, total);
                        Add(value, Scaled(AfterDraw(counts, drawn, used, tile.Id()), chance));
                        branched += chance;
                    }
                }
                const int64_t unchanged = eastl::max<int64_t>(0, kOwnDrawsProbBase - branched);
                Add(value, Scaled(Evaluate(counts, drawn, used + 1), unchanged));
                return value;
            }

            const TableView& _view;
            int _depth = 0;
            int _meldCount = 0;
            TileCounts _meldTiles{};
            TileCounts _root{};
            bool _east = false;
            /// The draws the seat has left at the start: the wall's hard limit.
            int _draws = 0;
            eastl::vector<int64_t> _alive;
            int _unseenTotal = 0;
            eastl::array<int, kKindCount> _openCopies{};
            eastl::map<StateKey, Value> _memo;
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

        eastl::vector<DiscardValue> BestFirst(eastl::vector<Ranked> ranked) {
            eastl::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.value.expected != b.value.expected ? a.value.expected > b.value.expected : a.order < b.order; });
            eastl::vector<DiscardValue> values;
            for (const Ranked& entry : ranked) {
                values.push_back(entry.value);
            }
            return values;
        }

    }  // namespace

    eastl::vector<DiscardValue> ValueDiscards(const TableView& view, int depth) {
        if (depth < 1 || !IsOwnDiscardTurn(view)) {
            return {};
        }
        eastl::array<bool, kKindCount> listed{};
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Discard && !action.declareCalling && action.tile.IsPlayable()) {
                listed[action.tile.Id()] = true;
            }
        }
        TileCounts counts = CountTiles(view.own.concealed);
        // The chooser's order; the best shanten and the slack are those of the listed discards.
        eastl::vector<DiscardOption> options;
        for (const DiscardOption& option : RateDiscards(counts, MeldCountOf(view), view)) {
            if (listed[option.tile.Id()]) {
                options.push_back(option);
            }
        }
        if (options.empty()) {
            return {};
        }
        const int bestShanten = options.front().shanten;
        Evaluator evaluator(view, eastl::min(depth, kOwnDrawsMaxDepth));
        eastl::vector<Ranked> ranked;
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

    eastl::vector<DiscardValue> ValueWaits(const TableView& view) {
        if (!IsOwnDiscardTurn(view)) {
            return {};
        }
        // The order of the ties: the most unseen winning copies, the lowest `KeepValue`, the lowest tile id.
        eastl::vector<WaitingDiscard> waiting = WaitingDiscards(view, true);
        eastl::sort(waiting.begin(), waiting.end(), [](const WaitingDiscard& a, const WaitingDiscard& b) {
            if (a.copies != b.copies) {
                return a.copies > b.copies;
            }
            return a.keep != b.keep ? a.keep < b.keep : a.tile.Id() < b.tile.Id();
        });
        TileCounts counts = CountTiles(view.own.concealed);
        Evaluator evaluator(view, 0);
        eastl::vector<Ranked> ranked;
        for (const WaitingDiscard& discard : waiting) {
            --counts[discard.tile.Id()];
            ranked.push_back({MakeDiscardValue(discard.tile, evaluator.Start(counts)), static_cast<int>(ranked.size())});
            ++counts[discard.tile.Id()];
        }
        return BestFirst(eastl::move(ranked));
    }

}  // namespace BECore::Mahjong
