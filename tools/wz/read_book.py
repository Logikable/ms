"""Prints one book beside the GMS readout, for reading a skill at a time.

The two audits compare a skill against the client mechanically. This does not
compare anything -- it lays our file's shape next to what GMS's tooltip says,
so a person can read the pair and see a mechanism we folded that a lever now
expresses. That is the only way to find the shapes a placeholder sweep cannot
name: a Scatter, a Magazine, a Stance, a hold.

    python3 tools/wz/read_book.py HERO            # one job's own book
    python3 tools/wz/read_book.py HERO --common   # and its per-level formulas
    python3 tools/wz/read_book.py --path shared/  # every file under a path
    python3 tools/wz/read_book.py --jobs          # the names it takes

A job selects on `placement`, so a shared skill shows up under every book
naming it. `fifth/` and `shared/` are left out of a job's own read -- they are
books of their own -- and `--all` puts them back.
"""
import argparse
import re
import sys

import audit_skills as A
import ms_pack

# The levers worth seeing at a glance: everything that says what SHAPE a skill
# is, rather than what it is worth. A block name here is a mechanism.
BLOCKS = re.compile(r'^(\w+) \{', re.M)


def ours(job, path, keep_all):
    """Our files in scope: (path, name, text, id prefixes), in path order."""
    out = []
    for rel, (name, text, prefixes) in A.our_skills().items():
        if path is not None:
            if path not in rel:
                continue
        elif not re.search(r'job_advancement:\s*%s\b' % job, text):
            continue
        elif not keep_all and ('/fifth/' in rel or 'shared/' in rel):
            continue
        out.append((rel, name, text, prefixes))
    return sorted(out)


def our_shape(text):
    """The one line saying what our file is: its kind and its sub-messages."""
    blocks = sorted(set(BLOCKS.findall(text)) - {'placement'})
    return '%s  %s' % (A.skill_kind(text), ','.join(blocks) or '-')


def gms_lines(entry):
    """The tooltip, unwrapped: GMS writes the readout as one `h` with \\n in it."""
    out = []
    for field in ('desc', 'h'):
        text = str(entry.get(field, '')).strip()
        if text:
            out.append(text.replace('\\n', '\n      '))
    return out


def formulas(skill_id):
    """The per-level formulas behind that readout, `x` the level."""
    _, common = ms_pack.skill_common(skill_id)
    if not common:
        return []
    return ['%-14s %s' % (k, v) for k, v in common.items()]


def read(job, path, keep_all, show_common):
    gms = A.gms_skills(A.load_cache())
    for rel, name, text, prefixes in ours(job, path, keep_all):
        print('=' * 72)
        print('%s  [%s]' % (name, rel.replace('data/skills/', '')))
        print('  ours: %s' % our_shape(text))
        found = A.candidates(gms.get(name, []), prefixes)
        if not found:
            print('  (no GMS entry in this skill\'s id space)')
        for sid, entry in found:
            print('  %s' % sid)
            for line in gms_lines(entry):
                print('      %s' % line)
            if show_common:
                for line in formulas(sid):
                    print('      %s' % line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('job', nargs='?', help='a job, e.g. HERO or BISHOP')
    parser.add_argument('--path', help='read by path fragment instead, e.g. '
                                       'shared/ or thief/hermit')
    parser.add_argument('--common', action='store_true',
                        help="print each skill's per-level formulas too")
    parser.add_argument('--all', action='store_true',
                        help="keep fifth/ and shared/ in a job's own read")
    parser.add_argument('--jobs', action='store_true',
                        help='list the job names and stop')
    args = parser.parse_args()
    if args.jobs:
        for full in sorted(A.JOB_PREFIX):
            print(full.replace('JOB_ADVANCEMENT_', ''))
        return 0
    if (args.job is None) == (args.path is None):
        parser.error('give a job or --path, not both')
    job = args.job
    if job is not None and not job.startswith('JOB_ADVANCEMENT_'):
        job = 'JOB_ADVANCEMENT_' + job.upper()
    if job is not None and job not in A.JOB_PREFIX:
        parser.error('%s is no job; --jobs lists them' % args.job)
    read(job, args.path, args.all, args.common)
    return 0


if __name__ == '__main__':
    sys.exit(main())
