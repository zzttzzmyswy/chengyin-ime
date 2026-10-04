#!/usr/bin/env python3
"""Generate our flat C/wave icon; only Python's standard library is required."""
from pathlib import Path
import math
import struct
ROOT = Path(__file__).resolve().parents[1]

def frame(size):
    pixels = bytearray()
    for y in reversed(range(size)):
        for x in range(size):
            u, v = (x + .5) / size, (y + .5) / size
            radius = math.hypot(u - .50, v - .42)
            c = .19 < radius < .27 and (u < .62 or v < .24 or v > .59)
            wave = .15 < u < .85 and abs(v - (.76 + .035 * math.sin(u * 4 * math.pi))) < .035
            r,g,b = (255,255,255) if c or wave else (0,70,127)
            pixels += bytes((b,g,r,255))
    mask = bytes(((size + 31)//32)*4*size)
    return struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels),0,0,0,0)+pixels+mask

frames = [(s,frame(s)) for s in (16,24,32,48,64)]
offset = 6+16*len(frames)
header = bytearray(struct.pack('<HHH',0,1,len(frames)))
for size, data in frames:
    header += struct.pack('<BBBBHHII',size,size,0,0,1,32,len(data),offset)
    offset += len(data)
(ROOT/'platforms/windows/chengyin.ico').write_bytes(header+b''.join(d for _,d in frames))
