import struct,sys
d=open("/opt/re/work/stock-ota0.elf","rb").read()
segs=[(0x3c2d0020,0x42f8b0,0xd4),(0x3fca1000,0x740,0x42f984),(0x42000020,0x2c1270,0x4300c4),(0x3fca1740,0x921c,0x6f1334),(0x40374000,0x1cfe4,0x6fa550)]
def rd(a,n):
    for v,s,o in segs:
        if v<=a<v+s: return d[o+a-v:o+a-v+n]
for x in sys.argv[1:]:
    a=int(x,16); b=rd(a,8)
    u=struct.unpack("<I",b[:4])[0]
    print(x, "u32=%#x"%u, "f32=%g"%struct.unpack("<f",b[:4])[0], "f64hi?", b.hex(" "))
