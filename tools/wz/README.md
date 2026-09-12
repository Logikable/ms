# Reading the GMS client

The live client is the freshest source for what a skill actually says. These
tools read it.

    python3 tools/wz/build_cache.py     # rebuild the cache from the client
    python3 tools/wz/audit_skills.py    # every shipped skill vs the client
    python3 tools/wz/read_book.py HERO  # one book beside its readout, to read
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

## The two audits

    python3 tools/wz/audit_skills.py     # which levers a tooltip NAMES
    python3 tools/wz/audit_values.py     # what those levers are WORTH

`audit_values.py` reads each skill's `common` formula out of the packs and
walks the shipped ladder to meet it: **master against master, level 1 against
level 1.** A book here is rescaled to the levels it has room for, so the two
ladders meet at their ends and nowhere in between -- which is why the level-1
rows are behind `--level1` and mean much less.

**Its 23 known departures are a table in the file**, each with the reason the
textproto argues for it, so a NEW disagreement is the only thing that shows.
Add to that table only after reading the file's own comment and agreeing.

Both audits scope a name to the id space it belongs to: a skill's own job, or
the 5th job pool for a V node -- never both, since GMS gives a 4th job skill
and a V node the same name and the V id sorts first. Where a name still has
several ids -- GMS calls Blizzard's swing and its Final Attack half the same
thing -- `audit_values.py` tries each and holds the skill to the one it
answers to best.

## Calibrating a field

`common` holds the tooltip variables and **the key names mean nothing on their
own** -- the `h` string says which `#field` is which. Weapon Aura's wave
interval is `q`, Solar Crest's is `t`, and both are plain seconds with
decimals, never scaled. A `z` is a hit count in one skill and a cooldown in the
next.

## Reading a book by hand

    python3 tools/wz/read_book.py HERO --common
    python3 tools/wz/read_book.py --path shared/

`read_book.py` compares nothing. It lays our file's shape -- its kind and its
sub-messages -- next to the GMS readout, and `--common` adds the per-level
formulas behind it. That is the only way to find a mechanism no placeholder
names: a swing that scatters, a bank of charges, a buff with several forms, a
hold. The two audits cannot see any of those, because GMS states them in
prose.

It is slow work and meant to be: one book at a time, a person reading the
pair. Assassinate's finishing blow was found this way -- GMS lifts it alone
and we had lifted the whole swing.

## What the audit can and cannot say

It compares which levers a tooltip *names*, not their values. GMS writes many
numbers as a bare `#x`, and hardcodes others into the sentence ("Number of
Attacks: 3"), so a skill stating neither is no evidence of a gap -- `VAGUE` in
`audit_skills.py` is what keeps those quiet. Treat every finding as a lead to
check by hand against the wiki, not a defect.
