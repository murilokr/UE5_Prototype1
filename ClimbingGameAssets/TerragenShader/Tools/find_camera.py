"""Find the Terragen camera of a skybox render with the sky on, by matching silhouettes.

The reference's sky is estimated from the image: smooth or clearly blue pixels connected to the top edge
(side faces) or to any edge (Up face). Our side of the comparison is Tests/tg_render --mask, the terrain/water
visibility from a candidate camera. The score is the overlap (IoU) of the sky masks over Front, Left, Back,
Right and Up, and a coordinate descent over heading, x, y and z maximises it.

Usage: python find_camera.py <ref dir> <frame> <height.r16> --start x y z heading [--steps 8 2 2 1]
"""
import argparse
import os
import struct
import subprocess
import tempfile
from collections import deque

from tgfiles import read_bmp

FACES = ['Front', 'Left', 'Back', 'Right', 'Up']
RES = 128
HERE = os.path.dirname(os.path.abspath(__file__))
RENDER = os.path.join(HERE, '..', 'Build', 'tg_render.exe')
NOISE = os.path.join(HERE, '..', 'Textures', 'noise128.r16')
USE_MASK = os.environ.get('TG_FIND_NO_MASK') != '1'


def reference_sky(path, from_all_edges):
    w, h, img = read_bmp(path)
    lum = [(img[3 * i] * 3 + img[3 * i + 1] * 6 + img[3 * i + 2]) / 10 for i in range(w * h)]
    cand = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            g = 0
            for dx, dy in ((2, 0), (0, 2), (-2, 0), (0, -2)):
                xx = min(max(x + dx, 0), w - 1)
                yy = min(max(y + dy, 0), h - 1)
                g = max(g, abs(lum[yy * w + xx] - lum[i]))
            r, _, b = img[3 * i:3 * i + 3]
            cand[i] = (g < 6 and lum[i] > 90) or (b > r + 40 and b > 120 and g < 12)
    sky = bytearray(w * h)
    seeds = [(x, 0) for x in range(w)]
    if from_all_edges:
        seeds += [(x, h - 1) for x in range(w)] + [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    q = deque()
    for x, y in seeds:
        if cand[y * w + x] and not sky[y * w + x]:
            sky[y * w + x] = 1
            q.append((x, y))
    while q:
        x, y = q.popleft()
        for xx, yy in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= xx < w and 0 <= yy < h and cand[yy * w + xx] and not sky[yy * w + xx]:
                sky[yy * w + xx] = 1
                q.append((xx, yy))
    step = w // RES
    return [sky[(y * step) * w + x * step] for y in range(RES) for x in range(RES)]


def render_sky(height, cam, heading, tmp):
    cmd = [RENDER, height, NOISE, tmp, '--cam', *[f'{v:.4f}' for v in cam], '--heading', f'{heading:.3f}',
           '--size', str(RES), '--faces', ','.join(FACES)]
    if USE_MASK:
        subprocess.run(cmd + ['--mask'], check=True, capture_output=True)
        return {f: [1 - b for b in open(os.path.join(tmp, f'{f}.mask'), 'rb').read()] for f in FACES}
    # Older harness builds have no --mask: take the hit kind from the full render's .aux instead.
    subprocess.run(cmd, check=True, capture_output=True)
    out = {}
    for f in FACES:
        d = open(os.path.join(tmp, f'{f}.aux'), 'rb').read()
        out[f] = [1 if struct.unpack_from('<f', d, i * 48)[0] == 0.0 else 0 for i in range(RES * RES)]
    return out


def score(ref, ours):
    inter = union = 0
    for f in FACES:
        for a, b in zip(ref[f], ours[f]):
            inter += a and b
            union += a or b
    return inter / union if union else 1.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ref_dir')
    ap.add_argument('frame')
    ap.add_argument('height')
    ap.add_argument('--start', type=float, nargs=4, required=True, metavar=('X', 'Y', 'Z', 'HEADING'))
    ap.add_argument('--steps', type=float, nargs=4, default=[8.0, 2.0, 2.0, 1.0], metavar=('DH', 'DX', 'DY', 'DZ'))
    ap.add_argument('--sweep', action='store_true', help='first sweep the heading over 360 degrees')
    args = ap.parse_args()

    ref = {f: reference_sky(os.path.join(args.ref_dir, f'Sky_{f}{args.frame}.bmp'), f == 'Up') for f in FACES}
    tmp = tempfile.mkdtemp()
    x, y, z, h = args.start
    best = score(ref, render_sky(args.height, (x, y, z), h, tmp))
    if args.sweep:
        for hh in range(0, 360, 6):
            s = score(ref, render_sky(args.height, (x, y, z), hh, tmp))
            if s > best:
                best, h = s, float(hh)
        print(f'sweep: heading {h:.1f} iou {best:.4f}')
    steps = list(args.steps)
    while max(steps) > 0.01:
        improved = False
        for k in range(4):
            for sign in (1, -1):
                trial = [h, x, y, z]
                trial[k] += sign * steps[k]
                s = score(ref, render_sky(args.height, (trial[1], trial[2], trial[3]), trial[0], tmp))
                if s > best + 1e-5:
                    best = s
                    h, x, y, z = trial
                    improved = True
        if not improved:
            steps = [s / 2 for s in steps]
        print(f'heading {h:.3f} cam {x:.3f} {y:.3f} {z:.3f} iou {best:.4f}  steps {[round(s, 3) for s in steps]}')
    print(f'best: --cam {x:.3f} {y:.3f} {z:.3f} --heading {h:.3f}  iou {best:.4f}')


if __name__ == '__main__':
    main()
