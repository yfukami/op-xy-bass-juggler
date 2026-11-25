#!/usr/bin/env python3
"""Generates machines/bass_juggler/perm_tables.h: every CELLS set's distinct cell
orders, in the order PERM steps through them.

  python3 tools/gen_perm_tables.py            # write the header and print the report
  python3 tools/gen_perm_tables.py --check    # fail if the header is out of date

Neighbouring PERM values should sound almost the same, so the order is a path
through the distinct permutations in which each step swaps two adjacent,
unequal cells (an adjacent-transposition Gray code). Index 0 is the set as
written. When no such path exists, the search finds one with the fewest other
steps ("imperfect" steps), and among those the smallest jumps (fewest cells
moved).

Such a path cannot always exist: each adjacent swap flips the order's parity, so
a set whose even and odd orders differ in number by d needs at least d - 1
imperfect steps. The report prints that lower bound beside the result.
"""
import argparse
import sys
import time
from itertools import permutations
from math import factorial
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'machines/bass_juggler/perm_tables.h'

# CELLS, in control order: (label shown on the device, cells as written).
SETS = [
    ('4444', (4, 4, 4, 4)),
    ('5533', (5, 5, 3, 3)),
    ('tres', (3, 3, 3, 3, 2, 2)),
    ('7cel', (3, 3, 2, 2, 2, 2, 2)),
    ('44332', (4, 4, 3, 3, 2)),
    ('5432', (5, 4, 3, 2, 2)),
    ('43322', (4, 3, 3, 2, 2, 2)),
    ('64321', (6, 4, 3, 2, 1)),
]
MAX_CELLS = 7
SEARCH_SECONDS = 20  # per set and per allowance of imperfect steps


def multinomial(cells):
    count = factorial(len(cells))
    for size in set(cells):
        count //= factorial(cells.count(size))
    return count


def is_adjacent_swap(a, b):
    """True when b is a with two adjacent, unequal cells swapped."""
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    return len(diff) == 2 and diff[1] == diff[0] + 1 and a[diff[0]] == b[diff[1]] and a[diff[1]] == b[diff[0]]


def moved(a, b):
    return sum(x != y for x, y in zip(a, b))


def parity(order):
    """Inversion parity, counting only pairs of unequal cells."""
    return sum(order[i] > order[j] for i in range(len(order)) for j in range(i + 1, len(order))) & 1


def neighbours(order):
    out = []
    for i in range(len(order) - 1):
        if order[i] != order[i + 1]:
            swapped = list(order)
            swapped[i], swapped[i + 1] = swapped[i + 1], swapped[i]
            out.append(tuple(swapped))
    return out


def search(start, nodes, allowance, deadline):
    """A Hamiltonian path from start using at most `allowance` imperfect steps,
    preferring the smallest jumps; None when none is found in time."""
    adjacent = {node: neighbours(node) for node in nodes}
    by_distance = {node: sorted((other for other in nodes if other != node), key=lambda o: moved(node, o))
                   for node in nodes}
    path, seen = [start], {start}
    total = len(nodes)

    def unvisited_degree(node):
        return sum(1 for n in adjacent[node] if n not in seen)

    def step(left):
        if len(path) == total:
            return True
        if time.monotonic() > deadline:
            return False
        here = path[-1]
        # Perfect steps first, most constrained first (Warnsdorff's rule).
        for nxt in sorted((n for n in adjacent[here] if n not in seen), key=unvisited_degree):
            path.append(nxt)
            seen.add(nxt)
            if step(left):
                return True
            path.pop()
            seen.discard(nxt)
        if left:
            for nxt in by_distance[here]:
                if nxt in seen or is_adjacent_swap(here, nxt):
                    continue
                path.append(nxt)
                seen.add(nxt)
                if step(left - 1):
                    return True
                path.pop()
                seen.discard(nxt)
        return False

    sys.setrecursionlimit(10000)
    return list(path) if step(allowance) else None


def johnson_trotter(cells):
    """Steinhaus-Johnson-Trotter: every order of distinct cells, each one adjacent
    swap from the last, starting with the cells as written."""
    rank = list(range(len(cells)))  # positions in the written order, moved instead of the cells
    facing = [-1] * len(cells)
    out = [tuple(cells[r] for r in rank)]
    while True:
        mobile = None
        for i, r in enumerate(rank):
            j = i + facing[r]
            if 0 <= j < len(rank) and rank[j] < r and (mobile is None or r > rank[mobile]):
                mobile = i
        if mobile is None:
            return out
        r = rank[mobile]
        j = mobile + facing[r]
        rank[mobile], rank[j] = rank[j], rank[mobile]
        for other in range(r + 1, len(rank)):
            facing[other] = -facing[other]
        out.append(tuple(cells[x] for x in rank))


def build(cells):
    nodes = sorted(set(permutations(cells)))
    assert len(nodes) == multinomial(cells)
    if len(set(cells)) == len(cells):
        return johnson_trotter(cells), 0
    even = sum(1 for n in nodes if parity(n) == 0)
    bound = max(0, abs(even - (len(nodes) - even)) - 1)
    for allowance in range(bound, len(nodes)):
        path = search(tuple(cells), nodes, allowance, time.monotonic() + SEARCH_SECONDS)
        if path:
            return path, bound
    raise SystemExit(f'no path found for {cells}')


def imperfect_steps(path):
    return [i for i in range(1, len(path)) if not is_adjacent_swap(path[i - 1], path[i])]


def header(tables):
    lines = [
        '// Generated by tools/gen_perm_tables.py; do not edit.',
        '// Every CELLS set\'s distinct cell orders, in PERM order: each step swaps two',
        '// adjacent, unequal cells, except the steps counted in kSetImperfect.',
        '#pragma once',
        '',
        'namespace bass_juggler {',
        f'const opxy::u32 kMaxCells = {MAX_CELLS};',
        f'const opxy::u32 kCellSetCount = {len(tables)};',
        f'const opxy::u32 kOrderCount = {sum(len(path) for _, _, path, _ in tables)};',
        '',
        '// Per set: its number of cells, of orders, where its orders start in',
        '// kOrders, and how many of its steps are not one adjacent swap.',
        'const opxy::u8 kSetCells[kCellSetCount] = {' + ', '.join(str(len(c)) for _, c, _, _ in tables) + '};',
        'const opxy::u8 kSetOrders[kCellSetCount] = {' + ', '.join(str(len(p)) for _, _, p, _ in tables) + '};',
    ]
    offsets, at = [], 0
    for _, _, path, _ in tables:
        offsets.append(at)
        at += len(path)
    lines += [
        'const opxy::u16 kSetFirst[kCellSetCount] = {' + ', '.join(map(str, offsets)) + '};',
        'const opxy::u8 kSetImperfect[kCellSetCount] = {' + ', '.join(str(len(imperfect_steps(p))) for _, _, p, _ in tables) + '};',
        '',
        '// Cell lengths in steps, unused places 0.',
        'const opxy::u8 kOrders[kOrderCount][kMaxCells] = {',
    ]
    for label, _, path, _ in tables:
        lines.append(f'    // {label}')
        for order in path:
            padded = list(order) + [0] * (MAX_CELLS - len(order))
            lines.append('    {' + ', '.join(map(str, padded)) + '},')
    lines += ['};', '}  // namespace bass_juggler', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='fail if the header is out of date')
    args = parser.parse_args()
    tables = []
    print(f'{"set":6} {"cells":14} {"orders":>6} {"imperfect":>9} {"bound":>5}  imperfect steps (index: from -> to)')
    for label, cells in SETS:
        assert sum(cells) == 16 and len(cells) <= MAX_CELLS, label
        path, bound = build(cells)
        tables.append((label, cells, path, bound))
        bad = imperfect_steps(path)
        detail = ', '.join(f'{i}: {"-".join(map(str, path[i - 1]))} -> {"-".join(map(str, path[i]))}' for i in bad)
        print(f'{label:6} {"-".join(map(str, cells)):14} {len(path):6} {len(bad):9} {bound:5}  {detail}')
    text = header(tables)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            raise SystemExit(f'{OUTPUT.relative_to(ROOT)} is out of date: run tools/gen_perm_tables.py')
        print('up to date')
    else:
        OUTPUT.write_text(text)
        print(f'wrote {OUTPUT.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
