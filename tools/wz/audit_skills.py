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
    ('damR', ['damage_pct'], ANY),
    ('bdR', ['boss_pct'], ANY),
    ('pdR', ['final_dmg_pct'], ANY),
    ('criticaldamage', ['crit_dmg'], ANY),
]

# GMS writes plenty of levers as a bare `#x`/`#y` rather than by name -- Sharp
# Eyes states its critical rate as `#x`. A tooltip carrying one of these could
# be stating any lever, so it is no evidence that a lever we model is absent.
VAGUE = {'x', 'y', 'z', 'u', 'v', 'w', 'q', 's', 'c'}

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
    """Every shipped skill: name -> (path, source text)."""
    out = {}
    pattern = os.path.join(ROOT, 'data', 'skills', '**', '*.textproto')
    for path in glob.glob(pattern, recursive=True):
        text = open(path).read()
        m = re.search(r'^name:\s*"([^"]*)"', text, re.M)
        if m:
            out[m.group(1)] = (os.path.relpath(path, ROOT), text)
    return out


def gms_skills(cache):
    """Every named GMS skill: name -> the richest entry carrying that name."""
    out = {}
    for sid, entry in cache['Skill.img'].items():
        if not isinstance(entry, dict) or not isinstance(entry.get('name'), str):
            continue
        prev = out.get(entry['name'])
        if prev is None or len(str(entry.get('h', ''))) > len(str(prev[1].get('h', ''))):
            out[entry['name']] = (sid, entry)
    return out


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
        path, text = ours[name]
        if name not in theirs:
            if name not in KNOWN_RENAMES:
                findings['unmatched'].append((name, path, ''))
            continue
        matched += 1
        sid, entry = theirs[name]
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
