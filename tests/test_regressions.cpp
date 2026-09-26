#include <gtest/gtest.h>
#include <jyotish/oracle.hpp>

using namespace jyotish::oracle;

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