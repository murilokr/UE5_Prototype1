"""Generate a Terragen 0.9 cloud map exactly as Terragen.dll does, for Terragen.ush's sky.

GenerateCloudLayer (0x1001cc30) and GenerateCloudStrip (0x1001ca60) build an (N+1) x (N+1) map of signed chars
by midpoint displacement: srand(seed), all zero, then levels = int(log2 N + 0.1) + feature-size steps; level L
works on squares of 2^L, setting each square's top edge midpoint, left edge midpoint, centre and right edge
midpoint (in that order, one rand() each) to the average of their corners plus (rand()/32767 - 0.5) * amplitude,
truncated, with 8-bit wrap-around like the original char arithmetic. The amplitude starts at 128 and is divided
by 2^(1 - persistence) after each level. The exe creates the layer with N = 512 (ConsCloudLayer(.., 512, ..)).
Every strip also stores amplitude * 0.3546099 as the layer's detail-noise amplitude (a float), so the finest
level's value is the one the renderer uses.

The seed, persistence and feature size come from the world's CLDL chunk (GSED, GPRS, GFSZ). GFSZ is a 16-bit
step count (the loader reads a short, SetCloudLayerGenProperties sign-extends it): -5 leaves 4 levels, so the
largest features are 16 cells across.
Outputs <name>_clouds.png (8-bit, value + 128) and <name>_clouds.r8 (raw bytes, same values).

Usage: python make_cloud_map.py <world.tgw> -o <out dir> --name <name>
"""
import argparse
import math
import os

from tgfiles import MsvcRand, read_tgw, write_png

N = 512


def to_char(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


def feature_steps(cldl):
    """GFSZ as the 16-bit signed value Terragen keeps."""
    v = cldl.i32('GFSZ', 0) & 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def noise_amplitude(persistence, feature, n=N):
    """The detail-noise amplitude GenerateCloudStrip leaves behind (0 when no level runs)."""
    levels = int(math.log2(n) + 0.1) + feature
    if levels < 1:
        return 0.0
    return 128.0 / 2.0 ** ((1.0 - persistence) * (levels - 1)) * 0.3546099


def generate(seed, persistence, feature_steps=0, n=N):
    rng = MsvcRand(seed)
    w = n + 1
    m = [0] * (w * w)
    amp = 128.0
    levels = int(math.log2(n) + 0.1) + feature_steps
    for level in range(levels, 0, -1):
        s = int(2.0 ** level + 0.1)
        half = s // 2
        for j in range(0, n, s):
            if n - s < 0:
                break
            for i in range(0, n - s + 1, s):
                a = m[j * w + i]
                b = m[j * w + i + s]
                c = m[(j + s) * w + i]
                d = m[(j + s) * w + i + s]

                def disp():
                    return int((rng.rand() * (1.0 / 32767.0) - 0.5) * amp)  # ftol truncates toward zero

                m[j * w + i + half] = to_char(disp() + to_char(int((a + b) / 2)))
                m[(j + half) * w + i] = to_char(disp() + to_char(int((a + c) / 2)))
                m[(j + half) * w + i + half] = to_char(disp() + to_char(int((a + b + c + d) / 4)))
                m[(j + half) * w + i + s] = to_char(disp() + to_char(int((b + d) / 2)))
        amp /= 2.0 ** (1.0 - persistence)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tgw')
    ap.add_argument('-o', '--out', default='.')
    ap.add_argument('--name', required=True)
    args = ap.parse_args()
    cldl = read_tgw(args.tgw).find('ATLI').find('CLDL')
    seed = cldl.i32('GSED')
    persistence = cldl.f32('GPRS', 0.25)
    feature = feature_steps(cldl)
    m = generate(seed, persistence, feature)
    noise_amp = noise_amplitude(persistence, feature)
    os.makedirs(args.out, exist_ok=True)
    w = N + 1
    rows = [bytes((v + 128) for v in m[y * w:(y + 1) * w]) for y in range(w)]
    write_png(os.path.join(args.out, f'{args.name}_clouds.png'), w, w, rows, 8, 0)
    with open(os.path.join(args.out, f'{args.name}_clouds.r8'), 'wb') as f:
        f.write(b''.join(rows))
    print(f'{args.name}: seed {seed}, persistence {persistence}, feature steps {feature}, '
          f'range {min(m)}..{max(m)}, detail noise amplitude {noise_amp:.4f}')


if __name__ == '__main__':
    main()
