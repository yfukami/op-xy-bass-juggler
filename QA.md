# Device checklist

Run every check on one OP-XY (OS 1.2.1), top to bottom, in one project, on the exact commit you will publish. Host tests model the SDK adapter and stock's chord records, so they cannot answer these questions.

**Setup:** one track with a simple sustaining bass patch, with Bass Juggler as its player. Start each section from these values (the defaults), then change only what a check says.

| Page 1 | Page 2 (press any encoder) |
|---|---|
| RATE `1/16` | JUGGLE `hold` |
| CELLS `tres` | OCTAVE `-1` |
| PERM `1` | GATE `60%` |
| PITCH `root` | SLIDE `0%` |

The bar counts from the first key, so "bar line" means every 16 steps after it.

## Core timing

1. **Clock while stopped.** With the transport stopped, hold C3. Pass: a bassline plays at once, one octave below C3, and keeps looping 3-3-3-3-2-2.
2. **Clock while playing.** CELLS `4444`. Press play and hold a key against a drum on quarter notes at 80, 120 and 160 BPM. Pass: notes stay locked to the drum.
3. **Long hold.** GATE `10%`, JUGGLE `rnd1`, SLIDE `100%`, hold C and G for five minutes. Pass: no stray or stuck notes or clicks.

## Notes stop cleanly

4. **Releasing keys.** GATE `100%`, RATE `1/4`, hold C, E, G and release them one by one while notes sound. Pass: when the lowest key goes, the next note uses the new lowest key as its root. After the last release, silence, nothing hangs.
5. **Stopping a sequence.** Sequence a chord, GATE `100%`, press play, then stop mid-note. Pass: silence, nothing hangs.
6. **Stop and play while holding a key.** Pass: the bassline keeps playing unchanged through play and stop, and nothing hangs after release.
7. **Interruptions while holding two keys.** (a) Switch track. Pass: the held keys keep playing on their own track until released. (b) Switch project. Pass: silence at once. (c) All-notes-off, where available. Pass: silence, and the next key starts from PERM.
8. **Nine keys.** Pass: the bassline keeps playing from the lowest key, and the ninth key has no effect, including on release.
9. **Changes mid-bar.** Hold a key, and turn RATE, CELLS, PERM and JUGGLE through their ranges mid-bar. Pass: no stuck notes, and the rhythm changes only at bar lines (RATE changes the step length at once).

## Controls and screen

10. **Page switching.** Press any encoder. Pass: the header switches pages, and all eight names and values read correctly.
11. **Full range of every control.** Pass: one value per detent, and no value touches the next column. RATE `1/2t` to `1/32t`, CELLS `4444` to `64321`, PERM `1` to the set's count, PITCH `root` to `walk`, JUGGLE `hold` to `rnd4`, OCTAVE `-2` to `+1`, GATE `10%` to `100%`, SLIDE `0%` to `100%`.
12. **PERM clamps and comes back.** CELLS `64321`, PERM `51`. Then CELLS `5533`. Pass: PERM shows `6`. CELLS `4444`: PERM shows `1`. CELLS `64321` again: PERM shows `51`.
13. **Screen.** Hold a key. Pass: white dots with dividers mark the cells at steps 0, 3, 6, 9, 12, 14 below the header, one tick moves across all 16 steps in order, and the text reads `1/15 hold`. The black covers hiding the other ticks are invisible.
14. **Screen while juggling.** JUGGLE `gen1`. Pass: the text counts `1/15`, `2/15`, ... once per bar, and the blocks change at each bar line. Release the key: the text returns to `1/15 gen1`.
15. **Saving.** Set all eight controls to unusual values, save, switch project and back. Pass: all eight return.
16. **Name.** Pass: the player menu shows `bass_juggler` in full.

## Musical features

17. **Straight reference.** CELLS `4444`, JUGGLE `hold`. Pass: straight quarter-note roots, all accented alike.
18. **Tresillo and one PERM click.** CELLS `tres`, PERM `1`. Pass: 3-3-3-3-2-2 (four even notes, then two quicker ones). PERM `2`: pass, the bar becomes 3-3-3-2-3-2, an audible swap of two adjacent cells, from the next bar line.
19. **CIRC.** JUGGLE `circ1`. Pass: the groove rotates every bar (3-3-3-2-2-3, 3-3-2-2-3-3, ...), and the seventh bar is 3-3-3-3-2-2 again. `circ2` and `circ4` hold each order for 2 and 4 bars.
20. **GEN and RND on the bar line.** JUGGLE `gen1`, then `rnd1`, with a drum on the bar's downbeat (press the key on a downbeat). Pass: each change lands exactly on the bar line, never mid-bar. `rnd1` never repeats the same order twice in a row.
21. **PERM is home.** JUGGLE `gen1`, let it run a few bars, then set PERM `10`. Pass: the next bar plays order 10 (`10/15`), and juggling continues `11/15`, `12/15`. Release all keys and press again: it starts from `10/15`.
22. **Chord and 7th.** PITCH `r5b7`. Hold C E G B (major 7th). Pass: C is the root (an octave down), the pattern runs root, fifth, B, octave. Then hold C E G Bb: it uses Bb. Hold C alone: it uses Bb (the default minor 7th).
23. **Root from the lowest key.** PITCH `chrd`, hold E G C (C lowest). Pass: the notes climb C E G from the lowest note, and adding a lower A mid-bar makes A the root from the next cell.
24. **WALK.** PITCH `walk`, hold C E G. Pass: each bar climbs C E G C E, and its last note is B, leading into the next bar's C.
25. **Accents.** CELLS `5533`, PERM `2` (5-3-5-3). Pass: the two 5-step cells are clearly louder than the 3-step cells. On `tres`, only the first note of the bar is accented.
26. **SLIDE.** Set the synth to legato or portamento. PITCH `r5`, SLIDE `100%`, hold C and G. Pass: the two short cells at the end of each `tres` bar slide into the next note. SLIDE `0%`: no slides. Hold only C at SLIDE `100%`: plays as with SLIDE `0%`.
27. **SLIDE chance.** SLIDE `50%`, as in check 26. Pass: about half the short cells slide, varying bar to bar.
28. **GATE and OCTAVE.** GATE `10%`: short staccato notes. GATE `100%`: each note lasts to the next with no gap or overlap. OCTAVE `+1`: the bassline sits an octave above the held keys.

## Extremes

29. **Fastest.** About 200 BPM, RATE `1/32t`, CELLS `64321`, JUGGLE `rnd1`, SLIDE `100%`, hold two keys. Pass: steady notes, none dropped or stuck, clean stop on release.
30. **Slowest.** About 40 BPM, RATE `1/2t`, GATE `100%`, hold two keys. Pass: notes change on time without hanging.
31. **Saved project on a new build.** Reopen the project from check 15 on a later build. Pass: Bass Juggler and all eight values return.
32. **Fresh start.** Release all keys, then press one. Pass: the first note comes at once, and the tick starts at the left end.

## Results

Not yet run.
