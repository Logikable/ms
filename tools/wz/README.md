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

## What does not: the numbers

Per-level formulas live in `Data/Packs/Skill_0000N.ms` (9 files, ~950 MB) and
`Mob_0000N.ms`. `Packs.ini` names them (`Skill|8`). Nothing here reads them
yet, but the shape is now known.

**A pack is an encrypted header followed by plaintext bodies.**

- The header measures 5 KB to 850 KB depending on the pack, and carries
  **entropy 7.7-7.99 with under 1% zero bytes**. That is ciphertext.
- Everything after it is **ordinary unencrypted WZ property data**. It decodes
  with the plain `0xAA` ascending mask, entropy 4.9 and ~36% zeros. Parsing
  from `0x1000`-aligned offsets, **44.8% of `Skill_00008.ms` reads as valid
  property trees**, in runs over 100 KB -- real content like
  `70000013 = { common: { maxLevel: 30, madX: ... } }`.
- `NameSpace.dll` names the design: `CipherHeaderStream<ChaCha20Cipher>` and
  `CipherHeaderStream<SnowCipher>` -- a stream that enciphers the header
  alone. It also carries `LAPackage`, `PackageHeader2/3` and a leftover
  `C:\Git\lapacker\lib\FileSystemLib/PackageLoader.inl`.

**What the header holds, and why the bodies are useless without it.** WZ
strings are back-references: a repeated name is written once inline and later
cited by its offset from the img's start. Every pack body cites offsets 1, 44,
70, 201 and the like -- `Property`, `Canvas`, `Shape2D#Vector2D`, `common` --
but **the string `Property` appears nowhere in any pack**, under any mask.
The packer strips each img's header and string table into the encrypted index,
leaving bodies whose every name is a dangling citation. So a body parses, and
still cannot say which skill or which field it describes.

Two dead ends already walked, so nobody repeats them:

- **The bodies are not compressed.** No inflatable zlib stream exists in the
  first 2 MB; the `78 9c 62 60 ...` runs sprinkled through them are empty
  canvas placeholders.
- **The headers do not reuse a keystream.** XOR any two and entropy stays
  above 7.1, so there is no two-time pad to unpick. Consecutive packs share a
  ~250-byte prefix, but that is a common plaintext prologue, not key reuse.

The remaining step is the **ChaCha20/SNOW key**. It is not a constant near the
cipher -- those five call sites are 17 KB unrolled SIMD rounds taking the key
as an argument -- so it has to be traced from the package-open path. That
wants a decompiler.

`pack_probe.py` is the harness for the next attempt: **decrypt a header, drop
it in, and `coverage` says whether it worked.** Today it reads

    header   ends 0x713c  entropy 7.986  zeros 0.90%
    coverage 106 runs >2KB, 11.1 MB parses (44.8%)
    pool     `Property` in file: False

Also known, from the PKG1 reader at `0x1529d7891`: the first 8 bytes of a pack
are a salt and a check,
`~(rotl(((hash + 0x1a2b3c4d) ^ salt), (salt&15)+(hash&15)) ^ salt ^ hash)`,
where `hash` is an FNV-1a over UTF-16 folded with `0x85ebca6b`. Brute-forcing
leaves one or two candidate hashes per pack; the name behind them is unknown.

So the recipe stands: **`h` text and field names from the local String.wz;
per-level values from maplestorywiki.net.**

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
