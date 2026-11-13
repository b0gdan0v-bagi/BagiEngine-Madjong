# Вход через Google на `bagi-mahjong`: инструкция для выкладки

Пошаговая инструкция для владельца. Справочник по серверу: [engine/tools/web/ACCESS.md](../engine/tools/web/ACCESS.md). Кто входит и что хранится: [web.md](web.md), «Who comes in».

Что получится: при первом заходе тестер видит `/signin` с кнопками «Войти через Google» и «Играть без входа». Оба пути ведут в одну и ту же игру. Матч вошедшего тестера хранится вместе с его Google email, матч гостя хранится без email.

> **Порядок важен.** Сервер не запустится, если одновременно заданы пароль (`BASIC_AUTH_*`) и вход через провайдера. Пароль убирается в том же заходе, в котором добавляются переменные Google, до перевыкладки.

## 1. Адрес сервиса

```bash
railway domain --service bagi-mahjong
```

Ниже этот адрес обозначен как `<домен>`, например `bagi-mahjong-production.up.railway.app`. Нужен именно тот адрес, по которому тестеры открывают игру; если подключён свой домен, берите его.

## 2. Google Cloud: экран согласия

1. Откройте https://console.cloud.google.com/ → Select project → **New project**, например `BagiMahjong`.
2. Перейдите в **APIs & Services → OAuth consent screen**; в новом интерфейсе это **Google Auth Platform → Branding / Audience**.
   - User type: **External**.
   - App name: его тестер увидит в окне Google, например `BagiMahjong`.
   - Support email и Developer contact: ваша почта.
   - **Branding → App domain** (без этих полей Google не даёт опубликовать приложение: «Valid app name, support email, homepage URL and privacy policy URL are required»):
     - Application home page: `https://<домен>/`
     - Application privacy policy link: `https://<домен>/privacy` — эту страницу отдаёт сам сервер, без входа (сервер должен быть уже выложен с новым `serve.py`, шаг 7);
     - Application terms of service link: можно оставить пустым;
     - **Authorized domains**: `<домен>` целиком (например `bagi-mahjong-production.up.railway.app`). Если Google его не примет, остаётся подключить к сервису свой домен и указать его.
   - Логотип не загружайте: с ним Google требует проверку бренда.
3. **Scopes** (в новом интерфейсе **Data Access**): добавьте только `openid` и `.../auth/userinfo.email`. Ничего больше не добавляйте: эти два не считаются чувствительными, поэтому проверка приложения в Google не нужна.
4. **Audience → Publish app**, подтвердите. Если Google снова пишет, что не хватает полей, проверьте Branding из пункта 2 и что `https://<домен>/privacy` открывается в браузере. Без этого шага войти смогут только тестовые пользователи (до 100), которых вы впишете руками.

## 3. Google Cloud: OAuth-клиент

1. **APIs & Services → Credentials → Create credentials → OAuth client ID.**
2. Application type: **Web application**, имя любое.
3. **Authorized JavaScript origins** оставьте пустым: сервер обходится без скриптов Google на странице.
4. **Authorized redirect URIs**: ровно `https://<домен>/auth/google/callback`. Обязательно `https`, без слэша в конце и без лишнего пути. Если адрес не совпадёт буква в букву, Google покажет ошибку `redirect_uri_mismatch`.
5. Нажмите **Create** и скопируйте **Client ID** и **Client secret**. Секрет никуда не коммитьте и не пересылайте.

## 4. Два секрета

```bash
python3 -c "import secrets; print(secrets.token_urlsafe(48))"   # SESSION_SECRET
python3 -c "import secrets; print(secrets.token_urlsafe(48))"   # MATCH_COLLECT_TOKEN
```

- `SESSION_SECRET` подписывает cookie входа. Если его сменить, все тестеры разлогинятся.
- `MATCH_COLLECT_TOKEN` — ваш ключ, чтобы забирать и удалять логи матчей. Сохраните его в менеджере паролей.

> **Порядок с Google и страницей политики.** Страница `/privacy` появится, когда на `bagi-mahjong` будет выложена новая версия (шаг 7). Удобный порядок: шаги 1, 3–7 (Google пока в режиме Testing — добавьте себя в Test users, чтобы проверить вход), затем Branding и Publish app из шага 2.

## 5. Хранилище матчей (volume)

Если volume уже подключён, пропустите этот шаг.

```bash
railway volume add --service bagi-mahjong --mount-path /data
```

Volume монтируется с владельцем root, а образ работает от пользователя `nobody`. Поэтому нужна переменная `RAILWAY_RUN_UID=0` (шаг 6), иначе сервер остановится с ошибкой «match store is not writable». С этой переменной процесс работает от root. На Railway это пока не проверено.

## 6. Переменные сервиса

```bash
railway variables --service bagi-mahjong \
  --set GOOGLE_CLIENT_ID=<client id> \
  --set GOOGLE_CLIENT_SECRET=<client secret> \
  --set PUBLIC_URL=https://<домен> \
  --set SESSION_SECRET=<первый секрет> \
  --set MATCH_COLLECT_TOKEN=<второй секрет> \
  --set PRIVACY_CONTACT=<ваша почта для запросов об удалении> \
  --set MATCH_STORE=/data/matches \
  --set MATCH_LOG_VERSIONS=1-6 \
  --set MATCH_FORWARDED_FOR=1 \
  --set RAILWAY_RUN_UID=0
```

Затем **удалите `BASIC_AUTH_USER` и `BASIC_AUTH_PASSWORD`**: в панели Railway откройте сервис `bagi-mahjong` → Variables → «⋮» → Delete. Удобнее делать всё в панели: Railway собирает изменения в один набор и применяет их кнопкой Deploy.

- `PUBLIC_URL` указывается без слэша в конце и совпадает с адресом из шага 3.
- `MATCH_LOG_VERSIONS=1-6`: нынешний формат лога матча — 6 (`kMatchLogFormatVersion` в `src/Mahjong/MatchLogJson.h`). С диапазоном `1-5` каждый новый матч отклоняется с ошибкой 400. Когда формат вырастет, расширьте диапазон.
- Необязательные переменные:
  - `SESSION_DAYS` — сколько дней действует вход, по умолчанию 30;
  - `SIGNIN_GUESTS=0` — убрать кнопку «Играть без входа».

## 7. Сборка и выкладка

Из корня игры, на свежем `main`:

```bash
python3 engine/tools/web/build_and_deploy.py --source-dir . --game BagiMahjong --service bagi-mahjong --deploy
```

С `--dry-run` команда только покажет, что будет выполнено. Если менялись правила или протокол комнат, пересоберите и сервер комнат `bagi-mahjong-rooms` (см. [web.md](web.md), раздел «The room server on Railway»), иначе клиент и сервер разойдутся.

## 8. Проверка

1. Запустите `railway logs --service bagi-mahjong`. В стартовой строке должно быть:
   `access: sign-in with google or as a guest (https://<домен>); match store: /data/matches`.
2. Откройте `https://<домен>` в приватном окне. Должна открыться `/signin` с двумя кнопками.
3. Войдите через Google. После выбора аккаунта откроется игра. Если снова открыть `https://<домен>/signin`, там будет «Вы вошли как …» и кнопка «Выйти».
4. В другом приватном окне нажмите «Играть без входа». Игра тоже должна открыться.
5. Проверьте логи матчей. Сыграйте в браузере раздачу, приняв согласие на отправку матчей (его можно поменять в Options), затем запросите список:

   ```bash
   curl -H "Authorization: Bearer <MATCH_COLLECT_TOKEN>" https://<домен>/matches/
   ```

   У матчей вошедших тестеров в списке будет поле `"tester": {"provider": "google", "email": "..."}`. Скачать логи для арены: `tools/collect_logs.py` (см. [tester-match-logs.md](tester-match-logs.md)), токен берётся из `MATCH_COLLECT_TOKEN`.

## Если что-то пошло не так

| Что видно | Причина и что делать |
|---|---|
| Сервер упал: `exclude each other` | Остался `BASIC_AUTH_*`: удалите его |
| Сервер упал: `PUBLIC_URL must be ...` | В адресе лишний путь, слэш или `http` |
| Сервер упал: `match store is not writable` | Нет `RAILWAY_RUN_UID=0` |
| Сервер упал: `... must be set together` | Задан только один из `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` |
| Сервер упал: `SESSION_SECRET` / `MATCH_COLLECT_TOKEN` ... `characters` | Секрет короче 32 символов: сгенерируйте заново (шаг 4) |
| Google: `redirect_uri_mismatch` | Адрес из шага 3 не совпадает с `PUBLIC_URL` + `/auth/google/callback` |
| Google: «Access blocked … app is in testing» | Приложение не опубликовано: нажмите Publish app (шаг 2.4) |
| После Google снова `/signin?failed=1` | Вход занял больше 10 минут или у почты в Google нет подтверждения: повторите вход |
| Всех разом выкинуло из игры | Сменился `SESSION_SECRET`: так задумано |

## Обслуживание

- **Удалить матчи тестера по его просьбе.** Найдите id в `/matches/` по email, затем выполните:
  `curl -X DELETE -H "Authorization: Bearer <MATCH_COLLECT_TOKEN>" https://<домен>/matches/<id>`.
- **Выкинуть всех.** Задайте новый `SESSION_SECRET` и перевыложите.
- **Токен сбора утёк.** Задайте новый `MATCH_COLLECT_TOKEN` и перевыложите.
- **Сменить секрет клиента Google.** Credentials → клиент → Reset secret, затем обновите `GOOGLE_CLIENT_SECRET`.
- **Другой провайдер** (Яндекс, Microsoft, Apple…):
  1. Добавьте строку в таблицу `PROVIDERS` в `engine/tools/web/serve.py`. Если провайдер не поддерживает OpenID Connect, нужен небольшой подкласс `Provider`.
  2. Создайте его клиент с адресом возврата `https://<домен>/auth/<имя>/callback`.
  3. Задайте две его переменные и перевыложите.
- **Проверить локально без Google:**
  `SIGNIN_FAKE_EMAIL=me@example.com python3 engine/tools/web/serve.py --root dist --host 127.0.0.1`.
  На Railway такой режим не включится.
