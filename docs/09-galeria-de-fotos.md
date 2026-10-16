# 09 — Galeria de fotos (montagem real)

Fotos tiradas durante o desenvolvimento, em bancada. Todas passaram por limpeza de metadados (localização, modelo do
celular, data) e recorte do fundo antes da publicação.

## Telas do aparelho (TFT 3,2")

| Foto | O que mostra |
|---|---|
| ![Home](fotos/01_tela_home_firmware.jpg) | **Tela inicial**: temperatura, umidade, CO₂, status (com rpm), ciclo e a data alternando no card do ciclo |
| ![Logs](fotos/02_tela_logs.jpg) | **Logs**: filtros de data e tipo, busca e paginação; eventos de atuador, boot, CO₂ e tampa |
| ![Fans](fotos/03_tela_fans_rpm.jpg) | **Fans**: PWM ligado, % por fan e RPM do fan 1 vindo do tacômetro |
| ![Tampa](fotos/04_tela_tampa_adxl.jpg) | **Tampa (ADXL345)**: estado, ângulo, barras de posição/movimento/vibração e calibração FECHADA/FORA |
| ![Status](fotos/05_tela_status_ambiente.jpg) | **Status do ambiente** com a tampa aberta: faixas, automação e botão manual |

## App (PWA)

| Foto | O que mostra |
|---|---|
| <img src="fotos/06_pwa_no_celular.jpg" width="320"> | PWA no celular: câmera, cards de temperatura/umidade/CO₂, status e ciclo |

## Sensor de CO₂ MH-Z19C

| Frente | Verso (etiqueta) | Cabo de 7 fios |
|---|---|---|
| <img src="fotos/07_mhz19c_frente.jpg" width="240"> | <img src="fotos/08_mhz19c_verso_etiqueta.jpg" width="240"> | <img src="fotos/09_mhz19c_cabo_7_fios.jpg" width="240"> |

| Lâmpada infravermelha acesa | Janela de difusão (membrana branca) |
|---|---|
| <img src="fotos/10_mhz19c_lampada_infravermelha.jpg" width="320"> | <img src="fotos/11_mhz19c_janela_de_difusao.jpg" width="320"> |

* O brilho alaranjado **é normal**: é a fonte infravermelha do sensor funcionando.
* A janela branca é a membrana por onde o gás entra: nunca cubra com fita, nem lave.
* Ordem dos fios no conector, etiqueta para cima, esquerda → direita: marrom, branco, preto, vermelho, azul, verde, amarelo.

## Expansores e drivers

| MCP23017 (verso) | MCP23017 (frente) | Módulo IRF520 (MH) |
|---|---|---|
| <img src="fotos/12_mcp23017_verso.jpg" width="240"> | <img src="fotos/13_mcp23017_frente.jpg" width="240"> | <img src="fotos/14_irf520_modulo_mh.jpg" width="240"> |

* MCP23017 Waveshare: conector de 6 vias (VCC, GND, SDA, SCL, INTA, INTB), pontes A0/A1/A2 e pinos PA0–PA7 / PB0–PB7.
* O IRF520 (SIG, VCC, GND; U+/U−; VIN/GND) **não é necessário** com fans de 4 fios.

## Fans

| Conector de 4 fios (marca "1") | Conector de 3 pinos | Etiqueta do fan |
|---|---|---|
| <img src="fotos/15_fan_conector_4_fios.jpg" width="240"> | <img src="fotos/16_fan_conector_3_pinos.jpg" width="240"> | <img src="fotos/17_fan_msi_etiqueta.jpg" width="240"> |

## Display: o pino LED e o transistor do backlight

<img src="fotos/18_display_verso_backlight.jpg" width="420">

No verso do módulo ILI9341 aparecem o transistor (SOT-23) e o resistor de 1 kΩ ligados ao pino `LED`: ele é uma
**entrada lógica**, por isso o GPIO47 aciona o backlight direto, sem MOSFET.

## Erro clássico: fios nos bornes RX/TX

<img src="fotos/19_erro_bornes_rx_tx.jpg" width="420">

Os bornes rotulados **RX** e **TX** do adaptador são a UART0 da ponte USB. Ligar um sensor ali impede a gravação
(`no sync reply`). Use GPIO15/16 para o CO₂.
