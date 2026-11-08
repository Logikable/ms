"""How many monsters each shipped skill's hitbox reaches on our own maps.

The fight has no positions, so a swing hits its max_enemies every time. This
measures what GMS's box would actually reach: the client's `lt`/`rb` against
the client's own spawn points and platforms for every map we ship, with each
monster's own body (its `stand` frame) and the character standing on a
foothold, facing either way, at the spot that reaches most.

  python3 tools/wz/audit_reach.py [--skill NAME] [--maps]

A skill is measured on the maps of its own job stage's levels (a 3rd job skill
on 60-99), capped at GMS's mobCount, and reported as the median over those
maps, with the range. Spawn points, not live positions: monsters wander their
`rx0`-`rx1` span. A projectile with no box, a homing shot's search box, and one tile of
an area attack are listed without a count.
"""
import argparse
import collections
import glob
import math
import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audit_skills as skills_audit
import audit_values as values_audit
import bgm
import ms_pack
import wz

ROOT = skills_audit.ROOT
# kAdvancementLevels in src/character/character.cc, and the cap.
STAGE_LEVELS = [1, 10, 30, 60, 100, 200, 260, 301]
# Spacing of the spots tried along a platform, in pixels.
STEP = 10
# Monsters whose image wz.py couldn't read, given a 70x80 body.
UNREAD = set()


def stage_of(text):
    """The job stage a shipped skill belongs to, from where it sits."""
    if re.search(r'^(v_node|hexa_node):', text, re.M):
        return 6 if 'hexa_node' in text else 5
    stages = set()
    for name in re.findall(r'job_advancement:\s*JOB_ADVANCEMENT_(\S+)', text):
        if name.endswith('_VI'):
            stages.add(6)
        elif name.endswith('_V') or name == 'COMMON':
            stages.add(5)
        elif name in ('SWORDMAN', 'ARCHER', 'MAGICIAN', 'ROGUE'):
            stages.add(1)
        elif name in ('FIGHTER', 'PAGE', 'SPEARMAN', 'HUNTER', 'CROSSBOWMAN',
                      'ICE_LIGHTNING_WIZARD', 'FIRE_POISON_WIZARD', 'CLERIC',
                      'ASSASSIN', 'BANDIT'):
            stages.add(2)
        elif name in ('BERSERKER', 'CRUSADER', 'WHITE_KNIGHT', 'RANGER',
                      'SNIPER', 'ICE_LIGHTNING_MAGE', 'FIRE_POISON_MAGE',
                      'PRIEST', 'HERMIT', 'CHIEF_BANDIT'):
            stages.add(3)
        else:
            stages.add(4)
    return min(stages) if stages else 0


def our_maps():
    """slug -> (name, (lowest, highest) level of its band), from data/maps."""
    out = {}
    for path in glob.glob(os.path.join(ROOT, 'data', 'maps', '*', '*.textproto')):
        name = re.search(r'^name:\s*"([^"]*)"', open(path).read(), re.M).group(1)
        band = os.path.basename(os.path.dirname(path))
        out[bgm.slug(name)] = (name, tuple(int(n) for n in band.split('-')))
    return out


def mob_body(mob_id, cache):
    """A monster's body box from its first standing frame, following `link`."""
    if mob_id in cache:
        return cache[mob_id]
    body = None
    pack, entry = ms_pack.find('Mob', mob_id.zfill(7) + '.img')
    tree = None
    if entry is not None:
        try:
            tree, _ = wz.read_img(pack.image(entry), 0)
        except ValueError:
            UNREAD.add(mob_id)            # a property type wz.py can't read
    if tree is not None:
        link = tree.get('info', {}).get('link')
        if link:
            body = mob_body(str(link), cache)
        else:
            for pose in ('stand', 'fly', 'move'):
                frame = tree.get(pose, {}).get('0')
                if isinstance(frame, dict) and 'lt' in frame:
                    body = (frame['lt'], frame['rb'])
                    break
    cache[mob_id] = body or ((-35, -80), (35, 0))
    return cache[mob_id]


def client_maps():
    """name -> (band, [(left, top, right, bottom) per monster], [spots])."""
    ours = our_maps()
    wanted = {mid: ours[bgm.slug(nm)] for mid, (_, nm) in bgm.map_names().items()
              if bgm.slug(nm) in ours}
    found = {}
    bodies = {}
    for pack in bgm.containers(bgm.MAPS):
        for img in pack.entries:
            mid = img.split('/')[-1][:-4].lstrip('0') or '0'
            if mid not in wanted:
                continue
            props, _ = wz.read_img(pack.d, pack.entries[img][2])
            life = [v for v in props.get('life', {}).values()
                    if isinstance(v, dict) and v.get('type') == 'm']
            name, band = wanted[mid]
            # GMS reuses a map's name for event copies with no monsters; the
            # real one has the most.
            if name in found and len(found[name][1]) >= len(life):
                continue
            mobs = []
            for v in life:
                (l, t), (r, b) = mob_body(str(v['id']), bodies)
                x, y = v['x'], v.get('cy', v.get('y'))
                mobs.append((x + l, y + t, x + r, y + b))
            found[name] = (band, mobs, spots(props.get('foothold', {})))
    return found


def spots(footholds):
    """Every STEP pixels along each walkable foothold."""
    lines = []

    def walk(node):
        if isinstance(node, dict):
            if 'x1' in node and 'y1' in node:
                lines.append((node['x1'], node['y1'], node['x2'], node['y2']))
            else:
                for v in node.values():
                    walk(v)
    walk(footholds)
    out = set()
    for x1, y1, x2, y2 in lines:
        if x1 == x2:
            continue                        # a wall
        lo, hi = sorted((x1, x2))
        for x in range(lo, hi + 1, STEP):
            y = y1 + (y2 - y1) * (x - x1) / (x2 - x1)
            out.add((x, round(y)))
    return sorted(out)


def reach(box, mobs, places, cap):
    """The most monsters the box overlaps from any spot, facing either way."""
    (l, t), (r, b) = box
    best = 0
    for x, y in places:
        for x0, x1 in ((x + l, x + r), (x - r, x - l)):
            y0, y1 = y + t, y + b
            n = 0
            for ml, mt, mr, mb in mobs:
                if ml <= x1 and mr >= x0 and mt <= y1 and mb >= y0:
                    n += 1
            best = max(best, n)
            if best >= cap:
                return cap
    return best


def level_node(node, master):
    levels = node.get('level')
    if isinstance(levels, dict) and levels:
        key = str(master) if str(master) in levels else max(levels, key=int)
        return levels[key]
    return {}


def box_of(node, kind, master):
    """The box GMS hits with, and what kind of box it is."""
    common = node.get('common') or level_node(node, master)
    summon = node.get('summon', {}).get('attack1', {}).get('info', {})
    summon = summon.get('range', {}) if isinstance(summon, dict) else {}
    if 'areaDotCount' in common:
        return None, 'one tile of an area'
    # Homing shots: the box is where they look for a target, each hitting one.
    if 'bulletCount' in common and str(common.get('mobCount', 1)) == '1':
        return None, 'homing'
    if kind == 'SKILL_KIND_AUTO_ATTACK' and 'lt' in summon:
        return (summon['lt'], summon['rb']), 'summon'
    if 'lt' in common:
        return (common['lt'], common['rb']), 'box'
    if 'lt' in summon:
        return (summon['lt'], summon['rb']), 'summon'
    if 'range' in common:
        return None, 'projectile'
    return None, 'no box'


def rule_reach(box):
    """The reach rule Skill.max_enemies states."""
    (l, t), (r, b) = box
    return (r - l) // 70 * (1 + (b - t) // 250)


def skills(only):
    """(name, path, stage, ours, GMS cap, box, box kind) per shipped skill."""
    cache = skills_audit.load_cache()
    ours = skills_audit.our_skills()
    theirs = skills_audit.gms_skills(cache)
    wanted = {}
    for path, (name, text, prefixes) in ours.items():
        if not re.search(r'^max_enemies:', text, re.M):
            continue
        if only and only.lower() not in name.lower():
            continue
        found = skills_audit.candidates(theirs.get(name, []), prefixes)
        wanted[path] = [sid for sid, _ in found]
    nodes = collections.defaultdict(list)
    by_image = collections.defaultdict(list)
    for path, ids in wanted.items():
        for sid in ids:
            key = (sid[:5] if len(sid) == 9 else sid[:3]) + '.img'
            by_image[key].append((path, sid))
    for image, rows in sorted(by_image.items()):
        pack, entry = ms_pack.find('Skill', image)
        if entry is None:
            continue
        tree, _ = wz.read_img(pack.image(entry), 0)
        catalog = tree.get('skill', {})
        for path, sid in rows:
            if isinstance(catalog.get(sid), dict):
                nodes[path].append((sid, catalog[sid]))
    out = []
    for path in sorted(wanted):
        name, text, _ = ours[path]
        skill = values_audit.parse_textproto(text)
        master = skill.get('max_level', 1)
        mine = values_audit.ours_at(skill, 'max_enemies', master)
        kind = skill.get('kind', '')
        # The same choice audit_values.py makes: the id whose numbers agree
        # most, since GMS gives an attack and its passive half one name.
        best = None
        for sid, node in nodes[path]:
            common = node.get('common') or level_node(node, master)
            agreed = values_audit.compare(skill, common)[1] if common else 0
            box, how = box_of(node, kind, master)
            score = (agreed, box is not None)
            if best is None or score > best[0]:
                summon = node.get('summon', {}).get('attack1', {})
                stated = common.get('mobCount',
                                    summon.get('info', {}).get('mobCount'))
                cap = (values_audit.formula(stated, common.get('maxLevel', master))
                       if stated is not None else None)
                best = (score, sid, box, how, cap)
        if best is None:
            out.append((name, path, stage_of(text), mine, None, None, 'no id', ''))
            continue
        _, sid, box, how, cap = best
        # No cap stated: GMS's is then ours, as the reach rule assumes.
        if cap is None and mine:
            cap, sid = mine, sid + '?'
        out.append((name, path, stage_of(text), mine, cap, box, how, sid))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--skill')
    ap.add_argument('--maps', action='store_true',
                    help='print each map measured per skill')
    args = ap.parse_args()
    maps = client_maps()
    rows = []
    for name, path, stage, mine, cap, box, how, sid in skills(args.skill):
        if box is None or not cap:
            rows.append((name, stage, mine, cap, how, '', '', '', ''))
            continue
        lo, hi = STAGE_LEVELS[stage], STAGE_LEVELS[stage + 1]
        counts = []
        for map_name, (band, mobs, places) in sorted(maps.items()):
            if band[0] < hi and band[1] >= lo:
                n = reach(box, mobs, places, int(cap))
                counts.append(n)
                if args.maps:
                    print('   %-28s %-34s %d' % (name, map_name, n))
        (l, t), (r, b) = box
        size = '%dx%d' % (r - l, b - t)
        median = statistics.median(counts) if counts else ''
        spread = '%d-%d' % (min(counts), max(counts)) if counts else ''
        rows.append((name, stage, mine, int(cap), size, rule_reach(box),
                     median, spread, len(counts)))
    if UNREAD:
        print('%d monsters unread, given a 70x80 body: %s' %
              (len(UNREAD), ' '.join(sorted(UNREAD))))
    print('%-34s %5s %4s %4s %-18s %4s %6s %6s %4s' %
          ('skill', 'stage', 'ours', 'GMS', 'box', 'rule', 'median', 'range',
           'maps'))
    for row in sorted(rows, key=lambda r: (r[1], r[0])):
        print('%-34s %5s %4s %4s %-18s %4s %6s %6s %4s' %
              tuple('' if v is None else v for v in row))


if __name__ == '__main__':
    main()
