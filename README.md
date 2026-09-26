# Жрица 3000 — ведический оракул (C++ бэкенд)

Онлайн-гадалка-автомат: точный ведический гороскоп по дате, времени и месту рождения.
Астрологический движок на **Swiss Ephemeris**, тексты генерирует **Ollama** (локальная LLM),
всё хранится в **SQLite**. Лёгкий одно-бинарник, поднимается даже на 1 vCPU VDS.

- Натальная карта: Лагна, Луна, ключевые планеты, дома
- Даши (Вимшоттари), транзиты, периоды планет
- Фото-анализ (ладонь/лицо) через мультимодальную модель
- Поиск по фактам в сети (grounding) + перепроверка источников цитат
- Оплата: «бесплатные вопросы» (5 на сессию) и монеты (10 монет / 100 ₽ по СБП)
- Код-кошелёк: короткий `XXX-XXX` восстанавливает аккаунт на другом устройстве
- Фидбек-датасет `«был ли ответ полезен?»` в `YYYY-MM-DD.jsonl` для будущего дообучения
- Обратная связь — кнопки 👍/👎 прямо в чате (никаких «напишите да/нет» в тексте реплик)
- Встроенная админка `/admin.html` (ручной режим, заявки на пополнение, дашборд)
- Лимит длины ответа: модель не разражается длиннее гороскопа (~1500 симв.)

## Архитектура

```
┌──────────┐   :80    ┌──────────────┐   :8000   ┌──────────────────────┐
│  nginx   │ ───────▶ │ jyotish_server│ ───────▶ │ SQLite (sessions,    │
│ (статич. │          │  (C++ бинарь) │          │  монеты, лог, фидбек)│
│  фронт)  │          └──────┬───────┘           └──────────────────────┘
│          │                 │
└──────────┘                 │ 127.0.0.1:11434 (Ollama)
                             ▼
                     ┌──────────────┐
                     │   Ollama     │  qwen2.5:7b-instruct (чат, фактчек)
                     │              │  qwen2.5vl:3b   (фото-анализ)
                     └──────────────┘
```

Оllama может быть на той же машине или на dev-машине через обратный SSH-туннель
(см. `deploy/README.md`).

## Стек

- C++20, CMake ≥ 3.20
- Swiss Ephemeris (`libswe`)
- SQLite3 (собственная thread-safe обёртка с мутексами, WAL)
- cpp-httplib (HTTP-сервер), nlohmann/json
- HTTP API одного бинарника: `/api/oracle`, `/api/chat`, `/api/chart`, `/api/dasha`,
  `/api/predictions`, `/api/transits`, `/api/period`, `/api/photo`, `/api/session`,
  `/api/wallet/*`, `/api/feedback`, `/api/payments`, `/api/manual`, `/api/admin/*`

## Сборка и тесты

Зависимости (Debian/Ubuntu):

```bash
apt install build-essential cmake libsqlite3-dev libswe-dev nlohmann-json3-dev
```

Для HTTP-сервера используется cpp-httplib (header-only) — скрипт `cmake/fetch_httplib.cmake`
подтянет его автоматически, либо укажите `CPPHTTPLIB_INCLUDE_DIR`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build            # юнит-тесты (GTest)
./build/jyotish_server            # старт
```

Данные для движка (`data/cities.tsv`, `data/famous.json`) — уже в репозитории;
без Swiss Ephemeris можно собрать с встроенным `mock` (параметры см. в CMakeLists).

## Запуск и переменные окружения

Все настройки — через окружение (никаких секретов в коде):

| Переменная | По умолчанию | Описание |
|---|---|---|
| `JYOTISH_HOST` / `JYOTISH_PORT` | `0.0.0.0` / `8000` | адрес HTTP-сервера |
| `JYOTISH_STORE_PATH` | `oracle_store.db` | файл SQLite |
| `JYOTISH_OLLAMA_BASE_URL` | `http://localhost:11434` | адрес Ollama |
| `JYOTISH_CHAT_MODEL` | `qwen2.5:7b-instruct` | модель для ответов |
| `JYOTISH_PHOTO_MODEL` | `qwen2.5vl:3b` | модель для фото-анализа |
| `JYOTISH_ADMIN_TOKEN` | пусто (выкл.) | токен админки (`/api/admin/*`) |
| `JYOTISH_ADMIN_USER` | `albadmin` | логин админки |
| `JYOTISH_TOPUP_PHONE` | `<TOPUP-PHONE-FROM-ENV>` | телефон СБП для пополнения монет |
| `JYOTISH_TOPUP_URL` | пусто | ссылка оплаты, если есть СБП-сервис |
| `JYOTISH_FEEDBACK_DIR` | `/mnt/oracle-data/feedback` | каталог JSONL-датасета |
| `JYOTISH_SESSION_TTL_DAYS` | `60` | неактивные сессии чистятся раз в неделю |
| `JYOTISH_MAX_REPLY_CHARS` | `1500` | жёсткий лимит длины ответа |
| `JYOTISH_WEB_SEARCH` | `1` | поиск фактов в сети (0 — выключить) |
| `JYOTISH_SEARCH_PROVIDER` | `duckduckgo` | `duckduckgo` · `searx` · `none` |

## Безопасность

- Пароли и токены задаются исключительно через окружение (`/etc/oracle.env` на сервере) —
  их **нет в этом репозитории**.
- Админка требует `JYOTISH_ADMIN_TOKEN` (пустой = API `/api/admin/*` отключён).
- SSH на сервер — только по ключу, UFW закрывает всё, кроме 22/80/443-udp.

## Деплой на VDS

Операционная памятка — в [`deploy/README.md`](deploy/README.md): systemd-юниты, туннель
к Ollama, бэкапы БД, обновление кода и экономика.

## Фронтенд

Отдельный репозиторий (React/Vite); в проекте привязан локально по симлинку как
`frontend/`. Сборка: `cd frontend && npm run build` → `frontend/dist`.