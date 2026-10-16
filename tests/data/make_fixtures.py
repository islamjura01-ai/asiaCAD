#!/usr/bin/env python3
"""DXF import testlari uchun fixture fayllarni yaratadi (ezdxf >= 1.1).

    pip install ezdxf
    python3 tests/data/make_fixtures.py

Natija shu papkaga yoziladi va repozitoriyga qo'shiladi — testlar ezdxf'siz ishlaydi.
Kutilgan qiymatlar tests/test_main.cpp dagi dxf_* testlarida.
"""
import math
import os

import ezdxf
from ezdxf.enums import TextEntityAlignment

HERE = os.path.dirname(os.path.abspath(__file__))


def r2018_mix():
    doc = ezdxf.new("R2018", setup=True)
    doc.header["$INSUNITS"] = 6  # metr
    doc.layers.add("YOL", color=3)
    lay = doc.layers.add("Чегара", color=1)
    lay.lock()
    off = doc.layers.add("OFF", color=4)
    off.off()
    msp = doc.modelspace()

    msp.add_line((0, 0), (10, 0), dxfattribs={"layer": "YOL"})
    # Kvadrat 10x10, yuqori qirrasi yarim aylana (bulge=1): maydon 100 + 25π/2
    msp.add_lwpolyline([(0, 0, 0, 0, 0), (10, 0, 0, 0, 0), (10, 10, 0, 0, 1), (0, 10, 0, 0, 0)],
                       format="xyseb", close=True, dxfattribs={"layer": "Чегара"})
    msp.add_circle((50, 50), 5, dxfattribs={"color": 2})
    # Teskari extrusion (AutoCAD'da MIRROR natijasi): OCS (20,0) r5 0..90 -> WCS markaz (-20,0), 90..180
    msp.add_arc((20, 0), 5, 0, 90, dxfattribs={"extrusion": (0, 0, -1)})
    msp.add_text("Тошкент", height=2.5).set_placement((5, 5), align=TextEntityAlignment.MIDDLE_CENTER)
    msp.add_mtext("Birinchi\\PIkkinchi {\\fArial|b1;qalin} %%d",
                  dxfattribs={"insert": (0, 100), "char_height": 2, "attachment_point": 1})
    msp.add_ellipse((100, 0), major_axis=(10, 0), ratio=0.5)
    msp.add_open_spline([(0, 0), (10, 10), (20, 0), (30, 10)], degree=3)
    msp.add_3dface([(0, 0, 1), (5, 0, 2), (0, 5, 3)])

    tree = doc.blocks.new("TREE", base_point=(1, 1))
    tree.add_circle((1, 1), 1, dxfattribs={"color": 0})  # BYBLOCK
    tree.add_line((1, 1), (1, 3))
    msp.add_blockref("TREE", (200, 0), dxfattribs={"xscale": 2, "yscale": 2, "rotation": 90, "layer": "YOL", "color": 5})
    m = msp.add_blockref("TREE", (400, 0), dxfattribs={"layer": "YOL"})
    m.grid(size=(2, 3), spacing=(10, 20))  # MINSERT: 2 qator x 3 ustun
    msp.add_blockref("TREE", (500, 0), dxfattribs={"xscale": 1, "yscale": 3})  # notekis masshtab

    forest = doc.blocks.new("FOREST")
    forest.add_blockref("TREE", (0, 0))
    forest.add_blockref("TREE", (10, 0))
    msp.add_blockref("FOREST", (300, 0))

    pt = doc.blocks.new("PT")
    pt.add_point((0, 0))
    pt.add_attdef("NUM", (0.5, 0.5), dxfattribs={"height": 1})
    ref = msp.add_blockref("PT", (600, 0))
    ref.add_auto_attribs({"NUM": "17"})

    dim = msp.add_linear_dim(base=(0, -5), p1=(0, 0), p2=(10, 0))
    dim.render()

    hatch = msp.add_hatch()
    hatch.paths.add_polyline_path([(0, 0), (1, 0), (1, 1)])
    doc.paperspace().add_line((0, 0), (1, 1))
    doc.saveas(os.path.join(HERE, "r2018_mix.dxf"))


def r12_cp1251():
    doc = ezdxf.new("R12")
    doc.encoding = "cp1251"  # $DWGCODEPAGE = ANSI_1251
    doc.layers.add("Слой1", color=6)
    msp = doc.modelspace()
    # "Қўғ" cp1251'da yo'q -> ezdxf \U+XXXX bilan yozadi
    msp.add_text("Привет, Ташкент Қўғ", height=3, dxfattribs={"layer": "Слой1", "insert": (1, 2)})
    p = msp.add_polyline2d([(0, 0), (10, 0), (10, 10)], dxfattribs={"layer": "Слой1"})
    p.vertices[1].dxf.bulge = -1  # (10,0)->(10,10) CW yarim aylana
    msp.add_polyline3d([(0, 0, 0), (3, 4, 12)])
    doc.saveas(os.path.join(HERE, "r12_cp1251.dxf"))


def hatches():
    doc = ezdxf.new("R2018")
    msp = doc.modelspace()
    # 1) Poliliniya yo'llari: 10x10 kvadrat, ichida 4x4 teshik -> maydon 84
    h = msp.add_hatch(color=1, dxfattribs={"layer": "SHTRIX"})
    h.set_pattern_fill("ANSI31", scale=2.0, angle=45)
    h.paths.add_polyline_path([(0, 0), (10, 0), (10, 10), (0, 10)], is_closed=True, flags=1)
    h.paths.add_polyline_path([(3, 3), (7, 3), (7, 7), (3, 7)], is_closed=True, flags=16)
    # 2) Qirralar, CCW yoy: yarim doira r=5 -> 12.5π
    h = msp.add_hatch(color=2)
    e = h.paths.add_edge_path()
    e.add_line((20, 0), (30, 0))
    e.add_arc((25, 0), 5, 0, 180, ccw=True)
    # 3) Qirralar, CW yoy: o'sha yarim doira teskari aylanib -> 12.5π
    h = msp.add_hatch(color=3)
    e = h.paths.add_edge_path()
    e.add_arc((45, 0), 5, 180, 0, ccw=False)
    e.add_line((50, 0), (40, 0))
    # 4) Ellips qirrasi: a=4, b=2 -> 8π
    h = msp.add_hatch(color=4)
    e = h.paths.add_edge_path()
    e.add_ellipse((60, 0), major_axis=(4, 0), ratio=0.5, start_angle=0, end_angle=360, ccw=True)
    # 5) Bulge'li poliliniya yo'li: (70,0)-(80,0) yarim aylana pastga + to'g'ri chiziq -> 12.5π
    h = msp.add_hatch(color=5)
    h.paths.add_polyline_path([(70, 0, 1), (80, 0, 0)], is_closed=True)
    doc.saveas(os.path.join(HERE, "hatch.dxf"))


def headerless_utf8():
    # Ko'p GIS/skript eksportchilari faqat ENTITIES yozadi
    text = "\n".join([
        "0", "SECTION", "2", "ENTITIES",
        "0", "LINE", "8", "Qatlam-ʻ", "10", "1", "20", "2", "30", "0", "11", "4", "21", "6", "31", "0",
        "0", "TEXT", "8", "0", "10", "0", "20", "0", "40", "1", "1", "Oʻzbekiston %%c50",
        "0", "ENDSEC", "0", "EOF", ""])
    with open(os.path.join(HERE, "headerless_utf8.dxf"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)


if __name__ == "__main__":
    r2018_mix()
    r12_cp1251()
    hatches()
    headerless_utf8()
    print("ok")
