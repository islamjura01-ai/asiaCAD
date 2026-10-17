# AsiaCAD — sinov uchun "bitta faylda KJ loyiha" (haqiqiy 1-43B-9 КЖ fayl tuzilishi bo'yicha):
#  * 5 ta ko'rinish model fazosida yonma-yon, mm da, M200 o'lcham uslubi (DIMLFAC = 2 — chizmada 3000, haqiqatda 6000)
#  * o'q pufakchalari (aylana + "1","2","3","А","Б"), sarlavhalar, "%%C16AIII, ш.200", "d 20"/"d 16" qatlamlari
#  * $INSUNITS = 6 (m), lekin chizma mm da — AsiaCAD matn balandligidan aniqlaydi
# Ishga tushirish: python3 make_kj.py  ->  kj_loyiha.dxf
import ezdxf, os
doc = ezdxf.new('R2000', setup=True)
doc.header['$INSUNITS'] = 6
ds = doc.dimstyles.new('M200')
ds.dxf.dimlfac = 2.0
ds.dxf.dimtxt = 300
ds.dxf.dimasz = 150
for n in ['ОСИ', 'СТЕНЫ', 'размеры1', 'd 20', 'd 16', 'надписи', 'Саша_Тонкая']:
    doc.layers.add(n)
msp = doc.modelspace()
AX = [('1', 0), ('2', 3000), ('3', 6000)]
AY = [('А', 0), ('Б', 3000)]

def view(ox, title):
    for n, x in AX:
        for y in (-2000, 5000):
            msp.add_circle((ox + x, y), 400, dxfattribs={'layer': 'ОСИ'})
            msp.add_text(n, height=500, dxfattribs={'layer': 'надписи'}).set_placement((ox + x, y), align=ezdxf.enums.TextEntityAlignment.MIDDLE_CENTER)
        msp.add_line((ox + x, -1600), (ox + x, 4600), dxfattribs={'layer': 'ОСИ'})
    for n, y in AY:
        for x in (-2000, 8000):
            msp.add_circle((ox + x, y), 400, dxfattribs={'layer': 'ОСИ'})
            msp.add_text(n, height=500, dxfattribs={'layer': 'надписи'}).set_placement((ox + x, y), align=ezdxf.enums.TextEntityAlignment.MIDDLE_CENTER)
    msp.add_text(title, height=500, dxfattribs={'layer': 'Саша_Тонкая'}).set_placement((ox - 1500, 6500))
    d = msp.add_linear_dim(base=(ox, -1000), p1=(ox, 0), p2=(ox + 3000, 0), dimstyle='M200', dxfattribs={'layer': 'размеры1'})
    d.render()

def outline(ox):
    msp.add_lwpolyline([(ox - 500, -500), (ox + 6500, -500), (ox + 6500, 3500), (ox - 500, 3500)], close=True, dxfattribs={'layer': 'размеры1'})

def columns(ox, layer='СТЕНЫ'):
    for _, x in AX:
        for _, y in AY:
            msp.add_lwpolyline([(ox + x - 125, y - 125), (ox + x + 125, y - 125), (ox + x + 125, y + 125), (ox + x - 125, y + 125)], close=True,
                               dxfattribs={'layer': layer})

S = 60000
view(0, 'Опалубочный план и основное армирование фундаментной плиты в верхней и нижней зонах')
outline(0)
columns(0)
msp.add_text('%%C16AIII, ш.200', height=300).set_placement((1000, 1500))
msp.add_text('бетон кл. В30', height=300).set_placement((1000, -1200))

view(S, 'Схема расположения выпусков фундаментной плиты')
columns(S)
for _, x in AX:
    for _, y in AY:
        for dx in (-75, 75):
            for dy in (-75, 75):
                msp.add_circle((S + x + dx, y + dy), 10, dxfattribs={'layer': 'd 20'})
msp.add_text('4%%C20AIII', height=300).set_placement((S + 1000, 1500))

view(2 * S, 'Схема расположения колонн и монолитных стен на отм. ±0.000')
columns(2 * S, 'размеры1')
msp.add_lwpolyline([(2 * S + 6400, 500), (2 * S + 6600, 500), (2 * S + 6600, 2500), (2 * S + 6400, 2500)], close=True, dxfattribs={'layer': 'размеры1'})
for t, y in (('+3.000', 4000), ('+3.000', 4400), ('+2.800', 3700), ('+2.800', 4100)):
    msp.add_text(t, height=300).set_placement((2 * S + 7000, y))
msp.add_text('100/200', height=300).set_placement((2 * S + 7000, -1500))

view(3 * S, 'Опалубочный план и схема основного армирования плиты перекрытия')
outline(3 * S)
columns(3 * S)
msp.add_text('%%C12AIII, ш.200', height=300).set_placement((3 * S + 1000, 1500))

view(4 * S, 'План дополнительного армирования плиты перекрытия в верхней зоне')
columns(4 * S)
for _, x in AX:
    for _, y in AY:
        cx, cy = 4 * S + x, y
        msp.add_lwpolyline([(cx - 500, cy - 500), (cx + 500, cy - 500), (cx + 500, cy + 500), (cx - 500, cy + 500)], close=True, dxfattribs={'layer': 'СТЕНЫ'})
        msp.add_line((cx - 500, cy + 100), (cx + 500, cy + 100), dxfattribs={'layer': 'd 16'})
        msp.add_line((cx + 100, cy - 500), (cx + 100, cy + 500), dxfattribs={'layer': 'd 16'})
msp.add_text('%%C16AIII', height=300).set_placement((4 * S + 1500, 1500))
msp.add_text('ш.200х200', height=300).set_placement((4 * S + 1500, 1100))

doc.saveas(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kj_loyiha.dxf'))
print('ok')
