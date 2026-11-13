#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1's sound library (firmware/gen/sound_lib.h): the factory kits' sounds (with CLASSIC's MT and HT, made from
its LT as app_fm1.c kit_from8 does) and new ones, from physical targets through tools/kit_values.py's curves, grouped
by kind. A sound: its channel name (3 letters at most), a label (11 at most) and its 24 knob values.

    python3 tools/library_values.py > firmware/gen/sound_lib.h
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit_values import P, KITS

FREQ, ODEC, NDEC, PAN = 1, 6, 13, 21
def toms(lt):                                   # app_fm1.c kit_from8: higher, shorter, to the right
    out = []
    for t in (1, 2):
        p = list(lt)
        p[FREQ] = min(127, p[FREQ] + 6 * t); p[ODEC] = max(0, p[ODEC] - 4 * t)
        p[NDEC] = max(0, p[NDEC] - 4 * t); p[PAN] = min(127, p[PAN] + 18 * t)
        out.append(p)
    return out

KIND = {'BD': 0, 'SD': 1, 'SN': 1, 'CP': 2, 'SP': 2, 'CH': 3, 'HH': 3, 'PH': 3, 'OH': 3, 'LT': 4, 'MT': 4, 'HT': 4,
        'TM': 4, 'LC': 5, 'CG': 5, 'BG': 5, 'CB': 5, 'CL': 5, 'WB': 5, 'RS': 5, 'SH': 5, 'MR': 5, 'TB': 5, 'TR': 5,
        'CY': 6, 'RD': 6, 'CR': 6, 'ZP': 7, 'MT_': 7, 'NS': 7, 'LZ': 7, 'BL': 7, 'NZ': 7, 'RV': 7, 'TK': 7}

lib = []                                        # (kind, name, label, values)
for kit, chs in KITS.items():
    for n, p in chs:
        k = 7 if (kit, n) == ('INDUSTR', 'MT') else KIND[n]   # (INDUSTR's MT is a metal hit)
        lib.append((k, 'ME' if k == 7 and n == 'MT' else n, kit, p))
    if kit == 'CLASSIC':
        mt, ht = toms(dict(chs)['LT'])
        lib += [(4, 'MT', kit, mt), (4, 'HT', kit, ht)]

NEW = [
    ('BD', '808 LONG', P(f=45, amt=20, rate=60, od=1500, mix=4, lvl=124, ph=30)),
    ('BD', 'PUNCH', P(f=55, amt=40, rate=18, od=200, nf=1, nhz=3000, q=2, nd=5, mix=20, dist=60, ef=100, eg=6, lvl=118, ph=100)),
    ('BD', 'SUB', P(f=40, amt=12, rate=80, od=900, ef=60, eg=6, lvl=124)),
    ('BD', 'DIST', P(wave=1, f=50, amt=30, rate=40, od=400, mix=10, dist=100, ef=90, eg=4, lvl=104, ph=80)),
    ('SD', '808', P(f=180, amt=5, rate=20, od=110, nf=2, nhz=2500, q=0.9, nd=170, mix=70, lvl=110, ph=20)),
    ('SD', 'BRUSH', P(nf=1, nhz=3500, q=1.0, ne=1, na=15, nd=220, st=1, mix=127, lvl=96)),
    ('SD', 'RIM SNARE', P(wave=1, f=330, amt=4, rate=10, od=60, nf=1, nhz=4000, q=2, nd=90, mix=60, dist=30, lvl=106, ph=60)),
    ('SD', 'LO-FI', P(wave=1, f=200, amt=10, rate=20, od=90, nhz=3000, nd=160, mix=80, dist=70, ef=1500, eg=6, lvl=100)),
    ('CP', 'WIDE', P(nf=1, nhz=1300, q=1.2, ne=2, nd=500, st=1, mix=127, dist=10, lvl=118)),
    ('SP', 'SNAP', P(nf=1, nhz=2600, q=3, nd=40, mix=127, dist=20, lvl=100)),
    ('CH', '808', P(wave=2, f=4000, pm=1, amt=10, ratehz=1800, od=25, nf=2, nhz=8000, q=1, nd=35, mix=100, lvl=92, choke=1)),
    ('OH', '808', P(wave=2, f=4000, pm=1, amt=10, ratehz=1800, od=280, nf=2, nhz=8000, q=1, nd=320, mix=100, lvl=88, choke=1)),
    ('PH', 'PEDAL', P(nf=2, nhz=7000, q=1.5, na=5, nd=60, mix=127, lvl=86, choke=1)),
    ('LT', '808', P(f=80, amt=8, rate=120, od=500, mix=0, lvl=114, pan=44)),
    ('MT', '808', P(f=120, amt=8, rate=110, od=430, mix=0, lvl=112, pan=64)),
    ('HT', '808', P(f=170, amt=8, rate=100, od=360, mix=0, lvl=110, pan=84)),
    ('LC', 'CONGA LO', P(f=220, amt=6, rate=15, od=240, nf=1, nhz=1500, q=4, nd=10, mix=8, lvl=106, pan=44)),
    ('CG', 'CONGA HI', P(f=330, amt=6, rate=15, od=180, nf=1, nhz=1500, q=4, nd=10, mix=8, lvl=106, pan=80)),
    ('BG', 'BONGO', P(f=440, amt=8, rate=10, od=110, nf=1, nhz=2000, q=4, nd=8, mix=8, lvl=104, pan=90)),
    ('CB', '808', P(wave=2, f=560, od=220, nf=1, nhz=840, q=10, nd=200, mix=45, dist=30, ef=800, eg=6, lvl=86, pan=84)),
    ('CL', 'CLAVE', P(f=2500, od=30, lvl=100, pan=70, ph=127)),
    ('WB', 'WOODBLOCK', P(wave=1, f=900, amt=3, rate=8, od=60, nf=1, nhz=1000, q=6, nd=20, mix=20, lvl=98, pan=52)),
    ('RS', '808 RIM', P(wave=1, f=1700, od=25, nf=1, nhz=3000, q=4, nd=10, mix=30, lvl=96, ph=127)),
    ('MR', 'MARACAS', P(nf=2, nhz=6000, q=1.5, na=8, nd=50, mix=127, lvl=82, pan=40)),
    ('TB', 'TAMBOURINE', P(wave=2, f=5200, pm=1, amt=6, ratehz=1300, od=180, nf=2, nhz=7000, q=2, nd=200, st=1, mix=90, lvl=80, pan=90)),
    ('TR', 'TRIANGLE', P(f=4200, pm=1, amt=1, ratehz=6, od=1500, lvl=78, pan=96)),
    ('RD', 'RIDE', P(wave=2, f=620, pm=1, amt=2, ratehz=410, od=1400, nf=1, nhz=7500, q=2, nd=1600, st=1, mix=70, ef=5000, eg=4, lvl=80, pan=60)),
    ('CR', 'CRASH', P(wave=2, f=480, pm=2, amt=4, ratehz=700, od=1800, nf=2, nhz=5000, q=0.8, nd=2200, st=1, mix=115, lvl=82)),
    ('LZ', 'LASER', P(wave=2, f=3000, amt=-48, rate=90, od=220, dist=40, lvl=90)),
    ('BL', 'BLIP', P(f=1400, amt=12, rate=15, od=60, lvl=96)),
    ('NZ', 'NOISE HIT', P(nhz=12000, nd=150, st=1, mix=127, dist=50, lvl=90)),
    ('RV', 'REVERSE', P(nf=1, nhz=3000, q=1, na=300, nd=10, st=1, mix=127, lvl=86)),
    ('TK', 'TICK', P(f=3000, od=8, nf=2, nhz=9000, nd=5, mix=50, lvl=100, ph=127)),
]
lib += [(KIND[n], n, label, p) for n, label, p in NEW]
lib.sort(key=lambda e: e[0])                    # (stable: the kits first in each kind)

for k, n, label, p in lib:
    assert len(n) <= 3 and len(label) <= 11 and len(p) == 24 and all(0 <= v <= 127 for v in p), (n, label)
print('/* SPDX-License-Identifier: GPL-3.0-only */')
print('/* generated by tools/library_values.py: the FM-1\'s sound library (ui_fm1.c), %d sounds by kind */' % len(lib))
print('#define SND_LIB_N %du' % len(lib))
print('static const struct { char name[4], label[12]; drum_patch_t p; } SND_LIB[SND_LIB_N] = {')
for k, n, label, p in lib:
    print('    {"%s", "%s", {{%s}}},' % (n, label, ', '.join(str(v) for v in p)))
print('};')
