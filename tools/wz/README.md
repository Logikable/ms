# Reading the GMS client

The live client is the freshest source for what a skill actually says. These
tools read it.

    python3 tools/wz/build_cache.py     # rebuild the cache from the client
    python3 tools/wz/audit_skills.py    # every shipped skill vs the client

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
yet. What is known:

- **They are not PKG1 or PKG2.** `NameSpace.dll` reads both magics
  (`0x31474b50`, `0x32474b50`) and the packs carry neither.
- **The first 8 bytes are a salt and a check.** `NameSpace.dll` computes
  `~(rotl(((hash + 0x1a2b3c4d) ^ salt), (salt & 15) + (hash & 15)) ^ salt ^ hash)`
  and compares it to the second word, where `hash` is an FNV-1a of a UTF-16
  name folded with `0x85ebca6b`. Brute-forcing the check leaves one or two
  candidate hashes per pack; the name behind them is still unknown.
- **The imgs inside are plain, unencrypted WZ property data.** Whole regions
  decode with the ordinary `0xAA` mask -- `Skill/_Canvas/1214.img/skill/...`
  path strings, and property trees like
  `70000013 = { common: { maxLevel: 30, madX: ... }, psd: 1 }`. The
  high-entropy stretches between them are canvas bitmaps, not ciphertext.
- **What blocks it is the directory.** Each img's string references are
  offsets from that img's own base, and the bases sit inside the opaque
  stretches. Without the index there is no way to know where an img begins, so
  a reference resolves to nothing. Scanning for parseable islands recovers
  fragments but not whole skills.
- `MapleBrowser_WZ2.exe` and `UI/WZ2Lua` say the new format is called WZ2
  internally. `NameSpace.dll` carries `LAPackage`, `PackageHeader2/3`,
  `ChaCha20Cipher` and `SnowCipher`, and
  `C:\Git\lapacker\lib\FileSystemLib/PackageLoader.inl`.

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
