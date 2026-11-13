"""Build single-pad band art from native page pixels.

The settings page draws both cards inside assets/settings-bg.png. The
single-pad layout has to replace them with one centred card without
shifting any colour: the band background and the card fill have to match
the page byte for byte.

  settings-single-base.png   native band, cards erased with a native
                             background fill, native left card re-pasted
                             centred with untouched pixels.
  settings-single-flash.png  RGBA. Only the orange rim is added on top of
                             the base art, so a flash changes the rim and
                             nothing else.
"""

import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
# The build reads ui/assets, so default there and fall back to the script's
# own folder when it is run from a scratch copy.
ASSETS = os.path.join(HERE, "..", "ui", "assets")
if not os.path.isfile(os.path.join(ASSETS, "settings-bg.png")):
    ASSETS = HERE
BG = os.path.join(ASSETS, "settings-bg.png")
FLASH_SRC = os.path.join(HERE, "settings-single-band.jpg")

BAND_TOP = 76
W, H = 480, 112

# Native card block, wide enough that its edge ramp lands on plain
# background instead of on the card stroke.
CARD_BLOCK = (4, 4, 246, 108)
# Native card interior, used to centre the copy on the value text.
CARD_INNER = (17, 17, 227, 98)
# Painted edge of the native card, in band coordinates.
CARD_STROKE = (15, 17, 230, 98)

# Two-pad flash art and where it sits in the band; the glow is borrowed
# from there so both layouts flash the same way.
FLASH_SRC = os.path.join(ASSETS, "settings-center-active.jpg")
FLASH_ORIGIN = (20, 19)


def row_bg(px, y, x0, x1):
    vals = [px[x, y] for x in range(x0, x1)]
    n = len(vals)
    return tuple(sum(v[i] for v in vals) // n for i in range(3))


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def fill_background(base):
    px = base.load()
    top, bottom = CARD_BLOCK[1], CARD_BLOCK[3]
    # The background behind the cards is never rendered in the native art,
    # so anchor each row on the three places it does show - left margin,
    # the gap between the cards, right margin - and interpolate across.
    for y in range(top, bottom):
        a = row_bg(px, y, 4, 12)
        m = row_bg(px, y, 236, 246)
        c = row_bg(px, y, 469, 477)
        for x in range(0, W):
            if x <= 240:
                col = lerp(a, m, x / 240.0)
            else:
                col = lerp(m, c, (x - 240) / 240.0)
            px[x, y] = tuple(int(round(v)) for v in col)


def paste_card(base, nat):
    block = nat.crop(CARD_BLOCK)
    # Centre on the interior so the value sits dead centre.
    inner_cx = (CARD_INNER[0] + CARD_INNER[2]) / 2.0
    left = int(round(W / 2.0 - (inner_cx - CARD_BLOCK[0])))
    top = CARD_BLOCK[1]

    # Ramp the block's outer 4 px into the background. That margin is plain
    # background on both sides, so the seam disappears while the card
    # stroke, 11 px inside, is copied verbatim.
    ramp = 4
    mask = Image.new("L", block.size, 0)
    d = ImageDraw.Draw(mask)
    d.rectangle((0, 0, block.width - 1, block.height - 1), fill=255)
    for i in range(ramp):
        v = int(round(255 * (i + 1) / float(ramp + 1)))
        d.rectangle((i, i, block.width - 1 - i, block.height - 1 - i),
                    outline=v, width=1)
    mask = mask.filter(ImageFilter.GaussianBlur(0.8))

    base.paste(block, (left, top), mask)
    return left


def build_flash(base, card):
    """Composite the page's own flash look over the base art.

    settings-center-active.jpg is what the two-pad layout flashes, so its rim
    is the established style. The art is exactly one card face, and the page
    lays it centred inside the card, so the same placement is reused here.

    The rim hugs the art's first row, so the unlit fill has to come from the
    interior rows: a per-row median of the middle columns gives the card
    body, and the difference is the light the rim adds. Adding that light to
    the base pixels leaves every other pixel exactly as drawn.
    """
    src = Image.open(FLASH_SRC).convert("RGB")
    w, h = src.size
    px = src.load()

    # Unlit fill, measured well inside the rim: rows and columns close to
    # the border already sit in the glow and would flatten the difference.
    deep = []
    for y in range(22, h - 22):
        for x in range(22, w - 22):
            deep.append(px[x, y])
    deep.sort(key=lambda c: c[0])
    n = len(deep)
    b = tuple(sum(c[i] for c in deep[n // 3:2 * n // 3]) // (n - 2 * (n // 3))
              for i in range(3))

    lit = src.copy()
    lp = lit.load()
    alpha = Image.new("L", src.size, 0)
    ap = alpha.load()
    # Wherever the art stands above its own fill it is showing light: keep
    # that pixel as painted and fade it out as the light dies off. Nothing
    # else is touched, so the card face keeps the base art's pixels.
    for y in range(h):
        for x in range(w):
            c = px[x, y]
            dev = max(c[0] - b[0], c[1] - b[1], c[2] - b[2])
            if dev <= 5:
                ap[x, y] = 0
            elif dev >= 45:
                ap[x, y] = 255
            else:
                ap[x, y] = int((dev - 5) * 255 / 40)

    # Only the border band carries light; the art's interior decals do not.
    band = Image.new("L", src.size, 0)
    d = ImageDraw.Draw(band)
    d.rectangle((0, 0, w - 1, h - 1), fill=255)
    d.rounded_rectangle((26, 26, w - 27, h - 27), radius=5, fill=0)
    band = band.filter(ImageFilter.GaussianBlur(4))
    alpha = ImageChops.multiply(alpha, band)

    # Same placement the two-pad layout uses: art origin five pixels in from
    # the card's painted edge.
    left = card[0] + 5
    top = card[1] + 2

    out = Image.new("RGBA", base.size, (0, 0, 0, 0))
    out.paste(lit, (left, top))
    a = Image.new("L", base.size, 0)
    a.paste(alpha, (left, top))
    out.putalpha(a)
    return out, (left, top)


def main():
    bg = Image.open(BG).convert("RGB")
    nat = bg.crop((0, BAND_TOP, W, BAND_TOP + H))

    base = nat.copy()
    fill_background(base)
    left = paste_card(base, nat)
    base.save(os.path.join(ASSETS, "settings-single-base.png"), optimize=True)

    shift = left - CARD_BLOCK[0]
    card = (CARD_STROKE[0] + shift, CARD_STROKE[1],
            CARD_STROKE[2] + shift, CARD_STROKE[3])
    flash, at = build_flash(base, card)
    flash.save(os.path.join(ASSETS, "settings-single-flash.png"), optimize=True)

    for name in ("settings-single-base.png", "settings-single-flash.png"):
        p = os.path.join(ASSETS, name)
        print(name, os.path.getsize(p), "bytes")
    inner = (CARD_INNER[0] + left - CARD_BLOCK[0] + 4,
             CARD_INNER[1], CARD_INNER[2] + left - CARD_BLOCK[0] + 4,
             CARD_INNER[3])
    print("flash art at", at, "inner rect", inner)
    print("card px", base.getpixel(((inner[0] + inner[2]) // 2, 60)),
          "native", nat.getpixel(((CARD_INNER[0] + CARD_INNER[2]) // 2, 60)))


if __name__ == "__main__":
    main()
