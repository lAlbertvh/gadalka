#include <jyotish/feedback.hpp>
#include <gtest/gtest.h>

namespace {

TEST(FeedbackTest, PureRatingsAreRecognized) {
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("да", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("нет", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("да, полезен", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("да, полезно", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("нет, не помог", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("спасибо, было полезно", "ru"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("yes", "en"));
    EXPECT_TRUE(jyotish::feedback::is_pure_rating("no, not helpful", "en"));
}

TEST(FeedbackTest, QuestionsAreNeverEatenByFeedbackHook) {
    // The bug the hook used to cause: "да, а расскажи кого мне лучше полюбить?"
    // started with "да," → classified as a rating → the question was swallowed
    // and the user got "Записал — спасибо". These must NOT be pure ratings.
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("да, а расскажи кого мне лучше полюбить?", "ru"));
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("да, а сколько ещё бесплатных вопросов у меня?", "ru"));
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("нет, а что с моей карьерой?", "ru"));
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("да полезен, но что ждёт меня в 2026?", "ru"));
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("yes, but what about love?", "en"));
    EXPECT_FALSE(jyotish::feedback::is_pure_rating("почему не помог ответ?", "ru"));
}

TEST(FeedbackTest, MarkerOnlyAsk) {
    // ask() no longer pushes the visible "answer yes/no" sentence, only the
    // marker, so nothing in a reply invites a typed rating anymore.
    std::string ru = jyotish::feedback::ask("ru", true);
    EXPECT_NE(ru.find("[FEEDBACK|g1]"), std::string::npos);
    EXPECT_EQ(ru.find("Был ли ответ полезен"), std::string::npos);
    std::string en = jyotish::feedback::ask("en", false);
    EXPECT_NE(en.find("[FEEDBACK|g0]"), std::string::npos);
    EXPECT_EQ(en.find("Was that answer helpful"), std::string::npos);
}

}  // namespace