#include <gtest/gtest.h>
#include <jyotish/oracle.hpp>
#include <jyotish/parsing.hpp>
#include <jyotish/core.hpp>
#include <jyotish/i18n.hpp>

using namespace jyotish::oracle;
using namespace jyotish;

TEST(RegressionsTest, JyotishExplainersAnswerBeforeOnboarding) {
    // "расскажи про джайотишь" is an informational question — it must get a
    // real answer, not the "give me your birth data" onboarding demand.
    for (const char* q : {
        "привет расскажи про джайотишь",
        "расскажи про джайотишь",
        "что такое джйотиш?",
        "расскажи про джайотиш",
        "расскажи про веды",
        "что такое ведическая астрология",
    }) {
        auto canned = casual_answer(q, "ru");
        ASSERT_TRUE(canned.has_value());
        EXPECT_NE(canned->find("ведическая астрология"), std::string::npos);
        EXPECT_TRUE(is_casual_question(q, "ru", nullptr));
    }

    auto en = casual_answer("tell me about jyotish", "en");
    ASSERT_TRUE(en.has_value());
    EXPECT_NE(en->find("Vedic astrology"), std::string::npos);
}

TEST(RegressionsTest, UnknownTimeNeverSilentlyConfirms) {
    EXPECT_TRUE(signals_unknown_time("да только время рождения я не знаю"));
    EXPECT_TRUE(signals_unknown_time("не помню точное время"));
    EXPECT_TRUE(signals_unknown_time("примерно, точно не знаю"));
    EXPECT_TRUE(signals_unknown_time("время рождения не знаю"));
    EXPECT_FALSE(signals_unknown_time("да, всё верно"));
    EXPECT_FALSE(signals_unknown_time("да"));
    EXPECT_FALSE(signals_unknown_time("подтверждаю"));
}

TEST(RegressionsTest, ConfirmGateBlocksLeadingDaCaveat) {
    BirthInfo info;
    info.name = "Ваня";
    info.birth_date = "1997-06-04";
    info.birth_time = "12:00";
    info.city = "Луга";
    info.city_found = true;

    // There was a confirmation echo with this exact marker; now the user says
    // "да" but immediately adds that the time is unknown.
    const std::string marker = "CONFIRM|1997-06-04|12:00|Луга|Ваня";
    std::vector<std::string> history = {
        "assistant: Я понял так: вы родились 04 июня 1997 в 12:00 в городе Луга.\nВас зовут Ваня.\nВерно? Если да — ответьте «да», «верно» или «подтверждаю».\n[" + marker + "]",
    };
    auto res = confirm_birth("да только время рождения я не знаю", history, info, "ru");
    EXPECT_EQ(res.state, ConfirmState::NeedConfirm);
    EXPECT_NE(res.reply.find("полдень (12:00)"), std::string::npos);

    // A clean "да" still confirms.
    res = confirm_birth("да", history, info, "ru");
    EXPECT_EQ(res.state, ConfirmState::Confirmed);
    EXPECT_TRUE(res.just_confirmed);
}

TEST(RegressionsTest, EnglishLeakDetection) {
    EXPECT_TRUE(has_english_leak("проявиться черезIncreased self-discipline and a greater focus on personal responsibility", 30));
    EXPECT_TRUE(has_english_leak("Это хорошо.This is a really long English sentence that keeps going on and on and on forever here", 30));
    EXPECT_FALSE(has_english_leak("Привет! Сегодня отличный день, правда? Кот Марк спит."));
    EXPECT_FALSE(has_english_leak("qwen2.5:7b-instruct говорит хорошо", 30));
    EXPECT_FALSE(has_english_leak(""));
}

TEST(RegressionsTest, MismatchGuardIsQuietOnHypotheticalsAndGenerals) {
    Chart::BirthData birth;
    birth.name = "Тест";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    jyotish::Chart chart = jyotish::compute_chart(birth);
    const auto& t = jyotish::get("ru");

    // Hypothetical ("а если бы…") — the guard must stay silent, not argue.
    auto mm = detect_chart_mismatch(chart, "а если бы Венера была в Скорпионе, что бы это значило?", "ru");
    EXPECT_TRUE(mm.wrong_planet_signs.empty());
    EXPECT_TRUE(mm.absent_nakshatras.empty());

    // General-knowledge nakshatra question — no "в вашей карте её нет" lecture.
    mm = detect_chart_mismatch(chart, "что значит накшатра Пушья?", "ru");
    EXPECT_TRUE(mm.absent_nakshatras.empty());

    // Anchored to the user's own chart with a wrong attribution — guard fires.
    const jyotish::Sign actual = chart.planets[5].sign;             // Venus
    const jyotish::Sign wrong = actual == jyotish::Sign::Scorpio ? jyotish::Sign::Taurus : jyotish::Sign::Scorpio;
    const std::string wname = t.sign.at(jyotish::sign_name(wrong));
    mm = detect_chart_mismatch(chart, "у меня Венера в " + wname, "ru");
    ASSERT_FALSE(mm.wrong_planet_signs.empty());
    ASSERT_FALSE(mm.true_positions.empty());

    // Own-chart nakshatra genuinely absent from the chart — fires with facts.
    jyotish::Nakshatra absent = jyotish::Nakshatra::Pushya;
    jyotish::Nakshatra chart_has[9];
    for (int i = 0; i < 9; ++i) chart_has[i] = chart.planets[i].nakshatra.nakshatra;
    bool in_chart = false;
    do {
        in_chart = false;
        for (auto nh : chart_has) if (nh == absent) { in_chart = true; break; }
        absent = in_chart ? static_cast<jyotish::Nakshatra>((static_cast<int>(absent) + 1) % 27) : absent;
    } while (in_chart);
    const auto lit = t.nakshatra.find(jyotish::nakshatra_name(absent));
    const std::string ndisp = (lit != t.nakshatra.end()) ? lit->second : jyotish::nakshatra_name(absent);
    mm = detect_chart_mismatch(chart, "что значит накшатра " + ndisp + " в моей карте?", "ru");
    ASSERT_FALSE(mm.absent_nakshatras.empty());
    EXPECT_NE(mismatch_block(mm, chart, "ru").find("НЕТ"), std::string::npos);
}
