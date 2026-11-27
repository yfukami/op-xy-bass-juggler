# Bass Juggler

An Open-XY note player for the OP-XY (OS 1.2.1). It turns held keys into a monophonic bassline. Each bar is split into rhythmic cells, and the player reorders those cells (Joseph Schillinger's permutation rhythm), either by hand with PERM or on its own with JUGGLE. The bass pattern moves on one note per cell, so the order of the cells shapes the melody too. It targets SDK 1.16, the first version that gives players a two-page header.

## How it plays

- **One note at a time,** an octave below the keys. The lowest held key is the root, and the other held keys supply chord tones. When keys change in the middle of a bar, the new root takes over from the next cell.
- **A bar is 16 RATE steps,** counted from the first key you press: the player has no bar position from the OP-XY. Releasing every key ends the phrase, and the next key starts a new bar.
- **Cells:** a cell of N steps plays one note on its first step and rests for the rest. GATE sets how long that note is held.
- **Accents:** the bar's first cell and every cell of 4 steps or more play at velocity 120, and the others at 90.
- **Changes wait for the bar line:** turning CELLS or PERM, and every JUGGLE move, take effect at the next bar.

## Controls

Press any encoder to switch pages.

| Encoder | Page 1 | Page 2 |
|---|---|---|
| 1 | RATE: 1/2t, 1/4, 1/4t, 1/8, 1/8t, 1/16, 1/16t, 1/32, 1/32t | JUGGLE: hold, circ1/2/4, gen1/2/4, rnd1/2/4 |
| 2 | CELLS: 8 cell sets (see Cell sets) | LATCH: off, on (experimental) |
| 3 | PERM: the cell order, 1 to the set's count | GATE: 10% to 100% of the cell |
| 4 | PITCH: 25 patterns, from root to walk (see PITCH) | SLIDE: 0% to 100% |

Defaults: 1/16, tres, 1, root, hold, off, 60%, 0%.

- **PERM** runs from 1 to 120, but stops at the current set's count. Turning past it has no effect, and the stored value returns when a larger set is chosen again.
- **GATE** is a share of the cell, not of the step: a 4-step cell at 50% holds for 2 steps.
- Notes outside MIDI's range fold back by octaves.

## Cell sets

Every set fills 16 steps.

| CELLS | Cells | Orders |
|---|---|---|
| `4444` | 4-4-4-4 | 1 (straight quarters) |
| `5533` | 5-5-3-3 | 6 |
| `tres` | 3-3-3-3-2-2 | 15 |
| `7cel` | 3-3-2-2-2-2-2 | 21 |
| `44332` | 4-4-3-3-2 | 30 |
| `5432` | 5-4-3-2-2 | 60 |
| `43322` | 4-3-3-2-2-2 | 60 |
| `64321` | 6-4-3-2-1 | 120 |

PERM 1 is always the set as written. Neighbouring PERM values swap two adjacent cells of different lengths, so one click makes a small change. A few steps cannot: `5533` (5 to 6), `tres` (9 to 10 and 14 to 15), `7cel` (9 to 10 and 18 to 19) and `44332` (25 to 26) move more cells. That is the fewest possible, because each swap flips the order's parity and these sets have more orders of one parity than the other. `tools/gen_perm_tables.py` prints the full report.

## PITCH

The pattern moves on one entry per cell and, by default, starts again at the root every bar. The examples are above the root.

| Label | Entries |
|---|---|
| `root` | root on every cell |
| `oct` | root, octave |
| `188` | root, octave, octave |
| `1881` | root, octave, octave, root |
| `r8b7` | root, octave, seventh, with the same seventh as `r5b7` |
| `r8b78` | root, octave, seventh, octave, with the same seventh |
| `r8r5` | root, octave, root, fifth (disco and funk) |
| `r5` | root, fifth |
| `r5lo` | root, the fifth below (country two-beat) |
| `r55` | root, fifth, fifth |
| `r55r` | root, fifth, fifth, root |
| `r58` | root, fifth, octave |
| `r585` | root, fifth, octave, fifth |
| `r5b7` | root, fifth, seventh. The seventh is a held 7th (10 or 11 semitones, in any octave), otherwise a minor 7th |
| `r5b78` | root, fifth, seventh, octave, with the same seventh |
| `r5b75` | root, fifth, seventh, fifth, with the same seventh |
| `r535` | root, fifth, third, fifth. The third is a held b3 or 3, otherwise a major 3rd |
| `pent` | the minor pentatonic upward: root, b3, 4, 5, b7, octave |
| `maj` | the major pentatonic upward: root, 2, 3, 5, 6, octave |
| `chrd` | the held notes, low to high |
| `dn` | the held notes, high to low |
| `ud` | the held notes up, then down, without repeating the ends (C E G E) |
| `pedl` | the root between each other held note in turn (C E C G) |
| `rtone` | a held note at random on each cell |
| `walk` | like `chrd`, but the bar's last cell plays a semitone below the next bar's first note |

The patterns from `chrd` on use the held notes. With one key held, they use root, fifth and octave instead.

## JUGGLE

The number is how many bars pass between changes.

- **hold:** always plays PERM's order.
- **circ:** moves the first cell to the end. After as many changes as the set has cells, the order is back where it started.
- **gen:** the next order in PERM's list, wrapping at the end.
- **rnd:** any other order, at random.

PERM is the home position. Turning PERM or CELLS jumps there at the next bar line, and juggling continues from it. A new phrase (all keys released, then a key pressed) also starts from PERM. The screen shows the order that is playing.

## LATCH (experimental)

With LATCH on, the bass keeps playing after you let go of the keys. The first key of a new chord, played after every key is up, replaces the latched chord from the next cell; the bar and JUGGLE carry on. A latched key pressed again in a new chord stays. Turning LATCH off stops the keys that are no longer down.

At most seven keys stay latched, so the OP-XY always has room for the next chord's first key: when all eight are held, the first one released is let go as usual.

This relies on the OP-XY keeping a key's notes after the player declines to release it, which the SDK does not promise. It is being tried on the device.

## SLIDE

SLIDE is the chance that a short cell (2 steps or less) ties into the next note. At each bar line, every short cell gets one roll. A tied note ignores GATE and holds on for a quarter step after the next note starts, so the two notes overlap. **The player does not glide by itself:** set the synth to legato or portamento to hear the overlap as a slide.

Two limits come from how a player sounds notes. Each note lives on a held key, and only a different key can sound the overlapping note. So a slide needs **at least two held keys**, and with one key the cells play as usual. A tie into the same pitch also plays as usual, without overlapping.

## Screen

The strip below the header has one position per step. A white dot with a divider on the line below it marks each cell start, so the line reads as the bar's cells. The tick above it is the playhead. The text shows the order that is playing and JUGGLE, such as `6/15 gen2`. With no key held, it shows PERM's order.

Accents and slides are not drawn: a player page has 24 dots that only show or hide, and the cells and the playhead use all of them. Long blocks, and the first, are the accents.

## Development

Run from this directory, with the SDK at `../open-xy`:

```sh
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/bass_juggler --cxx /opt/homebrew/opt/llvm/bin/clang++
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/bass_juggler --cxx /opt/homebrew/opt/llvm/bin/clang++ --firmware
```

The permutation tables are generated, not computed on the device. After changing the cell sets, run `python3 tools/gen_perm_tables.py`, and use `--check` to confirm the header is current.

`sdk.py preview` does not support note players. To review the screen, write an SVG and open it:

```sh
python3 tools/gen_panel.py --state tres,2,5 --note "2/15 circ1" --out /tmp/screen.svg
```

`--state` is the cell set, the order (from 1) and the playhead step (-1 for no key held). Running `python3 tools/gen_panel.py` with no arguments regenerates `panel.svg.fragment`.

`tests/harness.hpp` stands in for the OP-XY in host tests: it plays presses, releases and player steps, and logs every note on and off. Device checks are in [QA.md](QA.md).

## License

MIT. See [LICENSE](LICENSE).
