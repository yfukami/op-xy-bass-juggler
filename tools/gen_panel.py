#!/usr/bin/env python3
"""Generates machines/bass_juggler/panel.svg.fragment, or a standalone review SVG.

  python3 tools/gen_panel.py
  python3 tools/gen_panel.py --state tres,2,5 --out /path/to/review.svg
  python3 tools/gen_panel.py --state 64321,17,-1 --note "17/120 gen2" --out review.svg

--state is cells,order,playhead: a CELLS label, the order (1-based, as the
screen counts it) and the playhead step 0..15, or -1 when no key is held.
--note replaces the text line (by default "<order>/<count> hold").

The screen is a 16-step strip under the header:
- grey rest dots on every step, and a grey line under the bar;
- each cell start lights a white dot with a divider on the line, so the line
  reads as the cells' blocks and shows them move when the order changes;
- a tick above the step that plays.

Dots 0..15 are the starts. Dots 16..23 draw the playhead as in Euclidean Arp:
four ticks in every quarter, and four black covers that hide all quarters but
the playing one. A bound dot is one path with one paint (the SDK rejects
groups), and the page has only 24, so accents and slides are not drawn: the
blocks show which cells are long, and those (with the first) are the accents.
Everything is drawn with straight-line paths: the OP-XY drew no <circle>.
"""
import argparse
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / 'machines/bass_juggler'
OUTPUT = FOLDER / 'panel.svg.fragment'
PREFIX = 'bass_juggler'
STEPS = 16
X0, PITCH = 30, 28                 # step 0's centre and the spacing: 30..450 of 480
Y = 104                            # the dots' centre line (the header ends at y=38)
START_R, REST_R = 6.5, 2.2
LINE_Y, LINE_W = 124, 1.4          # the line under the bar
DIVIDER_UP, DIVIDER_DOWN, DIVIDER_W = 6, 4, 2.2
TICK_TOP, TICK_BOTTOM, TICK_W = 78, 87, 4
COVER_TOP, COVER_BOTTOM = 74, 91
TEXT_Y = 172


def n(value):
    text = f'{value:.3f}'.rstrip('0').rstrip('.')
    return '0' if text in ('-0', '') else text


def x_of(step):
    return X0 + PITCH * step


def at(x, y):
    return f'transform="matrix(1 0 0 1 {n(x)} {n(y)})"'


def paint(colour):
    return f'fill="{colour}" stroke="{colour}" stroke-width="1"'


def outline(points):
    return 'M' + ' L'.join(f'{n(x)} {n(y)}' for x, y in points) + ' Z'


def polygon(radius, sides, cx=0.0, cy=0.0):
    return [(cx + radius * math.sin(2 * math.pi * i / sides), cy - radius * math.cos(2 * math.pi * i / sides))
            for i in range(sides)]


def disc(radius, sides=16, cx=0.0, cy=0.0):
    return outline(polygon(radius, sides, cx, cy))


def rect(left, top, right, bottom):
    return outline([(left, top), (right, top), (right, bottom), (left, bottom)])


def start_dots():
    """Dots 0..15: a cell start's disc and its divider on the line, one white path."""
    result = []
    for step in range(STEPS):
        left = -PITCH / 2
        divider = rect(left - DIVIDER_W / 2, LINE_Y - Y - DIVIDER_UP, left + DIVIDER_W / 2, LINE_Y - Y + DIVIDER_DOWN)
        result.append(f'<path id="{PREFIX}_dot_{step}" {at(x_of(step), Y)} d="{disc(START_R)} {divider}" {paint("#FFFFFF")}/>')
    return result


def tick(step):
    x = x_of(step)
    return rect(x - TICK_W / 2, TICK_TOP, x + TICK_W / 2, TICK_BOTTOM)


def playhead_dots():
    """Dots 16..23, drawn in page coordinates (bound dots still need a transform)."""
    origin = at(0, 0)
    result = []
    for r in range(4):
        d = ' '.join(tick(r + 4 * q) for q in range(4))
        result.append(f'<path id="{PREFIX}_dot_{16 + r}" {origin} d="{d}" {paint("#FFFFFF")}/>')
    for q in range(4):
        left, right = x_of(4 * q) - PITCH / 2 + 1, x_of(4 * q + 3) + PITCH / 2 - 1
        result.append(f'<path id="{PREFIX}_dot_{20 + q}" {origin} d="{rect(left, COVER_TOP, right, COVER_BOTTOM)}" {paint("#000000")}/>')
    return result


def static_art():
    line = rect(x_of(0) - PITCH / 2, LINE_Y - LINE_W / 2, x_of(STEPS - 1) + PITCH / 2, LINE_Y + LINE_W / 2)
    end = x_of(STEPS - 1) + PITCH / 2
    closing = rect(end - DIVIDER_W / 2, LINE_Y - DIVIDER_UP, end + DIVIDER_W / 2, LINE_Y + DIVIDER_DOWN)
    rests = ' '.join(disc(REST_R, 8, x_of(step), Y) for step in range(STEPS))
    return [f'<path d="{line} {closing}" {paint("#4A4A4A")}/>',
            f'<path d="{rests}" {paint("#888888")}/>']


def text_node(content):
    return (f'<text id="{PREFIX}_text" {at(240, TEXT_Y)} fill="#FFFFFF" text-anchor="middle" '
            f'font-family="UniversNextPro-Light" font-size="26">{content}</text>')


def bound_dots():
    """Every bound dot's svg, by index, in paint order (cell starts 0..15 first)."""
    return dict(enumerate(start_dots() + playhead_dots()))


def fragment():
    body = static_art() + list(bound_dots().values()) + [text_node('1/15 hold')]
    return f'<g id="{PREFIX}_panel">\n' + '\n'.join(body) + '\n</g>\n'


# ---- Review: the screen for one bar, as display() in player.cpp lights it ----

def read_tables():
    """CELLS label -> its orders, from perm_tables.h."""
    tables, label = {}, None
    for line in (FOLDER / 'perm_tables.h').read_text().splitlines():
        match = re.match(r'\s*// (\S+)$', line)
        if match:
            label = match.group(1)
            tables[label] = []
            continue
        match = re.match(r'\s*\{([\d, ]+)\},$', line)
        if match and label:
            tables[label].append([int(v) for v in match.group(1).split(',') if int(v)])
    return tables


def starts_of(order):
    out, step = [], 0
    for cell in order:
        out.append(step)
        step += cell
    return out


def lit_mask(order, playhead):
    mask = 0
    for start in starts_of(order):
        mask |= 1 << start
    if playhead >= 0:
        mask |= 1 << (16 + playhead % 4)
        for q in range(4):
            if q != playhead // 4:
                mask |= 1 << (20 + q)
    return mask


def review(order, playhead, text):
    mask = lit_mask(order, playhead)
    shown = [svg for index, svg in bound_dots().items() if mask >> index & 1]
    body = '\n'.join(static_art() + shown + [text_node(text)])
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="960" height="444" viewBox="0 0 480 222">\n'
            '<rect width="480" height="222" fill="#000000"/>\n'
            '<rect width="480" height="38" fill="#1C1C1C"/>\n' + body + '\n</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', help='cells,order,playhead, e.g. tres,2,5')
    parser.add_argument('--note', help='the text line (default "<order>/<count> hold")')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    tables = read_tables()
    if args.state:
        if not args.out:
            parser.error('--state needs --out')
        label, number, playhead = args.state.split(',')
        orders = tables[label.lower()]
        number, playhead = int(number), int(playhead)
        if not 1 <= number <= len(orders) or not -1 <= playhead < STEPS:
            parser.error(f'{label} has orders 1..{len(orders)}; the playhead is -1..15')
        text = args.note if args.note is not None else f'{number}/{len(orders)} hold'
        args.out.write_text(review(orders[number - 1], playhead, text), encoding='utf-8')
    else:
        (args.out or OUTPUT).write_text(fragment(), encoding='utf-8')


if __name__ == '__main__':
    main()
