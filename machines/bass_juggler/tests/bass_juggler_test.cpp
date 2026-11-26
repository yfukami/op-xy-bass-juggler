#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <set>
#include "harness.hpp"

typedef std::vector<u32> Notes;
enum { R2T, R4, R4T, R8, R8T, R16, R16T, R32, R32T };
static const u32 kStart = machine::kDefaultChordWindow;  // a key alone waits about 15 ms (no tempo: 7 steps)

// Every control at its definition default unless a test changes it.
static void defaults(Host& h) {
    h.set(0, R16, machine::kRates);
    h.set(1, 2, machine::kCellSets);  // TRES
    h.set(2, 0, machine::kPerms);
    h.set(3, 0, machine::kPitches);   // ROOT
    h.set(4, 0, machine::kJuggles);   // HOLD
    h.set(5, 0, machine::kLatches);   // LATCH off
    h.set(6, 10, machine::kGates);    // 60%
    h.set(7, 0, machine::kSlides);    // 0%
}

// The CELLS sets as the spec writes them, kept apart from the generated header.
static const std::vector<std::vector<u32>> kSpecSets = {
    {4, 4, 4, 4}, {5, 5, 3, 3}, {3, 3, 3, 3, 2, 2}, {3, 3, 2, 2, 2, 2, 2},
    {4, 4, 3, 3, 2}, {5, 4, 3, 2, 2}, {4, 3, 3, 2, 2, 2}, {6, 4, 3, 2, 1}};
static const u32 kSpecCounts[] = {1, 6, 15, 21, 30, 60, 60, 120};

static u32 factorial(u32 n) { return n < 2 ? 1 : n * factorial(n - 1); }

static u32 multinomial(const std::vector<u32>& cells) {
    u32 count = factorial(static_cast<u32>(cells.size()));
    for (u32 size = 1; size <= 16; ++size)
        count /= factorial(static_cast<u32>(std::count(cells.begin(), cells.end(), size)));
    return count;
}

static std::vector<u32> order_of(u32 set, u32 index) {
    const opxy::u8* row = machine::kOrders[machine::kSetFirst[set] + index];
    return std::vector<u32>(row, row + machine::kSetCells[set]);
}

static bool adjacent_swap(const std::vector<u32>& a, const std::vector<u32>& b) {
    std::vector<u32> diff;
    for (u32 i = 0; i < a.size(); ++i) if (a[i] != b[i]) diff.push_back(i);
    return diff.size() == 2 && diff[1] == diff[0] + 1 && a[diff[0]] == b[diff[1]] && a[diff[1]] == b[diff[0]];
}

static void permutation_tables() {
    require(machine::kCellSetCount == kSpecSets.size() && machine::kPerms == 120);
    u32 next_first = 0;
    for (u32 set = 0; set < machine::kCellSetCount; ++set) {
        const std::vector<u32>& spec = kSpecSets[set];
        const u32 count = machine::kSetOrders[set];
        u32 sum = 0;
        for (u32 cell : spec) sum += cell;
        require(sum == 16);
        require(count == multinomial(spec) && count == kSpecCounts[set] && count <= machine::kPerms);
        require(machine::kSetCells[set] == spec.size() && machine::kSetFirst[set] == next_first);
        next_first += count;
        require(order_of(set, 0) == spec);  // index 0 is the set as written
        std::set<std::vector<u32>> seen;
        u32 imperfect = 0;
        for (u32 i = 0; i < count; ++i) {
            const std::vector<u32> order = order_of(set, i);
            const opxy::u8* row = machine::kOrders[machine::kSetFirst[set] + i];
            for (u32 j = static_cast<u32>(spec.size()); j < machine::kMaxCells; ++j) require(row[j] == 0);
            std::vector<u32> a = order, b = spec;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            require(a == b);                  // a reordering of the set
            require(seen.insert(order).second);  // every entry distinct
            if (i && !adjacent_swap(order_of(set, i - 1), order)) ++imperfect;
        }
        // Distinct, as many as the multinomial, and each a reordering: so every one is there.
        std::vector<u32> every = spec;
        std::sort(every.begin(), every.end());
        do require(seen.count(every)); while (std::next_permutation(every.begin(), every.end()));
        require(imperfect == machine::kSetImperfect[set]);  // the generator's report matches
    }
    require(next_first == machine::kOrderCount);
}

enum { k4444, k5533, kTres, k7cel, k44332, k5432, k43322, k64321 };

static std::vector<u32> starts_of(const machine::Bar& bar) {
    std::vector<u32> out;
    for (u32 step = 0; step < 16; ++step) if (bar.starts >> step & 1) out.push_back(step);
    return out;
}

static void bar_layout() {
    machine::Bar bar;
    machine::fill_bar(bar, kTres, 0);  // 3-3-3-3-2-2
    require(starts_of(bar) == Notes({0, 3, 6, 9, 12, 14}) && bar.count == 6 && bar.accents == 1);
    for (u32 cell = 0; cell < 6; ++cell) require(machine::cell_starting(bar, starts_of(bar)[cell]) == cell);
    require(machine::cell_starting(bar, 1) == machine::kMaxCells && machine::cell_starting(bar, 15) == machine::kMaxCells);
    machine::fill_bar(bar, k4444, 0);
    require(starts_of(bar) == Notes({0, 4, 8, 12}) && bar.accents == 0xf);  // every cell is long
    machine::fill_bar(bar, k64321, 0);
    require(starts_of(bar) == Notes({0, 6, 10, 13, 15}) && bar.accents == 3);
    machine::fill_bar(bar, k5533, 1);  // 5-3-5-3: the long cells
    require(starts_of(bar) == Notes({0, 5, 8, 13}) && bar.accents == 5);
    // Every table entry: the cells fill the bar exactly.
    for (u32 set = 0; set < machine::kCellSetCount; ++set)
        for (u32 i = 0; i < machine::kSetOrders[set]; ++i) {
            machine::fill_bar(bar, set, i);
            u32 sum = 0;
            for (u32 c = 0; c < bar.count; ++c) sum += bar.cells[c];
            require(sum == 16 && starts_of(bar).size() == bar.count);
        }
}

struct Note { u32 on, off, pitch, velocity; };

// Each note's start and end, in player steps from the first event.
static std::vector<Note> notes_of(const Host& h) {
    std::vector<Note> out;
    for (size_t i = 0; i < h.events.size(); ++i) {
        const Event& e = h.events[i];
        if (!e.on) continue;
        Note n{e.time, ~0u, e.pitch, e.velocity};
        for (size_t j = i + 1; j < h.events.size(); ++j)
            if (!h.events[j].on && h.events[j].pitch == e.pitch) { n.off = h.events[j].time; break; }
        out.push_back(n);
    }
    return out;
}

static void plays_the_cells() {
    Host h;
    defaults(h);  // TRES, 1/16 (60 player steps), GATE 60%
    h.press(48);
    h.step(2 * 16 * 60);
    const std::vector<Note> notes = notes_of(h);
    const u32 starts[] = {0, 3, 6, 9, 12, 14};
    const u32 cells[] = {3, 3, 3, 3, 2, 2};
    require(notes.size() == 12);
    for (u32 i = 0; i < 12; ++i) {
        const Note& n = notes[i];
        // The bar counts from the key; only the first note waits for the chord window.
        require(n.on == (i ? (16 * (i / 6) + starts[i % 6]) * 60 : kStart));
        require(n.pitch == 36);
        require(n.velocity == (i % 6 == 0 ? machine::kAccentVelocity : machine::kPlainVelocity));
        require(n.off - n.on == cells[i % 6] * 60 * 60 / 100);  // GATE is a share of the cell
    }
}

static void gate_full_never_overlaps() {
    Host h;
    defaults(h);
    h.set(6, 18, machine::kGates);  // 100%
    h.press(48);
    for (u32 i = 0; i < kStart + 3 * 16 * 60; ++i) {
        h.step();
        require(h.sounding() <= 1);
    }
    const std::vector<Note> notes = notes_of(h);
    for (size_t i = 0; i + 1 < notes.size(); ++i) require(notes[i].off == notes[i + 1].on);
}

static void changes_wait_for_the_bar_line() {
    Host h;
    defaults(h);
    h.press(48);
    h.step(8 * 60);  // half a bar (the bar counts from the key)
    h.set(2, 1, machine::kPerms);  // PERM 2
    h.step(8 * 60);
    require(h.state.bar.index == 0);  // still the bar that started
    h.step(1);
    require(h.state.bar.index == 1 && h.state.bar.set == kTres);
    // CELLS too: the order PERM names, clamped to the new set's orders.
    h.set(2, 50, machine::kPerms);  // PERM 51
    h.set(1, k5533, machine::kCellSets);
    h.step(16 * 60);
    require(h.state.bar.set == k5533 && h.state.bar.index == 5);
    h.set(1, k64321, machine::kCellSets);
    h.step(16 * 60);
    require(h.state.bar.set == k64321 && h.state.bar.index == 50);  // PERM kept its value
    h.release(48);
    require(h.sounding() == 0);
}

typedef std::vector<opxy::i32> Offsets;

static machine::Tones tones_of(const Notes& keys) {
    machine::State st = machine::State();
    for (u32 i = 0; i < keys.size(); ++i) {
        st.slots[i].root = static_cast<opxy::u8>(keys[i]);
        st.active |= 1u << i;
    }
    return machine::read_tones(st, machine::lowest_key(st));
}

static Offsets run(u32 pattern, const machine::Tones& t, u32 count) {
    Offsets out;
    for (u32 pos = 0; pos < count; ++pos) out.push_back(machine::pattern_offset(pattern, t, pos));
    return out;
}

static void reads_the_chord() {
    machine::Tones t = tones_of({55, 48, 58, 52});  // C E G Bb, any order
    require(t.count == 4 && t.offsets[0] == 0 && t.offsets[1] == 4 && t.offsets[2] == 7 && t.offsets[3] == 10);
    require(t.seventh == 10);
    require(tones_of({48, 59}).seventh == 11);  // a held major 7th
    require(tones_of({48, 70}).seventh == 10);  // a 7th an octave up still counts, placed in the root's octave
    require(tones_of({48}).seventh == 10 && tones_of({48, 55}).seventh == 10);  // none held: +10
}

static void pattern_steps() {
    using namespace machine;
    const Tones chord = tones_of({48, 52, 55, 59}), one = tones_of({48});
    require(run(Root, chord, 4) == Offsets({0, 0, 0, 0}));
    require(run(Octave, chord, 4) == Offsets({0, 12, 0, 12}));
    require(run(RootFifth, chord, 4) == Offsets({0, 7, 0, 7}));
    require(run(RootFifthOctave, chord, 6) == Offsets({0, 7, 12, 0, 7, 12}));
    require(run(RootFifthSeventh, chord, 5) == Offsets({0, 7, 11, 12, 0}));  // the held B
    require(run(RootFifthSeventh, one, 5) == Offsets({0, 7, 10, 12, 0}));    // no 7th held: +10
    require(run(ChordTones, chord, 6) == Offsets({0, 4, 7, 11, 0, 4}));
    require(run(ChordTones, one, 3) == Offsets({0, 7, 12}));  // one key climbs root, fifth, octave
    require(run(Walk, chord, 5) == Offsets({0, 4, 7, 11, 0}));
    require(run(RootOctaveOctave, chord, 4) == Offsets({0, 12, 12, 0}));
    require(run(RootOctaveOctaveRoot, chord, 5) == Offsets({0, 12, 12, 0, 0}));
    require(run(RootFifthFifth, chord, 4) == Offsets({0, 7, 7, 0}));
    require(run(RootFifthFifthRoot, chord, 5) == Offsets({0, 7, 7, 0, 0}));
    require(run(RootFifthOctaveFifth, chord, 5) == Offsets({0, 7, 12, 7, 0}));
    require(run(RootFifthSeventhFifth, chord, 5) == Offsets({0, 7, 11, 7, 0}));  // the held B
    require(run(RootFifthSeventhFifth, one, 4) == Offsets({0, 7, 10, 7}));
    require(run(RootOctaveSeventh, chord, 5) == Offsets({0, 12, 11, 12, 0}));
    require(run(RootOctaveSeventh, one, 4) == Offsets({0, 12, 10, 12}));
    require(run(RootLowFifth, chord, 3) == Offsets({0, -5, 0}));
    require(run(RootFifthThirdFifth, chord, 4) == Offsets({0, 7, 4, 7}));      // the held E
    require(run(RootFifthThirdFifth, tones_of({48, 51}), 4) == Offsets({0, 7, 3, 7}));  // a held Eb
    require(run(RootFifthThirdFifth, one, 4) == Offsets({0, 7, 4, 7}));        // no 3rd held: major
    require(run(Pentatonic, one, 7) == Offsets({0, 3, 5, 7, 10, 12, 0}));
    require(run(ChordDown, chord, 5) == Offsets({11, 7, 4, 0, 11}));
    require(run(ChordDown, one, 4) == Offsets({12, 7, 0, 12}));
    require(run(RootOctaveRootFifth, chord, 5) == Offsets({0, 12, 0, 7, 0}));
    require(run(MajorPentatonic, one, 7) == Offsets({0, 2, 4, 7, 9, 12, 0}));
    require(run(ChordUpDown, chord, 7) == Offsets({0, 4, 7, 11, 7, 4, 0}));   // the ends once
    require(run(ChordUpDown, tones_of({48, 55}), 3) == Offsets({0, 7, 0}));   // two notes: up, down
    require(run(ChordUpDown, one, 5) == Offsets({0, 7, 12, 7, 0}));
    require(run(Pedal, chord, 7) == Offsets({0, 4, 0, 7, 0, 11, 0}));
    require(run(Pedal, tones_of({48, 55}), 3) == Offsets({0, 7, 0}));
    require(run(Pedal, one, 5) == Offsets({0, 7, 0, 12, 0}));
    for (u32 pattern = 0; pattern < machine::kPitches; ++pattern)
        require(machine::pattern_length(pattern, chord) <= 2 * machine::kSlots);
}

static Offsets walk_bars(const machine::Tones& t, u32 cells, u32 bars, bool reset) {
    Offsets out;
    u32 pos = 0;
    for (u32 bar = 0; bar < bars; ++bar) {
        if (reset) pos = 0;
        for (u32 cell = 0; cell < cells; ++cell, ++pos)
            out.push_back(machine::cell_offset(machine::Walk, t, pos, cell + 1 == cells, reset));
    }
    return out;
}

static void walk_approach() {
    const machine::Tones c7 = tones_of({48, 52, 55, 58});  // C E G Bb
    // Each bar starts from the root, so the last cell leads into it from a semitone below.
    require(walk_bars(c7, 6, 2, true) == Offsets({0, 4, 7, 10, 0, -1, 0, 4, 7, 10, 0, -1}));
    // Running on: the last cell leads into wherever the next bar starts.
    require(walk_bars(c7, 6, 2, false) == Offsets({0, 4, 7, 10, 0, 6, 7, 10, 0, 4, 7, -1}));
}

static void interference() {
    // R5b7 (four entries) over TRES (six cells): with the pattern running on, the
    // second bar starts on its third entry; with it reset, every bar is the same.
    const machine::Tones one = tones_of({48});
    Offsets on, reset;
    u32 pos = 0;
    for (u32 bar = 0; bar < 2; ++bar)
        for (u32 cell = 0; cell < 6; ++cell) {
            on.push_back(machine::cell_offset(machine::RootFifthSeventh, one, pos++, cell == 5, false));
            reset.push_back(machine::cell_offset(machine::RootFifthSeventh, one, cell, cell == 5, true));
        }
    require(on == Offsets({0, 7, 10, 12, 0, 7, 10, 12, 0, 7, 10, 12}));
    require(reset == Offsets({0, 7, 10, 12, 0, 7, 0, 7, 10, 12, 0, 7}));
    require(machine::kPitchResetEachBar);  // the default the player uses
}

static Notes bar_pitches(u32 pattern, const Notes& keys, u32 bars) {
    Host h;
    defaults(h);
    h.set(3, pattern, machine::kPitches);
    for (u32 key : keys) h.press(key);
    h.step(bars * 16 * 60);
    return h.pitches();
}

static void plays_the_patterns() {
    using namespace machine;
    // TRES: six cells a bar, each bar starting the pattern again.
    // An octave below the keys.
    require(bar_pitches(RootFifthSeventh, {48, 52, 55, 59}, 2) == Notes({36, 43, 47, 48, 36, 43, 36, 43, 47, 48, 36, 43}));
    require(bar_pitches(RootFifthSeventh, {48}, 1) == Notes({36, 43, 46, 48, 36, 43}));
    require(bar_pitches(ChordTones, {55, 52, 48}, 1) == Notes({36, 40, 43, 36, 40, 43}));
    require(bar_pitches(Walk, {48, 52, 55}, 2) == Notes({36, 40, 43, 36, 40, 35, 36, 40, 43, 36, 40, 35}));
    require(bar_pitches(Octave, {40}, 1) == Notes({28, 40, 28, 40, 28, 40}));
    require(bar_pitches(RootFifthSeventhFifth, {48, 59}, 1) == Notes({36, 43, 47, 43, 36, 43}));
    require(bar_pitches(RootOctaveOctaveRoot, {48}, 1) == Notes({36, 48, 48, 36, 36, 48}));
    require(bar_pitches(RootLowFifth, {48}, 1) == Notes({36, 31, 36, 31, 36, 31}));  // the G below
    require(bar_pitches(ChordDown, {48, 52, 55}, 1) == Notes({43, 40, 36, 43, 40, 36}));
    // RTONE: only held notes, every one turning up, and not a fixed cycle.
    const Notes random = bar_pitches(RandomTone, {48, 52, 55}, 40);
    require(random.size() == 240);
    std::set<u32> seen(random.begin(), random.end());
    require(seen == std::set<u32>({36, 40, 43}));
    require(Notes(random.begin(), random.begin() + 6) != Notes(random.begin() + 6, random.begin() + 12) ||
            Notes(random.begin() + 6, random.begin() + 12) != Notes(random.begin() + 12, random.begin() + 18));
}

static void new_root_from_the_next_cell() {
    Host h;
    defaults(h);
    h.press(48);
    h.step(4 * 60);  // into the second cell (steps 3..5)
    h.press(43);     // a lower key: the new root
    h.step(12 * 60);
    require(h.pitches() == Notes({36, 36, 31, 31, 31, 31}));
    h.release(43);
    h.release(48);
    require(h.sounding() == 0);
}

static void reads_defaults() {
    Host h;
    defaults(h);
    const machine::Params p = machine::read_params(h.controls);
    require(p.rate == 60 && p.cells == 2 && p.perm == 0 && p.pitch == 0 && p.juggle == 0);
    require(!p.latch && p.gate == 60 && p.slide == 0);
}

static void root_an_octave_down() {
    Host h;
    defaults(h);
    h.press(48);
    h.step(16 * 60);  // one bar: TRES's six cells
    const std::vector<Event> ons = h.ons();
    require(ons.size() == 6 && ons[0].pitch == 36 && ons[0].time == kStart);
    h.release(48);
    require(h.sounding() == 0);
}

static void lowest_key_is_the_root() {
    Host h;
    defaults(h);
    h.press(55);
    h.press(48);  // arrives within the chord window
    h.step(kStart + 1);
    require(h.pitches() == Notes({36}));
    h.release(48);
    h.release(55);
    require(h.sounding() == 0);
}

static void folds_into_range() {
    require(machine::fold(-5) == 7 && machine::fold(130) == 118 && machine::fold(60) == 60);
}

static void nine_keys_and_cleanup() {
    Host h;
    defaults(h);
    for (u32 i = 0; i < 9; ++i) h.press(40 + i);
    h.step(1000);
    require(h.state.active == 0xff && h.passed_through == 0);
    h.release(48);  // the ninth key: never held
    h.all_notes_off();
    require(h.sounding() == 0 && h.state.active == 0 && h.stale_updates == 0);
}

static void long_hold_stays_bounded() {
    Host h;
    defaults(h);
    h.press(40);
    h.press(47);
    h.step(100000);  // past several parked-tone renewals
    require(h.sounding() <= 1 && h.stale_updates == 0 && h.largest_drain <= 8);
    h.release(40);
    h.release(47);
    require(h.sounding() == 0);
}

// ---- JUGGLE ----
enum { kHold, kCirc1, kCirc2, kCirc4, kGen1, kGen2, kGen4, kRnd1, kRnd2, kRnd4 };
static const u32 kBar = 16 * 60;  // a bar at 1/16, in player steps

// The table index of each bar's order, for `count` bars from the first key.
static Notes bar_indices(Host& h, u32 count) {
    Notes out;
    h.step(kStart + 1);
    out.push_back(h.state.bar.index);
    for (u32 i = 1; i < count; ++i) {
        h.step(kBar);
        out.push_back(h.state.bar.index);
    }
    return out;
}

static Host juggling(u32 juggle, u32 cells = kTres, u32 perm = 0) {
    Host h;
    defaults(h);
    h.set(1, cells, machine::kCellSets);
    h.set(2, perm, machine::kPerms);
    h.set(4, juggle, machine::kJuggles);
    h.press(48);
    return h;
}

static std::vector<u32> rotated(std::vector<u32> cells, u32 times) {
    std::rotate(cells.begin(), cells.begin() + times % cells.size(), cells.end());
    return cells;
}

static void juggle_modes() {
    for (u32 j = 0; j < machine::kJuggles; ++j) {
        const machine::Params p = [&] { Host h; defaults(h); h.set(4, j, machine::kJuggles); return machine::read_params(h.controls); }();
        require(p.juggle == j);
    }
    const u32 kinds[] = {machine::Hold, machine::Circular, machine::Circular, machine::Circular, machine::General,
                         machine::General, machine::General, machine::Random, machine::Random, machine::Random};
    const u32 periods[] = {1, 1, 2, 4, 1, 2, 4, 1, 2, 4};
    for (u32 j = 0; j < machine::kJuggles; ++j)
        require(machine::juggle_kind(j) == kinds[j] && machine::juggle_period(j) == periods[j]);
}

static void hold_plays_perm() {
    Host h = juggling(kHold, kTres, 7);
    require(bar_indices(h, 8) == Notes(8, 7));
}

static void circular_rotates() {
    // CIRC 1 over TRES: each bar moves the first cell to the end, and the
    // sixth rotation is the first order again.
    Host h = juggling(kCirc1);
    const Notes indices = bar_indices(h, 13);
    for (u32 bar = 0; bar < 13; ++bar)
        require(order_of(kTres, indices[bar]) == rotated(kSpecSets[kTres], bar));
    require(indices[6] == 0 && indices[12] == 0 && indices[1] != 0);
    // From another home, every set: the rotation is found in the table.
    for (u32 set = 0; set < machine::kCellSetCount; ++set) {
        const u32 home = machine::kSetOrders[set] - 1;
        Host other = juggling(kCirc1, set, home);
        const Notes got = bar_indices(other, 9);
        for (u32 bar = 0; bar < 9; ++bar)
            require(order_of(set, got[bar]) == rotated(order_of(set, home), bar));
    }
    // The bar's notes follow the rotated order: 3-3-3-2-2-3 in the second bar.
    Host played = juggling(kCirc1);
    played.step(2 * kBar);
    std::vector<u32> second;
    for (const Event& e : played.ons()) if (e.time >= kBar && e.time < 2 * kBar) second.push_back(e.time / 60 - 16);
    require(second == Notes({0, 3, 6, 9, 11, 13}));
    // CIRC 2 and CIRC 4 hold each order for 2 and 4 bars.
    Host two = juggling(kCirc2);
    const Notes slow = bar_indices(two, 6);
    require(slow[0] == slow[1] && slow[1] != slow[2] && slow[2] == slow[3] && slow[3] != slow[4]);
    require(order_of(kTres, slow[2]) == rotated(kSpecSets[kTres], 1) && order_of(kTres, slow[4]) == rotated(kSpecSets[kTres], 2));
    Host four = juggling(kCirc4);
    const Notes slower = bar_indices(four, 9);
    require(slower[0] == 0 && slower[3] == 0 && slower[4] != 0 && slower[4] == slower[7] && slower[8] != slower[7]);
}

static void general_steps_through_the_table() {
    Host h = juggling(kGen1);
    const Notes indices = bar_indices(h, 17);
    for (u32 bar = 0; bar < 17; ++bar) require(indices[bar] == bar % 15);  // TRES has 15, then wraps
    Host two = juggling(kGen2, k5533, 4);
    require(bar_indices(two, 6) == Notes({4, 4, 5, 5, 0, 0}));
    Host four = juggling(kGen4, k64321, 118);
    require(bar_indices(four, 9) == Notes({118, 118, 118, 118, 119, 119, 119, 119, 0}));
    Host one = juggling(kGen1, k4444);  // a single order stays
    require(bar_indices(one, 3) == Notes({0, 0, 0}));
}

static void random_jumps_elsewhere() {
    Host h = juggling(kRnd1);
    const Notes indices = bar_indices(h, 300);
    require(indices[0] == 0);
    std::set<u32> seen;
    for (u32 bar = 0; bar < indices.size(); ++bar) {
        require(indices[bar] < 15);
        if (bar) require(indices[bar] != indices[bar - 1]);
        seen.insert(indices[bar]);
    }
    require(seen.size() == 15);  // every order turns up
    Host two = juggling(kRnd2, k64321);
    const Notes slow = bar_indices(two, 7);
    require(slow[0] == slow[1] && slow[1] != slow[2] && slow[2] == slow[3] && slow[3] != slow[4]);
    Host one = juggling(kRnd1, k4444);
    require(bar_indices(one, 3) == Notes({0, 0, 0}));
    // The picks are spread evenly: no order of 64321 is far from its share.
    machine::State s = {};
    u32 hits[120] = {};
    u32 index = 0;
    for (u32 i = 0; i < 120 * 200; ++i) ++hits[index = machine::juggled(s, k64321, index, machine::Random)];
    for (u32 i = 0; i < 120; ++i) require(hits[i] > 100 && hits[i] < 300);
}

static void juggles_only_at_the_bar_line() {
    for (u32 juggle : {kCirc1, kGen1, kRnd1}) {
        Host h = juggling(juggle);
        u32 index = h.state.bar.index, changes = 0;
        for (u32 i = 0; i < 8 * kBar; ++i) {
            h.step();
            if (h.state.bar.index != index) {
                require(h.state.step == 0 && h.state.tick == 0);
                index = h.state.bar.index;
                ++changes;
            }
        }
        require(changes == 7);
    }
}

static void turning_perm_goes_home() {
    // GEN 1 from PERM 1; PERM 10 turned mid-bar takes over at the next bar
    // line, and GEN goes on from there.
    Host h = juggling(kGen1);
    h.step(kStart + 1 + 2 * kBar + kBar / 2);
    require(h.state.bar.index == 2);
    h.set(2, 9, machine::kPerms);
    h.step(kBar / 2);
    require(h.state.bar.index == 9);
    h.step(kBar);
    require(h.state.bar.index == 10);
    // CELLS too: the new set's home, then juggling from it.
    h.set(1, k5533, machine::kCellSets);
    h.step(kBar);
    require(h.state.bar.set == k5533 && h.state.bar.index == 5);  // PERM 10 clamped to 5533's 6
    h.step(kBar);
    require(h.state.bar.index == 0);
    // Switching to HOLD returns home at the next bar line.
    h.set(4, kHold, machine::kJuggles);
    h.step(kBar);
    require(h.state.bar.index == 5);
}

static void new_phrase_starts_from_perm() {
    for (u32 juggle : {kCirc1, kGen1, kRnd1}) {
        Host h = juggling(juggle, kTres, 3);
        bar_indices(h, 4);
        require(h.state.bar.index != 3);
        h.release(48);
        require(h.sounding() == 0);
        h.press(50);
        require(bar_indices(h, 2)[0] == 3);
        // All-notes-off (reset) too.
        h.step(3 * kBar);
        h.all_notes_off();
        h.press(48);
        require(bar_indices(h, 1)[0] == 3);
    }
    // A second key joining the phrase does not restart it.
    Host h = juggling(kGen1);
    bar_indices(h, 3);
    h.press(55);
    h.step(kBar);
    require(h.state.bar.index == 3);
}

// ---- SLIDE ----
static Host sliding(u32 slide, u32 pitch, u32 cells = kTres) {
    Host h;
    defaults(h);
    h.set(1, cells, machine::kCellSets);
    h.set(3, pitch, machine::kPitches);
    h.set(7, slide, machine::kSlides);
    return h;
}

// Pairs of notes where one is still sounding at the next one's start.
static u32 overlaps(const std::vector<Note>& notes) {
    u32 count = 0;
    for (size_t i = 0; i + 1 < notes.size(); ++i) if (notes[i].off > notes[i + 1].on) ++count;
    return count;
}

static void slide_overlaps_the_next_note() {
    // SLIDE 100%, R5 over TRES with C and G held: the two 2-step cells
    // (R then 5) tie, the last into the next bar's root.
    Host h = sliding(20, machine::RootFifth);
    h.press(48);
    h.press(55);
    for (u32 i = 0; i < 3 * kBar; ++i) {
        h.step();
        require(h.sounding() <= 2);
    }
    const std::vector<Note> notes = notes_of(h);
    require(notes.size() >= 13);
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
        const u32 cell = i % 6;
        if (cell >= 4) {
            require(notes[i].off == notes[i + 1].on + 15);  // a quarter of a 60-step 1/16
            require(notes[i].pitch != notes[i + 1].pitch);
        } else {
            require(notes[i].off <= notes[i + 1].on);
        }
    }
    // The rhythm and the pitches are unchanged: only the ends move.
    const Notes pitches = h.pitches();
    for (size_t i = 0; i < pitches.size(); ++i) require(pitches[i] == (i % 2 ? 43u : 36u));
    h.release(48);
    h.release(55);
    require(h.sounding() == 0 && h.stale_updates == 0 && h.largest_drain <= 8);
}

static void no_slide_without_a_second_key() {
    Host one = sliding(20, machine::RootFifth);
    one.press(48);
    for (u32 i = 0; i < 3 * kBar; ++i) {
        one.step();
        require(one.sounding() <= 1);
    }
    require(overlaps(notes_of(one)) == 0 && one.ons().size() == 18);
    // The same pitch again (ROOT) retriggers instead of overlapping itself.
    Host same = sliding(20, machine::Root);
    same.press(48);
    same.press(55);
    same.step(3 * kBar);
    require(overlaps(notes_of(same)) == 0 && same.ons().size() == 18);
}

static void slide_only_on_short_cells() {
    for (u32 set = 0; set < machine::kCellSetCount; ++set)
        for (u32 index = 0; index < machine::kSetOrders[set]; ++index) {
            Host h = sliding(20, machine::RootFifth, set);
            h.set(2, index, machine::kPerms);
            h.press(48);
            h.step(kStart + 1);
            for (u32 cell = 0; cell < h.state.bar.count; ++cell)
                require((h.state.bar.slides >> cell & 1) == (h.state.bar.cells[cell] <= 2));
        }
    Host off = sliding(0, machine::RootFifth);
    off.press(48);
    off.press(55);
    off.step(4 * kBar);
    require(off.state.bar.slides == 0 && overlaps(notes_of(off)) == 0);
}

static void slide_is_a_chance_per_cell_per_bar() {
    // SLIDE 50% over 400 bars of TRES: about half the 800 short cells.
    Host h = sliding(10, machine::RootFifth);
    h.press(48);
    h.step(kStart + 1);
    u32 rolled = 0;
    for (u32 bar = 0; bar < 400; ++bar) {
        require((h.state.bar.slides & 0xf) == 0);
        rolled += (h.state.bar.slides >> 4 & 1) + (h.state.bar.slides >> 5 & 1);
        const opxy::u8 dice = h.state.bar.slides;
        h.set(7, bar & 1 ? 0 : 20, machine::kSlides);  // turned mid-bar: the bar keeps its dice
        h.step(kBar / 2);
        require(h.state.bar.slides == dice);
        h.set(7, 10, machine::kSlides);  // back to 50% before the next bar line
        h.step(kBar / 2);
    }
    require(rolled > 320 && rolled < 480);
}

static void short_gate_keeps_the_overlap() {
    // 64321 at 1/32 (30 steps), GATE 10%: the long cells keep 10%, the 2-step
    // cell (5th) ties into the 1-step cell (root) for 7 steps, and that one
    // holds on into the next bar.
    Host h = sliding(20, machine::RootFifth, k64321);
    h.set(0, R32, machine::kRates);
    h.set(6, 0, machine::kGates);
    h.press(48);
    h.press(60);
    h.step(16 * 30);  // one bar: the next starts at step 480
    const std::vector<Note> notes = notes_of(h);
    require(notes.size() == 5 && notes[3].pitch == 43 && notes[4].pitch == 36);
    require(notes[0].off - notes[0].on == 18 && notes[1].off - notes[1].on == 12);
    require(notes[3].off == notes[4].on + 7 && notes[4].off == ~0u);
}

static void slide_survives_key_changes() {
    for (u32 rate = 0; rate < machine::kRates; ++rate) {
        Host h = sliding(20, machine::ChordTones, k64321);
        h.set(0, rate, machine::kRates);
        h.set(4, kRnd1, machine::kJuggles);
        h.press(48);
        h.press(55);
        h.press(60);
        for (u32 i = 0; i < 20000; ++i) {
            h.step();
            require(h.sounding() <= 2);
            if (i % 997 == 0) h.release(i % 2 ? 55 : 48);
            if (i % 997 == 500) h.press(i % 2 ? 55 : 48);
        }
        require(h.stale_updates == 0 && h.largest_drain <= 8);
        h.release(48);
        h.release(55);
        h.release(60);
        require(h.sounding() == 0);
    }
    // Releasing the key whose record holds the sliding note ends it at once.
    Host h = sliding(20, machine::RootFifth);
    h.press(48);
    h.press(55);
    h.step(kBar + 14 * 60 + 5);  // just after the last cell's note-on, while the 4th cell's note still sounds
    require(h.sounding() == 2);
    const u32 tail_root = h.state.slots[h.state.tail - 1].root;
    h.release(tail_root);
    require(h.sounding() == 1);
    h.release(tail_root == 48 ? 55 : 48);
    require(h.sounding() == 0);
}

// ---- Controls and header ----
struct Param {
    std::string id, label;
    double fallback;
    std::vector<std::string> labels;
};

// definition.json, next to tests/, read with a scanner just good enough for
// its own layout: the strings and numbers in order.
static std::vector<Param> read_definition(std::string& press) {
    std::string path = __FILE__;
    path = path.substr(0, path.find_last_of('/')) + "/../definition.json";
    std::ifstream file(path);
    require(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::vector<std::string> tokens;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') {
            const size_t end = text.find('"', i + 1);
            tokens.push_back(text.substr(i + 1, end - i - 1));
            i = end;
        } else if (text[i] == ':' ) {
            size_t j = i + 1;
            while (text[j] == ' ') ++j;
            if (text[j] == '-' || (text[j] >= '0' && text[j] <= '9')) {
                size_t end = j;
                while (end < text.size() && text[end] != ',' && text[end] != '\n' && text[end] != '}') ++end;
                tokens.push_back("#" + text.substr(j, end - j));
            }
        }
    }
    std::vector<Param> params;
    bool in_params = false;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "press") press = tokens[i + 1];
        if (t == "parameters") in_params = true;
        if (!in_params) continue;
        if (t == "id") params.push_back(Param{tokens[i + 1], "", -1, {}});
        else if (t == "label") params.back().label = tokens[i + 1];
        else if (t == "default") params.back().fallback = std::atof(tokens[i + 1].c_str() + 1);
        else if (t == "labels")
            for (size_t j = i + 1; j < tokens.size() && tokens[j] != "display"; ++j) params.back().labels.push_back(tokens[j]);
    }
    return params;
}

static std::vector<std::string> header_values(Host& h) {
    player::Display d;
    std::memset(&d, 0, sizeof d);
    machine::display(h.state, h.controls, d);
    std::vector<std::string> out;
    for (u32 i = 0; i < 8; ++i) {
        require(std::memchr(d.values[i], 0, sizeof d.values[i]));  // terminated
        out.push_back(d.values[i]);
    }
    return out;
}

static std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

static void definition_matches_the_code() {
    std::string press;
    const std::vector<Param> params = read_definition(press);
    require(press == "page");  // two pages of four
    const char* ids[] = {"rate", "cells", "perm", "pitch", "juggle", "latch", "gate", "slide"};
    const char* names[] = {"RATE", "CELLS", "PERM", "PITCH", "JUGGLE", "LATCH", "GATE", "SLIDE"};
    const u32 counts[] = {machine::kRates, machine::kCellSets, machine::kPerms, machine::kPitches,
                          machine::kJuggles, machine::kLatches, machine::kGates, machine::kSlides};
    // Defaults: 1/16, TRES, 1, ROOT, HOLD, LATCH off, 60%, 0%.
    const u32 defaults_at[] = {R16, kTres, 0, 0, kHold, 0, 10, 0};
    require(params.size() == 8);
    for (u32 i = 0; i < 8; ++i) {
        const Param& param = params[i];
        require(param.id == ids[i] && param.label == names[i] && param.labels.size() == counts[i]);
        const i32 stored = static_cast<i32>(param.fallback * 2147483647.0);
        require(player::choice(stored, counts[i]) == defaults_at[i]);
        // Every choice: the header shows the definition's label, lowercased,
        // with '-' as '/' and GATE and SLIDE as percentages.
        for (u32 choice = 0; choice < counts[i]; ++choice) {
            const std::string& label = param.labels[choice];
            require(label.size() <= 5);
            Host h;
            defaults(h);
            h.set(1, k64321, machine::kCellSets);  // PERM unclamped
            h.set(i, choice, counts[i]);
            std::string expected = lower(label);
            std::replace(expected.begin(), expected.end(), '-', '/');
            if (i == 6 || i == 7) expected = label + "%";
            require(header_values(h)[i] == expected);
        }
    }
    // The defaults as the header shows them, page 1 then page 2.
    Host h;
    defaults(h);
    require(header_values(h) == std::vector<std::string>({"1/16", "tres", "1", "root", "hold", "off", "60%", "0%"}));
}

static void perm_clamps_and_comes_back() {
    // PERM 51 in 64321; 5533 clamps it to 6 (its last order), and 64321 again
    // finds 51: in the header and in the bar that plays.
    Host h;
    defaults(h);
    h.set(1, k64321, machine::kCellSets);
    h.set(2, 50, machine::kPerms);
    require(header_values(h)[2] == "51");
    h.set(1, k5533, machine::kCellSets);
    require(header_values(h)[2] == "6");
    h.set(1, k4444, machine::kCellSets);
    require(header_values(h)[2] == "1");
    h.press(48);
    h.step(kStart + 1);
    require(h.state.bar.set == k4444 && h.state.bar.index == 0);
    h.set(1, k5533, machine::kCellSets);
    h.step(kBar);
    require(h.state.bar.index == 5);
    h.set(1, k64321, machine::kCellSets);
    h.step(kBar);
    require(h.state.bar.index == 50 && header_values(h)[2] == "51");
    require(machine::read_params(h.controls).perm == 50);  // the control kept its value throughout
    // Every set's every PERM: the header and the bar agree.
    for (u32 set = 0; set < machine::kCellSetCount; ++set)
        for (u32 perm = 0; perm < machine::kPerms; perm += 7) {
            Host g;
            defaults(g);
            g.set(1, set, machine::kCellSets);
            g.set(2, perm, machine::kPerms);
            g.press(48);
            g.step(kStart + 1);
            const u32 index = std::min(perm, machine::kSetOrders[set] - 1u);
            require(g.state.bar.index == index && header_values(g)[2] == std::to_string(index + 1));
        }
}

static void controls_change_safely_mid_bar() {
    // Every control swept through its choices while two keys play: notes stay
    // in range and nothing hangs (each change lands at a cell or bar line).
    const u32 counts[] = {machine::kRates, machine::kCellSets, machine::kPerms, machine::kPitches,
                          machine::kJuggles, machine::kLatches, machine::kGates, machine::kSlides};
    for (u32 control = 0; control < 8; ++control) {
        Host h;
        defaults(h);
        h.press(36);
        h.press(43);
        for (u32 i = 0; i < 4 * counts[control]; ++i) {
            h.set(control, i % counts[control], counts[control]);
            h.step(137);
            require(h.sounding() <= 2);
        }
        require(h.stale_updates == 0 && h.largest_drain <= 8);
        h.release(36);
        h.release(43);
        require(h.sounding() == 0);
    }
}

// ---- Screen ----
static player::Display screen(Host& h) {
    player::Display d;
    std::memset(&d, 0, sizeof d);
    machine::display(h.state, h.controls, d);
    return d;
}

static u32 starts_mask(u32 set, u32 index) {
    machine::Bar bar;
    machine::fill_bar(bar, set, index);
    return bar.starts;
}

static void screen_shows_the_bar() {
    // No key: PERM's order, no playhead, "n/count" and JUGGLE.
    Host h;
    defaults(h);
    h.set(2, 4, machine::kPerms);
    h.set(4, kGen2, machine::kJuggles);
    player::Display d = screen(h);
    require(d.lit == starts_mask(kTres, 4) && std::string(d.text) == "5/15 gen2");
    // Playing: the bar that plays, and one playhead tick on its step.
    h.press(48);
    h.step(kStart);  // the chord window: no bar yet, so PERM's order and no playhead
    require(screen(h).lit == starts_mask(kTres, 4));
    for (u32 i = 0; i < 2 * kBar + 1; ++i) {
        h.step();
        d = screen(h);
        const u32 step = h.state.step;
        require((d.lit & 0xffff) == h.state.bar.starts);
        const u32 ticks = d.lit >> 16 & 0xf, covers = d.lit >> 20 & 0xf;
        require(ticks == 1u << (step & 3) && covers == (0xfu & ~(1u << (step >> 2))));
    }
    // GEN 2 moved on after two bars: the screen shows the order that plays, not PERM.
    require(h.state.bar.index == 5);
    d = screen(h);
    require((d.lit & 0xffff) == starts_mask(kTres, 5) && std::string(d.text) == "6/15 gen2");
    // Clamped PERM, and the largest numbers.
    h.set(1, k5533, machine::kCellSets);
    h.set(2, 119, machine::kPerms);
    h.release(48);
    d = screen(h);
    require(d.lit == starts_mask(k5533, 5) && std::string(d.text) == "6/6 gen2");
    h.set(1, k64321, machine::kCellSets);
    h.set(4, kCirc4, machine::kJuggles);
    d = screen(h);
    require(std::string(d.text) == "120/120 circ4");
    // Every order: the screen marks exactly its cell starts.
    for (u32 set = 0; set < machine::kCellSetCount; ++set)
        for (u32 index = 0; index < machine::kSetOrders[set]; ++index) {
            machine::Bar bar;
            machine::fill_bar(bar, set, index);
            require(__builtin_popcount(bar.starts) == static_cast<int>(bar.count) && (bar.starts & 1));
        }
}

// ---- LATCH ----
static Host latched() {
    Host h;
    defaults(h);
    h.set(3, machine::RootFifth, machine::kPitches);
    h.set(5, 1, machine::kLatches);
    return h;
}

static void latch_keeps_playing() {
    Host h = latched();
    h.press(48);
    h.press(55);
    h.step(kBar / 2);
    h.release(48);
    h.release(55);
    require(h.passed_through == 0 && h.has_record(48) && h.has_record(55));  // records kept
    const size_t before = h.ons().size();
    h.step(2 * kBar);
    const std::vector<Event> ons = h.ons();
    require(ons.size() == before + 12);  // two more bars of TRES, without a key down
    for (size_t i = before; i < ons.size(); ++i) require(ons[i].pitch == 36 || ons[i].pitch == 43);
    require(h.stale_updates == 0 && h.sounding() <= 1);
}

static void latch_new_chord_replaces() {
    Host h = latched();
    h.press(48);
    h.press(55);
    h.release(48);
    h.release(55);
    h.step(kBar + kBar / 4);
    const u32 step = h.state.step;
    // A new gesture: F, then A while F is down. C and G end; the bar goes on.
    h.press(53);
    require(!h.has_record(48) && !h.has_record(55) && h.ended == 2);
    h.press(57);
    require(h.ended == 2 && h.has_record(53) && h.has_record(57));  // A joins F, nothing else ends
    require(h.state.step == step && h.state.juggling);  // no new phrase
    h.release(53);
    h.release(57);
    const size_t before = h.ons().size();
    h.step(kBar);
    const std::vector<Event> ons = h.ons();
    require(ons.size() > before);
    for (size_t i = before; i < ons.size(); ++i) require(ons[i].pitch == 41 || ons[i].pitch == 48);  // F's r5, an octave down
    // A latched key pressed again in a new gesture stays; the others end.
    h.press(57);
    require(h.has_record(57) && !h.has_record(53) && h.ended == 3);
    h.release(57);
    require(h.stale_updates == 0 && h.largest_drain <= 8);
}

static void latch_off_stops() {
    Host h = latched();
    h.press(48);
    h.press(52);
    h.press(55);
    h.release(52);  // let go of one while two stay down
    h.release(55);
    h.step(kBar);
    // LATCH off: the keys no longer down stop at the next drain; C plays on.
    h.set(5, 0, machine::kLatches);
    h.step(1);
    require(!h.has_record(52) && !h.has_record(55) && h.has_record(48) && h.state.active == 1);
    h.step(kBar);
    require(h.sounding() <= 1);
    h.release(48);  // now released as usual
    require(h.sounding() == 0 && !h.has_record(48) && h.state.active == 0);
    // Latched with no key down, then off: silence, and the next key is a new phrase.
    Host g = latched();
    g.press(48);
    g.release(48);
    g.step(kBar);
    g.set(5, 0, machine::kLatches);
    g.step(1);
    require(g.sounding() == 0 && g.state.active == 0 && !g.has_record(48));
    g.press(50);
    require(g.state.step == 0 && !g.state.juggling);
    require(g.stale_updates == 0);
}

static void latch_full_chords() {
    // Eight keys down, then released: the first goes as usual, so seven stay
    // latched and stock keeps a record free. Eight new keys: the seven end,
    // and every new one gets a record.
    Host h = latched();
    for (u32 i = 0; i < 8; ++i) h.press(40 + i);
    for (u32 i = 0; i < 8; ++i) h.release(40 + i);
    require(!h.has_record(40) && h.has_record(41) && __builtin_popcount(h.state.active) == 7);
    h.step(kBar);
    for (u32 i = 0; i < 8; ++i) h.press(60 + i);
    require(h.ended == 7 && h.state.active == 0xff);
    for (u32 i = 0; i < 8; ++i) require(!h.has_record(40 + i) && h.has_record(60 + i));
    for (u32 i = 0; i < 8; ++i) h.release(60 + i);
    h.step(kBar);
    require(h.stale_updates == 0 && h.largest_drain <= 8 && h.sounding() <= 1);
    h.all_notes_off();
    require(h.sounding() == 0 && h.state.active == 0);
}

static void latch_long_run() {
    // Chords come and go, LATCH and SLIDE toggle: nothing sticks or goes stale.
    Host h = latched();
    h.set(7, 20, machine::kSlides);
    h.set(4, kRnd1, machine::kJuggles);
    u32 chord = 0;
    for (u32 i = 0; i < 40000; ++i) {
        h.step();
        require(h.sounding() <= 2);
        if (i % 1501 == 0) {
            for (u32 k = 0; k < 3; ++k) h.press(40 + (chord * 5 + k * 4) % 24);
            for (u32 k = 0; k < 3; ++k) h.release(40 + (chord * 5 + k * 4) % 24);
            ++chord;
        }
        if (i % 7001 == 0) h.set(5, (i / 7001) % 2, machine::kLatches);
    }
    require(h.stale_updates == 0 && h.largest_drain <= 8);
    h.set(5, 0, machine::kLatches);
    h.step(1);
    require(h.sounding() == 0 && h.state.active == 0);
}

int main() {
    static_assert(sizeof(machine::State) <= player::state_bytes, "State fits the SDK's area");
    permutation_tables();
    reads_defaults();
    reads_the_chord();
    pattern_steps();
    walk_approach();
    interference();
    plays_the_patterns();
    new_root_from_the_next_cell();
    bar_layout();
    plays_the_cells();
    gate_full_never_overlaps();
    changes_wait_for_the_bar_line();
    root_an_octave_down();
    juggle_modes();
    hold_plays_perm();
    circular_rotates();
    general_steps_through_the_table();
    random_jumps_elsewhere();
    juggles_only_at_the_bar_line();
    turning_perm_goes_home();
    new_phrase_starts_from_perm();
    slide_overlaps_the_next_note();
    no_slide_without_a_second_key();
    slide_only_on_short_cells();
    slide_is_a_chance_per_cell_per_bar();
    short_gate_keeps_the_overlap();
    slide_survives_key_changes();
    definition_matches_the_code();
    perm_clamps_and_comes_back();
    controls_change_safely_mid_bar();
    screen_shows_the_bar();
    latch_keeps_playing();
    latch_new_chord_replaces();
    latch_off_stops();
    latch_full_chords();
    latch_long_run();
    lowest_key_is_the_root();
    folds_into_range();
    nine_keys_and_cleanup();
    long_hold_stays_bounded();
    std::printf("bass_juggler: all tests passed\n");
    return 0;
}
