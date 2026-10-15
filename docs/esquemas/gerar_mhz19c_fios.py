# -*- coding: utf-8 -*-
"""Gera 07_mhz19c_fios_sem_marcacao.svg — identificação dos fios do MH-Z19C pelo conector (placa sem marcações).
Ordem dos fios vista nas fotos do usuário (esq.→dir., lado da etiqueta): marrom, branco, preto, vermelho, azul, verde, amarelo."""
import io, os
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=17, fill=INK, gap=27):
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill)


def rect(x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


W, H = 2000, 1380
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 62, "MH-Z19C — descobrir os fios SEM marcações na placa (pela posição no conector)", 36, INK, "700")
text(60, 98, "O módulo é a versão 'terminal': 7 pads + conector branco de 7 vias. Cada fio ocupa uma posição fixa do conector; a posição define a função.", 18, MUTE)

# ── 1. ordem dos fios ──
rect(40, 130, 1920, 470, "#ffffff", "#cbd5e1", 2, 16)
text(64, 168, "1) Ordem real dos fios no seu conector (fotos: etiqueta para cima, esquerda → direita)", 22, INK, "700")
FIOS = [("marrom", "#92400e"), ("branco", "#ffffff"), ("preto", "#111827"), ("vermelho", "#dc2626"),
        ("azul", "#2563eb"), ("verde", "#15803d"), ("amarelo", "#facc15")]
HA = [("1", "HD", "off"), ("2", "Reservado", "off"), ("3", "GND", "gnd"), ("4", "Vin +5 V", "v5"), ("5", "RX", "rx"), ("6", "TX", "tx"), ("7", "PWM", "off")]
HB = list(reversed(HA))
STY = {"v5": ("#ffedd5", "#ea580c"), "gnd": ("#e5e7eb", "#111827"), "tx": ("#dcfce7", "#16a34a"),
       "rx": ("#dbeafe", "#2563eb"), "off": ("#f1f5f9", "#94a3b8")}
X0, DX = 330, 215
text(64, 262, "fio (cor)", 17, MUTE, "700")
text(64, 372, "Hipótese A", 18, INK, "700")
text(64, 394, "pino 1 = lado do marrom", 15, MUTE)
text(64, 492, "Hipótese B", 18, INK, "700")
text(64, 514, "pino 1 = lado do amarelo", 15, MUTE)
for i, (nome, cor) in enumerate(FIOS):
    cx = X0 + i * DX
    if i == 3:
        rect(cx - 100, 190, 200, 376, "#fff7ed", "#ea580c", 3, 14, "8 6")
    add(f'<circle cx="{cx}" cy="262" r="30" fill="{cor}" stroke="#334155" stroke-width="3"/>')
    text(cx, 322, nome, 20, INK, "700", "middle")
    for (row, y, H_) in [(0, 340, HA), (1, 460, HB)]:
        n, f, cat = H_[i]
        fill, st = STY[cat]
        rect(cx - 90, y, 180, 100, fill, st, 3, 10)
        text(cx, y + 36, f"pino {n}", 17, MUTE, "400", "middle")
        text(cx, y + 74, f, 24, INK, "700", "middle")
text(64, 590, "O fio do MEIO (vermelho) é o pino 4 = Vin (+5 V) nas duas hipóteses. Só falta saber de que lado fica o GND: preto (A) ou azul (B).", 19, "#9a3412", "700")

# ── 2. testes ──
rect(40, 620, 1190, 720, "#ffffff", "#cbd5e1", 2, 16)
text(64, 658, "2) Testes (nesta ordem) — tudo DESLIGADO, sensor sem ligar em nada", 22, INK, "700")
passos = [
    ("Teste 1 — decisivo: o GND encosta na carcaça", [
        "Multímetro em continuidade (bipe). Uma ponta na superfície dourada do sensor (ou numa das",
        "bolinhas prateadas de solda nos cantos da placa); a outra ponta no fio PRETO, depois no AZUL.",
        "Apitou com o PRETO  → GND = preto  → Hipótese A.      Apitou com o AZUL → GND = azul → Hipótese B.",
        "Se nenhum apitar (carcaça não é GND), siga para o Teste 2."]),
    ("Teste 2 — indício: resistência a partir do vermelho (Vin)", [
        "Multímetro em Ω. Meça vermelho–preto e vermelho–azul, e depois inverta as pontas e meça de novo.",
        "O vizinho que é GND costuma dar o MENOR valor (o seu vermelho–preto = 671 já é um bom sinal).",
        "'Não apitou' é normal: o bipe só vem abaixo de ~50 Ω; 671 Ω quer dizer 'sem curto'."]),
    ("Teste 3 — confirmar com energia (só com o 1 ou o 2 conclusivo)", [
        "Ligue SÓ Vin (vermelho) → +5 V do DC-DC e GND (o fio decidido) → GND. TX e RX ainda soltos.",
        "Medindo em DC V contra o GND: o fio PWM é o único que fica oscilando; HD e TX ficam perto de 3,3 V.",
        "Se o sensor ficar morno em segundos ou o fio vermelho mostrar menos de 4,5 V: desligue e reveja."]),
    ("Teste 4 — UART no ESP32", [
        "Ligue TX do sensor no GPIO16 e RX do sensor no GPIO15. Espere 3 min (aquecimento).",
        "Sem valor de CO₂? Troque os dois fios de lugar (inofensivo) e espere mais 3 min."]),
]
y = 696
for tit, ls in passos:
    text(64, y, tit, 19, "#1d4ed8", "700")
    for k, t in enumerate(ls):
        text(84, y + 28 + k * 25, t, 16, INK)
    y += 28 + len(ls) * 25 + 22

# ── 3. tabelas finais ──
rect(1250, 620, 710, 720, "#ffffff", "#cbd5e1", 2, 16)
text(1274, 658, "3) Ligação conforme o resultado", 22, INK, "700")
rect(1274, 684, 662, 290, "#f0fdf4", "#16a34a", 2, 12)
text(1294, 716, "Hipótese A (GND = preto)", 20, "#166534", "700")
lines(1294, 748, [
    "vermelho    → +5 V do DC-DC  (Vin)",
    "preto   → GND comum",
    "azul    → GPIO15  (RX do sensor ← TX do ESP32)",
    "verde   → GPIO16  (TX do sensor → RX do ESP32)",
    "marrom (HD), branco (reservado), amarelo (PWM): isolar",
], 17, INK, 28)
text(1294, 904, "Se o log disser 'sem resposta': troque azul ↔ verde.", 16, MUTE)
rect(1274, 990, 662, 290, "#eff6ff", "#2563eb", 2, 12)
text(1294, 1022, "Hipótese B (GND = azul)", 20, "#1d4ed8", "700")
lines(1294, 1054, [
    "vermelho    → +5 V do DC-DC  (Vin)",
    "azul    → GND comum",
    "preto   → GPIO15  (RX do sensor ← TX do ESP32)",
    "branco  → GPIO16  (TX do sensor → RX do ESP32)",
    "amarelo (HD), verde (reservado), marrom (PWM): isolar",
], 17, INK, 28)
text(1294, 1210, "Se o log disser 'sem resposta': troque preto ↔ branco.", 16, MUTE)
text(1274, 1316, "Alimentação: 5,0 V ±0,1 V, ≥ 150 mA. Nunca no 3V3 nem no pino '5V' do adaptador.", 15, "#b91c1c", "700")
add("</svg>")
with io.open(os.path.join(OUT, "07_mhz19c_fios_sem_marcacao.svg"), "w", encoding="utf-8") as f:
    f.write("\n".join(P))
print("ok")
