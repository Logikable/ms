"""Pulls the game's music out of the client's Sound.wz.

Every BGM MapleStory has ever played is in the installed client as an MP3 --
1134 of them -- so nothing needs downloading or transcoding for the jukebox to
play it.

    python3 tools/wz/bgm.py imgs                    # the 93 Bgm*.img files
    python3 tools/wz/bgm.py ls BgmUI                # one file's tracks
    python3 tools/wz/bgm.py find login              # search every track name
    python3 tools/wz/bgm.py get Bgm02/AboveTheTreetops out.mp3
    python3 tools/wz/bgm.py map "Right Around Lith Harbor"

`map` answers the question that actually comes up: which track does GMS play
there. It reads the name from String.wz and the track from the map's own
`info/bgm`, so a pick is never a guess.
"""
import argparse
import glob
import os
import re
import sys

import wz

CLIENT = '/mnt/c/Nexon/Games/maplestory/appdata/Data'
SOUND = CLIENT + '/Sound/Sound_*.wz'
MAPS = CLIENT + '/Map/Map/Map*/Map*_*.wz'
STRINGS = CLIENT + '/String/String_000.wz'


class SoundReader(wz.ImgReader):
    """An ImgReader that remembers where each sound blob sits.

    `wz.ImgReader` skips a `Sound_DX8` because nothing else wants one. The
    audio is what we are here for, so record the span instead.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.blobs = {}
        self.path = []

    def extended(self, end):
        save = self.p
        name = self.string_block()
        if name in ('Sound_DX8', 'RawData'):
            self.blobs['/'.join(self.path)] = (self.p, end)
            self.p = end
            return '<%s>' % name
        self.p = save
        return super().extended(end)

    def prop_list(self):
        self.u16()
        out = {}
        for _ in range(self.cint()):
            name = self.string_block()
            self.path.append(name)
            out[name] = self.value(self.u8())
            self.path.pop()
        return out


def containers(pattern):
    for path in sorted(glob.glob(pattern)):
        yield wz.Wz(path)


def tracks_in(pack, img):
    """{track name: (start, end)} for one img already found in `pack`."""
    _, _, off = pack.entries[img]
    r = SoundReader(pack.d, off, None, off)
    r.p = off
    r.string_block()
    r.prop_list()
    return {k.split('/')[0]: v for k, v in r.blobs.items()}


def read_sound(img):
    """Returns (file bytes, {track name: (start, end)}) for one Bgm*.img."""
    img = img if img.endswith('.img') else img + '.img'
    for pack in containers(SOUND):
        if img in pack.entries:
            return pack.d, tracks_in(pack, img)
    raise SystemExit('no such sound file: %s' % img)


def carve(data, span):
    """The MP3 inside a sound blob, found by its first frame sync.

    A blob opens with a WAVEFORMATEX header whose length varies. Scanning for
    the sync skips it without having to parse it.
    """
    blob = data[span[0]:span[1]]
    m = re.search(rb'ID3|\xff[\xe0-\xff]', blob)
    return blob[m.start():] if m else blob


def every_track():
    """(img, track, size) for all 1134 of them, reading each pack once."""
    for pack in containers(SOUND):
        for img in pack.entries:
            if not img.startswith('Bgm'):
                continue
            for track, (start, end) in tracks_in(pack, img).items():
                yield img[:-4], track, end - start


def sound_names():
    for pack in containers(SOUND):
        for name in pack.entries:
            if name.startswith('Bgm'):
                yield name[:-4]


def map_names():
    """Every GMS map: id -> (street, name), from String.wz."""
    pack = wz.Wz(STRINGS)
    _, _, off = pack.entries['Map.img']
    props, _ = wz.read_img(pack.d, off)
    out = {}
    for area in props.values():
        if not isinstance(area, dict):
            continue
        for mid, v in area.items():
            if isinstance(v, dict):
                out[mid] = (v.get('streetName', ''), v.get('mapName', ''))
    return out


def slug(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def map_bgm(name):
    """What GMS plays on every map of that name: [(street, map, bgm)]."""
    streets = map_names()
    wanted = {mid for mid, (_, nm) in streets.items() if slug(nm) == slug(name)}
    if not wanted:
        raise SystemExit('no GMS map named %r' % name)
    out = []
    for pack in containers(MAPS):
        for img in pack.entries:
            mid = img[:-4].lstrip('0') or '0'
            if mid not in wanted:
                continue
            _, _, off = pack.entries[img]
            props, _ = wz.read_img(pack.d, off)
            street, nm = streets[mid]
            out.append((street, nm, props.get('info', {}).get('bgm')))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('imgs')
    p = sub.add_parser('ls')
    p.add_argument('img')
    p = sub.add_parser('find')
    p.add_argument('pattern')
    p = sub.add_parser('get')
    p.add_argument('track', help='Bgm02/AboveTheTreetops')
    p.add_argument('out')
    p = sub.add_parser('map')
    p.add_argument('name')
    args = ap.parse_args()

    if args.cmd == 'imgs':
        for name in sorted(sound_names()):
            print(name)
    elif args.cmd == 'ls':
        _, blobs = read_sound(args.img)
        for track, (start, end) in blobs.items():
            print('%-40s %8d bytes' % (track, end - start))
    elif args.cmd == 'find':
        pat = re.compile(args.pattern, re.I)
        for img, track, size in every_track():
            if pat.search(track):
                print('%-46s %8d bytes' % ('%s/%s' % (img, track), size))
    elif args.cmd == 'get':
        img, _, track = args.track.partition('/')
        data, blobs = read_sound(img)
        if track not in blobs:
            raise SystemExit('%s has no track %r' % (img, track))
        mp3 = carve(data, blobs[track])
        open(args.out, 'wb').write(mp3)
        print('%s  %d bytes' % (args.out, len(mp3)))
    elif args.cmd == 'map':
        for street, nm, bgm in map_bgm(args.name):
            print('%-32s %-34s %s' % (street, nm, bgm))


if __name__ == '__main__':
    main()
