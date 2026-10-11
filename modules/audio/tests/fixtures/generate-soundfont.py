#!/usr/bin/env python3
"""Generate our original, deterministic SF2 fixture; no external bank/sample license."""
import math
import struct
from pathlib import Path

def chunk(tag, data):
    return tag.encode('ascii') + struct.pack('<I', len(data)) + data + b'\0' * (len(data) & 1)
def name(value):
    return value.encode('ascii').ljust(20, b'\0')
def gen(op, value):
    return struct.pack('<HH', op, value & 0xffff)
def listing(tag, *chunks):
    return chunk('LIST', tag.encode('ascii') + b''.join(chunks))
# 220 Hz at key 57 (A3), with 46 zero interpolation guard samples.
count = 400
samples = [int(16000 * math.sin(2 * math.pi * i / count)) for i in range(count)] + [0] * 46
info = listing('INFO', chunk('ifil', struct.pack('<HH', 2, 1)), chunk('INAM', b'NativeKit test bank\0'))
sdta = listing('sdta', chunk('smpl', struct.pack('<%dh' % len(samples), *samples)))
phdr = name('Test sine') + struct.pack('<HHHIII', 0, 0, 0, 0, 0, 0)
phdr += name('EOP') + struct.pack('<HHHIII', 0, 0, 1, 0, 0, 0)
pgen = gen(41, 0) + gen(0, 0)
inst = name('Sine') + struct.pack('<H', 0) + name('EOI') + struct.pack('<H', 1)
igen = gen(34, -12000) + gen(38, -3600) + gen(54, 1) + gen(53, 0) + gen(0, 0)
shdr = name('Sine sample') + struct.pack('<IIIIIBbHH', 0, count, 0, count, 88000, 57, 0, 0, 1)
shdr += name('EOS') + struct.pack('<IIIIIBbHH', count, count, count, count, 88000, 0, 0, 0, 1)
pdta = listing('pdta', chunk('phdr', phdr), chunk('pbag', struct.pack('<HHHH', 0, 0, 1, 0)),
    chunk('pmod', b'\0' * 10), chunk('pgen', pgen), chunk('inst', inst),
    chunk('ibag', struct.pack('<HHHH', 0, 0, 4, 0)), chunk('imod', b'\0' * 10),
    chunk('igen', igen), chunk('shdr', shdr))
Path(__file__).with_name('minimal.sf2').write_bytes(chunk('RIFF', b'sfbk' + info + sdta + pdta))
