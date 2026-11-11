#include "HandTestSupport.h"

#include <Mahjong/Table/WindMarks.h>
#include <gtest/gtest.h>

/**
 * Which tiles the winds of a seat mark (`Table/WindMarks.h`, pure logic): the wind tile of the own seat wind, that of the round wind, both
 * when they are the same wind, and no other tile.
 */
namespace BECore::Mahjong::Tests {

    TEST(WindMarksTest, TheTilesOfTheOwnWindAndOfTheRoundWindAreMarkedForEveryPairOfWinds) {
        for (int seat = 0; seat < kSeatCount; ++seat) {
            for (int round = 0; round < kSeatCount; ++round) {
                for (int wind = 0; wind < kSeatCount; ++wind) {
                    const WindMark want = wind == seat && wind == round ? WindMark::Both : wind == seat ? WindMark::Seat : wind == round ? WindMark::Round : WindMark::None;
                    EXPECT_EQ(WindMarkOf(Tile::FromWind(wind), static_cast<Wind>(seat), static_cast<Wind>(round)), want) << "wind " << wind << ", seat " << seat << ", round " << round;
                }
            }
        }
    }

    TEST(WindMarksTest, NoOtherTileIsMarked) {
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile(id);
            if (!tile.IsWind()) {
                EXPECT_EQ(WindMarkOf(tile, Wind::East, Wind::East), WindMark::None) << "tile id " << static_cast<int>(id);
            }
        }
        EXPECT_EQ(WindMarkOf(Tile{}, Wind::East, Wind::East), WindMark::None);
    }

}  // namespace BECore::Mahjong::Tests
