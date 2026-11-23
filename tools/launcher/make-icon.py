# Brume Launcher's icon (256x256 JPEG): a glass cube in the blue fog, drawn here (no outside image).
import sys, math
from PIL import Image, ImageDraw, ImageFilter
W = 256
img = Image.new('RGB', (W, W), (2, 3, 10))
fog = Image.new('L', (W, W), 0)
d = ImageDraw.Draw(fog)
for r in range(150, 0, -2):
    d.ellipse((128 - r, 140 - r * 0.8, 128 + r, 140 + r * 0.8), fill=int(255 * (1 - r / 150) ** 1.6))
blue = Image.new('RGB', (W, W), (40, 80, 200))
img = Image.composite(blue, img, fog.filter(ImageFilter.GaussianBlur(18)))
violet = Image.new('RGB', (W, W), (70, 40, 140))
m2 = Image.new('L', (W, W), 0); ImageDraw.Draw(m2).ellipse((20, 150, 140, 260), fill=70)
img = Image.composite(violet, img, m2.filter(ImageFilter.GaussianBlur(30)))
# the cube (isometric), translucent faces, light on the edges
c = Image.new('RGBA', (W, W), (0, 0, 0, 0))
g = ImageDraw.Draw(c)
cx, cy, s = 128, 122, 58
top = [(cx, cy - s), (cx + s * 0.87, cy - s * 0.5), (cx, cy), (cx - s * 0.87, cy - s * 0.5)]
left = [(cx - s * 0.87, cy - s * 0.5), (cx, cy), (cx, cy + s), (cx - s * 0.87, cy + s * 0.5)]
right = [(cx, cy), (cx + s * 0.87, cy - s * 0.5), (cx + s * 0.87, cy + s * 0.5), (cx, cy + s)]
g.polygon(top, fill=(200, 225, 255, 70))
g.polygon(left, fill=(120, 160, 255, 45))
g.polygon(right, fill=(90, 120, 230, 35))
for poly, a in ((top, 170), (left, 110), (right, 90)):
    g.line(poly + [poly[0]], fill=(220, 235, 255, a), width=2)
glow = c.filter(ImageFilter.GaussianBlur(6))
img = Image.alpha_composite(Image.alpha_composite(img.convert('RGBA'), glow), c)
# a soft highlight on the top face
h = Image.new('RGBA', (W, W), (0, 0, 0, 0))
ImageDraw.Draw(h).ellipse((cx - 30, cy - s * 0.78, cx + 10, cy - s * 0.52), fill=(255, 255, 255, 60))
img = Image.alpha_composite(img, h.filter(ImageFilter.GaussianBlur(5)))
img.convert('RGB').save(sys.argv[1], 'JPEG', quality=92)
