# -*- coding: utf-8 -*-
"""Gera 08_mcp23017_i2c.svg — expansor de I/O MCP23017 (Waveshare IO Expansion Board) no barramento I2C do ESP32 + 1o uso (LED de tampa aberta)."""
import io, os
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
C_V, C_G, C_SDA, C_SCL, C_SIG = "#dc2626", "#111827", "#2563eb", "#16a34a", "#ea580c"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=16, fill=INK, gap=26):
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill)


def rect(x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


def wire(pts, color, sw=5):
    add('<path d="M' + " L".join(f"{x},{y}" for x, y in pts) + f'" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"/>')


def pin(x, y): add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')


W, H = 2000, 1340
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 62, "MCP23017 (Waveshare IO Expansion Board) — ligação ao ESP32 e primeiro uso", 36, INK, "700")
text(60, 98, "Expansor de 16 pinos de entrada/saída pelo barramento I2C que já existe (SDA = GPIO8, SCL = GPIO9). Não ocupa nenhum GPIO novo.", 18, MUTE)

# ── ESP32 ──
rect(60, 140, 300, 420, "#e2e8f0", "#334155", 3, 14)
text(210, 176, "ESP32-S3", 28, INK, "700", "middle")
text(210, 200, "YD-ESP32-S3 44P", 15, MUTE, "400", "middle")
ys = {"3V3": 270, "GND": 340, "SDA": 410, "SCL": 480}
for k, y in ys.items():
    pin(360, y)
text(342, 276, "3V3", 19, INK, "700", "end")
text(342, 346, "GND", 19, INK, "700", "end")
text(342, 416, "GPIO8 (SDA)", 19, INK, "700", "end")
text(342, 486, "GPIO9 (SCL)", 19, INK, "700", "end")

# ── barramento I2C com os dispositivos que ja existem ──
rect(60, 600, 760, 260, "#ffffff", "#cbd5e1", 2, 14)
text(84, 636, "Já no mesmo barramento I2C (endereços diferentes, sem conflito)", 19, INK, "700")
for i, (n, a) in enumerate([("SHT31", "0x44"), ("ADXL345", "0x53"), ("DS3231", "0x68"), ("EEPROM do RTC", "0x57"), ("MCP23017", "0x20  ← novo")]):
    x = 84 + (i % 3) * 245; y = 662 + (i // 3) * 90
    rect(x, y, 225, 72, "#ecfdf5" if i == 4 else "#f1f5f9", "#16a34a" if i == 4 else "#94a3b8", 2, 10)
    text(x + 112, y + 32, n, 18, INK, "700", "middle")
    text(x + 112, y + 56, a, 17, "#166534" if i == 4 else MUTE, "700" if i == 4 else "400", "middle")

# ── placa MCP23017 ──
rect(560, 140, 460, 420, "#fef3c7", "#92400e", 3, 14)
text(790, 176, "Placa MCP23017", 26, INK, "700", "middle")
text(790, 200, "endereço 0x20 (A0 A1 A2 sem solda)", 15, MUTE, "400", "middle")
conn = [("VCC", 270, C_V), ("GND", 340, C_G), ("SDA", 410, C_SDA), ("SCL", 480, C_SCL)]
for n, y, c in conn:
    pin(560, y)
    text(578, y + 6, n, 19, INK, "700")
text(578, 520, "INTA / INTB: deixar SOLTOS (não há GPIO livre)", 15, MUTE)
for n, y, c in conn:
    wire([(360, y), (560, y)], c, 6)
text(460, 258, "3V3 (NÃO usar 5 V)", 15, C_V, "700", "middle")
text(460, 464, "fios curtos (< 30 cm)", 14, MUTE, "400", "middle")

# ── primeiro uso: LED de tampa aberta ──
rect(1080, 140, 880, 420, "#ffffff", "#cbd5e1", 2, 14)
text(1104, 176, "Primeiro uso sugerido: LED externo 'TAMPA ABERTA'", 22, INK, "700")
pin(1020, 270); text(1004, 276, "PA0", 18, INK, "700", "end")
wire([(1020, 270), (1140, 270)], C_SIG, 6)
rect(1140, 250, 130, 40, "#fde68a", "#92400e", 2, 8)
text(1205, 276, "330 Ω", 17, INK, "700", "middle")
wire([(1270, 270), (1400, 270)], C_SIG, 6)
add('<polygon points="1400,244 1400,296 1450,270" fill="#ef4444" stroke="#7f1d1d" stroke-width="3"/>')
add('<line x1="1450" y1="244" x2="1450" y2="296" stroke="#7f1d1d" stroke-width="4"/>')
wire([(1450, 270), (1560, 270), (1560, 340)], C_G, 6)
text(1560, 366, "GND", 17, INK, "700", "middle")
text(1425, 322, "LED vermelho 3 mm", 15, MUTE, "400", "middle")
lines(1104, 420, [
    "PA0 em nível alto acende; o firmware pisca o LED enquanto a tampa estiver aberta",
    "(e pode apagar quando fechar). 330 Ω limita a ~5 mA: bem abaixo do limite do chip.",
    "Mesmo princípio serve para um buzzer ativo de 3,3 V (com transistor se > 20 mA).",
], 16, INK, 28)

# ── tabelas e avisos ──
rect(60, 880, 960, 420, "#ffffff", "#cbd5e1", 2, 14)
text(84, 916, "Endereço I2C (pontes de solda A0 A1 A2 da placa)", 21, INK, "700")
lines(84, 952, [
    "A2 A1 A0 = 0 0 0  →  0x20  (como vem de fábrica; é o que vamos usar)",
    "A2 A1 A0 = 0 0 1  →  0x21        0 1 0  →  0x22        0 1 1  →  0x23 …",
    "Conferir antes de ligar: nenhum endereço pode repetir 0x44, 0x53, 0x57 ou 0x68.",
    "",
    "Registradores (referência): IODIRA/B = direção (1 = entrada) · GPPUA/B = pull-up",
    "interno de 100 kΩ · OLATA/B = saída · GPIOA/B = leitura das entradas.",
    "Cada pino aguenta ~25 mA; o chip todo ~125 mA. Pinos 3,3 V (não tolera 5 V",
    "se a placa estiver alimentada com 3,3 V).",
], 16, INK, 27)
rect(1040, 600, 920, 700, "#fff7ed", "#ea580c", 2, 14)
text(1064, 636, "Cuidados e conferências", 22, "#9a3412", "700")
lines(1064, 672, [
    "1) Alimente a placa com 3V3 do ESP32. Se for com 5 V, os pull-ups de 10 kΩ da",
    "   placa levam 5 V para SDA/SCL e o ESP32 (3,3 V) pode ser danificado.",
    "2) Antes de ligar, meça continuidade: VCC–GND NÃO pode apitar.",
    "3) Ligue com o aparelho desligado; depois do boot o log deve mostrar o",
    "   scan I2C com 0x20 (serial 'I'). Sem 0x20 = fio SDA/SCL trocado ou solto.",
    "4) Mantenha SDA/SCL longe dos fios do relé de 220 V (interferência).",
    "5) Pull-ups: SHT31 + ADXL + DS3231 + esta placa em paralelo ficam em ~1,6 kΩ;",
    "   é aceitável. Se começar a dar erro de leitura, retirar os pull-ups da placa.",
    "6) O firmware ainda NÃO tem driver para o MCP23017. É pequeno (probe do 0x20,",
    "   direção e escrita de saída) e só ativa quando o chip responde.",
    "",
    "Quando vale instalar: quando houver um uso concreto (LED/buzzer de tampa,",
    "TCRT5000 do ovoscópio, boia de nível de água, mais relés/entradas). Hoje não",
    "é necessário para nenhuma função que já existe.",
], 16, INK, 27)
add("</svg>")
with io.open(os.path.join(OUT, "08_mcp23017_i2c.svg"), "w", encoding="utf-8") as f:
    f.write("\n".join(P))
print("ok")
