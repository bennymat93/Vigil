#!/usr/bin/env python3
"""
Vigil icon generator.

Renders a professional, glossy 3D "watchful eye" badge in two states:
  - active  : the eye is open and glowing (keep-awake ON)
  - paused  : the eye is closed / dimmed    (keep-awake OFF)

Everything is drawn at a large supersampled size and downscaled with LANCZOS
so every tray size (16..256) stays crisp. Outputs multi-resolution .ico files
plus PNG previews for the README.
"""
from PIL import Image, ImageDraw, ImageFilter
import math, os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets")
os.makedirs(OUT, exist_ok=True)

SS = 1024          # supersample canvas
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def vgrad(size, top, bottom, mask=None):
    """Vertical gradient image."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    for y in range(size):
        t = y / (size - 1)
        c = lerp(top, bottom, t)
        for x in range(size):
            px[x, y] = (c[0], c[1], c[2], 255)
    if mask is not None:
        img.putalpha(mask)
    return img


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def make_badge(active: bool) -> Image.Image:
    S = SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    pad = int(S * 0.06)
    inner = S - pad * 2
    radius = int(inner * 0.28)

    # ---- soft drop shadow ----
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle([pad, pad + int(S * 0.03), pad + inner, pad + inner + int(S * 0.03)],
                         radius=radius, fill=(0, 0, 0, 120))
    shadow = shadow.filter(ImageFilter.GaussianBlur(S * 0.035))
    img = Image.alpha_composite(img, shadow)

    # ---- badge body (vertical gradient) ----
    if active:
        top = (46, 130, 255)      # bright blue
        bottom = (10, 36, 120)    # deep navy
    else:
        top = (120, 130, 146)     # cool grey
        bottom = (52, 60, 74)
    mask = rounded_mask(inner, radius)
    body = vgrad(inner, top, bottom, mask)
    body_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    body_layer.paste(body, (pad, pad), body)
    img = Image.alpha_composite(img, body_layer)

    d = ImageDraw.Draw(img)

    # ---- subtle inner border for depth ----
    d.rounded_rectangle([pad, pad, pad + inner, pad + inner], radius=radius,
                        outline=(255, 255, 255, 40), width=max(2, int(S * 0.006)))

    # ---- glossy top highlight (specular) ----
    gloss = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gloss)
    gh = int(inner * 0.46)
    gd.rounded_rectangle([pad + int(inner * 0.06), pad + int(inner * 0.05),
                          pad + inner - int(inner * 0.06), pad + gh],
                         radius=int(radius * 0.8), fill=(255, 255, 255, 55))
    gloss = gloss.filter(ImageFilter.GaussianBlur(S * 0.012))
    gmask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(gmask).rounded_rectangle([pad, pad, pad + inner, pad + inner],
                                            radius=radius, fill=255)
    gloss.putalpha(Image.composite(gloss.getchannel("A"), Image.new("L", (S, S), 0), gmask))
    img = Image.alpha_composite(img, gloss)

    d = ImageDraw.Draw(img)

    # ---- eye geometry ----
    cx, cy = S // 2, int(S * 0.52)
    eye_w = int(inner * 0.62)
    eye_h = int(inner * 0.34)

    if active:
        # open almond eye: intersection of two circles -> draw via polygon arcs
        white = (244, 249, 255, 255)
        # almond outline
        pts_top, pts_bot = [], []
        for i in range(0, 101):
            t = i / 100.0
            x = cx - eye_w // 2 + int(eye_w * t)
            off = math.sin(math.pi * t) * (eye_h / 2)
            pts_top.append((x, int(cy - off)))
            pts_bot.append((x, int(cy + off)))
        poly = pts_top + pts_bot[::-1]
        d.polygon(poly, fill=white)

        # iris (glowing cyan) with radial-ish shading
        iris_r = int(eye_h * 0.52)
        # glow
        glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse([cx - iris_r * 2, cy - iris_r * 2,
                                      cx + iris_r * 2, cy + iris_r * 2],
                                     fill=(34, 211, 238, 70))
        glow = glow.filter(ImageFilter.GaussianBlur(S * 0.02))
        # clip glow to eye white
        eyemask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(eyemask).polygon(poly, fill=255)
        glow.putalpha(Image.composite(glow.getchannel("A"), Image.new("L", (S, S), 0), eyemask))
        img = Image.alpha_composite(img, glow)
        d = ImageDraw.Draw(img)

        d.ellipse([cx - iris_r, cy - iris_r, cx + iris_r, cy + iris_r], fill=(20, 150, 180, 255))
        d.ellipse([cx - iris_r, cy - iris_r, cx + iris_r, cy + iris_r],
                  outline=(14, 90, 120, 255), width=max(2, int(S * 0.004)))
        # pupil
        pr = int(iris_r * 0.5)
        d.ellipse([cx - pr, cy - pr, cx + pr, cy + pr], fill=(8, 18, 30, 255))
        # specular highlight dot
        hr = int(iris_r * 0.26)
        hx, hy = cx - int(iris_r * 0.35), cy - int(iris_r * 0.38)
        d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=(255, 255, 255, 235))
        # re-stroke the almond edge for crispness
        d.line(pts_top + [pts_bot[-1]], fill=(210, 228, 245, 255), width=max(2, int(S * 0.006)), joint="curve")
        d.line(pts_bot, fill=(210, 228, 245, 255), width=max(2, int(S * 0.006)), joint="curve")
    else:
        # closed / sleeping eye: a soft curved lid + lashes, dimmed
        lw = max(6, int(S * 0.018))
        lid = []
        for i in range(0, 101):
            t = i / 100.0
            x = cx - eye_w // 2 + int(eye_w * t)
            off = math.sin(math.pi * t) * (eye_h * 0.42)
            lid.append((x, int(cy + off)))   # downward curve = closed
        d.line(lid, fill=(226, 232, 240, 255), width=lw, joint="curve")
        # a couple of short lashes
        for t in (0.25, 0.5, 0.75):
            x = cx - eye_w // 2 + int(eye_w * t)
            off = math.sin(math.pi * t) * (eye_h * 0.42)
            y = int(cy + off)
            d.line([(x, y), (x, y + int(eye_h * 0.28))], fill=(226, 232, 240, 230), width=max(3, int(S * 0.010)))
        # faint "zz" to read as paused/asleep
        zx, zy = int(cx + eye_w * 0.40), int(cy - eye_h * 0.85)
        zc = (226, 232, 240, 210)
        for scale, dx, dy in ((1.0, 0, 0), (0.6, int(S*0.085), int(-S*0.07))):
            s = int(eye_h * 0.32 * scale)
            x0, y0 = zx + dx, zy + dy
            d.line([(x0, y0), (x0 + s, y0), (x0, y0 + s), (x0 + s, y0 + s)],
                   fill=zc, width=max(3, int(S * 0.010)), joint="curve")

    return img


def main():
    for state in ("active", "paused"):
        big = make_badge(state == "active")
        # PNG preview (256)
        prev = big.resize((256, 256), Image.LANCZOS)
        prev.save(os.path.join(OUT, f"vigil_{state}_256.png"))
        # multi-size ICO: hand PIL a clean 256 master and let it emit every size
        master = big.resize((256, 256), Image.LANCZOS)
        master.save(os.path.join(OUT, f"vigil_{state}.ico"),
                    format="ICO", sizes=[(s, s) for s in ICO_SIZES])
    # a combined hero image for README
    a = Image.open(os.path.join(OUT, "vigil_active_256.png"))
    print("icons written to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
