#pragma once

#include <random>

namespace BECore::Mahjong {

    inline constexpr int kDieFaces = 6;

    /**
     * @brief The random source of a hand: a seeded `std::mt19937` and the few things mahjong asks of it. A seed gives the same game on
     *        every platform.
     *
     * Only the raw `mt19937` stream is used (its output is fixed by the standard). Never `std::shuffle` or `std::*_distribution`: their
     * output differs between standard libraries. The modulo bias of `Below` (n does not divide 2^32) is below 1e-8 for the n used here.
     */
    class MahjongRng {
    public:
        explicit MahjongRng(uint32_t seed = 0) {
            Seed(seed);
        }

        void Seed(uint32_t seed) {
            _seed = seed;
            _rng.seed(seed);
        }

        [[nodiscard]] uint32_t GetSeed() const noexcept {
            return _seed;
        }

        /// A number in [0, n); 0 when n is 0.
        [[nodiscard]] uint32_t Below(uint32_t n) {
            return n == 0 ? 0 : _rng() % n;
        }

        /// One die, 1..6.
        [[nodiscard]] int RollDie() {
            return 1 + static_cast<int>(Below(kDieFaces));
        }

        /// Two dice added, 2..12; the first die is rolled first.
        [[nodiscard]] int Roll2d6() {
            const int first = RollDie();
            return first + RollDie();
        }

        /// Fisher-Yates, written by hand: i from the last index down to 1 swaps with `Below(i + 1)`.
        template <typename T>
        void Shuffle(eastl::vector<T>& items) {
            for (size_t i = items.size(); i > 1; --i) {
                const size_t j = Below(static_cast<uint32_t>(i));
                eastl::swap(items[i - 1], items[j]);
            }
        }

    private:
        uint32_t _seed = 0;
        std::mt19937 _rng;
    };

}  // namespace BECore::Mahjong
