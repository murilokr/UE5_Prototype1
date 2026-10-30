"""Write Terragen 0.9's value-noise lattice as a 128x128 16-bit texture for Terragen.ush.

Terragen.dll builds this table once (0x1001fc30): srand(1), then 16384 calls of rand(), stored row by row,
each lattice value being rand() / 32767 - 0.5. Every fractal noise, coverage breakup and bump texture in the
renderer samples it with smoothstep interpolation and a period of 128. Each texel here stores the raw rand()
value, so the shader reproduces the table exactly.

Outputs: noise128.png (import into UE as NoiseTex) and noise128.r16 (raw uint16, for the CPU harness).

Usage: python make_noise_table.py [-o <out dir>]
"""
import argparse
import os
import struct

from tgfiles import MsvcRand, write_png_gray16


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--out', default='.')
    args = ap.parse_args()

    rng = MsvcRand(1)
    values = [rng.rand() for _ in range(128 * 128)]
    os.makedirs(args.out, exist_ok=True)
    write_png_gray16(os.path.join(args.out, 'noise128.png'), 128, 128, values)
    with open(os.path.join(args.out, 'noise128.r16'), 'wb') as f:
        f.write(struct.pack(f'<{len(values)}H', *values))
    print('noise128: first values', values[:4])


if __name__ == '__main__':
    main()
