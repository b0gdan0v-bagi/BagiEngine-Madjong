# Match logs of testers

The goal: when people try the game in a browser, their matches should not be lost. A match is a list of events, and from that list the
arena can say how people play against the computer levels. This page says what the game keeps, what the recorder in the game does,
how logs are collected and read, and, in "Not built yet", what is still open. **Nothing is sent anywhere yet.**

## What a log holds

A match log is a file of JSON lines, one file per match (`logs/matches/<time>_seed<seed>.jsonl` on a desktop build; the writer is off in
the browser build). The grammar is the header of `src/Mahjong/MatchLogJson.h`. The events:

- `start`: the format `version` (now 5), the `seed`, the `settings` (hands per match, flowers, drawn hand replayed, limit, starting
  score), `seats` (`PCCC`: a letter per seat, `P` a person, `C` a computer), `levels` (the strength 1..10 of each seat), and **the
  origin** of the match: `platform` (`desktop`, `web` or `unknown`), `build` (the short git hash of the build, `unknown` outside a git
  work tree) and `gameVersion` (the version for testers, `kGameVersion` in `src/Mahjong/BuildInfo.h`, bumped by hand when a build is
  published).
- `hand`: the dealt tiles of all four seats and the winds. A log is the owner's: the tiles of other seats are in it, so that a match can
  be replayed and checked. The analysis never looks at them (see below).
- `action`: the seat, `by` (`person` or `computer`: this tells a tester's move from a computer's) and the action.
- `settlement`: how a hand ended (`win` or `drawn`), the scores, the changes and the breakdown of the winner.
- `end`: the final scores and standings.

**There are no names, no addresses, no user agent and no time in a log** (only the desktop file name carries the local time), and none in what the recorder sends. The `build` is
read when the project is configured (`cmake --preset`), so a commit made after that shows only after the next configure.

Logs of format versions 1 to 4 (written before the origin existed) are still read; their platform is `unknown` and their build empty.

## What the recorder in the game does

`MatchUploader` (`src/Mahjong/MatchUpload.h`) sits in the chain of the match log sinks and keeps the match in progress in memory, cut
in pieces for a sink that does not exist yet:

- **Off unless consent is `Granted`.** With the consent `Unasked` (the default) or `Declined`, or with no sink, nothing is kept and
  nothing leaves. The choice is made at the `Start` of a match: consent given in the middle of a match records the next match (a log
  without its `Start` would not replay); consent withdrawn drops what is buffered and sends nothing more.
- **Pieces.** One piece is cut after each hand (`Open`) and one at the end of the match (`Ended`). A closed tab loses at most the last hand.
  A match that is given up (a new match begins, or the game stops) sends what is left as an `Abandoned` piece, which may be empty, so the
  store learns that the match was given up.
- **A piece holds** the random match id (32 hex digits, drawn like the seed, never made from it), its index, the JSON lines (exactly
  the bytes the file writer writes), `end` and `consentRemembered`. `consentRemembered` false means the choice will be asked again at
  the next visit and the record says so. Nothing else: no name, no address, no user agent, no time.
- `AssemblePieces` puts the pieces of one match together (and refuses a gap, a repeat, a mixed id or a piece after the end); what comes
  out is the log file and replays.

## How to collect

`python3 tools/collect_logs.py --from DIR [--from DIR ...] [--to logs/collected] [--verifier build/ClangmacOS/BagiMahjongArena] [--recheck]`
(standard library only). For each folder it runs the arena's `--logs DIR --check` and copies the logs that replay to
`logs/collected/<16 hex digits of the SHA-256>.jsonl`, each once, and keeps `logs/collected/index.json` (per hash: file, first source,
`kept` or `dropped`, the reason for a dropped one). **A log that does not replay is never dropped silently:** it is not copied, it is
in the index as `dropped` with its reason, and the summary on stdout counts it and lists it by reason. `--recheck` verifies the kept
files again and moves the ones that no longer replay: a change of the rules makes old logs stop replaying, and this is how the owner
hears of it. The folder `--to` must be outside git or ignored by it (`logs/` is). Exit 0 done, 1 verifier missing or failing, 2 bad
arguments. A dump downloaded from a store, once there is one, is put in a folder and given with `--from`.

## How to analyse

`BagiMahjongArena --logs PATH` (a folder, searched for `*.jsonl`, or one file) reads logs instead of playing; the options and the
formats are in `docs/bot-arena.md`, "Logs of real games". It prints, in this order:

1. **The corpus**: files read, replayed and dropped (with the reason kind), matches complete and abandoned, hands, person and computer
   seats, seat setups, the computer levels met, platforms and builds, the length of a match.
2. **The agreement of the people with the bots.** For each decision of a person (a seat whose `action.by` is `person`) the position is
   rebuilt as that seat saw it (the same `TableView` the analyzer builds, never the dealt tiles of the others) and a bot of the list
   (`--focus`, default `normal`) chooses on it; the share of equal choices is printed per bot, for discards and claim windows
   separately. A decision with only one legal action is not counted.
3. **The real-game numbers by the level of the computers met**: for every seat-hand of a person at a table with a computer, the logged
   results (win %, points a hand, drawn %, declared %, average winning score), grouped by the level when all the computers of the table
   have one level, else `mixed`.
4. **The differences**: the decisions where the person's move differs from the first bot, each with the line to paste into
   `BagiMahjongAnalyzer` (`--log ... --hand K --action I`).

Read the numbers with these limits in mind:

- **Testers are not experts.** The share of equal choices measures how people play, not what is best: a person who differs from the bot
  may be wrong, or may be right. The differences are a list to look at with the analyzer, not a list of mistakes.
- The unit of every interval is the **match**, not the decision or the hand (the decisions of one match are not independent).
- A hand that was not finished (a given-up match) is not counted in the real-game numbers.
- **Which arena run to put beside the real-game numbers.** For the row of level L, run the arena with the computers at that level on all
  other seats, for the bot whose strength you want to compare the people with: `A --focus normal --others L --first 4000 --seeds 1000`
  (`L` the level of the row). The arena player sits at each of the four seats in turn, so its share of wins at an all-`L` table is
  25 % by the mirror identity when it is as strong as the others; the testers' win % and points a hand against the same computers can be
  read against that. The sample of testers is small and their hands are not the arena's walls: do not compare to a decimal.

## Not built yet

Plainly what is open. The entry is `mahjong-tester-match-logs` of the todo store (area `mahjong`); what is built so far is the game side
that needs neither an engine change nor a decision of the owner.

- **The receiving endpoint and the upload call in the browser** (an engine entry): nothing is sent today. `GetUpload()` of `MahjongGame`
  has no sink behind it, so even a `Granted` consent records nothing.
- **The consent text** (in both languages), where it is shown, and where the choice is remembered (the browser's file system is memory
  and is lost on a reload, so today the choice would be asked at every visit: the reason of `consentRemembered`).
- **Retention** (how long matches are kept), **deletion** of one match by its id on a request, the size limit of a match, the rate limit
  per client, the hosting.
- **A continued saved match is sent twice** once a sink exists: `ResumeSavedMatch` replays the whole log through the chain, so the uploader
  records it again under a new match id, and the pieces of the part already played go out a second time. The glue that sets the sink has
  to decide (skip the replayed events, or send the resumed match as one that is marked as continued).
- Whether a tester may give a **nickname**.
- **The downloader** of the store for `collect_logs.py`.
- **The rows for the neural net** (`mahjong-bot-neural-net`): the decisions of people as rows. The decision list of `ArenaTester` is the
  reader to extend, and the record must say that testers are not experts.
- Not checked on the owner's machine: that the CMake call that reads the git hash works in the web presets.
- The decisions of people also feed `mahjong-hand-review`, `mahjong-coach-mode` and `mahjong-ev-analyzer` through the differences list.
