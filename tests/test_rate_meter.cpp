#include "inspector/RateMeter.h"

#include <gtest/gtest.h>

using inspector::RateMeter;
using namespace std::chrono_literals;

namespace {
const RateMeter::Clock::time_point t0{};
}

TEST(RateMeterTest, NeedsAtLeastTwoTicks)
{
    RateMeter meter;
    EXPECT_DOUBLE_EQ(meter.rate(t0), 0.0);
    meter.tick(t0);
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 10ms), 0.0);
}

TEST(RateMeterTest, MeasuresRateFromTickSpacing)
{
    RateMeter meter(1s);
    for (int i = 0; i < 10; ++i) {
        meter.tick(t0 + i * 100ms);  // 10 Hz
    }
    EXPECT_NEAR(meter.rate(t0 + 950ms), 10.0, 1e-9);
}

TEST(RateMeterTest, IsNotFooledByTicksOnTheWindowEdges)
{
    // 5 Hz akışta 1 sn'lik pencereye 6 olay (0, 200, ..., 1000 ms) sığabilir.
    // "olay sayısı / pencere" 6.0 derdi; aralık tabanlı ölçüm 5.0 demeli.
    RateMeter meter(1001ms);
    for (int i = 0; i <= 5; ++i) {
        meter.tick(t0 + i * 200ms);
    }
    EXPECT_NEAR(meter.rate(t0 + 1000ms), 5.0, 1e-9);
}

TEST(RateMeterTest, ForgetsTicksOlderThanTheWindow)
{
    RateMeter meter(1s);
    meter.tick(t0);
    meter.tick(t0 + 100ms);
    meter.tick(t0 + 600ms);

    EXPECT_NEAR(meter.rate(t0 + 1050ms), 2.0, 1e-9);  // 0 atıldı: 100 -> 600 ms, 1 aralık
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 3s), 0.0);         // hepsi eskidi: akış durdu
}

TEST(RateMeterTest, ResetClearsHistory)
{
    RateMeter meter;
    meter.tick(t0);
    meter.tick(t0 + 100ms);
    meter.reset();
    EXPECT_DOUBLE_EQ(meter.rate(t0 + 150ms), 0.0);
}
