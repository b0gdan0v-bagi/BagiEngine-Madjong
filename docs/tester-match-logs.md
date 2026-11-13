# Match logs of testers

The goal: when people try the game in a browser, their matches should not be lost. A match is a list of events, and from that list the
arena can say how people play against the computer levels. This page says what the game keeps, what the recorder in the game does,
how logs are sent, kept, collected and read, and, in "Not built", what is still open. **In the browser, with the tester's yes, finished
matches go to the game's web service.** On a desktop nothing is sent and the option is not shown.

## What a log holds

A match log is a file of JSON lines, one file per match (`logs/matches/<time>_seed<seed>.jsonl` on a desktop build; the writer is off in
the browser build, where the match goes to the service instead). The grammar is the header of `src/Mahjong/MatchLogJson.h`. The events:

- `start`: the format `version` (now 6), the `seed`, the `settings` (hands per match, flowers, drawn hand replayed, limit, starting
  score), `seats` (`PCCC`: a letter per seat, `P` a person, `C` a computer), `levels` (the strength 1..10 of each seat), and **the
  origin** of the match: `platform` (`desktop`, `web` or `unknown`), `build` (the short git hash of the build, `unknown` outside a git
  work tree) and `gameVersion` (the version for testers, `kGameVersion` in `src/Mahjong/BuildInfo.h`, bumped by hand when a build is
  published), and, since version 6 and only when the tester typed one and said Send, a `nickname` (at most 24 bytes).
- `hand`: the dealt tiles of all four seats and the winds. A log is the owner's: the tiles of other seats are in it, so that a match can
  be replayed and checked. The analysis never looks at them (see below).
- `action`: the seat, `by` (`person` or `computer`: this tells a tester's move from a computer's) and the action.
- `settlement`: how a hand ended (`win` or `drawn`), the scores, the changes and the breakdown of the winner.
- `end`: the final scores and standings.

**There are no addresses, no user agent and no time in a log, and no name but the nickname above** (only the desktop file name carries the local time), and none in what the game sends. A signed-in tester's Google email is not in the log either: the service stamps it beside the match (below). The `build` is
read when the project is configured (`cmake --preset`), so a commit made after that shows only after the next configure.

Logs of format versions 1 to 5 are still read; before version 4 the platform is `unknown` and the build empty, before version 6 there is no nickname.

## What the recorder in the game does

`MatchUploader` (`src/Mahjong/MatchUpload.h`) sits in the chain of the match log sinks and keeps the match in progress in memory, cut
in pieces for the sink (`WebUploadSink`, below):

- **Off unless consent is `Granted`.** With the consent `Unasked` (the default) or `Declined`, or with no sink, nothing is kept and
  nothing leaves. The choice is made at the `Start` of a match: consent given in the middle of a match records the next match (a log
  without its `Start` would not replay); consent withdrawn drops what is buffered and sends nothing more. A match **continued from a
  save is not sent** (its replayed part would go out twice under two ids); the next new match is.
- **Pieces.** One piece is cut after each hand (`Open`) and one at the end of the match (`Ended`). A closed tab loses at most the last hand and leaves the match open in the store (the collecting tool treats it as given up after `--idle-hours`).
  A match that is given up (a new match begins, or the game stops) sends what is left as an `Abandoned` piece, which may be empty, so the
  store learns that the match was given up.
- **A piece holds** the random match id (32 hex digits, drawn like the seed, never made from it), its index, the JSON lines (exactly
  the bytes the file writer writes), `end` and `consentRemembered`. `consentRemembered` false means the choice will be asked again at
  the next visit and the record says so. Nothing else: no name, no address, no user agent, no time.
- `AssemblePieces` puts the pieces of one match together (and refuses a gap, a repeat, a mixed id or a piece after the end); what comes
  out is the log file and replays.

## What is sent, where it is kept, who can read it

- **What:** the pieces above, as the bytes of the log file, posted to `/matches/<match id>/<piece index>` of the page's own server. The
  nickname only if the tester typed one **and** has said Send (a local file never carries one unsent). Nothing about the person or the
  machine. `consentRemembered` says whether the choice is kept by the browser (`upload.txt` in the persistent save folder; a private
  window asks again at each visit).
- **When:** after each hand, at the end of the match, when a new match begins or the game stops.
- **The choice:** a question at the first start (the owner's words, both languages) and two rows in Options (Send / Don't send, and the
  nickname with the note "Sent with your matches"). Only where the page can post; the desktop build is unchanged. The consent is one
  offer, **Accept** / **Don't send**, and says the matches go **together with the Google email** (the owner's wording; a guest has no
  email, so for a guest nothing but the match and the nickname is stored).
- **Where:** the volume of the web service, `MATCH_STORE` (`engine/tools/web/CLAUDE.md`, "Match logs"). Settings the owner makes on the
  service: `MATCH_STORE`, `MATCH_LOG_VERSIONS=1-6` (the format is 6; with `1-5` every new match is refused with 400),
  `MATCH_FORWARDED_FOR=1`, `MATCH_COLLECT_TOKEN` and a volume; the sign-in variables are in `docs/deploy-google-signin.md`.
- **Who is named:** a signed-in tester's match is stamped by the service with the verified Google email (`tester.json` beside the
  match, taken from Google's checked token, never from the page); a guest's match has no stamp. The nickname is an optional label.
- **Who can read it:** whoever holds `MATCH_COLLECT_TOKEN` (the owner): a session, signed in or not, never lists, reads or deletes
  matches (`engine/tools/web/ACCESS.md`). The collecting tool sends the token as a bearer (`MATCH_COLLECT_TOKEN` in its environment) and a
  list entry carries `"tester": {"provider","email"}` for a stamped match, which the tool keeps beside the log in the index.
- **Retention:** until the collecting tool has downloaded it, and at most 90 days on the server; then it lives in the owner's local
  folder `logs/downloaded/` (outside git).
- **Deletion:** `collect_logs.py --delete-id ID` removes one match on the server; `--forget-nickname NAME` removes every log with that
  nickname from the local folders and the index (and from the server with `--store`). The match id is not shown anywhere, so a
  tester who wants a match removed names the nickname; removing by email is not built (the email is in the index and in `state.json`,
  and the owner can look it up there and delete by id).
- **What the client does on an answer:** 200 done; 429, 5xx and network failures retry the same piece with a back-off (60 s doubling to
  10 min); 400, 409, 413 drop that match; 401 and 404 stop sending for the session. The queue is bounded (64 pieces / 4 MiB). A piece
  over 60000 bytes may not survive a closing tab.

## How to collect

`python3 tools/collect_logs.py --store https://the-service` with `MATCH_COLLECT_TOKEN` in the environment (a bearer token; never on the
command line; for a service behind Basic, `--user NAME` and `MATCH_STORE_PASSWORD`) downloads the matches of the service into
`logs/downloaded/<id>.jsonl`, and deletes a match on the server only after its file is on disk and read back equal. A match that may
still be played (no `end`, last piece not empty) is kept as `<id>.part`, which the flow below does not read (so a growing match is not
kept once per state), and is deleted on the server, and becomes `<id>.jsonl`, when it ends or when its piece count did not change for
`--idle-hours` (default 24). The folder then goes through the flow
below, as does any `--from DIR` (`--from DIR [--from DIR ...] [--to logs/collected] [--verifier build/macos-release/bin/BagiMahjongArena] [--recheck]`;
standard library only). For each folder it runs the arena's `--logs DIR --check` and copies the logs that replay to
`logs/collected/<16 hex digits of the SHA-256>.jsonl`, each once, and keeps `logs/collected/index.json` (per hash: file, first source,
`kept` or `dropped`, the reason for a dropped one). **A log that does not replay is never dropped silently:** it is not copied, it is
in the index as `dropped` with its reason, and the summary on stdout counts it and lists it by reason. `--recheck` verifies the kept
files again and moves the ones that no longer replay: a change of the rules makes old logs stop replaying, and this is how the owner
hears of it. The folder `--to` must be outside git or ignored by it (`logs/` is). Exit 0 done, 1 verifier missing or failing (or a failed request, after the files already written are in place), 2 bad
arguments.

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

## Not built

- **A match continued after a reload is lost to the store.** `Continue` works in the browser now (`mahjong-web-saves`), but the save does
  not keep the match id, so the part played before the reload cannot be rejoined and a continued match is not sent. Keeping the id in
  the save is `Save/`.
- **The rows for the neural net** (`mahjong-bot-neural-net`): that entry has no format yet, so no flag is invented. When it has, the decision list of
  `ArenaTester` is the reader to extend, and the record must say that **testers are not experts**.
- Not checked on the owner's machine: a real post from a browser to the real service, whether a closing tab reaches the game's `Stop()`,
  that IndexedDB keeps `upload.txt` over a reload, and that the CMake call that reads the git hash works in the web presets.
- The decisions of people also feed `mahjong-hand-review`, `mahjong-coach-mode` and `mahjong-ev-analyzer` through the differences list.
