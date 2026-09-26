#include <gtest/gtest.h>
#include <jyotish/oracle.hpp>

using namespace jyotish;
using namespace jyotish::oracle;

TEST(OnboardingTest, FutureYearDetectedInDate) {
    EXPECT_TRUE(future_year_in_text("привет я серега родился 35.04.2380 ночью").has_value());
    EXPECT_EQ(*future_year_in_text("35.04.6789 в 2.00"), 6789);
    EXPECT_EQ(*future_year_in_text("я родился 15.08.2030"), 2030);
    EXPECT_FALSE(future_year_in_text("15.08.1995 в 14:30 в Москве").has_value());
    EXPECT_FALSE(future_year_in_text("мне 36 лет").has_value());
    EXPECT_FALSE(future_year_in_text("телефон +79162345678").has_value());
    EXPECT_FALSE(future_year_in_text("").has_value());
}

TEST(OnboardingTest, FutureDateReplyMentionsYear) {
    BirthInfo info;
    info.name = "Серега";
    std::string ru = future_date_reply(info, "ru", 2380);
    EXPECT_NE(ru.find("посланник из будущего"), std::string::npos);
    EXPECT_NE(ru.find("2380"), std::string::npos);
    EXPECT_NE(ru.find("Серега"), std::string::npos);
    EXPECT_NE(ru.find("15.08.1995"), std::string::npos);

    std::string en = future_date_reply({}, "en", 6789);
    EXPECT_NE(en.find("messenger from the future"), std::string::npos);
    EXPECT_NE(en.find("6789"), std::string::npos);
}

TEST(OnboardingTest, ReplyClipNeverSplitsWordsOrUtf8) {
    // Short enough — untouched.
    EXPECT_EQ(clip_reply("короткий ответ.", 1500), "короткий ответ.");

    // The '.' lands at byte 73, past the cap 70 — the answer is hard-cut at 69
    // (right after a full "ь") and stays ≤ budget. Result stays VALID UTF-8.
    auto valid_utf8 = [](const std::string& s) {
        for (size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = 1;
            if (c < 0x80) n = 1;
            else if ((c & 0xE0) == 0xC0) n = 2;
            else if ((c & 0xF0) == 0xE0) n = 3;
            else if ((c & 0xF8) == 0xF0) n = 4;
            else return false;
            if (i + n > s.size()) return false;
            for (size_t j = 1; j < n; ++j)
                if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) return false;
            i += n;
        }
        return true;
    };
    const std::string wars =
        "Первое предложение про карьеру и деньги. Второе про любовь и семью. "
        "Третье про здоровье и поездки. Четвертое про войны и мир. "
        "Пятое про путешествия и творчество. Шестое про учёбу и друзей.";
    std::string clipped = clip_reply(wars, 70);
    ASSERT_LE(clipped.size(), 70u);
    EXPECT_NE(clipped.find("Первое предложение про карьеру"), std::string::npos);
    EXPECT_TRUE(valid_utf8(clipped)) << "обрезка не должна ломать UTF-8: '" << clipped << "'";

    // With a boundary in reach, the cut lands right after the sentence end.
    std::string clipped2 = clip_reply(wars, 120);
    ASSERT_LE(clipped2.size(), 120u);
    EXPECT_EQ(clipped2.back(), '.') << "конец обрезки = конец предложения: '" << clipped2 << "'";

    // Multibyte "…" (E2 80 A6) is kept whole as an end-boundary.
    // "да, всё хорошо" = 25 байт + "…"; budget 28 = как раз после "…".
    const std::string dots = "да, всё хорошо… и вдруг";
    EXPECT_EQ(clip_reply(dots, 28), "да, всё хорошо…");

    // No punctuation at all → hard byte-safe cut; byte 10 был серединой "д",
    // потому откат до 9 даёт "один " → "один".
    const std::string nopunct = "один два три четыре пять шесть семь восемь";
    EXPECT_EQ(clip_reply(nopunct, 10), "один");
    EXPECT_EQ(clip_reply(nopunct, 12), "один д");  // байт 12 = середина "в" → откат на 11

    // Cut that lands inside a multibyte char backs up to its leading byte.
    EXPECT_EQ(clip_reply("абвгдеёжз", 5), "аб");

    // budget 0 means empty.
    EXPECT_EQ(clip_reply("что угодно", 0), "");
    // Empty input stays empty.
    EXPECT_EQ(clip_reply("", 100), "");
}

TEST(OnboardingTest, FutureDateNotAcceptedIntoBirthData) {
    auto info = gather_birth({"Меня зовут Серега, родился 35.04.2380 ночью"});
    EXPECT_EQ(info.name, "Серега");
    EXPECT_TRUE(info.birth_date.empty());

    auto future = gather_birth({"Меня зовут Серега, родился 15.08.2030 в 14:30 в Москве"});
    EXPECT_TRUE(future.birth_date.empty());
}

TEST(OnboardingTest, PastDateStillAccepted) {
    auto info = gather_birth({"Меня зовут Анна, родилась 15.08.1995 в 14:30 в Москве"});
    EXPECT_EQ(info.birth_date, "1995-08-15");
    EXPECT_EQ(info.birth_time, "14:30");
    EXPECT_TRUE(info.city_found);
    EXPECT_TRUE(birth_is_ready(info));
}