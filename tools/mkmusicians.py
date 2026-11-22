#!/usr/bin/env python3
"""Builds for_musicians.html: runs tools/musiclog (make tools/musiclog first)
for the first 8 minutes with and without the harmony, cuts score excerpts from
what the engine actually played, and fills tools/musicians_template.html."""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(sys.argv[0]))
ROOT = os.path.dirname(HERE)
B = 131072                      # samples per bar (16 steps of 8192)
STEP = 8192                     # one 16th
SR = 44100

def run(*args):
    out = subprocess.run([os.path.join(HERE, 'musiclog'), *args], capture_output=True, text=True, check=True).stdout
    return [json.loads(l) for l in out.splitlines()]

ev = run('0', '480')
raw = run('-noharmony', '0', '480')
bars = {e['bar']: e for e in ev if 'bar' in e}

def kind(e):
    if e['solo']: return 'solo'
    if e['choir']: return 'choir'
    return 'melody' if e['note'] >= 0 else 'low'

def midi(e):
    # past's note 0 = 140.06 Hz (C#3, +18 cents). Past's voices sound at f * (j%2 + 1);
    # the choir an octave above that, the solo an octave above f.
    m = 49 + e['note']
    if e['solo']: return m + 12
    if e['choir']: return m + 12 + 12 * (e['j'] % 2)
    return m + 12 * (e['j'] % 2)

def excerpt(src, bar0, nbars):
    lo, hi = bar0 * B, (bar0 + nbars) * B
    notes = []
    for e in src:
        if 'n' in e and lo <= e['n'] < hi:
            length = (e['dur'] + e['amp'] / max(e['r'], 1e-3)) * SR / STEP
            notes.append({'x': round((e['n'] - lo) / STEP, 3), 'len': round(length, 2), 'm': midi(e), 'k': kind(e),
                          'v': round(min(1.0, e['amp'] / (2.0 if e['choir'] else 1.0)), 2), 'note': e['note']})
    drums = [{'x': round((d['d'] - lo) / STEP, 3), 'k': d['k'], 'f': d['f'], 'lvl': round(d['lvl'], 2)}
             for d in src if 'd' in d and lo <= d['d'] < hi]
    b = bars.get(bar0, {})
    return {'bar0': bar0, 'bars': nbars, 'key': b.get('key', 0), 'minor': b.get('minor', 0),
            'arc': [bars.get(bar0 + i, {}).get('arc', 0) for i in range(nbars)],
            'mood': b.get('mood', 1), 'notes': notes, 'drums': drums, 'genes': b.get('genes', [3, 3, 5, 7, 11])}

def rows_at(times):
    sc = [e for e in ev if 'scale' in e]
    out = []
    for t in times:
        last = ([e for e in sc if e['scale'] <= t * SR] or sc[:1])[-1]
        out.append({'t': t, 's0': last['s0'], 's1': last['s1'], 's2': last['s2']})
    return out

data = {
    'opening': excerpt(ev, 0, 8),
    'entrance': excerpt(ev, 20, 8),
    'full': excerpt(ev, 69, 2),
    'full_raw': excerpt(raw, 69, 2),
    'drums': excerpt(ev, 72, 2),
    'rows': rows_at([0, 30, 60, 120, 240, 479]),
    'changed': sum(1 for a, b in zip([e for e in ev if 'n' in e], [e for e in raw if 'n' in e]) if a['note'] != b['note']),
    'total': sum(1 for e in ev if 'n' in e),
}
page = open(os.path.join(HERE, 'musicians_template.html')).read().replace('/*DATA*/null', json.dumps(data, separators=(',', ':')))
open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'for_musicians.html'), 'w').write(page)
print('ok', len(page), 'notes', data['total'], 'changed by harmony', data['changed'])
