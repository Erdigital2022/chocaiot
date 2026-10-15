# 03 — Firmware

Arquivo único `firmware/src/main.cpp` (~10 mil linhas, organizado em seções comentadas por "Fase"). Plataforma:
**PlatformIO + Arduino-ESP32 (ESP32-S3)**, particionamento customizado (`partitions_16mb.csv`: 2 slots de app, LittleFS,
partição `coredump`).

## Compilar e gravar

```bash
cd firmware
cp src/firebase_credentials.example.h src/firebase_credentials.h   # preencha (nunca versionado)
pio run -e esp32-s3-controller                 # compila
pio run -e esp32-s3-controller -t upload       # grava (cargas de 220 V fora da tomada!)
pio device monitor                             # serial a 115200
```

> Abrir a porta serial **reinicia** o ESP32 (DTR/RTS). Isso é normal e gera um aviso de "queda de energia" no app
> (no máximo um a cada 30 min).

## Mapa do `main.cpp` (onde procurar)

| Tema | Procure por |
|---|---|
| Pinos e relés | `PIN_RELE_CH`, `RELAY_PINS`, `initRelays()` |
| Firebase (poll, heartbeat) | `pollActuatorsFromFirestore`, `applyActuatorsFromFirestore`, `pushHeartbeatToFirestore` |
| Telas | `drawMainScreen`, `drawSensorDetailScreen`, `handleSettingsTap`, `S_*` (enum `AppState`) |
| Ciclo e tipos de ovo | `TipoOvo`, `calcularDiaCicloAtual`, `aplicarLimitesDoTipo` |
| Controle PI | `autoPidTick`, `pidFimDireto`, `hwRonPrior` |
| Automação por relé / regras avançadas | `RegraAuto`, `RegraAv`, `automacaoTick`, `advLeitura`, `advCmp` |
| Tampa / ambiente / diagnóstico | `tampaTick`, `ambienteTick`, `ambDiagCalcular` |
| CO₂ | `co2Init`, `co2Tick`, `co2Limites`, `co2AlertaTick` |
| Fans | `fanRegras*`, `fansRegrasTick`, `tachIsr`, `pwmAplica` |
| Histórico/logs | `histAmostra`, `histServico`, `logEvento` |
| Diagnóstico de reset | `logResetReason`, `coredumpRelatar` |

## Controle de temperatura e umidade (PI de ciclo fixo)

1. A cada ciclo (período calculado pela potência/volume do **Hardware** informado) o aquecedor liga uma fração
   `duty = Kp·erro + I`.
2. O termo **I** aprende o *duty* de regime (a perda de calor da caixa) — salvo em `/pid3.cfg`.
3. Longe do alvo (> 1,2 °C) liga direto, com corte por previsão para não passar do ponto.
4. Um **governador de energia** impede novos pulsos enquanto o calor dos anteriores ainda "está em trânsito"
   (atraso morto do sensor: 2–4 min numa sonda selada).
5. Sobretemperatura (`máx + 0,5 °C`) desliga o aquecedor **sempre**, com push.

O umidificador usa a mesma estrutura (`autoPidTick`), com taxa estimada em %/s e proteção "sem água".

## Ciclo de incubação

```
sem ciclo ──▶ PREPARO (estabiliza sem ovos; "Aguarde" → "Pronto" após 5 min na faixa)
          ──▶ [Ovos colocados] ──▶ dia 1 … dia N (ovoscopias, viragem, eclosão) ──▶ encerrado/relatório
```

* O tipo de ovo define faixas de temperatura/umidade, viragem, dias de ovoscopia e início da eclosão.
* Na eclosão a faixa de umidade (e opcionalmente a de temperatura) se desloca sozinha.
* **Tampa aberta** (queda brusca ou ADXL345): alertas e diagnóstico suspensos, integral do PI congelado.

## CO₂ (MH-Z19C)

* UART1 9600 8N1; comando `FF 01 86 00 00 00 00 00 79`; resposta `FF 86 HH LL … CS`; `ppm = HH·256 + LL`.
* Ignora o primeiro minuto (pré-aquecimento) e leituras fora de 300–10000 ppm; **desliga o ABC** (a caixa fechada
  nunca volta a 400 ppm, o ABC "zeraria" no valor errado).
* Faixas por fase: incubação ≤ 2000 ok · > 3000 alto; pré-eclosão/eclosão ≤ 3000 ok · > 4500 alto.
* **Alerta:** acima do limite alto por ≥ 10 min (só com ovos e tampa fechada) → log + push; normaliza com 5 min ok.
* **Renovação por CO₂:** o fan com a função *Renovação* acelera entre 1500 e 3000 ppm (editável).
* Diagnóstico sem serial: categoria **CO2** no log ("Sensor respondeu", "Sem resposta (N bytes recebidos…)").

## Fans: funções e regras

Cada fan é *Manual* ou recebe uma função (compartilhada por todos):

| Função | Sobe com… |
|---|---|
| Circulação | tempo em que o aquecedor fica ligado |
| Resfriamento | temperatura acima do ponto médio da faixa |
| Renovação | umidade acima do máximo **e** CO₂ (se ligado) |

Rampa de no máximo 10 pontos a cada 5 s; tampa aberta ou sensor sem leitura → mínimo da função.

## Diagnóstico

* **Motivo do último reset:** tela *Sistema* ("Normal", "Externo", "Brownout!", "Panic!", "Watchdog").
* **Crash (Panic):** o ESP grava um *core dump* na partição `coredump`; no boot seguinte o firmware registra
  `Panic: Crash task=… pc=… bt=…` no log. Para decodificar:
  ```bash
  xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/esp32-s3-controller/firmware.elf <pc> <bt…>
  ```
  (use o `.elf` **do mesmo build** que estava gravado).
* **Teclas da serial** (monitor a 115200), entre outras: `A` leitura do ADXL, `I` varre o I2C, `P` liga/desliga PWM,
  `h` fecha o slot do histórico, `p` "print screen" do TFT em hexadecimal.

## Arquivos de configuração no LittleFS do aparelho

`/display.cfg`, `/auto.cfg`, `/pid3.cfg`, `/fanrules.cfg`, `/pwm.cfg`, `/adxl.cfg`, `/ciclo.cfg`, `/tipos.cfg`,
`/co2.cfg` (gravar `0` desliga o driver) — textos simples, fáceis de inspecionar.
