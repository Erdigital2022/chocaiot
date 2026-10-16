# -*- coding: utf-8 -*-
"""Gera os guias de instalacao em TEMA ESCURO (SVG + PNG) do ChocaIoT.
Uso:  python gerar_instalacao.py            (todos)
      python gerar_instalacao.py 1 3 5      (so os numeros pedidos)
Requer: pillow e Google Chrome (para rasterizar o SVG)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib_escuro import *

AQUI = os.path.dirname(os.path.abspath(__file__))
W, H = 1800, 1100


def saida(t, nome):
    svg = os.path.join(AQUI, nome + ".svg")
    t.salvar(svg)
    svg_para_png(svg, os.path.join(AQUI, nome + ".png"), t.w, t.h)
    print("ok", nome)


# ════════════════════════════════════════════════════════════════════
# 01 — Layout interno da caixa (corte lateral)
# ════════════════════════════════════════════════════════════════════
def d01():
    t = Tela(W, H, "Layout interno da chocadeira (corte lateral)",
             "Onde instalar cada item — resistência no alto, fans por função, sensores na altura dos ovos, água e eletrônica fora da área quente.")
    # caixa
    t.rect(110, 262, 800, 590, "#0f1730", "#4a5f9c", 6, 18)
    t.rect(98, 228, 824, 36, "#1a2342", "#4a5f9c", 4, 8)
    t.texto(110, 252, "tampa", 14, MUT)
    # ADXL na tampa (peso)
    t.rect(740, 198, 96, 32, "#111827", "#8da0c8", 3, 5)
    t.texto(788, 219, "ADXL345", 13, SIG, "700", "middle")
    t.num(868, 214, 10)
    # resistencia
    t.lampada(250, 270, 240)
    t.num(228, 296, 1, HEAT)
    t.texto(370, 392, "calor ↓", 14, HEAT, anchor="middle")
    # bandeja e ovos
    t.rect(330, 604, 440, 22, "#2a3558", "#6b86c9", 2, 6)
    t.ovos(374, 578, 8, 50)
    t.texto(550, 656, "bandeja de ovos", 14, MUT, anchor="middle")
    # SHT31
    t.sonda(840, 560)
    t.num(878, 552, 6, OK)
    t.caminho([(840, 600), (840, 640), (905, 640)], MUT, 3)
    # CO2
    t.rect(150, 520, 120, 70, "none", V5, 2, 10, dash="6 5")
    t.co2(166, 534)
    t.num(130, 540, 7, V5)
    t.texto(210, 612, "capa aberta embaixo", 13, V5, anchor="middle")
    # fan 3 renovacao (alto, sopra para a parede com 3 furos)
    t.fan(836, 330, 26)
    t.num(790, 292, 4, VIO)
    for yy in (306, 330, 354):
        t.circulo(910, yy, 6, "#05070f", "#8da0c8", 2)
        t.seta(918, yy, 985, yy, AIR, 3, "4 5")
    t.seta(866, 330, 898, 330, AIR, 4)
    t.texto(1000, 336, "saída de ar", 14, AIR)
    # entrada de ar (inferior, lado oposto)
    t.circulo(110, 820, 7, "#05070f", "#8da0c8", 2)
    t.seta(40, 820, 128, 820, OK, 4)
    t.texto(40, 806, "ar novo", 14, OK)
    t.num(70, 850, 5, OK)
    # fan 1 circulacao (piso) -> paredes
    t.fan(300, 812, 24)
    t.num(262, 770, 2, AIR)
    t.caminho([(270, 812), (170, 812), (140, 780), (140, 690)], AIR, 4, "6 5", seta=True)
    t.caminho([(330, 812), (420, 812)], AIR, 4, "6 5", seta=True)
    # fan 2 resfriamento (5 cm do piso, para cima)
    t.fan(500, 760, 24)
    t.num(462, 722, 3, VIO)
    t.seta(500, 726, 500, 664, AIR, 4)
    t.texto(520, 700, "5 cm do piso", 13, MUT)
    # umidificador + reservatorio interno
    t.rect(640, 800, 190, 46, "url(#gagua)", WATER, 3, 8, op=0.9)
    t.texto(735, 830, "reservatório ~150 ml", 13, BG, "700", "middle")
    for xx in (680, 735, 790):
        t.caminho([(xx, 790), (xx - 8, 770), (xx + 8, 750), (xx, 730)], WATER, 3, "3 4")
    t.num(612, 826, 8, WATER)
    # mangueira + ladrao + jarra externa
    t.jarra(990, 700, 170, 210, 0.7)
    t.rect(1010, 880, 56, 24, "#475569", "#94a3b8", 2, 4)
    t.texto(1038, 896, "bomba", 12, TXT, "700", "middle")
    t.num(1075, 676, 9, WATER)
    t.caminho([(1038, 880), (1038, 650), (930, 650)], WATER, 5)
    t.caminho([(930, 650), (905, 650)], WATER, 5)
    t.caminho([(905, 650), (790, 650), (790, 798)], WATER, 5)
    t.circulo(910, 650, 6, "#05070f", "#8da0c8", 2)
    t.texto(1060, 636, "mangueira acima do nível", 13, WATER)
    t.caminho([(830, 836), (905, 836), (990, 836)], "#7dd3fc", 3, "7 5", seta=True)
    t.texto(912, 824, "ladrão", 13, "#7dd3fc")
    # eletronica
    t.rect(110, 900, 800, 150, "#0c1124", "#3a4a7a", 3, 12)
    t.texto(124, 924, "caixa eletrônica (fora da área úmida/quente)", 15, MUT, "700")
    t.esp32(130, 936, 200, 96, "ESP32-S3")
    t.rele(350, 936, 300, 96)
    t.rect(680, 944, 110, 80, "#3a2a08", V5, 2, 8)
    t.texto(735, 980, "DC-DC", 17, V5, "700", "middle")
    t.texto(735, 1002, "12→5 V", 14, V5, anchor="middle")
    t.num(100, 940, 11, MUT)
    for xx in (560, 590, 620):
        t.caminho([(xx, 936), (xx, 862)], PWR, 2, "4 5")
    # legenda
    t.painel(1230, 150, 520, 900, "Legenda e regras de posição")
    L = [
        ("1", HEAT, "Resistência / lâmpada cerâmica: no alto, ≥ 12 cm acima dos ovos, longe de plástico e isopor. Proteção térmica independente do firmware."),
        ("2", AIR, "Fan 1 · Circulação: no piso, empurra o ar quente para as paredes."),
        ("3", VIO, "Fan 2 · Resfriamento: ~5 cm do piso, empurrando para cima."),
        ("4", VIO, "Fan 3 · Renovação: no alto, soprando para a parede com 3 furos (saída)."),
        ("5", OK, "Entrada de ar: furo baixo no lado oposto, com tela fina contra insetos."),
        ("6", OK, "SHT31: na altura dos ovos, 5–10 cm do aquecedor e fora do jato do umidificador."),
        ("7", V5, "MH-Z19C (CO₂): na altura dos ovos, janela de lado ou para baixo, com capa aberta embaixo."),
        ("8", WATER, "Umidificador com reservatório interno (~150 ml) e ladrão (extravasor)."),
        ("9", WATER, "Reservatório externo + bomba: mangueira ACIMA do nível da água (sem sifão)."),
        ("10", "#e2e8f0", "ADXL345 sobre a tampa como peso: ao ser retirado, \"tampa aberta\"."),
        ("11", MUT, "Eletrônica fora da caixa, longe do vapor; cabos de 220 V separados dos de sinal."),
    ]
    y = 238
    for n, cor, txt in L:
        t.num(1262, y - 6, n, cor)
        y = t.par(1292, y, txt, 440, 16) + 10
    saida(t, "01_layout_interno_da_caixa")


# ════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    import guias_a
    try:
        import guias_b
    except ImportError:
        guias_b = None
    fn = {1: d01,
          2: lambda: guias_a.d02(saida), 3: lambda: guias_a.d03(saida), 4: lambda: guias_a.d04(saida), 5: lambda: guias_a.d05(saida)}
    if guias_b:
        fn.update({6: lambda: guias_b.d06(saida), 7: lambda: guias_b.d07(saida), 8: lambda: guias_b.d08(saida)})
    pedidos = [int(a) for a in sys.argv[1:]] or sorted(fn)
    for k in pedidos:
        if k in fn: fn[k]()
