# The engine submodule and the todo store

`engine/` is the BagiEngine as a git submodule on its permanent branch `games/mahjong` (`.gitmodules`), not `main`; the owner merges `main` into it when the game should get newer engine work. The game is the engine's proving ground, so engine work is done in this checkout, in `engine/`.

- **Style and contract** (game code too): the engine's `engine/CLAUDE.md` and the nested `CLAUDE.md` files under `engine/src/`; this repository's `CLAUDE.md` comes first for anything about Mahjong.
- **An engine change** is a branch `todo/<id>` of the engine repository cut from `games/mahjong`, tried live by building `BagiMahjong`. The vendored code (`engine/src/Modules/EABase/`, `EnTT/`, `fmt/`, ...: `todo-project.md`, "Zones nobody edits") and `engine/external/` stay untouched.
- **The pointer of `engine/`** is moved only by `todo-merge` (after the merge and the owner's yes, a commit of its own `Update engine: <title>`) or by the owner. While an engine branch is checked out, `git status` shows `engine` modified: expected.
- **Workers** (`../BagiEngine-Madjong.workers/w<k>`): never run `git submodule update` in one (`engine/tools/todo/CLAUDE.md`, "Workers").
- **The todo store** holds the entries, plans and reviews of both areas (`mahjong`, `engine`); nothing of it is in git, and the service mirrors it into Vikunja by itself (no push, no pull). CLI: `python3 engine/tools/todo/todo.py` from this root, credentials in the git-ignored `.env`; the store: `engine/tools/todo/CLAUDE.md`; the areas, workers and loop facts of this repository: `todo-project.md`. **Plans are never committed** (working copies in `~/.claude/plans/`).
- **Skills and agents** (`.claude/skills/*`, `.claude/agents/todo-*.md`) are symlinks into `engine/.claude/`: change them in the engine repository. What differs here is in `todo-project.md` (the todo areas) and `build-project.md` (build, format, includes).
