"""Convert a Terragen 0.9 .ter heightfield into a lossless 16-bit texture for Terragen.ush.

Outputs, next to each other in the output folder:
  <name>_height.png   16-bit greyscale, import into UE as the shader's HeightTex
  <name>_height.r16   the same values as raw little-endian uint16 (used by the CPU test harness)
  <name>.json         the numbers needed to turn texels back into heights

Each texel stores the .ter's int16 sample + 32768, so nothing is lost. Height in terrain units is
  base_height + (texel - 32768) * height_scale / 65536
and one terrain unit (and one grid step) is scale_m metres.

Usage: python ter_convert.py <terrain.ter> [-o <out dir>] [--name <name>]
"""
import argparse
import json
import os
import struct

from tgfiles import read_ter, ter_heights, write_png_gray16


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ter')
    ap.add_argument('-o', '--out', default='.')
    ap.add_argument('--name', help='output base name (default: the .ter file name)')
    args = ap.parse_args()

    ter = read_ter(args.ter)
    name = args.name or os.path.splitext(os.path.basename(args.ter))[0]
    os.makedirs(args.out, exist_ok=True)
    w, h = ter['xpts'], ter['ypts']
    if w != h or (w - 1) & (w - 2):
        raise SystemExit(f'only square terrains of 2^n+1 points are supported (got {w}x{h})')

    texels = [r + 32768 for r in ter['raw']]
    write_png_gray16(os.path.join(args.out, f'{name}_height.png'), w, h, texels)
    with open(os.path.join(args.out, f'{name}_height.r16'), 'wb') as f:
        f.write(struct.pack(f'<{w * h}H', *texels))

    heights = ter_heights(ter)
    meta = {
        'source': os.path.basename(args.ter),
        'points': w,
        'cells': w - 1,
        'scale_m': ter['scale'][0],
        'base_height': ter['base_height'],
        'height_scale': ter['height_scale'],
        'min_height': min(heights),
        'max_height': max(heights),
        'planet_radius_km': ter['crad'],
        'curved': ter['crvm'],
        'encoding': 'height_units = base_height + (texel - 32768) * height_scale / 65536; 1 unit = scale_m metres',
    }
    with open(os.path.join(args.out, f'{name}.json'), 'w') as f:
        json.dump(meta, f, indent=2)
    print(f'{name}: {w}x{h} points, heights {meta["min_height"]:.3f}..{meta["max_height"]:.3f} units '
          f'({meta["min_height"] * meta["scale_m"]:.0f}..{meta["max_height"] * meta["scale_m"]:.0f} m), '
          f'curved={meta["curved"]}')


if __name__ == '__main__':
    main()
