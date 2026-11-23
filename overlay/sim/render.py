# Brume Overlay on the PC: makes the stand-in game (`python3 render.py game` -> game.png + game.rgba, ours: a dusk sky,
# hills, a road), then turns the simulator's raw frames (1280x720 RGBA, already put together as the display would) into PNGs.
import sys, os
from PIL import Image, ImageDraw
here = os.path.dirname(os.path.abspath(__file__))

def make_game():
    im = Image.new('RGB', (1280, 720))
    d = ImageDraw.Draw(im)
    for y in range(720):
        t = y / 720
        d.line([(0, y), (1280, y)], fill=(int(250 - 140 * t), int(150 - 90 * t), int(120 + 40 * t)))
    d.ellipse((860, 170, 1020, 330), fill=(255, 226, 150))
    d.polygon([(0, 470), (180, 330), (360, 430), (560, 300), (820, 440), (1040, 340), (1280, 450), (1280, 720), (0, 720)], fill=(70, 50, 95))
    d.polygon([(0, 560), (300, 480), (640, 540), (980, 470), (1280, 540), (1280, 720), (0, 720)], fill=(40, 90, 70))
    d.polygon([(560, 720), (640, 520), (700, 520), (900, 720)], fill=(60, 60, 70))
    for k in range(6):
        y = 540 + k * 32
        d.rectangle((640 + k * 20 - 3, y, 646 + k * 22, y + 14), fill=(230, 220, 120))
    d.rectangle((40, 40, 360, 64), fill=(30, 30, 40)); d.rectangle((44, 44, 290, 60), fill=(220, 60, 70))
    d.ellipse((1110, 540, 1250, 680), outline=(240, 240, 240), width=4)
    d.rectangle((600, 600, 640, 680), fill=(200, 80, 60)); d.ellipse((596, 560, 644, 606), fill=(240, 200, 170))
    im.save(os.path.join(here, 'game.png'))
    open(os.path.join(here, 'game.rgba'), 'wb').write(im.convert('RGBA').tobytes())

if len(sys.argv) > 1 and sys.argv[1] == 'game':
    make_game(); print('jeu : ok'); sys.exit(0)
names = ['power', 'power_anim', 'quick', 'quick_anim', 'quick_bright', 'quick_perf', 'quick_notes', 'quick_volume', 'volume']
shots = []
for name in names:
    p = os.path.join(here, name + '.rgba')
    if not os.path.exists(p):
        continue
    im = Image.frombuffer('RGBA', (1280, 720), open(p, 'rb').read(), 'raw', 'RGBA', 0, 1).convert('RGB')
    im.save(os.path.join(here, name + '.png'))
    shots.append(im.resize((640, 360), Image.LANCZOS))
sheet = Image.new('RGB', (1280, 360 * ((len(shots) + 1) // 2)))
for i, s in enumerate(shots): sheet.paste(s, ((i % 2) * 640, (i // 2) * 360))
sheet.save(os.path.join(here, 'planche.png'))
print('images : ok')
