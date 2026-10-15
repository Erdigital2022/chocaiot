# -*- coding: utf-8 -*-
"""Gera 04_irf520_pwm.svg — módulo MOSFET IRF520 ("MOS Module", marca MH) acionado por PWM do ESP32-S3 para um fan de 12 V.
Uso:  python gerar_irf520.py   (depois converter para PNG: ver converter.sh)"""
import io, os

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
C_SIG, C_12V, C_GND, C_LOAD, C_NC = "#2563eb", "#ea580c", "#111827", "#dc2626", "#94a3b8"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=15, fill=INK, gap=23, weight="400"):
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill, weight)


def rect(x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


def wire(pts, color, sw=5, dash=None):
    d = "M" + " L".join(f"{x},{y}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{da}/>')


def pin(x, y): add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')


def screw(x, y):
    add(f'<circle cx="{x}" cy="{y}" r="15" fill="#cbd5e1" stroke="#475569" stroke-width="2"/>')
    add(f'<line x1="{x-8}" y1="{y}" x2="{x+8}" y2="{y}" stroke="#475569" stroke-width="3"/>')


W, H = 2000, 1200
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 66, "Módulo IRF520 (MOSFET) — PWM do ESP32 controlando um fan de 12 V", 36, INK, "700")
text(60, 102, "O ESP32 só manda o sinal (SIG, 3,3 V, quase sem corrente). Quem alimenta o fan é a fonte de 12 V pelos bornes do módulo. Os GNDs precisam ser comuns.", 18, MUTE)

# ── ESP32 ──
rect(60, 150, 340, 470, "#e2e8f0", "#334155", 3, 14)
text(230, 192, "ESP32-S3", 30, INK, "700", "middle")
text(230, 220, "YD-ESP32-S3 44P · N16R8", 16, MUTE, "400", "middle")
Y_SIG, Y_VCC, Y_GND = 300, 350, 400
pin(400, Y_SIG); text(380, Y_SIG + 6, "GPIO4", 19, INK, "700", "end"); text(380, Y_SIG + 28, "fan 1 · PWM 1 kHz", 14, MUTE, "400", "end")
pin(400, Y_GND); text(380, Y_GND + 6, "GND", 19, INK, "700", "end")
pin(400, 500); text(380, 506, "3V3", 19, C_NC, "700", "end"); text(380, 528, "não usado neste módulo", 14, MUTE, "400", "end")
text(230, 590, "5 V do ESP32 também não é usado", 14, MUTE, "400", "middle")

# ── módulo ──
MX0, MX1 = 760, 1200
rect(MX0, 170, MX1 - MX0, 470, "#fee2e2", "#b91c1c", 3, 14)
text((MX0 + MX1) // 2, 208, "Módulo MOS IRF520 (placa vermelha “MH”)", 19, "#7f1d1d", "700", "middle")
text((MX0 + MX1) // 2, 234, "SIG aciona o transistor · carga ligada em U+ / U−", 15, "#991b1b", "400", "middle")
# cabeçalho de 3 pinos
pin(MX0, Y_SIG); text(MX0 + 14, Y_SIG - 12, "SIG", 17, "#7f1d1d", "700")
pin(MX0, Y_VCC); text(MX0 + 14, Y_VCC - 12, "VCC", 17, "#7f1d1d", "700")
pin(MX0, Y_GND); text(MX0 + 14, Y_GND - 12, "GND", 17, "#7f1d1d", "700")
# símbolo do MOSFET (interno, simplificado)
rect(930, 320, 130, 130, "#fff", "#7f1d1d", 2.5, 8)
text(995, 372, "IRF520", 20, "#7f1d1d", "700", "middle"); text(995, 398, "chave no lado −", 13, "#7f1d1d", "400", "middle")
wire([(MX0, Y_SIG), (880, Y_SIG), (880, 372), (930, 372)], "#7f1d1d", 3)
text(900, 360, "G", 15, "#7f1d1d", "700")
wire([(1060, 345), (1080, 345), (1080, 378), (1165, 378), (1165, 352)], "#7f1d1d", 3)
text(1068, 338, "D", 15, "#7f1d1d", "700")
wire([(MX0, Y_GND), (830, Y_GND), (830, 530), (MX1, 530)], "#7f1d1d", 3)
wire([(995, 450), (995, 530)], "#7f1d1d", 3)
text(1002, 470, "S", 15, "#7f1d1d", "700")
# bornes
rect(1130, 262, 70, 90, "#2563eb", "#1e3a8a", 2, 6); screw(1165, 285); screw(1165, 330)
rect(1130, 462, 70, 90, "#2563eb", "#1e3a8a", 2, 6); screw(1165, 485); screw(1165, 530)
text(1118, 292, "U+", 19, "#7f1d1d", "700", "end"); text(1118, 337, "U−", 19, "#7f1d1d", "700", "end")
text(1120, 492, "VIN", 19, "#7f1d1d", "700", "end"); text(1120, 520, "GND", 19, "#7f1d1d", "700", "end")
text(960, 262, "SAÍDA para a carga", 15, "#991b1b", "700", "middle")
text(960, 575, "ENTRADA da fonte (U+ é ligado ao VIN)", 15, "#991b1b", "700", "middle")

# ── fios ESP → módulo ──
wire([(400, Y_SIG), (MX0, Y_SIG)], C_SIG, 5)
text(580, Y_SIG - 12, "sinal PWM 3,3 V", 16, C_SIG, "700", "middle")
wire([(400, Y_GND), (MX0, Y_GND)], C_GND, 5)
text(580, Y_GND - 12, "GND comum", 16, C_GND, "700", "middle")
wire([(MX0 - 150, Y_VCC), (MX0, Y_VCC)], C_NC, 4, "8 8")
text(MX0 - 210, Y_VCC + 6, "VCC: NÃO ligar", 16, "#b91c1c", "700", "end")
add(f'<path d="M{MX0-198},{Y_VCC-12} l24,24 m0,-24 l-24,24" stroke="#b91c1c" stroke-width="5" stroke-linecap="round"/>')

# ── fan e fonte ──
FX = 1560
rect(FX, 170, 340, 200, "#e2e8f0", "#334155", 3, 14)
text(FX + 170, 208, "Fan 12 V (PC)", 24, INK, "700", "middle")
text(FX + 170, 234, "2 ou 3 fios · até ~0,3 A", 15, MUTE, "400", "middle")
pin(FX, 285); text(FX + 22, 291, "+  fio vermelho", 16, INK, "700")
pin(FX, 335); text(FX + 22, 341, "−  fio preto", 16, INK, "700")
wire([(1200, 285), (FX, 285)], C_LOAD, 5)
wire([(1200, 330), (1380, 330), (1380, 335), (FX, 335)], C_GND, 5)
text(1380, 266, "carga (+)", 15, C_LOAD, "700", "middle")
text(1380, 360, "carga (−) chaveada", 15, C_GND, "700", "middle")

rect(FX, 430, 340, 200, "#fff7ed", "#ea580c", 3, 14)
text(FX + 170, 468, "Fonte 12 V DC", 24, INK, "700", "middle")
text(FX + 170, 494, "a mesma que já alimenta os fans", 15, MUTE, "400", "middle")
pin(FX, 545); text(FX + 22, 551, "+ 12 V", 16, INK, "700")
pin(FX, 595); text(FX + 22, 601, "− GND", 16, INK, "700")
wire([(1200, 485), (1380, 485), (1380, 545), (FX, 545)], C_12V, 5)
wire([(1200, 530), (1300, 530), (1300, 595), (FX, 595)], C_GND, 5)
text(1290, 462, "12 V +", 15, C_12V, "700", "middle")
text(1430, 585, "GND da fonte", 15, C_GND, "700", "middle")

# ── legenda ──
rect(60, 650, 640, 60, "#fff", "#cbd5e1", 1.5, 10)
for k, (c, t) in enumerate([(C_SIG, "sinal GPIO"), (C_12V, "12 V +"), (C_GND, "GND"), (C_LOAD, "carga +"), (C_NC, "não ligado")]):
    xx = 80 + k * 125
    add(f'<line x1="{xx}" y1="680" x2="{xx+30}" y2="680" stroke="{c}" stroke-width="6" stroke-linecap="round"/>')
    text(xx + 38, 686, t, 15, INK)

# ── painéis ──
PY, PH = 730, 440
rect(60, PY, 440, PH, "#fff7ed", "#ea580c", 2, 12)
text(80, PY + 36, "1 · Montagem (tudo desligado)", 21, "#9a3412", "700")
lines(80, PY + 70, [
    "1) Fonte de 12 V desligada da tomada e",
    "   ESP32 sem USB.",
    "2) GPIO4 do ESP32 → pino SIG do módulo",
    "   (fio dupont, lado do cabeçalho).",
    "3) GND do ESP32 → pino GND do cabeçalho.",
    "4) Pino VCC do cabeçalho: deixar SOLTO.",
    "5) Fan: fio vermelho → borne U+ ;",
    "   fio preto → borne U−.",
    "6) Fonte: + 12 V → borne VIN ;",
    "   − → borne GND (os de baixo).",
    "7) Só depois ligar fonte e ESP32.",
    "",
    "NUNCA ligar 12 V no cabeçalho de 3 pinos:",
    "ele é o lado do ESP32 (3,3 V).",
], 15, "#7c2d12", 24)

rect(520, PY, 440, PH, "#eff6ff", "#2563eb", 2, 12)
text(540, PY + 36, "2 · Conferência sem energia", 21, "#1e3a8a", "700")
lines(540, PY + 70, [
    "Multímetro em continuidade (bipe):",
    "· VIN ↔ U+ : deve APITAR",
    "· GND do cabeçalho ↔ GND do borne:",
    "  deve APITAR",
    "· U− ↔ GND do borne: NÃO pode dar 0 Ω",
    "  (é o transistor; no modo diodo pode",
    "  marcar ~0,5 V, isso é normal).",
    "",
    "Se algo divergir, pare e me mande a",
    "leitura: este desenho assume que o",
    "módulo é igual ao da foto (placa",
    "vermelha, silk SIG/VCC/GND e",
    "U+ U− / VIN GND).",
], 15, "#1e3a8a", 24)

rect(980, PY, 440, PH, "#ecfdf5", "#16a34a", 2, 12)
text(1000, PY + 36, "3 · Teste com energia", 21, "#166534", "700")
lines(1000, PY + 70, [
    "1) Ligue a fonte e o ESP32 (com o PWM",
    "   ainda DESLIGADO o fan fica parado).",
    "2) Se o fan girar sozinho: o pino solto",
    "   flutua; 10 kΩ de SIG para GND resolve.",
    "3) Tecla P no serial liga o PWM (100 %).",
    "4) Medir nos bornes U+ ↔ U− (com o fan):",
    "   ≥ 11 V = conduz bem.",
    "   < 10 V = o IRF520 não satura em 3,3 V.",
    "5) Tocar no MOSFET: morno é normal;",
    "   quente demais = não está saturando.",
    "6) Pare o teste em 100 % se esquentar e",
    "   me diga a tensão medida.",
], 15, "#14532d", 24)

rect(1440, PY, 500, PH, "#faf5ff", "#7c3aed", 2, 12)
text(1460, PY + 36, "4 · Limites e canais preparados", 21, "#5b21b6", "700")
lines(1460, PY + 70, [
    "· IRF520 NÃO é MOSFET lógico: com 3,3 V no",
    "  gate ele só conduz parcialmente. Para fan",
    "  de PC (≤ 0,3 A) costuma bastar; se medir",
    "  < 10 V, trocar por IRLZ44N / IRLB8721",
    "  ou módulo com driver de 5 V.",
    "· Zumbido em 1 kHz: normal; se incomodar,",
    "  subir PWM_FREQ_HZ no firmware.",
    "",
    "Canais já previstos (Fase 99, desligados):",
    "  GPIO4 → fan 1  ·  GPIO5 → fan 2",
    "  GPIO6 → fan 3  ·  GPIO47 → backlight",
    "  (backlight: confirmar tensão e corrente",
    "  do LED de fundo antes de ligar aqui)",
    "· Um módulo por canal, mesmo desenho,",
    "  todos com o GND em comum.",
], 15, "#4c1d95", 24)

add("</svg>")
io.open(os.path.join(OUT, "04_irf520_pwm.svg"), "w", encoding="utf-8").write("\n".join(P))
print("ok")
