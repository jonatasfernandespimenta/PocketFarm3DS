# Bakes the UI fonts into atlases: <out>/fonts/<name>.png (white glyphs, alpha) + <name>.json metrics.
#   python3 tools/font.py <romfs>
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(__file__)
CHARS = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(160, 256)] + list('…·—–×→←↑↓“”‘’•★♥')
FONTS = {  # name: (file, size, weight, antialias)
    'px': ('Jersey10-Regular.ttf', 16, None, False),
    'body': ('Nunito.ttf', 11, 700, True),
    'small': ('Nunito.ttf', 9, 700, True),
}
out = os.path.join(sys.argv[1], 'fonts'); os.makedirs(out, exist_ok=True)
for name, (file, size, weight, aa) in FONTS.items():
    f = ImageFont.truetype(os.path.join(HERE, 'fonts', file), size)
    if weight: f.set_variation_by_axes([weight])
    asc, desc = f.getmetrics()
    atlas = Image.new('LA', (256, 256), (255, 0)); x = y = 0; rowh = 0; glyphs = {}
    tofu = bytes(f.getmask('\ue000'))   # what the font draws for a character it doesn't have
    for ch in CHARS:
        if ord(ch) > 255 and bytes(f.getmask(ch)) == tofu: continue   # missing: the game falls back to ASCII
        bb = f.getbbox(ch)
        adv = round(f.getlength(ch))
        if bb[2] <= bb[0]: glyphs[ord(ch)] = [0, 0, 0, 0, 0, 0, adv]; continue
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        if x + w + 1 > 256: x = 0; y += rowh + 1; rowh = 0
        g = Image.new('L', (w, h)); d = ImageDraw.Draw(g); d.fontmode = 'L' if aa else '1'
        d.text((-bb[0], -bb[1]), ch, font=f, fill=255)
        atlas.paste(Image.merge('LA', (Image.new('L', (w, h), 255), g)), (x, y))
        glyphs[ord(ch)] = [x, y, w, h, bb[0], bb[1], adv]
        x += w + 1; rowh = max(rowh, h)
    atlas.convert('RGBA').save(os.path.join(out, name + '.png'))
    json.dump({'ascent': asc, 'descent': desc, 'glyphs': glyphs}, open(os.path.join(out, name + '.json'), 'w'))
print('fonts:', ', '.join(FONTS))
