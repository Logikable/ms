# Reading the GMS client

The live client is the freshest source for what a skill actually says. These
tools read it.

    python3 tools/wz/build_cache.py     # rebuild the cache from the client
    python3 tools/wz/audit_skills.py    # every shipped skill vs the client
    python3 tools/wz/pack_probe.py      # measure a .ms pack (see below)

`string_cache.json.gz` is committed, so the audit runs without the client
mounted. Rebuild it only when the client updates.

## What reads

`Data/<Dir>/<Dir>_NNN.wz` is the classic Wizet `PKG1` container, and
`wz.py` reads it whole: header, version hash, encrypted offsets, directory
walk and the img property serialisation.

- **Version 270**, encver 29 at fstart 60.
- Strings are **XOR-only** -- mask `0xAA` ascending for ASCII, `0xAAAA` for
  unicode. No AES key is needed.
- Each `_NNN.wz` is a **complete standalone container** with its own header and
  root directory. Parse each on its own; offsets do not stitch across the set.

`String/String_000.wz` -> `Skill.img` gives every skill's `name`, `desc` and
`h` -- the readout naming which `#field` each number lives in. That is what
the audit compares against.

## The numbers, and the packs that hold them

**Cracked 2026-09-07.** `Data/Packs/Skill_0000N.ms` and `Mob_0000N.ms` open
with `ms_pack.py` -- no key from the client, and no decompiler:

    python3 tools/wz/ms_pack.py common 400011073      # one skill's formulas
    python3 tools/wz/ms_pack.py list Skill            # what each pack holds
    python3 tools/wz/ms_pack.py extract Skill 40001.img out.img

A pack is a **version 4 LAPackage**: a ChaCha20 header, a ChaCha20 entry
table, and images whose **first kilobyte alone** is enciphered -- which is why
half of one parsed as plaintext before any of this. Every key is spelled out
of the pack's own file name and a salt written in the clear at the front:

- `randByteCount = sum(chars of "skill_00005.ms") % 312 + 30`, and each of
  those bytes is arithmetic-shifted right one.
- The salt follows the version byte and a length, both XORed with `rand[0]`;
  each salt char is `((a | 0x4B) << 1) - a - 75` over `rand[i] ^ salt[2i]`.
- The header key is `(nameWithSalt[i % len] + i) ^ OBSCURE[i]`, the entry-table
  key `(i + (i % 3 + 2) * nameWithSalt[len - 1 - i % len]) ^ OBSCURE[i]`, over
  the same 32 obscure bytes.
- Each image has its own key and nonce, spelled out of an FNV-1a of the salt,
  the image's name and a 16-byte key in its table row.

**The entry-table reader rewinds the block counter** (`state[12] = 0`) every
time a read lands on a 64-byte boundary. Nothing decodes past the first entry
without it.

The format is Elem8100's and lastbattle's work, read off MapleLib
(`MapleLib/WzLib/MSFile/`); this is a Python port of what those files say.

**So the whole client is readable now**: `common` carries every per-level
formula GMS computes its readout from -- `damage = 190+7*x`, `attackCount`,
`mobCount`, `cooltime`, `lt`/`rb` hitboxes -- with `x` the skill level, `d()`
floor and `u()` ceiling. What is NOT there is anything the client hardcodes:
Instinctual Combo's tear states 3 rifts, 6 hits and 6 enemies, and no interval
at all.

## Calibrating a field

`common` holds the tooltip variables and **the key names mean nothing on their
own** -- the `h` string says which `#field` is which. Weapon Aura's wave
interval is `q`, Solar Crest's is `t`, and both are plain seconds with
decimals, never scaled. A `z` is a hit count in one skill and a cooldown in the
next.

## What the audit can and cannot say

It compares which levers a tooltip *names*, not their values. GMS writes many
numbers as a bare `#x`, and hardcodes others into the sentence ("Number of
Attacks: 3"), so a skill stating neither is no evidence of a gap -- `VAGUE` in
`audit_skills.py` is what keeps those quiet. Treat every finding as a lead to
check by hand against the wiki, not a defect.
