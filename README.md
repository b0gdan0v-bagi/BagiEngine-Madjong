# BagiEngine-Madjong

Mahjong on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine). The engine is the git submodule `engine/`, on the permanent branch `games/mahjong` of the engine repository. So far it is a skeleton: the game is registered and opens its scene, the rules are still to come.

## Get it

```bash
git clone --recurse-submodules <url> BagiEngine-Madjong
cd BagiEngine-Madjong
CI/setup_venv.sh          # Windows: CI\setup_venv.bat or .ps1; creates engine/.venv, needs uv
```

An existing clone: `git submodule update --init --recursive`.

**The identity of the commits is local and not versioned.** Every new clone sets it again, in the game and in the submodule (engine work is committed from inside `engine/`, which has its own config), with the email and the name this repository is committed with:

```bash
git config user.email "<email>"
git config user.name  "<name>"
git -C engine config user.email "<email>"
git -C engine config user.name  "<name>"

git config --show-origin user.email             # must show file:.git/config
git -C engine config --show-origin user.email   # must show file:.../.git/modules/engine/config
```

## Build and test

The presets are the engine's (`CMakePresets.json` includes `engine/CMakePresets.json`):

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"     # the reflection generator needs jinja2
cmake --preset macos-debug                    # list: cmake --list-presets
cmake --build --preset macos-debug
ctest --preset macos-debug                    # MahjongTests
build/ClangmacOS/BagiMahjong                  # the game; the executable folder depends on compiler and OS
```

## Layout

| Path | What |
|------|------|
| `src/Mahjong/` | the game: `MahjongGame` now, the rules (`Game/`) and the scene components (`Components/`) to come. `CLAUDE.md` describes it |
| `config/`, `assets/` | scene, application config; sprites to come |
| `tests/` | gtest cases |
| `todo/`, `TODO.md` | known problems and ideas live in the todo store (area `mahjong`), not in git (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiMahjong.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/mahjong`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
