#include "inspector/RateMeter.h"

#include <gtest/gtest.h>

using inspector::RateMeter;
using namespace std::chrono_literals;

TEST(RateMeterTest, EmptyMeterReportsZero)
{
    RateMeter meter;
    EXPECT_DOUBLE_EQ(meter.rate(RateMeter::Clock::time_point{}), 0.0);
}

TEST(RateMeterTest, CountsTicksInsideTheWindow)
{
    RateMeter meter(1s);
    const RateMeter::Clock::time_point t0{};
    for (int i = 0; i < 10; ++i) {
        meter.tick(t0 + i * 100ms);  // 10 Hz
    }
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 950ms), 10.0);
}

TEST(RateMeterTest, ForgetsTicksOlderThanTheWindow)
{
    RateMeter meter(1s);
    const RateMeter::Clock::time_point t0{};
    meter.tick(t0);
    meter.tick(t0 + 500ms);

    EXPECT_DOUBLE_EQ(meter.rate(t0 + 1200ms), 1.0);  // yalnızca 500ms'deki kaldı
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 3s), 0.0);
}

TEST(RateMeterTest, ScalesByWindowLength)
{
    RateMeter meter(2s);
    const RateMeter::Clock::time_point t0{};
    for (int i = 0; i < 10; ++i) {
        meter.tick(t0 + i * 100ms);
    }
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 1s), 5.0);  // 10 olay / 2 sn
}
