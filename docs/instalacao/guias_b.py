# -*- coding: utf-8 -*-
"""Guias 06-08 (agua, aquecedor/seguranca, quadro eletronico). Importado por gerar_instalacao.py"""
from lib_escuro import *


def d06(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação do umidificador e da bomba de água",
             "Reservatório externo de 5 L → bomba submersa → mangueira → reservatório interno (~150 ml) com ladrão. Sem sifão, sem transbordar.")
    # galao externo + bomba
    t.jarra(110, 250, 220, 300, 0.78)
    t.texto(220, 590, "reservatório externo · 5 L", 16, MUT, "700", "middle")
    t.rect(170, 500, 100, 36, "#475569", "#94a3b8", 2, 6)
    t.texto(220, 524, "BOMBA", 15, TXT, "700", "middle")
    # cabo da bomba (220 V) com laco
    t.caminho([(240, 500), (240, 440), (360, 440), (360, 470), (420, 470), (420, 380), (500, 380)], PWR, 3, "7 5")
    t.texto(340, 330, "cabo 220 V da bomba", 13, PWR, "700")
    t.texto(340, 348, "→ relé CH3 (com laço)", 13, PWR)
    # valvula de retencao + mangueira
    t.caminho([(200, 500), (200, 200), (480, 200)], WATER, 6)
    t.circulo(200, 440, 15, "#0b2a3d", WATER, 3); t.seta(192, 444, 208, 436, WATER, 2)
    t.texto(60, 446, "válvula de retenção", 14, WATER)
    t.texto(60, 176, "mangueira sobe acima do nível d'água (sem sifão)", 14, WATER)
    # parede da caixa
    t.rect(480, 150, 30, 600, "#1a2342", "#4a5f9c", 4, 6)
    t.texto(495, 770, "parede da caixa", 14, MUT, anchor="middle")
    t.caminho([(510, 200), (780, 200), (780, 300)], WATER, 6)
    t.etiqueta(560, 160, "ponta ACIMA do nível máximo", OK, 15)
    # reservatorio interno
    t.rect(660, 300, 260, 200, "#0f1730", "#6b86c9", 4, 12)
    t.rect(664, 380, 252, 116, "url(#gagua)", "none", 0, 8, op=0.9)
    t.linha(660, 380, 920, 380, WATER, 2, "6 5")
    t.texto(930, 386, "nível máx.", 13, WATER)
    t.texto(790, 530, "reservatório interno (~150 ml)", 15, MUT, "700", "middle")
    # umidificador ultrassonico + nevoa
    t.rect(740, 450, 100, 36, "#334155", "#94a3b8", 2, 6); t.texto(790, 473, "umidificador", 13, TXT, "700", "middle")
    for x in (760, 790, 820):
        t.caminho([(x, 440), (x - 8, 410), (x + 8, 380), (x, 350), (x - 6, 320)], "#bae6fd", 3, "3 4")
    # ladrao
    t.caminho([(920, 430), (960, 430), (960, 640), (330, 640), (330, 470)], "#7dd3fc", 4, "8 6", seta=True)
    t.texto(640, 660, "ladrão: o excesso volta ao galão por gravidade", 15, "#7dd3fc", anchor="middle")
    # teste do copo
    t.texto(60, 740, "Teste do copo (fora da caixa)", 18, MUT, "700")
    t.rect(100, 790, 90, 110, "#0f1730", "#8da0c8", 3, 10)
    t.rect(103, 850, 84, 47, "url(#gagua)", "none", 0, 6, op=0.9)
    t.caminho([(40, 780), (140, 780), (140, 800)], WATER, 5)
    t.texto(210, 810, "1) bombeie ~5 s e meça os ml  →  vazão (ml/s)", 15, TXT)
    t.texto(210, 838, "2) mangueira vazia: cronometre até a água sair", 15, TXT)
    t.texto(210, 866, "3) informe em Configurações → Hardware (pág. 3)", 15, V5, "700")

    t.painel(1000, 150, 750, 890, "Passo a passo e limites do firmware")
    y = t.passos(1030, 228, [
        "Posicione o galão ao lado da caixa. A bomba fica submersa e a mangueira sobe acima do nível da água antes de entrar na caixa.",
        "Coloque uma válvula de retenção perto da bomba; sem ela a mangueira esvazia de volta e cada pulso gasta ~3 s só para encher.",
        "A ponta da mangueira termina ACIMA do nível máximo do reservatório interno; instale o ladrão devolvendo o excesso ao galão.",
        "Meça com o teste do copo: tempo de enchimento (s) e vazão (ml/s) e informe em Hardware. Sem a medida, o firmware não bombeia.",
        "CH3 é 220 V (bomba classe II): fusível, DR, emendas isoladas, cabo com laço de gotejamento e longe da névoa.",
        "O comando do CH3 existe só no aparelho: o app e a nuvem nunca ligam a bomba.",
    ], 700, WATER, 16)
    t.texto(1030, y + 16, "Como o firmware dosa a água", 19, TXT, "700")
    y = t.linhas(1030, y + 48, [
        "• pulso = tempo de enchimento + 15 ml ÷ vazão (máximo 8 s, vigiado em ms);",
        "• só age após ~6,5 min com o umidificador ligado e a umidade sem reagir;",
        "• até 4 pulsos por sequência, 1 sequência por hora, 120 ml por dia;",
        "• sem hora válida (NTP/RTC) ele não bombeia; contadores ficam salvos.",
    ], 16, MUT, 26)
    t.faz_nao_faz(1030, y + 16, 690,
                  ["Fixar a bomba para não virar nem aspirar ar", "Testar com a ponta num copo antes de ligar na caixa"],
                  ["Deixar a bomba funcionar a seco", "Pôr a ponta da mangueira abaixo da água (sifão)"], 15)
    saida(t, "06_instalacao_umidificador_e_bomba")


def d07(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação do aquecedor e das proteções elétricas (220 V)",
             "O firmware comanda o relé, mas a segurança precisa de proteções em hardware que independam do software.")
    # diagrama unifilar
    t.texto(60, 150, "Esquema unifilar da carga de 220 V", 18, MUT, "700")
    cx = [100, 320, 540, 760, 980]
    nomes = [("Tomada", "L · N · PE", MUT), ("DR + disjuntor", "proteção geral", BAD), ("Fusível", "2 A (carga ≈ 100 W)", V5),
             ("Relé CH1", "contato NA na FASE", PWR), ("Fusível térmico", "ou termostato, em série", HEAT)]
    for x, (n, d, c) in zip(cx, nomes):
        t.rect(x, 190, 190, 92, CARD2, c, 3, 12)
        t.texto(x + 95, 228, n, 15 if len(n) > 14 else 17, c, "700", "middle")
        t.texto(x + 95, 256, d, 13, MUT, anchor="middle")
    for a, b in zip(cx, cx[1:]):
        t.caminho([(a + 190, 236), (b, 236)], PWR, 5)
    t.rect(1200, 190, 150, 92, "#2a1409", HEAT, 3, 12)
    t.texto(1275, 228, "Resistência", 17, HEAT, "700", "middle"); t.texto(1275, 256, "/ lâmpada", 13, MUT, anchor="middle")
    t.caminho([(1170, 236), (1200, 236)], PWR, 5)
    t.texto(140, 180, "FASE (L)", 14, PWR, "700")
    # neutro
    t.caminho([(190, 282), (190, 330), (1275, 330), (1275, 282)], TXT, 4)
    t.texto(700, 356, "NEUTRO (N) — direto, sem interromper", 15, MUT, anchor="middle")
    # terra
    t.caminho([(120, 282), (120, 420), (1000, 420)], OK, 4, "8 6")
    t.rect(1000, 396, 190, 48, "#0f2a22", OK, 2, 8)
    t.texto(1095, 427, "TERRA → partes metálicas", 14, OK, "700", "middle")
    # bobina / ESP
    t.rect(760, 470, 190, 60, "#10261f", "#1fbf8e", 2, 8); t.texto(855, 508, "ESP32 · GPIO1 → IN1", 15, "#5eead4", "700", "middle")
    t.seta(855, 470, 855, 286, "#5eead4", 3, "6 5")
    t.texto(840, 380, "sinal 3,3 V (pull-up 10 kΩ)", 13, "#5eead4", anchor="end")
    # posicionamento
    t.texto(60, 600, "Posicionamento da resistência", 18, MUT, "700")
    t.rect(100, 650, 600, 360, "#0f1730", "#4a5f9c", 4, 14)
    t.lampada(220, 670, 260)
    t.rect(220, 650, 260, 20, "#374151", "#94a3b8", 2, 4)
    t.texto(350, 645, "refletor / suporte metálico", 13, MUT, anchor="middle")
    t.rect(500, 690, 120, 50, "#1f2937", HEAT, 2, 8); t.texto(560, 712, "fusível", 13, TXT, "700", "middle"); t.texto(560, 730, "térmico", 13, TXT, "700", "middle")
    t.linha(480, 700, 500, 710, HEAT, 2)
    t.linha(230, 790, 230, 900, MUT, 2, "5 5"); t.texto(250, 850, "≥ 12 cm", 16, V5, "700")
    t.rect(180, 900, 400, 20, "#2a3558", "#6b86c9", 2, 6)
    t.ovos(220, 876, 6, 54)
    t.texto(380, 960, "nunca encostar no isopor/plástico", 15, BAD, anchor="middle")

    t.painel(1000, 480, 750, 560, "Regras de instalação")
    y = t.passos(1030, 560, [
        "Corte a energia e confirme com multímetro antes de mexer.",
        "Fase → DR/disjuntor → fusível → contato NA do relé → termostato/fusível térmico → carga. Neutro direto à carga.",
        "A proteção térmica em série vale mesmo se o firmware travar (o relé colado nunca deve superaquecer a caixa).",
        "Fios de 220 V longe do I2C, do SPI e da água; terminais com tampa; emendas isoladas.",
        "Use a potência adequada: 100 W numa caixa de 3 L é ~10× o necessário; informe potência e volume em Hardware.",
    ], 700, PWR, 16)
    t.etiqueta(1030, y + 24, "Primeiro teste: lâmpada comum no lugar da resistência", V5, 15)
    saida(t, "07_instalacao_aquecedor_e_protecoes")


def d08(saida, W=1800, H=1100):
    t = Tela(W, H, "Quadro eletrônico: baixa tensão separada do 220 V",
             "Dois compartimentos com barreira física; sinais de 3,3 V atravessam por uma abertura; pull-up de 10 kΩ nos relés.")
    # gabinete
    t.rect(70, 150, 1000, 700, "#0c1124", "#4a5f9c", 5, 18)
    t.rect(550, 160, 14, 680, "#3a4a7a", "#3a4a7a", 1, 4)
    t.texto(310, 190, "BAIXA TENSÃO  (3,3 V · 5 V · 12 V)", 18, OK, "700", "middle")
    t.texto(810, 190, "220 V  (sempre tampado)", 18, PWR, "700", "middle")
    # esquerda
    t.esp32(100, 230, 250, 130, "ESP32-S3")
    t.texto(225, 380, "bornes: 3V3 · GND · GPIO", 14, MUT, anchor="middle")
    t.rect(390, 230, 140, 90, "#3a2a08", V5, 3, 10)
    t.texto(460, 266, "DC-DC", 20, V5, "700", "middle"); t.texto(460, 292, "12/24 V → 5 V", 14, V5, anchor="middle")
    t.rect(390, 340, 140, 50, "#10261f", "#1fbf8e", 2, 8)
    t.texto(460, 362, "470 µF + 100 nF", 14, "#5eead4", "700", "middle"); t.texto(460, 380, "no CO₂ (Vin–GND)", 12, MUT, anchor="middle")
    t.rele(100, 450, 430, 110, 8)
    t.texto(100, 590, "módulo de relés 8 canais (bobinas 5 V)", 14, MUT)
    t.rect(100, 640, 430, 70, "#2b1d08", V5, 2, 10)
    t.texto(315, 668, "rede SIP 10 kΩ — comum no 3V3", 17, V5, "700", "middle")
    t.texto(315, 694, "cada resistor: GPIO → IN (mantém o relé desligado no reset)", 13, MUT, anchor="middle")
    t.seta(315, 640, 315, 566, V5, 3)
    # abertura da barreira
    t.rect(540, 480, 34, 70, "#05070f", "#8da0c8", 2, 6)
    t.texto(557, 572, "abertura", 12, MUT, anchor="middle")
    t.caminho([(530, 500), (574, 500)], "#5eead4", 3)
    # direita
    t.rect(620, 230, 400, 90, "#2b0f19", "#b91c4a", 3, 10)
    t.texto(820, 266, "bornes do relé: COM · NA · NF", 17, TXT, "700", "middle")
    t.texto(820, 292, "contato NA na FASE da carga", 14, MUT, anchor="middle")
    t.rect(620, 350, 190, 80, "#1f2937", V5, 2, 8); t.texto(715, 384, "fusível 2 A", 17, V5, "700", "middle"); t.texto(715, 408, "carga 220 V", 13, MUT, anchor="middle")
    t.rect(830, 350, 190, 80, "#0f2a22", OK, 2, 8); t.texto(925, 384, "TERRA (PE)", 17, OK, "700", "middle"); t.texto(925, 408, "carcaça metálica", 13, MUT, anchor="middle")
    t.rect(620, 470, 400, 120, CARD, LINE, 2, 10)
    t.linhas(640, 504, ["CH1 resistência · CH3 bomba · CH5 LED:  220 V", "CH2 umidificador 5 V · CH4 bomba de ar 3,7 V", "CH7 cooler 12 V · CH8 fans 12 V"], 15, MUT, 28)
    t.texto(820, 640, "cabos de 220 V só neste lado", 16, PWR, "700", "middle")
    # tabela IN -> GPIO
    t.painel(1100, 150, 650, 330, "Tabela IN do relé → GPIO do ESP32")
    linhas = [("IN1", "GPIO1", "resistência"), ("IN2", "GPIO2", "umidificador"), ("IN3", "GPIO38", "bomba de água"), ("IN4", "GPIO39", "bomba de ar"),
              ("IN5", "GPIO40", "LED ovoscópio"), ("IN6", "GPIO41", "reservado"), ("IN7", "GPIO42", "cooler"), ("IN8", "GPIO7", "fans (+12 V)")]
    for i, (a, b, c) in enumerate(linhas):
        col = i // 4; row = i % 4
        x = 1130 + col * 320; yy = 230 + row * 56
        t.texto(x, yy, a, 18, PWR, "800"); t.texto(x + 56, yy, "→ " + b, 18, V5, "700"); t.texto(x + 56, yy + 22, c, 13, MUT)
    t.painel(1100, 510, 650, 530, "Passo a passo")
    t.passos(1130, 590, [
        "Monte o gabinete com a barreira entre baixa tensão e 220 V; só a abertura da barreira passa os fios de sinal.",
        "Alimente o ESP32 e o display pelo DC-DC (5 V). Meça 4,9–5,1 V na saída, não no pino '5V' do adaptador (diodo).",
        "Ligue um resistor de 10 kΩ de cada IN ao 3V3 (rede SIP). Teste: RST com cargas fora da tomada — nenhum LED de relé acende.",
        "Passe os fios de comando (IN1–IN8) pela abertura; sinais com fios curtos e separados dos de potência.",
        "Feche e rotule os compartimentos; deixe a tampa do 220 V com parafuso (não encaixe).",
    ], 580, V5, 16)
    saida(t, "08_quadro_eletronico_e_reles")
