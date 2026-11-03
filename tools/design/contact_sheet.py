#!/usr/bin/env python3
"""Builds the WP4 contact sheet: every scenario of every board, captioned Board:scenario, with the
mockup on the left and the LVGL simulator's render on the right of each pair (brief 6.5).

Usage: contact_sheet.py --lv DIR [--ref DIR] --out contact-sheet.png [--cols 6]
DIR holds index.json and the PNGs written by `ccsim --shots` or `render_boards.js`."""
import argparse
import json
import os

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lv', required=True)
    ap.add_argument('--ref')
    ap.add_argument('--out', required=True)
    ap.add_argument('--cols', type=int, default=6)
    a = ap.parse_args()
    lv = json.load(open(os.path.join(a.lv, 'index.json')))
    ref = {}
    if a.ref and os.path.exists(os.path.join(a.ref, 'index.json')):
        ref = {(e['board'], e['scenario']): os.path.join(a.ref, e['png']) for e in json.load(open(os.path.join(a.ref, 'index.json')))}
    w, h, gap, cap = 240, 320, 4, 16
    cell_w = (2 * w + gap) if ref else w
    rows = (len(lv) + a.cols - 1) // a.cols
    sheet = Image.new('RGB', (a.cols * (cell_w + 12) + 12, rows * (h + cap + 10) + 30), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    d.text((12, 8), 'CC700 contact sheet: each pair is mockup (left) and LVGL simulator (right), 240 x 320, 1x'
           if ref else 'CC700 contact sheet: LVGL simulator, 240 x 320, 1x', fill=(230, 230, 230))
    for i, e in enumerate(lv):
        x = 12 + (i % a.cols) * (cell_w + 12)
        y = 30 + (i // a.cols) * (h + cap + 10)
        d.text((x, y), f"{e['board']}:{e['scenario']}", fill=(230, 230, 230))
        lx = x
        r = ref.get((e['board'], e['scenario']))
        if ref:
            if r:
                sheet.paste(Image.open(r).convert('RGB'), (x, y + cap))
            lx = x + w + gap
        sheet.paste(Image.open(os.path.join(a.lv, e['png'])).convert('RGB'), (lx, y + cap))
    sheet.save(a.out)
    print(a.out, sheet.size, len(lv), 'scenarios')


if __name__ == '__main__':
    main()
