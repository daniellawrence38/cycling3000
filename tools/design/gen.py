#!/usr/bin/env python3
"""Generate CC700 mockup boards (.dc.html) and static previews from one definition.

Holes are written [[name]] in source and emitted as {{ name }} in the .dc.html.
Scenario values are defined once (Python dicts) and used for both the dc script and the previews.
"""
import json, os, re, sys

ROOT = os.environ.get('CC700_DESIGN_ROOT', os.path.dirname(os.path.abspath(__file__)))
PROJ = ROOT + '/canvas/project'
OUT = ROOT + '/out'
FONTS = ROOT + '/node_modules/@fontsource'
os.makedirs(PROJ, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

COND = "font-family:'Barlow Condensed','Arial Narrow',Arial,sans-serif"

# ---------------------------------------------------------------- icons
PATHS = {
    'chev_r': '<path d="M9 5l7 7-7 7"></path>',
    'chev_l': '<path d="M15 5l-7 7 7 7"></path>',
    'check': '<path d="M5 12.5l4.5 4.5L19 7"></path>',
    'pause': '<path d="M8 5v14M16 5v14"></path>',
    'clock': '<circle cx="12" cy="12" r="9"></circle><path d="M12 7v5l3 2"></path>',
    'warn': '<path d="M12 3l10 18H2z"></path><path d="M12 10v5M12 18v.5"></path>',
    'flag': '<path d="M5 21V4M5 4h12l-2 4 2 4H5"></path>',
    'next': '<path d="M6 5l7 7-7 7M13 5l7 7-7 7"></path>',
    'batt': '<rect x="2" y="7" width="17" height="10" rx="2"></rect><path d="M22 10v4"></path>',
    'bolt': '<path d="M13 2L5 14h6l-1 8 8-12h-6z"></path>',
    'route': '<circle cx="6" cy="19" r="2"></circle><circle cx="18" cy="5" r="2"></circle><path d="M8 19h7a3 3 0 0 0 0-6H9a3 3 0 0 1 0-6h7"></path>',
    'pin': '<path d="M12 21s-6-5.2-6-10a6 6 0 1 1 12 0c0 4.8-6 10-6 10z"></path><circle cx="12" cy="11" r="2"></circle>',
    'cycle': '<path d="M17 2l4 4-4 4"></path><path d="M3 11V9a3 3 0 0 1 3-3h15"></path><path d="M7 22l-4-4 4-4"></path><path d="M21 13v2a3 3 0 0 1-3 3H3"></path>',
    'menu': '<path d="M4 6h16M4 12h16M4 18h16"></path>',
}


def icon(name, size=14, sw=3, color='currentColor'):
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="{color}" '
            f'stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{PATHS[name]}</svg>')


KEBAB = ('<svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">'
         '<circle cx="12" cy="5" r="1.8"></circle><circle cx="12" cy="12" r="1.8"></circle><circle cx="12" cy="19" r="1.8"></circle></svg>')
PLAY = '<svg width="11" height="11" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M6 4l14 8-14 8z"></path></svg>'

STATUS = """<div style="flex:none;height:22px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;background:#12161B;color:#C9D1DA">
<div role="group" aria-label="Connected: GPS, power meter, heart rate, phone" style="display:flex;align-items:center;gap:9px">
<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 21s-6-5.2-6-10a6 6 0 1 1 12 0c0 4.8-6 10-6 10z"></path><circle cx="12" cy="11" r="2"></circle></svg>
<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M13 2L5 14h6l-1 8 8-12h-6z"></path></svg>
<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 20s-7-4.6-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.4-7 10-7 10z"></path></svg>
<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="7" y="2.5" width="10" height="19" rx="2"></rect><path d="M11 18.5h2"></path></svg>
</div>
<div style="display:flex;align-items:center;gap:8px;font-size:11px;font-weight:600;line-height:1">
<span>14:32</span>
<span style="display:flex;align-items:center;gap:3px"><svg width="16" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="2" y="7" width="17" height="10" rx="2"></rect><path d="M22 10v4"></path><rect x="4.5" y="9.5" width="9" height="5" fill="currentColor" stroke="none"></rect></svg><span>68%</span></span>
</div>
</div>"""


# ---------------------------------------------------------------- building blocks
def label(text, extra=''):
    return (f'<div style="height:14px;line-height:14px;font-size:11px;font-weight:600;letter-spacing:.08em;'
            f'color:#9AA4B0;{extra}">{text}</div>')


def num(text, size=28, weight=600, lh=None):
    lh = lh or size - 2
    return (f'<span style="{COND};font-weight:{weight};font-size:{size}px;line-height:{lh}px;'
            f'font-variant-numeric:tabular-nums">{text}</span>')


def unit(text, size=11):
    return f'<span style="font-size:{size}px;font-weight:600;color:#9AA4B0">{text}</span>'


def header36(title, right=''):
    r = f'<span style="font-size:13px;font-weight:500;color:#9AA4B0">{right}</span>' if right else ''
    return (f'<div style="flex:none;height:36px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;'
            f'justify-content:space-between;border-bottom:1px solid #232830"><span style="font-size:18px;font-weight:700;'
            f'letter-spacing:.02em">{title}</span>{r}</div>')


def header44(title, aria='Back', kebab=False, kebab_display=None, size=18):
    kd = f'display:{kebab_display};' if kebab_display else 'display:flex;'
    k = (f'<button type="button" aria-label="More" style="flex:none;{kd}width:44px;height:43px;margin:0;padding:0;border:0;'
         f'background:transparent;color:#F2F5F8;align-items:center;justify-content:center;cursor:pointer">{KEBAB}</button>') if kebab else ''
    return ('<div style="flex:none;height:44px;box-sizing:border-box;display:flex;align-items:center;border-bottom:1px solid #232830">'
            f'<button type="button" aria-label="{aria}" style="flex:none;width:44px;height:43px;margin:0;padding:0;border:0;'
            f'background:transparent;color:#F2F5F8;display:flex;align-items:center;justify-content:center;cursor:pointer">'
            f'{icon("chev_l", 20)}</button>'
            f'<span style="flex:1;min-width:0;padding-right:8px;font-size:{size}px;font-weight:700;letter-spacing:.02em;'
            f'white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{title}</span>{k}</div>')


def chip(text, kind='neutral', h=22, fs=11, extra=''):
    k = {
        'neutral': ('#1B2027', '#F2F5F8', 'solid #3A424C'),
        'pending': ('transparent', '#F2C230', 'dashed #F2C230'),
        'ok': ('#3DBE6B', '#0B0D10', 'solid #3DBE6B'),
        'bad': ('#E5484D', '#0B0D10', 'solid #E5484D'),
    }[kind]
    return (f'<span style="box-sizing:border-box;height:{h}px;padding:0 8px;line-height:{h-2}px;border-radius:{h//2}px;'
            f'font-size:{fs}px;font-weight:700;letter-spacing:.06em;white-space:nowrap;flex:none;background:{k[0]};color:{k[1]};border:1px {k[2]};{extra}">{text}</span>')


def row46(name, sub, right='', focused=False, name_color='#F2F5F8'):
    bg = '#1B2027' if focused else 'transparent'
    sh = 'box-shadow:inset 3px 0 0 #F2F5F8;' if focused else ''
    subspan = (f'<span style="display:block;font-size:12px;font-weight:500;line-height:14px;color:#9AA4B0;white-space:nowrap;'
               f'overflow:hidden;text-overflow:ellipsis">{sub}</span>') if sub else ''
    return (f'<button type="button" style="display:flex;align-items:center;justify-content:space-between;gap:8px;width:100%;'
            f'height:46px;box-sizing:border-box;padding:0 12px 0 11px;margin:0;border:0;border-bottom:1px solid #232830;'
            f'background:{bg};{sh}color:inherit;font:inherit;text-align:left;cursor:pointer">'
            f'<span style="display:block;min-width:0;flex:1"><span style="display:block;font-size:15px;font-weight:600;'
            f'line-height:18px;color:{name_color};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{name}</span>'
            f'{subspan}</span><span style="flex:none;display:flex;align-items:center;gap:6px">{right}</span></button>')


def row46_single(name, right='', focused=False):
    bg = '#1B2027' if focused else 'transparent'
    sh = 'box-shadow:inset 3px 0 0 #F2F5F8;' if focused else ''
    return (f'<button type="button" style="display:flex;align-items:center;justify-content:space-between;gap:8px;width:100%;'
            f'height:46px;box-sizing:border-box;padding:0 12px 0 11px;margin:0;border:0;border-bottom:1px solid #232830;'
            f'background:{bg};{sh}color:inherit;font:inherit;text-align:left;cursor:pointer">'
            f'<span style="font-size:15px;font-weight:600">{name}</span>'
            f'<span style="display:flex;align-items:center;gap:6px">{right}</span></button>')


CHEV = icon('chev_r', 14, 3, '#9AA4B0')


def list_box(inner, thumb=None, h=230):
    t = (f'<div style="position:absolute;right:2px;top:2px;width:3px;height:{thumb}px;border-radius:2px;background:#5B6673"></div>'
         if thumb else '')
    return f'<div style="flex:none;position:relative;height:{h}px;overflow:hidden">{inner}{t}</div>'


def footer(left='BACK', right='OPEN', left_kind='back', right_kind='plain', right_hole_display=None):
    if left_kind == 'back':
        l = f'<span style="display:flex;align-items:center;gap:4px">{icon("chev_l", 12)}{left}</span>'
    else:
        l = f'<span style="display:flex;align-items:center;gap:6px">{icon("menu", 13)}{left}</span>'
    if right_kind == 'plain':
        d = f'display:{right_hole_display};' if right_hole_display else 'display:flex;'
        r = f'<span style="{d}align-items:center;gap:4px">{right}{icon("chev_r", 12)}</span>'
    else:
        r = (f'<span style="display:flex;align-items:center;gap:5px;padding:3px 8px;border-radius:5px;background:#F2F5F8;'
             f'color:#0B0D10">{right}{PLAY}</span>')
    return ('<div style="flex:none;height:32px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;'
            'justify-content:space-between;background:#12161B;color:#C9D1DA;font-size:12px;font-weight:700;letter-spacing:.08em">'
            f'{l}{r}</div>')


def btn(text, kind='primary', h=44, extra=''):
    k = {
        'primary': 'background:#F2F5F8;color:#0B0D10;border:0',
        'danger': 'background:#E5484D;color:#0B0D10;border:0',
        'outline': 'background:#1B2027;color:#F2F5F8;border:1px solid #3A424C',
    }[kind]
    return (f'<button type="button" style="margin:0;padding:0;width:100%;height:{h}px;border-radius:10px;{k};'
            f'font-family:\'Barlow\',\'Arial\',sans-serif;font-size:16px;font-weight:700;letter-spacing:.06em;cursor:pointer;{extra}">{text}</button>')


def dots(active):
    out = []
    for i in range(1, 5):
        w, c = (18, '#F2F5F8') if i == active else (6, '#5B6673')
        out.append(f'<span style="width:{w}px;height:6px;border-radius:3px;background:{c}"></span>')
    return ('<div style="flex:none;height:20px;display:flex;align-items:center;justify-content:center;gap:6px">'
            + ''.join(out) + '</div>')


def dots_holes():
    return ('<div style="flex:none;height:20px;display:flex;align-items:center;justify-content:center;gap:6px">'
            + ''.join(f'<span style="width:[[d{i}w]];height:6px;border-radius:3px;background:[[d{i}c]]"></span>' for i in range(1, 5))
            + '</div>')


def dot_vals(active):
    v = {}
    for i in range(1, 5):
        v[f'd{i}w'], v[f'd{i}c'] = ('18px', '#F2F5F8') if i == active else ('6px', '#5B6673')
    return v


def stat(lbl, value, u='', right=True, h=48, size=28, pad='4px 8px 0'):
    br = 'border-right:1px solid #232830;' if right else ''
    return (f'<div style="box-sizing:border-box;padding:{pad};{br}">{label(lbl)}'
            f'<div style="height:{h-22}px;display:flex;align-items:baseline;gap:3px">{num(value, size)}{unit(u) if u else ""}</div></div>')


# ---------------------------------------------------------------- boards
BOARDS = {}


def board(name, title, body, scen=None, default=None, root_extra='', notes=''):
    scen = scen or {'only': {}}
    default = default or list(scen)[0]
    BOARDS[name] = dict(name=name, title=title, body=body, scen=scen, default=default, root_extra=root_extra)


# 12 Routes -----------------------------------------------------------
board('Routes', 'Routes list',
      STATUS + header36('Routes', '3 / 10')
      + list_box(
          row46('Swan River loop', '38.4 km · 210 m · 2 climbs', CHEV, True)
          + row46('Kings Park hills', '24.6 km · 380 m · 4 climbs', CHEV)
          + row46('Perth Hills 80', '81.2 km · 1,150 m · 7 climbs', CHEV)
          + '<div style="padding:10px 11px 0;font-size:12px;font-weight:500;line-height:16px;color:#9AA4B0">'
            'Send routes from the phone app. Up to 10 are kept on the unit.</div>')
      + footer('BACK', 'OPEN'))

# 13 Route detail -----------------------------------------------------
_pts = [(0, 20), (6, 22), (8, 25), (9.5, 85), (12, 60), (18, 35), (24, 30), (27, 175), (29, 140), (34, 40), (38.4, 20)]
_X = lambda km: round(km / 38.4 * 224, 1)
_Y = lambda e: round(80 - (e - 20) / 155 * 64, 1)
_poly = ' '.join(f'{_X(k)},{_Y(e)}' for k, e in _pts) + ' 224,84 0,84'


def _climb(a, ea, b, eb):
    return f'<polygon points="{_X(a)},{_Y(ea)} {_X(b)},{_Y(eb)} {_X(b)},84 {_X(a)},84" fill="#F08A24"></polygon>'


_profile = (
    '<svg width="224" height="84" viewBox="0 0 224 84" role="img" aria-label="Route elevation profile with two climbs highlighted" style="display:block">'
    f'<g stroke="#0B0D10" stroke-width="1"><polygon points="{_poly}" fill="#3A424C"></polygon>'
    + _climb(8, 25, 9.5, 85) + _climb(24, 30, 27, 175) + '</g></svg>')
board('RouteDetail', 'Route detail',
      STATUS + header44('Swan River loop', 'Back to routes', size=16)
      + '<div style="flex:none;height:106px;box-sizing:border-box;padding:10px 8px 0">' + _profile
      + '<div style="display:flex;justify-content:space-between;height:12px;line-height:12px;font-size:11px;font-weight:600;color:#9AA4B0">'
        '<span>0</span><span>38.4 km</span></div></div>'
      + '<div style="flex:none;height:48px;box-sizing:border-box;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));border-top:1px solid #232830">'
      + stat('DIST', '38.4', 'km') + stat('ASCENT', '210', 'm') + stat('CLIMBS', '2', '', right=False) + '</div>'
      + '<div style="flex:none;height:68px;box-sizing:border-box;padding:8px;border-top:1px solid #232830">'
      + btn('LOAD ROUTE', 'primary', 52, 'font-size:18px') + '</div>'
      + footer('BACK', 'LOAD'))

# 14 History ----------------------------------------------------------
_up = chip(icon('check', 10, 3.2) + ' UPLOADED', 'neutral', 18, 10, 'display:flex;align-items:center;gap:3px')
_wt = chip(icon('clock', 10, 3) + ' WAITING', 'pending', 18, 10, 'display:flex;align-items:center;gap:3px')
board('History', 'History list',
      STATUS + header36('History', '12 rides')
      + list_box(
          row46('31 Oct · 42.3 km', '1:48:12 · avg 214 W', _wt, True)
          + row46('29 Oct · 31.7 km', '1:21:05 · avg 196 W', _up)
          + row46('27 Oct · 56.0 km', '2:19:40 · avg 188 W', _up)
          + row46('25 Oct · 18.9 km', '0:52:18 · avg 175 W', _up)
          + row46('24 Oct · 63.4 km', '2:41:22 · avg 201 W', _up), thumb=96)
      + footer('BACK', 'OPEN'))

# 15 Ride summary -----------------------------------------------------
_sg = ('<div style="flex:none;height:198px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));'
       'grid-template-rows:repeat(3,minmax(0,1fr))">')
_cells = [('TIME', '1:48:12', ''), ('DISTANCE', '42.3', 'km'), ('AVG POWER', '214', 'W'),
          ('AVG HEART RATE', '148', 'bpm'), ('ASCENT', '312', 'm'), ('AVG SPEED', '23.5', 'km/h')]
for _i, (_l, _v, _u) in enumerate(_cells):
    _br = 'border-right:1px solid #232830;' if _i % 2 == 0 else ''
    _sg += (f'<div style="box-sizing:border-box;padding:8px 8px 0;{_br}border-bottom:1px solid #232830">{label(_l)}'
            f'<div style="display:flex;align-items:baseline;gap:4px;margin-top:4px">{num(_v, 34, 600, 36)}{unit(_u, 13) if _u else ""}</div></div>')
_sg += '</div>'
board('Summary', 'Ride summary',
      STATUS
      + '<div style="flex:none;height:40px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;border-bottom:1px solid #232830">'
        '<span style="font-size:20px;font-weight:700;letter-spacing:.01em">Ride saved</span></div>'
      + '<div style="flex:none;height:28px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="font-size:12px;font-weight:500;color:#9AA4B0">Sat 31 Oct · 14:32</span>'
        '<span style="box-sizing:border-box;height:18px;padding:0 6px;line-height:16px;border-radius:9px;font-size:10px;font-weight:700;letter-spacing:.06em;'
        'background:transparent;color:[[chipFg]];border:1px [[chipBorder]]">[[chipText]]</span></div>'
      + _sg + footer('BACK', 'DONE'),
      scen={'waiting': dict(chipText='WAITING FOR PHONE', chipFg='#F2C230', chipBorder='dashed #F2C230'),
            'uploaded': dict(chipText='UPLOADED', chipFg='#F2F5F8', chipBorder='solid #3A424C')},
      default='waiting')

# 16 Paused -----------------------------------------------------------
board('Paused', 'Paused screen',
      STATUS
      + '<div style="flex:none;height:44px;box-sizing:border-box;padding:0 10px;display:flex;align-items:center;gap:8px;background:#F2C230;color:#0B0D10">'
        + icon('pause', 20, 3.2) + '<span style="flex:1;font-size:20px;font-weight:700;letter-spacing:.04em">[[title]]</span>'
        f'<span style="{COND};font-weight:700;font-size:24px">[[since]]</span></div>'
      + '<div style="flex:none;height:40px;box-sizing:border-box;padding:6px 10px 0;font-size:12px;font-weight:500;line-height:16px;color:#9AA4B0">[[hint]]</div>'
      + '<div style="flex:none;height:100px;box-sizing:border-box;padding:6px 8px 0;border-bottom:1px solid #232830">' + label('ELAPSED')
      + f'<div style="margin-top:2px">{num("1:12:47", 68, 700, 72)}</div></div>'
      + '<div style="flex:none;height:66px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));border-bottom:1px solid #232830">'
      + stat('DISTANCE', '32.6', 'km', True, 66, 36, '8px 8px 0') + stat('AVG POWER', '221', 'W', False, 66, 36, '8px 8px 0') + '</div>'
      + '<div style="flex:1"></div>'
      + footer('MENU', 'RESUME', left_kind='menu', right_kind='primary'),
      scen={'manual': dict(title='PAUSED', since='0:42', hint='Paused by you. Press RESUME to continue.'),
            'auto': dict(title='AUTO-PAUSED', since='0:15', hint='You stopped moving. Recording resumes when you ride.')},
      default='manual')

# 17 Pause menu -------------------------------------------------------
board('PauseMenu', 'Pause menu',
      STATUS + header36('Ride paused', '1:12:47')
      + list_box(
          row46('Resume', 'Keep recording', CHEV, True)
          + row46('Finish and save', 'Stop and save this ride', CHEV)
          + row46('Discard ride', 'Delete without saving', CHEV, name_color='#F2767A'))
      + footer('BACK', 'SELECT'))

# 18 Confirm ----------------------------------------------------------
board('Confirm', 'Confirm dialog',
      STATUS + header44('[[title]]', 'Cancel', size=18)
      + '<div style="flex:none;height:94px;box-sizing:border-box;padding:12px 10px 0;font-size:14px;font-weight:500;line-height:19px;color:#F2F5F8">[[msg]]</div>'
      + '<div style="flex:none;height:128px;box-sizing:border-box;padding:8px;display:flex;flex-direction:column;gap:8px">'
      + btn('[[destr]]', 'danger', 52) + btn('[[safe]]', 'outline', 52, 'box-shadow:0 0 0 2px #F2F5F8')
      + '</div>' + footer('BACK', '[[safeShort]]'),
      scen={'discard': dict(title='Discard ride?', msg='This permanently deletes the ride (1:12:47, 32.6 km). It has not been uploaded.',
                            destr='DISCARD RIDE', safe='KEEP RIDE', safeShort='KEEP'),
            'forgetsensor': dict(title='Forget sensor?', msg='PM500 ···7C2E will be removed from this unit. You can pair it again from Sensors.',
                                 destr='FORGET SENSOR', safe='KEEP SENSOR', safeShort='KEEP'),
            'forgetphone': dict(title='Forget phone?', msg='The phone must be paired again before it can send workouts or routes, or collect rides.',
                                destr='FORGET PHONE', safe='KEEP PHONE', safeShort='KEEP')},
      default='discard')

# 19 Alerts -----------------------------------------------------------
_main_page = (
    '<div style="flex:none;height:118px;box-sizing:border-box;padding:8px 8px 4px;display:flex;flex-direction:column;border-bottom:1px solid #232830">'
    + label('POWER · 3S')
    + '<div style="flex:1;display:flex;align-items:center;gap:6px">'
      f'<span style="{COND};font-weight:700;font-size:96px;line-height:84px;font-variant-numeric:tabular-nums;color:[[powerFg]]">[[powerText]]</span>'
      '<span style="align-self:flex-end;padding-bottom:12px;font-size:22px;font-weight:600;color:#9AA4B0">W</span>'
      '<span style="margin-left:auto;padding:1px 8px;border-radius:5px;background:[[zoneBg]];color:[[zoneFg]];font-size:20px;font-weight:700">[[zoneText]]</span></div>'
    '<div style="display:flex;gap:2px;height:8px"><div style="flex:1;border-radius:2px;background:#1B2027"></div>'
    '<div style="flex:1;border-radius:2px;background:#1B2027"></div><div style="flex:1;border-radius:2px;background:#1B2027"></div>'
    '<div style="flex:1;border-radius:2px;background:[[seg4]]"></div><div style="flex:1;border-radius:2px;background:#1B2027"></div>'
    '<div style="flex:1;border-radius:2px;background:#1B2027"></div><div style="flex:1;border-radius:2px;background:#1B2027"></div></div></div>'
    '<div style="flex:1;min-height:0;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));grid-template-rows:repeat(2,minmax(0,1fr))">'
    '<div style="box-sizing:border-box;padding:6px 8px;border-right:1px solid #232830;border-bottom:1px solid #232830">' + label('HEART RATE')
    + f'<div style="height:48px;display:flex;align-items:flex-end;gap:4px">{num("152", 46, 600, 46)}<span style="padding-bottom:5px;font-size:13px;font-weight:600;color:#9AA4B0">bpm</span></div></div>'
    '<div style="box-sizing:border-box;padding:6px 8px;border-bottom:1px solid #232830">' + label('CADENCE')
    + f'<div style="height:48px;display:flex;align-items:flex-end;gap:4px">{num("[[cadText]]", 46, 600, 46)}<span style="padding-bottom:5px;font-size:13px;font-weight:600;color:#9AA4B0">rpm</span></div></div>'
    '<div style="box-sizing:border-box;padding:6px 8px;border-right:1px solid #232830">' + label('SPEED')
    + f'<div style="height:48px;display:flex;align-items:flex-end;gap:4px">{num("32.4", 46, 600, 46)}<span style="padding-bottom:5px;font-size:13px;font-weight:600;color:#9AA4B0">km/h</span></div></div>'
    '<div style="box-sizing:border-box;padding:6px 8px">' + label('ELAPSED')
    + f'<div style="height:48px;display:flex;align-items:flex-end"><span style="padding-bottom:2px">{num("1:12:47", 34, 600, 34)}</span></div></div></div>'
    + dots(1))
_ico = lambda key, name: f'<span style="display:[[{key}]];align-items:center">{icon(name, 20, 3)}</span>'
_banner = (
    '<div style="position:absolute;left:0;right:0;bottom:0;height:52px;box-sizing:border-box;display:[[bnDisplay]];align-items:center;gap:8px;'
    'padding:0 10px;background:[[bnBg]];color:[[bnFg]];border-left:6px solid [[bnAccent]];box-shadow:0 -2px 0 #0B0D10">'
    + _ico('icoNext', 'next') + _ico('icoCheck', 'check') + _ico('icoWarn', 'warn') + _ico('icoFlag', 'flag') + _ico('icoBatt', 'batt')
    + '<span style="flex:1;min-width:0"><span style="display:block;font-size:15px;font-weight:700;letter-spacing:.04em;line-height:18px;white-space:nowrap">[[bnTitle]]</span>'
      '<span style="display:block;font-size:12px;font-weight:600;line-height:14px;white-space:nowrap">[[bnSub]]</span></span>'
      f'<span style="{COND};font-weight:700;font-size:28px;line-height:28px;font-variant-numeric:tabular-nums">[[bnRight]]</span></div>')
_hold = (
    '<div style="position:absolute;left:0;right:0;bottom:0;height:36px;box-sizing:border-box;display:[[holdDisplay]];flex-direction:column;justify-content:center;'
    'padding:0 10px;background:#12161B;border-top:1px solid #3A424C"><div style="display:flex;justify-content:space-between;font-size:12px;font-weight:700;letter-spacing:.08em">'
    '<span>HOLD TO PAUSE</span><span style="color:#9AA4B0">[[holdSecs]]</span></div>'
    '<div style="margin-top:4px;height:6px;border-radius:3px;background:#1B2027;overflow:hidden"><div style="width:[[holdPct]];height:100%;background:#F2F5F8"></div></div></div>')


def _a(**kw):
    base = dict(bnDisplay='flex', bnBg='#1B2027', bnFg='#F2F5F8', bnAccent='#F2F5F8', bnTitle='', bnSub='', bnRight='',
                icoNext='none', icoCheck='none', icoWarn='none', icoFlag='none', icoBatt='none',
                holdDisplay='none', holdSecs='', holdPct='0%',
                powerText='248', powerFg='#F2F5F8', zoneText='Z4', zoneBg='#F2C230', zoneFg='#0B0D10', seg4='#F2C230', cadText='88',
                battPct='68%')
    base.update(kw)
    return base


board('Alerts', 'Ride alerts',
      STATUS.replace('<span>68%</span>', '<span>[[battPct]]</span>') + _main_page + _banner + _hold,
      scen={
          'nextstep': _a(bnBg='#1B2027', bnAccent='#8E9AA6', icoNext='flex', bnTitle='NEXT · RECOVER', bnSub='136–150 W for 4:00', bnRight='5 s'),
          'lap': _a(bnBg='#1B2027', bnAccent='#F2F5F8', icoFlag='flex', bnTitle='LAP 3', bnSub='5:12 · 2.4 km', bnRight=''),
          'complete': _a(bnBg='#3DBE6B', bnFg='#0B0D10', bnAccent='#3DBE6B', icoCheck='flex', bnTitle='WORKOUT COMPLETE', bnSub='4 x 8 min threshold'),
          'powerlost': _a(bnBg='#E5484D', bnFg='#0B0D10', bnAccent='#E5484D', icoWarn='flex', bnTitle='POWER METER LOST', bnSub='Trying to reconnect',
                          powerText='--', powerFg='#9AA4B0', zoneText='--', zoneBg='#1B2027', zoneFg='#9AA4B0', seg4='#1B2027', cadText='--'),
          'gpslost': _a(bnBg='#F2C230', bnFg='#0B0D10', bnAccent='#F2C230', icoWarn='flex', bnTitle='GPS SIGNAL LOST', bnSub='No speed or distance'),
          'lowbatt': _a(bnBg='#F08A24', bnFg='#0B0D10', bnAccent='#F08A24', icoBatt='flex', bnTitle='LOW BATTERY 15%', bnSub='Recording continues', battPct='15%'),
          'holdpause': _a(bnDisplay='none', holdDisplay='flex', holdSecs='0.6 / 1 s', holdPct='60%'),
      },
      default='nextstep', root_extra='position:relative;')

# 20 Empty states -----------------------------------------------------
board('Empty', 'Empty states',
      STATUS
      + '<div style="flex:1;min-height:0;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:0 22px;text-align:center">'
        '<div style="width:68px;height:68px;border-radius:34px;border:2px dashed #5B6673;display:flex;align-items:center;justify-content:center;color:#9AA4B0">'
        f'<span style="display:[[icoBolt]]">{icon("bolt", 30, 2.2)}</span><span style="display:[[icoRoute]]">{icon("route", 30, 2.2)}</span>'
        f'<span style="display:[[icoPin]]">{icon("pin", 30, 2.2)}</span></div>'
        '<div style="margin-top:14px;font-size:20px;font-weight:700;line-height:24px">[[title]]</div>'
        '<div style="margin-top:6px;font-size:13px;font-weight:500;line-height:17px;color:#9AA4B0">[[hint]]</div></div>'
      + dots_holes(),
      scen={
          'noworkout': dict(icoBolt='block', icoRoute='none', icoPin='none', title='No workout loaded',
                            hint='Load one from Menu, then Workouts, before you start. You can still ride without one.', **dot_vals(2)),
          'noroute': dict(icoBolt='none', icoRoute='block', icoPin='none', title='No route loaded',
                          hint='Load a route from Menu, then Routes, for turn prompts. Your track is still recorded.', **dot_vals(4)),
          'gpswait': dict(icoBolt='none', icoRoute='none', icoPin='block', title='Waiting for GPS',
                          hint='Move to open sky. Navigation starts once your position is fixed.', **dot_vals(4)),
      },
      default='noworkout')

# 21 Heart rate -------------------------------------------------------
_bars = lambda: ''.join(
    f'<rect x="{x}" y="{y}" width="3" height="{h}" rx="1" style="fill:[[b{i}]]"></rect>'
    for i, (x, y, h) in enumerate([(0, 11, 5), (5.5, 7.5, 8.5), (11, 4, 12), (16.5, 0, 16)], 1))
board('HeartRate', 'Heart rate sensor',
      STATUS + header44('Heart rate', 'Back to sensors', kebab=True)
      + '<div style="flex:none;height:44px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="display:flex;flex-direction:column"><span style="font-size:16px;font-weight:700;line-height:20px">Sensor ···41B9</span>'
        '<span style="font-size:12px;font-weight:500;line-height:14px;color:#9AA4B0">Bluetooth</span></span>'
        '<span style="box-sizing:border-box;height:22px;padding:0 8px;line-height:20px;border-radius:11px;font-size:11px;font-weight:700;letter-spacing:.06em;white-space:nowrap;flex:none;'
        'background:transparent;color:[[chipFg]];border:1px [[chipBorder]]">[[chipText]]</span></div>'
      + '<div style="flex:none;height:78px;box-sizing:border-box;padding:6px 8px 0;border-bottom:1px solid #232830">' + label('LIVE HEART RATE')
      + f'<div style="display:flex;align-items:baseline;gap:6px">{num("[[hrText]]", 56, 700, 58)}<span style="font-size:18px;font-weight:600;color:#9AA4B0">bpm</span></div></div>'
      + '<div style="flex:none;height:44px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="font-size:11px;font-weight:600;letter-spacing:.08em;color:#9AA4B0">SIGNAL</span>'
        f'<svg width="20" height="16" viewBox="0 0 20 16" role="img" aria-label="[[sigAria]]">{_bars()}</svg></div>'
      + '<div style="flex:1;min-height:0;box-sizing:border-box;padding:8px;font-size:12px;font-weight:500;line-height:16px;color:#9AA4B0">[[help]]</div>'
      + footer('BACK', '', right_hole_display='none'),
      scen={'connected': dict(chipText='CONNECTED', chipFg='#F2F5F8', chipBorder='solid #3A424C', hrText='138', b1='#F2F5F8', b2='#F2F5F8', b3='#F2F5F8', b4='#3A424C',
                              sigAria='Signal strong', seen='Just now', help='Wet the strap contacts if the reading looks wrong. The strap wakes when worn.'),
            'searching': dict(chipText='SEARCHING', chipFg='#F2C230', chipBorder='dashed #F2C230', hrText='--', b1='#3A424C', b2='#3A424C', b3='#3A424C', b4='#3A424C',
                              sigAria='No signal', seen='2 min ago', help='Put the strap on to wake it. The unit keeps looking and reconnects by itself.')},
      default='connected')

# 22 Phone ------------------------------------------------------------
board('Phone', 'Phone pairing',
      STATUS + header44('Phone', 'Back to menu', kebab=True, kebab_display='[[kebabDisplay]]')
      + '<div style="flex:none;height:44px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="display:flex;flex-direction:column"><span style="font-size:16px;font-weight:700;line-height:20px">[[phoneName]]</span>'
        '<span style="font-size:12px;font-weight:500;line-height:14px;color:#9AA4B0">[[phoneSub]]</span></span>'
        '<span style="box-sizing:border-box;height:22px;padding:0 8px;line-height:20px;border-radius:11px;font-size:11px;font-weight:700;letter-spacing:.06em;white-space:nowrap;flex:none;'
        'background:[[chipBg]];color:[[chipFg]];border:1px [[chipBorder]]">[[chipText]]</span></div>'
      + '<div style="flex:none;height:132px;box-sizing:border-box;border-bottom:1px solid #232830">'
        '<div style="display:[[infoDisplay]];flex-direction:column">'
        '<div style="height:44px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="font-size:11px;font-weight:600;letter-spacing:.08em;color:#9AA4B0">LAST SYNC</span><span style="font-size:14px;font-weight:600">14:20 today</span></div>'
        '<div style="height:44px;box-sizing:border-box;padding:0 8px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #232830">'
        '<span style="font-size:11px;font-weight:600;letter-spacing:.08em;color:#9AA4B0">RIDES WAITING</span><span style="font-size:14px;font-weight:600">1</span></div></div>'
        '<div style="display:[[codeDisplay]];flex-direction:column;align-items:center;padding-top:10px">'
        + label('PAIRING CODE', 'text-align:center')
        + f'<div style="margin-top:4px">{num("[[code]]", 60, 700, 62)}</div></div>'
        '<div style="display:[[btnDisplay]];padding:44px 8px 0">' + btn('PAIR PHONE', 'primary', 44) + '</div></div>'
      + '<div style="flex:1;min-height:0;box-sizing:border-box;padding:8px;font-size:12px;font-weight:500;line-height:16px;color:#9AA4B0">[[hint]]</div>'
      + footer('[[footLeft]]', '[[footRight]]', right_hole_display='[[footRightDisplay]]'),
      scen={
          'connected': dict(kebabDisplay='flex', phoneName='Pixel 8', phoneSub='Paired · Bluetooth', chipText='CONNECTED', chipBg='#1B2027', chipFg='#F2F5F8',
                            chipBorder='solid #3A424C', infoDisplay='flex', codeDisplay='none', btnDisplay='none',
                            hint='Open the CC700 app on your phone to send workouts and routes.', footLeft='BACK', footRight='', footRightDisplay='none'),
          'pairing': dict(kebabDisplay='none', phoneName='Pairing…', phoneSub='Waiting for the phone', chipText='PAIRING', chipBg='transparent', chipFg='#F2C230',
                          chipBorder='dashed #F2C230', infoDisplay='none', codeDisplay='flex', btnDisplay='none', code='482 916',
                          hint='Enter this code in the CC700 app on your phone. Pairing stops after 60 seconds.', footLeft='CANCEL', footRight='', footRightDisplay='none'),
          'notpaired': dict(kebabDisplay='none', phoneName='No phone paired', phoneSub='For workouts and routes', chipText='NOT PAIRED', chipBg='#1B2027',
                            chipFg='#F2F5F8', chipBorder='solid #3A424C', infoDisplay='none', codeDisplay='none', btnDisplay='block', code='',
                            hint='Open the CC700 app, choose Add unit, then enter the code shown here.', footLeft='BACK', footRight='PAIR', footRightDisplay='flex'),
      },
      default='connected')
for _k in BOARDS['Phone']['scen'].values():
    _k.setdefault('code', '')

# 23 Settings ---------------------------------------------------------
_cyc = icon('cycle', 14, 2.4, '#9AA4B0')
_v = lambda t: f'<span style="font-size:14px;font-weight:600">{t}</span>'
board('Settings', 'Settings list',
      STATUS + header36('Settings')
      + list_box(
          row46('Brightness', '', _v('Auto') + _cyc, True)
          + row46('Screen off', 'While stopped', _v('1 min') + _cyc)
          + row46('Units', 'Distance and speed', _v('km') + _cyc)
          + row46('Auto-pause', 'Stop recording when still', _v('On') + _cyc)
          + row46('Time zone', 'Set by the phone', _v('UTC+8:00'))
          + row46('Firmware', '', _v('v0.1.0')), thumb=190)
      + footer('BACK', 'CHANGE'))


# ---------------------------------------------------------------- output
def H(s):
    return re.sub(r'\[\[(\w+)\]\]', r'{{ \1 }}', s)


def holes_in(s):
    return set(re.findall(r'\[\[(\w+)\]\]', s))


def check(b):
    need = holes_in(b['body'])
    for k, vals in b['scen'].items():
        miss = need - set(vals)
        assert not miss, f"{b['name']}/{k} missing {miss}"


FONT_LINK = ('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&amp;'
             'family=Barlow:wght@500;600;700&amp;display=swap">')


def render_dc(b):
    root = ("width:240px;height:320px;box-sizing:border-box;overflow:hidden;background:#0B0D10;color:#F2F5F8;"
            "font-family:'Barlow','Arial',sans-serif;display:flex;flex-direction:column;" + b['root_extra'])
    multi = len(b['scen']) > 1
    if multi:
        props = {"scenario": {"editor": "enum", "default": b['default'], "options": list(b['scen'])}, "$preview": {"width": 240, "height": 320}}
        table = json.dumps({k: {n: v for n, v in vals.items()} for k, vals in b['scen'].items()}, ensure_ascii=False, indent=1)
        script = (f"class Component extends DCLogic {{\nrenderVals() {{\nconst key = this.props.scenario || '{b['default']}';\n"
                  f"const table = {table};\nreturn table[key] || table['{b['default']}'];\n}}\n}}")
    else:
        props = {"$preview": {"width": 240, "height": 320}}
        script = "class Component extends DCLogic {\nrenderVals() {\nreturn {};\n}\n}"
    return (f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<title>{b["title"]}</title>\n'
            f'<script src="./support.js"></script>\n</head>\n<body>\n<x-dc>\n<helmet>\n{FONT_LINK}\n<style>\nbody{{margin:0}}\n</style>\n</helmet>\n'
            f'<div style="{root}">\n\n{H(b["body"])}\n\n</div>\n</x-dc>\n'
            f"<script type=\"text/x-dc\" data-dc-script data-props='{json.dumps(props, ensure_ascii=False)}'>\n{script}\n</script>\n</body>\n</html>\n")


def font_faces():
    css = []
    for fam, folder in (('Barlow', 'barlow'), ('Barlow Condensed', 'barlow-condensed')):
        for w in (500, 600, 700):
            css.append(f"@font-face{{font-family:'{fam}';font-weight:{w};src:url('file://{FONTS}/{folder}/files/{folder}-latin-{w}-normal.woff2') format('woff2')}}")
    return '\n'.join(css)


def render_preview(b, key):
    vals = b['scen'][key]
    body = re.sub(r'\[\[(\w+)\]\]', lambda m: str(vals[m.group(1)]), b['body'])
    root = ("width:240px;height:320px;box-sizing:border-box;overflow:hidden;background:#0B0D10;color:#F2F5F8;"
            "font-family:'Barlow','Arial',sans-serif;display:flex;flex-direction:column;" + b['root_extra'])
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{font_faces()}\nbody{{margin:0;background:#000}}</style></head>'
            f'<body><div id="b" style="{root}">{body}</div></body></html>')


def main():
    names = sys.argv[1:] or list(BOARDS)
    for n in names:
        b = BOARDS[n]
        check(b)
        with open(f'{PROJ}/{n}.dc.html', 'w', encoding='utf-8') as f:
            f.write(render_dc(b))
        for k in b['scen']:
            with open(f'{OUT}/{n}__{k}.html', 'w', encoding='utf-8') as f:
                f.write(render_preview(b, k))
    print('generated', ', '.join(names))


if __name__ == '__main__':
    main()
