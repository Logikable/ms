"""Audits every shipped skill's NUMBERS against the client's own formulas.

Its sibling audit_skills.py compares which levers a tooltip names; this one
compares what they are worth. The client states each as a formula in the
skill's `common` block -- `damage = 190+7*x`, `x` being the level -- so a
shipped ladder can be checked at both ends: level 1 and master.

  python3 tools/wz/audit_values.py [--verbose] [--skill NAME]

Reads Data/Packs/*.ms through ms_pack.py, and resolves which GMS id a shipped
skill means with audit_skills.py's own job scoping -- a name alone would
compare a Hero's skill against a Cygnus Knight's.

A row here is a LEAD, not a defect. This game departs from GMS on purpose in
places, and every departure is written down where it was made.
"""
import argparse
import collections
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audit_skills as skills_audit
import ms_pack
import wz

# What to compare: the client's `common` key, how to read the same number off
# a shipped skill, and how close counts as agreement.
#
# Only compared where BOTH sides state it. A skill modelling no cooldown when
# GMS gives one is audit_skills.py's finding, not this one's.
COMPARISONS = [
    ('damage', 'skill_pct', 0.5),
    ('attackCount', 'lines', 0.01),
    ('mobCount', 'max_enemies', 0.01),
    ('cooltime', 'cooldown_seconds', 0.01),
    ('time', 'duration_seconds', 0.01),
]


# Where this game deliberately says something else, and why. Every one is
# argued in the textproto that makes it -- this table is only what keeps them
# out of the way, so a NEW disagreement is visible the moment it appears.
#
# A row is (skill, field). Add one only after reading the file's own comment
# and agreeing with it.
DEPARTURES = {
    ('Arrow Illusion', 'max_enemies'): 'reach rule: 6 off its hitbox',
    ('Assassinate', 'lines'): 'GMS\'s two presses are one swing here',
    ('Brandish', 'max_enemies'): 'reach rule: 4 off a 300x220 hitbox',
    ('Creeping Toxin', 'skill_pct'): 'the client states the detonation, ours the pool',
    ('Cruel Stab', 'max_enemies'): 'reach rule: 6 off its hitbox',
    ('Divine Charge', 'max_enemies'): 'reach rule: 5 off a 415x200 hitbox',
    ('Divine Mark', 'lines'): 'the brand it leaves is priced apart, 7 + 5',
    ('Double Stab', 'max_enemies'): 'reach rule: 4 off its hitbox',
    ('Flame Sweep', 'max_enemies'): 'reach rule: 7 off its hitbox',
    ('Frozen Orb', 'lines'): 'the orb pulses 19 times in one throw',
    ('La Mancha Spear', 'max_enemies'): 'reach rule: 5 off its hitbox',
    ('Last Resort', 'duration_seconds'): 'only GMS\'s second stage is built',
    ('Lightning Orb', 'max_enemies'): 'reach rule: 14 off a 500x400 sphere',
    ('Lucky Seven', 'max_enemies'): 'reach rule: 4 off its hitbox',
    ('Megiddo Flame', 'max_enemies'): 'eleven flames, scattered one enemy apiece',
    ('Midnight Carnival', 'skill_pct'): 'carries Phase Dash, the two are one skill here',
    ('Midnight Carnival', 'max_enemies'): 'reach rule: 5 off a 360x210 hitbox',
    ('Mist Eruption', 'lines'): 'a cast sets off two mists, so ten hits land twice',
    ('Savage Blow', 'max_enemies'): 'reach rule: 4 off its hitbox',
    ('Shuriken Burst', 'max_enemies'): 'reach rule: 2 off its hitbox',
    ('Shuriken Challenge', 'max_enemies'): 'reach rule: 5 off its hitbox',
    ('Slash Blast', 'max_enemies'): 'reach rule: 4 off a 295x175 hitbox',
    ('Spear Sweep', 'max_enemies'): 'reach rule: 5 off a 360x190 hitbox',
    ('Sword Illusion', 'lines'): 'the explosions are priced apart, 4 + 5',
}


def parse_textproto(text):
    """The shipped skill, as nested dicts. Repeated fields become lists."""
    text = re.sub(r'^\s*#.*$', '', text, flags=re.M)
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"|[{}]|[^\s{}]+', text)
    root, stack = {}, []
    current, key = root, None
    index = 0
    while index < len(tokens):
        token = tokens[index]
        index += 1
        if token == '}':
            current = stack.pop()
        elif token.endswith(':'):
            key = token[:-1]
            # A block can be written `base { ... }` or `base: { ... }`.
            if index < len(tokens) and tokens[index] == '{':
                continue
        elif index < len(tokens) and tokens[index] == '{':
            key = token
        elif token == '{':
            child = {}
            stack.append(current)
            _add(current, key, child)
            current = child
        elif key is not None:
            _add(current, key, _value(token))
    return root


def _add(node, key, value):
    if key in node:
        if not isinstance(node[key], list):
            node[key] = [node[key]]
        node[key].append(value)
    else:
        node[key] = value


def _value(token):
    if token.startswith('"'):
        return token[1:-1]
    try:
        return float(token) if '.' in token else int(token)
    except ValueError:
        return token


def formula(expr, level):
    """A client formula at one level. `d()` floors, `u()` ceilings, `x` is the
    level. None for anything else -- a few carry text this cannot read."""
    if isinstance(expr, (int, float)):
        return float(expr)
    text = str(expr).strip()
    if not re.fullmatch(r'[\dxdu+\-*/().% ]+', text):
        return None
    text = re.sub(r'\bd\(', 'math.floor(', text)
    text = re.sub(r'\bu\(', 'math.ceil(', text)
    try:
        return float(eval(text, {'math': math, 'x': level, '__builtins__': {}}))
    except (SyntaxError, ZeroDivisionError, TypeError, NameError):
        return None


def ladder(node, field, level):
    """A shipped ladder at one level: base + per_level * (L - 1)."""
    base = node.get('base', {})
    step = node.get('per_level', {})
    if field not in base and field not in step:
        return None
    return base.get(field, 0.0) + step.get(field, 0.0) * (level - 1)


def ours_at(skill, key, level):
    """What the shipped skill is worth at `level`, in the client's own units."""
    if key == 'skill_pct':
        value = ladder(skill, 'skill_pct', level)
        return None if value is None else value * 100.0
    if key == 'lines':
        if 'lines' not in skill and 'lines_per_level' not in skill:
            return None
        total = skill.get('lines', 0) + skill.get('lines_per_level', 0) * (level - 1)
        # A second hit of the same swing is more of GMS's own attack count --
        # Raging Blow's four are two and two here.
        extra = skill.get('extra_hit', [])
        for hit in extra if isinstance(extra, list) else [extra]:
            total += hit.get('lines', 0)
        return math.floor(total + 1e-9)
    if key == 'max_enemies':
        if 'max_enemies' not in skill:
            return None
        step = skill.get('max_enemies_per_level', 0)
        return math.floor(skill['max_enemies'] + step * (level - 1) + 1e-9)
    if key == 'cooldown_seconds':
        if 'cooldown_seconds' not in skill:
            return None
        step = skill.get('cooldown_seconds_per_level', 0)
        return skill['cooldown_seconds'] + step * (level - 1)
    if key == 'duration_seconds':
        buff = skill.get('buff')
        if not isinstance(buff, dict) or 'duration_seconds' not in buff:
            return None
        step = buff.get('duration_seconds_per_level', 0)
        return buff['duration_seconds'] + step * (level - 1)
    return None


def client_commons(wanted):
    """Every `common` block asked for, one image read apiece.

    `wanted` is name -> [ids]; the answer is name -> [(id, common)], because
    GMS reuses a name across a job's own book and its passive half.
    """
    by_image = collections.defaultdict(list)
    for name, ids in wanted.items():
        for sid in ids:
            key = (sid[:5] if len(sid) == 9 else sid[:3]) + '.img'
            by_image[key].append((name, sid))
    out = collections.defaultdict(list)
    for image, rows in sorted(by_image.items()):
        pack, entry = ms_pack.find('Skill', image)
        if entry is None:
            continue
        tree, _ = wz.read_img(pack.image(entry), 0)
        catalog = tree.get('skill', {})
        for name, sid in rows:
            node = catalog.get(sid)
            if isinstance(node, dict) and isinstance(node.get('common'), dict):
                out[name].append((sid, node['common']))
    return out


def compare(skill, common):
    """One shipped skill against one client entry: the rows that disagree, and
    how many agreed. Master against master and level 1 against level 1 -- a
    book here is rescaled to the levels it has room for, so the two ladders
    meet at their ends and nowhere in between."""
    master = skill.get('max_level', 1)
    client_master = common.get('maxLevel', master)
    rows, agreed = [], 0
    for key, field, tolerance in COMPARISONS:
        if key not in common:
            continue
        for level, level_theirs, label in (
                (master, client_master, 'master'), (1, 1, 'Lv1')):
            mine = ours_at(skill, field, level)
            if mine is None:
                continue
            yours = formula(common[key], level_theirs)
            if yours is None:
                continue
            if abs(mine - yours) > tolerance:
                rows.append((label, field, round(mine, 3), round(yours, 3),
                             '%s @%d' % (common[key], level_theirs)))
            else:
                agreed += 1
    return rows, agreed


def audit(only=None):
    cache = skills_audit.load_cache()
    ours = skills_audit.our_skills()
    theirs = skills_audit.gms_skills(cache)
    wanted = {}
    for name, (path, text, prefixes) in ours.items():
        if only and only.lower() not in name.lower():
            continue
        found = skills_audit.candidates(theirs.get(name, []), prefixes)
        if found:
            wanted[name] = [sid for sid, _ in found]
    commons = client_commons(wanted)

    rows, checked, read = [], 0, 0
    for name in sorted(commons):
        path, text, _ = ours[name]
        skill = parse_textproto(text)
        read += 1
        # GMS gives a swing and its passive half the same name -- Blizzard is
        # both -- so every id in this skill's own job is tried and the one it
        # answers to best is the one it is held to.
        best = None
        for sid, common in commons[name]:
            disagreed, agreed = compare(skill, common)
            score = (agreed, -len(disagreed))
            if best is None or score > best[0]:
                best = (score, sid, disagreed, agreed)
        _, sid, disagreed, agreed = best
        checked += agreed + len(disagreed)
        for label, field, mine, yours, expr in disagreed:
            known = DEPARTURES.get((name, field)) if label == 'master' else None
            rows.append((label, name, path, field, mine, yours, expr, known))
    return read, len(wanted), checked, rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--skill', default=None)
    parser.add_argument('--departures', action='store_true',
                        help='also list the disagreements this game makes on '
                             'purpose, with the reason each was made for')
    parser.add_argument('--level1', action='store_true',
                        help='also list the level-1 ends, which this game '
                             'moves on purpose whenever it rescales a ladder')
    args = parser.parse_args()
    read, wanted, checked, rows = audit(args.skill)
    print('read %d of %d resolved skills from the client, %d values compared'
          % (read, wanted, checked))

    unexplained = [r for r in rows if r[0] == 'master' and r[7] is None]
    known = [r for r in rows if r[0] == 'master' and r[7] is not None]
    print('\nUnexplained at master level: %d' % len(unexplained))
    _table(unexplained, args.verbose)
    print('\nDeliberate departures: %d%s' %
          (len(known), '' if args.departures else ' (--departures to list)'))
    if args.departures:
        _table(known, args.verbose, reason=True)
    if args.level1:
        early = [r for r in rows if r[0] == 'Lv1']
        print('\nLevel 1 disagrees (a rescaled ladder moves this end on '
              'purpose): %d' % len(early))
        _table(early, args.verbose)


def _table(rows, verbose, reason=False):
    if not rows:
        return
    print('%-28s %-16s %9s %9s  %s' %
          ('SKILL', 'WHAT', 'OURS', 'CLIENT', 'WHY' if reason else 'FORMULA'))
    shown = rows if verbose else rows[:40]
    for _, name, path, field, mine, yours, expr, why in shown:
        print('%-28s %-16s %9s %9s  %s' %
              (name[:28], field, mine, yours, why if reason else expr))
    if len(shown) < len(rows):
        print('... %d more (--verbose)' % (len(rows) - len(shown)))


if __name__ == '__main__':
    main()
