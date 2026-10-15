# 02 — Hardware e ligações

> Esquemas prontos em [`docs/esquemas/`](esquemas/) (PNG para ver, SVG para editar; os `.py` os geram).
> **Nada aqui substitui bom senso elétrico**: 220 V mata. Veja [`05-seguranca.md`](05-seguranca.md).

## Pinagem do ESP32-S3 (YD-ESP32-S3 44P, N16R8)

| GPIO | Uso | Observação |
|---|---|---|
| 8 / 9 | I2C SDA / SCL | SHT31 `0x44`, ADXL345 `0x53`, DS3231 `0x68`, EEPROM do RTC `0x57` |
| 10, 11, 12, 13, 14, 21 | Display ILI9341: CS, MOSI, SCLK, MISO, DC, RST | SPI a 27 MHz (40 MHz dava artefatos perto do relé) |
| 48 | CS do touch XPT2046 | mesmo barramento SPI |
| 18 | CS do cartão SD | compartilha o SPI do display (`SD.begin(CS, tft.getSPIinstance())`) |
| 1, 2, 38, 39, 40, 41, 42, 7 | Relés CH1…CH8 | ativo em **LOW**; todos iniciam HIGH (desligado) |
| 4, 5, 6 | PWM dos fans 1/2/3 (25 kHz) | fio 4 do fan de PC, por resistor de 1 kΩ |
| 17 | Tacômetro do fan 1 | fio 3 do fan; interrupção nas duas bordas |
| 47 | Backlight (PWM 5 kHz) | liga no pino LED do display (entrada lógica de um transistor da placa) |
| 15 / 16 | UART1 do MH-Z19C (TX / RX do ESP) | 9600 8N1 |
| 0 | Botão BOOT / acorda do deep sleep | strapping |
| 19 / 20 | USB nativo | não usar |
| 43 / 44 | UART0 (ponte USB-serial CH343) | **não ligue nada aqui** (ver armadilhas) |
| 35, 36, 37 | PSRAM octal | reservados |
| 3, 45, 46 | strapping | evitar |

### Canais de relé

| Canal | GPIO | Carga | Tensão |
|---|---|---|---|
| CH1 | 1 | Resistência / lâmpada cerâmica (aquecedor) | 220 V |
| CH2 | 2 | Umidificador | 5 V |
| CH3 | 38 | Bomba de água (reservatório) | 220 V — comando só no aparelho |
| CH4 | 39 | Bomba de ar / ventilação | 3,7 V |
| CH5 | 40 | LED do ovoscópio | 220 V |
| CH6 | 41 | Reservado (áudio) | — |
| CH7 | 42 | Cooler do sistema (automático pela temperatura do chip) | 12 V |
| CH8 | 7 | Fans (alimentação +12 V dos fans) | 12 V |

## Ligações por subsistema

### Alimentação
Conversor DC-DC 12/24 V → **5 V / 3 A** alimenta: bobinas do módulo de relé, display, ESP32 (pino 5V) e o MH-Z19C.
O GND de **tudo** é comum. O pino "5V" de alguns adaptadores de bornes mede ~0,6 V (diodo de proteção): meça a
tensão **na saída do conversor**, não ali.

> Lição aprendida: alimentar o ESP32 só pelo USB do PC causava *brownout* com WiFi + display (3,3 V caía a 2,9 V).

### Sensor de temperatura/umidade (SHT31)
4 fios: VCC (3,3 V), GND, SDA→GPIO8, SCL→GPIO9. Se a leitura falhar, inverta SDA/SCL (não há risco).

### Acelerômetro ADXL345 (tampa)
Esquema: [`01_ligacao_ADXL345.png`](esquemas/01_ligacao_ADXL345.png). Módulo com regulador: **5V** → 5 V,
**CS** → pino 3V3 *do próprio módulo*, SDA/SCL no barramento. **SDO e VS ficam livres** (já resolvidos por
resistores de 0 Ω na placa). O módulo é um *peso* sobre a tampa: ao ser retirado, o firmware entende "tampa aberta".

### CO₂ (MH-Z19C) — 7 fios, **sem marcação**
Passo a passo completo em [`06_mhz19c_identificar_fios.png`](esquemas/06_mhz19c_identificar_fios.png) e
[`07_mhz19c_fios_sem_marcacao.png`](esquemas/07_mhz19c_fios_sem_marcacao.png).

| Pino do conector | Função | Ligar em |
|---|---|---|
| 4 (meio) | Vin | **+5 V** do conversor (5,0 V ±0,1 V, ≥ 150 mA) |
| 3 | GND | GND comum |
| 6 | TX do sensor | **GPIO16** (RX do ESP) |
| 5 | RX do sensor | **GPIO15** (TX do ESP) |
| 1, 2, 7 | HD, reservado, PWM | **sem ligação** (isolar) |

Identifique o GND por continuidade com a carcaça dourada (a moldura é GND). Trocar TX/RX não queima nada.
**Nunca** ligue o sensor aos bornes rotulados RX/TX do adaptador: eles são a UART0 da ponte USB e impedem a gravação.

### Fans de PC (4 fios) com PWM
Esquema: [`05_fan_4fios_pwm.png`](esquemas/05_fan_4fios_pwm.png). +12 V **fixo** no fio 2, GND no fio 1,
PWM de 25 kHz no fio 4 (GPIO→1 kΩ), tacômetro no fio 3. Com 0 % o fan ainda gira (mínimo do padrão Intel):
para parar de verdade, corte o +12 V pelo relé CH8 (o firmware faz isso quando os 3 fans estão em 0 %).
Módulos IRF520 **não** são necessários com fan de 4 fios.

### Backlight do display
O pino `LED` do módulo ILI9341 é a base de um transistor (via resistor de 1 kΩ): ligue o **GPIO47 direto**, sem MOSFET.
Remova antes o fio que o prendia ao +5 V.

### Relés e segurança na partida — **pull-up**
Durante o reset/gravação os GPIOs ficam "soltos" e o módulo de relé pode acionar sozinho (aconteceu com a bomba de 220 V).
Mitigação em hardware: resistor de **10 kΩ de cada IN até o 3V3** (rede SIP), esquema
[`03_pullup_rele.png`](esquemas/03_pullup_rele.png). Use o **3V3**, nunca o 5 V (o GPIO não tolera 5 V).
Enquanto não instalar: **tire a resistência e a bomba da tomada ao gravar**.

### Expansor MCP23017 (opcional)
Esquema: [`08_mcp23017_i2c.png`](esquemas/08_mcp23017_i2c.png). Alimentar em **3V3**. Útil para LED/buzzer e, no
futuro, para liberar pinos nativos (I2S do áudio). Não serve para tacômetros.

## Armadilhas conhecidas (todas já custaram horas)

| Armadilha | Sintoma | Solução |
|---|---|---|
| Fio no borne RX/TX | Erro "no sync reply" na gravação | Mover para GPIO15/16; manter RX/TX vazios |
| Fio de sinal no GPIO46 (ou outro strapping) | Não entra em modo de gravação | Evitar 0, 3, 45, 46 |
| 5 V instável (4,8–5,7 V) | CO₂ errado, resets | Regulador dedicado de 5,0 V para o sensor |
| `SPI.begin()` extra para o SD | Tela branca | Usar `tft.getSPIinstance()` |
| Cabos longos no barramento I2C perto de relé | Leituras NaN | Fios curtos, longe do 220 V |
