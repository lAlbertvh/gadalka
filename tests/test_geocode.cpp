#include <gtest/gtest.h>
#include <jyotish/geocode.hpp>
#include <jyotish/oracle.hpp>
#include <jyotish/parsing.hpp>

#include <cmath>
#include <string>

namespace geo = jyotish::geocode;

TEST(GeocodeTest, NormalizeAndTransliterate) {
    EXPECT_EQ(geo::normalize_city("Санкт-Петербург"), geo::normalize_city("санкт петербург"));
    EXPECT_EQ(geo::normalize_city("München"), geo::normalize_city("munchen"));
    EXPECT_EQ(geo::transliterate("Москва"), "moskva");
    EXPECT_EQ(geo::transliterate("Ёлка"), "elka");
}

TEST(GeocodeTest, ExactLookup) {
    auto m = geo::resolve_exact("Москва");
    ASSERT_TRUE(m.has_value());
    EXPECT_GT(m->population, 5000000);

    auto spb = geo::resolve_exact("СПб");
    ASSERT_TRUE(spb.has_value());
    EXPECT_GT(spb->population, 1000000);
}

TEST(GeocodeTest, FuzzyDeclension) {
    auto res = geo::search("в Москве", 3);
    ASSERT_FALSE(res.empty());
    EXPECT_GT(res.front().score, 0);

    auto m = geo::resolve_city("москве");
    ASSERT_TRUE(m.has_value());
    EXPECT_GT(m->population, 5000000);
}

TEST(GeocodeTest, LatinAndCyrillicAliases) {
    auto ny = geo::find_cities("New York", 3);
    ASSERT_FALSE(ny.empty());
    EXPECT_NEAR(ny.front().latitude, 40.7, 1.5);

    auto beijing = geo::find_cities("Beijing", 3);
    ASSERT_FALSE(beijing.empty());
    EXPECT_NEAR(beijing.front().longitude, 116.4, 1.5);

    auto istanbul = geo::find_cities("Стамбул", 3);
    ASSERT_FALSE(istanbul.empty());
    EXPECT_GT(istanbul.front().population, 5000000);
}

TEST(GeocodeTest, ExtractCityFromPhrase) {
    auto m = jyotish::oracle::extract_city("Меня зовут Анна, родилась 15.08.1995 в 14:30 в Москве");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->name, "Москва");

    auto k = jyotish::oracle::extract_city("родился в Казани");
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(k->name, "Казань");

    auto spb = jyotish::oracle::extract_city("I was born in Saint Petersburg");
    ASSERT_TRUE(spb.has_value());
    EXPECT_NE(spb->name.find("Петербург"), std::string::npos);
}

TEST(GeocodeTest, WorldCoverage) {
    EXPECT_TRUE(geo::resolve_exact("Токио").has_value());
    EXPECT_TRUE(geo::resolve_exact("Мехико").has_value());
    EXPECT_TRUE(geo::resolve_exact("Рим").has_value());
    EXPECT_TRUE(geo::resolve_exact("Пекин").has_value());
    EXPECT_TRUE(geo::resolve_exact("Стамбул").has_value());
    EXPECT_TRUE(geo::resolve_exact("Мумбаи").has_value());
}

TEST(GeocodeTest, EnforceUserNameReplacesWrongName) {
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Алексей, ситуация устойчивая. Продолжайте.",
                  "Альберт"),
              "Альберт, ситуация устойчивая. Продолжайте.");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "алексей, смотрите сюда",
                  "Альберт"),
              "альберт, смотрите сюда");
    // Famous-person analogy must survive (capitalized name + surname).
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "карта Александра Пушкина очень показательна",
                  "Альберт"),
              "карта Александра Пушкина очень показательна");
    // Astrology terms that are also names ("лев", "рак") stay untouched.
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "у вас лев в десятом доме, а рак на границе",
                  "Альберт"),
              "у вас лев в десятом доме, а рак на границе");
    // No replacement needed when the user's own name is already used.
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Альберт, всё сходится",
                  "Альберт"),
              "Альберт, всё сходится");
}

TEST(GeocodeTest, EnforceUserNameOneMore) {
    EXPECT_EQ(jyotish::oracle::enforce_user_name("да, Алексей, вы правы", "Альберт"),
              "да, Альберт, вы правы");
    EXPECT_EQ(jyotish::oracle::enforce_user_name("Анна, послушайте", "Альберт"),
              "Альберт, послушайте");
    EXPECT_EQ(jyotish::oracle::enforce_user_name("вы знаете, Пётр...", "Алексей"),
              "вы знаете, Алексей...");
}

// Names starting with Cyrillic Р-Я (second UTF-8 byte 0xA0-0xAF) must be
// lowercased for the wrong-name matcher, otherwise the LLM's invented
// "Сергей"/"Юлия" would never be replaced with the real name.
TEST(GeocodeTest, EnforceUserNameReplacesNamesStartingWithRtoYa) {
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Приветствую вас, Сергей! Я начну с гороскопа.",
                  "Альберт"),
              "Приветствую вас, Альберт! Я начну с гороскопа.");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Сергей, давайте посмотрим на вашу Луну",
                  "Альберт"),
              "Альберт, давайте посмотрим на вашу Луну");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Юлия, всё готово",
                  "Альберт"),
              "Альберт, всё готово");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Светлана", "Альберт"),
              "Альберт");
    // The user's own name starting with Р-Я stays untouched.
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Сергей, всё сходится",
                  "Сергей"),
              "Сергей, всё сходится");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Юлия, привет", "Юлия"),
              "Юлия, привет");
    // A real name starting with Р-Я still gets the capital letter in the reply.
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "Анна, привет", "Юлия"),
              "Юлия, привет");
    EXPECT_EQ(jyotish::oracle::enforce_user_name(
                  "поздравляю, Алексей", "Сергей"),
              "поздравляю, Сергей");
}

// The LLM sometimes emits a literal "[Ваше имя]"-style placeholder instead of
// the user's name. It must be replaced with the real name (or dropped).
TEST(GeocodeTest, SubstituteNamePlaceholders) {
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "Здравствуйте! Меня зовут [Ваше имя], и я ваш гид.",
                  "Альберт"),
              "Здравствуйте! Меня зовут Альберт, и я ваш гид.");
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "[Имя пользователя], вот ваш гороскоп",
                  "Альберт"),
              "Альберт, вот ваш гороскоп");
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "Привет, [Your Name]!",
                  "Альберт"),
              "Привет, Альберт!");
    // Unknown name -> placeholder brackets are removed, not left in the text.
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "Меня зовут [Ваше имя].", ""),
              "Меня зовут .");
    // Non-name brackets and real names are left untouched.
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "Меня зовут [Цитата] оракул.", "Альберт"),
              "Меня зовут [Цитата] оракул.");
    EXPECT_EQ(jyotish::oracle::substitute_name_placeholders(
                  "Здравствуйте, Альберт!", "Альберт"),
              "Здравствуйте, Альберт!");
}
