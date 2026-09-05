"""Audits every shipped skill against the GMS client's own tooltip text.

The client's `h` line names each number its tooltip states -- `#mobCount`,
`#attackCount`, `#cooltime` and the rest. That says which levers GMS gives a
skill, so a skill we model with no cooldown whose tooltip states one, or one we
give a hit count GMS never mentions, is a modelling error this catches.

  python3 tools/wz/audit_skills.py [--verbose]

It reads tools/wz/string_cache.json, so build_cache.py has to have run. The
per-level VALUES still come from the wiki -- they live in Data/Packs/*.ms,
which nothing here can read yet.
"""
import argparse
import collections
import glob
import gzip
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CACHE = os.path.join(HERE, 'string_cache.json.gz')

# A GMS tooltip placeholder, and the proto fields that model what it states.
# Only placeholders that name one lever unambiguously are listed. `#x`, `#y`
# and the flat ATT/MATT pair are whatever a given skill needs, so a skill that
# states them says nothing about which lever it means.
ATTACK = {'SKILL_KIND_ATTACK'}
TIMED = {'SKILL_KIND_ACTIVE'}
ANY = None

# A GMS tooltip placeholder, the proto fields modelling what it states, and the
# skill kinds the comparison means anything for.
#
# Only placeholders naming one lever unambiguously are listed: `#x`, `#y` and
# the flat ATT/MATT pair are whatever a given skill needs.
#
# The kinds matter because this engine folds a buff a player keeps up forever
# into a passive with no clock -- so GMS stating `#time` or `#cooltime` on one
# is the design working, not a gap. Only a skill modelled with a clock of its
# own is worth comparing against GMS's.
LEVERS = [
    ('mobCount', ['max_enemies'], ATTACK),
    ('attackCount', ['lines'], ATTACK),
    ('damage', ['skill_pct'], ATTACK),
    ('cooltime', ['cooldown_seconds'], TIMED),
    ('time', ['duration_seconds', 'duration_seconds_per_level'], TIMED),
    ('ignoreMobpdpR', ['ied_pct'], ANY),
    ('cr', ['crit_rate'], ANY),
    ('damR', ['damage_pct', 'final_dmg_pct_per_combo_orb'], ANY),
    ('bdR', ['boss_pct'], ANY),
    ('pdR', ['final_dmg_pct'], ANY),
    ('criticaldamage', ['crit_dmg'], ANY),
]

# GMS writes plenty of levers as a bare `#x`/`#y` rather than by name -- Sharp
# Eyes states its critical rate as `#x`. A tooltip carrying one of these could
# be stating any lever, so it is no evidence that a lever we model is absent.
VAGUE = {'x', 'y', 'z', 'u', 'v', 'w', 'q', 's', 'c'}

# A GMS skill id is <branch><job><n>: 3120005 is the archer branch, the Bow
# Master's job, its fifth skill. Every other class shares these names -- the
# Wind Archer's Bow Mastery is 13100025 and grants Final Damage the Hunter's
# 3100000 does not -- so an audit that matches on name alone silently compares
# a skill against a different class's. This is what scopes it to Explorers.
JOB_PREFIX = {
    'JOB_ADVANCEMENT_COMMON': '000',
    'JOB_ADVANCEMENT_SWORDMAN': '100',
    'JOB_ADVANCEMENT_FIGHTER': '110',
    'JOB_ADVANCEMENT_CRUSADER': '111',
    'JOB_ADVANCEMENT_HERO': '112',
    'JOB_ADVANCEMENT_PAGE': '120',
    'JOB_ADVANCEMENT_WHITE_KNIGHT': '121',
    'JOB_ADVANCEMENT_PALADIN': '122',
    'JOB_ADVANCEMENT_SPEARMAN': '130',
    'JOB_ADVANCEMENT_BERSERKER': '131',
    'JOB_ADVANCEMENT_DARK_KNIGHT': '132',
    'JOB_ADVANCEMENT_MAGICIAN': '200',
    'JOB_ADVANCEMENT_FIRE_POISON_WIZARD': '210',
    'JOB_ADVANCEMENT_FIRE_POISON_MAGE': '211',
    'JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE': '212',
    'JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD': '220',
    'JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE': '221',
    'JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE': '222',
    'JOB_ADVANCEMENT_CLERIC': '230',
    'JOB_ADVANCEMENT_PRIEST': '231',
    'JOB_ADVANCEMENT_BISHOP': '232',
    'JOB_ADVANCEMENT_ARCHER': '300',
    'JOB_ADVANCEMENT_HUNTER': '310',
    'JOB_ADVANCEMENT_RANGER': '311',
    'JOB_ADVANCEMENT_BOW_MASTER': '312',
    'JOB_ADVANCEMENT_CROSSBOWMAN': '320',
    'JOB_ADVANCEMENT_SNIPER': '321',
    'JOB_ADVANCEMENT_MARKSMAN': '322',
    'JOB_ADVANCEMENT_ROGUE': '400',
    'JOB_ADVANCEMENT_ASSASSIN': '410',
    'JOB_ADVANCEMENT_HERMIT': '411',
    'JOB_ADVANCEMENT_NIGHT_LORD': '412',
    'JOB_ADVANCEMENT_BANDIT': '420',
    'JOB_ADVANCEMENT_CHIEF_BANDIT': '421',
    'JOB_ADVANCEMENT_SHADOWER': '422',
}

# Fifth-job skills sit in an id space of their own: 400 then a two-digit
# branch, so Weapon Aura is 400011000 and Guided Arrow 400031000. A V node
# names no job_advancement -- it belongs to a whole line, or to everyone -- so
# the branch its file sits under is what says which pool to look in.
V_PREFIX = {'common': '40000', 'shared': '40000', 'warrior': '40001',
            'magician': '40002', 'bowman': '40003', 'thief': '40004'}

# Skills whose GMS name this repo deliberately does not use. Each is a design
# decision recorded where it was made, not a name to go fix.
KNOWN_RENAMES = {
    'Advanced Combo Attack - Barricade': 'GMS calls it Barricade Mastery',
    'Elemental Adaptation': 'GMS qualifies it "(Fire, Poison)" / "(Ice, Lightning)"',
    'Final Attack: Bow': 'GMS names bow and crossbow alike "Final Attack"',
    'Final Attack: Crossbow': 'GMS names bow and crossbow alike "Final Attack"',
    'Final Pact - Reduce Cooldown': 'our own hyper for a skill GMS gave none',
    'Frailty Curse - Bulwark': 'our own hyper for a skill GMS gave none',
    'Ice Strike - Extra Strike': 'GMS moved the I/L hypers off Ice Strike',
    'Ice Strike - Reinforce': 'GMS moved the I/L hypers off Ice Strike',
    'Ice Strike - Spread': 'GMS moved the I/L hypers off Ice Strike',
    'Sharp Eyes - Bulwark': 'our own hyper for a skill GMS gave none',
}


def our_skills():
    """Every shipped skill: name -> (path, text, the id prefixes it may take)."""
    out = {}
    pattern = os.path.join(ROOT, 'data', 'skills', '**', '*.textproto')
    for path in glob.glob(pattern, recursive=True):
        text = open(path).read()
        m = re.search(r'^name:\s*"([^"]*)"', text, re.M)
        if not m:
            continue
        rel = os.path.relpath(path, ROOT)
        jobs = set(re.findall(r'job_advancement:\s*(\S+)', text))
        prefixes = {JOB_PREFIX[j] for j in jobs if j in JOB_PREFIX}
        branch = rel.split(os.sep)[2] if rel.count(os.sep) > 2 else ''
        if branch in V_PREFIX:
            prefixes.add(V_PREFIX[branch])
        out[m.group(1)] = (rel, text, prefixes)
    return out


def gms_skills(cache):
    """Named GMS skills: name -> [(id, entry)], Explorer ids only.

    Every id is kept, not the richest. Which one a shipped skill means is the
    caller's to say, from the job its placement names.
    """
    out = collections.defaultdict(list)
    for sid, entry in cache['Skill.img'].items():
        if not isinstance(entry, dict) or not isinstance(entry.get('name'), str):
            continue
        if not sid.isdigit() or len(sid) not in (7, 9):
            continue                          # 8 digits is another class's
        if len(sid) == 9 and not sid.startswith('400'):
            continue                          # 9 digits that are not 5th job
        out[entry['name']].append((sid, entry))
    return out


def pick(entries, prefixes):
    """The entry whose id sits in one of this skill's jobs, if any does."""
    if not prefixes:
        return None
    for sid, entry in sorted(entries):
        key = sid[:5] if len(sid) == 9 else sid.zfill(7)[:3]
        if key in prefixes:
            return sid, entry
    return None


def placeholders(entry):
    """The `#field` names a skill's tooltip states.

    Only `h`. A skill's `ph` is the pre-revamp readout GMS still ships beside
    it, and reading both credits a skill with levers the live one dropped.
    """
    return set(re.findall(r'#([a-zA-Z][a-zA-Z0-9]*)', str(entry.get('h', ''))))


def load_cache():
    with gzip.open(CACHE, 'rt') as f:
        return json.load(f)


def skill_kind(text):
    m = re.search(r'^kind:\s*(\S+)', text, re.M)
    return m.group(1) if m else ''


def has_field(text, field):
    """Whether a field is set anywhere -- `base { skill_pct: 1.2 }` counts."""
    return re.search(r'(?:^|[{\s])%s:' % re.escape(field), text, re.M) is not None


def audit(verbose):
    cache = load_cache()
    ours, theirs = our_skills(), gms_skills(cache)
    findings = collections.defaultdict(list)
    matched = 0
    for name in sorted(ours):
        path, text, prefixes = ours[name]
        found = pick(theirs.get(name, []), prefixes)
        if found is None:
            if name not in KNOWN_RENAMES:
                note = 'no Explorer id in %s' % (','.join(sorted(prefixes)) or '?')
                findings['unmatched'].append((name, path, note))
            continue
        matched += 1
        sid, entry = found
        states = placeholders(entry)
        kind = skill_kind(text)
        for tag, fields, kinds in LEVERS:
            if kinds is not None and kind not in kinds:
                continue
            models = any(has_field(text, f) for f in fields)
            if tag in states and not models:
                findings['missing'].append((name, path, '%s (GMS #%s)' % (fields[0], tag)))
            elif models and tag not in states and not (states & VAGUE):
                findings['extra'].append((name, path, '%s (no GMS #%s)' % (fields[0], tag)))
    return matched, len(ours), findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()
    matched, total, findings = audit(args.verbose)
    print('matched %d/%d shipped skills against GMS %s\n' % (
        matched, total, load_cache()['_version']))
    titles = {
        'unmatched': 'Named nothing in the client (and not a recorded rename)',
        'missing': 'GMS states a number this skill does not model',
        'extra': 'This skill models a number GMS never states',
    }
    for key in ('unmatched', 'missing', 'extra'):
        rows = findings[key]
        print('%s: %d' % (titles[key], len(rows)))
        for name, path, note in rows if args.verbose else rows[:25]:
            print('  %-34s %-22s %s' % (name[:34], note, path))
        if not args.verbose and len(rows) > 25:
            print('  ... %d more (--verbose)' % (len(rows) - 25))
        print()


if __name__ == '__main__':
    main()
