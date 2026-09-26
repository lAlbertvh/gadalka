#include <jyotish/theory.hpp>
#include <cctype>
#include <algorithm>
#include <unordered_set>

namespace jyotish::theory {

namespace {

// Lowercase only true Cyrillic-capital bytes (U+0410..U+042F -> U+0430..U+043F,
// U+0401 -> U+0451) and ASCII; leaves Lowercase Cyrillic untouched so keywords
// still match. This mirrors the fixed lower_u8 in research.cpp.
std::string lower_ru(const std::string& s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(out[i]);
        if (b0 == 0xD0) {
            unsigned char b1 = static_cast<unsigned char>(out[i + 1]);
            if (b1 >= 0x90 && b1 <= 0x9F) {
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(b1 + 0x20); // 0xB0..0xBF
                ++i;
            } else if (b1 == 0x81) { // Ё
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(0x91);
                ++i;
            }
        } else if (b0 < 0x80) {
            out[i] = static_cast<char>(std::tolower(b0));
        }
    }
    return out;
}

bool contains_any(const std::string& text, const std::vector<std::string>& keys) {
    for (const auto& k : keys)
        if (text.find(k) != std::string::npos) return true;
    return false;
}

std::string tag_of(Domain d) {
    switch (d) {
        case Domain::Macro:      return "macroeconomics";
        case Domain::Psychology: return "psychology";
        case Domain::Math:       return "probability & math";
        case Domain::Philosophy: return "philosophy";
    }
    return "framework";
}

} // namespace

std::string domain_name(Domain d) { return tag_of(d); }

const std::vector<Module>& modules() {
    static const std::vector<Module> v = {
        {Domain::Macro, "monetary policy",
         {"ставк", "инфляц", "цб", "денеж", "рубль", "доллар", "валют", "экономик", "ввп", "бюджет", "кризис", "санкци", "экономич", "фонд", "индекс", "акци", "рынок", "рынк", "рыноч", "котиров"},
         {"rate", "interest", "inflation", "cpi", "central bank", "fed", "ecb", "ruble", "usd", "eur", "currency", "economy", "gdp", "recession", "market", "stock", "index", "sanction", "debt", "fiscal", "monetary"},
         "Монетарная политика: центральный банк повышает ставку, чтобы сдержать инфляцию (дороже кредит -> меньше спрос), и снижает, чтобы поддержать рост. Экономика реагирует с лагом в несколько кварталов. Реальная ставка = номинальная минус инфляция; если ставка ниже инфляции, денежно-кредитные условия на деле мягкие.",
         "Monetary policy: a central bank raises the rate to cool inflation (costlier credit -> less demand) and cuts to support growth. The economy responds with a lag of several quarters. Real rate = nominal minus inflation; if the rate is below inflation, credit conditions are in fact loose."},
{Domain::Macro, "business cycle",
         {"цикл", "бум", "спад", "рецесси", "кризис", "оздоровлен", "восстановлен", "тренд", "капитал", "инвестиц"},
         {"cycle", "boom", "bust", "recession", "recovery", "trend", "investment", "capacity", "unemployment", "lead"},
         "Деловые циклы: подъём -> пик -> спад -> дно -> подъём. Показатели-опережающие (индексы менеджеров, нового строительной активности) двигаются раньше тренда; запаздывающие (безработица) — позже. Бум подкармливается кредитом, спад — сжатием ликвидности.",
         "Business cycles: expansion -> peak -> contraction -> trough -> expansion. Leading indicators (PMI, new orders) move ahead of the trend; lagging ones (unemployment) behind it. Booms are fueled by credit, contractions by liquidity tightening."},
        {Domain::Macro, "supply & prices",
         {"бензин", "нефт", "топлив", "заправк", "очеред", "дефицит", "цен", "поставк", "подорожа", "скачк", "цены"},
         {"fuel", "gas", "gasoline", "petrol", "oil", "station", "queue", "shortage", "supply", "price", "prices", "hike", "spike"},
         "Цены = баланс спроса и предложения. Очередь и дефицит возникают, когда цена не успевает подстроиться (регулируется) или спрос ажиотажно растёт из-за ожиданий. Резкий скачок цены обычно вызван шоком предложения (логистика, экспорт/импорт, санкции) или паникой. Анализируй цепочку: причина изменения предложения -> реакция спроса -> как быстро рынок вернётся к балансу.",
         "Prices = supply/demand balance. Queues and shortages appear when the price cannot adjust (regulated) or demand surges on expectations. A sharp spike usually means a supply shock (logistics, trade, sanctions) or a panic. Trace the chain: the cause of the supply shift -> how demand reacts -> how fast the market rebalances."},
        {Domain::Psychology, "biases",
         {"психолог", "страх", "тревог", "тревож", "эмоци", "паник", "уверенност", "самообман", "оптимизм", "пессимизм", "ожидан", "настроени", "ажиотаж", "паника", "очеред"},
         {"psycholog", "fear", "panic", "emotion", "bias", "optimism", "pessimism", "expectation", "anxiety", "confidence", "herd", "mood", "hoarding", "queue", "frenzy", "stampede"},
         "Когнитивные искажения: склонность к подтверждению (ищем то, что поддерживает нашу веру), эффект привязки (первое число зацепляет оценку), эвристика доступности (яркое и недавнее кажется частым). Паника сжимает горизонт решения до «сейчас». Доверие к прогнозу должно учитывать, что человек смещает веса в пользу подтверждения и меньшего риска.",
         "Cognitive biases: confirmation bias (we seek what supports our belief), anchoring (the first number grips the estimate), availability heuristic (vivid and recent feels frequent). Panic shrinks the decision horizon to 'now'. Trust in a forecast must account for weights shifting toward confirmation and lower risk."},
        {Domain::Psychology, "decisions",
         {"решени", "выбор", "поступ", "альтернатив", "риск", "шанс", "вероятност", "ложн", "совет", "стратег"},
         {"decision", "choice", "alternative", "risk", "chance", "probability", "advice", "strategy", "outcome"},
         "Принятие решений под неопределённостью: перечисли варианты, оцени выигрыш и проигрыш каждого, присвой вероятности и посчитай ожидаемое значение. Не решай из «должен оправдаться» — из ожидаемой ценности. Разделяй факты, допущения и вкусы: в предсказаниях мира допущения обязательны и должны быть названы.",
         "Decision under uncertainty: list options, score the upside/downside of each, assign probabilities and compute expected value. Decide from expected value, not from 'I must be vindicated'. Separate facts, assumptions and tastes: world forecasts are built on assumptions that must be stated."},
        {Domain::Math, "expected value",
         {"математическ", "ожидани", "вероятност", "шанс", "статистик", "среднее", "риск", "выигрыш", "расчет", "формул"},
         {"expected value", "probability", "statistics", "mean", "average", "variance", "odds", "outcome", "formula", "random"},
         "Ожидаемое значение = сумма (вероятность × исход). Высокая вероятность малого выигрыша может уступать малой вероятности большого. Дисперсия описывает разброс: прогноз, который всегда «чуть в плюс», может быть нестабильным; хвосты событий — редкие и резкие — важнее среднего в кризисах.",
         "Expected value = sum(probability × outcome). A high probability of a small gain can lose to a small probability of a large one. Variance describes spread: a forecast that is always 'slightly up' can still be unstable; fat tails — rare and sharp events — matter more than the mean in crises."},
        {Domain::Math, "reasoning",
         {"байес", "условн", "гипотез", "логик", "доказательств", "данн", "факт", "корреляц", "причин"},
         {"bayes", "conditional", "hypothesis", "logic", "evidence", "data", "fact", "correlation", "causation"},
         "Байесовское рассуждение: новая вероятность = старая вероятность × правдоподобие данных, нормированное. Один случайный факт не опровергает и не подтверждает гипотезу; счёт по накоплению. Корреляция ≠ причинность. Не принимай «одно совпадение» за закономерность.",
         "Bayesian reasoning: new probability = prior × likelihood of the data, normalized. A single random fact neither confirms nor refutes a hypothesis; count accumulates. Correlation is not causation. Do not mistake a single coincidence for a pattern."},
        {Domain::Philosophy, "wisdom",
         {"смысл", "мудрост", "жизн", "судьб", "карма", "дух", "ведическ", "джйотиш", "астролог", "учени", "путь", "осознан"},
         {"wisdom", "meaning", "karma", "dharma", "spirit", "vedic", "jyotish", "astrology", "path", "awareness", "purpose"},
         "Философский слой: у Вед и мудрости Востока цель — не точное предсказание, а ясность о выборе: различай изменяемое и неизменное, действуй по дхарме, не привязывайся к плодам. Прогноз — инструмент, а не приговор. Честная работа с судьбой — это работа с качеством решений, а не поиск гарантированных дат.",
         "Philosophical layer: the goal of the Vedas and Eastern wisdom is not precise prediction but clarity about choice: distinguish changeable from fixed, act in dharma, do not cling to results. A forecast is a tool, not a verdict. Honest work with fate is work on the quality of decisions, not the search for guaranteed dates."},
    };
    return v;
}

std::vector<Domain> detect(const std::string& question, const std::string& lang) {
    const std::string low = lower_ru(question);
    std::vector<Domain> hits;
    std::unordered_set<int> seen;
    for (const auto& m : modules()) {
        const bool ok = (lang == "ru")
            ? contains_any(low, m.keywords_ru)
            : contains_any(low, m.keywords_en);
        if (ok && !seen.count(static_cast<int>(m.domain))) {
            seen.insert(static_cast<int>(m.domain));
            hits.push_back(m.domain);
        }
    }
    return hits;
}

std::string theory_block(const std::string& question, const std::string& lang) {
    const auto hits = detect(question, lang);
    if (hits.empty()) return "";

    std::string s;
    if (lang == "ru")
        s = "\n\nАНАЛИТИЧЕСКАЯ ТЕОРИЯ (рамки мышления, а НЕ проверенные факты):";
    else
        s = "\n\nANALYTICAL THEORY (thinking frameworks, NOT verified facts):";
    for (const auto& d : hits) {
        for (const auto& m : modules()) {
            if (m.domain == d) {
                s += "\n- " + (lang == "ru" ? m.ru : m.en);
                break;
            }
        }
    }
    return s;
}

} // namespace jyotish::theory