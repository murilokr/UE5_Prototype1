"""Undo Scripts/GenerateSkyboxTexture.py: split a 2048x2048 skybox cross back into six Terragen faces.

GenerateSkyboxTexture.py pastes Back, Left, Front, Right mirrored left-right in a row at y=768, and
Up (at 512,256) and Down (at 512,1280) mirrored and rotated 180 degrees, i.e. flipped top-bottom.
This reverses both, so the faces line up with Terragen's own output again.

Writes <out>/Sky_<Face><frame>.bmp. Reads a 24-bit BMP or a non-interlaced 8-bit RGB/RGBA PNG.

Usage: python unpack_cross.py <cross.png> <out dir> <frame>
"""
import argparse
import os
import struct
import zlib

from tgfiles import read_bmp, write_bmp


def read_png_rgb(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    i, idat = 8, b''
    while i < len(d):
        n = struct.unpack_from('>I', d, i)[0]
        tag = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + n]
        if tag == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif tag == b'IDAT':
            idat += body
        elif tag == b'IEND':
            break   # Unreal's render-target export pads the file after IEND
        i += 12 + n
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError(f'{path}: only 8-bit non-interlaced RGB/RGBA PNGs are supported')
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = line[x * bpp:x * bpp + 3]
        prev = line
    return w, h, out


def crop(img, w, x0, y0, size, mirror_x, mirror_y):
    face = bytearray(size * size * 3)
    for y in range(size):
        sy = y0 + (size - 1 - y if mirror_y else y)
        for x in range(size):
            sx = x0 + (size - 1 - x if mirror_x else x)
            face[(y * size + x) * 3:(y * size + x) * 3 + 3] = img[(sy * w + sx) * 3:(sy * w + sx) * 3 + 3]
    return face


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cross')
    ap.add_argument('out')
    ap.add_argument('frame')
    args = ap.parse_args()
    w, h, img = read_bmp(args.cross) if args.cross.lower().endswith('.bmp') else read_png_rgb(args.cross)
    s = w // 4
    os.makedirs(args.out, exist_ok=True)
    layout = {'Back': (0, 768, True, False), 'Left': (s, 768, True, False), 'Front': (2 * s, 768, True, False),
              'Right': (3 * s, 768, True, False), 'Up': (512, 256, False, True), 'Down': (512, 1280, False, True)}
    for face, (x0, y0, mx, my) in layout.items():
        write_bmp(os.path.join(args.out, f'Sky_{face}{args.frame}.bmp'), s, s, crop(img, w, x0, y0, s, mx, my))
    print(f'{args.cross}: wrote 6 faces of {s}x{s} to {args.out}')


if __name__ == '__main__':
    main()
