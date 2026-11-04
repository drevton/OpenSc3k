"""Draw the OpenSc3k app icon: an isometric city block on a rounded square. Output: icon_1024.png (run from this folder)"""
from PIL import Image, ImageDraw, ImageFilter
import math

S = 4096          # supersampled canvas
R = S // 1024
im = Image.new('RGBA', (S, S), (0, 0, 0, 0))

# macOS icon grid: 824px rounded square centred in 1024
margin = 100 * R
size = S - 2 * margin
radius = int(size * 0.2237)
bg = Image.new('RGBA', (S, S), (0, 0, 0, 0))
grad = Image.new('RGBA', (S, S))
gp = grad.load()
top, bottom = (66, 134, 244), (20, 40, 112)
for y in range(S):
    t = y / S
    c = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)) + (255,)
    for x in range(0, S):
        gp[x, y] = c
mask = Image.new('L', (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((margin, margin, S - margin, S - margin), radius=radius, fill=255)
im.paste(grad, (0, 0), mask)

# soft sky glow
glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse((S * 0.15, S * 0.05, S * 0.85, S * 0.6), fill=(255, 255, 255, 60))
glow = glow.filter(ImageFilter.GaussianBlur(S * 0.06))
im = Image.alpha_composite(im, Image.composite(glow, Image.new('RGBA', (S, S), (0, 0, 0, 0)), mask))

d = ImageDraw.Draw(im)
cx, cy = S // 2, int(S * 0.585)
u = S * 0.0505      # unit: half-width of one isometric cell

def iso(x, y, z=0.0):
    """Project grid (x,y) and height z to canvas pixels."""
    return (cx + (x - y) * u, cy + (x + y) * u * 0.5 - z * u)

def prism(x0, y0, w, h, z, colors):
    top, left, right = colors
    a, b, c, e = iso(x0, y0, z), iso(x0 + w, y0, z), iso(x0 + w, y0 + h, z), iso(x0, y0 + h, z)
    a0, b0, c0, e0 = iso(x0, y0), iso(x0 + w, y0), iso(x0 + w, y0 + h), iso(x0, y0 + h)
    d.polygon([e, c, c0, e0], fill=left)       # left face (towards viewer-left)
    d.polygon([c, b, b0, c0], fill=right)      # right face
    d.polygon([a, b, c, e], fill=top)

def shade(base, k):
    return tuple(max(0, min(255, int(v * k))) for v in base)

ground = (210, 226, 240)
# ground slab
prism(-3.6, -3.6, 7.2, 7.2, -0.0 + 0.5, (shade(ground, 1.0), shade(ground, .78), shade(ground, .62)))
# roads
road = (96, 106, 128)
a, b, c, e = iso(-0.45, -3.6, 0.5), iso(0.45, -3.6, 0.5), iso(0.45, 3.6, 0.5), iso(-0.45, 3.6, 0.5)
d.polygon([a, b, c, e], fill=road)
a, b, c, e = iso(-3.6, -0.45, 0.5), iso(3.6, -0.45, 0.5), iso(3.6, 0.45, 0.5), iso(-3.6, 0.45, 0.5)
d.polygon([a, b, c, e], fill=road)

def tri(base):
    return (shade(base, 1.08), shade(base, .86), shade(base, .66))

# buildings: back to front (painter's order by x+y)
blocks = [
    (-3.0, -3.0, 1.9, 1.9, 5.2, (255, 198, 84)),
    (1.1, -3.0, 1.9, 1.9, 3.0, (236, 120, 96)),
    (-3.0, 1.1, 1.9, 1.9, 2.4, (120, 200, 150)),
    (1.1, 1.1, 1.9, 1.9, 6.6, (236, 240, 250)),
    (-1.2, -1.2, 0.0, 0.0, 0, None),
]
order = sorted([b for b in blocks if b[5]], key=lambda b: b[0] + b[1])
for x, y, w, h, z, base in order:
    prism(x, y, w, h, 0.5 + z, tri(base))
    # windows on the right face
    for k in range(int(z * 1.4)):
        for j in range(2):
            zz = 0.5 + 0.35 + k * 0.7
            if zz + 0.4 > 0.5 + z: break
            p0 = iso(x + w, y + 0.3 + j * 0.8, zz + 0.45)
            p1 = iso(x + w, y + 0.3 + j * 0.8 + 0.45, zz + 0.45)
            p2 = iso(x + w, y + 0.3 + j * 0.8 + 0.45, zz)
            p3 = iso(x + w, y + 0.3 + j * 0.8, zz)
            d.polygon([p0, p1, p2, p3], fill=(255, 244, 200))

im = im.resize((1024, 1024), Image.LANCZOS)
im.save('icon_1024.png')
print('ok')
