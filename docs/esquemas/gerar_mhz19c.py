# -*- coding: utf-8 -*-
"""Gera 06_mhz19c_identificar_fios.svg — ficha para identificar os 7 fios do cabo do MH-Z19C e ligar ao ESP32.
Fonte dos pinos: manual Winsen MH-Z19C v1.0 (2020-02-04), "Terminal connection type" (conector de 7 vias).
Uso: python gerar_mhz19c.py  (PNG: converter.sh)"""
import io, os
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"
C_5V, C_GND, C_TX, C_RX = "#ea580c", "#111827", "#2563eb", "#16a34a"
P = []


def add(x): P.append(x)


def text(x, y, t, size=18, fill=INK, weight="400", anchor="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def lines(x, y, arr, size=16, fill=INK, gap=25):
    for i, t in enumerate(arr):
        text(x, y + i * gap, t, size, fill)


def rect(x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


def wire(pts, color, sw=6):
    d = "M" + " L".join(f"{x},{y}" for x, y in pts)
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"/>')


def pin(x, y): add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')


W, H = 2000, 1480
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="100%" height="100%" fill="{BG}"/>')
text(60, 62, "MH-Z19C (sensor de CO₂) — ficha para identificar os 7 fios e ligar ao ESP32", 36, INK, "700")
text(60, 98, "Fonte: manual Winsen MH-Z19C v1.0 (conector de 7 vias). A cor de cada fio NÃO vem em nenhum manual — ela é descoberta com o multímetro (passo 2).", 18, MUTE)

# ───────────────────────── 1. conector ─────────────────────────
rect(40, 130, 1010, 560, "#ffffff", "#cbd5e1", 2, 16)
text(64, 168, "1) Os 7 pinos do conector (numeração do manual: 7 … 1, da esquerda para a direita)", 22, INK, "700")
text(64, 196, "Escreva, em cada caixa tracejada, a COR do fio que você descobrir para aquele pino.", 17, MUTE)

SLOT_W, GAP, X0, Y0 = 128, 14, 64, 224
pins = [  # numero, nome, funcao, categoria
    (7, "PWM", "saída PWM (não usar)", "off"),
    (6, "TX", "saída de dados UART (3,3 V)", "tx"),
    (5, "RX", "entrada de dados UART (3,3 V)", "rx"),
    (4, "Vin", "+5 V DC (alimentação)", "v5"),
    (3, "GND", "terra (−)", "gnd"),
    (2, "Reservado", "não ligar", "off"),
    (1, "HD", "calibração zero (não usar)", "off"),
]
STY = {
    "v5": ("#ffedd5", "#ea580c"), "gnd": ("#e5e7eb", "#111827"),
    "tx": ("#dbeafe", "#2563eb"), "rx": ("#dcfce7", "#16a34a"), "off": ("#f1f5f9", "#94a3b8"),
}
for i, (n, nome, func, cat) in enumerate(pins):
    x = X0 + i * (SLOT_W + GAP)
    fill, st = STY[cat]
    rect(x, Y0, SLOT_W, 250, fill, st, 3, 12)
    text(x + SLOT_W / 2, Y0 + 52, str(n), 44, st if cat != "off" else MUTE, "800", "middle")
    text(x + SLOT_W / 2, Y0 + 88, nome, 24, INK, "700", "middle")
    # funcao em 2-3 linhas
    palavras = func.split(" ")
    ln, cur = [], ""
    for p in palavras:
        if len(cur) + len(p) + 1 > 14 and cur:
            ln.append(cur); cur = p
        else:
            cur = (cur + " " + p).strip()
    ln.append(cur)
    for k, t in enumerate(ln[:3]):
        text(x + SLOT_W / 2, Y0 + 120 + k * 22, t, 15, MUTE, "400", "middle")
    # caixa para a cor
    text(x + SLOT_W / 2, Y0 + 196, "cor do fio:", 14, MUTE, "400", "middle")
    rect(x + 12, Y0 + 204, SLOT_W - 24, 34, "#ffffff", "#64748b", 2, 8, "6 5")
text(64, 520, "Legenda:", 17, INK, "700")
for k, (c, t) in enumerate([("#ffedd5", "Vin = +5 V (o único que pode queimar o sensor se errar)"),
                            ("#e5e7eb", "GND"), ("#dbeafe", "TX do sensor → RX do ESP32"),
                            ("#dcfce7", "RX do sensor ← TX do ESP32"), ("#f1f5f9", "pinos que ficam sem ligação")]):
    y = 546 + k * 28
    rect(64, y - 16, 22, 20, c, "#64748b", 1, 4)
    text(98, y, t, 16, INK)

# cores do cabo
text(64, 706 - 6, "", 10)
rect(40, 710, 1010, 118, "#ffffff", "#cbd5e1", 2, 16)
text(64, 746, "As 7 cores que o seu cabo tem (marque aqui cada uma que for usada):", 19, INK, "700")
CORES = [("amarelo", "#facc15"), ("verde", "#22c55e"), ("azul", "#3b82f6"), ("vermelho", "#dc2626"),
         ("preto", "#111827"), ("branco", "#ffffff"), ("marrom", "#92400e")]
for k, (nome, c) in enumerate(CORES):
    x = 90 + k * 140
    add(f'<circle cx="{x}" cy="794" r="17" fill="{c}" stroke="#334155" stroke-width="2"/>')
    text(x + 26, 800, nome, 17, INK)

# ───────────────────────── 2. procedimento ─────────────────────────
rect(1070, 130, 890, 698, "#ffffff", "#cbd5e1", 2, 16)
text(1094, 168, "2) Como descobrir qual fio é qual — multímetro em CONTINUIDADE (bipe)", 22, INK, "700")
text(1094, 196, "Faça com tudo DESLIGADO: sensor fora do ESP32, DC-DC desligado, nada energizado.", 17, "#b91c1c", "700")
passos = [
    "a) Encaixe o cabo no sensor. Deixe as pontas do outro lado do cabo soltas, sem encostar umas nas outras.",
    "b) Na placa do sensor, ache as marcações dos furos: Hd, SR, Tx, Rx, Vo (esq.) e PWM, AOT, GND, Vin (dir.).",
    "c) Uma ponta do multímetro em UM furo da placa (ex.: GND); a outra, em cada uma das pontas dos 7 fios.",
    "d) O fio que apitar é o daquele sinal. Anote a cor na caixa do pino correspondente (ex.: GND → pino 3).",
    "e) Repita para Vin (pino 4), Tx (pino 6) e Rx (pino 5). Em PWM e Hd é só para conferir (pinos 7 e 1).",
    "f) Confirmação final: entre o fio Vin e o fio GND NÃO pode apitar (se apitar = curto: não energize).",
    "g) Sem marcações na placa: use a figura 'Fig.4' do manual como referência da ordem dos pinos e confirme",
    "    cada fio com o teste de continuidade antes de energizar.",
]
for k, t in enumerate(passos):
    text(1094, 238 + k * 36, t, 17, INK)
rect(1094, 540, 842, 262, "#fff7ed", "#ea580c", 2, 12)
text(1114, 574, "Regra de ouro (segurança)", 19, "#9a3412", "700")
lines(1114, 604, [
    "• Só o +5 V no pino errado queima o sensor. TX/RX trocados NÃO queimam nada (são 3,3 V): se o log mostrar",
    "   'sem resposta', é só trocar os dois fios de lugar.",
    "• Ligue o Vin só depois de a tabela estar completa e conferida.",
    "• A alimentação exige 5,0 V ±0,1 V e pelo menos 150 mA. Use a saída 5 V do conversor DC-DC,",
    "   não o pino '5V' do adaptador do ESP32 (lá aparecem ~0,6 V, é um diodo) e nunca o 3V3.",
    "• Não aperte a câmara dourada; mantenha a janela de difusão (filtro branco) livre e longe da névoa.",
], 16, INK, 27)

# ───────────────────────── 3. ligação final ─────────────────────────
rect(40, 850, 1190, 590, "#ffffff", "#cbd5e1", 2, 16)
text(64, 888, "3) Ligação final (depois da tabela preenchida)", 22, INK, "700")
# ESP32
rect(80, 920, 300, 360, "#e2e8f0", "#334155", 3, 14)
text(230, 954, "ESP32-S3", 26, INK, "700", "middle")
text(230, 978, "YD-ESP32-S3 44P", 15, MUTE, "400", "middle")
pin(380, 1030); text(362, 1036, "GPIO16 (RX1)", 17, INK, "700", "end")
pin(380, 1100); text(362, 1106, "GPIO15 (TX1)", 17, INK, "700", "end")
pin(380, 1240); text(362, 1246, "GND", 17, INK, "700", "end")
# DC-DC (entre o ESP e o sensor)
rect(560, 1140, 200, 170, "#fde68a", "#92400e", 3, 12)
text(660, 1176, "Conversor", 19, INK, "700", "middle")
text(660, 1200, "DC-DC 5 V (3 A)", 17, INK, "700", "middle")
text(660, 1226, "entrada 12/24 V", 14, MUTE, "400", "middle")
text(660, 1290, "saída 5 V → direita", 14, MUTE, "400", "middle")
# sensor
rect(930, 920, 270, 400, "#f1f5f9", "#334155", 3, 14)
text(1065, 954, "MH-Z19C", 26, INK, "700", "middle")
text(1065, 978, "conector 7 vias", 15, MUTE, "400", "middle")
for y, t in [(1030, "TX (pino 6)"), (1100, "RX (pino 5)"), (1170, "Vin (pino 4)"), (1240, "GND (pino 3)")]:
    pin(930, y)
    text(950, y + 6, t, 17, INK, "700")
text(1065, 1296, "HD, PWM, Reservado: sem ligação", 14, MUTE, "400", "middle")
# fios (retos, sem cruzar)
wire([(380, 1030), (930, 1030)], C_RX, 6)     # sensor TX -> GPIO16
wire([(380, 1100), (930, 1100)], C_TX, 6)     # GPIO15 -> sensor RX
wire([(760, 1170), (930, 1170)], C_5V, 6)     # +5 V -> Vin
wire([(380, 1240), (560, 1240)], C_GND, 6)    # GND ESP -> DC-DC (comum)
wire([(760, 1240), (930, 1240)], C_GND, 6)    # GND DC-DC -> GND sensor
pin(560, 1240); pin(760, 1240); pin(760, 1170)
text(655, 1018, "TX do sensor  →  GPIO16", 16, C_RX, "700", "middle")
text(655, 1088, "GPIO15  →  RX do sensor", 16, C_TX, "700", "middle")
text(845, 1158, "+5 V", 16, C_5V, "700", "middle")
text(845, 1228, "GND", 16, INK, "700", "middle")
text(470, 1228, "GND comum", 16, INK, "700", "middle")
# capacitor
rect(840, 1346, 360, 76, "#ecfdf5", "#16a34a", 2, 10)
text(858, 1376, "Recomendado: 220–470 µF + 100 nF", 16, INK, "700")
text(858, 1402, "entre Vin e GND, junto ao sensor (pico 125 mA)", 15, MUTE)
text(80, 1330, "Alimentação: 5,0 V ±0,1 V e ≥ 150 mA (fio curto e grosso).", 16, INK)
text(80, 1356, "Os fios TX/RX são 3,3 V: ligam direto, sem conversor.", 16, INK)
text(80, 1382, "HD e PWM ficam soltos; isole as pontas com fita.", 16, INK)

# ───────────────────────── 4. o que esperar ─────────────────────────
rect(1250, 850, 710, 590, "#ffffff", "#cbd5e1", 2, 16)
text(1274, 888, "4) O que o firmware já faz e o que esperar", 22, INK, "700")
lines(1274, 930, [
    "• UART1 9600 8N1: GPIO16 lê o TX do sensor, GPIO15 envia ao RX.",
    "• Comando de leitura: FF 01 86 00 00 00 00 00 79 (resposta FF 86 HH LL … CS).",
    "• Ignora os 3 primeiros minutos (aquecimento) e valores fora de 300–10000 ppm.",
    "• Desliga a autocalibração (ABC) uma vez: numa caixa fechada com ovos o CO₂",
    "   quase nunca volta a 400 ppm e o ABC faria o sensor 'zerar' no valor errado.",
    "• Faixa do MH-Z19C: 400 a 5000 ppm (precisão ±(50 ppm + 5 %)). Incubadora",
    "   costuma ficar entre 1000 e 3000 ppm: dentro da faixa.",
    "• Sem resposta no log = TX/RX trocados ou 5 V fora de 4,9–5,1 V.",
    "• O valor aparece no card CO₂ do aparelho e do PWA (via heartbeat, ~1 min).",
    "• As regras do fan de Renovação por CO₂ continuam INATIVAS até você pedir.",
    "",
    "Instalação: longe da resistência (limite 50 °C), da névoa do umidificador",
    "e da saída do ventilador; janela de difusão livre; sem condensação.",
    "Calibração manual a cada ≤ 6 meses: ar livre (~400 ppm) por 20 min e",
    "fio HD em 0 V por 7 s (uma única vez, fora da caixa).",
], 16, INK, 27)
add("</svg>")
with io.open(os.path.join(OUT, "06_mhz19c_identificar_fios.svg"), "w", encoding="utf-8") as f:
    f.write("\n".join(P))
print("ok")
