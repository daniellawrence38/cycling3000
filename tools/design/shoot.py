#!/usr/bin/env python3
"""Render static previews to PNG (2x), report overflow, and build a contact sheet."""
import glob, os, sys
from playwright.sync_api import sync_playwright
from PIL import Image, ImageDraw

OUT = os.path.join(os.environ.get('CC700_DESIGN_ROOT', os.path.dirname(os.path.abspath(__file__))), 'out')
want = sys.argv[1:]
files = sorted(glob.glob(OUT + '/*.html'))
if want:
    files = [f for f in files if any(os.path.basename(f).startswith(w + '__') for w in want)]

JS = """() => { const out=[]; const root=document.getElementById('b');
 if (root.scrollHeight>root.clientHeight+1) out.push('ROOT height '+root.scrollHeight+' > '+root.clientHeight);
 root.querySelectorAll('*').forEach(el=>{ if(el.closest('svg')) return; const cs=getComputedStyle(el);
   if(el.clientWidth>0 && el.scrollWidth>el.clientWidth+1 && cs.display!=='inline')
     out.push(el.tagName+' "'+(el.textContent||'').trim().slice(0,32)+'" sw='+el.scrollWidth+' cw='+el.clientWidth); });
 return out; }"""

shots = []
with sync_playwright() as p:
    br = p.chromium.launch()
    pg = br.new_page(viewport={'width': 260, 'height': 340}, device_scale_factor=2)
    for f in files:
        pg.goto('file://' + f)
        pg.wait_for_timeout(150)
        pg.evaluate("document.fonts.ready")
        issues = pg.evaluate(JS)
        png = f.replace('.html', '.png')
        pg.locator('#b').screenshot(path=png)
        shots.append((os.path.basename(f)[:-5], png))
        print(os.path.basename(f)[:-5], '->', ('OK' if not issues else issues))
    br.close()

# contact sheet (1x per board, 6 columns)
cols = 6
w, h = 240, 320
rows = (len(shots) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (w + 8) + 8, rows * (h + 26) + 8), (40, 40, 44))
d = ImageDraw.Draw(sheet)
for i, (name, png) in enumerate(shots):
    im = Image.open(png).convert('RGB').resize((w, h))
    x = 8 + (i % cols) * (w + 8)
    y = 8 + (i // cols) * (h + 26)
    d.text((x, y), name, fill=(230, 230, 230))
    sheet.paste(im, (x, y + 14))
sheet.save(OUT + '/_sheet.png')
print('sheet', sheet.size)
