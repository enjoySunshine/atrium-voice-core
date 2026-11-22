import re, zlib, struct

data = open('/workspace/work/report_data.js').read()

def get_arr(name, lr, which):
    m = re.search(name + r':\{L:\{mn:\[([^\]]*)\],mx:\[([^\]]*)\]\},R:\{mn:\[([^\]]*)\],mx:\[([^\]]*)\]\}', data)
    groups = m.groups()
    idx = {'L': (0, 1), 'R': (2, 3)}[lr][0 if which == 'mn' else 1]
    return [float(x) for x in groups[idx].split(',')]

SWITCH = 22050
N = 66048
CW, CH = 1400, 160
NBUF = 1032

# 3 strips: cave_dry, hall_dry(+ref), hall_rev
strips = [("cave_dry", None), ("hall_dry", "ref_hall_dry"), ("hall_rev", None)]
PAD = 10
imgw = CW + 2 * PAD
imgh = len(strips) * (CH + PAD) + PAD
px = [[(16, 19, 26) for _ in range(imgw)] for _ in range(imgh)]

def put(x, y, c):
    if 0 <= x < imgw and 0 <= y < imgh:
        px[y][x] = c

def draw(oy, name, refname):
    for y in range(CH):
        for x in range(CW):
            put(PAD + x, oy + y, (10, 13, 18))
    def trace(ch, lr, color):
        mn = get_arr(ch, lr, 'mn'); mx = get_arr(ch, lr, 'mx')
        mid = CH / 2; amp = CH / 2 - 6
        for x in range(CW):
            b = min(NBUF - 1, x * NBUF // CW)
            y0 = int(mid - mx[b] * amp); y1 = int(mid - mn[b] * amp)
            for y in range(min(y0, y1), max(y0, y1) + 1):
                put(PAD + x, oy + y, color)
    if refname:
        trace(refname, 'L', (150, 100, 220))
    trace(name, 'L', (90, 200, 250))
    trace(name, 'R', (230, 160, 90))
    sx = int(SWITCH / N * CW)
    for y in range(CH):
        put(PAD + sx, oy + y, (255, 70, 70))

for i, (nm, rf) in enumerate(strips):
    draw(PAD + i * (CH + PAD), nm, rf)

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
open('/workspace/work/key_channels.png', 'wb').write(png)
print('wrote key_channels.png')
