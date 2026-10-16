# -*- coding: utf-8 -*-
"""Guias 02-05 (sensores e fans). Importado por gerar_instalacao.py"""
from lib_escuro import *


def d02(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação do sensor de temperatura e umidade (SHT31)",
             "Medir o ar que os ovos sentem: nem colado no aquecedor, nem no jato do umidificador ou dos fans.")
    # vista de cima da caixa
    t.texto(60, 150, "Vista de cima", 18, MUT, "700")
    t.rect(70, 170, 780, 500, "#0f1730", "#4a5f9c", 5, 16)
    # zonas ruins
    t.rect(90, 180, 400, 150, BAD, BAD, 1, 14, op=0.13)
    t.rect(540, 540, 290, 110, BAD, BAD, 1, 14, op=0.13)
    t.rect(90, 520, 230, 130, BAD, BAD, 1, 14, op=0.13)
    t.rect(120, 200, 300, 30, "url(#gcalor)", HEAT, 2, 8)
    t.texto(270, 192, "resistência", 15, HEAT, "700", "middle")
    t.rect(620, 580, 150, 40, "url(#gagua)", WATER, 2, 8, op=0.9)
    t.texto(695, 650, "umidificador (névoa)", 14, WATER, anchor="middle")
    t.fan(120, 560, 24)
    t.seta(150, 560, 250, 560, AIR, 4, "6 5")
    t.texto(135, 612, "jato do fan", 14, AIR)
    # bandeja
    t.rect(250, 330, 420, 180, "#16203e", "#6b86c9", 3, 12, dash="8 6")
    for r in range(3):
        t.ovos(290 + (r % 2) * 20, 372 + r * 54, 8, 48)
    # posicao correta
    t.circulo(460, 292, 44, "#0e3a30", OK, 2)
    t.sonda(460, 292)
    t.num(520, 262, 1, OK)
    t.texto(520, 322, "posição ideal", 15, OK, "700")
    t.linha(420, 232, 460, 258, MUT, 2, "5 5")
    t.texto(380, 252, "5–10 cm", 14, MUT)
    t.etiqueta(560, 706, "zona vermelha = evite", BAD, 15)
    # detalhe da passagem do cabo
    t.texto(60, 720, "Passagem do cabo (corte)", 18, MUT, "700")
    t.rect(120, 750, 40, 260, "#1a2342", "#4a5f9c", 3, 6)
    t.texto(140, 1030, "parede", 14, MUT, anchor="middle")
    t.rect(112, 840, 56, 36, "#475569", "#94a3b8", 2, 6)
    t.texto(140, 862, "prensa", 12, TXT, "700", "middle")
    t.caminho([(40, 858), (112, 858)], MUT, 5)
    t.caminho([(168, 858), (230, 858), (290, 940), (350, 940), (410, 858), (470, 858)], MUT, 5)
    t.texto(320, 980, "laço de gotejamento", 15, WATER, anchor="middle")
    t.seta(320, 968, 320, 950, WATER, 2)
    t.texto(60, 836, "sonda", 14, OK)
    t.texto(480, 862, "→ eletrônica", 15, MUT)
    t.num(250, 800, 2, V5)
    t.num(380, 1000, 3, V5)

    t.painel(900, 150, 850, 890, "Passo a passo")
    y = t.passos(930, 228, [
        "Instale o SHT31 na altura dos ovos, no meio da bandeja ou levemente deslocado para o lado oposto ao aquecedor (5–10 cm de distância).",
        "Passe o cabo por um prensa-cabo (ou furo vedado com silicone) na parede. Sem folga: vapor sai por ali.",
        "Faça um laço de gotejamento do lado de fora para a água escorrer em vez de entrar pelo furo.",
        "Fixe o cabo com abraçadeira sem apertar o isolante; mantenha-o longe de cabos de 220 V (interferência no I2C).",
        "Não encoste a ponta metálica no isopor/plástico quente nem em ovos: leia o ar, não a superfície.",
        "Sonda IP68 selada tem atraso de 2–4 min: não 'persiga' a leitura; deixe o PI aprender.",
    ], 790, OK)
    t.texto(930, y + 20, "Ligação (4 fios)", 20, TXT, "700")
    cores = [("vermelho", "#ef4444", "VCC", "3V3"), ("preto", "#e5e7eb", "GND", "GND"),
             ("amarelo", "#facc15", "SCL", "GPIO9"), ("verde", "#22c55e", "SDA", "GPIO8")]
    for i, (n, c, f, d) in enumerate(cores):
        yy = y + 52 + i * 44
        t.circulo(950, yy - 6, 11, c, BG, 2)
        t.texto(975, yy, n, 17, TXT, "700")
        t.texto(1110, yy, f, 17, MUT)
        t.texto(1230, yy, "→", 17, DIM)
        t.texto(1260, yy, d, 17, V5, "700")
    t.par(930, y + 52 + 4 * 44 + 10, "Cores seguem a convenção do cabo da sonda IP68 (SHT30/PVC68). Se não responder, troque amarelo ↔ verde — não há risco. Endereço I2C 0x44.", 780, 15, MUT)
    saida(t, "02_instalacao_sensor_sht31")


def d03(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação do sensor de CO₂ (MH-Z19C)",
             "Sensor infravermelho NDIR: precisa trocar ar pela janela, ficar longe do calor e da névoa e receber 5,0 V estáveis.")
    # corpo
    t.texto(60, 150, "Conector de 7 vias (vista da etiqueta)", 18, MUT, "700")
    t.rect(80, 180, 320, 230, "url(#gouro)", "#7a5b14", 3, 10)
    t.rect(100, 330, 120, 60, "#f5f1e6", "#9a8f70", 2, 4)
    t.texto(160, 366, "janela", 15, "#6b5d34", "700", "middle")
    t.rect(400, 230, 22, 130, "#eee9dc", "#8d8774", 2, 4)
    cores = [("marrom", "#92400e", "1", "HD", "isolar", MUT), ("branco", "#f8fafc", "2", "reservado", "isolar", MUT),
             ("preto", "#111827", "3", "GND", "GND comum", TXT), ("vermelho", "#ef4444", "4", "Vin +5 V", "+5 V do DC-DC", V5),
             ("azul", "#2563eb", "5", "RX", "GPIO15 (TX do ESP)", SIG), ("verde", "#16a34a", "6", "TX", "GPIO16 (RX do ESP)", SIG),
             ("amarelo", "#facc15", "7", "PWM", "isolar", MUT)]
    for i, (n, c, pino, f, d, cd) in enumerate(cores):
        x = 460
        y = 200 + i * 48
        t.circulo(x, y, 13, c, "#8da0c8", 2)
        t.texto(x + 24, y + 6, f"pino {pino} · {n}", 16, TXT, "700")
        t.texto(x + 190, y + 6, f, 16, MUT)
        t.texto(x + 290, y + 6, "→ " + d, 16, cd, "700")
    t.caminho([(422, 295), (440, 295), (440, 200)], "#8da0c8", 2)
    t.caminho([(440, 295), (440, 488)], "#8da0c8", 2)
    # vista lateral com montagem
    t.texto(60, 560, "Montagem (vista lateral)", 18, MUT, "700")
    t.rect(140, 640, 460, 20, "#2a3558", "#6b86c9", 2, 6)
    t.texto(370, 632, "suporte / parede interna", 14, MUT, anchor="middle")
    t.rect(180, 664, 18, 40, "#94a3b8", "#cbd5e1", 2, 3)
    t.rect(540, 664, 18, 40, "#94a3b8", "#cbd5e1", 2, 3)
    t.texto(100, 690, "espaçador", 13, MUT)
    t.texto(100, 706, "5 mm", 13, MUT)
    t.rect(160, 704, 420, 36, "url(#gouro)", "#7a5b14", 2, 6)
    t.rect(220, 740, 100, 12, "#f5f1e6", "#9a8f70", 1, 3)
    t.texto(270, 770, "janela de difusão ↓", 14, V5, anchor="middle")
    # capa aberta embaixo
    t.caminho([(140, 760), (140, 800), (620, 800), (620, 760)], WATER, 3, "7 6")
    t.texto(380, 826, "capa/anteparo aberta embaixo (contra respingo e penugem)", 15, WATER, anchor="middle")
    t.seta(260, 880, 260, 830, AIR, 4)
    t.texto(300, 890, "o ar sobe e entra pela janela", 15, AIR)
    t.etiqueta(100, 940, "5,0 V ±0,1 V · ≥ 150 mA · 470 µF + 100 nF junto ao sensor", V5, 16)
    t.etiqueta(100, 990, "faixa de operação: −10 a 50 °C, sem condensação", OK, 16)

    t.painel(960, 150, 790, 890, "Passo a passo")
    y = t.passos(990, 228, [
        "Fixe pelos 2 furos da placa, com espaçadores de ~5 mm. Nunca aperte a câmara dourada.",
        "Posição: altura dos ovos, longe do aquecedor (limite 50 °C), da névoa e da saída dos fans.",
        "Janela branca para baixo ou de lado; deixe 1 cm livre ao redor para o ar difundir.",
        "Proteção LEVE: uma capa aberta embaixo ou tela fina a ~5 mm; nunca fita, filme, espuma úmida ou recipiente fechado.",
        "Alimente com o 5 V do conversor (não 3V3, não o pino 5V do adaptador). Adicione 470 µF + 100 nF entre Vin e GND.",
        "Ligue TX/RX em GPIO15/16 — NUNCA nos bornes rotulados RX/TX do adaptador (são a USB).",
        "Aguarde ~1 min de aquecimento; confira o valor no card de CO₂ e o log \"CO2: Sensor respondeu\".",
    ], 740, V5, 16)
    t.faz_nao_faz(990, y + 10, 720,
                  ["Ar de sala ao ar livre ≈ 400–600 ppm: use para conferir", "Isolar HD, PWM e o fio reservado"],
                  ["Soprar ou lavar a membrana", "Deixar o fio HD no GND por 7 s (calibra o zero!)"], 15)
    saida(t, "03_instalacao_co2_mhz19c")


def d04(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação do acelerômetro ADXL345 (detecção de tampa aberta)",
             "O módulo fica sobre a tampa como um peso: quando alguém o retira para abrir a caixa, o firmware entende \"tampa aberta\".")
    def tampa(x, y, aberto=False, estado="", cor=OK):
        t.rect(x, y + 120, 300, 90, "#0f1730", "#4a5f9c", 4, 10)           # caixa
        t.texto(x + 150, y + 175, "caixa", 15, MUT, anchor="middle")
        if aberto:
            t.poligono([(x, y + 118), (x + 300, y + 118), (x + 340, y + 40), (x + 40, y + 40)], "#1a2342", "#4a5f9c", 4)
        else:
            t.rect(x - 6, y + 90, 312, 30, "#1a2342", "#4a5f9c", 4, 6)
        t.etiqueta(x, y + 250, estado, cor, 17)

    # estado 1: fechada (modulo na tampa)
    tampa(80, 180, False, "FECHADA — módulo na tampa", OK)
    t.rect(180, 238, 100, 34, "#111827", "#8da0c8", 3, 5)
    t.texto(230, 261, "ADXL345", 14, SIG, "700", "middle")
    t.texto(230, 226, "vibra com fans/bomba", 13, MUT, anchor="middle")
    # estado 2: aberta (modulo retirado)
    tampa(520, 180, True, "ABERTA — módulo retirado/movido", BAD)
    t.rect(660, 300, 100, 34, "#111827", "#8da0c8", 3, 5)
    t.texto(710, 323, "ADXL345", 14, SIG, "700", "middle")
    t.seta(640, 236, 700, 296, V5, 3, "5 5")
    # estado 3: calibracao FORA
    t.texto(100, 560, "Calibração (tela Configurações → Tampa)", 18, MUT, "700")
    t.rect(80, 640, 300, 24, "#2a3558", "#6b86c9", 2, 6)
    t.texto(230, 690, "mesa / bancada", 14, MUT, anchor="middle")
    t.rect(180, 604, 100, 34, "#111827", "#8da0c8", 3, 5)
    t.texto(230, 627, "ADXL345", 14, SIG, "700", "middle")
    t.etiqueta(80, 740, "1) CALIBRAR FECHADA: módulo na tampa", OK, 16)
    t.etiqueta(80, 790, "2) CALIBRAR FORA: módulo na mesa", V5, 16)
    t.par(80, 850, "Cada calibração exige 3 s parado. A diferença entre as duas posições (ângulo e vibração) é o que o firmware usa. Se tampa e mesa tiverem inclinação e vibração parecidas, o fechamento será inferido pela temperatura.", 700, 16, MUT)
    # esquema do modulo
    t.texto(520, 560, "Módulo com regulador (pinos)", 18, MUT, "700")
    t.rect(520, 590, 330, 150, "#0c1730", "#3a5fb0", 3, 10)
    pins = [("5V", "+5 V do DC-DC", V5), ("3V3", "saída — NÃO ligar", BAD), ("GND", "GND comum", TXT), ("SDA", "GPIO8", SIG),
            ("SCL", "GPIO9", SIG), ("CS", "pino 3V3 do módulo", OK)]
    for i, (n, d, c) in enumerate(pins):
        t.texto(540, 620 + i * 22, n, 15, c, "700")
        t.texto(600, 620 + i * 22, "→ " + d, 15, MUT)
    t.texto(520, 770, "SDO e VS: livres. Endereço I2C 0x53.", 15, MUT)

    t.painel(960, 150, 790, 890, "Passo a passo")
    y = t.passos(990, 228, [
        "Use o ADXL345 em uma caixinha e coloque-o SOBRE a tampa, apoiado, como um peso. Não precisa colar na tampa.",
        "Ligue com cabo flexível e com folga para a tampa abrir sem esticar: 5V, GND, SDA, SCL e CS ao pino 3V3 do módulo.",
        "No aparelho: Configurações → Tampa → \"Detectar abertura\" Ligado.",
        "Com o módulo na tampa fechada, toque CALIBRAR FECHADA e aguarde 3 s.",
        "Coloque o módulo sobre a mesa e toque CALIBRAR FORA.",
        "Teste: retire o módulo — o card Status deve mostrar \"TAMPA\" e os alertas pausam; recoloque e ele volta a \"recuperando\".",
        "Ajuste a sensibilidade (mg) e a tolerância (graus) se der alarme falso; veja o contador \"ign\" na tela.",
    ], 740, V5, 16)
    t.faz_nao_faz(990, y + 10, 720,
                  ["Fixar o módulo num lugar que sinta a vibração dos fans", "Deixar folga no cabo"],
                  ["Ligar o pino 3V3 do módulo ao 3V3 do ESP32", "Usar cabo curto e esticado: ele puxa o módulo"], 15)
    saida(t, "04_instalacao_adxl345_tampa")


def d05(saida, W=1800, H=1100):
    t = Tela(W, H, "Instalação dos fans de PC (4 fios) com PWM de 25 kHz",
             "+12 V fixo no fio 2, velocidade pelo sinal PWM no fio 4, RPM no fio 3. Sem módulo MOSFET.")
    # conector
    t.texto(60, 150, "Conector do fan (vista de frente)", 18, MUT, "700")
    t.rect(100, 180, 330, 110, "#101826", "#8da0c8", 3, 8)
    t.rect(100, 180, 330, 24, "#1a2342", "#8da0c8", 2, 8)
    t.texto(265, 198, "▼ chave (entalhe)", 13, MUT, anchor="middle")
    xs = [140, 215, 290, 365]
    nomes = [("1", "GND", TXT, "fio com a marca “1”"), ("2", "+12 V", V5, "fixo, via relé CH8"),
             ("3", "TACH", SIG, "→ GPIO17"), ("4", "PWM", VIO, "← GPIO4/5/6 (1 kΩ)")]
    for x, (n, f, c, d) in zip(xs, nomes):
        t.rect(x - 18, 222, 36, 50, "#05070f", c, 3, 5)
        t.texto(x, 254, n, 24, c, "800", "middle")
        t.texto(x, 318, f, 16, c, "700", "middle")
        t.texto(470, 214 + (int(n) - 1) * 30, f"{n}  {f}", 17, c, "700")
        t.texto(570, 214 + (int(n) - 1) * 30, d, 16, MUT)
    # esquema eletrico
    t.texto(60, 420, "Ligação", 18, MUT, "700")
    t.rect(100, 450, 160, 70, "#3a2a08", V5, 2, 8); t.texto(180, 492, "DC-DC / 12 V", 17, V5, "700", "middle")
    t.rect(330, 450, 150, 70, "#2b0f19", "#b91c4a", 2, 8); t.texto(405, 478, "relé CH8", 17, TXT, "700", "middle"); t.texto(405, 502, "contato NA", 14, MUT, anchor="middle")
    t.rect(560, 450, 150, 70, "#101826", "#8da0c8", 2, 8); t.texto(635, 492, "FAN", 20, TXT, "700", "middle")
    t.caminho([(260, 485), (330, 485)], V5, 5); t.caminho([(480, 485), (560, 485)], V5, 5)
    t.texto(300, 470, "+12 V", 14, V5, anchor="middle"); t.texto(520, 470, "pino 2", 14, V5, anchor="middle")
    t.caminho([(180, 520), (180, 570), (635, 570), (635, 520)], TXT, 4); t.texto(400, 590, "GND comum (pino 1) — liga com o GND do ESP32", 14, MUT, anchor="middle")
    # esp32
    t.esp32(100, 640, 260, 120, "ESP32-S3")
    t.texto(368, 656, "GPIO4/5/6", 14, VIO, "700")
    t.caminho([(360, 668), (470, 668)], VIO, 4)
    t.rect(470, 650, 90, 36, "#2a2f45", "#94a3b8", 2, 6); t.texto(515, 674, "1 kΩ", 15, TXT, "700", "middle")
    t.caminho([(560, 668), (640, 668), (640, 522)], VIO, 4)
    t.texto(368, 708, "GPIO17 (tacômetro)", 14, SIG, "700")
    t.caminho([(360, 720), (700, 720), (700, 522), (710, 522)], SIG, 4)
    t.linha(560, 720, 560, 748, SIG, 3); t.circulo(560, 720, 5, SIG, BG, 1)
    t.linha(690, 720, 690, 748, SIG, 3); t.circulo(690, 720, 5, SIG, BG, 1)
    t.rect(500, 748, 120, 34, "#2a2f45", "#94a3b8", 2, 6); t.texto(560, 771, "10 kΩ → 3V3", 14, TXT, "700", "middle")
    t.rect(640, 748, 100, 34, "#2a2f45", "#94a3b8", 2, 6); t.texto(690, 771, "10 nF → GND", 14, TXT, "700", "middle")
    t.texto(100, 826, "Filtro RC no tacômetro: estabiliza o RPM com PWM < 100 % (fio 3 longe do fio 4).", 15, MUT)
    # posicoes
    t.texto(60, 890, "Posição e sentido do ar", 18, MUT, "700")
    t.fan(140, 960, 36); t.texto(140, 1030, "Fan 1 · Circulação", 14, AIR, anchor="middle")
    t.fan(400, 960, 36); t.texto(400, 1030, "Fan 2 · Resfriamento", 14, VIO, anchor="middle")
    t.fan(660, 960, 36); t.texto(660, 1030, "Fan 3 · Renovação", 14, VIO, anchor="middle")
    for x in (140, 400, 660):
        t.seta(x - 60, 905, x + 60, 905, AIR, 2)
    t.texto(260, 890, "setas na lateral do fan = sentido do ar e da rotação", 13, DIM)

    t.painel(960, 150, 790, 890, "Passo a passo")
    y = t.passos(990, 228, [
        "Identifique os fios: com 4 fios iguais, a contagem começa no lado marcado com “1”: 1 GND, 2 +12 V, 3 tacômetro, 4 PWM.",
        "Alimente o pino 2 com +12 V através do contato NA do relé CH8; o GND do fan e o do ESP32 são o mesmo.",
        "PWM: GPIO4 (fan 1), GPIO5 (fan 2), GPIO6 (fan 3) → resistor de 1 kΩ → pino 4. A frequência é 25 kHz.",
        "Tacômetro (só o fan 1): pino 3 → GPIO17, com pull-up de 10 kΩ ao 3V3 e 10 nF ao GND.",
        "Confira o sentido do ar pelas setas moldadas na carcaça; fixe com coxins de borracha para não vibrar a caixa.",
        "No aparelho: tela Fans → ligue o PWM, escolha a função de cada fan e confira o RPM (500–2000 rpm no MSI APA1225M12).",
    ], 740, V5, 16)
    t.faz_nao_faz(990, y + 10, 720,
                  ["Fan de 3 fios: use o esquema com MOSFET (04_irf520_pwm)", "Cortar o +12 V pelo relé para parar de verdade"],
                  ["Ligar o fio 3 em +12 V ou ao lado do fio 4 (ruído no RPM)", "Esperar que 0 % pare o fan: ele gira no mínimo"], 15)
    saida(t, "05_instalacao_fans_pc")
