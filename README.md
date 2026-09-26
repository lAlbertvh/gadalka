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
| `JYOTISH_TOPUP_PHONE` | *(обязат. на сервере)* | телефон СБП для пополнения монет — задаётся только в env, в коде нет |
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

## Ревизия и отладка

**Карта кода: где чинить типичные ошибки**

| Симптом | Файл(ы) |
|---|---|
| Поведение по умолчанию, дефолты, настройки | `include/jyotish/config.hpp`, `src/core/config.cpp` (все `env_or(...)`) |
| База данных, схема, запросы | `src/core/store.cpp`, `include/jyotish/store.hpp` |
| HTTP-маршруты и хендлеры | `src/http/server.cpp` (ищи `Post("/api/...")`, `Get("/api/...")`) |
| Логика ответов оракула, лимит длины | `src/oracle/oracle.cpp` (+ `feedback.cpp`) |
| Разбор даты/имени/координат (онбординг) | `src/oracle/parsing.cpp`, `src/oracle/onboarding.cpp` |
| Геокодинг | `src/core/geocode.cpp`, `data/cities.tsv` |
| Язык и форматирование реплик | `src/core/i18n.cpp`, `include/jyotish/i18n.hpp` |
| Вызов Ollama | `src/ollama/client.cpp` |

**Рабочий цикл (локально, не трогая прод)**

```bash
cmake --build build -j$(nproc)        # пересборка
./build/jyotish_tests                 # быстрый прогон юнит-тестов
ctest --test-dir build                # то же через CTest

# отладочный сервер на своём порту и с временной БД — прод не задеваем:
env JYOTISH_PORT=8001 JYOTISH_STORE_PATH=/tmp/dbg.db JYOTISH_FEEDBACK_DIR=/tmp/dbg-fb \
    JYOTISH_ADMIN_TOKEN=tok JYOTISH_OLLAMA_BASE_URL=http://localhost:11434 \
    ./build/jyotish_server
curl http://127.0.0.1:8001/api/session   # проверить ответ
```

**Логи на сервере**

- `journalctl -u oracle --no-pager -n 200` — свежий лог сервиса
- `log_*`/`server.log` в рабочем каталоге (`/opt/oracle`), если включено
- `systemctl restart oracle` — перезапуск после замены бинарника
- Валидировать после деплоя: `curl -s http://127.0.0.1:8000/api/session` → HTTP 400 (это норма: ручка ждёт тело POST), `curl -s -o /dev/null -w '%{http_code}' http://<ip>/` → 200

**Обновление фронта (React/Vite → dist)**

```bash
cd frontend && npm run build          # соберёт dist/ (обязательно с public/*: admin.html, шрифты, иконка)
tar czf dist.tgz dist
# залить архив на VDS и заменить /opt/oracle/frontend/dist
```
Статика читается с диска без рестарта сервиса (`set_mount_point("./frontend/dist")`), замену делай через `mv dist dist-bak` перед выкладкой.

## Деплой на VDS

Операционная памятка — в [`deploy/README.md`](deploy/README.md): systemd-юниты, туннель
к Ollama, бэкапы БД, обновление кода и экономика.

## Фронтенд

Отдельный репозиторий (React/Vite); в проекте привязан локально по симлинку как
`frontend/`. Сборка: `cd frontend && npm run build` → `frontend/dist`.