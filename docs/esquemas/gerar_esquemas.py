# -*- coding: utf-8 -*-
"""Gera os esquemas de ligação (SVG) do ChocaIoT: 01_ligacao_ADXL345 e 02_visao_geral_sistema.
Uso:  python gerar_esquemas.py   (depois converter para PNG com o Chrome headless — ver converter.sh)
Fonte das ligações: CLAUDE.md (pinout confirmado). Itens marcados "confirmar" não estão documentados no projeto."""
import io, os

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Segoe UI, Arial, sans-serif"
C_SDA, C_SCL, C_3V3, C_GND, C_5V, C_12V = "#16a34a", "#d4a000", "#ea580c", "#111827", "#dc2626", "#7c3aed"
BG, INK, MUTE = "#f8fafc", "#0f172a", "#475569"


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.p = w, h, []

    def add(self, s):
        self.p.append(s)

    def text(self, x, y, t, size=18, fill=INK, weight="400", anchor="start", italic=False):
        t = t.replace("&", "&amp;").replace("<", "&lt;")
        st = ' font-style="italic"' if italic else ""
        self.add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{st}>{t}</text>')

    def lines(self, x, y, arr, size=17, fill=INK, gap=None, weight="400"):
        gap = gap or size + 7
        for i, t in enumerate(arr):
            self.text(x, y + i * gap, t, size, fill, weight)

    def rect(self, x, y, w, h, fill="#fff", stroke="#334155", sw=2, rx=10, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def wire(self, pts, color, sw=5, dash=None, hops=None):
        """polilinha ortogonal; hops = lista de (x,y) onde um segmento horizontal 'pula' sobre outro fio"""
        hops = hops or []
        d = f"M{pts[0][0]},{pts[0][1]}"
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            if y1 == y2:  # horizontal
                xs = sorted([hx for hx, hy in hops if hy == y1 and min(x1, x2) < hx < max(x1, x2)], reverse=(x2 < x1))
                for hx in xs:
                    r = 9
                    sgn = 1 if x2 > x1 else -1
                    d += f" L{hx - sgn * r},{y1} A{r},{r} 0 0 {1 if sgn > 0 else 0} {hx + sgn * r},{y1}"
            d += f" L{x2},{y2}"
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{da}/>')

    def dot(self, x, y, color, r=7):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}" stroke="#fff" stroke-width="2"/>')

    def pin(self, x, y, label=None, side="r", size=16, color=INK):
        self.add(f'<circle cx="{x}" cy="{y}" r="7" fill="#e5b73b" stroke="#7a5b00" stroke-width="2"/>')

    def save(self, name):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">'
                f'<rect width="100%" height="100%" fill="{BG}"/>')
        io.open(os.path.join(OUT, name), "w", encoding="utf-8").write(head + "\n".join(self.p) + "</svg>")


def legenda(s, x, y):
    s.rect(x, y, 380, 168, "#fff", "#cbd5e1", 1.5, 10)
    s.text(x + 16, y + 28, "Cores dos fios", 17, INK, "700")
    for i, (c, t) in enumerate([(C_SDA, "SDA (dados I2C)"), (C_SCL, "SCL (clock I2C)"), (C_3V3, "3V3"), (C_5V, "+5 V"), (C_GND, "GND (comum)")]):
        yy = y + 54 + i * 22
        s.add(f'<line x1="{x+16}" y1="{yy-5}" x2="{x+64}" y2="{yy-5}" stroke="{c}" stroke-width="6" stroke-linecap="round"/>')
        s.text(x + 76, yy, t, 15, INK)


# ═══════════════════════════ 01 — ADXL345 ═══════════════════════════
def adxl():
    s = Svg(2560, 1740)
    s.text(60, 70, "ADXL345 — ligação ao ChocaIoT (I2C, mesmo barramento do relógio e do SHT31)", 38, INK, "700")
    s.text(60, 106, "Peças já ligadas em cinza; ADXL345 (NOVO) em destaque. Ajustado ao esquemático do fabricante (ME6206-33, R4/R5 0 Ω, R6 10 kΩ). Confira a fiação real antes de energizar.", 18, MUTE)

    # ESP32
    s.rect(80, 160, 440, 960, "#e2e8f0", "#334155", 3, 14)
    s.text(300, 200, "ESP32-S3", 30, INK, "700", "middle")
    s.text(300, 228, "YD-ESP32-S3 44P · N16R8", 17, MUTE, "400", "middle")
    s.lines(104, 640, ["Outros usos dos pinos (já ligados):",
                       "· Display ILI9341 / touch / SD:",
                       "  GPIO 11 MOSI · 13 MISO · 12 SCK",
                       "  10 CS · 14 DC · 21 RST · 48 T_CS · 18 SD_CS",
                       "· Módulo de relé (IN1…IN8):",
                       "  GPIO 1 · 2 · 38 · 39 · 40 · 41 · 42 · 7",
                       "· Reservados (futuro): 4/5/6 PWM fans,",
                       "  15/16/17 tacômetro, 47 backlight"], 15, "#334155", 22)
    for (yy, lab, col) in [(220, "3V3", C_3V3), (300, "GPIO8  ·  SDA", C_SDA), (380, "GPIO9  ·  SCL", C_SCL), (460, "GND", C_GND)]:
        s.text(500, yy + 6, lab, 18, INK, "700", "end")
        s.pin(520, yy, None)
    # pinos de baixo
    s.pin(360, 1120); s.text(360, 1100, "5V", 18, INK, "700", "middle")
    s.pin(440, 1120); s.text(440, 1100, "GND", 18, INK, "700", "middle")

    # trilhos
    X_G, X_SCL, X_SDA, X_3 = 700, 760, 820, 880
    s.wire([(520, 220), (X_3, 220), (X_3, 770)], C_3V3)
    s.wire([(520, 300), (X_SDA, 300), (X_SDA, 1000)], C_SDA)
    s.wire([(520, 380), (X_SCL, 380), (X_SCL, 1030)], C_SCL)
    s.wire([(520, 460), (X_G, 460), (X_G, 1560)], C_GND)
    for (x, y, c) in [(X_3, 220, C_3V3), (X_SDA, 300, C_SDA), (X_SCL, 380, C_SCL), (X_G, 460, C_GND)]:
        s.dot(x, y, c)
    s.text(X_G, 150, "barramento I2C + alimentação dos sensores", 17, MUTE, "400", "start")
    s.text(X_G - 8, 190, "GND", 15, C_GND, "700"); s.text(X_SCL - 8, 190, "SCL", 15, C_SCL, "700")
    s.text(X_SDA - 8, 190, "SDA", 15, C_SDA, "700"); s.text(X_3 - 8, 190, "3V3", 15, C_3V3, "700")

    def device(x, y, w, h, titulo, sub, pins, cor_pin=None):
        s.rect(x, y, w, h, "#e2e8f0", "#475569", 2.5, 12)
        s.text(x + w / 2 + 20, y + 32, titulo, 22, INK, "700", "middle")
        s.text(x + w / 2 + 20, y + 56, sub, 15, MUTE, "400", "middle")
        for (yy, lab) in pins:
            s.pin(x, yy); s.text(x + 18, yy + 6, lab, 16, INK, "600")

    # RTC
    device(1000, 500, 330, 220, "Relógio DS3231", "I2C 0x68 (+ EEPROM 0x57)", [(550, "VCC"), (590, "SDA"), (630, "SCL"), (670, "GND")])
    rh = [(X_3, 550), (X_SDA, 590), (X_SCL, 630), (X_G, 670)]
    # hops (cruzamentos): [(x do fio vertical, y da horizontal)]
    s.wire([(X_3, 550), (1000, 550)], C_3V3); s.dot(X_3, 550, C_3V3, 6)
    s.wire([(X_SDA, 590), (1000, 590)], C_SDA, hops=[(X_3, 590)]); s.dot(X_SDA, 590, C_SDA, 6)
    s.wire([(X_SCL, 630), (1000, 630)], C_SCL, hops=[(X_SDA, 630), (X_3, 630)]); s.dot(X_SCL, 630, C_SCL, 6)
    s.wire([(X_G, 670), (1000, 670)], C_GND, hops=[(X_SCL, 670), (X_SDA, 670), (X_3, 670)]); s.dot(X_G, 670, C_GND, 6)

    # SHT31
    device(1000, 740, 330, 220, "Sonda SHT31 (IP68)", "I2C 0x44 · temperatura e umidade", [(810, "VCC  (vermelho)"), (850, "SDA  (verde)"), (890, "SCL  (amarelo)"), (930, "GND  (preto)")])
    s.wire([(X_3, 810), (1000, 810)], C_3V3); s.dot(X_3, 810, C_3V3, 6)
    s.wire([(X_SDA, 850), (1000, 850)], C_SDA); s.dot(X_SDA, 850, C_SDA, 6)
    s.wire([(X_SCL, 890), (1000, 890)], C_SCL, hops=[(X_SDA, 890)]); s.dot(X_SCL, 890, C_SCL, 6)
    s.wire([(X_G, 930), (1000, 930)], C_GND, hops=[(X_SCL, 930), (X_SDA, 930)]); s.dot(X_G, 930, C_GND, 6)
    # corrigir fim do trilho 3V3 (termina no SHT31, y=790)
    # (o trilho foi desenhado até 770; estende para 790)
    s.wire([(X_3, 770), (X_3, 810)], C_3V3)

    # ADXL345 (módulo)
    mx, my, mw, mh = 1100, 1060, 520, 240
    s.rect(mx - 20, my - 40, mw + 40, mh + 80, "none", "#16a34a", 4, 18, "14 8")
    s.add(f'<rect x="{mx}" y="{my}" width="{mw}" height="{mh}" rx="14" fill="#1e5fa8" stroke="#0b3a6e" stroke-width="3"/>')
    s.add('<rect x="1140" y="1100" width="150" height="110" rx="6" fill="#111" stroke="#444" stroke-width="2"/>')
    s.text(1215, 1148, "ADXL345", 22, "#e5e7eb", "700", "middle"); s.text(1215, 1175, "U1 · acelerômetro 3 eixos", 13, "#cbd5e1", "400", "middle")
    s.add('<rect x="1380" y="1105" width="100" height="70" rx="6" fill="#222" stroke="#555" stroke-width="2"/>')
    s.text(1430, 1136, "regulador", 14, "#e5e7eb", "700", "middle"); s.text(1430, 1156, "5V → 3,3V", 13, "#cbd5e1", "400", "middle")
    s.add('<circle cx="1330" cy="1230" r="9" fill="#ef4444" stroke="#7f1d1d" stroke-width="2"/>'); s.text(1346, 1236, "D1 (LED)", 13, "#e5e7eb")
    # eixos
    s.add('<g stroke="#fff" stroke-width="3" fill="none"><line x1="1530" y1="1230" x2="1530" y2="1160"/><line x1="1530" y1="1230" x2="1590" y2="1200"/><line x1="1530" y1="1230" x2="1500" y2="1200"/></g>')
    s.text(1536, 1152, "Z (cima)", 13, "#fff", "700"); s.text(1592, 1200, "X", 13, "#fff", "700"); s.text(1480, 1196, "Y", 13, "#fff", "700")
    # badge NOVO
    s.add('<rect x="1100" y="1000" width="0" height="0"/>')
    s.rect(1420, 990, 200, 34, "#16a34a", "#166534", 2, 17); s.text(1520, 1013, "NOVO · ADXL345", 17, "#fff", "700", "middle")

    # pinos topo: SCL SDA SDO INT2 INT1
    top = [(1150, "SCL", C_SCL), (1230, "SDA", C_SDA), (1310, "SDO", "#94a3b8"), (1390, "INT2", "#94a3b8"), (1470, "INT1", "#94a3b8")]
    for (x, lab, col) in top:
        s.pin(x, my); s.text(x, my + 26, lab, 16, "#fff", "700", "middle")
    # SDA / SCL do trilho até o módulo
    s.wire([(X_SDA, 1000), (1230, 1000), (1230, my)], C_SDA); s.dot(X_SDA, 1000, C_SDA, 6)
    s.wire([(X_SCL, 1030), (1150, 1030), (1150, my)], C_SCL); s.dot(X_SCL, 1030, C_SCL, 6)
    s.text(1060, 992, "SDA", 14, C_SDA, "700", "end"); s.text(1060, 1022, "SCL", 14, C_SCL, "700", "end")
    # SDO -> GND (contorna pela direita)
    for xx in (1310, 1390, 1470):
        s.text(xx, my - 14, "livre", 13, "#475569", "700", "middle")
    # INT livres

    # pinos base: 5V 3V3 GND VS CS
    bot = [(1150, "5V", C_5V), (1230, "3V3", C_3V3), (1310, "GND", C_GND), (1390, "VS", "#94a3b8"), (1470, "CS", C_3V3)]
    yb = my + mh
    for (x, lab, col) in bot:
        s.pin(x, yb); s.text(x, yb - 12, lab, 16, "#fff", "700", "middle")
    # 5V
    s.wire([(1150, yb), (1150, 1400)], C_5V)
    # GND
    s.wire([(1310, yb), (1310, 1560)], C_GND)
    # CS -> 3V3 do módulo (jumper curto, hop sobre o fio do GND)
    s.wire([(1470, yb), (1470, 1350), (1230, 1350), (1230, yb)], C_3V3, hops=[(1310, 1350)])
    s.text(1330, 1378, "jumper CS → 3V3 (como no esquema do fabricante)", 15, C_3V3, "700")
    s.text(1390, yb + 30, "livre", 13, "#475569", "700", "middle")

    # DC-DC
    s.rect(60, 1300, 240, 320, "#fef3c7", "#a16207", 3, 14)
    s.text(180, 1338, "Conversor", 22, INK, "700", "middle"); s.text(180, 1364, "DC-DC step-down", 22, INK, "700", "middle")
    s.lines(80, 1410, ["entrada 12/24 V DC", "saída 5 V · 3 A"], 15, MUTE, 22)
    for i in range(2):
        pass
    s.text(180, 1490, "alimenta: bobinas dos relés,", 14, MUTE, "400", "middle"); s.text(180, 1510, "display e pino 5V do ESP32", 14, MUTE, "400", "middle")
    s.pin(300, 1400); s.text(318, 1388, "+5V", 16, C_5V, "700")
    s.pin(300, 1560); s.text(318, 1548, "GND", 16, C_GND, "700")
    # lanes 5V e GND
    s.wire([(300, 1400), (1150, 1400)], C_5V, hops=[(X_G, 1400)])
    s.wire([(300, 1560), (1310, 1560)], C_GND); s.dot(1310, 1560, C_GND, 6)
    s.dot(X_G, 1560, C_GND, 6)
    # ESP 5V/GND desce até as lanes
    s.wire([(360, 1120), (360, 1400)], C_5V); s.dot(360, 1400, C_5V, 6)
    s.wire([(440, 1120), (440, 1560)], C_GND, hops=[(440, 1400)]); s.dot(440, 1560, C_GND, 6)
    s.text(1140, 1426, "+5 V do mesmo trilho do relé/display", 14, C_5V, "700", "end")
    s.text(1000, 1584, "GND comum (todos os GND ligados juntos)", 15, C_GND, "700")

    # painel direito
    px = 2000
    s.rect(px - 20, 150, 530, 470, "#fff", "#cbd5e1", 1.5, 12)
    s.text(px, 190, "Tabela de ligação do ADXL345", 22, INK, "700")
    rows = [("SCL", "GPIO9 (SCL)", "amarelo", C_SCL), ("SDA", "GPIO8 (SDA)", "verde", C_SDA), ("5V", "+5 V do DC-DC", "vermelho", C_5V),
            ("GND", "GND comum", "preto", C_GND), ("CS", "pino 3V3 do módulo (jumper)", "laranja", C_3V3), ("3V3", "SAÍDA 3,3 V (só o jumper do CS)", "—", "#94a3b8"),
            ("SDO", "livre (R4 0 Ω já põe no GND)", "—", "#94a3b8"), ("VS", "livre (R5 0 Ω já liga ao 3,3 V)", "—", "#94a3b8"), ("INT1/INT2", "livres (rótulo IN1/IN2)", "—", "#94a3b8")]
    s.text(px, 226, "Pino do módulo", 14, MUTE, "700"); s.text(px + 160, 226, "Vai para", 14, MUTE, "700"); s.text(px + 420, 226, "Fio", 14, MUTE, "700")
    for i, (a, b, c, col) in enumerate(rows):
        yy = 258 + i * 36
        s.add(f'<rect x="{px-8}" y="{yy-22}" width="6" height="28" fill="{col}" rx="3"/>')
        s.text(px + 8, yy, a, 17, INK, "700"); s.text(px + 160, yy, b, 16, INK); s.text(px + 420, yy, c, 16, MUTE)
    s.rect(px - 20, 640, 530, 250, "#ecfdf5", "#16a34a", 2, 12)
    s.text(px, 676, "O que a placa já faz (esquemático do fabricante)", 19, "#166534", "700")
    s.lines(px, 706, ["· U2 ME6206-33: regulador 3,3 V alimentado pelo pino 5V.",
                      "· Pino 3V3 é SAÍDA 3,3 V — não ligue o 3V3 do ESP32 nele.",
                      "· R2/R3 10 kΩ: pull-ups de SCL/SDA em 3,3 V.",
                      "· R4 0 Ω: SDO ligado ao GND → endereço I2C 0x53.",
                      "· R5 0 Ω: VS ligado ao 3,3 V (VS e SDO ficam livres).",
                      "· R6 10 kΩ: CS puxado a 3,3 V (o jumper CS→3V3 reforça).",
                      "· D1 + R1: LED de energia."], 15, "#14532d", 24)

    s.rect(px - 20, 910, 530, 340, "#fff7ed", "#ea580c", 2, 12)
    s.text(px, 946, "Montagem em cima da tampa", 21, "#9a3412", "700")
    s.lines(px, 978, ["· Fixe o módulo numa massa pesada e rígida (parafuso ou",
                       "  cola forte) — o peso mantém o sensor firme na tampa.",
                       "· Chip virado PARA CIMA: o eixo Z fica na vertical.",
                       "  Marque X/Y na massa para lembrar a orientação.",
                       "· Cabo I2C curto (até ~50 cm), com folga em laço e",
                       "  preso na dobradiça para não puxar o módulo.",
                       "· Longe dos fios de 220 V (resistência/bomba/LED).",
                       "· Se o cabo for longo ou ruidoso: I2C a 100 kHz.",
                       "· Endereço 0x53 não conflita com 0x44 (SHT31) e",
                       "  0x68/0x57 (relógio). MCP23017 (0x20) cabe no mesmo par."], 15, "#7c2d12", 24)

    s.rect(px - 20, 1270, 530, 170, "#fef2f2", "#dc2626", 2, 12)
    s.text(px, 1304, "Antes de ligar / conferir", 19, "#991b1b", "700")
    s.lines(px, 1332, ["· Tudo desenergizado ao mexer na fiação.",
                       "· LED D1 aceso = módulo energizado com 3,3 V.",
                       "· Um scanner I2C deve achar 0x53, 0x44 e 0x68.",
                       "· SDA/SCL invertidos: o módulo só não responde."], 15, "#7f1d1d", 22)

    legenda(s, px - 20, 1480)
    s.save("01_ligacao_ADXL345.svg")


# ═══════════════════════════ 02 — visão geral ═══════════════════════════
def geral():
    s = Svg(2400, 1380)
    s.text(60, 66, "ChocaIoT — visão geral da fiação (estado atual + itens futuros)", 38, INK, "700")
    s.text(60, 102, "Fonte: pinout documentado no projeto (CLAUDE.md). Linhas tracejadas = ainda não instalado. Confirme a sua fiação real.", 18, MUTE)

    # alimentação
    s.rect(60, 160, 300, 150, "#fef3c7", "#a16207", 3, 14)
    s.text(210, 200, "Fonte 12/24 V DC", 22, INK, "700", "middle"); s.text(210, 228, "(entrada geral)", 15, MUTE, "400", "middle")
    s.text(210, 262, "12 V também alimenta as cargas", 14, MUTE, "400", "middle"); s.text(210, 282, "CH7/CH8 (cooler, fans) via relé", 14, MUTE, "400", "middle")
    s.rect(60, 380, 300, 150, "#fde68a", "#a16207", 3, 14)
    s.text(210, 420, "DC-DC step-down", 22, INK, "700", "middle"); s.text(210, 448, "12/24 V → 5 V · 3 A", 17, MUTE, "400", "middle")
    s.text(210, 484, "alimenta relé (VCC), display", 14, MUTE, "400", "middle"); s.text(210, 504, "e o pino 5V do ESP32", 14, MUTE, "400", "middle")
    s.wire([(210, 310), (210, 380)], C_12V)
    s.text(224, 350, "12/24 V", 14, C_12V, "700")

    # ESP32
    s.rect(560, 160, 520, 800, "#e2e8f0", "#334155", 3, 14)
    s.text(820, 204, "ESP32-S3  (controlador)", 28, INK, "700", "middle"); s.text(820, 232, "YD-ESP32-S3 44P · N16R8 · Wi-Fi · LittleFS", 16, MUTE, "400", "middle")
    s.text(580, 280, "Barramento I2C  (GPIO8 SDA · GPIO9 SCL)", 18, C_SDA, "700")
    s.text(580, 520, "Barramento SPI compartilhado", 18, "#0369a1", "700")
    s.text(580, 560, "GPIO11 MOSI · GPIO13 MISO · GPIO12 SCK", 15, INK)
    s.text(580, 584, "CS display 10 · DC 14 · RST 21 · CS touch 48 · CS SD 18", 15, INK)
    s.text(580, 700, "Saídas para o módulo de relé (ativo em nível BAIXO)", 18, C_5V, "700")
    s.text(580, 726, "IN1 GPIO1 · IN2 GPIO2 · IN3 GPIO38 · IN4 GPIO39", 15, INK)
    s.text(580, 748, "IN5 GPIO40 · IN6 GPIO41 · IN7 GPIO42 · IN8 GPIO7", 15, INK)
    s.text(580, 800, "Reservados / futuros (tracejado)", 18, "#7c3aed", "700")
    s.text(580, 826, "GPIO47 backlight (IRF520) · GPIO4/5/6 PWM dos fans", 15, INK)
    s.text(580, 848, "GPIO15/16/17 tacômetros · UART do MH-Z19B (CO2)", 15, INK)
    s.text(580, 900, "Alimentação: pino 5V ← DC-DC 5 V · GND comum", 15, MUTE)
    s.wire([(360, 455), (560, 455)], C_5V); s.dot(560, 455, C_5V); s.text(380, 445, "+5 V", 15, C_5V, "700")
    s.wire([(360, 505), (560, 505)], C_GND); s.dot(560, 505, C_GND); s.text(364, 532, "GND comum", 14, C_GND, "700")

    # I2C
    def box(x, y, w, h, titulo, sub, fill="#e2e8f0", stroke="#475569", dash=None, tcolor=INK):
        s.rect(x, y, w, h, fill, stroke, 2.5, 12, dash)
        s.text(x + w / 2, y + 30, titulo, 20, tcolor, "700", "middle")
        for i, t in enumerate(sub):
            s.text(x + w / 2, y + 56 + i * 20, t, 14, MUTE, "400", "middle")
    s.text(1180, 150, "I2C (mesmo par de fios)", 18, C_SDA, "700")
    box(1180, 168, 340, 90, "Relógio DS3231", ["0x68 · bateria LIR2032"])
    box(1180, 276, 340, 90, "Sonda SHT31 (IP68)", ["0x44 · temperatura/umidade"])
    box(1180, 384, 340, 90, "ADXL345 (NOVO)", ["0x53 · vibração / inclinação"], "#dcfce7", "#16a34a", None, "#166534")
    box(1180, 492, 340, 90, "MCP23017 (futuro)", ["0x20 · expansor de GPIO"], "#fff", "#7c3aed", "10 6", "#6d28d9")
    s.wire([(1080, 300), (1130, 300), (1130, 213), (1180, 213)], C_SDA); s.wire([(1130, 300), (1130, 537), (1180, 537)], C_SDA, dash="10 6")
    s.wire([(1130, 321), (1180, 321)], C_SDA); s.wire([(1130, 429), (1180, 429)], C_SDA)
    s.dot(1130, 300, C_SDA); s.dot(1130, 321, C_SDA, 5); s.dot(1130, 429, C_SDA, 5)
    s.text(1088, 290, "I2C", 14, C_SDA, "700")

    # SPI
    box(1180, 640, 340, 100, "Display ILI9341 3,2\"", ["320×240 · touch XPT2046 (CS 48)", "VCC e LED em 5 V"])
    box(1180, 756, 340, 80, "microSD", ["CS GPIO18 · divide MOSI/MISO/SCK"])
    s.wire([(1080, 560), (1130, 560), (1130, 690), (1180, 690)], "#0369a1"); s.wire([(1130, 560), (1130, 796), (1180, 796)], "#0369a1")
    s.dot(1130, 560, "#0369a1")

    # relé
    s.rect(560, 1040, 520, 270, "#fee2e2", "#b91c1c", 3, 14)
    s.text(820, 1082, "Módulo de relé 8 canais (5 V)", 24, INK, "700", "middle")
    s.text(820, 1110, "SRD-05VDC-SL-C · contatos 10 A / 250 V", 15, MUTE, "400", "middle")
    s.text(820, 1150, "bobinas ← 5 V do DC-DC · IN1…IN8 ← ESP32", 15, INK, "400", "middle")
    s.text(820, 1190, "Todos os INs iniciam em HIGH (desligado) no setup()", 15, INK, "400", "middle")
    s.text(820, 1230, "⚠ Na gravação os pinos ficam soltos: tire a bomba (220 V) da", 15, "#991b1b", "700", "middle")
    s.text(820, 1252, "tomada e, de preferência, coloque pull-up 10 kΩ IN→3V3", 15, "#991b1b", "700", "middle")
    s.wire([(820, 960), (820, 1040)], C_5V, 6)
    s.text(836, 1008, "IN1…IN8 (GPIO 1,2,38,39,40,41,42,7)", 15, C_5V, "700")
    s.wire([(360, 505), (440, 505), (440, 1200), (560, 1200)], C_GND, 4, hops=[]); s.dot(440, 505, C_GND, 5)
    s.wire([(360, 455), (480, 455), (480, 1140), (560, 1140)], C_5V, 4, hops=[]); s.dot(480, 455, C_5V, 5)

    # cargas
    cargas = [("CH1", "Resistência 100 W", "220 V AC", "#dc2626"), ("CH2", "Umidificador ultrassônico", "5 V DC", "#16a34a"),
              ("CH3", "Bomba de água VA-BA01 (3 W)", "220 V AC · máx. 8 s", "#dc2626"), ("CH4", "Bomba de ar", "3,7 V DC", "#16a34a"),
              ("CH5", "LED do ovoscópio", "220 V AC", "#dc2626"), ("CH6", "Áudio (MAX98357)", "5 V · reserva/áudio", "#16a34a"),
              ("CH7", "Cooler do sistema (auto 55/48 °C)", "12 V DC", "#7c3aed"), ("CH8", "Fans de circulação", "12 V DC", "#7c3aed")]
    s.text(1600, 1020, "Cargas nos contatos dos relés", 20, INK, "700")
    for i, (ch, nome, v, col) in enumerate(cargas):
        y = 1038 + i * 36
        s.rect(1600, y, 760, 30, "#fff", col, 2, 8)
        s.text(1614, y + 21, ch, 15, col, "700"); s.text(1666, y + 21, nome, 15, INK); s.text(2350, y + 21, v, 15, col, "700", "end")
        pass
    s.wire([(1080, 1175), (1600, 1175)], "#b91c1c", 4, dash="2 8"); s.text(1090, 1165, "COM/NA de cada relé → carga", 15, "#991b1b", "700")

    # câmera + futuros
    box(1600, 160, 360, 120, "ESP32-S3-CAM + OV5640", ["projeto separado · HTTP na rede local", "(foto/stream; sem fio ao controlador)"], "#fff", "#64748b", "10 6")
    box(1600, 310, 360, 100, "Módulos IRF520 (futuro)", ["backlight (1) + fans PWM (3)", "chegam entre 3 e 24/out"], "#fff", "#7c3aed", "10 6", "#6d28d9")
    box(1600, 440, 360, 100, "MH-Z19B — CO₂ (futuro)", ["UART + 5 V · chega entre 3 e 24/out"], "#fff", "#7c3aed", "10 6", "#6d28d9")
    s.wire([(1080, 880), (1560, 880), (1560, 360), (1600, 360)], "#7c3aed", 3, dash="10 6")
    s.wire([(1560, 490), (1600, 490)], "#7c3aed", 3, dash="10 6")
    s.text(1090, 872, "GPIO 47 · 4/5/6 · 15/16/17 · UART", 14, "#6d28d9", "700")

    # Nuvem
    box(2010, 160, 340, 120, "Wi-Fi → Firebase", ["Firestore · FCM (push)", "PWA SEU-PROJETO.web.app"], "#e0f2fe", "#0369a1")
    s.wire([(1080, 190), (1100, 190)], "#0369a1", 1)

    legenda(s, 1980, 330)
    s.save("02_visao_geral_sistema.svg")


adxl()
geral()
print("ok")
