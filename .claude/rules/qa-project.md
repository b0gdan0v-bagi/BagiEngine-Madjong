---
paths:
  - "tools/ui_shots.py"
  - "config/nodes/**/*"
  - "engine/tools/shots/**/*"
---

# The screenshots of this repository

Read by the architect (the `**Shots:**` line of a plan), the conductor of `todo-loop` and the reviewer (before / after pictures of a slice that changes how something looks; the tool and the agents' rules: `engine/tools/shots/CLAUDE.md`). The command is `tools/ui_shots.py` (`tools/CLAUDE.md`): headless, manual clock, seed fixed, so one build gives equal pictures.

## Which scenes a node file changes

`config/nodes/Mahjong<Name>.xml` and the scenes that show it; a `**Shots:**` line names the scenes of the nodes the slice changes (a change of a component or of `config/UIStyle` reaches every scene that shows what it draws).

| Node | Scenes |
|------|--------|
| `Table`, `SeatPlates`, `CenterPanel`, `TileHover`, `Hud`, `HudMenu`, `HintsButton` | all nine (the table is under every screen) |
| `Actions`, `ActionsCompact` | `claim-window` |
| `HandOver`, `HandOverAction` | `score-table` |
| `MatchOver` | `standings` |
| `Setup` | `setup`, and `online` (dimmed behind the panel) |
| `Consent` | none (the upload question exists only in a browser build) |
| `Options`, `Rules`, `RulesAction` | `options`, `rules` |
| `Handoff` | `handoff` |
| `Online`, `OnlineEntry` | `online` |
| `Hints`, `HintsWhy` | none (the hints are off in every scene) |

## Screenshots

```json
{
  "preset": "macos-debug",
  "targets": ["BagiMahjong"],
  "exe": "BagiMahjong",
  "command": ["{python}", "{root}/tools/ui_shots.py", "--exe", "{exe}", "--label", "{label}", "--out", "{out}", "--scenes", "{scenes}", "--window", "{window}"],
  "scenes": {
    "fresh-table": "the table after the deal, the person's seat at the bottom, no screen open",
    "claim-window": "the person may claim the last discard: the action buttons over the table",
    "score-table": "the score table after a won hand",
    "standings": "the standings at the end of the match",
    "setup": "the seat choice (New game)",
    "handoff": "the hot-seat cover between two people",
    "options": "the page Options over the table",
    "rules": "the rules page (Options, then Rules)",
    "online": "the online panel over Setup, its menu page"
  },
  "sizes": ["1920x1080", "720x1280"],
  "defaultSize": "1920x1080"
}
```
