# CHECKPOINT — Projeto ChocaIoT
**Data do checkpoint:** atualização completa (substitui a versão anterior)
**Autor:** Eric (GitHub: Erdigital2022)
**Status geral:** Pré-produção avançada — firmware do controlador com 7 fases concluídas, problema de energia do display **resolvido**, PWA com controle remoto funcional, Fase 8 (integração Firebase↔firmware) em andamento, e um **novo problema ativo sendo investigado: resets aleatórios após minutos/horas ligado**.

---

## 1. VISÃO GERAL DO PROJETO

ChocaIoT é uma chocadeira automática inteligente para ~70 ovos de galinha, com:
- Controlador principal: ESP32-S3 (YD-ESP32-S3 44P, módulo N16R8)
- Câmera separada: ESP32-S3-CAM + OV5640 (projeto PlatformIO independente)
- Backend Firebase (Auth, Firestore, Hosting) — **em produção**
- PWA multiplataforma — **em produção, com controle remoto de atuadores já funcional**
- Display touchscreen ILI9341 3.2" — interface local completa (WiFi, atuadores, configurações), **agora alimentado em 5V** (ver seção 7)

---

## 2. HARDWARE — STATUS ATUAL

### 2.1 Recebido e confirmado funcionando
| Componente | Status |
|---|---|
| YD-ESP32-S3 44P (controlador, N16R8) | ✅ Pinout confirmado fisicamente, todas as fases testadas |
| Display ILI9341 3.2" SPI + touch XPT2046 | ✅ Funcionando — flicker resolvido (ver seção 7) |
| RTC DS3231 (EstarDyn) | ✅ Confirmado via I2C scan, em uso |
| Relé 8 canais 5V (SRD-05VDC-SL-C) | ✅ Pinout confirmado, lógica testada (sem carga real ainda) |
| DC-DC Step Down 12/24V→5V 3A | ✅ Em uso — alimenta o relé **e agora também o display** (VCC+backlight) |
| Bateria LIR2032 3,6V | ✅ Recebida (backup do RTC) |
| MicroSD 32GB | ✅ Recebido, reservado pra câmera (não usado ainda) |
| Kit jumpers 120pcs | ✅ Recebido |
| Fonte ATX 250W (Sol Tech) | ✅ Desmontada — forneceu o capacitor eletrolítico 470µF/16V e cerâmico 100nF usados no fix do display |

### 2.2 Com problema / substituído
| Componente | Status |
|---|---|
| ESP32-S3-CAM N16R8 + OV5640 (1ª unidade) | ❌ **Aquecimento severo e imediato no próprio módulo da câmera** (não no ESP32) — indício forte de defeito de fábrica. Não usar essa unidade novamente. Substituto já comprado, anunciado como compatível especificamente com esse tipo de placa — aguardando entrega. |

### 2.3 Pendente (ainda não chegou / não comprado)
| Componente | Status |
|---|---|
| SHT31 (temp/umidade) | 🔶 Pedido feito (variante sonda à prova d'água) — aguardando entrega |
| Câmera OV5640 (substituta) | 🔶 Pedido feito, anunciada como compatível — aguardando entrega |
| Módulo Driver MOSFET IRF520 — backlight | 🔶 Identificado e especificado, **ainda não comprado** (1 unidade) |
| Módulo Driver MOSFET IRF520 — fans | 🔶 Identificado e especificado, **ainda não comprado** (3 unidades, uma por fan) |
| ADXL345 (acelerômetro/vibração) | ⏳ Ainda não comprado |
| MH-Z19B (CO2) | ⏳ Ainda não comprado |
| DS18B20 (fluxo bomba) | ⏳ Ainda não comprado |
| TCRT5000 ×2 (presença ovoscópio) | ⏳ Ainda não comprado |

### 2.4 Pinout confirmado — Controlador (YD-ESP32-S3 44P)
```
I2C (sensores):        SDA=8,  SCL=9
SPI (display):         MOSI=11, MISO=13, SCLK=12, CS=10
Display TFT:           DC=14, RST=21
Backlight (BL):        GPIO47 — RESERVADO para PWM via módulo MOSFET
                        (ver seção 9). Atualmente ainda ligado direto
                        no 5V (sempre aceso) até o módulo chegar.
Touch XPT2046:         TOUCH_CS=48
⚠️ USE_HSPI_PORT obrigatório no User_Setup.h (senão TFT_eSPI usa o barramento
   do flash por padrão no ESP32-S3 e trava com StoreProhibited)

Relé 8 canais (ACTIVE LOW — IN=LOW liga o relé):
  CH1=GPIO1  Resistência 220V/200W
  CH2=GPIO2  Umidificador 5V
  CH3=GPIO38 Bomba de água 12V
  CH4=GPIO39 Bomba de ar/O2 12V
  CH5=GPIO40 LED ovoscópio 220V
  CH6=GPIO41 Áudio MAX98357
  CH7=GPIO42 Reserva
  CH8=GPIO7  Reserva
⚠️ Todos os 8 pinos devem ser inicializados HIGH (desligado) ANTES de
   qualquer outra coisa no setup() — segurança contra boot instável.
   Os 2 canais de reserva permanecem livres (ideia de usar um deles pra
   cortar energia do display foi avaliada e descartada — ver seção 9).

Fans (planejado, ainda não implementado):
  PWM:      GPIO4, GPIO5, GPIO6      (um por fan — controle independente)
  Tacômetro: GPIO15, GPIO16, GPIO17  (um por fan — detecção individual de falha)

Pinos bloqueados (hardware, sem exceção):
  GPIO35/36/37 — PSRAM Octal SPI interno
  GPIO0, 3, 45, 46 — strapping pins
  GPIO19/20 — USB nativo (USB-OTG)
  GPIO43/44 — UART0 (USB-UART/CH343P)

Terminal 22 do terminal adapter tem contato intermitente — usar terminal
21 pra 3V3. Dois terminais 3V3 da fileira de cima têm defeito físico
(resolvido com fio extra num terceiro terminal 3V3 disponível).
```

### 2.5 Pinout confirmado — Câmera (ESP32-S3-N16R8-CAM + OV5640)
Confirmado por duas fontes comunitárias independentes (issue oficial do
arduino-esp32 + fórum Arduino, mesmos 15 valores exatos):
```
XCLK=15, SIOD=4, SIOC=5
D0-D7 (Y2-Y9) = 11, 9, 8, 10, 12, 18, 17, 16
VSYNC=6, HREF=7, PCLK=13
PWDN=-1, RESET=-1 (não usados nesta placa)
```
⚠️ Não confirmado fisicamente na 2ª unidade (a que chegou aquecendo).
Revisar depois que a substituta chegar.

### 2.6 Alimentação — mapa atual do conversor DC-DC 5V/3A
O pino **"5Vin"** do terminal adapter do ESP32 **NÃO é uma saída de 5V
confiável** quando a placa está alimentada só pela USB — mede ~0,6V
(assinatura de diodo de proteção reversa, sem corrente real por trás).

Tudo que precisa de 5V de verdade vem do **conversor DC-DC 12/24V→5V 3A**,
com o GND do conversor somado ao mesmo ponto de GND do ESP32 (não precisa
desviar nada pelo ESP32 — os GNDs só precisam se encontrar em algum ponto
comum, ex: terminal GND do relé). Atualmente alimenta:
- Bobinas do relé (8 canais)
- VCC do display (migrado do 3,3V — ver seção 7)
- Backlight do display (LED — migrado do 3,3V junto com o VCC)
- Em breve: lado de potência dos módulos MOSFET (backlight PWM + 3 fans)

Margem de corrente ampla — mesmo somando tudo isso, está bem abaixo dos 3A.

---

## 3. SOFTWARE — FIRMWARE DO CONTROLADOR (firmware)

Stack: C++/Arduino, PlatformIO, board `esp32-s3-devkitc-1` com
`board_build.arduino.memory_type = qio_opi` (força N16R8 = Quad Flash +
Octal PSRAM) e `board_upload.flash_size = 16MB`. Partição customizada
(`partitions_16mb.csv`) com OTA de 2 slots + LittleFS.

### Fases concluídas:
- **Fase 2** — Blink + Serial confirmado (upload via porta USB-UART/CH343P,
  COM8; GPIO38 = LED verde onboard)
- **Fase 3** — I2C scan confirmado (DS3231 em 0x68, AT24C32 em 0x57)
- **Fase 4** — Display ILI9341 + touch XPT2046 funcionando
- **Fase 5** — Animação de boot nativa (ovo rachando + pintinho, sem GIF/
  LittleFS — desenhado direto com primitivas do TFT_eSPI)
- **Fase 6** — RTC + WiFi nativo via touchscreen: painel de configurações,
  lista/detalhes/adicionar rede WiFi, teclado virtual completo. Swipe do
  topo foi removido (touchscreen não é sensível o suficiente pra gestos
  de arrastar — só toque seco é confiável nesse hardware).
- **Bluetooth (removido)** — Tentativa de HID host (mouse/teclado sem fio)
  via NimBLE. **Causa raiz do fracasso: o ESP32-S3 não tem rádio Bluetooth
  Clássico, só BLE** — e os periféricos do usuário (kit mouse X7 + teclado
  M96BT1) são Bluetooth Clássico, invisíveis por hardware pro scanner BLE.
  Todo o código Bluetooth foi removido do firmware — também suspeito de
  ter contribuído pra um bug de reset ao abrir a tela de WiFi (conflito
  de coexistência RF / memória do controlador BLE reservada
  permanentemente). **Nota:** um reset diferente (aleatório, por tempo)
  segue sendo investigado mesmo com o Bluetooth já removido — ver seção 8.
- **Fase 7** — Controle de Atuadores (relé): tela dedicada com os 8 canais,
  paginação via botão "Próximo >" (não setas, não swipe), toggle grande
  sem legenda de texto (só cor), linha inteira tocável (facilita uso sem
  caneta touch, que o usuário perdeu). Card de navegação na tela principal
  no mesmo estilo visual dos cards de sensor (1 célula, não 3).

### Em andamento — Fase 8 (integração com Firebase):
Ver seção 6 completa.

---

## 4. SOFTWARE — FIRMWARE DA CÂMERA (chocaiot-camera)

Projeto PlatformIO **separado** (pasta própria, não reaproveita o `src/`
do controlador). Ambiente: `esp32-s3-cam`.

Status: bring-up (Fase Cam-1) foi feito, câmera inicializou e capturou
com sucesso — mas a UNIDADE FÍSICA aqueceu perigosamente (ver seção 2.2).
Aguardando substituto chegar pra repetir o teste.

⚠️ Lição já aprendida e corrigida: **não incluir `-DARDUINO_USB_CDC_ON_BOOT=1`**
no `platformio.ini` da câmera (mesmo bug que já resolvemos no controlador —
desvia o Serial pra porta USB-OTG em vez da USB-UART/CH343P monitorada).

---

## 5. SOFTWARE — PWA (frontend/)

Estrutura: `index.html`, `app.js`, `style.css`, `manifest.json`,
`service-worker.js`. Deploy via `firebase deploy --only hosting`,
URL ativa em `https://SEU-PROJETO.web.app`.

### Funcional em produção:
- Login Google real, onboarding de chocadeira
- Dashboard com cards de sensor (Temperatura/Umidade/CO2/Dia da
  incubação) — ainda mostrando dados de teste da chocadeira "Cobaia"
  (id: `ID_DA_SUA_CHOCADEIRA`, conta seu-email@exemplo.com)
- Seção "Atuadores": 8 toggles estilo iOS espelhando os mesmos nomes/
  tensões/ordem dos canais do firmware (`RELAY_NAMES`). Escreve em
  `chocadeiras/{id}.atuadores.ch0` até `ch7` via update com dot-notation.
  Escuta o documento em tempo real — reflete mudanças vindas do firmware
  ou de outra aba, com proteção contra loop de escrita.

### Lição aprendida (bug já corrigido):
Ao adicionar CSS novo com `>>` (append) num arquivo que não tinha sido
criado com o conteúdo original primeiro, o resultado ficou **sem** esse
conteúdo original — só as regras novas sobreviveram. Lição: sempre
partir do conteúdo real e completo antes de usar append.

### Firestore Security Rules
**Nenhuma alteração foi necessária** para liberar a escrita de
atuadores — a regra existente já libera update de qualquer campo do
documento da chocadeira pro dono.

---

## 6. FASE 8 — INTEGRAÇÃO FIRMWARE ↔ FIREBASE (em andamento)

### Arquitetura decidida:
O ESP32 autentica como **Service Account** (nível Admin, via biblioteca
`mobizt/FirebaseClient`), contornando a necessidade de bater `donoId`
nas regras do Firestore (Service Account ignora Security Rules por
padrão). Como o Firestore **não suporta escuta em tempo real em
dispositivos embarcados**, o firmware vai **perguntar periodicamente**
em vez de reagir instantaneamente.

### Descobertas técnicas importantes (via pesquisa, não assumidas):
- `#define ENABLE_SERVICE_AUTH` é **obrigatório** antes de incluir
  `FirebaseClient.h` — corrige bug documentado de assinatura JWT em
  ESP32-S3 (issue #282 da biblioteca)
- Relógio precisa estar sincronizado via **NTP** ANTES de autenticar
  (JWT tem timestamp)
- Classe correta é `ServiceAuth` (não "CustomAuth")

### Fase 8a — só autenticação (código entregue, AINDA NÃO TESTADO):
- Sketch isolado (`fase8a_auth_teste.cpp`), substituindo temporariamente
  o `main.cpp` (backup do Fase 7 já feito)
- Credenciais reais já preenchidas (`firebase_credentials_TEMPLATE.h`)
- `.gitignore` atualizado pra proteger credenciais
- WiFi de teste: rede "MARI12"
- **Status: ainda não confirmado se rodou/autenticou com sucesso na
  placa real** — investigação do reset aleatório (seção 8) tomou
  prioridade antes de retomar esse teste.

### Fase 8b — ler atuadores + aplicar no relé (não iniciada):
Vai ler `chocadeiras/{id}.atuadores` e chamar `setRelay()` (já existe
desde a Fase 7). **Bloqueada até a Fase 8a ser confirmada.**

---

## 7. ENERGIA DO DISPLAY — ✅ RESOLVIDO

### Diagnóstico (histórico):
Trilho de 3,3V da placa estava sobrecarregado — oscilava entre 2,92V e
3,3V (~11% de sag) sob carga, causando flicker visual no display e
suspeito de contribuir pra reset ao abrir a tela de WiFi.

### Solução aplicada, na ordem de segurança:
1. **Capacitor de desacoplamento** — eletrolítico 470µF/16V + cerâmico
   100nF soldados nos pinos VCC/GND do display (peças salvas de uma
   fonte ATX desmontada). Melhorou a estabilidade da leitura no
   multímetro, mas sozinho não eliminou o flicker visualmente.
2. **Verificação do jumper J1** (parte de trás do display) — testado
   com multímetro em modo continuidade: **não apitou → J1 está aberto**,
   confirmando que o regulador `XC6206P332MR` embutido está ativo e
   5V no VCC é seguro.
3. **Migração de VCC + backlight (LED) para 5V** — os dois fios que
   antes iam pro terminal 3,3V foram redirecionados pro mesmo conversor
   DC-DC que já alimenta o relé. GND não mudou.

### Resultado confirmado:
- Flicker **eliminado**
- Regulador U1 do display fica **morno, não quente**, após minutos de uso
- Tensão medida: **5,09V estável**
- O capacitor continua fisicamente no lugar (nos pinos do display) —
  agora filtrando o trilho de 5V em vez do 3,3V. Não foi removido; não
  há motivo pra remover.

---

## 8. PROBLEMA ATIVO — RESETS ALEATÓRIOS (em investigação)

**Sintoma:** o sistema reinicia sozinho, sem ação específica do
usuário, sempre depois de ficar ligado um tempo (minutos a horas).

**Já eliminado como causa:**
- Não é a migração do display pra 5V (o problema já existia antes)
- Não é uma ação específica (tocar em Atuadores, abrir WiFi) — é um
  padrão de acúmulo ao longo do tempo, não um gatilho pontual

**Hipóteses em aberto** (padrão "funciona bem no início, quebra depois
de um tempo" é clássico de):
- Vazamento de memória (heap sendo consumido aos poucos até esgotar)
- Fragmentação de heap
- Algum componente esquentando ao longo do tempo até falhar
  intermitentemente

**Diagnóstico entregue, ainda não executado:** função `esp_reset_reason()`
pra adicionar no início do `setup()` — na próxima vez que o reset
acontecer, essa linha vai dizer exatamente o motivo (brownout, watchdog,
crash/panic, etc.) em vez de continuar supondo.

**Pergunta em aberto, ainda sem resposta do usuário:** qual firmware
está gravado no momento em que esse reset acontece — o da Fase 7
(Atuadores, sem Firebase) ou já uma tentativa da Fase 8a (autenticação,
que usa TLS/WiFiClientSecure, muito mais pesado em memória)? Essa
resposta muda bastante a direção da investigação.

---

## 9. CONTROLE PWM PLANEJADO — BACKLIGHT E FANS (especificado, não comprado)

### Componente escolhido: Módulo Driver MOSFET IRF520
Barato (~R$4-5), amplamente disponível (AliExpress e lojas BR), aceita
sinal de controle em 3,3-5V (compatível direto com GPIO do ESP32-S3,
sem level-shifter), suporta até 5A/24V do lado da carga.

### Uso 1 — Backlight do display (1 unidade):
- Corrente medida do backlight: **30mA** (bem dentro da capacidade do
  módulo)
- Fiação: `SIG→GPIO47`, `VCC→3V3`, `GND→comum`, `V+→5V (mesmo conversor)`,
  `V-OUT→fio do LED do display` (redirecionado do 5V direto atual)
- Código (ESP32 Arduino core 3.x já suporta direto, sem setup manual
  de canal LEDC):
  ```cpp
  #define BACKLIGHT_PIN 47
  analogWrite(BACKLIGHT_PIN, 230);  // operação normal (200-255)
  analogWrite(BACKLIGHT_PIN, 65);   // standby (50-80)
  analogWrite(BACKLIGHT_PIN, 0);    // desligado (apaga backlight, mantém touch ativo)
  ```
- Pedido do usuário (registrado anteriormente): brilho alto em operação
  normal, baixo em standby, apagado quando não em uso.

### Uso 2 — Fans (3 unidades, uma por fan):
Confirmado que os fans precisam de **controle independente**, não
sincronizado — o usuário quer que a chocadeira tome decisões por fan
individualmente (ex: reduzir a velocidade de uma fan específica que
sopra muito perto dos ovos), o que só é possível com um módulo MOSFET
por fan, alimentando os pinos já reservados desde o início do projeto
(GPIO4/5/6 pra PWM, GPIO15/16/17 pra tacômetro — um par por fan).

### Ideia avaliada e DESCARTADA — desligar o display via relé:
Cogitou-se rotear VCC+LED do display por um canal reserva do relé pra
desligamento completo após inatividade. Descartada por dois motivos:
1. O contato correto seria **NC** (normalmente fechado), não NO —
   mesma lógica de segurança "falha pro estado visível" que já usamos
   nos outros 8 canais, só invertida (aqui queremos que falhas
   **mantenham** a tela ligada, não desliguem)
2. Mesmo com NC, cortar o **VCC inteiro** desligaria também o chip de
   touch (XPT2046), que compartilha a mesma alimentação — impossibilitando
   "tocar pra acender de novo"
3. Cortar só o **LED** via relé preservaria o touch, mas seria
   redundante com o módulo MOSFET/PWM já planejado (mesmo resultado via
   `analogWrite(0)`, sem gastar o canal de relé nem introduzir desgaste
   mecânico de um contato clicando várias vezes por dia)

**Decisão final do usuário: usar exclusivamente MOSFET/PWM para o
backlight** (incluindo desligamento total via `analogWrite(0)`). Os 2
canais de reserva do relé permanecem livres.

---

## 10. LGPD & SEGURANÇA — CHECKLIST CRIADO, AINDA NÃO EXECUTADO

Uma aba inteira foi adicionada ao checklist interativo
(`chocaiot_checklist.html`) com 10 itens (lgpd1-lgpd10): mapeamento de
dados pessoais, política de privacidade, base legal do consentimento,
exclusão de conta em cascata, Firestore Rules "nega tudo" padrão,
auditoria de segredos no GitHub, rotação de credencial MQTT, verificação
de integridade de OTA, plano de resposta a incidentes, proteção contra
abuso de cota.

**Nenhum item foi revisitado desde a criação.** Vale retomar antes de
qualquer uso fora do ambiente de teste.

---

## 11. PENDÊNCIAS ATIVAS (ordem sugerida)

1. ⏳ **Adicionar `esp_reset_reason()` no setup()** e aguardar o próximo
   reset acontecer sozinho — diagnóstico definitivo do motivo
2. ⏳ Confirmar qual firmware estava rodando quando os resets acontecem
   (Fase 7 puro ou já com tentativa de Fase 8a)
3. ⏳ Retomar e confirmar Fase 8a (autenticação Firebase) na placa real
4. ⏳ Fase 8b — ler atuadores do Firestore e aplicar no relé de verdade
5. ⏳ Comprar 4 módulos MOSFET IRF520 (1 backlight + 3 fans)
6. ⏳ Instalar e testar backlight PWM (3 níveis de brilho)
7. ⏳ Testar substituto da câmera quando chegar (bring-up + teste térmico
   cuidadoso antes de deixar rodando sem supervisão)
8. ⏳ Testar SHT31 quando chegar
9. ⏳ Testar relé com carga real (bombas 12V primeiro, antes de 220V)
10. ⏳ Implementar e testar controle PWM dos 3 fans (independente)
11. ⏳ Retomar checklist de LGPD/Segurança
12. ⏳ Comprar sensores restantes: ADXL345, MH-Z19B, DS18B20, TCRT5000×2

---

## 12. LIÇÕES APRENDIDAS (acumulado)

- **Sempre verificar a porta de upload/monitor correta** — placas com
  USB-OTG + USB-UART (CH343P) precisam da flag `ARDUINO_USB_CDC_ON_BOOT`
  **desligada** pra Serial sair pela porta monitorada
- **TFT_eSPI no ESP32-S3 exige `USE_HSPI_PORT`**
- **Bibliotecas de nicho (NimBLE, FirebaseClient) mudam nomes de classe/
  método entre versões** — sempre verificar exemplo oficial atual antes
  de escrever código
- **Testar contato elétrico "encostando com o dedo" não é confiável**
  pra componentes que precisam reagir rápido — só solda ou jacaré de
  teste valem como teste real
- **Gestos de swipe não são confiáveis neste touchscreen específico** —
  só toque seco
- **Ao usar append (`>>`) num arquivo, sempre partir do conteúdo real
  e completo**
- **Componentes de câmera barata podem vir defeituosos** — aquecimento
  imediato e severo é motivo pra trocar a peça, não insistir no código
- **Contato NC vs NO em relé muda o comportamento de falha** — NC
  ("normalmente fechado") mantém o circuito ativo por padrão, ideal
  quando falha deve manter algo visível/ligado (ex: display); NO
  ("normalmente aberto") mantém desligado por padrão, ideal quando
  falha deve manter algo seguro/desligado (ex: resistência 220V)
- **Antes de rotear um sinal por um relé pra "desligar algo", checar se
  esse algo compartilha alimentação com outra função que ainda precisa
  ficar ativa** (ex: touch controller e VCC do display) — pode inviabilizar
  a ideia por completo
- **Identificar NC/NO com multímetro:** modo continuidade, uma ponta no
  COM, outra ponta alternando entre os outros dois terminais, com o relé
  em repouso (sem energia) — o que apitar É o NC

---

## 13. DECISÕES DE DESIGN — REFERÊNCIA RÁPIDA (atualizada)

| Decisão | Motivo |
|---|---|
| ESP32-S3 (não ESP32 clássico) | Aceleração de hardware pra IA/visão computacional (ESP-DL) |
| N16R8 (16MB flash + 8MB PSRAM) | Buffer de câmera de maior resolução exige mais RAM |
| Bluetooth removido do firmware | ESP32-S3 não tem rádio Bluetooth Clássico; periféricos do usuário são Clássicos, não BLE |
| Relé ACTIVE LOW, init HIGH primeiro | Segurança — evita acionamento acidental no boot |
| Paginação por botão, não swipe | Touchscreen não detecta arrasto de forma confiável |
| Service Account (não user auth) no ESP32 | Dispositivo não pode "logar" como usuário final; Service Account ignora Security Rules |
| Polling (não listener) no firmware pra Firestore | Realtime listening não existe pra dispositivos embarcados no Firestore |
| Display migrado pra 5V (VCC + backlight) | Trilho de 3,3V estava sobrecarregado; J1 confirmado aberto (regulador ativo, 5V seguro) |
| Backlight via MOSFET/PWM, não relé | Preserva alimentação do touch controller (compartilha VCC); PWM já cobre "desligar" sem gastar canal de relé |
| 1 módulo MOSFET por fan (3 no total) | Controle independente é requisito — chocadeira deve poder ajustar cada fan com base em sua posição/função |
| Fases isoladas antes de integrar (câmera, Firebase) | Padrão que já evitou múltiplas rodadas de debug confuso no projeto |

---

*Fim do checkpoint. Este documento pode ser colado no início de uma nova conversa para retomar o contexto técnico completo do projeto ChocaIoT.*
