#!/usr/bin/env python3
"""Builds the GitHub-readable docs:

    docs/TECHNICAL_STATEMENT.md, docs/FOR_MUSICIANS.md, docs/figures/*.svg

The pages are written as HTML (tools/*_template.html, filled by
tools/mkstatement.py and tools/mkmusicians.py), whose figures are drawn by
JavaScript. This script renders each page in headless Chrome, saves every
finished figure as a standalone SVG, and converts the page to Markdown that
GitHub displays (inline <sub>/<sup>/<i> are kept: GitHub allows them).

    python3 -m venv .venv && .venv/bin/pip install verovio     (once)
    make tools/musiclog && .venv/bin/python tools/mkdocs.py
"""
import html, os, re, subprocess, sys, tempfile
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DOCS = os.path.join(ROOT, 'docs')
FIGS = os.path.join(DOCS, 'figures')
CHROME = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
PAGES = [  # generator, template-built html name, markdown name, figure prefix
    ('mkstatement.py', 'technical_statement.html', 'TECHNICAL_STATEMENT.md', 'tech'),
    ('mkmusicians.py', 'for_musicians.html', 'FOR_MUSICIANS.md', 'music'),
]
LINKS = {'technical_statement.html': 'TECHNICAL_STATEMENT.md', 'for_musicians.html': 'FOR_MUSICIANS.md'}
VAR_NAMES = {'ink': 'white', 'sky': 'light blue', 'teal': 'teal', 'orange': 'orange', 'rose': 'rose'}
BG = '#121418'

# ---- a small DOM ----
class Node:
    def __init__(self, tag, attrs, parent):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []
    def cls(self): return self.attrs.get('class', '').split()

VOID = {'br', 'img', 'meta', 'link', 'hr', 'input'}
class Builder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node('root', {}, None); self.cur = self.root
    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur); self.cur.kids.append(n)
        if tag not in VOID: self.cur = n
    def handle_startendtag(self, tag, attrs):
        self.cur.kids.append(Node(tag, attrs, self.cur))
    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root and n.tag != tag: n = n.parent
        if n is not self.root: self.cur = n.parent
    def handle_data(self, d): self.cur.kids.append(d)

def find(n, pred):
    if isinstance(n, str): return
    if pred(n): yield n
    for k in n.kids: yield from find(k, pred)

def text_of(n):
    return n if isinstance(n, str) else ''.join(text_of(k) for k in n.kids)

def slug(t):
    t = t.strip().lower()
    t = re.sub(r'[^\w\- ]', '', t)
    return t.replace(' ', '-')

# ---- SVG export ----
SVG_CASE = {k.lower(): k for k in ('viewBox', 'refX', 'refY', 'markerWidth', 'markerHeight', 'preserveAspectRatio', 'markerUnits', 'textLength', 'lengthAdjust')}
def svg_markup(n):
    if isinstance(n, str): return html.escape(n, quote=False)
    a = ''.join(f' {SVG_CASE.get(k, k)}="{html.escape(v)}"' for k, v in n.attrs.items() if k not in ('class', 'style') and v is not None)
    inner = ''.join(svg_markup(k) for k in n.kids)
    return f'<{n.tag}{a}>{inner}</{n.tag}>'

def save_svg(n, path):
    vb = n.attrs.get('viewbox') or n.attrs.get('viewBox') or '0 0 820 300'
    w, h = vb.split()[2:4]
    body = ''.join(svg_markup(k) for k in n.kids)
    out = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" width="{w}" height="{h}">'
           f'<style>text {{ font-family: "D-DIN", "DIN Alternate", "Helvetica Neue", Helvetica, Arial, sans-serif; }}</style>'
           f'<rect x="-20" y="-20" width="{float(w) + 40}" height="{float(h) + 40}" fill="{BG}"/>{body}</svg>\n')
    open(path, 'w').write(out)

# ---- HTML -> Markdown ----
class MD:
    def __init__(self, prefix, ids):
        self.prefix, self.ids, self.nfig = prefix, ids, 0

    def inline(self, n):
        if isinstance(n, str): return re.sub(r'\s+', ' ', n)
        t, c = n.tag, n.cls()
        inner = ''.join(self.inline(k) for k in n.kids)
        if t == 'br': return '<br>'
        if t in ('em', 'b', 'strong'): return f'**{inner.strip()}**' if inner.strip() else ''
        if t == 'i': return f'<i>{inner}</i>'
        if t in ('sub', 'sup'): return f'<{t}>{inner}</{t}>'
        if t == 'code': return '`' + text_of(n) + '`'
        if t == 'a':
            h = n.attrs.get('href', '')
            if h.startswith('#'): h = '#' + self.ids.get(h[1:], h[1:])
            h = LINKS.get(h, h)
            return f'[{inner.strip()}]({h})'
        if t == 'span' and 'tag' in c: return f'**`{inner.strip()}`** '
        if t == 'span' and 'n' in c: return inner.strip() + '. '
        if t == 'span' and n.attrs.get('style', '').startswith('--c:'):
            v = re.search(r'--(\w+)\)', n.attrs['style']); col = VAR_NAMES.get(v.group(1), v.group(1)) if v else ''
            return f'{col}: {inner.strip()}'
        return inner

    def para(self, n): return self.inline(n).strip()

    def table(self, n):
        rows = list(find(n, lambda x: x.tag == 'tr'))
        out = []
        for i, r in enumerate(rows):
            cells = [self.inline(c).strip().replace('|', '\\|') for c in r.kids if not isinstance(c, str) and c.tag in ('td', 'th')]
            span = sum(int(c.attrs.get('colspan', 1)) - 1 for c in r.kids if not isinstance(c, str) and c.tag in ('td', 'th'))
            cells += [''] * span
            out.append('| ' + ' | '.join(cells) + ' |')
            if i == 0: out.append('|' + '---|' * len(cells))
        return '\n'.join(out)

    def figure(self, n):
        c = n.cls()
        cap = next(find(n, lambda x: x.tag == 'figcaption'), None)
        if 'code' in c:
            pre = next(find(n, lambda x: x.tag == 'pre'))
            src = text_of(cap).strip() if cap else ''
            return f'`{src}`\n\n```c\n{text_of(pre).rstrip()}\n```'
        im = next(find(n, lambda x: x.tag == 'img'), None)
        if im is not None:                           # an engraved score, already a file in docs/figures
            src = im.attrs.get('src', '')
            src = src[len('docs/'):] if src.startswith('docs/') else src
            captxt = self.inline(cap).strip() if cap else ''
            return f'![{im.attrs.get("alt", "")}]({src})' + (f'\n\n<sub>{captxt}</sub>' if captxt else '')
        svg = next(find(n, lambda x: x.tag == 'svg'), None)
        if svg is None: return ''
        self.nfig += 1
        name = f'{self.prefix}-{svg.attrs.get("id", "fig%d" % self.nfig).replace("fig-", "").replace("sc-", "")}.svg'
        save_svg(svg, os.path.join(FIGS, name))
        captxt = self.inline(cap).strip() if cap else ''
        alt = re.sub(r'[\[\]*`<>]', '', svg.attrs.get('aria-label', '') or captxt)[:120]
        return f'![{alt}](figures/{name})\n\n<sub>{captxt}</sub>' if captxt else f'![{alt}](figures/{name})'

    def block(self, n):
        if isinstance(n, str): return [n.strip()] if n.strip() else []
        t, c = n.tag, n.cls()
        if t in ('script', 'style'): return []
        if t == 'h1': return ['# ' + ' '.join(x.strip() for x in n.kids if isinstance(x, str) and x.strip())]
        if t in ('h2', 'h3'): return [('## ' if t == 'h2' else '### ') + self.para(n)]
        if t == 'p': return [self.para(n)]
        if t == 'table': return [self.table(n)]
        if t == 'figure': return [self.figure(n)]
        if t == 'nav':
            return ['\n'.join(f'- {self.inline(a).strip()}' for a in find(n, lambda x: x.tag == 'a'))]
        if t == 'div' and 'math' in c: return ['> ' + self.para(n)]
        if t == 'div' and 'prov' in c: return ['> ' + self.para(n).replace('<br>', '<br>\n> ')]
        if t == 'div' and 'key' in c:
            return ['\n'.join('- ' + self.inline(s).strip() for s in n.kids if not isinstance(s, str))]
        if t == 'div' and ('credit' in c or 'sub' in c): return ['*' + self.para(n) + '*']
        out = []
        for k in n.kids: out += self.block(k)
        return out

def convert(dom_html, prefix, out_md):
    b = Builder(); b.feed(dom_html)
    main = next(find(b.root, lambda x: x.tag == 'main'))
    ids = {}
    for h in find(main, lambda x: x.tag == 'h2' and 'id' in x.attrs):
        ids[h.attrs['id']] = slug(MD(prefix, {}).para(h).replace('**', ''))
    md = MD(prefix, ids)
    blocks = [x for x in md.block(main) if x]
    text = '\n\n'.join(blocks)
    text = re.sub(r'\n{3,}', '\n\n', text)
    open(out_md, 'w').write(text + '\n')
    return md.nfig

def main():
    os.makedirs(FIGS, exist_ok=True)
    for f in os.listdir(FIGS):
        if f.endswith('.svg'): os.remove(os.path.join(FIGS, f))
    with tempfile.TemporaryDirectory() as tmp:
        for gen, page, md, prefix in PAGES:
            # build the page next to fonts/ so it renders as it would in the repo
            path = os.path.join(ROOT, '.' + page)
            subprocess.run([sys.executable, os.path.join(HERE, gen), path], check=True, stdout=subprocess.DEVNULL)
            dom = subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--virtual-time-budget=4000', '--dump-dom', 'file://' + path],
                                 capture_output=True, text=True, check=True).stdout
            os.remove(path)
            n = convert(dom, prefix, os.path.join(DOCS, md))
            print(f'docs/{md}: {n} figures')

if __name__ == '__main__':
    main()
