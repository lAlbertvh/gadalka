#include <jyotish/forecast.hpp>
#include <jyotish/i18n.hpp>
#include "swe_util.h"
#include <array>
#include <cmath>

namespace jyotish::forecast {

namespace {

// Whole-sign house a transiting planet's sign occupies relative to a rising sign.
int house_of(Sign transit, Sign lagna) {
    return (static_cast<int>(transit) - static_cast<int>(lagna) + 12) % 12 + 1;
}

// One house slot of the period copy. All six fields are RU; the source template
// is Russian and the five trainable courses («Границы», «Тело», «Дуальность»,
// «Проявленность», «Монетизация талантов») are RU brand names, so the copy is
// intentionally not translated.
struct Slot {
    const char* focus;
    const char* jupiter;
    const char* mars;
    const char* favourable;   // optional trailing sentence(s), may be empty
    const char* how_to_live;
    const char* training;
};

// Index 0 is unused; SLOTS[1..12] hold the copy for the corresponding house.
const std::array<Slot, 13> SLOTS = {{
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},                       // 0
    {"Фокус на себе: личность, тело и новый жизненный цикл выходят на первый план.",
     "Юпитер расширяет уверенность и даёт внутренний рост — желание начать всё с чистого листа.",
     "Марс добавляет энергию и решимость, но может спровоцировать импульсивность и поспешные решения.",
     "",
     "Начинайте без войны с собой. Юпитер учит начинать мягко и уверенно, Марс — действовать, но не сжигать себя на старте.",
     "тренинг 4 «Проявленность» уберёт синдром самозванца и поможет уверенно заявить о себе на новом этапе."},                  // 1
    {"Фокус на финансах, накоплениях, речи и семейных ценностях.",
     "Юпитер приносит рост дохода через знания, контакты и постоянство, укрепляет семейную опору.",
     "Марс может давать импульсивные траты и острые слова — считайте до покупок и говорите мягче.",
     "",
     "Растите спокойно и без жадности. Деньги приходят туда, где есть устойчивость: ведите бюджет, вкладывайте с умом, цените близких.",
     "тренинг 5 «Монетизация талантов» покажет новые варианты прихода денег."},                                                  // 2
    {"Фокус на коммуникации, обучении, документах, торговле и перемещениях.",
     "Юпитер даёт наставников и полезные знания, помогает в переговорах, оформлении документов и коротких поездках.",
     "Марс ускоряет дела, но может добавлять споры и поспешные слова.",
     "",
     "Договаривайтесь, а не продавливайте. Курсы, поездки и честные разговоры сейчас заряжают сильнее всего.",
     "тренинг 3 «Дуальность» поможет выходить победителем из любой ситуации, не деля её на плохое и хорошее."},                  // 3
    {"Фокус на доме, семье, недвижимости и внутреннем уюте.",
     "Юпитер защищает семью, помогает с жильём и приносит тепло в дом.",
     "Марс может вносить бытовые конфликты и суету с переездом — дом лучше укреплять спокойно.",
     "",
     "Стройте внутренний дом — там, где есть опора, не страшны внешние бури. Семейное решайте через заботу, а не через спор.",
     "тренинг 1 «Границы» вернёт вашу энергию и поможет беречь пространство дома и семьи."},                                    // 4
    {"Фокус на любви, творчестве, детях и радости жизни.",
     "Юпитер усиливает вдохновение и желание делиться теплом.",
     "Марс может давать эмоциональные качели или страх потери.",
     "Благоприятно инвестировать, налаживать связи, обучать других и выстраивать бизнес-системы.",
     "Позвольте себе радость без драм. Творите, любите, играйте — не через боль, а через искренность. Сердце сейчас лечится удовольствием и правдой чувств.",
     "тренинг 5 «Монетизация талантов» покажет, как зарабатывать на том, что вы любите."},                                      // 5
    {"Фокус на здоровье, режиме, работе и служении.",
     "Юпитер помогает восстановить силы и найти смысл в заботе о теле.",
     "Марс может давать воспаления, усталость и раздражение от рутины.",
     "При этом вы очень конкурентоспособны, и даже сложные юридические или судебные вопросы могут разрешиться в вашу пользу.",
     "Не игнорируйте сигналы тела. Маленькие, регулярные шаги дадут большой результат. Юпитер исцеляет через осознанность, Марс требует движения — но без перегруза.",
     "тренинг 2 «Тело» вернёт контакт с телом, энергию и здоровье."},                                                          // 6
    {"Фокус на партнёрстве и отношениях.",
     "Юпитер приносит возможность зрелых, поддерживающих союзов.",
     "Марс может провоцировать эмоциональные реакции и борьбу за лидерство — важно искать баланс и учиться заряжаться друг от друга.",
     "",
     "Договаривайтесь сердцем, а не силой. Отношения либо углубляются, либо показывают, где нет опоры. Юпитер учит сотрудничеству, Марс — честности чувств.",
     "тренинг 4 «Проявленность» научит выражать себя без борьбы и быть увереннее в себе."},                                    // 7
    {"Глубокая трансформация через доверие, близость и ресурсы партнёров.",
     "Юпитер даёт защиту в кризисах и поддержку рода.",
     "Марс может поднимать страхи, ревность и желание всё контролировать.",
     "Возможны неожиданные деньги, вскрытие тайного, а также изменения в здоровье, которые вы наконец решитесь гармонично решить — всё к лучшему.",
     "Отпускайте контроль. Там, где появляется доверие жизни, приходит исцеление. Это время внутреннего взросления и освобождения от старых узлов.",
     "тренинг 1 «Границы» вернёт вашу энергию, а тренинг 5 «Монетизация талантов» покажет новые варианты прихода денег."},      // 8
    {"Фокус на смысле, вере, учителях, дальних дорогах, а также отношениях с отцом, правительством и государственными структурами.",
     "Юпитер приносит духовный рост и наставников,",
     "Марс может вызывать внутренний протест и конфликты с авторитетами.",
     "",
     "Не воюйте с истиной — позвольте ей раскрыться. Это период, когда вера укрепляется через опыт. Благоприятно для обучения, духовных практик и осознанных путешествий.",
     "тренинг 3 «Дуальность» поможет выходить победителем из любой ситуации, не деля её на плохое и хорошее."},                  // 9
    {"Фокус на карьере, статусе и социальной реализации.",
     "Юпитер может дать рост, признание и поддержку руководства.",
     "Марс создаёт давление ответственности и возможные конфликты с авторитетами.",
     "",
     "Действуйте не через борьбу, а через внутреннюю уверенность. Желание всё бросить — признак усталости, а не ошибки пути. Юпитер учит вас расти мягко, Марс — брать ответственность, не разрушая себя.",
     "тренинг 4 «Проявленность» уберёт синдром самозванца и поможет ставить цену без обесценивания."},                         // 10
    {"Фокус на друзьях, социальных связях, публичности и больших целях.",
     "Юпитер расширяет круг общения, приводит полезных людей и поддерживает крупные цели.",
     "Марс активизирует социальную жизнь, но возможны споры и необходимость отстаивать своё место в коллективе.",
     "",
     "Даже если кто-то уходит — это освобождение. Юпитер приведёт тех, с кем можно строить будущее, а Марс поможет отстоять границы без разрушений.",
     "в программе есть чат поддержки и бонусная встреча с индивидуальными ответами — зрелые связи и опора."},                     // 11
    {"Фокус на отдыхе, завершении циклов, одиночестве и духовных практиках.",
     "Юпитер помогает мягко отпустить лишнее и обрести глубинный покой, поддерживает практики и дальние земли.",
     "Марс может давать тревогу, бессонницу и желание уйти от мира.",
     "",
     "Разрешите себе паузу. То, что завершается сейчас, завершается к лучшему; отдых и уединение — не лень, а восстановление корней.",
     "тренинг 2 «Тело» вернёт контакт с телом, энергию и здоровье — пауза сейчас продуктивна."}                                 // 12
}};

// Sign block headers, exactly as in the source template ("♎️ ВЕСЫ").
const std::array<const char*, 12> EMOJI = {
    "♈️", "♉️", "♊️", "♋️", "♌️", "♍️", "♎️", "♏️", "♐️", "♑️", "♒️", "♓️"
};
const std::array<const char*, 12> SIGN_UPPER = {
    "ОВЕН", "ТЕЛЕЦ", "БЛИЗНЕЦЫ", "РАК", "ЛЕВ", "ДЕВА",
    "ВЕСЫ", "СКОРПИОН", "СТРЕЛЕЦ", "КОЗЕРОГ", "ВОДОЛЕЙ", "РЫБЫ"
};

std::string period_paragraph(const SignForecast& sg) {
    std::string out = sg.jupiter;
    if (!sg.mars.empty()) out += " " + sg.mars;
    if (!sg.favourable.empty()) out += " " + sg.favourable;
    return out;
}

std::string build_block(int lagna_idx, const SignForecast& sg) {
    std::string out;
    out += EMOJI[static_cast<size_t>(lagna_idx)];
    out += " " + std::string(SIGN_UPPER[static_cast<size_t>(lagna_idx)]) + "\n";
    out += sg.focus + "\n";
    out += period_paragraph(sg) + "\n";
    out += "💗Как проживать:\n" + sg.how_to_live + "\n";
    out += "🌟К интенсиву: " + sg.training;
    return out;
}

std::string sign_local(const std::string& lang, Sign s) {
    const auto& t = jyotish::get(lang);
    auto it = t.sign.find(sign_name(s));
    return it != t.sign.end() ? it->second : sign_name(s);
}

} // namespace

PeriodForecast compute_period_forecast(const std::string& date, const std::string& time,
                                        double tz_offset, const std::string& lang) {
    PeriodForecast f;
    f.date = date;
    f.time = time;
    f.tz_offset = tz_offset;
    f.lang = lang == "ru" ? "ru" : "en";

    swe_detail::swe_init();
    const double jd = swe_detail::julian_day_ut(date, time, tz_offset);

    double jlon = 0.0, jspd = 0.0, mlon = 0.0, mspd = 0.0;
    swe_detail::calc_planet(Planet::Jupiter, jd, jlon, jspd);
    swe_detail::calc_planet(Planet::Mars, jd, mlon, mspd);

    f.jupiter_sign = sign_of(jlon);
    f.mars_sign = sign_of(mlon);
    f.jupiter_deg = static_cast<int>(std::lround(deg_in_sign(jlon)));
    f.mars_deg = static_cast<int>(std::lround(deg_in_sign(mlon)));

    if (f.jupiter_sign == f.mars_sign) {
        f.title = "Прогноз периода: Юпитер и Марс в " + sign_local(f.lang, f.jupiter_sign);
    } else {
        f.title = "Прогноз периода: Юпитер в " + sign_local(f.lang, f.jupiter_sign)
                + ", Марс в " + sign_local(f.lang, f.mars_sign);
    }
    f.summary = "✨Итог периода\n"
                "Это не время быстрых побед.\n"
                "Это время создания внутреннего дома —\n"
                "где есть мудрость Юпитера, живая энергия Марса\n"
                "и нет войны внутри себя.";

    f.signs.reserve(12);
    for (int s = 0; s < 12; ++s) {
        const Sign lagna = static_cast<Sign>(s);
        const int house = house_of(f.jupiter_sign, lagna);
        const int mars_house = house_of(f.mars_sign, lagna);

        SignForecast sg;
        sg.lagna = lagna;
        sg.house = house;
        sg.mars_house = mars_house;
        sg.focus = SLOTS[static_cast<size_t>(house)].focus;
        sg.jupiter = SLOTS[static_cast<size_t>(house)].jupiter;
        sg.mars = SLOTS[static_cast<size_t>(mars_house)].mars;
        sg.favourable = SLOTS[static_cast<size_t>(house)].favourable;
        sg.how_to_live = SLOTS[static_cast<size_t>(house)].how_to_live;
        sg.training = SLOTS[static_cast<size_t>(house)].training;
        sg.text = build_block(s, sg);
        f.signs.push_back(std::move(sg));
    }
    return f;
}

void to_json(nlohmann::json& j, const PeriodForecast& f) {
    j["date"] = f.date;
    j["time"] = f.time;
    j["tz_offset"] = f.tz_offset;
    j["lang"] = f.lang;
    j["jupiter"] = nlohmann::json{{"sign", sign_name(f.jupiter_sign)},
                                  {"sign_local", sign_local(f.lang, f.jupiter_sign)},
                                  {"degree", f.jupiter_deg}};
    j["mars"] = nlohmann::json{{"sign", sign_name(f.mars_sign)},
                               {"sign_local", sign_local(f.lang, f.mars_sign)},
                               {"degree", f.mars_deg}};
    j["title"] = f.title;
    j["summary"] = f.summary;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& sg : f.signs) {
        nlohmann::json item;
        item["lagna"] = sign_name(sg.lagna);
        item["lagna_local"] = sign_local(f.lang, sg.lagna);
        item["house"] = sg.house;
        item["mars_house"] = sg.mars_house;
        item["focus"] = sg.focus;
        item["jupiter"] = sg.jupiter;
        item["mars"] = sg.mars;
        item["favourable"] = sg.favourable;
        item["how_to_live"] = sg.how_to_live;
        item["training"] = sg.training;
        item["text"] = sg.text;
        arr.push_back(std::move(item));
    }
    j["signs"] = std::move(arr);
}

} // namespace jyotish::forecast