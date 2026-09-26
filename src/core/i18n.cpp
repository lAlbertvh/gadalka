#include <jyotish/i18n.hpp>
#include <jyotish/types.hpp>
#include <unordered_map>

namespace jyotish {

namespace {
I18n make_ru() {
    I18n i;
    i.lagna_word = "Лагна"; i.lord_word = "Лорд"; i.planets_word = "Планеты";
    i.moon_nakshatra_word = "Лунная накшатра"; i.balance_word = "Баланс Вимшоттари"; i.years_word = "лет";
    i.retro_word = " (R)"; i.combust_word = " [Comb]";
    i.aspects_word = "Аспекты"; i.yogas_word = "Йоги"; i.areas_word = "Сферы"; i.why_word = "Почему так считаю:";
    
    i.sign = {{"Aries","Овен"},{"Taurus","Телец"},{"Gemini","Близнецы"},{"Cancer","Рак"},{"Leo","Лев"},{"Virgo","Дева"},
              {"Libra","Весы"},{"Scorpio","Скорпион"},{"Sagittarius","Стрелец"},{"Capricorn","Козерог"},{"Aquarius","Водолей"},{"Pisces","Рыбы"}};
    i.planet = {{"Sun","Солнце"},{"Moon","Луна"},{"Mars","Марс"},{"Mercury","Меркурий"},{"Jupiter","Юпитер"},
                {"Venus","Венера"},{"Saturn","Сатурн"},{"Rahu","Раху"},{"Ketu","Кету"}};
    i.nakshatra = {{"Ashwini","Ашвини"},{"Bharani","Бхарани"},{"Krittika","Криттика"},{"Rohini","Рохини"},
                   {"Mrigashira","Мригашира"},{"Ardra","Ардра"},{"Punarvasu","Пunarвасу"},{"Pushya","Пушья"},
                   {"Ashlesha","Ашлеша"},{"Magha","Магха"},{"Purva Phalguni","Пурва Пхалгуни"},
                   {"Uttara Phalguni","Уттара Пхалгуни"},{"Hasta","Хаста"},{"Chitra","Читра"},{"Swati","Свати"},
                   {"Vishakha","Вишакха"},{"Anuradha","Анурадха"},{"Jyeshtha","Джейештха"},{"Mula","Мула"},
                   {"Purva Ashadha","Пурва Ашадха"},{"Uttara Ashadha","Уттара Ашадха"},{"Shravana","Шравана"},
                   {"Dhanishta","Дхаништха"},{"Shatabhisha","Шатабхиша"},{"Purva Bhadrapada","Пурва Бхадрапада"},
                   {"Uttara Bhadrapada","Уттара Бхадрапада"},{"Revati","Ревати"}};
    i.aspect = {{"conjunction","соединение"},{"opposition","оппозиция"},{"trine","трин"},{"square","квадратура"},{"sextile","секстиль"}};
    return i;
}

I18n make_en() {
    I18n i;
    i.lagna_word = "Lagna"; i.lord_word = "Lord"; i.planets_word = "Planets";
    i.moon_nakshatra_word = "Moon Nakshatra"; i.balance_word = "Vimshottari balance"; i.years_word = "years";
    i.retro_word = " (R)"; i.combust_word = " [Comb]";
    i.aspects_word = "Aspects"; i.yogas_word = "Yogas"; i.areas_word = "Areas"; i.why_word = "Reasoning:";
    
    i.sign = {{"Aries","Aries"},{"Taurus","Taurus"},{"Gemini","Gemini"},{"Cancer","Cancer"},{"Leo","Leo"},{"Virgo","Virgo"},
              {"Libra","Libra"},{"Scorpio","Scorpio"},{"Sagittarius","Sagittarius"},{"Capricorn","Capricorn"},{"Aquarius","Aquarius"},{"Pisces","Pisces"}};
    i.planet = {{"Sun","Sun"},{"Moon","Moon"},{"Mars","Mars"},{"Mercury","Mercury"},{"Jupiter","Jupiter"},
                {"Venus","Venus"},{"Saturn","Saturn"},{"Rahu","Rahu"},{"Ketu","Ketu"}};
    i.nakshatra = {{"Ashwini","Ashwini"},{"Bharani","Bharani"},{"Krittika","Krittika"},{"Rohini","Rohini"},
                   {"Mrigashira","Mrigashira"},{"Ardra","Ardra"},{"Punarvasu","Punarvasu"},{"Pushya","Pushya"},
                   {"Ashlesha","Ashlesha"},{"Magha","Magha"},{"Purva Phalguni","Purva Phalguni"},
                   {"Uttara Phalguni","Uttara Phalguni"},{"Hasta","Hasta"},{"Chitra","Chitra"},{"Swati","Swati"},
                   {"Vishakha","Vishakha"},{"Anuradha","Anuradha"},{"Jyeshtha","Jyeshtha"},{"Mula","Mula"},
                   {"Purva Ashadha","Purva Ashadha"},{"Uttara Ashadha","Uttara Ashadha"},{"Shravana","Shravana"},
                   {"Dhanishta","Dhanishta"},{"Shatabhisha","Shatabhisha"},{"Purva Bhadrapada","Purva Bhadrapada"},
                   {"Uttara Bhadrapada","Uttara Bhadrapada"},{"Revati","Revati"}};
    i.aspect = {{"conjunction","conjunction"},{"opposition","opposition"},{"trine","trine"},{"square","square"},{"sextile","sextile"}};
    return i;
}
}

const I18n& get(const std::string& lang) {
    static const I18n ru = make_ru();
    static const I18n en = make_en();
    return lang == "ru" ? ru : en;
}

} // namespace jyotish