#include <gtest/gtest.h>
#include <jyotish/core.hpp>
#include <jyotish/forecast.hpp>
#include <nlohmann/json.hpp>

using namespace jyotish;

TEST(DashaTest, AntardashaStructure) {
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
    
    // Check that first mahadasha has antardashas
    ASSERT_FALSE(timeline.empty());
    EXPECT_GT(timeline[0].antardashas.size(), 0);
    
    for (const auto& ad : timeline[0].antardashas) {
        EXPECT_GE(static_cast<int>(ad.lord), 0);
        EXPECT_LT(static_cast<int>(ad.lord), 9);
        EXPECT_GT(ad.years, 0);
    }
}

TEST(DashaTest, DashaAtDateFuture) {
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
    
    // Test a future date
    auto period = dasha_at_date(timeline, "2030-06-15");
    EXPECT_TRUE(period.has_value());
    if (period) {
        EXPECT_GE(static_cast<int>(period->mahadasha), 0);
        EXPECT_LT(static_cast<int>(period->mahadasha), 9);
    }
}

TEST(TransitTest, SnapshotStructure) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    TransitSnapshot snap = compute_transit_snapshot(chart, "2025-01-01", "12:00", 3.0);
    
    EXPECT_EQ(snap.date, "2025-01-01");
    EXPECT_EQ(snap.planets.size(), 9);
}

TEST(TransitTest, LocalizeTransits) {
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    Chart chart = compute_chart(birth);
    TransitSnapshot snap = compute_transit_snapshot(chart, "2025-01-01", "12:00", 3.0);
    TransitSnapshot localized = localize_transits(snap, "ru");
    
    EXPECT_EQ(localized.date, snap.date);
    EXPECT_EQ(localized.planets.size(), snap.planets.size());
}

TEST(PeriodForecastTest, Structure) {
    auto f = forecast::compute_period_forecast("2025-01-01", "12:00", 3.0, "ru");

    ASSERT_EQ(f.signs.size(), 12);
    EXPECT_FALSE(f.title.empty());
    EXPECT_FALSE(f.summary.empty());
    EXPECT_EQ(f.date, "2025-01-01");
    EXPECT_EQ(f.time, "12:00");

    for (const auto& sg : f.signs) {
        EXPECT_GE(sg.house, 1);
        EXPECT_LE(sg.house, 12);
        EXPECT_GE(sg.mars_house, 1);
        EXPECT_LE(sg.mars_house, 12);
        EXPECT_FALSE(sg.focus.empty());
        EXPECT_FALSE(sg.jupiter.empty());
        EXPECT_FALSE(sg.mars.empty());
        EXPECT_FALSE(sg.how_to_live.empty());
        EXPECT_FALSE(sg.training.empty());
        EXPECT_FALSE(sg.text.empty());

        // House book-keeping against the real computed transit signs.
        int house = (static_cast<int>(f.jupiter_sign) - static_cast<int>(sg.lagna) + 12) % 12 + 1;
        EXPECT_EQ(sg.house, house);
        int mars_house = (static_cast<int>(f.mars_sign) - static_cast<int>(sg.lagna) + 12) % 12 + 1;
        EXPECT_EQ(sg.mars_house, mars_house);

        EXPECT_NE(sg.text.find("Как проживать"), std::string::npos);
        EXPECT_NE(sg.text.find("К интенсиву"), std::string::npos);
    }
}

TEST(PeriodForecastTest, ToJson) {
    auto f = forecast::compute_period_forecast("2025-01-01", "12:00", 3.0, "ru");
    nlohmann::json j;
    forecast::to_json(j, f);

    EXPECT_TRUE(j.contains("signs"));
    EXPECT_EQ(j["signs"].size(), 12);
    EXPECT_FALSE(j["title"].get<std::string>().empty());
    EXPECT_FALSE(j["summary"].get<std::string>().empty());
    EXPECT_TRUE(j["jupiter"].contains("sign"));
    EXPECT_TRUE(j["jupiter"].contains("sign_local"));
    EXPECT_TRUE(j["mars"].contains("sign"));

    EXPECT_FALSE(j["signs"][0]["lagna"].get<std::string>().empty());
    EXPECT_FALSE(j["signs"][0]["text"].get<std::string>().empty());
}