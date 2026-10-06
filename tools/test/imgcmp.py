#!/usr/bin/env python3
"""imgcmp: compares rendered images with references (no libraries needed).

  imgcmp.py compare <image> <reference> [--tolerance N] [--max-pixels F]
             [--diff <diff.png>]
      Exit 0 if at most F (fraction, default 0.001) of the pixels differ by
      more than N (default 2) in any channel. --diff writes the differing
      pixels in red over a darkened reference.
  imgcmp.py montage <out.png> <image>...
      Puts the images side by side (for looking at new references).

Reads and writes 8 bit RGB/RGBA PNGs, not interlaced.
"""
import struct
import sys
import zlib


def read_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(path + ': not a PNG')
    pos = 8
    idat = b''
    width = height = channels = None
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack(
                '>IIBBBBB', chunk)
            if depth != 8 or color not in (2, 6) or interlace != 0:
                raise ValueError(path + ': only 8 bit RGB/RGBA, not interlaced')
            channels = 4 if color == 6 else 3
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'IEND':
            break
    raw = zlib.decompress(idat)
    stride = width * channels
    pixels = bytearray(height * width * 4)
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[x] = (line[x] + predictor) & 255
        for x in range(width):
            o = (y * width + x) * 4
            pixels[o:o + channels] = line[x * channels:(x + 1) * channels]
            if channels == 3:
                pixels[o + 3] = 255
        previous = line
    return width, height, pixels


def write_png(path, width, height, pixels):
    raw = b''.join(b'\x00' + bytes(pixels[y * width * 4:(y + 1) * width * 4])
        for y in range(height))

    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload
            + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0,
            0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def compare(args):
    image, reference = args[0], args[1]
    tolerance, max_fraction, diff_path = 2, 0.001, None
    i = 2
    while i < len(args):
        if args[i] == '--tolerance':
            tolerance = int(args[i + 1])
        elif args[i] == '--max-pixels':
            max_fraction = float(args[i + 1])
        elif args[i] == '--diff':
            diff_path = args[i + 1]
        i += 2
    w1, h1, p1 = read_png(image)
    w2, h2, p2 = read_png(reference)
    if (w1, h1) != (w2, h2):
        print('size %dx%d, reference %dx%d' % (w1, h1, w2, h2))
        return 1
    differing = 0
    largest = 0
    diff = bytearray(len(p2))
    for o in range(0, len(p1), 4):
        delta = max(abs(p1[o + c] - p2[o + c]) for c in range(4))
        largest = max(largest, delta)
        if delta > tolerance:
            differing += 1
            diff[o:o + 4] = b'\xff\x00\x00\xff'
        else:
            diff[o:o + 4] = bytes((p2[o] // 3, p2[o + 1] // 3, p2[o + 2] // 3,
                255))
    fraction = differing / (w1 * h1)
    ok = fraction <= max_fraction
    print('%d of %d pixels differ by more than %d (%.4f%%), largest '
        'difference %d: %s' % (differing, w1 * h1, tolerance, fraction * 100,
        largest, 'OK' if ok else 'DIFFERENT'))
    if diff_path is not None and differing > 0:
        write_png(diff_path, w1, h1, diff)
    return 0 if ok else 1


def montage(args):
    images = [read_png(path) for path in args[1:]]
    gap = 4
    height = max(h for _, h, _ in images)
    width = sum(w for w, _, _ in images) + gap * (len(images) - 1)
    out = bytearray(b'\x80\x80\x80\xff' * (width * height))
    x0 = 0
    for w, h, pixels in images:
        for y in range(h):
            out[(y * width + x0) * 4:(y * width + x0 + w) * 4] = \
                pixels[y * w * 4:(y + 1) * w * 4]
        x0 += w + gap
    write_png(args[0], width, height, out)
    return 0


if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in ('compare', 'montage'):
        print(__doc__)
        sys.exit(2)
    sys.exit(compare(sys.argv[2:]) if sys.argv[1] == 'compare'
        else montage(sys.argv[2:]))
