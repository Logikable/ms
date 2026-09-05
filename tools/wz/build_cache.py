"""Builds the local cache of the GMS client's String.wz text.

The client is the freshest source for a skill's name, description and `h`
readout -- the line that says which `#field` each number lives in. Parsing the
WZ on every lookup costs seconds, so this writes one JSON file that every
later query reads instead.

  python3 tools/wz/build_cache.py [--client DIR] [--out FILE]

It is gzipped: 19 MB of JSON, 3 MB on disk, and json.load reads it back in
under a second.

Per-level formulas do NOT live here. They are packed into Data/Packs/*.ms,
which is a format this repo cannot yet read -- see tools/wz/README.md.
"""
import argparse
import gzip
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wz import Wz, read_img

CLIENT = '/mnt/c/Nexon/Games/maplestory/appdata'
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'string_cache.json.gz')

# The String.wz imgs worth caching. Skill.img is the one the audit reads; the
# rest are cheap and answer the next question without a rebuild.
IMGS = ['Skill.img', 'Eqp.img', 'Consume.img', 'Etc.img', 'Ins.img', 'Cash.img',
        'Map.img', 'Mob.img', 'MonsterBook.img', 'Npc.img']


def build(client, out):
    path = os.path.join(client, 'Data', 'String', 'String_000.wz')
    wz = Wz(path)
    cache = {'_version': wz.version, '_source': path}
    for img in IMGS:
        if img not in wz.entries:
            continue
        _, _, off = wz.entries[img]
        tree, _ = read_img(wz.d, off)
        cache[img] = tree
        print('%-18s %d entries' % (img, len(tree)))
    with gzip.open(out, 'wt') as f:
        json.dump(cache, f, separators=(',', ':'), sort_keys=True)
    print('wrote %s (%.1f MB)' % (out, os.path.getsize(out) / 1e6))


def load(path=CACHE):
    """The cache as a dict, for anything that wants to query it."""
    with gzip.open(path, 'rt') as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', default=CLIENT)
    ap.add_argument('--out', default=CACHE)
    args = ap.parse_args()
    build(args.client, args.out)


if __name__ == '__main__':
    main()
