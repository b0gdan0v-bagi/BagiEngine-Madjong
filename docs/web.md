# The game in a browser

The game also builds as a web page (Emscripten, WebGL). The SDK is installed once into the submodule (about 2 GB); use Python 3.14 for it, the Homebrew 3.15 and 3.12 give an empty `platform.mac_ver()` on this Mac:

```bash
python3.14 engine/CI/scripts/setup_emsdk.py
cmake --preset web-release                    # threaded; output in build/web-release/bin
cmake --build --preset web-release --target BagiMahjong
python3 engine/tools/web/package.py build/web-release/bin dist --game BagiMahjong --app web/app.json   # dist/ is git-ignored
python3 engine/tools/web/serve.py --root dist                                  # http://localhost:8080
```

Open `localhost` or an https address: the threaded page needs cross-origin isolation, which a plain `http://` host never has. The single-thread page needs no headers: the preset `web-release-single-thread`, `package.py build/web-release-single-thread/bin dist-single --game BagiMahjong --app web/app.json --single-thread`, `serve.py --root dist-single`. In a worker folder, which has no SDK of its own, the same presets exist as `-local` ones in the git-ignored `CMakeUserPresets.json`.

## Browser smoke

`tools/web_smoke.py` (the engine's smoke, then `Start` and a tile click on a 1280x720 page) needs Playwright in a venv:

```bash
python3.14 -m venv build/web-venv && build/web-venv/bin/python -m pip install playwright
build/web-venv/bin/python -m playwright install chromium                       # or --browser-channel chrome with the installed Chrome
build/web-venv/bin/python tools/web_smoke.py --dist dist --expect-console "Initialized with \d+ worker threads"
build/web-venv/bin/python tools/web_smoke.py --dist dist-single --expect-console "single-thread mode"
```

## The web-like desktop build

Without a browser it tests the same code paths: `cmake -S . -B build/webcheck -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DENGINE_WITH_VULKAN=OFF -DENGINE_WITH_REMOTE_CONTROL=OFF -DENGINE_STATIC_SDL=ON -DENGINE_SINGLE_THREAD=ON` (and `OFF`), then build `BagiMahjong MahjongTests` and `ctest --test-dir build/webcheck`. The output lands in `build/webcheck/bin/`, apart from the presets.

## The web app

`web/app.json` names the page's home-screen app (name, any orientation, the felt colour, the icons of `web/icons/`, made by `python3 tools/make_app_icons.py`); `build_and_deploy.py` picks it up by itself. On a phone the first tap asks for fullscreen and locks no orientation: a phone held upright gets the portrait table (`engine/tools/web/CLAUDE.md`).

## What differs in the browser

No saves and no `Continue` (the page's file system is memory, lost on reload; the seat choice is not remembered either), no match log, no control API, no bot arena. Persistence needs IDBFS, which is engine work and not done. Online play is on when `config/MahjongOnlineConfig.xml` names a `wss://` server in `webServer`: the `Online` button of `Setup` is there only then (below).

## Deploy to Railway

Written down, not run; the owner runs it. After `railway login` and `railway link` in a service made once (`railway add --service <name>`):

```bash
python3 engine/tools/web/build_and_deploy.py --source-dir . --game BagiMahjong --service <name> --deploy
```

`--dry-run` prints the commands without running them. It builds with the SDK in `engine/external/emsdk`, so it is not possible in a worker folder. The service `bagi-mahjong` takes its Railway settings (builder Dockerfile, health check `/healthz`, restart on failure, limits) from `web/.railway/railway.ts`, an IaC partial that owns only it: after a change, `cd web && npm install && railway config plan`, then `railway config apply`.

**Who comes in** (`engine/tools/web/ACCESS.md`; the variables are the service's, never in git or the partial). The first visit lands on `/signin`: "Sign in with Google" or "Play without signing in", both lead to the same game. Sign-in gives identity, not a closed door: any Google account is let in (the OAuth app is published, so not only test users), and the shared Basic password is not used for `bagi-mahjong` any more (it stays in `serve.py` for other services). **What is kept:** a match a signed-in tester sends is stored with their Google email (from Google's checked token, never from the page); a guest's match has no email; nothing else about a person is stored, matches go after 90 days, and a request to delete is answered by email or match id (`DELETE /matches/<id>` with `MATCH_COLLECT_TOKEN`). The owner's Google Cloud and `railway variables` steps are the checklist at the end of `ACCESS.md`. More providers later are a row of `PROVIDERS` in `serve.py` and two variables each.

Two IaC partials share the project and neither touches the other: `web/.railway` is `mahjong-web` (the service `bagi-mahjong`), `tools/server/.railway` is `mahjong-rooms` (the service `bagi-mahjong-rooms`, below). Each owns only its service; variables are never in them.

## The room server on Railway

Written down, not run; the owner runs it. `BagiMahjongServer` (`tools/server/main.cpp`) keeps the online rooms; the hosted page reaches it over `wss://` at the address `webServer` in `config/MahjongOnlineConfig.xml`. The order matters, because the page carries the address:

1. `railway add --service bagi-mahjong-rooms`, then generate its domain (the dashboard, or `railway domain --service bagi-mahjong-rooms`). Compare it with `webServer` of `config/MahjongOnlineConfig.xml` (shipped as `wss://bagi-mahjong-rooms-production.up.railway.app`, a guess from the pattern of the web service); if it differs, edit the file (the page needs the web build again, step 6).
2. The settings: `cd tools/server && npm install && railway config plan`, then `railway config apply` (commit `package-lock.json` if npm changed it). `plan` is the first check of `tools/server/.railway/railway.ts`, the Amsterdam region id above all.
3. The variables, from the root: `railway variables --service bagi-mahjong-rooms --set BAGI_ROOMS_ORIGINS=https://bagi-mahjong-production.up.railway.app`. Without it browsers are refused (programs without an `Origin` are let in). Optional: `--set BAGI_ROOMS_SHUTDOWN_GRACE_MS=<ms>` together with `RAILWAY_DEPLOYMENT_DRAINING_SECONDS` for a longer drain on a redeploy; the other `BAGI_ROOMS_*` variables are in `engine/README.md`, "Hosting the room server".
4. The deploy of the server (needs Docker and the Railway CLI, logged in, the project linked):

```bash
python3 engine/tools/rooms/build_and_deploy.py --source-dir . --server-target BagiMahjongServer --deploy --service bagi-mahjong-rooms
```

   The preset is the engine's `linux-server-release`, which the game's presets include. It builds for `linux/amd64` in Docker (slow in emulation on an arm Mac), stages the executable with a runtime-only `Dockerfile` and uploads a few MB; Railway compiles nothing. `--dry-run` prints the commands. This Linux build of `BagiMahjongServer` has not been run yet: the first deploy is its first compile.
5. Check `https://<domain>/healthz` answers `ok`.
6. The web page again, with the command of "Deploy to Railway", so it carries the config.

**One replica.** The rooms live in the memory of the process: a second replica would split them in two servers that do not know each other's codes, so the partial pins one. A redeploy or a restart loses every room; a client that reconnects is told `room_gone`.

**The live check** (the owner, in two browsers or a browser and a phone):
- the `Online` button is on `Setup`; create a room, a second device joins by the code;
- take seats, the host starts, a hand is played to its end and the ready check passes;
- redeploy the server while connected: the clients get `room_gone`;
- an `Origin` that is not in `BAGI_ROOMS_ORIGINS` is refused.

**The desktop** reads `BAGI_MAHJONG_SERVER` (else `ws://localhost:8080`) and ignores `MahjongOnlineConfig`. It speaks `wss://` through the system TLS on macOS and Windows, so `BAGI_MAHJONG_SERVER=wss://<domain>` joins the hosted server; Linux has no TLS backend yet and refuses `wss://`.
