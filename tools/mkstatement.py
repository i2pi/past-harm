#!/usr/bin/env python3
# Builds technical_statement.html from the source: code blocks are copied
# verbatim (between start / end markers) and HTML-escaped.
import html, os, sys

ROOT = os.path.expanduser('~/src/p10m1-past')

def ex(fn, start, end, after=0):
    s = open(os.path.join(ROOT, fn)).read().split('\n')
    i = next(k for k, l in enumerate(s) if start in l)
    j = next(k for k in range(i, len(s)) if end in s[k]) + after
    lines = s[i:j + 1]
    # strip common leading tabs
    def lead(l): return len(l) - len(l.lstrip('\t'))
    m = min((lead(l) for l in lines if l.strip()), default=0)
    lines = [l[m:] for l in lines]
    code = '\n'.join(lines).replace('\t', '    ')
    return f'<figure class="code"><figcaption>{html.escape(fn)}:{i + 1}</figcaption><pre><code>{html.escape(code)}</code></pre></figure>'

C = {
 'voice': ex('past_engine.c', 'num = 2 + (j % 7);', 's->voices = s->num_on;', after=-4),
 'xfb': ex('past_engine.c', 's->y =  s->y + 0.30*sin', 's->outr += 32768'),
 'snap': ex('past_engine.c', 'static int snap_note', '\treturn note;', after=1),
 'key': ex('key.c', 'for (m = 0; m < 2; m++)', 'if (c > best)', after=1),
 'cap': ex('past_engine.c', 'static int voice_cap', '\t\treturn cap;', after=2),
 'pad': ex('past_engine.c', 'for (h = 1; h * f0 < 10000.0; h++)', 'fft_inverse (re, im, PAD_N);'),
 'bass': ex('past_engine.c', 'static const double det[3]', 's->bass_lp += (y - s->bass_lp)'),
 'poly': ex('past_engine.c', '/* which hit of its bar layer k', 'return (int) ((b * (unsigned long) poly_div (s, k)) / PAST_BAR);', after=1),
 'mutate': ex('past_engine.c', 'static int gcd_i', '\t\t\treturn;', after=3),
 'convolve': ex('harm_fx.c', 'static short\tconvolve (short x, circBufT *buf)', 'return (i386_fists (fsum + bsum));', after=1),
 'taps': ex('harm_fx.c', 'unsigned long D = (unsigned long) h->ldiv;', 'cbuf[j].tap[6].tapnum = fold_tap (HB * 7 / (4 * D));'),
 'glide': ex('harm_fx.c', '/* (added) past\'s networks: when a delay grid mutates', 'else d = buf->data[(buf->idx - buf->tap[n].tapnum) % CBUF_SIZE];'),
 'fdn': ex('reverb.c', '/* the network */', '\t\tif (++r->pos[i] >= r->len[i]) r->pos[i] = 0;', after=1),
 'sparse': ex('reverb.c', '/* how full past is', 'r->rt60 = RT_MIN'),
 'swell': ex('past_engine.c', 'double t = i / 44100.0;\n' if False else 's->choir_rich = (float) (0.5 - 0.5 * cos', 's->bass_rich  = (float)'),
 'orbit': ex('main.c', 'int   note = P.note[j], pc = ((note % 12) + 12) % 12', 'w = dir * (7.0f + 30.0f * env);'),
 'feedback': ex('main.c', 'static const char *fs_feedback', 'gl_FragColor = vec4 (a.rgb + vec3 (tr (d.r)'),
 'flaw': ex('main.c', 'static const char *fs_flaw', 'gl_FragColor = vec4 (0.5 + 0.5 * n.x'),
 'snell': ex('main.c', '/* Snell at the face (n = 1.5)', '"  float path = (0.6 + 0.8 * h) / (0.35 + 0.65 * c);\\n"'),
 'integ': ex('main.c', 'static const char *fs_integ', 'gl_FragColor = vec4 (v); }'),
}

page = open(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), 'statement_template.html')).read()
for k, v in C.items():
    page = page.replace('{{' + k + '}}', v)
assert '{{' not in page, page[page.index('{{'):page.index('{{') + 40]
open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'technical_statement.html'), 'w').write(page)
print('ok', len(page))
