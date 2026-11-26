// Bass Juggler: a monophonic bassline whose rhythm is a permutation of cells.
//
// A player has no note-on/note-off output and no clock callback, so (as in
// Euclidean Arp):
// - each sounding note lives in the chord record of a held key (its carrier);
// - a held key that is silent keeps its record with a far-future queued tone,
//   because an empty Replace would release the root for good;
// - update() is drained before every player step and after every press and
//   release, so press and release mark their drain and the others are the clock.
#include <opxy/divide.hpp>
#include <opxy/player.hpp>

namespace bass_juggler {
using opxy::i32;
using opxy::u8;
using opxy::u16;
using opxy::u32;
using opxy::player::Controls;
using opxy::player::Display;
using opxy::player::Plan;
using opxy::player::Tone;
using opxy::player::Update;
}  // namespace bass_juggler

#include "perm_tables.h"

namespace bass_juggler {
const u32 kSlots = opxy::player::max_tones;  // the SDK holds at most eight roots
const u32 kBarSteps = 16;                    // a bar is 16 RATE steps
const u32 kDrainLimit = 8;                   // the SDK's updates per drain
const u16 kParkedDelay = 65535;              // player steps before a parked tone would sound
const u32 kRearmInterval = 32768;            // parked tones are renewed well before that
const u32 kDefaultChordWindow = 7;           // player steps, about 15 ms at 120 BPM

const u32 kRates = 9;
const u32 kCellSets = kCellSetCount;
const u32 kPerms = 120;  // PERM's choices: the largest set's permutation count
const u32 kPitches = 23;
const u32 kJuggles = 10;
const u32 kLatches = 2;
const u32 kGates = 19;
const u32 kSlides = 21;

// PITCH: the bass patterns, each moving on one entry per cell. All but the
// patterns before ChordTones are fixed sequences (kSequences); the rest use the held notes.
enum Pattern {
    Root, Octave, RootOctaveOctave, RootOctaveOctaveRoot, RootOctaveSeventh, RootOctaveRootFifth, RootFifth,
    RootLowFifth, RootFifthFifth, RootFifthFifthRoot, RootFifthOctave, RootFifthOctaveFifth, RootFifthSeventh,
    RootFifthSeventhFifth, RootFifthThirdFifth, Pentatonic, MajorPentatonic,
    ChordTones, ChordDown, ChordUpDown, Pedal, RandomTone, Walk
};
const u32 kSequenceCount = ChordTones;
// Whether each bar starts its pattern again from the root. false lets the
// pattern run on across bars, so a pattern whose length differs from the bar's
// cell count drifts across the rhythm (Schillinger's interference). A constant
// for now; read_params is the one place that would read it from a control.
const bool kPitchResetEachBar = true;
const i32 kFifth = 7;
const i32 kOctave = 12;
const i32 kDefaultSeventh = 10;  // a minor seventh when no 7th is held
const i32 kDefaultThird = 4;     // a major third when no 3rd is held
const i32 kBassOctave = -1;      // the bassline sits an octave below the keys

// The fixed patterns, in semitones from the root; the two entries below stand
// for the 7th and the 3rd, held or default.
const u32 kMaxSequence = 6;
const signed char kSeventhEntry = 100;
const signed char kThirdEntry = 101;
struct Sequence {
    u8 length;
    signed char steps[kMaxSequence];
};
const Sequence kSequences[kSequenceCount] = {
    {1, {0}},                         // root
    {2, {0, 12}},                     // oct
    {3, {0, 12, 12}},                 // 188
    {4, {0, 12, 12, 0}},              // 1881
    {4, {0, 12, kSeventhEntry, 12}},  // r8b7
    {4, {0, 12, 0, 7}},               // r8r5
    {2, {0, 7}},                      // r5
    {2, {0, -5}},                     // r5lo: the fifth below
    {3, {0, 7, 7}},                   // r55
    {4, {0, 7, 7, 0}},                // r55r
    {3, {0, 7, 12}},                  // r58
    {4, {0, 7, 12, 7}},               // r585
    {4, {0, 7, kSeventhEntry, 12}},   // r5b7
    {4, {0, 7, kSeventhEntry, 7}},    // r5b75
    {4, {0, 7, kThirdEntry, 7}},      // r535
    {6, {0, 3, 5, 7, 10, 12}},        // pent: minor pentatonic
    {6, {0, 2, 4, 7, 9, 12}},         // maj: major pentatonic
};

// Accents: the bar's first cell and every long cell play louder.
const u8 kAccentVelocity = 120;
const u8 kPlainVelocity = 90;
const u32 kAccentCellSteps = 4;  // cells at least this long are accented

// JUGGLE: HOLD, then CIRC, GEN and RND, each every 1, 2 or 4 bars.
enum Juggle { Hold, Circular, General, Random };
const u32 kJugglePeriods = 3;
const u8 kJugglePeriod[kJugglePeriods] = {1, 2, 4};
const u32 kRandomSeed = 0x2545f491;  // any nonzero start for the generator

// SLIDE: the chance that a short cell ties into the next note, holding on a
// quarter step past its note-on so a legato synth slides. The overlapping note
// must sound on another held key's record (a Replace on its own record would
// end it), so a slide needs two held keys.
const u32 kSlideCellSteps = 2;    // cells at most this long can slide
const u32 kSlideOverlapParts = 4;  // the overlap: 1/4 of a RATE step

struct Slot {
    u8 root, velocity, next_pitch, next_velocity;
    u32 order;
};

// The bar that is playing: its cells, fixed at the bar line.
struct Bar {
    u8 cells[kMaxCells];  // cell lengths in steps, in playing order
    u8 count;             // cells in the bar
    u8 set, index;        // CELLS and the order's index in its table
    u8 accents;           // bit k: cell k is accented
    u8 slides;            // bit k: cell k ties into the next note (rolled at the bar line)
    u16 starts;           // bit k: a cell starts on step k
};

// The held keys as offsets from the root (the lowest key), low to high.
struct Tones {
    i32 offsets[kSlots];
    u32 count;
    i32 seventh;  // the held 7th (10 or 11 above the root, within its octave), else kDefaultSeventh
    i32 third;    // the held 3rd (3 or 4), else kDefaultThird
};

struct State {
    Slot slots[kSlots];
    Bar bar;
    u32 pitch_pos;  // the pattern's next position
    u32 random;     // xorshift state; 0 until first used
    u8 home_cells, home_perm;  // CELLS and PERM as the last bar line read them
    u8 juggle_bars;            // bars played since the last juggle
    u8 juggling;               // 1 once this phrase's first bar has started
    u8 tie_next;               // 1 while the sounding note should overlap the next one
    u8 tail;                   // the slot still sounding a slid note, plus one
    u32 tail_left;             // player steps until the slid note ends
    u32 order_counter;
    u32 step, tick, gate_left, rearm, start_wait;
    u8 active, sounding, off_mask, on_mask;
    u8 started, event_pending, draining, drain_calls;
    u8 down;        // keys physically held (active also holds latched keys)
    u8 drops[kSlots];  // roots of latched keys let go, whose records update() ends
    u8 drop_count;
    u8 latch;       // LATCH as the last drain read it (release() gets no controls)
    u8 waiting, carrier;  // carrier: the slot sounding the note, plus one
};

struct Params {
    u32 rate, cells, perm, pitch, juggle, gate, slide;
    bool latch;
    bool reset_each_bar;
};

inline u32 smaller(u32 a, u32 b) { return a < b ? a : b; }
// quotient16's divide steps fail when both operands share a register, which the
// compiler chooses when it can prove them equal (100 / 100 gave the "e" in "e00%").
inline u32 divide(u32 n, u32 d) { return n == d ? 1 : opxy::divide::quotient16(n, d); }
inline u32 modulo(u32 n, u32 d) { return n - d * divide(n, d); }

inline Params read_params(const Controls& c) {
    using opxy::player::choice;
    // The same nine rates as Euclidean Arp, in player steps.
    static const u16 rates[kRates] = {320, 240, 160, 120, 80, 60, 40, 30, 20};
    Params p;
    p.rate = rates[choice(c.value[0], kRates)];
    p.cells = choice(c.value[1], kCellSets);
    p.perm = choice(c.value[2], kPerms);
    p.pitch = choice(c.value[3], kPitches);
    p.juggle = choice(c.value[4], kJuggles);
    p.latch = choice(c.value[5], kLatches) == 1;
    p.gate = 10 + 5 * choice(c.value[6], kGates);
    p.slide = 5 * choice(c.value[7], kSlides);
    p.reset_each_bar = kPitchResetEachBar;
    return p;
}

inline u32 find_slot(const State& s, u32 note) {
    for (u32 i = 0; i < kSlots; ++i)
        if ((s.active >> i & 1) && s.slots[i].root == note) return i;
    return kSlots;
}

inline u32 lowest_key(const State& s) {
    u32 lowest = kSlots;
    for (u32 i = 0; i < kSlots; ++i)
        if ((s.active >> i & 1) && (lowest == kSlots || s.slots[i].root < s.slots[lowest].root)) lowest = i;
    return lowest;
}

// The held keys relative to the lowest, which is the root (offset 0).
inline Tones read_tones(const State& s, u32 root_slot) {
    Tones t;
    t.count = 0;
    t.seventh = kDefaultSeventh;
    t.third = kDefaultThird;
    bool found_seventh = false, found_third = false;
    const i32 root = s.slots[root_slot].root;
    for (u32 i = 0; i < kSlots; ++i) {
        if (!(s.active >> i & 1)) continue;
        const i32 offset = static_cast<i32>(s.slots[i].root) - root;
        u32 at = t.count++;
        for (; at && t.offsets[at - 1] > offset; --at) t.offsets[at] = t.offsets[at - 1];
        t.offsets[at] = offset;
    }
    for (u32 i = 0; i < t.count && !found_seventh; ++i) {
        const i32 interval = static_cast<i32>(modulo(static_cast<u32>(t.offsets[i]), 12));
        if (interval == 10 || interval == 11) {
            t.seventh = interval;
            found_seventh = true;
        }
    }
    for (u32 i = 0; i < t.count && !found_third; ++i) {
        const i32 interval = static_cast<i32>(modulo(static_cast<u32>(t.offsets[i]), 12));
        if (interval == 3 || interval == 4) {
            t.third = interval;
            found_third = true;
        }
    }
    return t;
}

// CHRD and WALK climb the held notes; a single key climbs root, fifth, octave.
inline u32 climb_length(const Tones& t) { return t.count >= 2 ? t.count : 3; }
inline i32 climb(const Tones& t, u32 at) {
    static const i32 single[3] = {0, kFifth, kOctave};
    return t.count >= 2 ? t.offsets[at] : single[at];
}

inline u32 pattern_length(u32 pattern, const Tones& t) {
    if (pattern < kSequenceCount) return kSequences[pattern].length;
    const u32 n = climb_length(t);
    if (pattern == ChordUpDown) return n > 2 ? 2 * n - 2 : n;  // the ends once: C E G E
    if (pattern == Pedal) return 2 * (n - 1);                  // the root between each other note
    return n;
}

// The pattern's offset from the root at position `pos` (any count; it wraps).
inline i32 pattern_offset(u32 pattern, const Tones& t, u32 pos) {
    const u32 at = modulo(pos, pattern_length(pattern, t));
    const u32 n = climb_length(t);
    if (pattern == ChordDown) return climb(t, n - 1 - at);
    if (pattern == ChordUpDown) return climb(t, at < n ? at : 2 * n - 2 - at);
    if (pattern == Pedal) return at & 1 ? climb(t, 1 + (at >> 1)) : 0;
    if (pattern >= kSequenceCount) return climb(t, at);
    const signed char step = kSequences[pattern].steps[at];
    return step == kSeventhEntry ? t.seventh : step == kThirdEntry ? t.third : step;
}

// A cell's offset. WALK's last cell of the bar is instead an approach note a
// semitone below the next bar's first note: the root again when each bar
// restarts the pattern, otherwise the position after this one.
inline i32 cell_offset(u32 pattern, const Tones& t, u32 pos, bool last_cell, bool reset_each_bar) {
    if (pattern == Walk && last_cell) return pattern_offset(Walk, t, reset_each_bar ? 0 : pos + 1) - 1;
    return pattern_offset(pattern, t, pos);
}

// Folds a pitch into MIDI's range by whole octaves.
inline u32 fold(i32 pitch) {
    while (pitch < 0) pitch += 12;
    while (pitch > 127) pitch -= 12;
    return static_cast<u32>(pitch);
}

inline void queue_note(State& s, u32 slot, u32 pitch, u32 velocity) {
    const u8 bit = static_cast<u8>(1u << slot);
    s.slots[slot].next_pitch = static_cast<u8>(pitch);
    s.slots[slot].next_velocity = static_cast<u8>(velocity);
    s.on_mask |= bit;
    s.off_mask &= static_cast<u8>(~bit);  // its Replace already ends the old note
}

// ---- Rhythm: a bar of cells, each playing on its first step ----

// The order PERM chooses, clamped to the set's orders. The control keeps its
// own value, so a larger set chosen again finds it.
inline u32 home_index(const Params& p) { return smaller(p.perm, kSetOrders[p.cells] - 1u); }

// Fills a bar from one table entry: where its cells start and which are accented.
inline void fill_bar(Bar& bar, u32 set, u32 index) {
    const u8* order = kOrders[kSetFirst[set] + index];
    bar.set = static_cast<u8>(set);
    bar.index = static_cast<u8>(index);
    bar.count = kSetCells[set];
    bar.starts = 0;
    bar.accents = 0;
    bar.slides = 0;
    u32 step = 0;
    for (u32 i = 0; i < bar.count; ++i) {
        bar.cells[i] = order[i];
        bar.starts = static_cast<u16>(bar.starts | (1u << step));
        if (i == 0 || order[i] >= kAccentCellSteps) bar.accents = static_cast<u8>(bar.accents | (1u << i));
        step += order[i];
    }
}

// The cell starting on `step`, or kMaxCells when none does.
inline u32 cell_starting(const Bar& bar, u32 step) {
    if (!(bar.starts >> step & 1)) return kMaxCells;
    u32 cell = 0;
    for (u32 below = bar.starts & ((1u << step) - 1u); below; below &= below - 1u) ++cell;
    return cell;
}

// JUGGLE's kind and how many bars pass between its changes.
inline u32 juggle_kind(u32 juggle) { return juggle ? 1 + (juggle - 1) / kJugglePeriods : Hold; }
inline u32 juggle_period(u32 juggle) { return juggle ? kJugglePeriod[(juggle - 1) % kJugglePeriods] : 1; }

// The index of a cell order in its set's table, or the set's count if absent
// (never: the tables hold every order).
inline u32 find_order(u32 set, const u8* cells) {
    const u32 count = kSetOrders[set];
    const u32 length = kSetCells[set];
    for (u32 i = 0; i < count; ++i) {
        const u8* order = kOrders[kSetFirst[set] + i];
        u32 same = 0;
        while (same < length && order[same] == cells[same]) ++same;
        if (same == length) return i;
    }
    return count;
}

inline u32 next_random(State& s) {
    u32 x = s.random ? s.random : kRandomSeed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.random = x;
    return x;
}

// The order after `index`: CIRC moves the first cell to the end (the table's
// entry for that rotation), GEN takes the table's next entry, and RND any
// other entry.
inline u32 juggled(State& s, u32 set, u32 index, u32 kind) {
    const u32 count = kSetOrders[set];
    if (count < 2) return index;
    if (kind == Circular) {
        const u8* order = kOrders[kSetFirst[set] + index];
        const u32 length = kSetCells[set];
        u8 rotated[kMaxCells];
        for (u32 i = 0; i < length; ++i) rotated[i] = order[i + 1 < length ? i + 1 : 0];
        const u32 found = find_order(set, rotated);
        return found < count ? found : index;
    }
    if (kind == General) return index + 1 < count ? index + 1 : 0;
    // A uniform pick among the other count - 1 entries, without a division.
    const u32 pick = ((next_random(s) >> 16) * (count - 1)) >> 16;
    return pick >= index ? pick + 1 : pick;
}

// At each bar line the bar takes its cells: PERM's order (the home) when the
// phrase starts, when CELLS or PERM was turned, or under HOLD; otherwise
// JUGGLE moves on from the order that played, every 1, 2 or 4 bars. Changes
// made during a bar wait for the next one.
inline void start_bar(State& s, const Params& p) {
    const bool turned = !s.juggling || p.cells != s.home_cells || p.perm != s.home_perm;
    s.juggling = 1;
    s.home_cells = static_cast<u8>(p.cells);
    s.home_perm = static_cast<u8>(p.perm);
    const u32 kind = juggle_kind(p.juggle);
    u32 index = s.bar.index;
    if (turned || kind == Hold) {
        index = home_index(p);
        s.juggle_bars = 0;
    } else if (++s.juggle_bars >= juggle_period(p.juggle)) {
        s.juggle_bars = 0;
        index = juggled(s, p.cells, index, kind);
    }
    fill_bar(s.bar, p.cells, index);
    // SLIDE's dice: once per short cell, for this bar.
    if (p.slide)
        for (u32 i = 0; i < s.bar.count; ++i)
            if (s.bar.cells[i] <= kSlideCellSteps && ((next_random(s) >> 16) * 100 >> 16) < p.slide)
                s.bar.slides = static_cast<u8>(s.bar.slides | (1u << i));
    if (p.reset_each_bar) s.pitch_pos = 0;
}

// The lowest held key other than `skip`, or kSlots when none is.
inline u32 lowest_key_except(const State& s, u32 skip) {
    u32 lowest = kSlots;
    for (u32 i = 0; i < kSlots; ++i)
        if (i != skip && (s.active >> i & 1) && (lowest == kSlots || s.slots[i].root < s.slots[lowest].root)) lowest = i;
    return lowest;
}

// Plays a cell: the pattern's next note, read from the keys held now, on the
// lowest held key's record, held for GATE of the cell. After a sliding cell the
// old note keeps sounding (as the tail) and this one goes on another key's
// record; with one key held, or the same pitch again, the old note just ends.
inline void trigger(State& s, const Params& p, u32 cell) {
    const u32 root_slot = lowest_key(s);
    const u32 pos = s.pitch_pos++;
    const bool tie = s.tie_next;
    s.tie_next = 0;
    if (root_slot == kSlots) {
        s.off_mask |= s.sounding;
        return;
    }
    const Tones tones = read_tones(s, root_slot);
    // RTONE: any of the held notes (or root, fifth, octave), at random.
    const i32 offset = p.pitch == RandomTone
                           ? climb(tones, ((next_random(s) >> 16) * climb_length(tones)) >> 16)
                           : cell_offset(p.pitch, tones, pos, cell + 1 == s.bar.count, p.reset_each_bar);
    const u32 pitch = fold(static_cast<i32>(s.slots[root_slot].root) + offset + 12 * kBassOctave);
    u32 slot = root_slot;
    u8 keep = 0;
    const u32 old = s.carrier ? s.carrier - 1u : kSlots;
    s.tail = 0;
    if (tie && old < kSlots && (s.sounding >> old & 1) && s.gate_left && s.slots[old].next_pitch != pitch) {
        const u32 other = lowest_key_except(s, old);
        if (other < kSlots) {
            slot = other;
            keep = static_cast<u8>(1u << old);
            s.tail = static_cast<u8>(old + 1);
            s.tail_left = s.gate_left;
        }
    }
    s.off_mask |= static_cast<u8>(s.sounding & ~keep);
    queue_note(s, slot, pitch, (s.bar.accents >> cell & 1) ? kAccentVelocity : kPlainVelocity);
    s.carrier = static_cast<u8>(slot + 1);
    if (s.bar.slides >> cell & 1) {
        // The whole cell and a quarter step into the next note, whatever GATE says.
        const u32 overlap = divide(p.rate, kSlideOverlapParts);
        s.gate_left = s.bar.cells[cell] * p.rate + (overlap ? overlap : 1);
        s.tie_next = 1;
        return;
    }
    const u32 gate = divide(s.bar.cells[cell] * p.rate * p.gate, 100);
    s.gate_left = gate ? gate : 1;
}

inline void play_step(State& s, const Params& p) {
    if (s.step == 0) start_bar(s, p);
    const u32 cell = cell_starting(s.bar, s.step);
    if (cell < kMaxCells) trigger(s, p, cell);
}

inline u32 count_bits(u32 mask) {
    u32 count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}

// How long a key that arrives alone waits for the rest of a hand-played chord:
// about 15 ms (a player step lasts 2,500,000 / bpm_x10 microseconds), always
// under half a RATE step, so it cannot reach the next step.
inline u32 chord_window(u32 bpm_x10, u32 rate) {
    u32 window = bpm_x10 ? divide(bpm_x10 * 3, 500) : kDefaultChordWindow;
    if (window < 1) window = 1;
    return smaller(window, rate / 2);
}

inline void advance_clock(State& s, const Params& p) {
    const u8 tail_bit = s.tail ? static_cast<u8>(1u << (s.tail - 1)) : 0;
    if (s.tail && --s.tail_left == 0) {
        s.off_mask |= static_cast<u8>(s.sounding & tail_bit);
        s.tail = 0;
    }
    if (s.gate_left && --s.gate_left == 0) s.off_mask |= static_cast<u8>(s.sounding & ~tail_bit);
    if (++s.rearm >= kRearmInterval) {
        s.rearm = 0;
        s.off_mask |= static_cast<u8>(s.active & ~s.sounding);
    }
    if (++s.tick < p.rate) return;
    s.tick = 0;
    if (++s.step >= kBarSteps) s.step = 0;
    play_step(s, p);
}

inline void clock_step(State& s, const Controls& c) {
    if (!s.active) return;
    const Params p = read_params(c);
    if (!s.started) {
        s.started = 1;
        // Keys that arrived together (a sequenced chord) play at once. A key that
        // arrived alone waits about 15 ms for the rest of a hand-played chord, so
        // the root is the chord's lowest note. The steps still count from the first key.
        if (count_bits(s.active) >= 2) {
            play_step(s, p);
        } else {
            s.waiting = 1;
            s.start_wait = chord_window(c.bpm_x10, p.rate);
        }
        return;
    }
    if (s.waiting) {
        ++s.tick;
        if (--s.start_wait) return;
        s.waiting = 0;
        play_step(s, p);
        return;
    }
    advance_clock(s, p);
}

inline u32 lowest_bit(u32 mask) {
    u32 i = 0;
    while (!(mask >> i & 1)) ++i;
    return i;
}

inline void park(Tone& tone, u32 root) {
    tone.pitch = static_cast<u8>(root);
    tone.velocity = 1;
    tone.delay = kParkedDelay;
}

// Stops using keys: they leave the chord and nothing more is sent to them.
inline void forget(State& s, u8 mask) {
    const u8 keep = static_cast<u8>(~mask);
    s.active &= keep;
    s.down &= keep;
    s.sounding &= keep;
    s.off_mask &= keep;
    s.on_mask &= keep;
    if (s.carrier && (mask >> (s.carrier - 1) & 1)) {
        s.carrier = 0;
        s.gate_left = 0;
        s.tie_next = 0;
    }
    if (s.tail && (mask >> (s.tail - 1) & 1)) s.tail = 0;
    if (!s.active) s.gate_left = 0;
}

// Lets go of latched keys: forgets them now and queues their roots, whose
// records update() ends with an empty Replace (which stock releases for good).
inline void let_go(State& s, u8 mask) {
    for (u32 i = 0; i < kSlots; ++i)
        if ((mask >> i & 1) && s.drop_count < kSlots) s.drops[s.drop_count++] = s.slots[i].root;
    forget(s, mask);
}

bool press(State& s, const Controls& c, u32 note, u32 velocity, Plan& plan) {
    plan.count = 0;
    if (note > 127) return false;
    s.event_pending = 1;
    s.latch = read_params(c).latch;
    const bool first = !s.active;
    u32 slot = find_slot(s, note);
    // LATCH: the first key of a new gesture (no key down) replaces the latched
    // chord; a latched key pressed again stays. The phrase goes on.
    if (s.latch && !s.down) let_go(s, static_cast<u8>(s.active & ~(slot < kSlots ? 1u << slot : 0u)));
    if (slot == kSlots) {
        if (s.active == 0xff) return true;  // the SDK cannot hold a ninth root
        slot = lowest_bit(static_cast<u8>(~s.active));
    }
    const u8 bit = static_cast<u8>(1u << slot);
    Slot& key = s.slots[slot];
    key.root = static_cast<u8>(note);
    key.velocity = static_cast<u8>(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
    key.order = ++s.order_counter;
    s.active |= bit;
    s.down |= bit;
    s.sounding &= static_cast<u8>(~bit);  // the plan replaces anything this root held
    if (s.carrier == slot + 1) s.carrier = 0;
    if (s.tail == slot + 1) s.tail = 0;
    s.off_mask &= static_cast<u8>(~bit);
    s.on_mask &= static_cast<u8>(~bit);
    plan.count = 1;
    park(plan.tones[0], note);
    if (!first) return true;

    s.step = s.tick = s.gate_left = s.rearm = s.start_wait = s.pitch_pos = 0;
    s.started = s.waiting = s.carrier = s.juggling = s.tie_next = s.tail = 0;  // a new phrase starts from PERM
    s.random = s.random * 31u + note * 131u + key.velocity;  // the keys vary RND's sequence
    return true;
}

bool release(State& s, u32 note) {
    s.event_pending = 1;
    const u32 slot = find_slot(s, note);
    if (slot == kSlots) return true;  // a key press() could not hold
    s.down &= static_cast<u8>(~(1u << slot));
    // LATCH keeps the key and its record, which goes on sounding the bass
    // (an experiment: the SDK promises this only for keys that sound nothing).
    // At most seven stay, so a new chord's first key always finds a free record.
    if (s.latch && s.active != 0xff) return true;
    forget(s, static_cast<u8>(1u << slot));
    return false;  // stock releases this root's notes
}

bool update(State& s, const Controls& c, Update& change) {
    if (!s.draining) {
        s.draining = 1;
        s.drain_calls = 0;
        s.latch = read_params(c).latch;
        // LATCH turned off: latched keys no longer down stop.
        if (!s.latch && (s.active & ~s.down)) let_go(s, static_cast<u8>(s.active & ~s.down));
        if (s.event_pending) s.event_pending = 0;
        else clock_step(s, c);
    }
    if (s.drop_count) {
        change.mode = opxy::player::Replace;
        change.root = s.drops[--s.drop_count];
        change.plan.count = 0;  // ends the record for good
        if (++s.drain_calls >= kDrainLimit) s.draining = 0;
        return true;
    }
    if (!s.off_mask && !s.on_mask) {
        s.draining = 0;
        return false;
    }
    change.mode = opxy::player::Replace;
    change.plan.count = 1;
    if (s.off_mask) {
        const u32 slot = lowest_bit(s.off_mask);
        const u8 bit = static_cast<u8>(1u << slot);
        s.off_mask &= static_cast<u8>(~bit);
        s.sounding &= static_cast<u8>(~bit);
        change.root = s.slots[slot].root;
        park(change.plan.tones[0], change.root);
    } else {
        const u32 slot = lowest_bit(s.on_mask);
        const u8 bit = static_cast<u8>(1u << slot);
        s.on_mask &= static_cast<u8>(~bit);
        s.sounding |= bit;
        change.root = s.slots[slot].root;
        change.plan.tones[0].pitch = s.slots[slot].next_pitch;
        change.plan.tones[0].velocity = s.slots[slot].next_velocity;
        change.plan.tones[0].delay = 0;
    }
    if (++s.drain_calls >= kDrainLimit) s.draining = 0;  // the SDK stops calling here
    return true;
}

// Stock reset and all-notes-off: stock has already silenced every record.
void reset(State& s) { __builtin_memset(&s, 0, sizeof s); }

inline void append(char* out, u32 size, u32& at, const char* text) {
    while (*text && at + 1 < size) out[at++] = *text++;
    out[at] = 0;
}

inline void append_number(char* out, u32 size, u32& at, u32 value) {
    char digits[4] = {0, 0, 0, 0};
    u32 count = 0;
    if (value >= 100) digits[count++] = static_cast<char>('0' + divide(value, 100));
    if (value >= 10) digits[count++] = static_cast<char>('0' + modulo(divide(value, 10), 10));
    digits[count] = static_cast<char>('0' + modulo(value, 10));
    append(out, size, at, digits);
}

// Writes a number and a suffix into one header value.
inline void show_number(char* out, u32 value, const char* suffix) {
    u32 at = 0;
    out[0] = 0;
    append_number(out, 8, at, value);
    append(out, 8, at, suffix);
}

// The header: page 1 RATE, CELLS, PERM, PITCH; page 2 JUGGLE, OCTAVE, GATE,
// SLIDE. Short lowercase words, which fit the header's columns (see CLAUDE.md).
// PERM shows the order it names in the current set (clamped), counted from 1.
void display(const State& s, const Controls& c, Display& d) {
    static const char rates[kRates][6] = {"1/2t", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32", "1/32t"};
    static const char cells[kCellSets][6] = {"4444", "5533", "tres", "7cel", "44332", "5432", "43322", "64321"};
    static const char pitches[kPitches][6] = {"root", "oct",  "188",  "1881", "r8b7", "r8r5",  "r5",   "r5lo",
                                              "r55",  "r55r", "r58",  "r585", "r5b7", "r5b75", "r535", "pent",
                                              "maj",  "chrd", "dn",   "ud",   "pedl", "rtone", "walk"};
    static const char juggles[kJuggles][6] = {"hold", "circ1", "circ2", "circ4", "gen1",
                                              "gen2", "gen4", "rnd1", "rnd2", "rnd4"};
    const Params p = read_params(c);
    u32 at = 0;
    append(d.values[0], sizeof d.values[0], at, rates[opxy::player::choice(c.value[0], kRates)]);
    at = 0;
    append(d.values[1], sizeof d.values[1], at, cells[p.cells]);
    show_number(d.values[2], home_index(p) + 1, "");
    at = 0;
    append(d.values[3], sizeof d.values[3], at, pitches[p.pitch]);
    at = 0;
    append(d.values[4], sizeof d.values[4], at, juggles[p.juggle]);
    at = 0;
    append(d.values[5], sizeof d.values[5], at, p.latch ? "on" : "off");
    show_number(d.values[6], p.gate, "%");
    show_number(d.values[7], p.slide, "%");

    // The bar that plays, or PERM's order while no phrase plays.
    Bar home;
    const bool playing = s.active && s.juggling;
    if (!playing) fill_bar(home, p.cells, home_index(p));
    const Bar& bar = playing ? s.bar : home;
    // Dots 0..15 mark cell starts; the artwork sizes them by accent (see
    // tools/gen_panel.py). Dots 16..19 each draw a playhead tick in all four
    // quarters of the bar, and dots 20..23 cover a quarter's ticks, leaving one.
    d.lit = bar.starts;
    if (playing) {
        d.lit |= 1u << (16 + (s.step & 3));
        for (u32 quarter = 0; quarter < 4; ++quarter)
            if (quarter != s.step >> 2) d.lit |= 1u << (20 + quarter);
    }
    // The order that plays, of the set's, and JUGGLE: "17/60 gen2".
    at = 0;
    d.text[0] = 0;
    append_number(d.text, sizeof d.text, at, bar.index + 1u);
    append(d.text, sizeof d.text, at, "/");
    append_number(d.text, sizeof d.text, at, kSetOrders[bar.set]);
    append(d.text, sizeof d.text, at, " ");
    append(d.text, sizeof d.text, at, juggles[p.juggle]);
}
}  // namespace bass_juggler
