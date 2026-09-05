"""Minimal reader for the classic PKG1 WZ container (GMS v270, XOR-only strings)."""
import struct


def version_hash(version):
    h = 0
    for c in str(version):
        h = (32 * h + ord(c) + 1) & 0xFFFFFFFF
    return h


def version_check(h):
    b = struct.pack('<I', h)
    return 0xFF ^ b[0] ^ b[1] ^ b[2] ^ b[3]


def rotl32(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i8(self):
        v = self.u8()
        return v - 256 if v > 127 else v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]
        self.p += 2
        return v

    def i16(self):
        v = struct.unpack_from('<h', self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]
        self.p += 8
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]
        self.p += 4
        return v

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]
        self.p += 8
        return v

    def cint(self):
        b = self.i8()
        return self.i32() if b == -128 else b

    def clong(self):
        b = self.i8()
        return self.i64() if b == -128 else b

    def cfloat(self):
        b = self.i8()
        return self.f32() if b == -128 else 0.0

    def string(self):
        n = self.i8()
        if n == 0:
            return ''
        if n > 0:                                   # unicode, mask 0xAAAA
            if n == 127:
                n = self.i32()
            out = []
            m = 0xAAAA
            for _ in range(n):
                out.append(chr(self.u16() ^ m))
                m = (m + 1) & 0xFFFF
            return ''.join(out)
        n = 128 if n == -128 else -n                # ascii, mask 0xAA
        if n == 128:
            n = self.i32()
        out = bytearray()
        m = 0xAA
        for _ in range(n):
            out.append(self.u8() ^ m)
            m = (m + 1) & 0xFF
        return out.decode('latin-1')


class Wz:
    """One `<Dir>_NNN.wz` file: a complete PKG1 container with its own header."""

    def __init__(self, path, version=270):
        self.d = open(path, 'rb').read()
        assert self.d[:4] == b'PKG1', path
        self.fsize, self.fstart = struct.unpack_from('<QI', self.d, 4)
        self.version = version
        self.vhash = version_hash(version)
        self.encver = struct.unpack_from('<H', self.d, self.fstart)[0]
        self.entries = {}                           # path -> (kind, size, offset)
        self._dir('', self.fstart + 2)

    def decode_offset(self, pos):
        off = (pos - self.fstart) ^ 0xFFFFFFFF
        off = (off * self.vhash) & 0xFFFFFFFF
        off = (off - 0x581C3F6D) & 0xFFFFFFFF
        off = rotl32(off, off & 0x1F)
        enc = struct.unpack_from('<I', self.d, pos)[0]
        off ^= enc
        return (off + self.fstart * 2) & 0xFFFFFFFF

    def _dir(self, prefix, pos):
        r = Reader(self.d, pos)
        for _ in range(r.cint()):
            kind = r.u8()
            if kind == 1:
                r.p += 10
                continue
            if kind in (2, 3, 4):
                if kind == 2:
                    off = self.fstart + 1 + r.i32()
                    s = Reader(self.d, off)
                    kind = s.u8()
                    name = s.string()
                else:
                    name = r.string()
                size = r.cint()
                r.cint()                            # checksum
                offset = self.decode_offset(r.p)
                r.p += 4
                full = prefix + name
                if kind in (3, 1):
                    self._dir(full + '/', offset)
                else:
                    self.entries[full] = (kind, size, offset)


class ImgReader(Reader):
    """Property serialisation shared by classic imgs and the .ms pack blobs.

    `pool` resolves a string reference to an already-seen string; `base` is the
    offset every reference is measured from.
    """

    def __init__(self, data, pos=0, pool=None, base=0):
        super().__init__(data, pos)
        self.pool = pool if pool is not None else {}
        self.base = base

    def string_block(self):
        mode = self.u8()
        if mode in (0x00, 0x73):
            off = self.p - self.base
            s = self.string()
            self.pool[off] = s
            return s
        if mode in (0x01, 0x1B):
            off = self.i32()
            if off in self.pool:
                return self.pool[off]
            keep = self.p
            self.p = self.base + off
            s = self.string()
            self.p = keep
            self.pool[off] = s
            return s
        raise ValueError('string mode %#x at %#x' % (mode, self.p - 1))

    def value(self, kind):
        if kind == 0x00:
            return None
        if kind == 0x02 or kind == 0x0B:
            return self.i16()
        if kind == 0x03 or kind == 0x13:
            return self.cint()
        if kind == 0x04 or kind == 0x14:
            return self.cfloat()
        if kind == 0x05:
            return self.f64()
        if kind == 0x08:
            return self.string_block()
        if kind == 0x09:
            size = self.u32()
            end = self.p + size
            v = self.extended(end)
            self.p = end
            return v
        if kind == 0x10:
            return self.clong()
        raise ValueError('type %#x at %#x' % (kind, self.p - 1))

    def prop_list(self):
        self.u16()                                  # reserved
        out = {}
        for _ in range(self.cint()):
            name = self.string_block()
            out[name] = self.value(self.u8())
        return out

    def extended(self, end):
        name = self.string_block()
        if name == 'Property':
            return self.prop_list()
        if name == 'Canvas':
            self.u8()
            kids = {}
            if self.u8() == 1:
                kids = self.prop_list()
            kids['_w'] = self.cint()
            kids['_h'] = self.cint()
            kids['_format'] = self.cint()
            kids['_format2'] = self.u8()
            self.i32()
            n = self.i32()
            self.p += n
            return kids
        if name == 'Shape2D#Vector2D':
            return (self.cint(), self.cint())
        if name == 'Shape2D#Convex2D':
            return [self.value(0x09) for _ in range(self.cint())]
        if name == 'Sound_DX8':
            self.p = end
            return '<sound>'
        if name == 'UOL':
            self.u8()
            return '@' + self.string_block()
        raise ValueError('extended %r at %#x' % (name, self.p))


def read_img(data, pos, pool=None, base=None):
    r = ImgReader(data, pos, pool, pos if base is None else base)
    assert r.u8() in (0x73, 0x01), hex(data[pos])
    r.p = pos
    name = r.string_block()
    assert name == 'Property', name
    return r.prop_list(), r
