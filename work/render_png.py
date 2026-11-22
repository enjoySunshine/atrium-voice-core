import re, zlib, struct, sys

data = open('/workspace/work/report_data.js').read()

def get_arr(name, lr, which):
    m = re.search(name + r':\{L:\{mn:\[([^\]]*)\],mx:\[([^\]]*)\]\},R:\{mn:\[([^\]]*)\],mx:\[([^\]]*)\]\}', data)
    if not m:
        raise Exception('not found: ' + name)
    groups = m.groups()
    idx = {'L': (0, 1), 'R': (2, 3)}[lr][0 if which == 'mn' else 1]
    return [float(x) for x in groups[idx].split(',')]

SWITCH = 22050
N = 66048
CW, CH = 520, 96
SPACES = [("cave", "dong"), ("hall", "mu"), ("yard", "yuan"), ("well", "jing")]
KINDS = ["dry", "rev", "cho"]

# image buffer
ROWS = 4
COLS = 3
PAD = 8
TITLEH = 16
imgw = COLS * (CW + PAD) + PAD
imgh = ROWS * (CH + TITLEH + PAD) + PAD + 30
px = [[(16, 19, 26) for _ in range(imgw)] for _ in range(imgh)]

def put(x, y, c):
    if 0 <= x < imgw and 0 <= y < imgh:
        px[y][x] = c

def draw_wave(ox, oy, name, refname=None):
    # background
    for y in range(CH):
        for x in range(CW):
            put(ox + x, oy + y, (10, 13, 18))
    NBUF = 1032
    def trace(ch, lr, color):
        mn = get_arr(ch, lr, 'mn'); mx = get_arr(ch, lr, 'mx')
        mid = CH / 2; amp = CH / 2 - 4
        for x in range(CW):
            b = min(NBUF - 1, x * NBUF // CW)
            y0 = int(mid - mx[b] * amp); y1 = int(mid - mn[b] * amp)
            for y in range(min(y0, y1), max(y0, y1) + 1):
                put(ox + x, oy + y, color)
    if refname:
        trace(refname, 'L', (120, 80, 160))
    trace(name, 'L', (90, 200, 250))
    trace(name, 'R', (220, 150, 90))
    sx = int(SWITCH / N * CW)
    for y in range(CH):
        put(ox + sx, oy + y, (255, 90, 90))

for r, (skey, _) in enumerate(SPACES):
    for c, kkey in enumerate(KINDS):
        ox = PAD + c * (CW + PAD)
        oy = PAD + 30 + r * (CH + TITLEH + PAD)
        name = skey + "_" + kkey
        ref = "ref_hall_dry" if (skey == "hall" and kkey == "dry") else None
        draw_wave(ox, oy, name, ref)

# encode PNG
raw = b''
for row in px:
    raw += b'\x00' + b''.join(struct.pack('BBB', *p) for p in row)
def chunk(typ, dat):
    c = struct.pack('>I', len(dat)) + typ + dat
    return c + struct.pack('>I', zlib.crc32(typ + dat) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', imgw, imgh, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 9))
png += chunk(b'IEND', b'')
open('/workspace/work/waveforms.png', 'wb').write(png)
print('wrote waveforms.png', imgw, 'x', imgh)
