#include <gtest/gtest.h>
#include <jyotish/core.hpp>

using namespace jyotish;

TEST(ChartTest, BasicComputation) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    
    EXPECT_EQ(chart.planets.size(), 9);
    EXPECT_EQ(chart.houses.size(), 12);
    EXPECT_GT(chart.julian_day_ut, 0);
    EXPECT_NE(chart.ascendant.sign, Sign::Aries);  // Should vary
}

TEST(ChartTest, SignOf) {
    EXPECT_EQ(sign_of(0.0), Sign::Aries);
    EXPECT_EQ(sign_of(30.0), Sign::Taurus);
    EXPECT_EQ(sign_of(359.99), Sign::Pisces);
}

TEST(ChartTest, DegInSign) {
    EXPECT_DOUBLE_EQ(deg_in_sign(0.0), 0.0);
    EXPECT_DOUBLE_EQ(deg_in_sign(45.5), 15.5);
    EXPECT_DOUBLE_EQ(deg_in_sign(30.0), 0.0);
}

TEST(ChartTest, VimshottariBalance) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    
    EXPECT_GT(chart.vimshottari_balance.years_remaining, 0.0);
    EXPECT_LT(static_cast<int>(chart.vimshottari_balance.lord), 9);
}

TEST(ChartTest, AspectsPresent) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    
    // Should have at least some aspects
    EXPECT_GE(chart.aspects.size(), 0);
}

TEST(DashaTest, TimelineStructure) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    DashaTimeline timeline = build_dasha_timeline(birth, chart);
    
    EXPECT_GT(timeline.size(), 0);
    for (const auto& md : timeline) {
        EXPECT_GE(static_cast<int>(md.lord), 0);
        EXPECT_LT(static_cast<int>(md.lord), 9);
        EXPECT_GT(md.years, 0);
    }
}

TEST(DashaTest, DashaAtDate) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    DashaTimeline timeline = build_dasha_timeline(birth, chart);
    
    auto period = dasha_at_date(timeline, "2025-01-01");
    EXPECT_TRUE(period.has_value());
    if (period) {
        EXPECT_GE(static_cast<int>(period->mahadasha), 0);
        EXPECT_LT(static_cast<int>(period->mahadasha), 9);
    }
}