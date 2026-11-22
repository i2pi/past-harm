#!/usr/bin/env python3
"""Builds for_musicians.html: runs tools/musiclog (make tools/musiclog first)
for the first 8 minutes with and without the harmony, analyses the harmony
(tools/harmony.py), engraves score excerpts of what the engine actually
played (tools/engrave.py, with Verovio) into docs/figures/, and fills
tools/musicians_template.html.

    python3 tools/mkmusicians.py [out.html]      (needs: pip install verovio)"""
import collections, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(sys.argv[0]))
ROOT = os.path.dirname(HERE)
FIGS = os.path.join(ROOT, 'docs', 'figures')
sys.path.insert(0, HERE)
import harmony as H
import engrave as E

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

def excerpt(src, bar0, nbars):
    lo, hi = bar0 * B, (bar0 + nbars) * B
    notes = []
    for e in src:
        if 'n' in e and lo <= e['n'] < hi:
            length = (e['dur'] + e['amp'] / max(e['r'], 1e-3)) * SR / STEP
            notes.append({'x': round((e['n'] - lo) / STEP, 3), 'len': round(length, 2), 'hold': round(e['dur'] * SR / STEP, 2),
                          'm': H.midi(e), 'k': kind(e), 'v': round(min(1.0, e['amp'] / (2.0 if e['choir'] else 1.0)), 2), 'note': e['note']})
    drums = [{'x': round((d['d'] - lo) / STEP, 3), 'k': d['k'], 'f': d['f'], 'lvl': round(d['lvl'], 2)}
             for d in src if 'd' in d and lo <= d['d'] < hi]
    b = bars.get(bar0, {})
    return {'bar0': bar0, 'bars': nbars, 'key': b.get('key', 0), 'minor': b.get('minor', 0),
            'arc': [bars.get(bar0 + i, {}).get('arc', 0) for i in range(nbars)],
            'mood': b.get('mood', 1), 'notes': notes, 'drums': drums, 'genes': b.get('genes', [3, 3, 5, 7, 11])}

def key_of(bar0):
    st = bars.get(bar0, {})
    mode = 'phrygian' if st.get('mood', 1) < 0.5 else ('minor' if st.get('minor') else 'major')
    return E.Key((1 + st.get('key', 0)) % 12, mode)

def rows_at(times):
    sc = [e for e in ev if 'scale' in e]
    out = []
    for t in times:
        last = ([e for e in sc if e['scale'] <= t * SR] or sc[:1])[-1]
        out.append({'t': t, 's0': last['s0'], 's1': last['s1'], 's2': last['s2']})
    return out

os.makedirs(FIGS, exist_ok=True)
errors = []
def engrave(name, kern, **kw):
    errors.extend(E.render(kern, os.path.join(FIGS, 'music-score-' + name + '.svg'), **kw))

# ---- the harmony, over the whole first eight minutes ----
A = H.analyse(ev, bars, 0, 161)
A_raw = H.analyse(raw, bars, 0, 161)
ch = [a for a in A if a['chord']]
fn = collections.Counter(a['fn'] for a in ch)
q = collections.Counter(a['q'] for a in ch)
seq = []
for a in ch:
    if not seq or seq[-1][1] != a['numeral']: seq.append((a['mode'], a['numeral'], a['fn']))
fseq = [x[2] for x in seq]
fmoves = collections.Counter(zip(fseq, fseq[1:]))
prog = collections.Counter((seq[i][1], seq[i + 1][1]) for i in range(len(seq) - 1) if seq[i][0] == seq[i + 1][0])
# leading tone against subtonic, in the major and minor stretches
lt = st = tonal = 0
for a in ch:
    if a['mode'] == 'phrygian': continue
    tonal += 1
    w = a['w']; tot = sum(w) or 1
    if w[(a['tonic'] + 11) % 12] > 0.1 * tot: lt += 1
    if w[(a['tonic'] + 10) % 12] > 0.1 * tot: st += 1
raw_ch = [a for a in A_raw if a['chord']]
raw_q = collections.Counter(a['q'] for a in raw_ch)
def pct(n, d): return '%d%%' % round(100 * n / max(d, 1))
tert = sum(q[x] for x in ('maj', 'min', 'dim', 'aug', '7', 'm7', 'maj7', 'hdim'))
raw_tert = sum(raw_q[x] for x in ('maj', 'min', 'dim', 'aug', '7', 'm7', 'maj7', 'hdim'))
NAMES = {'T': 'tonic', 'S': 'predominant', 'D': 'dominant', 'modal': 'modal colour'}
top_prog = ', '.join(f'{a} → {b} ({n})' for (a, b), n in prog.most_common(6))
# the bass pairs (two choir notes starting together): their interval
pairs = collections.Counter()
on = collections.defaultdict(list)
for e in ev:
    if 'n' in e and e['choir'] and not e['solo']: on[e['n']].append(H.midi(e))
for t, ms in on.items():
    if len(ms) == 2: pairs[abs(ms[0] - ms[1]) % 12] += 1
ICN = {0: 'unison/octave', 1: 'minor 2nd', 2: 'major 2nd', 3: 'minor 3rd', 4: 'major 3rd', 5: 'perfect 4th', 6: 'tritone',
       7: 'perfect 5th', 8: 'minor 6th', 9: 'major 6th', 10: 'minor 7th', 11: 'major 7th'}
npairs = sum(pairs.values())
pairs_txt = ', '.join(f'{ICN[i]} {pct(n, npairs)}' for i, n in pairs.most_common(5))
def times(n): return {0: 'never', 1: 'once', 2: 'twice'}.get(n, '%d times' % n)
vi = sum(1 for x, y in zip(seq, seq[1:]) if x[0] == y[0] != 'phrygian' and x[2] == 'D' and y[2] == 'T')
b2i = sum(1 for x, y in zip(seq, seq[1:]) if x[0] == y[0] == 'phrygian' and x[1].startswith('-II') and y[1].lower().startswith('i') and not y[1].lower().startswith('ii'))
stats = {
    'beats': str(len(A)), 'chords': str(len(ch)),
    'pT': pct(fn['T'], len(ch)), 'pS': pct(fn['S'], len(ch)), 'pD': pct(fn['D'], len(ch)), 'pM': pct(fn['modal'], len(ch)),
    'p5': pct(q['5'], len(ch)), 'psus': pct(q['sus2'] + q['sus4'], len(ch)), 'ptert': pct(tert, len(ch)),
    'p7': pct(q['7'] + q['m7'] + q['maj7'] + q['hdim'], len(ch)),
    'raw_tert': pct(raw_tert, len(raw_ch)), 'raw_p5': pct(raw_q['5'], len(raw_ch)),
    'TD': str(fmoves[('T', 'D')]), 'DT': str(fmoves[('D', 'T')]), 'TS': str(fmoves[('T', 'S')]), 'ST': str(fmoves[('S', 'T')]),
    'SD': str(fmoves[('S', 'D')]), 'DS': str(fmoves[('D', 'S')]), 'SDw': times(fmoves[('S', 'D')]), 'DSw': times(fmoves[('D', 'S')]),
    'VI': times(vi), 'bIIi': times(b2i), 'moves': str(sum(fmoves.values())),
    'pairs': pairs_txt, 'npairs': str(npairs),
    'lt': pct(lt, tonal), 'st': pct(st, tonal), 'top_prog': top_prog.replace('-', '♭'),
}

# ---- engraved scores ----
ex = {
    'opening': excerpt(ev, 0, 8), 'entrance': excerpt(ev, 20, 8),
    'full': excerpt(ev, 69, 2), 'full_raw': excerpt(raw, 69, 2), 'drums': excerpt(ev, 72, 2),
}
def nums(an, bar0, nb): return [a.get('numeral') if a['chord'] else None for a in an if bar0 <= a['bar'] < bar0 + nb]
for name, (bar0, nb) in {'opening': (0, 8), 'entrance': (20, 8)}.items():
    engrave(name, E.kern_score(ex[name], key_of(bar0), nums(A, bar0, nb)))
engrave('heard', E.kern_score(ex['full'], key_of(69), nums(A, 69, 2)), width=2100)
engrave('raw', E.kern_score(ex['full_raw'], key_of(69), nums(A_raw, 69, 2)), width=2100)
red0, redn = 60, 24
engrave('reduction', E.kern_reduction([a if a['chord'] else None for a in A if red0 <= a['bar'] < red0 + redn], key_of(red0), redn, red0))

# rows, masks, drum pitches, the spectrum of a voice
mm = lambda t: '%d:%02d' % (t // 60, t % 60)
cs = E.Key(1, 'minor')                                   # C# minor, for the examples
engrave('rows', E.kern_free([(mm(r['t']), [61 + v for v in r['s0']], None) for r in rows_at([0, 30, 60, 120, 240, 479])], cs), width=2100)
masks = [('major', [0, 2, 4, 5, 7, 9, 11]), ('minor', [0, 2, 3, 5, 7, 8, 10]), ('major pentatonic', [0, 2, 4, 7, 9]),
         ('minor pentatonic', [0, 3, 5, 7, 10]), ('Phrygian', [0, 1, 3, 5, 7, 8, 10]), ('in', [0, 1, 5, 7, 8])]
engrave('masks', E.kern_free([(n, [61 + v for v in iv], None) for n, iv in masks], E.Key(1, 'phrygian')), width=2100)
R = 280.1
def fold(f, lo):
    while f >= 2 * lo: f /= 2
    while f < lo: f *= 2
    return f
import math
M = lambda f: round(69 + 12 * math.log2(f / 440) - 0.18)
st_ = lambda n: 2 ** (n / 12)
drum = [('thump', [M(fold(R, 55)) + 12], ['root, 8va']), ('log drum', [M(fold(R * st_(7), 150))], ['5th']),
        ('rim', [M(fold(R, 700)), M(fold(R * st_(7), 700))], ['root', '5th']),
        ('bell', [M(fold(R * st_(v), 450)) for v in (3, 7, 12, 0)], ['3rd', '5th', '8ve', 'root']),
        ('harm tick', [M(fold(R * st_(7), 700))], ['5th'])]
engrave('drumpitch', E.kern_free([(n, ms, [n + ': ' + u[0]] + u[1:]) for n, ms, u in drum], cs, dur='2'), width=2100)
f0 = 280.11; N = 8; tot = N * (N - 1) / 2; parts = []
for k in range(1, N):
    freq = k * f0 / 4 if k % 2 else k * f0; h = k if k % 2 else 4 * k
    mid = 69 + 12 * math.log2(freq / 440) - 0.18; m = round(mid)
    parts.append((freq, m, 'h%d %+d¢' % (h, round(100 * (mid - m))), (N - k) / tot))
parts.sort()
engrave('spectrum', E.kern_free([('', [p[1] for p in parts], [p[2] for p in parts])], E.Key(1, 'major'), clef='*clefG2', dur='2'), width=2100)
if errors: print('verovio:', *errors, sep='\n  ')

data = {
    'drums': ex['drums'],
    'changed': sum(1 for a, b in zip([e for e in ev if 'n' in e], [e for e in raw if 'n' in e]) if a['note'] != b['note']),
    'total': sum(1 for e in ev if 'n' in e),
}
page = open(os.path.join(HERE, 'musicians_template.html')).read().replace('/*DATA*/null', json.dumps(data, separators=(',', ':')))
for k, v in stats.items(): page = page.replace('{{' + k + '}}', v)
assert '{{' not in page, page[page.index('{{'):page.index('{{') + 30]
open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'for_musicians.html'), 'w').write(page)
print('ok', len(page), 'notes', data['total'], 'changed by harmony', data['changed'], stats)
