"""Readers and writers for Terragen 0.9 files (.ter, .tgw) and the few image formats the tools need.

Only the standard library is used, so the tools run on a stock Python install.
"""
import struct
import zlib


# ---------------------------------------------------------------- .ter terrain

def read_ter(path):
    """Parse a Terragen .ter heightfield.

    Heights are stored as int16; the elevation in terrain units is
    base_height + raw * height_scale / 65536, and one terrain unit is `scale` metres.
    """
    d = open(path, 'rb').read()
    if d[:16] != b'TERRAGENTERRAIN ':
        raise ValueError(f'{path}: not a Terragen terrain file')
    ter = {'crad': 6370.0, 'crvm': 0, 'scale': (30.0, 30.0, 30.0)}
    i = 16
    while i + 4 <= len(d):
        tag = d[i:i + 4]
        if tag == b'SIZE':
            ter['size'] = struct.unpack_from('<h', d, i + 4)[0]; i += 8
        elif tag == b'XPTS':
            ter['xpts'] = struct.unpack_from('<h', d, i + 4)[0]; i += 8
        elif tag == b'YPTS':
            ter['ypts'] = struct.unpack_from('<h', d, i + 4)[0]; i += 8
        elif tag == b'SCAL':
            ter['scale'] = struct.unpack_from('<3f', d, i + 4); i += 16
        elif tag == b'CRAD':
            ter['crad'] = struct.unpack_from('<f', d, i + 4)[0]; i += 8
        elif tag == b'CRVM':
            ter['crvm'] = struct.unpack_from('<h', d, i + 4)[0]; i += 8
        elif tag == b'ALTW':
            ter['height_scale'], ter['base_height'] = struct.unpack_from('<hh', d, i + 4)
            xpts = ter.get('xpts', ter['size'] + 1)
            ypts = ter.get('ypts', xpts)
            ter['xpts'], ter['ypts'] = xpts, ypts
            ter['raw'] = list(struct.unpack_from(f'<{xpts * ypts}h', d, i + 8))
            i += 8 + ((2 * xpts * ypts + 3) & ~3)  # the data is padded to 4 bytes
        elif tag == b'EOF ':
            break
        else:
            raise ValueError(f'{path}: unknown chunk {tag!r} at {i}')
    return ter


def ter_heights(ter):
    """Elevations in terrain units, row-major (index = y * xpts + x)."""
    hs, bh = ter['height_scale'], ter['base_height']
    return [bh + r * hs / 65536.0 for r in ter['raw']]


# ---------------------------------------------------------------- .tgw world

class Chunk:
    """One chunk of a .tgw file: either a container (children) or a leaf (data)."""

    def __init__(self, tag, data=None, children=None):
        self.tag, self.data, self.children = tag, data, children

    def find(self, tag):
        for c in self.children or []:
            if c.tag == tag:
                return c
        return None

    def findall(self, tag):
        return [c for c in self.children or [] if c.tag == tag]

    def f32(self, tag, default=0.0):
        c = self.find(tag)
        return struct.unpack('<f', c.data[:4])[0] if c and len(c.data) >= 4 else default

    def f32s(self, tag, n):
        c = self.find(tag)
        return list(struct.unpack(f'<{n}f', c.data[:4 * n])) if c else [0.0] * n

    def i32(self, tag, default=0):
        c = self.find(tag)
        return struct.unpack('<i', c.data[:4])[0] if c and len(c.data) >= 4 else default

    def colour(self, tag, default=(0, 0, 0)):
        """Colours are three uint16 where 256 means 1.0."""
        c = self.find(tag)
        return tuple(v / 256.0 for v in struct.unpack('<3H', c.data[:6])) if c else default

    def text(self, tag):
        c = self.find(tag)
        return c.data.split(b'\0')[0].decode('latin1') if c else ''


def _is_tag(b):
    return len(b) == 4 and all(65 <= c <= 90 or 48 <= c <= 57 or c == 32 for c in b)


def _parse_chunks(d, i, end):
    out = []
    while i + 8 <= end and _is_tag(d[i:i + 4]):
        tag = d[i:i + 4].decode()
        n = struct.unpack_from('<I', d, i + 4)[0]
        body = d[i + 8:i + 8 + n]
        # A container's body starts with a valid child chunk header that fits inside it.
        if n >= 8 and _is_tag(body[:4]) and struct.unpack_from('<I', body, 4)[0] <= n - 8:
            out.append(Chunk(tag, children=_parse_chunks(d, i + 8, i + 8 + n)))
        else:
            out.append(Chunk(tag, data=body))
        i += 8 + ((n + 3) & ~3)  # chunks are padded to 4 bytes
    return out


def read_tgw(path):
    d = open(path, 'rb').read()
    if d[:16] != b'TERRAGENWORLD2  ':
        raise ValueError(f'{path}: not a Terragen 0.9 world file')
    return Chunk('ROOT', children=_parse_chunks(d, 16, len(d)))


# ---------------------------------------------------------------- MSVC rand()

class MsvcRand:
    """The Microsoft C runtime rand(), which Terragen 0.9 uses for its noise tables."""

    def __init__(self, seed=1):
        self.state = seed & 0xFFFFFFFF

    def rand(self):
        self.state = (self.state * 214013 + 2531011) & 0xFFFFFFFF
        return (self.state >> 16) & 0x7FFF


# ---------------------------------------------------------------- images

def _png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, rows, bit_depth, colour_type):
    """rows: list of bytes objects, one per image row, already packed (big-endian for 16-bit)."""
    raw = b''.join(b'\0' + r for r in rows)
    png = b'\x89PNG\r\n\x1a\n'
    png += _png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, bit_depth, colour_type, 0, 0, 0))
    png += _png_chunk(b'IDAT', zlib.compress(raw, 9))
    png += _png_chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def write_png_gray16(path, width, height, values):
    rows = [struct.pack(f'>{width}H', *values[y * width:(y + 1) * width]) for y in range(height)]
    write_png(path, width, height, rows, 16, 0)


def write_png_rgb8(path, width, height, pixels):
    """pixels: bytes/bytearray of width*height*3, top row first."""
    rows = [bytes(pixels[y * width * 3:(y + 1) * width * 3]) for y in range(height)]
    write_png(path, width, height, rows, 8, 2)


def read_bmp(path):
    """Read an uncompressed 24-bit BMP; returns (width, height, bytearray RGB top row first)."""
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        raise ValueError(f'{path}: not a BMP')
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp != 24:
        raise ValueError(f'{path}: only 24-bit BMPs are supported')
    stride = (w * 3 + 3) & ~3
    top_down = h < 0
    h = abs(h)
    out = bytearray(w * h * 3)
    for y in range(h):
        src = off + (y if top_down else h - 1 - y) * stride
        row = d[src:src + w * 3]
        for x in range(w):
            b, g, r = row[3 * x:3 * x + 3]
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes((r, g, b))
    return w, h, out


def write_bmp(path, width, height, pixels):
    """Write a 24-bit BMP from RGB bytes, top row first."""
    stride = (width * 3 + 3) & ~3
    body = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            r, g, b = pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]
            row += bytes((b, g, r))
        body += row + b'\0' * (stride - width * 3)
    header = b'BM' + struct.pack('<IHHI', 54 + len(body), 0, 0, 54)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(path, 'wb').write(header + body)
