"""Reads the .ms packs the client keeps its WZ images in.

    python3 tools/wz/ms_pack.py list Skill_00008
    python3 tools/wz/ms_pack.py extract Skill 400011073.img out.img

A pack is a name-keyed archive whose header, entry table and first kilobyte of
every image are ChaCha20 stream cipher. Every key comes from the pack's own
file name and a salt written in the clear at the front, so nothing here needs
a key out of the client.

Version 4 (ChaCha20) is what the live client ships. Version 2 (Snow2) is not
read here.
"""

import os
import struct
import sys

PACK_DIR = "/mnt/c/Nexon/Games/maplestory/appdata/Data/Packs"

KEY_OBSCURE = bytes([
    0x7B, 0x2F, 0x35, 0x48, 0x43, 0x95, 0x02, 0xB9,
    0xAE, 0x91, 0xA6, 0xE1, 0xD8, 0xD6, 0x24, 0xB4,
    0x33, 0x10, 0x1D, 0x3D, 0xC1, 0xBB, 0xC6, 0xF4,
    0xA5, 0xFE, 0xB3, 0x69, 0x6B, 0x56, 0xE4, 0x75,
])

BLOCK = 64
PAGE = 1024


def _rotl(v, n):
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF


class ChaCha20:
    """The client's ChaCha20, keystream and all -- state[12] is reachable
    because the pack reader rewinds it between blocks."""

    def __init__(self, key, nonce, counter):
        assert len(key) == 32 and len(nonce) == 12
        self.state = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574]
        self.state += list(struct.unpack("<8I", key))
        self.state += [counter] + list(struct.unpack("<3I", nonce))
        self.block = bytearray(BLOCK)
        self.offset = BLOCK

    def _generate(self):
        x = list(self.state)
        for _ in range(10):
            for a, b, c, d in ((0, 4, 8, 12), (1, 5, 9, 13), (2, 6, 10, 14),
                               (3, 7, 11, 15), (0, 5, 10, 15), (1, 6, 11, 12),
                               (2, 7, 8, 13), (3, 4, 9, 14)):
                x[a] = (x[a] + x[b]) & 0xFFFFFFFF
                x[d] = _rotl(x[d] ^ x[a], 16)
                x[c] = (x[c] + x[d]) & 0xFFFFFFFF
                x[b] = _rotl(x[b] ^ x[c], 12)
                x[a] = (x[a] + x[b]) & 0xFFFFFFFF
                x[d] = _rotl(x[d] ^ x[a], 8)
                x[c] = (x[c] + x[d]) & 0xFFFFFFFF
                x[b] = _rotl(x[b] ^ x[c], 7)
        out = [(x[i] + self.state[i]) & 0xFFFFFFFF for i in range(16)]
        self.block = bytearray(struct.pack("<16I", *out))
        self.state[12] = (self.state[12] + 1) & 0xFFFFFFFF
        if self.state[12] == 0:
            self.state[13] = (self.state[13] + 1) & 0xFFFFFFFF
        self.offset = 0

    def transform(self, data):
        data = bytearray(data)
        pos = 0
        if self.offset < BLOCK:
            count = min(len(data), BLOCK - self.offset)
            for i in range(count):
                data[i] ^= self.block[self.offset + i]
            self.offset += count
            pos = count
        while len(data) - pos >= BLOCK:
            self._generate()
            for i in range(BLOCK):
                data[pos + i] ^= self.block[i]
            self.offset = BLOCK
            pos += BLOCK
        if pos < len(data):
            self._generate()
            for i in range(len(data) - pos):
                data[pos + i] ^= self.block[i]
            self.offset = len(data) - pos
        return bytes(data)


class Entry:
    def __init__(self, name, checksum, flags, start, size, size_aligned, key):
        self.name = name
        self.checksum = checksum
        self.flags = flags
        self.start = start
        self.size = size
        self.size_aligned = size_aligned
        self.key = key

    def __repr__(self):
        return "Entry(%s, %d bytes @ %d)" % (self.name, self.size, self.start)


class Reader:
    """The entry table's own reader: 64 bytes at a time, and the block counter
    goes back to zero every time a read lands on the boundary. Odd, and the
    client does it, so the table only decodes if this does too."""

    def __init__(self, data, key):
        self.data = data
        self.pos = 0
        self.cipher = ChaCha20(key, bytes(12), 0)
        self.buffer = b""
        self.offset = BLOCK

    def read(self, count):
        out = bytearray()
        while len(out) < count:
            if self.offset >= BLOCK:
                chunk = self.data[self.pos:self.pos + BLOCK]
                if len(chunk) < BLOCK:
                    raise EOFError("pack ends inside its entry table")
                self.pos += BLOCK
                self.buffer = self.cipher.transform(chunk)
                self.offset = 0
            take = min(count - len(out), BLOCK - self.offset)
            out += self.buffer[self.offset:self.offset + take]
            self.offset += take
        if self.offset >= BLOCK:
            self.cipher.state[12] = 0
        return bytes(out)

    def int32(self):
        return struct.unpack("<i", self.read(4))[0]

    def string(self):
        length = self.int32()
        if length < 0 or length > 1 << 20:
            raise ValueError("entry name length %d" % length)
        return self.read(length * 2).decode("utf-16-le")


def _key(name_with_salt, entry_key):
    """The pack's two keys, both spelled out of its own name and salt."""
    key = bytearray(32)
    n = len(name_with_salt)
    for i in range(32):
        if entry_key:
            c = ord(name_with_salt[n - 1 - i % n])
            key[i] = (i + (i % 3 + 2) * c) & 0xFF
        else:
            key[i] = (ord(name_with_salt[i % n]) + i) & 0xFF
        key[i] ^= KEY_OBSCURE[i]
    return bytes(key)


class Pack:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        name = os.path.basename(path).lower()
        char_sum = sum(ord(c) for c in name)
        rand_count = char_sum % 312 + 30
        rand = bytearray(self.data[:rand_count])
        for i in range(rand_count):
            rand[i] = (rand[i] - 256 if rand[i] > 127 else rand[i]) >> 1 & 0xFF
        pos = rand_count
        version = self.data[pos] ^ rand[0]
        pos += 1
        if version != 4:
            raise ValueError("%s is version %d, not 4" % (name, version))
        hashed_salt_len = struct.unpack_from("<i", self.data, pos)[0]
        pos += 4
        salt_len = (hashed_salt_len & 0xFF) ^ rand[0]
        if not 0 < salt_len <= rand_count:
            raise ValueError("salt length %d" % salt_len)
        salt_bytes = self.data[pos:pos + salt_len * 2]
        pos += salt_len * 2
        salt = ""
        for i in range(salt_len):
            a = rand[i] ^ salt_bytes[i * 2]
            salt += chr(((a | 0x4B) << 1) - a - 75)
        self.salt = salt
        self.name_with_salt = name + salt

        header_start = pos
        head = ChaCha20(_key(self.name_with_salt, False), bytes(12), 0).transform(
            self.data[pos:pos + 8])
        self.hash, self.entry_count = struct.unpack("<ii", head)
        pad = (char_sum * 3) % 212 + 64
        self.entry_start = header_start + 8 + pad
        self.entries = self._read_entries()

    def _read_entries(self):
        reader = Reader(self.data[self.entry_start:],
                        _key(self.name_with_salt, True))
        entries = []
        for _ in range(self.entry_count):
            name = reader.string()
            checksum = reader.int32()
            flags = reader.int32()
            start = reader.int32()
            size = reader.int32()
            size_aligned = reader.int32()
            reader.int32()
            reader.int32()
            key = reader.read(16)
            reader.int32()
            reader.int32()
            entries.append(
                Entry(name, checksum, flags, start, size, size_aligned, key))
        data_start = (self.entry_start + reader.pos - (BLOCK - reader.offset)
                      + PAGE - 1) & ~(PAGE - 1)
        # The reader runs a whole block ahead of what it handed out, so where
        # the table really ended is where it read to, less what it held back.
        for entry in entries:
            entry.start = data_start + entry.start * PAGE
        self.data_start = data_start
        return entries

    def _img_key(self, entry):
        key_hash = 0x811C9DC5
        for c in self.salt:
            key_hash = ((key_hash ^ ord(c)) * 0x1000193) & 0xFFFFFFFF
        digits = str(key_hash)
        img_key = bytearray(32)
        for i in range(32):
            d = int(digits[i % len(digits)])
            nxt = int(digits[(i + 1) % len(digits)])
            pick = int(digits[(i + 2) % len(digits)])
            c = ord(entry.name[i % len(entry.name)])
            img_key[i] = (i + c * ((d % 2) + entry.key[(pick + i) % 16] +
                                   ((nxt + i) % 5))) & 0xFF
            img_key[i] ^= KEY_OBSCURE[i]
        mangled = bytearray(struct.pack("<3I", key_hash, key_hash >> 1,
                                        (key_hash >> 1) ^ 0x6C))
        a = b = d = 0
        c = 90
        for i in range(12):
            mangled[i] ^= (d + 11 * (i // 11) + (c ^ (i >> 2)) + (a ^ b)) & 0xFF
            d -= 1
            a += 8
            b += 17
            c += 43
        nonce = bytes(4) + bytes(mangled[:8])
        counter = struct.unpack("<I", bytes(mangled[8:]))[0]
        return bytes(img_key), nonce, counter

    def image(self, entry):
        """One entry's WZ image, decrypted. Only its first kilobyte is
        enciphered; the rest is plain in the file."""
        raw = bytearray(self.data[entry.start:entry.start + entry.size])
        key, nonce, counter = self._img_key(entry)
        head = min(len(raw), 1024)
        if head:
            raw[:head] = ChaCha20(key, nonce, counter).transform(raw[:head])
        return bytes(raw)


def packs_named(stem):
    """Every pack file of one WZ, in order: Skill -> Skill_0000N.ms."""
    files = sorted(f for f in os.listdir(PACK_DIR)
                   if f.lower().startswith(stem.lower() + "_")
                   and f.endswith(".ms"))
    return [os.path.join(PACK_DIR, f) for f in files]


def find(stem, name):
    """The pack and entry holding `name`, searching every file of that WZ."""
    for path in packs_named(stem):
        pack = Pack(path)
        for entry in pack.entries:
            if entry.name == name or entry.name.endswith("/" + name):
                return pack, entry
    return None, None


def skill_common(skill_id):
    """One skill's `common` block, straight out of the client: every per-level
    formula GMS computes its readout from. `x` is the level."""
    import wz

    for width in (3, 4, 5, 6):
        pack, entry = find("Skill", skill_id[:width] + ".img")
        if entry is None:
            continue
        tree, _ = wz.read_img(pack.image(entry), 0)
        node = tree.get("skill", {}).get(skill_id)
        if node is not None:
            return entry.name, node.get("common")
    return None, None


def main(argv):
    if len(argv) == 3 and argv[1] == "common":
        img, common = skill_common(argv[2])
        if common is None:
            print("no skill", argv[2])
            return 1
        print(img, argv[2])
        for key, value in common.items():
            print("   %-14s %s" % (key, value))
        return 0
    if len(argv) >= 3 and argv[1] == "list":
        for path in packs_named(argv[2]) or [os.path.join(PACK_DIR, argv[2])]:
            pack = Pack(path)
            print("%s  salt=%r  %d entries  data at 0x%x" %
                  (os.path.basename(path), pack.salt, pack.entry_count,
                   pack.data_start))
            for entry in pack.entries[:int(argv[3]) if len(argv) > 3 else 5]:
                print("   ", entry)
        return 0
    if len(argv) == 5 and argv[1] == "extract":
        pack, entry = find(argv[2], argv[3])
        if entry is None:
            print("no entry named", argv[3])
            return 1
        open(argv[4], "wb").write(pack.image(entry))
        print("wrote %s (%d bytes) from %s" %
              (argv[4], entry.size, os.path.basename(pack.path)))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
