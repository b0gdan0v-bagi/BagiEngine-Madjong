---
paths:
  - "engine/tools/perf/**/*"
  - "src/Mahjong/Arena/**/*"
  - "src/Mahjong/Game/ComputerSearch*"
---

# The benchmarks of this repository

Read by the skills `perf` and `perf-investigate` and by the sentinel step of `todo-merge` (`.claude/skills/perf/SKILL.md`; the runner and the format of the set: `engine/tools/perf/CLAUDE.md`). The engine has a file of the same name with the Match3 set; the skills are the same text in both.

## Benchmark set

```json
{
  "area": "mahjong",
  "preset": "macos-release",
  "profilePreset": "macos-profile",
  "targets": ["BagiMahjong", "BagiMahjongArena", "MahjongTests"],
  "repetitions": 5,
  "thresholdPercent": 3,
  "benchmarks": [
    {
      "name": "arena-decision",
      "about": "the Normal bot and its three Normal opponents, 60 seeds, one thread: microseconds per decision",
      "fast": true,
      "command": ["{bin}/BagiMahjongArena", "--focus", "normal", "--others", "normal", "--seeds", "60", "--time"],
      "metrics": {
        "normal_us": {"source": "regex", "pattern": "^normal\\s+\\d+\\s+([0-9.]+)\\s*$", "unit": "us/decision", "better": "lower"},
        "others_us": {"source": "regex", "pattern": "^\\s+others \\(3 seats\\)\\s+\\d+\\s+([0-9.]+)\\s*$", "unit": "us/decision", "better": "lower"},
        "wall_s": {"source": "wall", "unit": "s", "better": "lower", "gate": false}
      }
    },
    {
      "name": "arena-search",
      "about": "the search player (2 worlds, 3 candidates) against Normal, 6 seeds: microseconds per decision, i.e. per batch of rollouts",
      "fast": true,
      "command": ["{bin}/BagiMahjongArena", "--focus", "normal:searchWorlds=2,searchCandidates=3", "--others", "normal", "--seeds", "6", "--time"],
      "metrics": {
        "search_us": {"source": "regex", "pattern": "^normal:\\S+\\s+\\d+\\s+([0-9.]+)\\s*$", "unit": "us/decision", "better": "lower"}
      }
    },
    {
      "name": "table-frames",
      "about": "a match at the table without a window: 600 frames after 120 of warm-up (the computers play their first turns)",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiMahjong", "--frames", "600"],
      "traceCommand": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiMahjong", "--frames", "600", "--trace", "{trace}"],
      "metrics": {
        "fps": {"source": "json", "key": "framesPerSecond", "unit": "frames/s", "better": "higher"},
        "cpu_ms": {"source": "json", "key": "cpuMs", "unit": "ms/frame", "better": "lower"},
        "work_ms": {"source": "json", "key": "workMs", "unit": "ms/frame", "better": "lower"},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "tests",
      "about": "MahjongTests in one process, one thread",
      "repetitions": 3,
      "warmup": 0,
      "command": ["{bin}/MahjongTests", "--gtest_brief=1"],
      "metrics": {
        "wall_s": {"source": "wall", "unit": "s", "better": "lower"}
      }
    }
  ]
}
```

## Notes

- **The seed of a match is random** (`MahjongGame`, `std::random_device`), so `table-frames` plays another deal each run: its draw calls move a little, which is why they are not gated. The arena runs are the same bytes every run; only their time moves.
- Headless, the present of the SDL software renderer is about 97 % of a frame (5.9 of 6 ms on the owner's M4 Pro), whatever the scene draws; `work_ms` (the frame without the present) is the number that moves with the table's code.
- The arena cannot write a trace yet (no `--trace=`; a `ScopedTrace` in its `main` is the game's follow-up, entry `mahjong-qa-perf-rules`), so `perf-investigate` of `arena-*` uses the sampling profiler.
- The sentinel (`todo-merge`): `ask` (it is offered after a merge, not run by itself).
