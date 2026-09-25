# Reading the GMS client

The live client is the most current source for what a skill actually says.
These tools read it.

    python3 tools/wz/build_cache.py     # rebuild the cache from the client
    python3 tools/wz/audit_skills.py    # every shipped skill vs the client
    python3 tools/wz/read_book.py HERO  # one book beside its readout, to read
    python3 tools/wz/pack_probe.py      # measure a .ms pack (see below)
    python3 tools/wz/bgm.py find login  # the music, and which map plays what

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
`h`, the readout naming which `#field` holds each number. That's what the
audit compares against.

## The numbers, and the packs that hold them

**Cracked 2026-09-07.** `Data/Packs/Skill_0000N.ms` and `Mob_0000N.ms` open
with `ms_pack.py` -- no key from the client, and no decompiler:

    python3 tools/wz/ms_pack.py common 400011073      # one skill's formulas
    python3 tools/wz/ms_pack.py list Skill            # what each pack holds
    python3 tools/wz/ms_pack.py extract Skill 40001.img out.img

A pack is a **version 4 LAPackage**: a ChaCha20 header, a ChaCha20 entry
table, and images whose **first kilobyte only** is encrypted, which is why half
of one parsed as plaintext before the format was cracked. Every key is derived
from the pack's own file name and a salt stored unencrypted at the front:

- `randByteCount = sum(chars of "skill_00005.ms") % 312 + 30`, and each of
  those bytes is arithmetic-shifted right one.
- The salt follows the version byte and a length, both XORed with `rand[0]`;
  each salt char is `((a | 0x4B) << 1) - a - 75` over `rand[i] ^ salt[2i]`.
- The header key is `(nameWithSalt[i % len] + i) ^ OBSCURE[i]`, the entry-table
  key `(i + (i % 3 + 2) * nameWithSalt[len - 1 - i % len]) ^ OBSCURE[i]`, over
  the same 32 obscure bytes.
- Each image has its own key and nonce, derived from an FNV-1a of the salt,
  the image's name and a 16-byte key in its table row.

**The entry-table reader rewinds the block counter** (`state[12] = 0`) every
time a read lands on a 64-byte boundary. Nothing decodes past the first entry
without it.

The format is Elem8100's and lastbattle's work, from MapleLib
(`MapleLib/WzLib/MSFile/`); this is a Python port of it.

**So the whole client is readable now.** `common` holds every per-level formula
GMS computes its readout from (`damage = 190+7*x`, `attackCount`, `mobCount`,
`cooltime`, `lt`/`rb` hitboxes), with `x` the skill level, `d()` floor and
`u()` ceiling. What isn't there is anything the client hardcodes: Instinctual
Combo's tear states 3 rifts, 6 hits and 6 enemies, and no interval at all.

## The two audits

    python3 tools/wz/audit_skills.py     # which levers a tooltip names
    python3 tools/wz/audit_values.py     # what those levers are worth

`audit_values.py` reads each skill's `common` formula from the packs and
compares the shipped ladder to it: **master against master, level 1 against
level 1.** A book here is rescaled to the levels it has room for, so the two
ladders only meet at their ends. That's why the level-1 rows are behind
`--level1` and mean much less.

**Its known departures are a table in the file**, each with the reason its
textproto gives, so only a new disagreement shows up. Add to that table only
after reading the textproto's comment and agreeing with it.

Both audits limit a name to the id space it belongs to: a skill's own job, or
the 5th job pool for a V node. Never both, since GMS gives a 4th job skill and
a V node the same name and the V id sorts first. Where a name still has several
ids (GMS gives Blizzard's attack and its Final Attack half the same name),
`audit_values.py` tries each and uses the best match.

## Calibrating a field

`common` holds the tooltip variables, and **the key names mean nothing on
their own**: the `h` string says which `#field` is which. Weapon Aura's wave
interval is `q`, Solar Crest's is `t`, and both are plain seconds with
decimals, never scaled. A `z` is a hit count in one skill and a cooldown in the
next.

## Reading a book by hand

    python3 tools/wz/read_book.py HERO --common
    python3 tools/wz/read_book.py --path shared/

`read_book.py` compares nothing. It shows our file's shape (its kind and
sub-messages) next to the GMS readout, and `--common` adds the per-level
formulas behind it. That's the only way to find a mechanism no placeholder
names: an attack that scatters, a bank of charges, a buff with several forms, a
hold. The two audits can't see any of those, because GMS states them in prose.

It's slow work by design: one book at a time, with a person reading the pair.
Assassinate's finishing blow was found this way: GMS boosts only that hit, and
we had boosted the whole attack.

## What the audit can and cannot say

`audit_skills.py` compares which levers a tooltip *names*, not their values.
GMS writes many numbers as a bare `#x` and hardcodes others into the sentence
("Number of Attacks: 3"), so a skill stating neither is no evidence of a gap;
`VAGUE` in `audit_skills.py` suppresses those. Treat every finding as a lead to
check by hand, not a defect.

## The music

`Sound/Sound_*.wz` holds 1134 BGM tracks, already MP3, across 93 `Bgm*.img`.
`bgm.py` lists them, searches them and writes one out:

    python3 tools/wz/bgm.py find login
    python3 tools/wz/bgm.py get Bgm02/AboveTheTreetops out.mp3
    python3 tools/wz/bgm.py map "Right Around Lith Harbor"

A track is a `Sound_DX8` blob starting with a WAVEFORMATEX header of varying
length; `carve` scans to the first frame sync instead of parsing it.

Use `map` when picking a track. It reads the map's name from `String.wz` and
its `info/bgm` from `Map.wz`, so the answer is what GMS actually plays there.
