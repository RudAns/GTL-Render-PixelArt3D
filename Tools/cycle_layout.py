"""Search a layout of three axis-aligned beams that forms a precedence cycle under the demo camera
(yaw 0, pitch 41.41 deg) while the beams never intersect in 3D. Mirrors UPixelArtSubsystem::InFront
and the screen-rect overlap test, so whatever this prints is what the game will see.
"""
import itertools
import math
import random

import sys
P = math.radians(41.41)
Y = math.radians(float(sys.argv[1]) if len(sys.argv) > 1 else 0.0)  # camera yaw
F = (math.cos(P) * math.cos(Y), math.cos(P) * math.sin(Y), -math.sin(P))
R = (-math.sin(Y), math.cos(Y), 0.0)
U = (math.sin(P) * math.cos(Y), math.sin(P) * math.sin(Y), math.cos(P))
TEXEL = 6.25


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def corners(box):
    (x0, y0, z0), (x1, y1, z1) = box
    return [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]


def rect(box):
    xs = [dot(c, R) / TEXEL for c in corners(box)]
    ys = [dot(c, U) / TEXEL for c in corners(box)]
    return min(xs), max(xs), min(ys), max(ys)


def overlap(a, b):
    ra, rb = rect(a), rect(b)
    w = min(ra[1], rb[1]) - max(ra[0], rb[0])
    h = min(ra[3], rb[3]) - max(ra[2], rb[2])
    return w * h if w > 0 and h > 0 else 0.0


def in_front(a, b):
    best, gap_best = -1, -1.0
    for k in range(3):
        fk = F[k]
        if abs(fk) < 1e-6:
            continue
        if a[1][k] <= b[0][k]:
            g = (b[0][k] - a[1][k]) * abs(fk) + 1e-3
            if g > gap_best:
                gap_best, best = g, (1 if fk > 0 else 0)
        if b[1][k] <= a[0][k]:
            g = (a[0][k] - b[1][k]) * abs(fk) + 1e-3
            if g > gap_best:
                gap_best, best = g, (1 if fk < 0 else 0)
    if best >= 0:
        return best == 1
    ca = [(a[0][i] + a[1][i]) / 2 for i in range(3)]
    cb = [(b[0][i] + b[1][i]) / 2 for i in range(3)]
    return dot(ca, F) < dot(cb, F)


def separated(a, b, gap=6.0):
    for k in range(3):
        if abs(F[k]) < 1e-6:
            continue
        if a[1][k] + gap <= b[0][k] or b[1][k] + gap <= a[0][k]:
            return True
    return False


def intersects(a, b, eps=2.0):
    return all(a[0][i] + eps < b[1][i] and b[0][i] + eps < a[1][i] for i in range(3))


def beam(cx, cy, cz, sx, sy, sz):
    return ((cx - sx / 2, cy - sy / 2, cz - sz / 2), (cx + sx / 2, cy + sy / 2, cz + sz / 2))


def is_cycle(boxes):
    names = range(3)
    edges = {}
    for i, j in itertools.combinations(names, 2):
        o = overlap(boxes[i], boxes[j])
        if o <= 0 or intersects(boxes[i], boxes[j]):
            return None
        if not separated(boxes[i], boxes[j]):
            return None  # require a genuine separating plane so per-pixel depth agrees with the graph
        front = i if in_front(boxes[i], boxes[j]) else j
        back = j if front == i else i
        edges[(back, front)] = o
    # a 3-cycle: every node has exactly one outgoing edge
    outs = [sum(1 for (b, f) in edges if b == n) for n in names]
    return edges if outs == [1, 1, 1] else None


def main():
    rnd = random.Random(7)
    best = None
    for _ in range(3000000):
        boxes = []
        kinds = [rnd.randrange(3) for _ in range(3)]
        if len(set(kinds)) < 2:
            continue
        for _k in range(3):
            # one beam across the view (along Y), one along the view (along X), one upright post
            t = rnd.choice([30.0, 40.0])
            ln = rnd.choice([280.0, 340.0])
            kind = kinds[_k]
            if kind == 0:
                sx, sy, sz = t, ln, 30.0
            elif kind == 1:
                sx, sy, sz = ln, t, 30.0
            else:
                sx, sy, sz = t, t, rnd.choice([150.0, 200.0])
            boxes.append(beam(rnd.uniform(-150, 150), rnd.uniform(-150, 150), rnd.uniform(15, 160), sx, sy, sz))
        e = is_cycle(boxes)
        if not e:
            continue
        m = min(e.values())
        score = m  # want every overlap clearly visible
        if best is None or score > best[0]:
            best = (score, boxes, e)
    if not best:
        print("no cycle found")
        return
    score, boxes, e = best
    print("min overlap texels^2:", round(score, 1))
    for i, b in enumerate(boxes):
        c = [(b[0][k] + b[1][k]) / 2 for k in range(3)]
        s = [b[1][k] - b[0][k] for k in range(3)]
        print(f"beam {i}: center=({c[0]:.0f},{c[1]:.0f},{c[2]:.0f}) size=({s[0]:.0f},{s[1]:.0f},{s[2]:.0f})")
    for (bk, fr), o in e.items():
        print(f"  {fr} covers {bk}: {o:.0f} texels^2")


if __name__ == "__main__":
    main()
