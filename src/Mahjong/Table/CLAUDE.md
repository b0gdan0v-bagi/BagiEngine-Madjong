# Table/ — the scene as pure functions

Everything here is plain C++ over the model, a `TableView` or a `UIStrings&`: no scene, no renderer, no engine services, so the tests need none. The components in `../Components/` draw and publish what these functions return.

## Files

| File | What |
|------|------|
| `TableLayout` | the table as rectangles in view units (below) |
| `TileAtlas.h` | atlas cells by arithmetic (`SourceRect`, `BackRect`, `SpriteName`); must match `tools/make_tiles.py` (`TileSpritesTest`) |
| `UiPalette.h` | every colour, the type scale `kFontScale`, the grid `kGridUnit` / `kGridStep`, the modal numbers; see `config/CLAUDE.md` ("UI style") |
| `Words` | `Tr(strings, key[, args])`, `Subst`, `WindName` |
| `SeatName`, `SeatPlates` | how a seat is named; `BottomSeat(session)` / `TurnSeat(session, hand)` — the one definition of the seat at the bottom and of whose turn it is; `MakeSeatPlates` |
| `ScoreText`, `MoveText`, `RulesText` | the sentences of the end screens, the move line and the 13 rules pages |
| `ActionButtons` | `SpecOf` (element, action, value, word and hint keys, pictured tiles), `ButtonOf`, `MakeActionButtons(legal, pendingTile)`, `ChowTilesTaken` (the hand tiles a chow takes) |
| `Hints`, `CallingMarks` | `WaitingTiles`, `DeclaringDiscards`, `AdviceFor` (= `ComputerPlayer(kAdviceSeed)`, Normal), `AdviceText`, `HintsApply`; `KeepsWaitingDiscards` |
| `AnimationSpeed.h` | the player's speed Slow / Normal / Fast: `SpeedFactor`, `ScaledSeconds`, `SpeedText` / `TryParseSpeed`; the base times stay the Normal ones |
| `TileFlights` | `PlanFlights(before, after, newMoves)`, `FlightRect`, `kTileFlightSeconds` (must stay below `kComputerStepSeconds`) |
| `TouchRegime.h` | `FitScale`, `NeedsCompactActions`, `CompactScale`, `CompactRowHeight` and the compact-row constants the scene file must match (`MahjongTableScreenTest`) |

## Rules

- **No peeking.** Hints, marks and advice read only `MakeTableView(hand, viewer)` or public data (open melds, the pile, declarations, `Moves()`), never another seat's hand or the live wall. Tests compare models that differ only in hidden data.
- **Words:** one key with `{placeholders}` per sentence; word order belongs to the translation, never pieces joined in code. Rules pages never retype a number or a button word: numbers are placeholders filled from the rule constants, button words come from `WordOf` (`RulesTextTest` fails on a digit outside a placeholder).

## The table layout

`MakeTableLayout(TableInput)` → `TableLayout { viewer, slots, zones }`; a `TileSlot { rect, role (HandTile | MeldTile | BonusTile | DiscardTile | WallStack), seat, index, part, claimEdge }`. The layout knows no tile; the component maps `index` to tiles.

- Design size 1280 x 720 (`kViewWidth` / `kViewHeight`, repeated in `config/SceneConfig.xml`); another size scales uniformly and centres.
- The viewer is at the bottom; `SeatPosition(viewer, seat)` 0 bottom, 1 right, 2 top, 3 left.
- Discards: a block per seat, 6 per row, extra rows squeezed (a zone never grows). The wall: a ring of `ceil(Remaining() / 2)` stacks filled clockwise (a simplification; the physical break is not tracked).
- Every zone holds the most the rules allow (14 concealed, four kongs, 8 bonus tiles, the 144-tile wall); no slot leaves the view and no zone overlaps another — `TableLayoutTest` holds this for the worst-case table.
- `zones`: the discard blocks, the wall ring, the four seat plates. `claimEdge` marks the claimed tile of an open meld and the side facing the seat it came from. `BoundsOfMeld`, `HitHandTile` (the later tile wins on overlap).
