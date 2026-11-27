#!/usr/bin/env python3
"""Generates BrazenWorks' full-colour toolbar icons (resources/icons/color/*.svg).

Original artwork: shaded isometric solids, light sketch geometry and small
coloured badges, drawn from a few primitives so the set stays consistent.
Run from anywhere:  python3 tools/icons/make_color_icons.py
"""
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "resources", "icons", "color")

# Palettes: top / left / right faces and outline.
BLUE = dict(top="#9BD1F8", left="#4A9BE0", right="#2F72BA", line="#1C4E85")
GREY = dict(top="#F3F5F7", left="#CDD3DA", right="#A6AFB9", line="#56616C")
ORANGE = dict(top="#FFD08A", left="#F5A23A", right="#D97F17", line="#8A4A08")
PURPLE = dict(top="#D4BCF7", left="#9B6BE0", right="#7347BA", line="#46247A")
GREEN = "#3DBB4F"
RED = "#E5484D"
AMBER = "#F39A2B"
POINT = "#4FA3F7"
INK = "#E9EDF1"  # light line work (icons sit on a dark ribbon)
INK_DIM = "#9AA4AE"


class Icon:
    def __init__(self, name):
        self.name = name
        self.parts = []
        self.defs = []
        self.n = 0

    def uid(self, tag):
        self.n += 1
        return f"{self.name}-{tag}{self.n}"

    def add(self, s):
        self.parts.append(s)

    def save(self):
        svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">']
        if self.defs:
            svg.append("<defs>" + "".join(self.defs) + "</defs>")
        svg += self.parts
        svg.append("</svg>\n")
        with open(os.path.join(OUT, self.name + ".svg"), "w") as f:
            f.write("".join(svg))


# ---- Isometric projection ----------------------------------------------------
class Iso:
    """x to the right-down, y to the left-down, z up."""

    def __init__(self, ox=16, oy=18, s=1.0):
        self.ox, self.oy, self.s = ox, oy, s

    def p(self, x, y, z=0.0):
        return (self.ox + (x - y) * 0.866 * self.s, self.oy + (x + y) * 0.5 * self.s - z * self.s)


def pts(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def poly(ic, points, fill, stroke="none", sw=0.8, extra=""):
    ic.add(f'<polygon points="{pts(points)}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" '
           f'stroke-linejoin="round" {extra}/>')


def line(ic, points, stroke=INK, sw=1.6, dash=None, cap="round"):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    ic.add(f'<polyline points="{pts(points)}" fill="none" stroke="{stroke}" stroke-width="{sw}" '
           f'stroke-linecap="{cap}" stroke-linejoin="round"{d}/>')


def path(ic, d, fill="none", stroke=INK, sw=1.6, extra=""):
    ic.add(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="round" '
           f'stroke-linejoin="round" {extra}/>')


def box(ic, iso, x, y, z, w, d, h, c=BLUE, sw=0.8):
    p = iso.p
    top = [p(x, y, z + h), p(x + w, y, z + h), p(x + w, y + d, z + h), p(x, y + d, z + h)]
    left = [p(x, y + d, z), p(x + w, y + d, z), p(x + w, y + d, z + h), p(x, y + d, z + h)]
    right = [p(x + w, y, z), p(x + w, y + d, z), p(x + w, y + d, z + h), p(x + w, y, z + h)]
    poly(ic, left, c["left"], c["line"], sw)
    poly(ic, right, c["right"], c["line"], sw)
    poly(ic, top, c["top"], c["line"], sw)
    return top, left, right


def cylinder(ic, iso, cx, cy, z, r, h, c=BLUE, sw=0.8, top_fill=None):
    """Vertical cylinder; returns the top ellipse (cx, cy, rx, ry)."""
    bx, by = iso.p(cx, cy, z)
    tx, ty = iso.p(cx, cy, z + h)
    rx, ry = 1.2247 * r * iso.s, 0.7071 * r * iso.s
    g = ic.uid("g")
    ic.defs.append(f'<linearGradient id="{g}" x1="0" x2="1" y1="0" y2="0">'
                   f'<stop offset="0" stop-color="{c["left"]}"/><stop offset="0.45" stop-color="{c["top"]}"/>'
                   f'<stop offset="1" stop-color="{c["right"]}"/></linearGradient>')
    path(ic, f"M{bx - rx:.2f},{by:.2f} A{rx:.2f},{ry:.2f} 0 0 0 {bx + rx:.2f},{by:.2f} L{tx + rx:.2f},{ty:.2f} "
             f"L{tx - rx:.2f},{ty:.2f} Z", fill=f"url(#{g})", stroke=c["line"], sw=sw)
    ic.add(f'<ellipse cx="{tx:.2f}" cy="{ty:.2f}" rx="{rx:.2f}" ry="{ry:.2f}" fill="{top_fill or c["top"]}" '
           f'stroke="{c["line"]}" stroke-width="{sw}"/>')
    return tx, ty, rx, ry


def plane(ic, iso, x, y, z, w, d, fill="#C9D3DC", stroke="#7D8A96", opacity=0.9):
    p = iso.p
    poly(ic, [p(x, y, z), p(x + w, y, z), p(x + w, y + d, z), p(x, y + d, z)], fill, stroke, 0.8,
         f'fill-opacity="{opacity}"')


def badge(ic, x, y, kind="plus", color=GREEN, r=4.6):
    ic.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}" stroke="#14532D" stroke-width="0.7"/>')
    if kind == "plus":
        ic.add(f'<path d="M{x - 2.4},{y}H{x + 2.4}M{x},{y - 2.4}V{y + 2.4}" stroke="#fff" stroke-width="1.7" '
               f'stroke-linecap="round"/>')
    elif kind == "play":
        poly(ic, [(x - 1.5, y - 2.4), (x + 2.6, y), (x - 1.5, y + 2.4)], "#fff")


def arrow(ic, x0, y0, x1, y1, color=AMBER, sw=2.0, head=3.6):
    a = math.atan2(y1 - y0, x1 - x0)
    hx, hy = x1 - head * math.cos(a), y1 - head * math.sin(a)
    line(ic, [(x0, y0), (hx, hy)], color, sw)
    l = (x1 - head * math.cos(a) + head * 0.6 * math.cos(a + math.pi / 2),
         y1 - head * math.sin(a) + head * 0.6 * math.sin(a + math.pi / 2))
    r = (x1 - head * math.cos(a) - head * 0.6 * math.cos(a + math.pi / 2),
         y1 - head * math.sin(a) - head * 0.6 * math.sin(a + math.pi / 2))
    poly(ic, [(x1, y1), l, r], color, color, 0.6)


def pt(ic, x, y, color=POINT, s=3.0):
    ic.add(f'<rect x="{x - s / 2:.2f}" y="{y - s / 2:.2f}" width="{s}" height="{s}" fill="{color}" '
           f'stroke="#0F2C4D" stroke-width="0.6"/>')


def mark(ic, text, x, y, color=AMBER, size=9):
    ic.add(f'<text x="{x}" y="{y}" font-family="Segoe UI, Arial, sans-serif" font-size="{size}" '
           f'font-weight="700" fill="{color}" text-anchor="middle">{text}</text>')


ICONS = {}


def icon(name):
    def wrap(fn):
        ICONS[name] = fn
        return fn
    return wrap


# ---- Design: create ------------------------------------------------------------
@icon("sketch")
def _(ic):
    iso = Iso(15, 19, 1.0)
    plane(ic, iso, -8, -8, 0, 16, 16, "#DDE4EA", "#8794A0")
    path(ic, "M9,21 C11,13 17,11 22,15", stroke=AMBER, sw=1.8)
    line(ic, [(9, 21), (19, 23)], "#2F3A45", 1.4)
    for x, y in ((9, 21), (22, 15), (19, 23)):
        pt(ic, x, y)
    badge(ic, 25, 8)


@icon("box")
def _(ic):
    box(ic, Iso(16, 17, 1.0), -6, -6, -3, 12, 12, 10)


@icon("extrude")
def _(ic):
    iso = Iso(15, 19, 1.0)
    plane(ic, iso, -8, -8, -3, 15, 15, "#DDE4EA", "#8794A0")
    box(ic, iso, -5, -5, -3, 9, 9, 9)
    arrow(ic, 26, 22, 26, 6)


@icon("revolve")
def _(ic):
    iso = Iso(15, 18, 1.0)
    cylinder(ic, iso, 0, 0, -5, 6.5, 11)
    line(ic, [(15, 2), (15, 30)], "#2F3A45", 1.0, "2 1.6")
    path(ic, "M7,7 A10,4 0 1 0 24,6", stroke=AMBER, sw=1.9)
    poly(ic, [(24.5, 3.3), (26, 7.5), (21.8, 7)], AMBER)


@icon("sweep")
def _(ic):
    path(ic, "M5,26 C5,14 15,18 16,10 C17,4 24,5 27,6", stroke=BLUE["line"], sw=7.5)
    path(ic, "M5,26 C5,14 15,18 16,10 C17,4 24,5 27,6", stroke=BLUE["left"], sw=5.8)
    path(ic, "M4.4,25 C4.6,15 14,17.4 15.2,9.6", stroke=BLUE["top"], sw=1.6)
    ic.add('<ellipse cx="5" cy="27" rx="3.6" ry="1.8" fill="#9BD1F8" stroke="#1C4E85" stroke-width="0.8"/>')


@icon("loft")
def _(ic):
    iso = Iso(16, 17, 1.0)
    p = iso.p
    # Square base to round top.
    plane(ic, iso, -6, -6, -6, 12, 12, BLUE["left"], BLUE["line"], 1.0)
    path(ic, f"M{p(-6, 6, -6)[0]:.1f},{p(-6, 6, -6)[1]:.1f} L11,9 A5,2.8 0 0 0 21,9 "
             f"L{p(6, -6, -6)[0]:.1f},{p(6, -6, -6)[1]:.1f} L{p(6, 6, -6)[0]:.1f},{p(6, 6, -6)[1]:.1f} Z",
         fill=BLUE["left"], stroke=BLUE["line"], sw=0.8)
    path(ic, f"M21,9 L{p(6, -6, -6)[0]:.1f},{p(6, -6, -6)[1]:.1f} L{p(6, 6, -6)[0]:.1f},{p(6, 6, -6)[1]:.1f} "
             f"L16,11.8 Z", fill=BLUE["right"], stroke=BLUE["line"], sw=0.8)
    ic.add('<ellipse cx="16" cy="9" rx="5" ry="2.8" fill="#9BD1F8" stroke="#1C4E85" stroke-width="0.8"/>')


@icon("hole")
def _(ic):
    iso = Iso(16, 18, 1.0)
    box(ic, iso, -7, -7, -4, 14, 14, 7, GREY)
    cx, cy = iso.p(0, 0, 3)
    ic.add(f'<ellipse cx="{cx}" cy="{cy}" rx="4.6" ry="2.6" fill="#1D2329" stroke="#2F72BA" stroke-width="1.2"/>')
    path(ic, f"M{cx - 4.6},{cy} A4.6,2.6 0 0 0 {cx + 4.6},{cy}", stroke="#4A9BE0", sw=1.2)
    line(ic, [(cx, 2), (cx, cy - 3.5)], AMBER, 1.8)
    poly(ic, [(cx, cy - 1.2), (cx - 2.2, cy - 4.4), (cx + 2.2, cy - 4.4)], AMBER)


# ---- Design: modify ------------------------------------------------------------
@icon("press-pull")
def _(ic):
    iso = Iso(16, 20, 1.0)
    box(ic, iso, -7, -7, -4, 14, 14, 5, GREY)
    box(ic, iso, -4, -4, 1, 8, 8, 4, BLUE)
    arrow(ic, 25, 18, 25, 5)
    arrow(ic, 25, 14, 25, 28, AMBER, 1.6, 3.0)


@icon("fillet")
def _(ic):
    iso = Iso(15, 18, 1.0)
    box(ic, iso, -6, -6, -5, 12, 12, 10, GREY)
    p = iso.p
    a, b, c = p(6, -6, 5), p(6, 6, 5), p(6, 6, 2)
    path(ic, f"M{p(3, -6, 5)[0]:.1f},{p(3, -6, 5)[1]:.1f} L{p(3, 6, 5)[0]:.1f},{p(3, 6, 5)[1]:.1f} "
             f"Q{b[0]:.1f},{b[1]:.1f} {c[0]:.1f},{c[1]:.1f} L{p(6, -6, 2)[0]:.1f},{p(6, -6, 2)[1]:.1f} "
             f"Q{a[0]:.1f},{a[1]:.1f} {p(3, -6, 5)[0]:.1f},{p(3, -6, 5)[1]:.1f} Z",
         fill=BLUE["left"], stroke=BLUE["line"], sw=0.9)


@icon("chamfer")
def _(ic):
    iso = Iso(15, 18, 1.0)
    box(ic, iso, -6, -6, -5, 12, 12, 10, GREY)
    p = iso.p
    poly(ic, [p(3, -6, 5), p(3, 6, 5), p(6, 6, 2), p(6, -6, 2)], BLUE["left"], BLUE["line"], 0.9)


@icon("shell")
def _(ic):
    iso = Iso(16, 18, 1.0)
    box(ic, iso, -7, -7, -5, 14, 14, 9, GREY)
    p = iso.p
    poly(ic, [p(-5, -5, 4), p(5, -5, 4), p(5, 5, 4), p(-5, 5, 4)], "#3A4550", GREY["line"], 0.8)
    poly(ic, [p(-5, -5, 4), p(5, -5, 4), p(5, -5, -2), p(-5, -5, -2)], BLUE["right"], BLUE["line"], 0.6)
    poly(ic, [p(-5, -5, 4), p(-5, 5, 4), p(-5, 5, -2), p(-5, -5, -2)], BLUE["left"], BLUE["line"], 0.6)


@icon("combine")
def _(ic):
    box(ic, Iso(13, 20, 0.9), -6, -6, -3, 11, 11, 9, GREY)
    box(ic, Iso(19, 17, 0.9), -5, -5, -3, 10, 10, 9, BLUE)


# ---- Design: assemble / construct / inspect / insert / select -------------------
@icon("joint")
def _(ic):
    box(ic, Iso(11, 20, 0.85), -5, -5, -4, 10, 10, 6, GREY)
    box(ic, Iso(21, 13, 0.85), -5, -5, -4, 10, 10, 6, PURPLE)
    ic.add('<circle cx="16" cy="17" r="4.2" fill="none" stroke="#F39A2B" stroke-width="1.8"/>')
    ic.add('<circle cx="16" cy="17" r="1.4" fill="#F39A2B"/>')


@icon("plane")
def _(ic):
    iso = Iso(16, 19, 1.0)
    plane(ic, iso, -8, -8, -4, 16, 16, "#C9D3DC", "#7D8A96", 0.8)
    plane(ic, iso, -8, -8, 4, 16, 16, ORANGE["top"], ORANGE["right"], 0.85)
    arrow(ic, 27, 22, 27, 9, POINT, 1.6, 3.0)


@icon("axis")
def _(ic):
    iso = Iso(16, 19, 1.0)
    cylinder(ic, iso, 0, 0, -6, 5, 10, GREY)
    line(ic, [(16, 2), (16, 30)], AMBER, 1.8)


@icon("measure")
def _(ic):
    ic.add('<g transform="rotate(-35 16 16)"><rect x="3" y="12" width="26" height="8" rx="1" fill="#F2C94C" '
           'stroke="#8A6A10" stroke-width="0.8"/>'
           + "".join(f'<path d="M{5 + i * 3},12v{3 if i % 2 else 5}" stroke="#5A4508" stroke-width="0.9"/>'
                     for i in range(8)) + "</g>")


@icon("insert")
def _(ic):
    box(ic, Iso(17, 21, 0.9), -6, -6, -4, 12, 12, 8, GREY)
    arrow(ic, 6, 4, 14, 12, GREEN, 2.2, 4.0)


@icon("select")
def _(ic):
    ic.add('<rect x="4" y="4" width="20" height="20" fill="none" stroke="#9AA4AE" stroke-width="1.1" '
           'stroke-dasharray="2.2 1.8"/>')
    poly(ic, [(14, 11), (14, 27), (18, 23), (21, 29), (23.5, 28), (20.6, 22.2), (26, 22)], "#FFFFFF",
         "#20262C", 1.0)


# ---- Sketch tools (flat) -------------------------------------------------------
@icon("line")
def _(ic):
    line(ic, [(7, 25), (25, 7)])
    pt(ic, 7, 25)
    pt(ic, 25, 7)


@icon("rectangle")
def _(ic):
    ic.add(f'<rect x="6" y="9" width="20" height="14" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" '
           'stroke-width="1.6"/>')
    pt(ic, 6, 23)
    pt(ic, 26, 9)


@icon("circle")
def _(ic):
    ic.add(f'<circle cx="16" cy="16" r="10" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" stroke-width="1.6"/>')
    pt(ic, 16, 16)
    line(ic, [(16, 16), (23.1, 8.9)], INK_DIM, 1.0, "1.6 1.4")


@icon("arc")
def _(ic):
    path(ic, "M5,23 A12,12 0 0 1 27,23")
    for x, y in ((5, 23), (27, 23), (16, 11.9)):
        pt(ic, x, y)


@icon("point")
def _(ic):
    line(ic, [(16, 6), (16, 12)], INK_DIM, 1.2)
    line(ic, [(16, 20), (16, 26)], INK_DIM, 1.2)
    line(ic, [(6, 16), (12, 16)], INK_DIM, 1.2)
    line(ic, [(20, 16), (26, 16)], INK_DIM, 1.2)
    pt(ic, 16, 16, POINT, 4.2)


@icon("construction")
def _(ic):
    line(ic, [(6, 26), (26, 6)], AMBER, 1.8, "3 2.4")
    pt(ic, 6, 26)
    pt(ic, 26, 6)


@icon("close")
def _(ic):
    line(ic, [(9, 9), (23, 23)], RED, 3.0)
    line(ic, [(23, 9), (9, 23)], RED, 3.0)


@icon("rectangle3")
def _(ic):
    poly(ic, [(5, 20), (17, 6), (27, 14), (15, 28)], "#4A9BE0", INK, 1.6, 'fill-opacity="0.18"')
    for x, y in ((5, 20), (17, 6), (27, 14)):
        pt(ic, x, y)


@icon("rectanglec")
def _(ic):
    ic.add(f'<rect x="6" y="9" width="20" height="14" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" '
           'stroke-width="1.6"/>')
    line(ic, [(6, 23), (26, 9)], INK_DIM, 1.0, "1.6 1.4")
    pt(ic, 16, 16)
    pt(ic, 26, 9)


@icon("circle2")
def _(ic):
    ic.add(f'<circle cx="16" cy="16" r="10" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" stroke-width="1.6"/>')
    line(ic, [(8.9, 23.1), (23.1, 8.9)], INK_DIM, 1.0, "1.6 1.4")
    pt(ic, 8.9, 23.1)
    pt(ic, 23.1, 8.9)


@icon("circle3")
def _(ic):
    ic.add(f'<circle cx="16" cy="16" r="10" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" stroke-width="1.6"/>')
    for x, y in ((6, 16), (21, 7.3), (21, 24.7)):
        pt(ic, x, y)


@icon("arcc")
def _(ic):
    path(ic, "M26,16 A10,10 0 0 0 16,6")
    line(ic, [(16, 16), (26, 16)], INK_DIM, 1.0, "1.6 1.4")
    line(ic, [(16, 16), (16, 6)], INK_DIM, 1.0, "1.6 1.4")
    pt(ic, 16, 16)
    pt(ic, 26, 16)
    pt(ic, 16, 6)


@icon("arct")
def _(ic):
    line(ic, [(4, 24), (14, 24)])
    path(ic, "M14,24 A9,9 0 0 0 23,15", stroke=AMBER, sw=1.8)
    pt(ic, 14, 24)
    pt(ic, 23, 15)


@icon("polygon")
def _(ic):
    hexagon = [(16 + 10 * math.cos(math.radians(30 + 60 * k)), 16 + 10 * math.sin(math.radians(30 + 60 * k)))
               for k in range(6)]
    poly(ic, hexagon, "#4A9BE0", INK, 1.6, 'fill-opacity="0.18"')
    pt(ic, 16, 16)


@icon("ellipse")
def _(ic):
    ic.add(f'<ellipse cx="16" cy="16" rx="11" ry="6.5" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" '
           'stroke-width="1.6" transform="rotate(-20 16 16)"/>')
    pt(ic, 16, 16)


@icon("sketch-slot")
def _(ic):
    path(ic, "M10,11 H22 A5,5 0 0 1 22,21 H10 A5,5 0 0 1 10,11 Z", fill="#4A9BE0", extra='fill-opacity="0.18"')
    line(ic, [(10, 16), (22, 16)], INK_DIM, 1.0, "1.6 1.4")
    pt(ic, 10, 16)
    pt(ic, 22, 16)


@icon("spline")
def _(ic):
    path(ic, "M4,24 C9,6 15,6 17,16 S25,26 28,8")
    for x, y in ((4, 24), (12, 10.5), (17, 16), (24, 21), (28, 8)):
        pt(ic, x, y)


@icon("sketch-fillet")
def _(ic):
    line(ic, [(6, 6), (6, 18)])
    line(ic, [(14, 26), (26, 26)])
    path(ic, "M6,18 A8,8 0 0 0 14,26", stroke=AMBER, sw=2.0)
    line(ic, [(6, 18), (6, 26), (14, 26)], INK_DIM, 1.0, "1.6 1.4")


@icon("sketch-chamfer")
def _(ic):
    line(ic, [(6, 6), (6, 17)])
    line(ic, [(15, 26), (26, 26)])
    line(ic, [(6, 17), (15, 26)], AMBER, 2.0)
    line(ic, [(6, 17), (6, 26), (15, 26)], INK_DIM, 1.0, "1.6 1.4")


@icon("trim")
def _(ic):
    line(ic, [(5, 16), (27, 16)])
    line(ic, [(16, 5), (16, 15)])
    line(ic, [(16, 17), (16, 27)], RED, 1.6, "2 1.8")
    path(ic, "M20,21 l6,6 M26,21 l-6,6", stroke=RED, sw=1.8)


@icon("extend")
def _(ic):
    line(ic, [(26, 5), (26, 27)])
    line(ic, [(5, 16), (15, 16)])
    arrow(ic, 15, 16, 25, 16, GREEN, 1.8, 3.4)


@icon("break")
def _(ic):
    line(ic, [(5, 22), (14, 15)])
    line(ic, [(18, 12), (27, 5)])
    line(ic, [(10, 6), (22, 26)], INK_DIM, 1.2)
    pt(ic, 14, 15, AMBER)
    pt(ic, 18, 12, AMBER)


@icon("offset")
def _(ic):
    path(ic, "M5,26 V14 A8,8 0 0 1 13,6 H27")
    path(ic, "M10,26 V14 A3,3 0 0 1 13,11 H27", stroke=AMBER, sw=1.6)
    arrow(ic, 19, 6.5, 19, 10.5, POINT, 1.2, 2.4)


@icon("mirror")
def _(ic):
    line(ic, [(16, 3), (16, 29)], AMBER, 1.4, "2.2 1.8")
    poly(ic, [(5, 22), (12, 8), (12, 22)], "#4A9BE0", INK, 1.4, 'fill-opacity="0.18"')
    poly(ic, [(27, 22), (20, 8), (20, 22)], "none", INK_DIM, 1.2, 'stroke-dasharray="1.8 1.6"')


@icon("move")
def _(ic):
    for x0, y0, x1, y1 in ((16, 16, 16, 4), (16, 16, 16, 28), (16, 16, 4, 16), (16, 16, 28, 16)):
        arrow(ic, x0, y0, x1, y1, INK, 1.6, 3.4)
    pt(ic, 16, 16, AMBER, 3.6)


@icon("rotate")
def _(ic):
    path(ic, "M8,20 A9,9 0 1 1 13,25", stroke=INK, sw=1.6)
    poly(ic, [(13, 25), (8.5, 27.5), (9.3, 22.2)], INK, INK, 0.6)
    pt(ic, 16, 16, AMBER, 3.4)


@icon("scale")
def _(ic):
    ic.add(f'<rect x="5" y="15" width="12" height="12" fill="none" stroke="{INK_DIM}" stroke-width="1.2" '
           'stroke-dasharray="1.8 1.6"/>')
    ic.add(f'<rect x="5" y="5" width="22" height="22" fill="#4A9BE0" fill-opacity="0.15" stroke="{INK}" '
           'stroke-width="1.6"/>')
    arrow(ic, 15, 17, 24, 8, AMBER, 1.6, 3.2)


@icon("patternr")
def _(ic):
    for x in (6, 15, 24):
        for y in (8, 18):
            ic.add(f'<rect x="{x - 3}" y="{y - 3}" width="6" height="6" fill="#4A9BE0" fill-opacity="0.25" '
                   f'stroke="{INK if (x, y) == (6, 18) else INK_DIM}" stroke-width="1.3"/>')
    line(ic, [(4, 27), (28, 27)], AMBER, 1.2, "2 1.6")


@icon("patternc")
def _(ic):
    ic.add(f'<circle cx="16" cy="16" r="9" fill="none" stroke="{AMBER}" stroke-width="1.1" stroke-dasharray="2 1.6"/>')
    for k in range(6):
        a = math.radians(-90 + 60 * k)
        ic.add(f'<circle cx="{16 + 9 * math.cos(a):.2f}" cy="{16 + 9 * math.sin(a):.2f}" r="2.6" fill="#4A9BE0" '
               f'fill-opacity="0.35" stroke="{INK if k == 0 else INK_DIM}" stroke-width="1.2"/>')
    pt(ic, 16, 16, POINT, 2.6)


@icon("project")
def _(ic):
    box(ic, Iso(16, 13, 0.75), -6, -6, 0, 12, 12, 7, GREY)
    plane(ic, Iso(16, 23, 0.75), -9, -9, 0, 18, 18, "#DDE4EA", "#8794A0", 0.6)
    p = Iso(16, 23, 0.75).p
    line(ic, [p(-6, -6), p(6, -6), p(6, 6), p(-6, 6), p(-6, -6)], "#B04DE0", 1.6)
    arrow(ic, 25, 9, 25, 19, "#B04DE0", 1.4, 2.8)


@icon("smooth")
def _(ic):
    path(ic, "M4,24 C10,24 12,16 16,16")
    path(ic, "M16,16 C20,16 22,8 28,8", stroke=AMBER, sw=1.8)
    pt(ic, 16, 16)
    mark(ic, "G2", 23, 27, AMBER, 8)


@icon("autoconstrain")
def _(ic):
    poly(ic, [(6, 24), (16, 6), (26, 24)], "#4A9BE0", INK, 1.5, 'fill-opacity="0.15"')
    badge(ic, 23, 23, "plus", GREEN, 5)
    mark(ic, "=", 11, 17, AMBER, 8)


@icon("midline")
def _(ic):
    line(ic, [(5, 25), (27, 7)])
    pt(ic, 16, 16, AMBER, 3.6)
    pt(ic, 5, 25)
    pt(ic, 27, 7)


def _polygon(ic, r_ring, mark_side):
    hexagon = [(16 + 10 * math.cos(math.radians(30 + 60 * k)), 16 + 10 * math.sin(math.radians(30 + 60 * k)))
               for k in range(6)]
    poly(ic, hexagon, "#4A9BE0", INK, 1.5, 'fill-opacity="0.15"')
    ic.add(f'<circle cx="16" cy="16" r="{r_ring}" fill="none" stroke="{INK_DIM}" stroke-width="1" '
           'stroke-dasharray="1.6 1.4"/>')
    if mark_side:
        line(ic, [hexagon[1], hexagon[2]], AMBER, 2.0)


@icon("polygonc")
def _(ic):
    _polygon(ic, 8.66, False)


@icon("polygone")
def _(ic):
    _polygon(ic, 0.01, True)


@icon("sloto")
def _(ic):
    path(ic, "M10,11 H22 A5,5 0 0 1 22,21 H10 A5,5 0 0 1 10,11 Z", fill="#4A9BE0", extra='fill-opacity="0.18"')
    pt(ic, 5, 16, AMBER)
    pt(ic, 27, 16, AMBER)


@icon("slotc")
def _(ic):
    path(ic, "M10,11 H22 A5,5 0 0 1 22,21 H10 A5,5 0 0 1 10,11 Z", fill="#4A9BE0", extra='fill-opacity="0.18"')
    pt(ic, 16, 16, AMBER, 3.4)
    pt(ic, 22, 16)


@icon("hv")
def _(ic):
    line(ic, [(4, 26), (18, 26)], AMBER, 2.0)
    line(ic, [(24, 6), (24, 26)], AMBER, 2.0)
    mark(ic, "H", 10, 20, INK, 9)
    mark(ic, "V", 18, 12, INK, 9)


@icon("rectround")
def _(ic):
    ic.add(f'<rect x="5" y="8" width="22" height="16" rx="4" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" '
           'stroke-width="1.6"/>')
    pt(ic, 5, 24)
    pt(ic, 27, 8)


@icon("rectcround")
def _(ic):
    ic.add(f'<rect x="5" y="8" width="22" height="16" rx="4" fill="#4A9BE0" fill-opacity="0.18" stroke="{INK}" '
           'stroke-width="1.6"/>')
    pt(ic, 16, 16, AMBER)
    pt(ic, 27, 8)


@icon("spline-cp")
def _(ic):
    line(ic, [(4, 24), (10, 6), (22, 26), (28, 8)], INK_DIM, 1.0, "1.6 1.4")
    path(ic, "M4,24 C10,6 22,26 28,8")
    for x, y in ((4, 24), (10, 6), (22, 26), (28, 8)):
        ic.add(f'<rect x="{x - 2}" y="{y - 2}" width="4" height="4" fill="{POINT}"/>')


@icon("conic")
def _(ic):
    path(ic, "M5,26 Q16,0 27,26")
    pt(ic, 5, 26)
    pt(ic, 27, 26)


@icon("text")
def _(ic):
    mark(ic, "A", 16, 25, INK, 22)


@icon("blend")
def _(ic):
    line(ic, [(3, 22), (11, 22)])
    line(ic, [(21, 10), (29, 10)])
    path(ic, "M11,22 C16,22 16,10 21,10", stroke=AMBER, sw=1.8)


@icon("fx")
def _(ic):
    mark(ic, "fx", 16, 22, INK, 16)


@icon("configure")
def _(ic):
    box(ic, Iso(15, 15, 0.8), -6, -6, 0, 12, 12, 9, GREY)
    badge(ic, 24, 24, "plus", POINT, 5)


@icon("config-table")
def _(ic):
    ic.add(f'<rect x="5" y="6" width="22" height="20" rx="2" fill="none" stroke="{INK}" stroke-width="1.5"/>')
    for y in (12, 17, 22):
        line(ic, [(5, y), (27, y)], INK_DIM, 1.0)
    line(ic, [(13, 6), (13, 26)], INK_DIM, 1.0)


@icon("config-rules")
def _(ic):
    for y, c in ((9, "#4A9BE0"), (16, "#B04DE0"), (23, "#3DBB4F")):
        ic.add(f'<rect x="6" y="{y - 2.5}" width="20" height="5" rx="2.5" fill="{c}"/>')


@icon("section")
def _(ic):
    ic.add(f'<rect x="6" y="6" width="20" height="20" fill="#4A9BE0" fill-opacity="0.3" stroke="{INK}" stroke-width="1.4"/>')
    line(ic, [(16, 3), (16, 29)], AMBER, 1.8)


@icon("interference")
def _(ic):
    ic.add(f'<rect x="5" y="5" width="15" height="15" fill="none" stroke="{INK}" stroke-width="1.4"/>')
    ic.add(f'<rect x="12" y="12" width="15" height="15" fill="none" stroke="{INK}" stroke-width="1.4"/>')
    ic.add(f'<rect x="12" y="12" width="8" height="8" fill="{RED}"/>')


@icon("com")
def _(ic):
    ic.add(f'<circle cx="16" cy="16" r="10" fill="{INK}"/>')
    path(ic, "M16,6 A10,10 0 0 1 26,16 H16 Z M16,26 A10,10 0 0 1 6,16 H16 Z", fill="#222")


@icon("decal")
def _(ic):
    ic.add(f'<rect x="5" y="7" width="16" height="13" fill="#4A9BE0" stroke="{INK}" stroke-width="1.2"/>')
    box(ic, Iso(21, 22, 0.6), -5, -5, 0, 10, 10, 6, GREY)


@icon("canvas")
def _(ic):
    ic.add('<rect x="4" y="6" width="24" height="20" rx="2" fill="#2E7BD6"/>')
    poly(ic, [(4, 24), (13, 14), (20, 21), (24, 17), (28, 22), (28, 26), (4, 26)], "#9BD0FF")
    ic.add('<circle cx="22" cy="11" r="2.4" fill="#FFE27A"/>')


@icon("svg")
def _(ic):
    ic.add(f'<rect x="7" y="4" width="18" height="24" rx="2" fill="#F3F5F7"/>')
    mark(ic, "SVG", 16, 24, "#222", 7)
    arrow(ic, 4, 10, 14, 10, POINT, 1.8, 3)


@icon("dxf")
def _(ic):
    ic.add(f'<rect x="7" y="4" width="18" height="24" rx="2" fill="#F3F5F7"/>')
    mark(ic, "DXF", 16, 24, "#222", 7)
    arrow(ic, 4, 10, 14, 10, POINT, 1.8, 3)


@icon("insert-mesh")
def _(ic):
    poly(ic, [(16, 6), (27, 12), (27, 23), (16, 28), (5, 23), (5, 12)], "#C8A45A", "#7A5E2A", 1.0)
    line(ic, [(5, 12), (16, 18), (27, 12)], "#7A5E2A", 0.9)
    line(ic, [(16, 18), (16, 28)], "#7A5E2A", 0.9)
    arrow(ic, 6, 4, 14, 8, POINT, 1.8, 3)


@icon("insert-derive")
def _(ic):
    box(ic, Iso(18, 15, 0.8), -6, -6, 0, 12, 12, 9, GREY)
    arrow(ic, 4, 24, 12, 18, INK, 1.8, 3)


@icon("window-select")
def _(ic):
    ic.add(f'<rect x="4" y="4" width="20" height="18" fill="#4A9BE0" fill-opacity="0.2" stroke="{INK}" '
           'stroke-width="1.3" stroke-dasharray="2.4 1.8"/>')
    poly(ic, [(17, 15), (17, 29), (20.5, 25.5), (23, 30), (25, 29), (22.5, 24.5), (27, 24.5)], INK, "#222", 0.8)


@icon("block")
def _(ic):
    ic.add(f'<rect x="5" y="9" width="18" height="16" fill="none" stroke="{INK}" stroke-width="1.4"/>')
    for y in (14, 19):
        line(ic, [(5, y), (23, y)], INK_DIM, 1.0)
    badge(ic, 24, 9, "plus", GREEN, 4.5)


@icon("lookat")
def _(ic):
    ic.add('<rect x="5" y="9" width="22" height="15" rx="2" fill="#2E7BD6" stroke="#9BD0FF" stroke-width="1.2"/>')
    poly(ic, [(9, 9), (12, 4), (24, 4), (27, 9)], "#4A5560")
    line(ic, [(13, 28), (19, 28)], INK_DIM, 1.6)


def constraint(name, draw):
    ICONS[name] = draw


def _coincident(ic):
    line(ic, [(5, 22), (16, 15)])
    line(ic, [(27, 22), (16, 15)])
    ic.add(f'<circle cx="16" cy="15" r="3.6" fill="none" stroke="{AMBER}" stroke-width="1.6"/>')
    pt(ic, 16, 15, POINT, 3.2)


def _horizontal(ic):
    line(ic, [(5, 18), (27, 18)])
    pt(ic, 5, 18)
    pt(ic, 27, 18)
    mark(ic, "H", 16, 13)


def _vertical(ic):
    line(ic, [(13, 5), (13, 27)])
    pt(ic, 13, 5)
    pt(ic, 13, 27)
    mark(ic, "V", 22, 19)


def _parallel(ic):
    line(ic, [(5, 21), (19, 7)])
    line(ic, [(13, 27), (27, 13)])
    path(ic, "M20,22 l3,-3 M22.5,24.5 l3,-3", stroke=AMBER, sw=1.6)


def _perpendicular(ic):
    line(ic, [(6, 26), (26, 26)])
    line(ic, [(12, 26), (12, 5)])
    path(ic, "M12,20 h6 v6", stroke=AMBER, sw=1.6)


def _tangent(ic):
    ic.add(f'<circle cx="16" cy="18" r="7.5" fill="none" stroke="{INK}" stroke-width="1.6"/>')
    line(ic, [(4, 10.5), (28, 10.5)], AMBER, 1.8)


def _equal(ic):
    line(ic, [(5, 9), (19, 9)])
    line(ic, [(13, 23), (27, 23)])
    path(ic, "M11,15 h10 M11,18 h10", stroke=AMBER, sw=1.6)


def _midpoint(ic):
    line(ic, [(5, 25), (27, 7)])
    pt(ic, 5, 25)
    pt(ic, 27, 7)
    ic.add(f'<path d="M16,11 l4,5 -4,5 -4,-5 Z" fill="{AMBER}" stroke="#7A3E06" stroke-width="0.7"/>')


def _concentric(ic):
    ic.add(f'<circle cx="16" cy="16" r="10" fill="none" stroke="{INK}" stroke-width="1.6"/>')
    ic.add(f'<circle cx="16" cy="16" r="5" fill="none" stroke="{INK}" stroke-width="1.6"/>')
    ic.add(f'<circle cx="16" cy="16" r="1.8" fill="{AMBER}"/>')


def _collinear(ic):
    line(ic, [(4, 22), (12, 18)])
    line(ic, [(20, 14), (28, 10)])
    line(ic, [(12, 18), (20, 14)], AMBER, 1.6, "1.8 1.8")


def _symmetric(ic):
    line(ic, [(16, 3), (16, 29)], AMBER, 1.4, "2.2 1.8")
    pt(ic, 8, 16)
    pt(ic, 24, 16)
    path(ic, "M11,12 l-3,4 3,4 M21,12 l3,4 -3,4", stroke=INK, sw=1.4)


def _fix(ic):
    ic.add(f'<rect x="9" y="14" width="14" height="11" rx="2" fill="{AMBER}" stroke="#7A3E06" stroke-width="0.8"/>')
    path(ic, "M12,14 v-3 a4,4 0 0 1 8,0 v3", stroke=INK, sw=1.8)
    ic.add('<circle cx="16" cy="19.5" r="1.5" fill="#3A2105"/>')


def _dimension(ic):
    line(ic, [(5, 8), (5, 24)], INK_DIM, 1.2)
    line(ic, [(27, 8), (27, 24)], INK_DIM, 1.2)
    line(ic, [(7, 16), (25, 16)], POINT, 1.5)
    poly(ic, [(5.5, 16), (10, 13.4), (10, 18.6)], POINT)
    poly(ic, [(26.5, 16), (22, 13.4), (22, 18.6)], POINT)
    mark(ic, "1.5", 16, 12, INK, 7.5)


for n, f in [("coincident", _coincident), ("horizontal", _horizontal), ("vertical", _vertical),
             ("parallel", _parallel), ("perpendicular", _perpendicular), ("tangent", _tangent),
             ("equal", _equal), ("midpoint", _midpoint), ("concentric", _concentric),
             ("collinear", _collinear), ("symmetric", _symmetric), ("fix", _fix), ("dimension", _dimension)]:
    constraint(n, f)


# ---- Manufacture -----------------------------------------------------------------
def stock(ic, iso, c=GREY, h=7):
    return box(ic, iso, -8, -8, -4, 16, 16, h, c)


def toolpath(ic, d, color=GREEN, sw=1.3):
    path(ic, d, stroke=color, sw=sw)


@icon("setup")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    o = iso.p(-8, -8, 3)
    line(ic, [o, iso.p(4, -8, 3)], RED, 1.8)
    line(ic, [o, iso.p(-8, 4, 3)], "#3DBB4F", 1.8)
    line(ic, [o, (o[0], o[1] - 11)], "#3E7CF0", 1.8)


@icon("face")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    p = iso.p
    d = "M" + " L".join(f"{p(-7, y, 3)[0]:.1f},{p(-7, y, 3)[1]:.1f} {p(7, y, 3)[0]:.1f},{p(7, y, 3)[1]:.1f}"
                        if i % 2 == 0 else
                        f"{p(7, y, 3)[0]:.1f},{p(7, y, 3)[1]:.1f} {p(-7, y, 3)[0]:.1f},{p(-7, y, 3)[1]:.1f}"
                        for i, y in enumerate(range(-6, 8, 3)))
    toolpath(ic, d)


@icon("adaptive")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    cx, cy = iso.p(0, 0, 3)
    d = f"M{cx},{cy}" + "".join(
        f" L{cx + (1 + t * 0.5) * math.cos(t) * 1.2:.1f},{cy + (1 + t * 0.5) * math.sin(t) * 0.7:.1f}"
        for t in [i * 0.35 for i in range(1, 34)])
    toolpath(ic, d)


@icon("contour")
def _(ic):
    iso = Iso(16, 19, 0.95)
    box(ic, iso, -5, -5, -4, 10, 10, 7, BLUE)
    p = iso.p
    q = [p(-7, -7, -1), p(7, -7, -1), p(7, 7, -1), p(-7, 7, -1), p(-7, -7, -1)]
    line(ic, q, GREEN, 1.4)


@icon("pocket")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    p = iso.p
    poly(ic, [p(-5, -5, 3), p(5, -5, 3), p(5, 5, 3), p(-5, 5, 3)], "#3A4550", GREY["line"], 0.7)
    line(ic, [p(-3, -3, 3), p(3, -3, 3), p(3, 3, 3), p(-3, 3, 3), p(-3, -1, 3), p(1, -1, 3), p(1, 1, 3)],
         GREEN, 1.2)


@icon("slot")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    p = iso.p
    a, b = p(-5, 0, 3), p(5, 0, 3)
    line(ic, [a, b], "#1D2329", 4.2)
    line(ic, [a, b], GREEN, 1.3)


@icon("engrave")
def _(ic):
    iso = Iso(16, 19, 0.95)
    stock(ic, iso)
    p = iso.p
    line(ic, [p(-5, 4, 3), p(-2, -4, 3), p(1, 4, 3), p(4, -4, 3), p(6, 2, 3)], GREEN, 1.4)


@icon("cam-parallel")
def _(ic):
    path(ic, "M4,24 C10,14 22,14 28,24 Z", fill=GREY["left"], stroke=GREY["line"], sw=0.8)
    for i in range(5):
        x = 8 + i * 4
        path(ic, f"M{x},{24 - (3 if i in (0, 4) else 6)} v{3 if i in (0, 4) else 6}", stroke=GREEN, sw=1.2)


@icon("scallop")
def _(ic):
    path(ic, "M4,25 C8,12 24,12 28,25 Z", fill=GREY["left"], stroke=GREY["line"], sw=0.8)
    for r in (3, 6, 9):
        path(ic, f"M{16 - r},24 A{r},{r * 0.75} 0 0 1 {16 + r},24", stroke=GREEN, sw=1.2)


@icon("drill")
def _(ic):
    iso = Iso(16, 21, 0.9)
    stock(ic, iso, GREY, 5)
    cx, cy = iso.p(0, 0, 1)
    ic.add(f'<ellipse cx="{cx}" cy="{cy}" rx="3" ry="1.7" fill="#1D2329"/>')
    poly(ic, [(cx - 2, 3), (cx + 2, 3), (cx + 2, cy - 5), (cx, cy - 2), (cx - 2, cy - 5)], "#C9D3DC", "#56616C", 0.8)
    path(ic, f"M{cx - 2},7 l4,3 M{cx - 2},11 l4,3", stroke="#56616C", sw=0.9)


@icon("simulate")
def _(ic):
    box(ic, Iso(14, 19, 0.9), -7, -7, -4, 14, 14, 8, GREY)
    badge(ic, 23, 22, "play", "#3E7CF0", 6)


@icon("post")
def _(ic):
    poly(ic, [(7, 4), (20, 4), (25, 9), (25, 28), (7, 28)], "#F3F5F7", "#56616C", 0.9)
    poly(ic, [(20, 4), (20, 9), (25, 9)], "#CDD3DA", "#56616C", 0.8)
    for i, w in enumerate((10, 13, 8, 12)):
        line(ic, [(10, 13 + i * 3.5), (10 + w, 13 + i * 3.5)], "#4A9BE0" if i == 0 else "#7D8A96", 1.4)
    badge(ic, 24, 24, "plus", GREEN, 4.4)


@icon("library")
def _(ic):
    for i, x in enumerate((8, 16, 24)):
        ic.add(f'<rect x="{x - 2.5}" y="4" width="5" height="10" rx="0.8" fill="#C9D3DC" stroke="#56616C" '
               f'stroke-width="0.7"/>')
        poly(ic, [(x - 2.2, 14), (x + 2.2, 14), (x + 2.2, 24 - i), (x, 27 - i), (x - 2.2, 24 - i)],
             ["#F2C94C", "#4A9BE0", "#9B6BE0"][i], "#30363C", 0.7)


# ---- Browser ---------------------------------------------------------------------
@icon("folder")
def _(ic):
    path(ic, "M3,9 h9 l2,2.5 h15 v15 h-26 Z", fill="#F2C94C", stroke="#8A6A10", sw=0.9)
    path(ic, "M3,13 h26 v13.5 h-26 Z", fill="#F7D978", stroke="#8A6A10", sw=0.9)


@icon("body")
def _(ic):
    box(ic, Iso(16, 17, 1.0), -6, -6, -3, 12, 12, 10, GREY)


@icon("component")
def _(ic):
    iso = Iso(16, 17, 1.0)
    box(ic, iso, -6, -6, -3, 12, 12, 10, dict(GREY, top="#FFFFFF"))
    p = iso.p
    line(ic, [p(-6, 6, 7), p(-6, -6, 7), p(6, -6, 7)], "#3E7CF0", 1.4)


@icon("origin")
def _(ic):
    o = (12, 20)
    line(ic, [o, (27, 20)], RED, 1.8)
    line(ic, [o, (12, 5)], "#3E7CF0", 1.8)
    line(ic, [o, (4, 27)], "#3DBB4F", 1.8)
    ic.add('<circle cx="12" cy="20" r="2.2" fill="#F3F5F7" stroke="#56616C" stroke-width="0.8"/>')


@icon("settings")
def _(ic):
    ic.add('<g fill="#C9D3DC" stroke="#56616C" stroke-width="0.8">' + "".join(
        f'<rect x="14" y="3" width="4" height="7" rx="1" transform="rotate({a} 16 16)"/>' for a in range(0, 360, 45))
        + '<circle cx="16" cy="16" r="8"/></g><circle cx="16" cy="16" r="3" fill="#2B3138"/>')


@icon("units")
def _(ic):
    ic.add('<g transform="rotate(-35 16 16)"><rect x="3" y="12" width="26" height="8" rx="1" fill="#F2C94C" '
           'stroke="#8A6A10" stroke-width="0.8"/>'
           + "".join(f'<path d="M{5 + i * 3},12v{3 if i % 2 else 5}" stroke="#5A4508" stroke-width="0.9"/>'
                     for i in range(8)) + "</g>")


# The light theme gets the same art with dark line work instead of light.
LIGHT_SWAPS = {INK: "#2B3138", INK_DIM: "#6B7480"}


def main():
    os.makedirs(OUT, exist_ok=True)
    light_out = OUT + "-light"
    os.makedirs(light_out, exist_ok=True)
    for name, fn in ICONS.items():
        ic = Icon(name)
        fn(ic)
        ic.save()
        with open(os.path.join(OUT, name + ".svg")) as f:
            svg = f.read()
        for a, b in LIGHT_SWAPS.items():
            svg = svg.replace(a, b)
        with open(os.path.join(light_out, name + ".svg"), "w") as f:
            f.write(svg)
    print(f"wrote {len(ICONS)} icons (dark and light) to {os.path.normpath(OUT)}[-light]")


if __name__ == "__main__":
    main()
