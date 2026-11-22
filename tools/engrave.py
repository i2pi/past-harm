"""Engraves what the engine plays as conventional notation (for tools/mkmusicians.py).

The notes logged by tools/musiclog are quantised to past's sixteenth grid and
written as Humdrum **kern, three staves (melody, choir, low figures) with
the choir in half-note triplets (it sings on the bar's three beats) and a
**harm spine of Roman numerals under the bass. Verovio engraves it to SVG.

Quantisation, stated plainly: past's melody voices B and C, which sound
23-46 ms after the sixteenth, are merged with voice A on the sixteenth;
the low figures, which start on past's tick rather than the grid, are moved
to the nearest sixteenth; a choir note is written on every beat it is still
sounding above a quarter of its level, tied over."""
import verovio

SR, B, STEP = 44100, 131072, 8192
SCALES = {'major': [0, 2, 4, 5, 7, 9, 11], 'minor': [0, 2, 3, 5, 7, 8, 10], 'phrygian': [0, 1, 3, 5, 7, 8, 10]}
LETTER_PC = [0, 2, 4, 5, 7, 9, 11]          # C D E F G A B
SHARP_ORDER, FLAT_ORDER = 'fcgdaeb', 'beadgcf'
TONIC_NAME = {  # pitch class (C = 0) -> (letter index, alteration), per mode
    'major': {0: (0, 0), 1: (1, -1), 2: (1, 0), 3: (2, -1), 4: (2, 0), 5: (3, 0), 6: (3, 1), 7: (4, 0), 8: (5, -1), 9: (5, 0), 10: (6, -1), 11: (6, 0)},
    'minor': {0: (0, 0), 1: (0, 1), 2: (1, 0), 3: (1, 1), 4: (2, 0), 5: (3, 0), 6: (3, 1), 7: (4, 0), 8: (4, 1), 9: (5, 0), 10: (6, -1), 11: (6, 0)},
}
TONIC_NAME['phrygian'] = TONIC_NAME['minor']

class Key:
    def __init__(self, tonic_c, mode):
        self.mode = mode
        tl, ta = TONIC_NAME[mode][tonic_c]
        self.letters = {}                                    # pc -> (letter, alter) for scale notes
        for d, iv in enumerate(SCALES[mode]):
            L = (tl + d) % 7
            pc = (tonic_c + iv) % 12
            alt = (pc - LETTER_PC[L] + 6) % 12 - 6
            self.letters[pc] = (L, alt)
        self.sig = {L: a for L, a in self.letters.values()}  # letter -> alteration in the signature
        self.flats = sum(1 for a in self.sig.values() if a < 0) > sum(1 for a in self.sig.values() if a > 0)

    def kern_sig(self):
        s = [c for c in SHARP_ORDER if self.sig['cdefgab'.index(c)] > 0] if not self.flats else []
        f = [c + '-' for c in FLAT_ORDER if self.sig['cdefgab'.index(c)] < 0] if self.flats else []
        return '*k[' + ''.join(c + '#' for c in s) + ''.join(f) + ']'

    def spell(self, m):
        pc = m % 12
        if pc in self.letters: L, a = self.letters[pc]
        else:  # chromatic: raise the letter below in sharp keys, lower the one above in flat keys
            cands = [(L, (pc - LETTER_PC[L] + 6) % 12 - 6) for L in range(7)]
            nat = [c for c in cands if c[1] == 0]
            if nat: L, a = nat[0]                    # a natural (it will carry a natural sign)
            else:
                cands = [c for c in cands if abs(c[1]) == 1]
                want = -1 if self.flats else 1
                L, a = next((c for c in cands if c[1] == want), cands[0])
        octv = (m - a) // 12 - 1
        return L, a, octv

    def kern(self, m):
        L, a, o = self.spell(m)
        ch = 'cdefgab'[L]
        name = ch * (o - 3) if o >= 4 else ch.upper() * (4 - o)
        return name + ('#' * a if a > 0 else '-' * (-a))

DUR = {1: '16', 2: '8', 3: '8.', 4: '4', 6: '4.', 8: '2', 12: '2.', 16: '1'}
def split(k):
    out = []
    for d in (16, 12, 8, 6, 4, 3, 2, 1):
        while k >= d: out.append(d); k -= d
    return out

def grid_voice(notes, bar0, nbars, key, max_len=None):
    """notes [{x (16ths from bar0), len, m}] -> per bar: list of (pos, length, [midis] or None)"""
    by = {}
    for n in notes:
        s = int(round(n['x']))
        if 0 <= s < nbars * 16: by.setdefault(s, []).append(n)
    starts = sorted(by)
    bars = [[] for _ in range(nbars)]
    for b in range(nbars):
        pos = 0
        for s in [x for x in starts if b * 16 <= x < (b + 1) * 16]:
            p = s - b * 16
            if p > pos: bars[b].append((pos, p - pos, None))
            nxt = next((x for x in starts if x > s), (b + 1) * 16)
            ln = max(1, int(round(max(n['len'] for n in by[s]))))
            ln = min(ln, nxt - s, (b + 1) * 16 - s)
            ms = sorted({n['m'] for n in by[s]})
            bars[b].append((p, ln, ms))
            pos = p + ln
        if pos < 16: bars[b].append((pos, 16 - pos, None))
    return bars

def grid_tokens(bar, key):
    """-> {position (16ths): token}, eighths and sixteenths beamed within each beat"""
    ev = []                                      # (pos, dur, notes or None, tie state per part)
    for pos, ln, ms in bar:
        parts = split(ln)
        p = pos
        for i, d in enumerate(parts):
            tie = '' if len(parts) == 1 else ('[' if i == 0 else (']' if i == len(parts) - 1 else '_'))
            ev.append((p, d, ms, tie)); p += d
    beam = {}
    for beat in range(4):                        # runs of short notes inside one beat
        run = [e for e in ev if beat * 4 <= e[0] and e[0] + e[1] <= beat * 4 + 4]
        cur = []
        for e in run + [None]:
            if e is not None and e[2] is not None and e[1] < 4: cur.append(e[0]); continue
            if len(cur) >= 2: beam[cur[0]] = 'L'; beam[cur[-1]] = 'J'
            cur = []
    toks = {}
    for p, d, ms, tie in ev:
        if ms is None: toks[p] = DUR[d] + 'r'; continue
        notes = []
        for j, m in enumerate(ms):
            pre = '[' if tie == '[' else ''
            suf = tie if tie in (']', '_') else ''
            notes.append(pre + DUR[d] + key.kern(m) + suf + (beam.get(p, '') if j == 0 else ''))
        toks[p] = ' '.join(notes)
    return toks

def choir_sets(notes, nbars, beats=3, floor=0.25):
    """the pitches sounding (above floor x their level) on each beat"""
    sets = []
    for b in range(nbars):
        for k in range(beats):
            t = (b * 16 + k * 16 / beats)
            s = set()
            for n in notes:
                q = round(n['x'] * beats / 16) * 16 / beats       # onsets on the beat
                if q <= t + 1e-6 and n['x'] + n['len'] > t:
                    # linear release: level at t relative to the note's own
                    hold = n.get('hold', 0)
                    lvl = 1.0 if t - q <= hold else max(0.0, 1 - (t - q - hold) / max(n['len'] - hold, 1e-3))
                    if lvl >= floor: s.add(n['m'])
            sets.append(sorted(s))
    return sets

def choir_tokens(sets, key, beats=3):
    toks = []
    for i, s in enumerate(sets):
        if not s: toks.append(str(beats) + 'r'); continue
        prev = set(sets[i - 1]) if i > 0 else set()
        nxt = set(sets[i + 1]) if i + 1 < len(sets) else set()
        out = []
        for m in s:
            a, z = m in prev, m in nxt
            pre = '[' if (z and not a) else ''
            suf = '_' if (a and z) else (']' if (a and not z) else '')
            out.append(pre + str(beats) + key.kern(m) + suf)
        toks.append(' '.join(out))
    return toks

def kern_score(ex, key, numerals=None, beats=3, title=None, labels=None):
    """ex: an excerpt from tools/mkmusicians.py (notes with x, len, m, k)."""
    nb = ex['bars']
    mel = [n for n in ex['notes'] if n['k'] == 'melody']
    low = [n for n in ex['notes'] if n['k'] == 'low']
    cho = [n for n in ex['notes'] if n['k'] in ('choir', 'solo')]
    mbars, lbars = grid_voice(mel, ex['bar0'], nb, key), grid_voice(low, ex['bar0'], nb, key)
    ctoks = choir_tokens(choir_sets(cho, nb, beats), key, beats)
    cm = sorted(n['m'] for n in cho)
    cclef = '*clefG2' if (cm and cm[len(cm) // 2] >= 60) else '*clefF4'
    head = ['**kern\t**harm\t**kern\t**kern',
            '*I"Low figures\t*\t*I"Choir\t*I"Melody',
            '*clefF4\t*\t' + cclef + '\t*clefG2',
            '\t'.join([key.kern_sig(), '*', key.kern_sig(), key.kern_sig()]),
            '*M4/4\t*\t*M4/4\t*M4/4', '*MM81\t*\t*MM81\t*MM81']
    lines = head[:]
    for b in range(nb):
        lines.append('\t'.join(['=%d' % (ex['bar0'] + b + 1)] * 4))
        lt, mt = grid_tokens(lbars[b], key), grid_tokens(mbars[b], key)
        ct = {k * 16 / beats: ctoks[b * beats + k] for k in range(beats)}
        hm = {}
        for k in range(beats):
            i = b * beats + k
            cur = numerals[i] if numerals else None
            hm[k * 16 / beats] = cur if cur and (i == 0 or numerals[i - 1] != cur) else '.'
        for p in sorted(set(range(16)) | set(ct)):
            row = [lt.get(p, '.'), hm.get(p, '.'), ct.get(p, '.'), mt.get(p, '.')]
            if all(x == '.' for x in row): continue
            lines.append('\t'.join(row))
    lines.append('\t'.join(['=='] * 4)); lines.append('\t'.join(['*-'] * 4))
    return '\n'.join(lines) + '\n'

def kern_reduction(chords, key, nb, bar0, beats=3):
    """a harmonic reduction: bass + close-position chord per beat, numerals and functions."""
    lines = ['**kern\t**text\t**harm\t**kern', '*I"Bass\t*\t*\t*I"Chord', '*clefF4\t*\t*\t*clefG2',
             '\t'.join([key.kern_sig(), '*', '*', key.kern_sig()]), '*M4/4\t*\t*\t*M4/4']
    prev = None
    for b in range(nb):
        lines.append('\t'.join(['=%d' % (bar0 + b + 1)] * 4))
        for k in range(beats):
            c = chords[b * beats + k]
            if not c or not c.get('pcs'):
                lines.append('\t'.join([str(beats) + 'r', '.', '.', str(beats) + 'r'])); prev = None; continue
            bass = c['bass']
            upper = c['voicing']
            kc = (prev is None or c['tonic'] != prev['tonic'] or c['mode'] != prev['mode']) and (b or k)
            kn = KEYNAME[c['mode']][c['tonic']] + ': ' if kc else ''
            lines.append('\t'.join([str(beats) + key.kern(bass), kn + c['fn'], c['numeral'] if c['numeral'] != (prev or {}).get('numeral') or kc else '.',
                                    ' '.join(str(beats) + key.kern(m) for m in upper)]))
            prev = c
    lines.append('\t'.join(['=='] * 4)); lines.append('\t'.join(['*-'] * 4))
    return '\n'.join(lines) + '\n'

def kern_free(groups, key, clef='*clefG2', dur='4'):
    """meterless measures of notes with a text label under the first: groups [(label, [midi...], [under...])]"""
    lines = ['**kern\t**text', clef + '\t*', key.kern_sig() + '\t*', '*MX\t*']
    for i, (label, ms, under) in enumerate(groups):
        lines.append('=%d\t=%d' % (i + 1, i + 1))
        for j, m in enumerate(ms):
            t = (under[j] if under else (label if j == 0 else '.')) or '.'
            lines.append(dur + key.kern(m) + '\t' + t.replace(' ', ' '))
    lines.append('==\t=='); lines.append('*-\t*-')
    return '\n'.join(lines) + '\n'

KEYNAME = {'major': ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'],
           'minor': ['c', 'c♯', 'd', 'd♯', 'e', 'f', 'f♯', 'g', 'g♯', 'a', 'b♭', 'b'],
           'phrygian': ['c phr', 'c♯ phr', 'd phr', 'd♯ phr', 'e phr', 'f phr', 'f♯ phr', 'g phr', 'g♯ phr', 'a phr', 'b♭ phr', 'b phr']}

PAPER = '#fbfaf6'
def render(kern, path, width=2100, scale=38):
    tk = verovio.toolkit()
    tk.setOptions({'pageWidth': width, 'pageHeight': 60000, 'adjustPageHeight': True, 'scale': scale, 'footer': 'none',
                   'header': 'none', 'pageMarginLeft': 40, 'pageMarginRight': 40, 'pageMarginTop': 30, 'pageMarginBottom': 30,
                   'spacingSystem': 14, 'lyricSize': 3.2})
    if not tk.loadData(kern):
        raise RuntimeError('verovio could not read the score: ' + tk.getLog())
    pages = tk.getPageCount()
    svg = tk.renderToSVG(1)
    if pages > 1: raise RuntimeError('score runs to %d pages: %s' % (pages, path))
    # paper behind the music, so it reads in GitHub's dark theme too
    i = svg.index('>', svg.index('<svg')) + 1
    svg = svg[:i] + '<rect x="0" y="0" width="100%" height="100%" fill="' + PAPER + '"/>' + svg[i:]
    open(path, 'w').write(svg)
    log = tk.getLog()
    return [l for l in log.splitlines() if 'Error' in l]
