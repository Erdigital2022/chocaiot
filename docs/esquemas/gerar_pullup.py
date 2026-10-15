# -*- coding: utf-8 -*-
"""Gera 03_pullup_rele.svg — pull-up de 10 kΩ (rede SIP) entre cada GPIO de relé e o 3V3, no módulo de relé de 8 canais.
Uso:  python gerar_pullup.py   (depois converter para PNG: ver converter.sh)"""
import io, os

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
C_3V3, C_SIG, C_GND = "#ea580c", "#2563eb", "#111827"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=17, fill=INK, gap=None, weight="400"):
    gap = gap or size + 8
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill, weight)


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
                r = 9; sg = 1 if x2 > x1 else -1
                d += f" L{hx - sg * r},{y1} A{r},{r} 0 0 {1 if sg > 0 else 0} {hx + sg * r},{y1}"
        d += f" L{x2},{y2}"
    da = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{da}/>')


def dot(x, y, color, r=7): add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}" stroke="#fff" stroke-width="2"/>')
def pin(x, y): add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')


W, H = 2000, 1330
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 66, "Pull-up de 10 kΩ nos relés — evita que a carga ligue sozinha durante a gravação e o reinício", 36, INK, "700")
text(60, 102, "Um resistor de cada GPIO do relé até o 3V3 do ESP32 mantém a entrada em nível ALTO (relé desligado) quando o pino está \"solto\". Nunca ligue ao 5 V.", 18, MUTE)

ROWS = [("GPIO1", "IN1", "CH1  Resistência  220 V"), ("GPIO2", "IN2", "CH2  Umidificador  5 V"), ("GPIO38", "IN3", "CH3  Bomba de água  220 V"),
        ("GPIO39", "IN4", "CH4  Bomba de ar  3,7 V"), ("GPIO40", "IN5", "CH5  LED ovoscópio  220 V"), ("GPIO41", "IN6", "CH6  Áudio"),
        ("GPIO42", "IN7", "CH7  Cooler do sistema  12 V"), ("GPIO7", "IN8", "CH8  Fans  12 V")]
Y0, DY = 300, 78

# ESP32
rect(60, 150, 400, 780, "#e2e8f0", "#334155", 3, 14)
text(260, 192, "ESP32-S3", 30, INK, "700", "middle")
text(260, 220, "YD-ESP32-S3 44P · N16R8", 17, MUTE, "400", "middle")
pin(460, 240); text(440, 246, "3V3", 19, C_3V3, "700", "end")
for i, (g, _, _) in enumerate(ROWS):
    y = Y0 + i * DY
    pin(460, y); text(440, y + 6, g, 18, INK, "700", "end")
text(260, 905, "(durante a gravação os GPIOs ficam soltos)", 15, MUTE, "400", "middle")

# módulo de relé
RX = 1500
rect(RX, 150, 440, 780, "#e2e8f0", "#334155", 3, 14)
text(RX + 220, 192, "Módulo de relé 8 canais", 26, INK, "700", "middle")
text(RX + 220, 220, "ativo em nível BAIXO (LOW liga o relé)", 16, MUTE, "400", "middle")
for i, (_, inn, lab) in enumerate(ROWS):
    y = Y0 + i * DY
    pin(RX, y); text(RX + 20, y + 6, f"{inn} · {lab}", 16, INK, "600")

# trilho 3V3 + SIP
BUS = 1060
wire([(460, 240), (BUS, 240), (BUS, Y0 + 7 * DY + 34)], C_3V3, 6)
dot(BUS, 240, C_3V3)
rect(790, 170, 330, Y0 + 7 * DY + 70 - 170, "none", "#7c3aed", 3, 16, "12 8")
text(955, 200, "Rede SIP 10 kΩ · 9 pinos", 21, "#6d28d9", "700", "middle")
text(955, 225, "pino 1 (marcado) = COMUM → 3V3", 15, "#6d28d9", "600", "middle")

JX = 720
for i in range(8):
    y = Y0 + i * DY
    hops = [(BUS, y)]
    wire([(460, y), (RX, y)], C_SIG, 5, hops=hops)
    dot(JX, y, C_SIG, 7)
    wire([(JX, y), (JX, y + 34), (840, y + 34)], C_SIG, 4)
    rect(840, y + 22, 100, 24, "#fff", "#7c3aed", 2.5, 4)
    text(890, y + 40, "10 kΩ", 15, "#4c1d95", "700", "middle")
    wire([(940, y + 34), (BUS, y + 34)], C_SIG, 4)
    dot(BUS, y + 34, C_3V3, 6)
text(JX, 262, "derivação (solda ou", 14, MUTE, "400", "middle"); text(JX, 280, "conector dupont)", 14, MUTE, "400", "middle")

# legenda de fios
rect(60, 960, 380, 130, "#fff", "#cbd5e1", 1.5, 10)
text(76, 990, "Fios", 17, INK, "700")
for k, (c, t) in enumerate([(C_3V3, "3V3 do ESP32 (NUNCA 5 V)"), (C_SIG, "sinal GPIO → IN do relé"), ("#7c3aed", "rede de resistores SIP")]):
    yy = 1018 + k * 24
    add(f'<line x1="76" y1="{yy-5}" x2="124" y2="{yy-5}" stroke="{c}" stroke-width="6" stroke-linecap="round"/>')
    text(136, yy, t, 15, INK)

# painel 1 — por quê
rect(480, 960, 480, 330, "#ecfdf5", "#16a34a", 2, 12)
text(500, 996, "Por que isso resolve", 21, "#166534", "700")
lines(500, 1028, ["· Relé ativo em LOW: um pino SOLTO pode",
                  "  ser lido como \"ligar\" (visto na bomba).",
                  "· Na gravação/reinício o ESP32 deixa os",
                  "  GPIOs soltos antes do firmware rodar:",
                  "  a bomba (CH3) e a resistência (CH1)",
                  "  podem ligar por 1–45 s.",
                  "· O resistor puxa o pino a 3,3 V (HIGH =",
                  "  desligado). Quando o firmware assume,",
                  "  ele manda LOW/HIGH normalmente (só",
                  "  0,33 mA passam pelo resistor)."], 15, "#14532d", 23)

# painel 2 — montagem
rect(980, 960, 500, 330, "#fff7ed", "#ea580c", 2, 12)
text(1000, 996, "Montagem (tudo desligado e desplugado)", 21, "#9a3412", "700")
lines(1000, 1028, ["1) Achar o pino COMUM da SIP: é o do ponto/",
                   "   traço no corpo (pino 1). Código \"103\" = 10 kΩ.",
                   "2) COMUM → pino 3V3 do ESP32 (o mesmo do",
                   "   sensor; NÃO use o 5 V do módulo).",
                   "3) Cada um dos outros 8 pinos → no fio que",
                   "   vai do GPIO ao IN do relé (IN1…IN8 na",
                   "   ordem; pode ser no conector do módulo).",
                   "4) Sem SIP: 8 resistores de 10 kΩ (ou 4,7 kΩ),",
                   "   um de cada IN até o 3V3. Mínimo útil:",
                   "   IN1 (resistência) e IN3 (bomba)."], 15, "#7c2d12", 23)

# painel 3 — conferência
rect(1500, 960, 440, 330, "#eff6ff", "#2563eb", 2, 12)
text(1520, 996, "Conferência (multímetro)", 21, "#1e3a8a", "700")
lines(1520, 1028, ["· ESP32 energizado e em RESET (botão RST",
                   "  apertado): cada IN deve medir ≈ 3,3 V.",
                   "· Durante a gravação o LED do relé NÃO",
                   "  deve acender (antes acendia o do CH3).",
                   "· Com o firmware rodando, ligar um",
                   "  atuador pelo app e o LED acende:",
                   "  o resistor não atrapalha.",
                   "· Se o relé ainda ligar na gravação,",
                   "  meça o IN: abaixo de 2,5 V = ligação",
                   "  errada ou módulo alimentado em 3,3 V."], 15, "#1e3a8a", 23)

add("</svg>")
io.open(os.path.join(OUT, "03_pullup_rele.svg"), "w", encoding="utf-8").write("\n".join(P))
print("ok")
