#include <jyotish/wisdom.hpp>
#include <algorithm>
#include <cstdint>

namespace jyotish::wisdom {

namespace {

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace

std::string theme_for_area(const std::string& area) {
    static const std::vector<std::pair<std::string, std::string>> m = {
        {"general", "equanimity"},
        {"career", "karma"},
        {"love", "devotion"},
        {"health", "equanimity"},
        {"family", "duty"},
        {"spirituality", "reincarnation"},
    };
    for (const auto& [a, t] : m) if (a == area) return t;
    return "equanimity";
}

const std::vector<Item>& items() {
    static const std::vector<Item> v = {
        {Kind::Gita, "karma",
         "«Ты имеешь право на действие, но не на его плоды. Пусть плоды действия не будут твоей целью, но и бездействию не предавайся» (Бхагавад-гита 2.47)",
         "\"You have a right to action, but never to its fruits. Let not the fruits of action be your motive, nor give up action\" (Bhagavad Gita 2.47)"},
        {Kind::Gita, "equanimity",
         "«С одинаковым чувством встречай успех и неудачу, приобретение и потерю — пребывай в равновесии ума, ибо йога есть уравновешенность» (Бхагавад-гита 2.48)",
         "\"Perform action with an equal mind in success and failure, gain and loss — equanimity is yoga\" (Bhagavad Gita 2.48)"},
        {Kind::Gita, "impermanence",
         "«Соприкасаясь с внешними ощущениями, мудрец не волнуется — изменившемуся телу приходит старость и смерть, а душа не умирает» (Бхагавад-гита 2.13–2.20)",
         "\"The wise are unmoved by contact with the senses — old age and death come to the body, but the soul never dies\" (Bhagavad Gita 2.13-2.20)"},
        {Kind::Gita, "duty",
         "«Лучше исполнять свой долг, пусть и несовершенно, чем чужой — совершенно» (Бхагавад-гита 3.35)",
         "\"It is better to do your own duty, however imperfectly, than another's duty perfectly\" (Bhagavad Gita 3.35)"},
        {Kind::Gita, "purpose",
         "«Всякий раз, когда на земле убывает праведность и растёт неправедность, Я нисхожу Сам» (Бхагавад-гита 4.7)",
         "\"Whenever righteousness declines and unrighteousness prevails, I manifest Myself\" (Bhagavad Gita 4.7)"},
        {Kind::Gita, "surrender",
         "«Оставив все обязанности, приди ко Мне как к единственному прибежищу, и Я освобожу тебя от всех грехов — не скорби» (Бхагавад-гита 18.66)",
         "\"Abandon all duties and come to Me as your only refuge — I shall free you from all sins, grieve not\" (Bhagavad Gita 18.66)"},
        {Kind::Gita, "focus",
         "«Как реки, стекая в океан, хранящий покой, не нарушают его — так входят желания в того, кто достиг покоя» (Бхагавад-гита 2.70)",
         "\"As rivers flow into the calm ocean without disturbing it, so desires enter one who has attained peace\" (Bhagavad Gita 2.70)"},
        {Kind::Gita, "transformation",
         "«Возвышай себя собственным усилием, не унижай себя — ум может быть и другом души, и врагом её» (Бхагавад-гита 6.5)",
         "\"Elevate yourself by your own effort, do not degrade yourself — the mind can be both the soul's friend and its enemy\" (Bhagavad Gita 6.5)"},
        {Kind::Parable, "equanimity",
         "Притча о колеснице (Катха-упанишада): тело — колесница, живущее в нём «я» — седок, разум — возничий, ум — вожжи, чувства — кони. Если возничий мудр, а кони послушны, седок доезжает до цели царства. Если же возничий неопытен, кони уносят колесницу в пропасть. Веди колесницу ума осознанно — и цель будет достигнута.",
         "The chariot parable (Katha Upanishad): the body is the chariot, the self within is the rider, reason the charioteer, the mind the reins, the senses the horses. A wise charioteer and obedient horses bring the rider to the goal; an unskilled one drives the chariot into the abyss. Steer your mind with awareness — and you will reach the goal."},
        {Kind::Parable, "transformation",
         "Притча о двух птицах (Мундака-упанишада): на одном дереве сидят две птицы. Одна клюёт сладкие и горькие плоды — она страдает и радуется. Другая лишь спокойно созерцает. Плоды — события жизни, а спокойная птица — свидетель, джива, осознавшая свою высшую природу. Когда человек перестаёт отождествляться с событиями и помнит о свидетеле в себе — жизнь обретает глубину.",
         "The two-bird parable (Mundaka Upanishad): two birds sit on one tree. One eats sweet and bitter fruits, suffering and rejoicing. The other calmly watches. The fruits are life's events, the tranquil bird is the seer, the jiva aware of its higher nature. When you stop identifying with events and remember the witness within, life gains depth."},
        {Kind::Parable, "impermanence",
         "Притча-наблюдение: человек увидел, как в море впадает река с мутной водой, и огорчился. Река сказала: «Не смотри на мою рябь — во мне течёт та же вечная вода». Так и судьба сменяет обстоятельства, но глубина человека неизменна.",
         "An observation parable: a man saw a muddy river flowing into the sea and grieved. The river said, \"Do not look at my ripples — the same eternal water flows within me.\" Likewise fate changes circumstances, but a person's depth remains."},
        {Kind::Parable, "karma",
         "Притча о садовнике: ростку нужно не оглядываться на чужой сад, а пускать корни у себя. Какой бы ни был сезон, корни решают всё. Поливай свои поступки добром — плоды появятся в свой срок, не захваченные силой.",
         "The gardener parable: a seedling must not envy another garden, but take root in its own soil. Whatever the season, the roots decide everything. Water your deeds with goodness and fruit will come in its own time, not snatched by force."},
        {Kind::Story, "devotion",
         "История царей-преданных: в IX веке в Керале жил царь Кулашекхара Азвар, один из двенадцати святых-альваров. Он оставил трон и посвятил жизнь воспеванию Говинды-Мукунды, сочинив «Мукунда-малу» — гирлянду стихов-гимнов, которые поются и поныне.",
         "Story of devotional kings: in 9th-century Kerala lived king Kulasekhara Azhvar, one of the twelve saint-poets (Azhvars). He left his throne and devoted his life to singing of Govinda-Mukunda, composing the \"Mukunda-mala\" — a garland of hymns still sung today."},
        {Kind::Story, "devotion",
         "История Джорджа Харрисона: в 1973 году битл подарил вайшнавам свою усадьбу в Уотфорде под Лондоном — она стала храмом Бхактиведанта-Мэнор, одним из самых посещаемых в Европе. Прабхупада сказал о нём: «Он дал приют Кришне — и Кришна даст приют ему». В 1969-м Харрисон спродюсировал сингл «Hare Krishna Mantra» (Битлз-студия Apple), ставший хитом в Европе; по данным Джона Уинна, на ударных той записи, скорее всего, играл Ринго Старр.",
         "Story of George Harrison: in 1973 the Beatle gifted his estate in Watford, near London, to the Vaishnavas — it became Bhaktivedanta Manor temple, one of the most visited in Europe. Srila Prabhupada said of him: \"He gave Krishna a home — Krishna will give him a home.\" In 1969 Harrison produced the single \"Hare Krishna Mantra\" (Apple Studios), a European hit; per John Winn, Ringo Starr likely played drums on that recording."},
        {Kind::Story, "humility",
         "История Рассела Брэнда: известный комик и актёр годами посещает храм Бхактиведанта-Мэнор — скандирует мантры, участвует в киртанах, получил в дар «Шримад-Бхагаватам». Он рассказывал, что начал повторять «Харе Кришна» как обращение к высшему и находит в этом опору и покой.",
         "Story of Russell Brand: the famous comedian and actor has visited Bhaktivedanta Manor for years — chanting mantras, joining kirtans, given a Shrimad Bhagavatam. He said he began chanting \"Hare Krishna\" as a call to the higher, finding support and peace in it."},
        {Kind::Story, "humility",
         "История Джулии Робертс: голливудская звезда называет себя практикующей индуисткой — посещает храм «петь, молиться и праздновать», а интерес начался с фотографии святого Неим-Кароли Бабы. Своим детям она выбрала имена Махалакшми, Кришна-Баларам и Ганеша, а свою продюсерскую компанию назвала Red Om Films.",
         "Story of Julia Roberts: the Hollywood star calls herself a practicing Hindu — visiting temples \"to sing, pray and celebrate\", her interest sparked by a photo of saint Neem Karoli Baba. She chose the names MahaLakshmi, Krishna-Balarama and Ganesha for her children, and named her production company Red Om Films."},
        {Kind::Story, "perseverance",
         "История о царе Бхартрихари: легендарный правитель Уджайна, по преданию, пресытившись дворцовой жизнью, оставил престол и стал отшельником. Его стихи о мудрости и разлуке с бренным до сих пор изучают. Даже самый сильный может сменить роль — если этого требует внутренний зов.",
         "Story of king Bhartrihari: the legendary ruler of Ujjain, according to tradition, weary of palace life, left the throne and became a hermit. His verses on wisdom and the transience of life are still studied. Even the strongest can change roles — if an inner call demands it."},
        {Kind::Song, "curiosity",
         "Цитата из песни «Гадалка» (муз. М. Дунаевского, сл. Л. Дербенёва, к/ф «Ах, водевиль, водевиль!», 1979): «Устроены так люди — желают знать, желают знать, желают знать, что будет». Настоящий смысл гадания — не подчиниться прогнозу, а заглянуть в себя.",
         "Quote from the song \"Gadalka (The Fortune-Teller)\" (music M. Dunaevsky, lyrics L. Derbenev, film \"Ah, Vaudeville, Vaudeville!\", 1979): \"People are made this way — they want to know, they want to know what will be.\" The true meaning of divination is not to obey a forecast but to look into yourself."},
        {Kind::Song, "reincarnation",
         "Владимир Высоцкий, «Песенка о переселении душ» (1969): «Хорошую религию придумали индусы: что мы, отдав концы, не умираем насовсем». И как предупреждение: «Если туп, как дерево, — родишься баобабом». Судьба не наказание, а зеркало наших привычек.",
         "Vladimir Vysotsky, \"Song about the Migration of Souls\" (1969): \"The Hindus invented a fine religion: that we, when we pass on, do not die for good.\" And as a warning: \"If you are dull as wood — you will be reborn a baobab.\" Fate is not a punishment but a mirror of our habits."},
        {Kind::Song, "fate",
         "Из песни «Гадалка» (Дунаевский/Дербенёв): «Счастье в жизни предскажет гаданье и нежданный удар роковой». У пророчества две стороны — что сбудется, во многом зависит от самого человека и его выбора.",
         "From the song \"Gadalka (The Fortune-Teller)\" (Dunaevsky/Derbenev): \"Fortune-telling foretells happiness in life and an unexpected fatal blow.\" A prophecy has two sides — much of what comes true depends on the person and their choices."},
    };
    return v;
}

std::string build_wisdom_block(const std::vector<std::string>& areas,
                               const std::string& seed,
                               const std::string& lang) {
    std::string area = areas.empty() ? "general" : areas[0];
    std::string theme = theme_for_area(area);

    const auto& all = items();
    auto kind = [&all](Kind k) {
        std::vector<Item> out;
        for (const auto& it : all) if (it.kind == k) out.push_back(it);
        return out;
    };

    std::string header;
    if (lang == "en") {
        header = ("Inspiration from the Vedic tradition (choose 1-2 fitting items and weave "
                  "them naturally into the answer, referencing the source):");
    } else {
        header = ("Вдохновение из ведической традиции (выбери уместные 1–2 элемента и вплети их "
                  "в ответ органично, со ссылкой на источник):");
    }

    std::vector<std::string> lines = {header};
    auto pick_ = [&lines, &seed, &theme, &lang](const std::vector<Item>& pool, const std::string& kind_key) -> void {
        if (pool.empty()) return;
        const Item* chosen = nullptr;
        // Prefer items matching the theme; fall back to deterministic pick.
        std::vector<const Item*> themed;
        for (const auto& it : pool) if (it.theme == theme) themed.push_back(&it);
        if (!themed.empty()) {
            chosen = themed[fnv1a(seed + ":q2:" + theme + kind_key) % themed.size()];
        } else {
            chosen = &pool[fnv1a(seed + ":" + kind_key + ":" + theme) % pool.size()];
        }
        lines.push_back("- " + (lang == "en" ? chosen->en : chosen->ru));
    };

    pick_(kind(Kind::Gita), "q");
    pick_(kind(Kind::Parable), "p");
    pick_(kind(Kind::Story), "s");
    pick_(kind(Kind::Song), "h");

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

} // namespace jyotish::wisdom