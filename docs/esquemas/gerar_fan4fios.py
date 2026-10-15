# -*- coding: utf-8 -*-
"""Gera 05_fan_4fios_pwm.svg — fans de PC de 4 fios: 12 V fixo + PWM de 25 kHz no fio 4 (GPIO4/5/6), sem IRF520.
Uso: python gerar_fan4fios.py  (PNG: converter.sh)"""
import io, os
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
C_12V, C_SIG, C_GND = "#ea580c", "#2563eb", "#111827"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=15, fill=INK, gap=23):
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill)


def rect(x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


def wire(pts, color, sw=5, dash=None, hops=None):
    hops = hops or []
    d = f"M{pts[0][0]},{pts[0][1]}"
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        if y1 == y2:
            xs = sorted([hx for hx, hy in hops if hy == y1 and min(x1, x2) < hx < max(x1, x2)], reverse=(x2 < x1))
            for hx in xs:
                r = 9
                sg = 1 if x2 > x1 else -1
                d += f" L{hx - sg * r},{y1} A{r},{r} 0 0 {1 if sg > 0 else 0} {hx + sg * r},{y1}"
        d += f" L{x2},{y2}"
    da = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{da}/>')


def pin(x, y): add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')
def dot(x, y, c): add(f'<circle cx="{x}" cy="{y}" r="7" fill="{c}" stroke="#fff" stroke-width="2"/>')


W, H = 2000, 1240
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 66, "Fans de PC de 4 fios — 12 V fixo + PWM de 25 kHz no fio 4 (sem módulo IRF520)", 36, INK, "700")
text(60, 102, "A fonte alimenta o fan direto (12 V sempre ligado). A velocidade vem do sinal PWM do ESP32 no fio 4. Cada fan usa um GPIO próprio; os GNDs são comuns.", 18, MUTE)

# ESP32
rect(60, 150, 340, 520, "#e2e8f0", "#334155", 3, 14)
text(230, 172, "ESP32-S3", 30, INK, "700", "middle")
text(230, 194, "YD-ESP32-S3 44P · N16R8", 15, MUTE, "400", "middle")
FY = [210 + i * 170 for i in range(3)]
for i, y in enumerate(FY):
    pin(400, y)
    text(380, y + 6, f"GPIO{4 + i}", 19, INK, "700", "end")
    text(380, y + 28, f"PWM 25 kHz · fan {i + 1}", 14, MUTE, "400", "end")
pin(400, 620)
text(380, 626, "GND", 19, INK, "700", "end")

# fans
FX0, FX1 = 1300, 1640
for i in range(3):
    y0 = 150 + i * 170
    rect(FX0, y0, FX1 - FX0, 150, "#e2e8f0", "#334155", 3, 14)
    text((FX0 + FX1) // 2, y0 + 30, f"Fan {i + 1} (PC, 4 fios)", 22, INK, "700", "middle")
    pin(FX0, y0 + 60)
    text(FX0 + 16, y0 + 66, "4 · PWM", 16, "#1d4ed8", "700")
    pin(FX0, y0 + 100)
    text(FX0 + 16, y0 + 106, "3 · tacômetro (livre)", 15, MUTE, "400")
    pin(FX1, y0 + 60)
    text(FX1 - 16, y0 + 66, "2 · +12 V", 16, "#c2410c", "700", "end")
    pin(FX1, y0 + 100)
    text(FX1 - 16, y0 + 106, "1 · GND", 16, C_GND, "700", "end")

# PWM: GPIO -> resistor 1k -> pino 4
for i, y in enumerate(FY):
    wire([(400, y), (FX0, y)], C_SIG, 5)
    rect(760, y - 13, 110, 26, "#fff", "#7c3aed", 2.5, 4)
    text(815, y + 5, "1 kΩ", 15, "#4c1d95", "700", "middle")
    text(1100, y - 14, "sinal PWM 3,3 V", 15, C_SIG, "700", "middle")

# barramentos 12 V (x=1740) e GND (x=1800)
BX12, BXG = 1740, 1800
SRC_Y = 770
y12 = [150 + i * 170 + 60 for i in range(3)]
yg = [150 + i * 170 + 100 for i in range(3)]
wire([(BX12, y12[0]), (BX12, SRC_Y)], C_12V, 6)
wire([(BXG, yg[0]), (BXG, SRC_Y)], C_GND, 6)
for y in y12:
    wire([(FX1, y), (BX12, y)], C_12V, 5)
    dot(BX12, y, C_12V)
for y in yg:
    wire([(FX1, y), (BXG, y)], C_GND, 5, hops=[(BX12, y)])
    dot(BXG, y, C_GND)
# GND comum do ESP32 -> barramento GND
wire([(400, 620), (520, 620), (520, 705), (BXG, 705)], C_GND, 5, hops=[(BX12, 705)])
dot(BXG, 705, C_GND)
text(900, 692, "GND comum (ESP32 ↔ fonte)", 16, C_GND, "700", "middle")

# fonte
rect(1560, SRC_Y, 340, 95, "#fff7ed", "#ea580c", 3, 14)
text(1730, SRC_Y + 36, "Fonte 12 V DC", 22, INK, "700", "middle")
text(1730, SRC_Y + 60, "a mesma que já alimenta os fans/cooler", 14, MUTE, "400", "middle")
pin(BX12, SRC_Y)
text(BX12 - 14, SRC_Y + 84, "+ 12 V", 16, "#c2410c", "700", "end")
pin(BXG, SRC_Y)
text(BXG + 14, SRC_Y + 84, "− GND", 16, C_GND, "700")

# legenda
rect(60, 735, 640, 50, "#fff", "#cbd5e1", 1.5, 10)
for k, (c, t) in enumerate([(C_SIG, "sinal PWM (fio 4)"), (C_12V, "+12 V (fio 2)"), (C_GND, "GND (fio 1)")]):
    xx = 80 + k * 210
    add(f'<line x1="{xx}" y1="760" x2="{xx + 30}" y2="760" stroke="{c}" stroke-width="6" stroke-linecap="round"/>')
    text(xx + 38, 766, t, 15, INK)
text(60, 822, "Pinagem do conector de fan de 4 fios (padrão Intel): 1 = GND · 2 = +12 V · 3 = tacômetro · 4 = PWM.", 17, INK, "700")
text(60, 848, "Na foto do usuário: o fio com a listra / marca \"1\" é o pino 1 (GND); o seguinte é o +12 V.", 16, MUTE)

PY, PH = 880, 330
panels = [
    (60, 460, "#fff7ed", "#ea580c", "#9a3412", "#7c2d12", "1 · Montagem (tudo desligado)", [
        "1) Fonte 12 V desligada.",
        "2) + 12 V → pino 2 de cada fan.",
        "3) − da fonte → pino 1 de cada fan.",
        "4) GPIO4 → 1 kΩ → pino 4 do fan 1;",
        "   GPIO5 → fan 2 ; GPIO6 → fan 3.",
        "5) GND do ESP32 no − da fonte.",
        "6) Pino 3 (tacômetro): sem ligar.",
        "7) Os módulos IRF520 saem do circuito."]),
    (540, 460, "#eff6ff", "#2563eb", "#1e3a8a", "#1e3a8a", "2 · Por que mudou", [
        "· Fan de 4 fios já tem o próprio driver:",
        "  quer 12 V sempre presente.",
        "· Chavear o 12 V a 1 kHz (IRF520)",
        "  faz o fan zumbir, falhar ou nem partir.",
        "· A velocidade vai no fio 4: PWM de 25 kHz",
        "  (3,3 V do ESP32 funciona).",
        "· Sem sinal no fio 4 o fan gira a 100 %.",
        "· 1 kΩ em série protege o GPIO."]),
    (1020, 460, "#ecfdf5", "#16a34a", "#166534", "#14532d", "3 · Teste", [
        "1) Ligue a fonte: os fans giram a 100 %",
        "   (ainda sem PWM do ESP32).",
        "2) Grave o firmware (Fase 104).",
        "3) Config. > Fans: ligue a chave e",
        "   ajuste cada fan (0–100 %).",
        "4) 0 % = mínimo do fan; muitos mantêm",
        "   20–30 %, alguns param.",
        "5) Fan mudo? Teste pino 2 (+) e 1 (−)."]),
    (1500, 440, "#faf5ff", "#7c3aed", "#5b21b6", "#4c1d95", "4 · Notas", [
        "· Liga/desliga geral (opcional): passe o",
        "  +12 V pelo contato NA do relé CH8 Fans.",
        "· Fan de 3 fios (sem fio PWM): use o",
        "  IRF520 como no esquema 04.",
        "· Tacômetro: sobra o GPIO17 (1 fan);",
        "  GPIO15/16 estão no sensor de CO₂.",
        "· Todos os fans na MESMA fonte e GND."]),
]
for x, w, bg, br, ct, cb, title, arr in panels:
    rect(x, PY, w, PH, bg, br, 2, 12)
    text(x + 20, PY + 36, title, 21, ct, "700")
    lines(x + 20, PY + 70, arr, 15, cb, 24)
add("</svg>")
io.open(os.path.join(OUT, "05_fan_4fios_pwm.svg"), "w", encoding="utf-8").write("\n".join(P))
print("ok")
