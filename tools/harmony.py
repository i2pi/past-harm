"""Functional harmonic analysis of what the engine plays (for tools/mkmusicians.py).

Notes come from tools/musiclog. The harmonic rhythm is the choir's: three
beats per bar. In each beat every sounding note is weighted by its envelope
(held level, then the linear release), pitch classes are summed, and the
best-fitting chord template is chosen. The chord is then read in the key the
engine was tracking at the time (Phrygian while the opening's mood is dark),
as a Roman numeral and a function: tonic (T), predominant (S), dominant (D),
or a modal colour that is none of these."""

SR, B = 44100, 131072

def midi(e):
    m = 49 + e['note']
    if e['solo']: return m + 12
    if e['choir']: return m + 12 + 12 * (e['j'] % 2)
    return m + 12 * (e['j'] % 2)

def sounding(ev):
    out = []
    for e in ev:
        if 'n' not in e: continue
        s = e['n']; hold = e['dur'] * SR; rel = e['amp'] / max(e['r'], 1e-3) * SR
        out.append({'s': s, 'h': s + hold, 'e': s + hold + rel, 'amp': e['amp'], 'm': midi(e), 'choir': e['choir'] or e['solo']})
    return out

def env(n, t):
    if t < n['s'] or t >= n['e']: return 0.0
    if t < n['h']: return n['amp']
    return n['amp'] * (1 - (t - n['h']) / (n['e'] - n['h']))

TEMPLATES = [  # name, intervals, numeral case (True = upper), suffix
    ('maj', (0, 4, 7), True, ''), ('min', (0, 3, 7), False, ''), ('dim', (0, 3, 6), False, 'o'),
    ('aug', (0, 4, 8), True, '+'), ('7', (0, 4, 7, 10), True, '7'), ('m7', (0, 3, 7, 10), False, '7'),
    ('maj7', (0, 4, 7, 11), True, 'M7'), ('hdim', (0, 3, 6, 10), False, 'o7'),
    ('sus4', (0, 5, 7), True, 'sus4'), ('sus2', (0, 2, 7), True, 'sus2'), ('5', (0, 7), True, '5'),
]
NAMES_SHARP = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B']
NAMES_FLAT = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'G♭', 'G', 'A♭', 'A', 'B♭', 'B']
SCALES = {'major': [0, 2, 4, 5, 7, 9, 11], 'minor': [0, 2, 3, 5, 7, 8, 10], 'phrygian': [0, 1, 3, 5, 7, 8, 10]}
ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII']

def best_chord(w):
    tot = sum(w)
    if tot < 0.15: return None
    best = None
    for root in range(12):
        for name, iv, up, suf in TEMPLATES:
            pcs = {(root + i) % 12 for i in iv}
            inn = sum(w[p] for p in pcs); out = tot - inn
            missing = sum(1 for p in pcs if w[p] < 0.04 * tot)
            sc = inn - 0.8 * out - 0.25 * tot * missing - 0.02 * tot * len(iv) + (0.05 * tot if w[root] >= max(w) * 0.6 else 0)
            if best is None or sc > best[0]: best = (sc, root, name, up, suf)
    return best[1:]

def numeral(root_c, name, up, suf, tonic_c, mode):
    """Roman numeral (Humdrum **harm spelling: '-' flat, '#' sharp, 'o' dim) and function."""
    iv = (root_c - tonic_c) % 12
    # the degree each interval is named from, with its accidental, per mode
    NAME = {
        'major':    {0: (0, ''), 1: (1, '-'), 2: (1, ''), 3: (2, '-'), 4: (2, ''), 5: (3, ''), 6: (3, '#'), 7: (4, ''), 8: (5, '-'), 9: (5, ''), 10: (6, '-'), 11: (6, '')},
        'minor':    {0: (0, ''), 1: (1, '-'), 2: (1, ''), 3: (2, ''), 4: (2, '#'), 5: (3, ''), 6: (3, '#'), 7: (4, ''), 8: (5, ''), 9: (5, '#'), 10: (6, ''), 11: (6, '#')},
        'phrygian': {0: (0, ''), 1: (1, '-'), 2: (1, ''), 3: (2, ''), 4: (2, '#'), 5: (3, ''), 6: (3, '#'), 7: (4, ''), 8: (5, ''), 9: (5, '#'), 10: (6, ''), 11: (6, '#')},
    }
    deg, acc = NAME[mode][iv]
    r = ROMAN[deg]
    r = r if up else r.lower()
    num = acc + r + suf
    # function
    if mode == 'major':
        F = {0: 'T', 2: 'S', 4: 'T', 5: 'S', 7: 'D', 9: 'T', 11: 'D', 10: 'modal', 8: 'modal', 3: 'modal', 1: 'S', 6: 'D'}
    elif mode == 'minor':
        F = {0: 'T', 2: 'S', 3: 'T', 5: 'S', 7: 'D', 8: 'T', 10: 'modal', 11: 'D', 1: 'S', 4: 'T', 9: 'S', 6: 'D'}
    else:  # phrygian: the b2 is the mode's characteristic tension, resolving down to the tonic
        F = {0: 'T', 1: 'D', 3: 'T', 5: 'S', 7: 'S', 8: 'T', 10: 'S', 2: 'modal', 4: 'modal', 6: 'modal', 9: 'modal', 11: 'modal'}
    return num, F[iv], iv

def analyse(ev, bars, bar0, nbars, beats=3):
    """per beat: dict(t, chord name, numeral, function, key) for bars bar0..bar0+nbars-1"""
    notes = sounding(ev)
    out = []
    for b in range(bar0, bar0 + nbars):
        st = bars.get(b, {})
        key = st.get('key', 0); minor = st.get('minor', 0); mood = st.get('mood', 1)
        tonic_c = (1 + key) % 12
        mode = 'phrygian' if mood < 0.5 else ('minor' if minor else 'major')
        for k in range(beats):
            t0 = b * B + k * B / beats; t1 = t0 + B / beats
            w = [0.0] * 12
            act = [n for n in notes if n['s'] < t1 and n['e'] > t0]
            for n in act:
                for q in range(6):
                    t = t0 + (q + 0.5) * (t1 - t0) / 6
                    w[n['m'] % 12] += env(n, t) / 6 * (1.6 if n['choir'] else 1.0)
            c = best_chord(w)
            if c is None:
                out.append({'bar': b, 'beat': k, 'mode': mode, 'tonic': tonic_c, 'chord': None}); continue
            root, name, up, suf = c
            # the bass: the lowest note sounding with real weight; a close-position voicing from G3 up
            lv = {n['m']: 0.0 for n in act}
            for n in act: lv[n['m']] += env(n, (t0 + t1) / 2)
            strong = [m for m, v in lv.items() if v >= 0.15 * max(lv.values())] if lv else []
            bass = min(strong) if strong else 36 + root
            while bass > 52: bass -= 12
            tmpl = next(t for t in TEMPLATES if t[0] == name)[1]
            v0 = 55 + (root - 55) % 12
            voicing = [v0 + i for i in tmpl]
            num, fn, iv = numeral(root, name, up, suf, tonic_c, mode)
            flats = tonic_c in (5, 10, 3, 8, 1) and not (minor and tonic_c == 1)
            nm = (NAMES_FLAT if flats else NAMES_SHARP)[root] + {'maj': '', 'min': 'm', 'dim': '°', 'aug': '+', '7': '7', 'm7': 'm7', 'maj7': 'maj7', 'hdim': 'ø7', 'sus4': 'sus4', 'sus2': 'sus2', '5': '5'}[name]
            out.append({'bar': b, 'beat': k, 'mode': mode, 'tonic': tonic_c, 'chord': nm, 'numeral': num, 'fn': fn, 'iv': iv, 'q': name,
                        'pcs': sorted({(root + i) % 12 for i in tmpl}), 'bass': bass, 'voicing': voicing, 'w': w})
    return out
