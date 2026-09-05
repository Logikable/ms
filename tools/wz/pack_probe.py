"""Measures a Data/Packs/*.ms file, to check what README.md claims about it.

    python3 tools/wz/pack_probe.py [--client DIR] [--pack NAME]

It reports the four things that pin the format down:

  header      where plaintext starts, and the entropy before it (ciphertext
              reads near 8.0 with almost no zero bytes; WZ body reads ~4.9
              with a third of it zero)
  coverage    how much of the file parses as WZ property data from
              0x1000-aligned starts
  keystream   whether any two pack headers XOR to something structured, which
              is what a reused keystream would leave
  pool        whether the string `Property` is anywhere in the pack, which is
              what a body's back-references cite and never resolve to

This is the harness for the next attempt: **decrypt a header, drop it in, and
coverage is what says whether it worked.** See README.md for what is known.
"""
import argparse
import collections
import itertools
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wz import ImgReader

CLIENT = '/mnt/c/Nexon/Games/maplestory/appdata'
# The offsets a pack's bodies cite for these names. They are the classic img
# header layout, and nothing in a pack resolves them.
TYPES = {1: 'Property', 44: 'Canvas', 70: 'Shape2D#Vector2D',
         201: 'common', 222: 'maxLevel', 372: 'UOL', 382: 'Shape2D#Convex2D'}


def mask(s):
    """A WZ string as it sits on disk: a length byte, then the 0xAA ladder."""
    return bytes([256 - len(s)]) + bytes([ord(c) ^ ((0xAA + i) & 0xFF)
                                          for i, c in enumerate(s)])


def entropy(b):
    if not b:
        return 0.0
    counts = collections.Counter(b)
    return -sum((n / len(b)) * math.log2(n / len(b)) for n in counts.values())


class Probe(ImgReader):
    """Parses property data without a string pool, naming what it cannot."""

    def string_block(self):
        mode = self.u8()
        if mode in (0x00, 0x73):
            return self.string()
        if mode in (0x01, 0x1B):
            return TYPES.get(self.i32(), '?')
        raise ValueError('mode %#x' % mode)


def reach(data, start, cap=1 << 20):
    """Bytes that parse as property data from `start`."""
    r = Probe(data, start, {}, 0)
    try:
        while r.p < start + cap:
            r.string_block()
            r.value(r.u8())
    except Exception:
        pass
    return r.p - start


def body_start(data, limit=1 << 22):
    """Where the plaintext begins: the first inline string of real letters."""
    for p in range(1, min(len(data), limit) - 64):
        if data[p - 1] not in (0x00, 0x73) or data[p] < 0xE0:
            continue
        n = 256 - data[p]
        s = bytes(data[p + 1 + i] ^ ((0xAA + i) & 0xFF) for i in range(n))
        if n >= 6 and all(65 <= c < 91 or 97 <= c < 123 for c in s):
            return p - 1
    return None


def report(path):
    data = open(path, 'rb').read()
    name = os.path.basename(path)
    head = body_start(data)
    prefix = data[:head] if head else b''
    print('%s  %.1f MB' % (name, len(data) / 1e6))
    print('  header   ends %#x  entropy %.3f  zeros %.2f%%' % (
        head or 0, entropy(prefix), 100 * prefix.count(0) / max(len(prefix), 1)))
    runs = [(s, n) for s in range(0, len(data) - 0x1000, 0x1000)
            if (n := reach(data, s)) > 2000]
    covered = sum(n for _, n in runs)
    print('  coverage %d runs >2KB, %.1f MB parses (%.1f%%)' % (
        len(runs), covered / 1e6, 100.0 * covered / len(data)))
    print('  pool     `Property` in file: %s' % (mask('Property') in data))


def keystream(paths, size=0x7000):
    """A reused keystream would leave two headers XORing to something ordered."""
    heads = {os.path.basename(p): open(p, 'rb').read(size) for p in paths}
    worst = min((entropy(bytes(a ^ b for a, b in zip(heads[x], heads[y]))), x, y)
                for x, y in itertools.combinations(sorted(heads), 2))
    print('\nlowest-entropy header XOR: %.3f  (%s ^ %s)' % worst)
    print('  above ~7 means no reused keystream, so no two-time pad')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', default=CLIENT)
    ap.add_argument('--pack', default='Skill_00008.ms')
    args = ap.parse_args()
    packs = os.path.join(args.client, 'Data', 'Packs')
    report(os.path.join(packs, args.pack))
    keystream([os.path.join(packs, f) for f in sorted(os.listdir(packs))
               if f.endswith('.ms')])


if __name__ == '__main__':
    main()
