# Деплой «Жрицы 3000» на VDS — операционная памятка

## Схема
- **VDS** (96.126.129.239, Ubuntu 24.04, 1 vCPU/961 MiB): C++-сервер + фронт + SQLite + админка.
  Лёгкий, весь «мозг» — LLM — через обратный SSH-туннель на dev-машину (там Ollama со всеми моделями).
- **Dev-машина**: systemd юзер-юнит `oracle-tunnel.service` держит `-R 11434:127.0.0.1:11434`, чтобы на VDS `127.0.0.1:11434` = Ollama dev'а.
- Публичный доступ: **только `http://96.126.129.239` (:80)** через nginx → reverse proxy → `127.0.0.1:8000`. Порт 8000 наружу закрыт (bind 127.0.0.1).

## Как управлять

На VDS (по SSH: `ssh root@96.126.129.239` — **только по ключу**, парольная авторизация отключена):
```bash
systemctl status oracle        # статус сервера
systemctl restart oracle       # перезапуск после изменения кода/конфига
journalctl -u oracle -f        # логи (healthz, новости, запросы)
cat /etc/oracle.env            # конфигурация (токен, OLLAMA_BASE_URL, порт)
```
На dev-машине:
```bash
systemctl --user status oracle-tunnel   # туннель
systemctl --user restart oracle-tunnel
```
Бэкапы БД: каждый день 03:30 в `/root/oracle-backups/` (хранятся 14 дней).
Восстановление: `sqlite3 oracle_store.db` с копии.

## Обновление кода на VDS
```bash
cd /opt/oracle
# пересобрать бинарник
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DSWISSEPH_LIB=/usr/lib/x86_64-linux-gnu/libswe.so > cmake.log 2>&1
cmake --build build --target jyotish_server -j2 > build.log 2>&1
# обновить фронт (папку frontend/dist скопировать с dev), данные (cities.tsv/famous.json)
systemctl restart oracle
```

## Экономика (деньги пользователя)
- 5 бесплатных вопросов на сессию (`client_id` из localStorage).
- Далее монеты: пакет 10 монет / 100 ₽. Кнопка «Положить монетку» → переводишь по СБП
  на `<TOPUP-PHONE-FROM-ENV>` (СБП-сервис оператора, телефон в `JYOTISH_TOPUP_PHONE`), затем нажимаешь
  «Я перевёл — жду пополнения» (заявка в `/api/payments`), жрица подтверждает в `/admin.html`.
  Если задать `JYOTISH_TOPUP_URL` (ссылка от сервиса СБП-приёма) — кнопка будет вести туда.
- Онбординг (сбор даты/места, подтверждение, фидбек) — бесплатный; платный оборот —
  только сам гороскоп после подтверждения рождения.
- Ответы на вопросы, которые оракул задаёт сам («Как вас зовут?», «Верно?», «Был ли
  ответ полезен?», любой вопрос жрицы в конце реплики) — тоже бесплатные: монета/слот
  списывается только с заявленного «собственного» вопроса пользователя (когда последняя
  реплика оракула не была вопросом). Лимит «свободных» ходов (5) на онбординг/фидбек не тратится.
- При сбоях сети/сервера пользователю показывается «У оракула очередь — вы N-й».

## HTTPS (сделать когда купишь домен)
1. Купить домен (Porkbun/Namecheap: `.xyz`/`.top` ~150–250 ₽/год; или `.ru` на reg.ru ~200–300 ₽/год).
2. NS/A-запись на 96.126.129.239.
3. `apt install certbot python3-certbot-nginx` (nginx уже стоит и проксирует на 127.0.0.1:8000).
4. `certbot --nginx -d твой-домен` → включит HTTPS автоматически + редирект с :80.
5. порт 8000 снаружи уже закрыт (bind 127.0.0.1) — ничего менять не надо.

## Безопасность (сделано 25.09.2026)
- SSH: только по ключу (`PermitRootLogin prohibit-password`, `PasswordAuthentication no`,
  `MaxAuthTries 3`, `LoginGraceTime 30`, `X11Forwarding no`; `AllowTcpForwarding yes` — нужен для туннеля).
- `fail2ban` active + `unattended-upgrades` (обновления каждый день, автоперезагрузка в 04:00 при необходимости).
- UFW: default deny incoming, разрешены `OpenSSH` (22), `Nginx HTTP` (80), `443/udp` (hysteria VPN).
  ВАЖНО: `ufw --force reset` стирает правила от hysteria — после сброса возвращать `ufw allow 443/udp`.
- Админка: `/admin.html` → вход по логину `albadmin` + пароль (его же сервер требует как
  `JYOTISH_ADMIN_TOKEN`; `/api/admin/login` проверяет `JYOTISH_ADMIN_USER`/токен).
  Смена пароля: `sed -i 's|^JYOTISH_ADMIN_TOKEN=.*|JYOTISH_ADMIN_TOKEN=<новый>|' /etc/oracle.env && systemctl restart oracle`.
- Пока работает на HTTP по :80 — до покупки домена не «пиарить» адрес с платёжкой.

## Прочее
- **Hysteria 2 VPN** уже стоит (`/usr/local/bin/hysteria`, `/etc/hysteria/config.yaml`, UDP :443).
  Пароль hysteria НЕ менять (по решению владельца) — это секрет доступа к VDS; он есть только
  в диалогах и в `/etc/hysteria/config.yaml` (mode 600). Не выносить в чаты/файлы без надобности.
- **Яндекс Метрика**: ID счётчика задаётся в `frontend/index.html` (`var YM_ID = 0;` → реальный ID),
  затем пересборка фронта. Директ — отдельная платная реклама, кампанию запускать после домена/HTTPS.