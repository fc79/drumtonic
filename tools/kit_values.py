#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The factory kits (firmware/src/kits.c) from physical targets: Hz, ms, semitones -> knob values 0..127,
through the same curves as firmware/src/drum.c. Prints the C initialiser of DRUM_KITS.

    python3 tools/kit_values.py
"""
import math
L=math.log2
def c(v): return max(0,min(127,round(v)))
def freq(hz): return c(L(hz/20)*127/8)
def ms(m): return c(L(max(m,1))*127/12.3)
def atk(m): return 0 if m<=0 else c(L(4*m)*127/12.3)
def nfreq(hz): return c(L(hz/20)*127/10)
def Q(qq): return c(L(qq/0.707)*127/5)
def eqf(hz): return c(L(hz/50)*127/7.6)
def lfo(hz): return c(L(hz/0.1)*127/14.3)
def semis(s): return c(64+s*63/48)
def mode(m): return [0,64,127][m]
def eqg(db):  # gain G: +db up to +12 (G=3); cut
    if db>=0: G=10**(db/20)-1; return c(64+G*63/3)
    G=10**(db/20)-1; return c(64+G*64)
# DIST's makeup (firmware/src/drum.c DIST_UP, dB): the engine takes it off the level, so a sound with DIST is as loud
# as without; the factory levels make it up again, all of them less 8.8 dB (INDUSTR's BD, the loudest, at 127) so each
# kit and the library keep their balance (until 2026-11-12, no makeup: DIST made a sound up to 19 dB louder)
DIST_AT=[0,1,8,16,32,48,64,80,96,112,127]
DIST_UP=[0,6531,50072,76197,100145,113207,120827,126269,130624,133889,136066]
LEVEL_DB=-8.8
def dist_db(d):
    if d<=0: return 0.0
    for i in range(1,11):
        if d<=DIST_AT[i]: return (DIST_UP[i-1]+(DIST_UP[i]-DIST_UP[i-1])*(d-DIST_AT[i-1])/(DIST_AT[i]-DIST_AT[i-1]))/65536*6.0206
def level(lvl,d): return c(lvl*10**((dist_db(d)+LEVEL_DB)/40))   # (a square law: 40 log10)
# fields: wave,freq,pmode,pamt,prate,oatk,odec,ovel, nfilt,nfreq,nq,nenv,natk,ndec,stereo,nvel, mix,dist,eqf,eqg,level,pan,choke,phase
def P(wave=0,f=50,pm=0,amt=0,rate=30,oa=0,od=200,ov=100,nf=0,nhz=5000,q=0.707,ne=0,na=0,nd=50,st=0,nv=100,mix=0,dist=0,ef=1000,eg=0,lvl=110,pan=64,choke=0,ph=0,ratehz=None):
    pr = lfo(ratehz) if ratehz is not None else ms(rate)
    return [mode(wave),freq(f),mode(pm),semis(amt),pr,atk(oa),ms(od),ov, mode(nf),nfreq(nhz),Q(q),mode(ne),atk(na),ms(nd),127 if st else 0,nv, mix,dist,eqf(ef),eqg(eg),level(lvl,dist),pan,choke*32,ph]
KITS={
 'CLASSIC':[('BD',P(f=48,amt=26,rate=45,od=480,nf=0,nhz=3000,nd=6,mix=12,dist=18,ef=90,eg=4,lvl=124,ph=40)),
            ('SD',P(wave=1,f=185,amt=7,rate=25,od=140,nf=2,nhz=1800,q=1.0,nd=190,mix=78,dist=10,ef=4000,eg=3,lvl=112,ph=20)),
            ('CP',P(nf=1,nhz=1150,q=1.6,ne=2,nd=260,st=1,mix=127,dist=24,ef=2500,eg=2,lvl=127)),
            ('CH',P(nf=2,nhz=8500,q=0.9,nd=45,st=1,mix=127,ef=10000,eg=3,lvl=96,pan=58,choke=1)),
            ('OH',P(nf=2,nhz=8000,q=0.9,nd=420,st=1,mix=127,ef=10000,eg=3,lvl=92,pan=70,choke=1)),
            ('LT',P(f=92,amt=5,rate=90,od=340,nf=0,nhz=2000,nd=20,mix=14,lvl=112,pan=46)),
            ('RS',P(wave=1,f=430,amt=2,rate=10,od=35,nf=1,nhz=2600,q=6,nd=16,mix=56,dist=40,lvl=96,pan=82)),
            ('CY',P(wave=2,f=560,pm=1,amt=3,ratehz=330,od=900,nf=1,nhz=6500,q=1.5,nd=1300,st=1,mix=104,ef=9000,eg=2,lvl=82,pan=72))],
 'TIGHT':[('BD',P(f=56,amt=34,rate=22,od=230,nf=1,nhz=2500,q=2,nd=4,mix=20,dist=40,ef=120,eg=3,lvl=112,ph=90)),
          ('SD',P(f=230,amt=12,rate=12,od=80,nf=1,nhz=3200,q=1.4,nd=110,mix=86,dist=30,lvl=112,ph=60)),
          ('CP',P(nf=1,nhz=1500,q=2.5,ne=2,nd=140,st=1,mix=127,dist=20,lvl=108)),
          ('CH',P(nf=2,nhz=9500,q=1.2,nd=28,mix=127,lvl=94,pan=56,choke=1)),
          ('OH',P(nf=2,nhz=9000,q=1.2,ne=1,nd=260,st=1,mix=127,lvl=90,pan=72,choke=1)),
          ('TM',P(f=150,amt=9,rate=40,od=180,nf=1,nhz=1200,q=3,nd=30,mix=18,lvl=110,pan=40)),
          ('CB',P(wave=1,f=545,od=160,nf=1,nhz=820,q=8,nd=120,mix=40,dist=50,ef=900,eg=6,lvl=88,pan=88)),
          ('SH',P(nf=1,nhz=5200,q=2,ne=1,na=12,nd=70,st=1,mix=127,lvl=84,pan=96))],
 'INDUSTR':[('BD',P(wave=2,f=42,amt=36,rate=60,od=600,nf=0,nhz=600,nd=200,mix=30,dist=110,ef=80,eg=3,lvl=104,ph=127)),
            ('SN',P(wave=2,f=160,pm=2,amt=10,ratehz=900,od=120,nf=1,nhz=2400,q=4,nd=220,mix=90,dist=90,lvl=106)),
            ('CP',P(nf=1,nhz=900,q=5,ne=2,nd=420,st=1,mix=127,dist=60,lvl=102)),
            ('HH',P(wave=2,f=3200,pm=1,amt=12,ratehz=1500,od=30,nf=2,nhz=7000,q=3,nd=36,st=1,mix=100,dist=70,lvl=88,choke=1)),
            ('OH',P(wave=2,f=3200,pm=1,amt=12,ratehz=1500,od=300,nf=2,nhz=6800,q=3,nd=380,st=1,mix=100,dist=70,lvl=84,choke=1)),
            ('ZP',P(f=900,amt=-40,rate=70,od=250,mix=0,dist=50,lvl=100,pan=36)),
            ('MT',P(wave=1,f=310,pm=2,amt=24,ratehz=40,od=90,nf=1,nhz=1800,q=12,nd=60,mix=50,dist=80,lvl=94,pan=90)),
            ('NS',P(nf=0,nhz=900,q=8,ne=1,na=200,nd=900,st=1,mix=127,dist=40,lvl=80))]}
if __name__ == '__main__':                     # (tools/library_values.py imports P and KITS)
    out=[]
    for name,chs in KITS.items():
        out.append('    {"%s", {%s},\n     {' % (name, ', '.join('"%s"'%n for n,_ in chs)))
        out.append(',\n      '.join('{{' + ', '.join(str(x) for x in p) + '}}' for _,p in chs))
        out.append('}},\n')
    print(''.join(out))
