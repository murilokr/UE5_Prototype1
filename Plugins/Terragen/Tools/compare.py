"""Compare Tests/tg_render output with real Terragen renders of the same camera.

For every face it writes <ours>/compare_<Face>.png (Terragen | ours | difference x4) and prints:
  iou     overlap of the non-black silhouettes (1.0 = identical outlines)
  mae     mean absolute difference per channel (0..255) where both images show terrain
  mean    average colour of Terragen's and our terrain pixels

With --calibrate it also searches per-channel multipliers Ks (sun) and Ka (ambient) that minimise the
display-space error, predicting each pixel as
  tone(exposure * albedo * (Ks * SunColour * sun_term + Ka * Ambient))
from the albedo and sun term the shader wrote to <Face>.aux, so nothing is re-rendered. Pass the
SunColour, Ambient, exposure and gamma the render used. (Fitting in linear light instead is misleading:
inverting the tone curve near white turns tiny display differences into huge linear ones.)

Usage: python compare.py <terragen dir> <frame> <ours dir> [--faces Front,Left,...] [--black-level n]
         [--calibrate --sun r g b --ambient r g b --exposure e --gamma g]
"""
import argparse
import math
import os
import struct

from tgfiles import read_bmp, write_png_rgb8

FACES = ['Front', 'Left', 'Back', 'Right', 'Up', 'Down']
AUX = 12  # floats per pixel in <Face>.aux, see Tests/tg_render.cpp


def read_aux(path, n):
    d = open(path, 'rb').read()
    v = struct.unpack(f'<{n * AUX}f', d[:n * AUX * 4])
    return [v[i * AUX:(i + 1) * AUX] for i in range(n)]


def tone_mode2(x, gamma):
    """TG_ToneChannel for mode 2, then Terragen's 16-bit and 8-bit quantisation."""
    if x <= 0.0:
        return 0
    d = 1.0 if x * 256.0 > 1023.0 else (1.0 - math.exp(-1.38 * x * x)) ** (0.5 / gamma)
    return int(d * 32767.0 + 0.5) >> 7


def calibrate(samples, args):
    ks_grid = [0.6 + 0.1 * i for i in range(9)]    # 0.6 .. 1.4
    ka_grid = [0.5 + 0.25 * i for i in range(7)]   # 0.5 .. 2.0
    for c, ch in enumerate('rgb'):
        def mae(ks, ka):
            err = 0
            for s in samples:
                lin = s[c] * (ks * args.sun[c] * s[3] + ka * args.ambient[c])
                err += abs(tone_mode2(lin * args.exposure, args.gamma) - s[4 + c])
            return err / len(samples)
        best = min((mae(ks, ka), ks, ka) for ks in ks_grid for ka in ka_grid)
        print(f'  {ch}: best Ks {best[1]:.2f} Ka {best[2]:.2f} mae {best[0]:.2f}   (as rendered: mae {mae(1.0, 1.0):.2f})')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('terragen_dir')
    ap.add_argument('frame', help='frame number suffix, e.g. 0001')
    ap.add_argument('ours_dir')
    ap.add_argument('--faces', default=','.join(FACES))
    ap.add_argument('--calibrate', action='store_true')
    ap.add_argument('--sun', type=float, nargs=3)
    ap.add_argument('--ambient', type=float, nargs=3)
    ap.add_argument('--exposure', type=float, default=1.0)
    ap.add_argument('--gamma', type=float, default=2.0)
    ap.add_argument('--black-level', type=int, default=0,
                    help='pixels at or below this in every channel count as sky (for post-processed screenshots)')
    args = ap.parse_args()

    samples = []  # albedo rgb, sun term, terragen rgb
    total_iou = []
    for face in args.faces.split(','):
        ref_path = os.path.join(args.terragen_dir, f'Sky_{face}{args.frame}.bmp')
        our_path = os.path.join(args.ours_dir, f'{face}.bmp')
        if not (os.path.exists(ref_path) and os.path.exists(our_path)):
            print(f'{face}: missing {ref_path if not os.path.exists(ref_path) else our_path}')
            continue
        w, h, ref = read_bmp(ref_path)
        w2, h2, ours = read_bmp(our_path)
        if (w, h) != (w2, h2):
            raise SystemExit(f'{face}: size mismatch {w}x{h} vs {w2}x{h2}')
        n = w * h
        aux = read_aux(os.path.join(args.ours_dir, f'{face}.aux'), n) if args.calibrate else None

        inter = union = both = 0
        sad = [0, 0, 0]
        mean_ref = [0, 0, 0]
        mean_ours = [0, 0, 0]
        diff = bytearray(n * 3)
        for i in range(n):
            r = ref[3 * i:3 * i + 3]
            o = ours[3 * i:3 * i + 3]
            rin = max(r) > args.black_level
            oin = max(o) > args.black_level
            inter += rin and oin
            union += rin or oin
            for c in range(3):
                diff[3 * i + c] = min(255, abs(r[c] - o[c]) * 4)
            if rin and oin:
                both += 1
                for c in range(3):
                    sad[c] += abs(r[c] - o[c])
                    mean_ref[c] += r[c]
                    mean_ours[c] += o[c]
                if aux is not None and i % 16 == 0 and aux[i][0] == 1.0:
                    a = aux[i]
                    samples.append((a[4], a[5], a[6], a[7], r[0], r[1], r[2]))
        iou = inter / union if union else 1.0
        total_iou.append(iou)
        mae = [s / both if both else 0 for s in sad]
        mr = [m / both if both else 0 for m in mean_ref]
        mo = [m / both if both else 0 for m in mean_ours]
        print(f'{face:5s} iou {iou:.3f}  mae {mae[0]:5.1f} {mae[1]:5.1f} {mae[2]:5.1f}  '
              f'mean terragen ({mr[0]:.0f},{mr[1]:.0f},{mr[2]:.0f}) ours ({mo[0]:.0f},{mo[1]:.0f},{mo[2]:.0f})')

        strip = bytearray(w * 3 * h * 3)
        for y in range(h):
            for k, img in enumerate((ref, ours, diff)):
                strip[(y * 3 * w + k * w) * 3:(y * 3 * w + (k + 1) * w) * 3] = img[y * w * 3:(y + 1) * w * 3]
        write_png_rgb8(os.path.join(args.ours_dir, f'compare_{face}.png'), 3 * w, h, strip)

    if total_iou:
        print(f'mean iou {sum(total_iou) / len(total_iou):.3f}')
    if args.calibrate and samples:
        print(f'calibration over {len(samples)} terrain pixels:')
        calibrate(samples, args)


if __name__ == '__main__':
    main()
