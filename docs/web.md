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

No saves and no `Continue` (the page's file system is memory, lost on reload; the seat choice is not remembered either), no match log, no control API, no bot arena. Persistence needs IDBFS, which is engine work and not done.

## Deploy to Railway

Written down, not run; the owner runs it. After `railway login` and `railway link` in a service made once (`railway add --service <name>`):

```bash
python3 engine/tools/web/build_and_deploy.py --source-dir . --game BagiMahjong --service <name> --deploy
```

`--dry-run` prints the commands without running them. It builds with the SDK in `engine/external/emsdk`, so it is not possible in a worker folder. The service `bagi-mahjong` takes its Railway settings (builder Dockerfile, health check `/healthz`, restart on failure, limits) from `web/.railway/railway.ts`, an IaC partial that owns only it: after a change, `cd web && npm install && railway config plan`, then `railway config apply`.
