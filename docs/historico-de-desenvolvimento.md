# ChocaIoT — Contexto do Projeto

> Baseado em `docs/CHECKPOINT_ChocaIoT.md`, corrigido e atualizado em 2026-09-17
> com o estado real do código verificado nesta sessão (firmware compilado e
> testado na placa; PWA implantado em produção). Onde o checkpoint original
> descrevia algo como "planejado" ou "não testado" mas o código atual já mostra
> diferente, este arquivo reflete o código — não o checkpoint antigo.

**Status geral:** Pré-produção avançada. Firmware do controlador com a
integração Firebase (nuvem ↔ atuadores) **funcionando e testada**, PWA em
produção com dashboard redesenhado e sync bidirecional confirmado, e um
problema de hardware ativo (resets aleatórios) ainda em investigação.

---

## 1. Visão geral

ChocaIoT é uma chocadeira automática inteligente para ~70 ovos de galinha:
- Controlador principal: ESP32-S3 (YD-ESP32-S3 44P, módulo N16R8)
- Câmera separada: ESP32-S3-CAM + OV5640 (projeto PlatformIO independente,
  `chocaiot-camera/`) — hardware com problema, ver seção 2.2
- Backend Firebase (Auth, Firestore, Hosting) — em produção
- PWA (`SEU-PROJETO.web.app`) — em produção, controle de atuadores
  bidirecional confirmado funcionando com o firmware real
- Display touchscreen ILI9341 3.2" — interface local completa (WiFi,
  atuadores, configurações, memória, câmera placeholder, screensaver)

---

## 2. Hardware — status atual

### 2.1 Confirmado funcionando
| Componente | Status |
|---|---|
| YD-ESP32-S3 44P (controlador, N16R8) | ✅ Pinout confirmado fisicamente |
| Display ILI9341 3.2" SPI + touch XPT2046 | ✅ Funcionando, flicker resolvido (seção 7) |
| RTC DS3231 | ✅ Confirmado via I2C scan (0x68), em uso |
| Relé 8 canais 5V (SRD-05VDC-SL-C) | ✅ Pinout confirmado, lógica testada (sem carga real ainda) |
| DC-DC Step Down 12/24V→5V 3A | ✅ Em uso — alimenta o relé e o display |
| Bateria LIR2032 3,6V | ✅ Recebida (backup do RTC) |
| MicroSD 32GB | ✅ Recebido, reservado pra câmera (não usado ainda) |
| SHT31 (temp/umidade, sonda IP68) | 🔶 Chegou (2026-09-21), **integrado no firmware (Fase 20)** — código compilado, mas ainda NÃO testado fisicamente, ver seção 6 |

### 2.2 Com problema / substituído
| Componente | Status |
|---|---|
| ESP32-S3-CAM N16R8 + OV5640 (1ª unidade) | ❌ Aquecimento severo no módulo da câmera — defeito de fábrica suspeito. Não reutilizar. Substituta chegou (2026-09-21), ainda não instalada/testada — ver `chocaiot-camera/` (projeto separado). |
| **Placa ESP32-S3-CAM (V1695) usada nos testes de 2026-09-24** | ❌ **DEFEITO NA PLACA (não no módulo DCX-OV5640-100MM):** GPIO **9, 10, 11, 12 e 13** (D0, D1, D3, D4 e PCLK do DVP) estão **em curto entre si** — confirmado por firmware (`chocaiot-camera/src/diag_curto_cam7.cpp.bk`, mesmo resultado SEM o cabo flat) e por multímetro (11↔12 e 12↔13 apitam; nenhum apita pro GND). Sintoma: sensor responde (PID 0x5640, SCCB ok), PCLK/VSYNC/HREF vivos, mas JPEG nunca fecha (`fb_get` timeout 8 s) e RGB565 chega com bits D0/D1/D3/D4 = 1 sempre. SD (SDMMC 1-bit, CLK 39/CMD 38/D0 40) e PSRAM funcionam; temperatura do chip 39-46 °C (ok). Pinagem da câmera confirmada pela serigrafia. Placa a ser trocada (comprar outra; ao chegar, rodar o localizador de curto ANTES de ligar a câmera). |

### 2.3 Pendente (ainda não chegou / não comprado)
| Componente | Status |
|---|---|
| Módulo Driver MOSFET IRF520 — backlight (1x) | 🔶 Especificado, ainda não comprado |
| Módulo Driver MOSFET IRF520 — fans (3x) | 🔶 Especificado, ainda não comprado |
| ADXL345 (vibração), MH-Z19B (CO2), DS18B20 (fluxo), TCRT5000 ×2 (presença ovoscópio) | ⏳ Ainda não comprados |

**Importante:** o SHT31 agora é lido no firmware (Fase 20, ver seção 6) —
os cards de Temperatura/Umidade na tela principal e no PWA já mostram o
valor real assim que o sensor estiver fisicamente conectado e a primeira
leitura for confirmada (Serial mostra "[SHT31] Sensor de
temperatura/umidade OK"); até lá, e para os sensores ainda não comprados
(CO2 etc.), o card mostra `--.-`. A tela "Automação" nas Configurações
continua marcada honestamente como "Em desenvolvimento" (perfis de
incubação automatizados, não a leitura crua dos sensores).

### 2.4 Pinout confirmado — Controlador (YD-ESP32-S3 44P)
```
I2C (sensores):        SDA=8,  SCL=9
SPI (display):         MOSI=11, MISO=13, SCLK=12, CS=10
Display TFT:           DC=14, RST=21
Touch XPT2046:         TOUCH_CS=48

Relé 8 canais (ACTIVE LOW — IN=LOW liga o relé):
  CH1=GPIO1  Resistência 220V/200W
  CH2=GPIO2  Umidificador 5V
  CH3=GPIO38 Bomba de água 12V
  CH4=GPIO39 Bomba de ar/O2 12V
  CH5=GPIO40 LED ovoscópio 220V
  CH6=GPIO41 Áudio MAX98357
  CH7=GPIO42 Cooler Sistema 12V DC (Fase 33 -- era "Reserva")
  CH8=GPIO7  Reserva
⚠️ Todos os 8 pinos são inicializados HIGH (desligado) em initRelays(),
   ANTES de qualquer outra coisa no setup() — segurança contra boot instável.

Fans (planejado, ainda não implementado no firmware):
  PWM:      GPIO4, GPIO5, GPIO6      (um por fan)
  Tacômetro: GPIO15, GPIO16, GPIO17  (um por fan)

Backlight do display: ver seção 7 — ⚠️ ponto em aberto entre este
documento e o checkpoint anterior, revisar com o usuário.

Pinos bloqueados (hardware, sem exceção):
  GPIO35/36/37 — PSRAM Octal SPI interno
  GPIO0, 3, 45, 46 — strapping pins
  GPIO19/20 — USB nativo (USB-OTG)
  GPIO43/44 — UART0 (USB-UART/CH343P)

SD_CS = GPIO18 (Fase 21/27)

⚠️ Confirmado por fotos reais dos conectores físicos (2026-09-22):
GPIO22/23/24 NÃO existem nos terminais desta placa de 44 pinos —
não tente usá-los pra nada. Pinos física e visivelmente presentes nos
conectores (por foto, pode haver mais em terminais não fotografados):
GND, 0, 4, 5, 6, 15, 16, 17, 18, 19, 20, 35, 36, 37, 45, 47, RX, TX,
RST, 3V3 — a maioria já blocked/reservada (ver acima + seção 9), só o
18 estava livre e virou SD_CS.
```
⚠️ Exceção documentada (Fase 15): GPIO0 é usado como fonte de wake do
deep sleep (`esp_sleep_enable_ext0_wakeup`, botão físico BOOT) — não é
uso como GPIO de propósito geral, não conflita com o papel dele na
seleção de modo de boot.
⚠️ `USE_HSPI_PORT` é obrigatório no `User_Setup.h` (senão TFT_eSPI usa o
barramento do flash por padrão no ESP32-S3 e trava com StoreProhibited).

### 2.5 Pinout confirmado — Câmera (ESP32-S3-N16R8-CAM + OV5640)
```
XCLK=15, SIOD=4, SIOC=5
D0-D7 (Y2-Y9) = 11, 9, 8, 10, 12, 18, 17, 16
VSYNC=6, HREF=7, PCLK=13
PWDN=-1, RESET=-1 (não usados nesta placa)
```
Confirmado por duas fontes comunitárias independentes. Não confirmado
fisicamente na 2ª unidade (a que chegou aquecendo) — revisar quando a
substituta chegar.

### 2.6 Alimentação
O pino "5Vin" do terminal adapter do ESP32 **não é confiável** quando a
placa está alimentada só por USB (mede ~0,6V, assinatura de diodo de
proteção reversa). Tudo que precisa de 5V real vem do conversor DC-DC
12/24V→5V 3A (GND comum com o ESP32 via terminal GND do relé). Alimenta
hoje: bobinas do relé (8 canais) e VCC do display. Margem de corrente
ampla mesmo somando tudo.

---

## 3. Firmware do controlador (`src/main.cpp`)

Stack: C++/Arduino, PlatformIO, board `esp32-s3-devkitc-1`,
`board_build.arduino.memory_type = qio_opi` (N16R8 = Quad Flash + Octal
PSRAM), `board_upload.flash_size = 16MB`, partição customizada
(`partitions_16mb.csv`), filesystem LittleFS.

### Fases de UI concluídas (tela touch, sem Firebase)
- Boot animation nativa (ovo rachando + pintinho, desenhado com
  primitivas do TFT_eSPI, sem GIF/LittleFS)
- RTC + WiFi nativo via touchscreen (painel de configurações, lista/
  detalhes/adicionar rede, teclado virtual completo)
- Bluetooth **removido do firmware** — ESP32-S3 não tem rádio Bluetooth
  Clássico (só BLE), e os periféricos do usuário são Clássicos. Também
  suspeito de ter contribuído para um bug de reset ao abrir a tela de WiFi.
- Controle de Atuadores: tela dedicada com os 8 canais, paginação via
  botão "Próximo >" (gestos de swipe não são confiáveis neste
  touchscreen — só toque seco)

### Configurações — completas nesta sessão
Grid 3×2 na tela de Configurações, fontes maiores (estilo Android,
pensado pra dedos grandes no display 3.2"):
| Botão | Estado |
|---|---|
| WiFi | ✅ Funcional (já existia) |
| Memória | ✅ **Novo** — RAM/heap/flash/PSRAM com barra de progresso, atualiza ao vivo a cada 500ms |
| Data/Hora | ✅ **Corrigido** — o botão existia mas não tinha nenhuma ação ligada (bug). Agora abre um editor manual (steppers −/+) que grava no RTC |
| Sistema | ✅ Funcional (já existia) — versão, chip, flash, RAM livre, uptime, relés |
| Automação | 🔶 Placeholder "Em desenvolvimento" — cobre Perfil de Incubação + Limites de Segurança, que dependem de leitura de sensor (nenhum sensor está lido ainda) e de lógica de controle automático que não existe no firmware |
| Tela | ✅ **Novo** — ajusta o tempo até o screensaver aparecer (persistido em `/display.cfg` na LittleFS) |

A tela **Sistema** (dentro de Configurações) agora também mostra **"ID
Disp.:"** — é só o ID do documento do Firestore (parte depois de
`chocadeiras/` em `FIRESTORE_DOCUMENT_PATH`, via `getDeviceId()`), sem
nenhuma escrita nova. Serve pro usuário copiar esse valor na tela
"Vincular Chocadeira" do PWA (seção 5) pra associar o dispositivo à conta.

### Screensaver (substituiu a ideia de "sleep" do ILI9341)
Ver seção 7 para o porquê. Depois de `screenOffTimeoutSec` (padrão 120s)
sem toque em **qualquer** tela do sistema, a tela fica preta de verdade
(o LCD bloqueia a luz de fundo pixel a pixel) com um relógio digital
discreto no centro e o logo "ChocaIoT" flutuando estilo screensaver de
DVD. Qualquer toque volta exatamente pra tela de onde saiu (não sempre
pra home). A tela de Câmera em tela cheia é isenta — nunca aciona o
screensaver sozinha.

### Câmera — card + tela cheia (placeholder)
Card "Câmera" na home (grid principal, ao lado do card Atuadores). Toque
abre uma tela cheia com borda tracejada + ícone de câmera + "Módulo em
desenvolvimento" — mesma linguagem visual do painel de câmera no PWA
(consistência entre os dois). Não há feed real: o hardware de câmera é
outra placa (`chocaiot-camera/`, projeto separado), ainda não integrada.
Toque em qualquer lugar da tela cheia volta pra home.

---

## 4. Firmware da câmera (`chocaiot-camera/`)

Projeto PlatformIO **separado** (pasta própria, não reaproveita o `src/`
do controlador). Ambiente: `esp32-s3-cam`. Status: bring-up (Fase Cam-1)
feito, câmera inicializou e capturou com sucesso, mas a unidade física
aqueceu perigosamente (ver seção 2.2) — aguardando substituto.

⚠️ Não incluir `-DARDUINO_USB_CDC_ON_BOOT=1` no `platformio.ini` da
câmera (mesmo bug já resolvido no controlador — desvia o Serial pra
porta USB-OTG em vez da USB-UART/CH343P monitorada).

---

## 5. PWA (`frontend/`, repo separado em `<SEU_CAMINHO>`)

Estrutura: `index.html`, `app.js`, `style.css`, `manifest.json`,
`service-worker.js`. Deploy via `firebase deploy --only hosting`
(a partir da raiz do repo EggBr), URL em `https://SEU-PROJETO.web.app`.

### Funcional em produção (confirmado, não só planejado)
- Login Google real
- **Vínculo por Device ID** (substituiu o antigo formulário de "criar
  chocadeira"): o documento da chocadeira já existe (criado junto com o
  firmware, ID fixo — não é mais o PWA que cria documentos novos). Conta
  sem dispositivo associado (`donoId` de nenhuma chocadeira bate com o
  uid) cai na tela "Vincule sua chocadeira" — usuário digita o ID exibido
  na tela Sistema do display físico (seção 3), o PWA faz um
  `update({donoId: uid})` direto no documento (sem `get()` antes: as
  Security Rules bloqueiam leitura de um dispositivo que ainda não é seu,
  então o próprio resultado do `update()` — sucesso, `not-found` ou
  `permission-denied` — decide a mensagem). Conta com dispositivo já
  vinculado (`donoId == uid`) vai direto ao dashboard
- **Bloqueio de controles offline** (bug crítico corrigido): antes disso
  dava pra tocar num atuador ou no botão "Iniciar Chocadeira" mesmo com o
  dispositivo offline — a escrita no Firestore tinha sucesso e só o
  feedback avisava do problema depois do toque. Agora
  `atualizarControlesDisponibilidade()` desabilita de verdade (`disabled`
  + tooltip "Chocadeira offline — ações indisponíveis") todos os toggles
  de atuador e o botão de ciclo sempre que `estadoConexao !== "online"` —
  chamada toda vez que o status é reavaliado (heartbeat, evento
  online/offline do navegador, ou o intervalo de 5s)
- **Dashboard redesenhado** para espelhar o layout do display físico:
  - Header com "ChocaIoT" em gradiente dourado (`#c8890a`→`#f5c842`) e
    brilho animado sutil; menu hambúrguer agrupa status/email/logout
    (antes ficavam soltos no topbar)
  - Animação de abertura: ovo racha, pintinho sai correndo revelando o
    texto "ChocaIoT" no caminho (via `clip-path`), termina parado ao
    lado do nome — com duração mínima garantida mesmo se o login
    resolver mais rápido
  - Painel de câmera reservando metade da tela (placeholder com ícone +
    "Módulo em desenvolvimento" — mesma peça visual do firmware)
  - 6 cards de sensor (Temperatura/Umidade/CO2/Status/Viragem/Timer,
    igual ao display físico) + **um único card "Atuadores"** agrupando
    os 8 canais em linhas (não mais 8 cards separados)
  - Mobile: os painéis de Sensores/Atuadores viram abas (não empilham
    verticalmente) — evita ter que rolar pra ver os atuadores
  - Cada linha de atuador acende uma borda neon (`#38bdf8`) que preenche
    o contorno em 3s ao ligar e esvazia em 2s ao desligar (CSS puro,
    `conic-gradient` + `mask` + `@property --border-fill: inherits:true`
    — sem o `inherits:true` o `::after` não recebe o valor setado pelo
    elemento pai, foi um bug real encontrado e corrigido nesta sessão)
- **Sync bidirecional com o firmware confirmado funcionando**: toggle no
  PWA liga o relé físico em até ~4s (intervalo de polling do firmware);
  toque físico na tela do firmware atualiza o PWA quase instantaneamente
  (via `onSnapshot` do Firestore)

### Schema real do Firestore (verificado, não o README genérico)
Coleção `chocadeiras/{id}` (chocadeira "Cobaia", id
`ID_DA_SUA_CHOCADEIRA`, dono `seu-email@exemplo.com`):
- `donoId` (string, uid do Firebase Auth) — dono da chocadeira. Setado uma
  única vez pelo fluxo de vínculo (seção 5); depois disso as Security
  Rules travam qualquer mudança nesse campo (seção 5)
- `atuadores.ch0` até `ch7` (boolean) — mesma ordem/nomes de `RELAY_NAMES`
  no firmware. PWA escreve com `update({"atuadores.chN": bool})`
  (dot-notation, não sobrescreve os outros campos)
- `firmwareLastSeen` (timestamp) — heartbeat: o firmware grava a cada
  `FB_HEARTBEAT_INTERVAL_MS` (30s). O PWA diferencia 3 estados a partir
  disso: "online" (heartbeat recente), "chocadeira offline" (heartbeat
  velho, mas o navegador tem internet) e "sem conexão" (o navegador em
  si está offline, `navigator.onLine === false` — nesse caso nem dá pra
  saber o estado real da placa)
- `cicloIniciadoEm` (timestamp) — gravado pelo botão de ciclo no
  cabeçalho do PWA (`FieldValue.serverTimestamp()`). O contador "Dia
  X/Y" é calculado 100% no client a partir desse campo — **o firmware
  não sabe nada sobre isso ainda**, não há controle automático de
  temperatura/umidade atrelado ao ciclo (só a contagem de dias)
- `cicloPausadoEm` (timestamp | ausente) — pausa o contador sem perder
  o progresso. Enquanto presente, o dia atual fica congelado nesse
  instante em vez de avançar com `Date.now()`. Ao retomar, o PWA desloca
  `cicloIniciadoEm` pra frente pelo tempo que ficou pausado (não pula
  dias) e apaga este campo
- `cicloDuracaoDias` (number, padrão 21) — meta de dias do ciclo,
  ajustável no modal "Configurações do Ciclo" (15-35 dias, cobre
  galinha/codorna/pata/ganso). Usada pra calcular o "Dia X/Y" e a
  previsão de eclosão exibida
- Botão de ciclo fica no cabeçalho (sticky), ao lado do menu hambúrguer:
  sem ciclo ativo inicia direto; com ciclo ativo/pausado abre o modal
  (progresso, duração, pausar/retomar, parar — "parar" passa por uma
  confirmação dentro do próprio modal antes de apagar `cicloIniciadoEm`/
  `cicloPausadoEm`)
- Subcoleção `leituras/{id}`: `temperatura`, `umidade`, `co2`, `timestamp`
  — **ainda não populada pelo firmware** (nenhum sensor lido ainda),
  então os cards de sensor no PWA também mostram `--` até isso existir
- `comandoDesligar` (boolean, Fase 15) — só existe/vale `true` pelo
  tempo entre o PWA escrever o comando e o firmware processar (poll de
  2.5s). O firmware sempre limpa de volta pra `false` (síncrono, antes
  de dormir) — nunca fica `true` "em repouso" no documento. Se aparecer
  `true` persistente no Console, é sinal de que o firmware não conseguiu
  confirmar a limpeza (offline?) antes do timeout de 5s
- `comandoReiniciar` (boolean, Fase 28) — mesmo padrão do
  `comandoDesligar` acima, mas o firmware processa com `ESP.restart()`
  (software reset) em vez de deep sleep — volta sozinho em poucos
  segundos, sem precisar de botão físico. Escrito pelo botão "Reiniciar"
  do modal de Desligar no PWA

### Firestore Security Rules
Nenhuma alteração foi necessária pra liberar escrita de atuadores — a
regra existente já libera update de qualquer campo do documento da
chocadeira pro dono. O firmware ESP32 autentica como Service Account
(Admin), que ignora Security Rules por padrão — comentado explicitamente
no próprio `firestore.rules`.

**Exceção — vínculo (pairing):** a regra original só permitia `update`
pro dono já existente (`ehDonoDaChocadeira() && donoIdNaoMudou()`), o que
tornava o "vincular" via client update **impossível por padrão**
(nenhuma escrita mudando `donoId` passava — inclusive a primeira, já que
sem dono ainda ninguém "é o dono"). Adicionada `ehClaimValido()`: libera
update quando o documento **ainda não tem `donoId`** e a escrita muda
**só** esse campo (`diff().affectedKeys().hasOnly(["donoId"])`), pro uid
de quem está autenticado. Isso fecha o vínculo sem precisar de Cloud
Function (que não existe neste projeto) e sem abrir brecha pra "roubar"
uma chocadeira já vinculada — depois do primeiro claim, a regra antiga
(dono + `donoIdNaoMudou()`) volta a valer.
Deploy: `firebase deploy --only firestore:rules` (separado do deploy de
hosting).

### Lição aprendida
Nunca usar `>>` (append) num arquivo sem antes garantir que o conteúdo
original completo já está presente — um append cego pode acabar
substituindo o arquivo em vez de complementá-lo, dependendo de como foi
criado.

---

## 6. Integração Firmware ↔ Firebase — ✅ concluída e funcionando

*(O checkpoint anterior descrevia isso como "Fase 8a não testada" e
"Fase 8b não iniciada" — está desatualizado. Ambas estão implementadas,
compiladas, gravadas na placa real e confirmadas funcionando com o PWA.)*

### Arquitetura
- ESP32 autentica como **Service Account** via `mobizt/FirebaseClient`
  (`#define ENABLE_SERVICE_AUTH` antes de incluir `FirebaseClient.h` —
  obrigatório, corrige bug de assinatura JWT no ESP32-S3, issue #282 da
  lib). Classe correta é `ServiceAuth`.
- Relógio sincronizado via NTP **antes** de autenticar (JWT tem
  timestamp) — `syncTimeAndStartFirebase()`, chamado ao conectar WiFi
  (boot ou manualmente pela tela), com `app.setTime(time(nullptr))`.
- Firestore **não tem listener em tempo real pra dispositivos
  embarcados** nesta lib (só Realtime Database tem SSE) — o firmware
  faz **polling a cada 4s** (`FB_POLL_INTERVAL_MS`) via `Docs.get()` com
  `DocumentMask("atuadores")`. Escrita local→nuvem é imediata via
  `Docs.patch()`, só no campo do canal que mudou.
- Documento fixo em `FIRESTORE_DOCUMENT_PATH` (`"chocadeiras/ID_DA_SUA_CHOCADEIRA"`).
  Trocar aqui se um dia existir mais de uma chocadeira.

### Detalhe técnico não-óbvio (achado nesta sessão)
O JSON que a API REST do Firestore devolve vem **pretty-printed** (com
espaços e quebras de linha entre chave e valor: `"ch0": {\n  "booleanValue": false\n}`).
Um parser que busca a sequência exata de caracteres (`"ch0":{"booleanValue":`)
falha silenciosamente. `applyActuatorsFromFirestore()` busca a chave,
depois o próximo `"booleanValue"` (com limite de distância), e pula
espaços/quebras até achar o valor — mais robusto.

### Fase 9 — Coexistência toque local × nuvem, e confirmação de risco (2026-09-20)
Perguntas do usuário sobre o design do polling expuseram uma corrida de
dados real, mais uma lacuna de segurança nunca endereçada:

**Corrida toque físico × poll (corrigida):** o GET do poll é assíncrono
— se disparado ANTES de um toque físico mas com a resposta chegando
DEPOIS, ele carrega um valor velho e desfazia o toque que acabou de
acontecer (o relé pisca de volta por até ~1 ciclo de poll, até o próximo
poll re-sincronizar). O Firestore em si sempre ficava certo (o toque
escreve via `pushActuatorToFirestore` imediatamente), só o RELÉ FÍSICO
local piscava errado por alguns segundos.

Corrigido com "logical clock" por canal:
- `lastLocalTouchMs[8]` — grava `millis()` no instante de cada toque
  físico (`setRelayAndSync`).
- `fbPollIssuedAtMs` — grava `millis()` no instante em que CADA poll é
  DISPARADO (não quando a resposta chega).
- `applyActuatorsFromFirestore(json, pollIssuedAtMs)` ignora, por canal,
  qualquer valor da nuvem se `lastLocalTouchMs[ch] >= pollIssuedAtMs`
  (ou seja: um toque local aconteceu depois que esta leitura específica
  foi disparada, então ela está desatualizada pra este canal). O poll
  seguinte (disparado depois do toque) aplica o valor certo normalmente.
- Isso resolve o caso concreto (poll velho desfazendo toque novo) sem
  bloquear comandos remotos legítimos que cheguem depois do toque.

**Confirmação obrigatória pra ligar 220V remotamente (PWA):** qualquer
pessoa com a conta logada podia ligar Resistência/LED Ovoscópio (220V AC)
de qualquer lugar do mundo, sem estar fisicamente presente pra notar se
algo desse errado — sem nenhuma fricção a mais que ligar o Umidificador
(5V). Adicionado modal de confirmação (`#modal-220v-backdrop` em
`index.html`, lógica em `app.js`) que intercepta o toggle **só ao LIGAR**
(desligar nunca precisa de confirmação — é sempre a ação segura) um canal
cujo `RELAY_VOLTAGE` contém "220". Cancelar reverte o toggle sem escrever
nada; confirmar chama `alternarAtuador()` normalmente. Reaproveita as
classes CSS do modal de ciclo (`.modal-backdrop`, `.ciclo-actions`,
`.btn-secondary`, `.btn-danger`) — nenhum CSS novo precisou ser criado.
Essa confirmação existe só no PWA (controle remoto); a tela física do
firmware não pede confirmação, porque tocar nela já exige presença
física no local.

**Status:** ambos implementados e testados (o firmware compilou; o
comportamento do modal 220V foi verificado num preview isolado — abrir,
cancelar, confirmar, e canal não-220V ligando direto sem modal). A
correção da corrida de poll ainda não foi observada em uso real
prolongado (precisa gravar na placa).

### Bug reportado: PWA mostra "chocadeira offline" incorretamente (2026-09-20)
Usuário reportou que o PWA às vezes mostra "chocadeira offline" mesmo com
o dispositivo ligado e funcionando normalmente (localmente). Tentei
consultar o documento no Firestore diretamente (via as credenciais do
Service Account, `firebase_credentials.h`, que já autenticam com poder de
admin) pra ver o valor real de `firmwareLastSeen` e confirmar a causa com
dado ao vivo — **bloqueado pelo classificador de segurança do Claude Code
("Credential Leakage")**, por rodar um script com a chave privada
embutida. Não tentei contornar; segue como pendência **se quiser
diagnosticar com dado real de novo**: rodar esse tipo de consulta exige
liberar essa ação (permissão de Bash) ou usar o Console do Firebase
manualmente (não precisa de nenhuma permissão especial, é só a conta
`seu-email@exemplo.com` olhando o próprio documento).

Sem esse dado ao vivo, revisão do código encontrou **duas lacunas reais
de robustez que causam exatamente esse sintoma** (dispositivo fisicamente
ok, mas o campo `firmwareLastSeen` para de atualizar OU nasce errado):

1. **NTP sem retry:** `syncTimeAndStartFirebase()` tenta sincronizar a
   hora **uma única vez no boot** (`if(fbStarted) return;` impede
   qualquer nova tentativa depois). Se o NTP falhar nessa janela (ex: DNS
   lento, internet ainda instável logo após conectar no WiFi), o relógio
   do sistema fica preso no epoch (1970) **pra sempre** — todo heartbeat
   futuro seria gravado com timestamp de 1970, que o PWA sempre vai
   calcular como "extremamente antigo" → "chocadeira offline", mesmo com
   o dispositivo ligado e enviando heartbeats normalmente o tempo todo.
   **Corrigido:** `ntpWatchdog()` (chamado a cada 30s no `loop()`)
   detecta relógio inválido (`< 2024-01-01`) e tenta ressincronizar até
   conseguir; `pushHeartbeatToFirestore()` agora recusa gravar um
   timestamp claramente inválido em vez de mandar lixo pro Firestore.
2. **Sem watchdog de WiFi:** `wifiOK` só era setado no boot ou nas telas
   manuais de WiFi — se o rádio caísse sozinho no meio da operação (ex:
   interferência do relé de 220V chaveando por perto — já suspeito de
   EMI no display, seção 8.4), nada no `loop()` percebia nem tentava
   reconectar. O dispositivo continuava funcionando localmente (touch,
   relés), mas o heartbeat simplesmente parava de ser enviado — de novo,
   "chocadeira offline" no PWA sem a chocadeira estar realmente
   desligada. **Corrigido:** `wifiWatchdog()` (a cada 10s) detecta
   `WiFi.status() != WL_CONNECTED` e chama `WiFi.reconnect()`.
3. **Bônus de diagnóstico:** heartbeat agora tem callback próprio
   (`fbHeartbeatCallback`, não mais o `fbPatchCallback` genérico) que
   loga no Serial `"[Firebase] Heartbeat confirmado pelo servidor"` em
   caso de sucesso — antes só havia log de ERRO, nunca de sucesso, então
   não dava pra confirmar visualmente que os heartbeats estavam saindo.

**Status:** compilado com sucesso. Ainda **não confirmado contra dado
real do Firestore** (diagnóstico bloqueado, ver acima) nem observado em
uso prolongado — as duas causas são plausíveis e bem fundamentadas pelo
código, mas não há certeza de que sejam A causa exata deste caso
específico até gravar na placa e/ou checar o Console do Firebase.

**Importante:** essas correções (seção 8 do reset, WiFi/NTP watchdogs)
foram todas feitas em *rodadas separadas* do que está documentado aqui —
verificar com o usuário se o firmware já foi **gravado na placa** de fato
antes de investigar mais a fundo um "continua offline" (o sintoma pode
simplesmente ser o firmware antigo, sem nenhuma dessas correções, ainda
rodando no hardware).

### Fase 10 — Reorganização de layout + card "Ciclo" na home (2026-09-20)
Dois pedidos combinados: (1) mover Atuadores pra dentro de Configurações
em ambos os sistemas — a visão é o firmware assumir o controle automático
dos atuadores no futuro, então o controle manual deixa de ser o painel
principal do dia a dia; (2) o card "Timer"/"Ciclo" existia na tela inicial
(firmware e PWA) mas nunca mostrava nada de verdade, sempre travado em
"--:--".

**Firmware:**
- Atuadores saiu do card da home (`drawMainScreen`) e agora é um botão
  dentro de Configurações, ao lado de Automação (grid virou 4×2, linhas
  mais baixas — 40px em vez de 54px — pra caber sem estourar a tela).
  Botão "Atuadores" mostra "N de 8 ligados" como subtítulo. Voltar da tela
  de Atuadores agora retorna pra Configurações, não mais pra home.
- Card "Timer" virou "Ciclo": o firmware agora **lê** (nunca escreve —
  isso continua sendo do PWA) os campos `cicloIniciadoEm`/
  `cicloPausadoEm`/`cicloDuracaoDias` via `pollCicloFromFirestore()`, um
  poll separado do de atuadores (15s, não 4s — ciclo muda raro, e isso
  reduz a concorrência no mesmo cliente async `fbClient`). Mostra
  "dia/total" (ex. "3/21") em verde, ou âmbar + "Pausado" se pausado, ou
  "--" sem ciclo ativo. Mesma fórmula de `calcularDiaAtual()` do PWA,
  portada pra C++ em `calcularDiaCicloAtual()`.
- Parsing de timestamp RFC3339 (`cicloIniciadoEm`/`cicloPausadoEm`) novo
  em C: `parseRfc3339()` usa `sscanf`+`mktime()` — funciona como epoch UTC
  porque `configTime()` já roda com offset 0 (mesma convenção do
  heartbeat).

**PWA:**
- Removidas as abas "Sensores/Atuadores" do dashboard (só sobrou
  Sensores, não precisa mais de abas) — `.side-tabs`/`.side-tab`/
  `.side-panel[hidden]` removidos do CSS.
- Atuadores agora mora em uma tela própria `#screen-configuracoes`,
  aberta pelo novo botão "⚙ Configurações" no menu hambúrguer (com botão
  "← Voltar" pro dashboard).
- Card "Timer" da grid de sensores virou "Ciclo": `atualizarDiaCiclo()"
  agora também escreve em `#card-ciclo-value` (formato "3/21", cor
  `--ok` rodando / `--warn` pausado / neutro sem ciclo).
- Diagnóstico extra pro bug da seção 8: `#menu-last-seen` no dropdown do
  menu mostra "Último sinal: há Xs/min/h" a partir de
  `state.firmwareLastSeen` — dá pra ver se o heartbeat está chegando sem
  precisar abrir o Console do Firebase.

**Status:** ambos compilados/testados. Firmware compilou sem erros. PWA
testado num preview isolado com stub do Firebase — dashboard mostra
"CICLO 3/21" corretamente, painel de Atuadores sumiu do dashboard,
"⚙ Configurações" abre a tela nova com os atuadores funcionando (toggle
com borda neon), "← Voltar" retorna ao dashboard, layout mobile
permanece limpo sem as abas antigas.

### Fase 11 — Controle local do ciclo + bug do botão bloqueado offline (2026-09-20)

**Bug corrigido (PWA):** o botão de ciclo (`#btn-iniciar-ciclo`) tinha
sido desabilitado inteiro quando `estadoConexao !== "online"` (Fase 9,
pensado como reforço de segurança geral). Só que iniciar/pausar/retomar/
**parar** o ciclo é 100% um registro de datas no Firestore
(`cicloIniciadoEm`/`cicloPausadoEm`/`cicloDuracaoDias`) — não depende do
ESP32 estar alcançável pra ter efeito nenhum, ao contrário dos atuadores
(que continuam corretamente bloqueados offline, esses sim têm efeito
físico real). Resultado do bug: ficava impossível até **parar** um ciclo
enquanto o status mostrasse offline — removida a checagem de
`atualizarControlesDisponibilidade()` pro botão de ciclo; só os toggles
de atuador continuam gated por conectividade.

**Feature nova — Controle Local do Ciclo (firmware):** até aqui o
firmware só **lia** os campos de ciclo (Fase 10). Agora a tela "Ciclo"
(`S_CICLO`, aberta tocando no card "Ciclo" da home — mesmo card da Fase
10) espelha o modal "Configurações do Ciclo" do PWA por completo:
progresso com barra, duração (stepper 15-35 dias), Pausar/Retomar, Parar
com tela de confirmação própria. Botão "Voltar" reseta
`cicloConfirmandoParar` pra não voltar preso na confirmação da próxima
vez que a tela abrir.

Funções de escrita novas (`pushCicloTimestampField`, `pushCicloDuracao`,
`pushCicloRetomar`, `pushCicloParar`) usam um truque de REST puro pra
"apagar" um campo sem `FieldValue.delete()` (isso é só do SDK JS): um
campo que está no `updateMask` mas **ausente** do corpo do `Document`
enviado é removido pelo Firestore. É assim que "Retomar" seta
`cicloIniciadoEm` E apaga `cicloPausadoEm` num único patch, e "Parar"
apaga os dois de uma vez com um `Document` vazio.

**Bug de compilação encontrado e corrigido nesta feature — "most vexing
parse" do C++:** `DocumentMask(campo)` (ou `DocumentMask(String(campo))`)
direto dentro de outro construtor, com `campo` sendo um identificador
solto, é interpretado pelo compilador como **declaração de função** (não
chamada de construtor) — erro de compilação genuinamente enigmático
("no matching function... note: candidate expects N arguments"). Corrigido
isolando a `String`/`DocumentMask` em variáveis nomeadas antes de passar
pro `PatchDocumentOptions`. Ver "Lições aprendidas".

**Bug corrigido nesta mesma passada:** a zona de toque do antigo card
"Atuadores" na tela principal (`handleTap`, `case S_MAIN`) tinha ficado
viva mesmo depois do card ser removido visualmente na Fase 10 — tocar
naquela área ainda abria a tela de Atuadores "no vazio". Removida; o
card "Ciclo" ganhou sua própria zona de toque no lugar certo.

**PWA:** o card "Ciclo" do dashboard (Fase 10) agora também é clicável
(`#card-ciclo`, `role="button"`, `tabindex="0"`) e abre o mesmo modal que
o botão do cabeçalho — mesmo padrão "toque no card pra gerenciar" que a
tela inicial do firmware.

**Status:** firmware compilado com sucesso (depois de corrigir o vexing
parse). PWA testado num preview isolado simulando `estadoConexao =
"device-offline"`: botão de ciclo permanece clicável, card "Ciclo" abre o
modal, "Parar ciclo" completa o fluxo (confirmação → parada → card volta
pra "--") mesmo com o status offline. Controle local do firmware
(`S_CICLO`) não foi testado em hardware real (sem acesso físico à placa).

### Bug crítico de infraestrutura encontrado logo depois: Service Worker mascarando deploys reais (2026-09-20)

Usuário reportou (com print de tela) que o botão de ciclo continuava
bloqueado mesmo depois do deploy da Fase 11. **O deploy tinha funcionado
de verdade** (confirmado: "Deploy complete!" nos dois últimos deploys) —
o problema era o **service worker** do PWA (`service-worker.js`)
mascarando isso.

**Causa raiz:** a estratégia era *cache-first* pro app shell (`index.html`/
`app.js`/`style.css`), e o service worker só re-executa seu evento
`install` (que repopula o cache) quando o **próprio arquivo
`service-worker.js`** muda de bytes — e `CACHE_VERSION` tinha ficado
travado em `"v2"` durante TODA a sessão de hoje. Resultado: qualquer
usuário que já tinha o PWA aberto/instalado continuava recebendo o
`app.js` de **antes de todos os fixes de hoje** (pairing, bloqueio
offline, ciclo, tudo), não importa quantas vezes o `firebase deploy`
rodasse com sucesso no servidor. Isso pode ter mascarado varios "já
corrigi e já fiz deploy" anteriores nesta sessão — vale reconferir depois
de um hard refresh se algo "não corrigido" ainda aparecer.

**Corrigido:**
1. `service-worker.js`: estratégia trocada de cache-first pra
   **network-first** no `fetch` handler — tenta a rede sempre que
   possível, só cai pro cache quando genuinamente offline. Isso remove a
   dependência de lembrar de subir `CACHE_VERSION` a cada deploy (ainda
   assim, subir a versão quando a LISTA de `CORE_ASSETS` mudar).
   `CACHE_VERSION` também subiu pra `"v3"` nesta rodada.
2. `firebase.json`: adicionado `Cache-Control: no-cache` explícito pro
   `service-worker.js` e pra `**/*.@(html|js|css)` — garante que o
   HTTP cache do navegador sempre revalida com o servidor antes de usar
   uma cópia guardada, em vez de confiar só na lógica do service worker.

**Para o usuário ver a versão nova AGORA** (sem esperar o navegador
decidir sozinho quando checar por um SW novo): um **hard refresh**
(Ctrl+Shift+R / Cmd+Shift+R) na aba do PWA já aberta.

### Fase 12 — Ciclo "reiniciando sozinho" + falso offline (2026-09-20)

**Bug crítico corrigido (firmware): ciclo "ressuscitava" sozinho ~15-30s
depois de Parar localmente.** Causa: a mesma corrida de dados já corrigida
pros atuadores na Fase 9 (poll assíncrono disparado ANTES de uma ação
local, com a resposta chegando DEPOIS, carregando um valor velho) nunca
tinha sido aplicada ao ciclo — `applyCicloFromFirestore()` não tinha
nenhum guard de "esta leitura é mais velha que a última ação local?".
Sequência do bug: usuário toca Parar (zera `fbCicloAtivo` local + manda
`pushCicloParar()`) → um `pollCicloFromFirestore()` disparado ANTES desse
toque responde bem depois, carregando o ciclo ainda ativo → o ciclo
"volta" sozinho.

Corrigido com o mesmo padrão de "logical clock" da Fase 9:
`lastLocalCicloActionMs` (setado em toda ação local — Iniciar, Pausar,
Retomar, Parar, ajustar duração) comparado contra `fbCicloPollIssuedAtMs`
(quando o poll foi disparado, não quando a resposta chega) em
`applyCicloFromFirestore()`. Qualquer leitura mais velha que a última ação
local é descartada; o próximo poll (disparado depois da ação) aplica o
valor certo normalmente.

**Bug de "chocadeira offline" falso — mitigado (não 100% confirmado sem
dado ao vivo):** hipótese de que os *três* pollers periódicos (atuadores
4s, heartbeat 30s, ciclo — agora 30s) competindo pelo mesmo cliente async
`fbClient` atrasam o heartbeat ocasionalmente além do limite de "stale"
do PWA. Mitigações:
- `FB_CICLO_POLL_INTERVAL_MS`: 15s → 30s (não precisa ser mais rápido
  que o próprio heartbeat).
- `loop()`: heartbeat agora é verificado **primeiro**, antes do poll de
  atuadores/ciclo — evita que ele fique sistematicamente em último na
  fila do cliente async.
- PWA: `HEARTBEAT_STALE_MS` (app.js) subiu de 90s → 150s (5x o intervalo
  de heartbeat, era 3x) — absorve atraso ocasional sem deixar de detectar
  uma queda real em tempo razoável.

**Status:** firmware compilado com sucesso. A correção da corrida do
ciclo é a mesma técnica já validada nos atuadores (alta confiança). A
mitigação do falso-offline é uma hipótese bem fundamentada, mas **ainda
não confirmada com dado real do Firestore** (ver bloqueio de diagnóstico
na seção 8) — vale reconferir depois de gravar e observar em uso real.

### Fase 13 — Latência de atuadores + toggles "bloqueados" = sintoma do falso-offline (2026-09-20)

**Decisão consciente: NÃO removido o `disabled` dos toggles de atuador.**
Usuário reportou os toggles bloqueados no PWA junto com o status
"offline", pedindo pra remover o bloqueio. Diferença importante do botão
de ciclo (Fase 11, onde a remoção foi certa): um toggle de atuador manda
um comando pro RELÉ FÍSICO — se o dispositivo não estiver alcançável de
verdade, o comando nunca chega, sem nenhum aviso. Remover o `disabled`
ali removeria uma proteção real, não corrigiria bug nenhum. A causa
raiz genuína é a precisão da detecção de "offline" (heartbeat) — corrigir
essa é o que desbloqueia os toggles de forma correta (só quando o
dispositivo estiver de fato acessível).

**Latência PWA↔firmware reduzida:** `FB_POLL_INTERVAL_MS` (poll de
atuadores) baixado de 4000 → **2500ms** — a latência de um toggle feito
no PWA até o firmware aplicar é literalmente esse intervalo (não há
listener em tempo real do Firestore pra dispositivos embarcados nesta
lib, só polling). Não desceu mais que isso por dois motivos: (1) evitar
aumentar demais a concorrência no mesmo cliente async que o heartbeat
usa (Fase 12), e (2) ficar com margem confortável sob o limite de 50 mil
leituras/dia do plano gratuito do Firestore (2500ms ≈ 34,5 mil/dia).

**Tolerância de heartbeat aumentada de novo:** `HEARTBEAT_STALE_MS`
(app.js) subiu de 150s → **200s** (~6-7x o intervalo de heartbeat).

**Achado colateral:** `knolleary/PubSubClient` (biblioteca MQTT) está em
`platformio.ini` mas **nunca é usada em `main.cpp`** — resquício de uma
arquitetura MQTT/HiveMQ considerada antes de adotar o Firestore (o
`service-worker.js` do PWA também ainda tem um bypass de cache pra
`hivemq.cloud`, mesma origem). Não removido nesta sessão (fora do escopo
do pedido), mas é peso morto — e se um dia a latência de polling deixar
de ser suficiente, MQTT (push de verdade, sem polling) seria o caminho
mais direto pra isso, já que a dependência já está meio pronta.

**Status:** firmware compilado com sucesso. Nenhuma das duas mudanças de
intervalo foi validada em hardware real. Se "chocadeira offline" persistir
por bem mais que ~200s mesmo depois de gravar este firmware, não é mais
jitter de concorrência — provavelmente é firmware antigo ainda rodando
(sem os watchdogs de WiFi/NTP da Fase 8) ou uma queda de conexão real;
vale checar o Console do Firebase pra confirmar o valor real de
`firmwareLastSeen` nesse caso.

**2026-09-20 (confirmado):** usuário confirmou ter gravado o firmware na
placa (log de upload compartilhado). Pra diagnosticar em definitivo se o
"offline" é real, adicionado ao Serial:
- `diagnosticoStatusPeriodico()` — uma linha de status a cada 10s:
  `[DIAG] WiFi=OK/CAIU RSSI=... IP=... | fbApp.ready()=sim/nao | ultimo
  heartbeat OK ha Xs | heap=... | uptime=...`. Uma linha só, tudo que
  importa pra saber se o dispositivo está realmente conectado.
- `pushHeartbeatToFirestore()` agora loga **toda tentativa** (mesmo
  quando pulada por `fbStarted`/`fbApp.ready()` falso), não só
  sucesso/erro — antes não dava pra distinguir "não tentou" de "tentou e
  ainda não respondeu".
- Aguardando o usuário rodar `pio device monitor` e compartilhar o log
  pra confirmar (ou descartar de vez) a causa do falso-offline.

**✅ RESULTADO — causa raiz é 100% do lado do PWA, não do firmware.** Log
real de `pio device monitor` (2026-09-20) mostrou WiFi=OK o tempo todo
(RSSI -55 a -62dBm), `fbApp.ready()=sim` o tempo todo, e heartbeat
enviado + confirmado a cada 30s **sem uma única falha por 4+ minutos
seguidos**. O firmware está funcionando perfeitamente — o problema nunca
esteve aqui.

Causa raiz real: **o listener `onSnapshot` do PWA fica parado/atrasado**
(comportamento conhecido do SDK do Firestore em aba em segundo plano por
muito tempo, notebook suspenso, blips de rede etc.), fazendo
`state.firmwareLastSeen` no navegador ficar velho mesmo com o Firestore
tendo o valor atualizado no servidor. Corrigido no `app.js` com duas
camadas (o `onSnapshot` continua sendo a fonte principal/instantânea,
isso é reforço):
1. `visibilitychange` — ao voltar pra aba, busca o documento direto do
   **servidor** (`get({source: "server"})`, bypassa cache local) pra
   corrigir o status na hora, sem esperar o próximo push do listener.
2. Reforço periódico a cada 45s, independente de foco, caso o listener
   fique parado sem nenhum evento de visibilidade pra disparar a correção.

**Detalhe curioso encontrado no log, não investigado a fundo:** cada
heartbeat gera **duas** linhas de "Heartbeat confirmado pelo servidor" no
Serial, não uma. Não parece causar nenhum problema funcional (nenhum
efeito colateral observado), mas fica registrado — pode ser o
`AsyncResult` do FirebaseClient disparando o callback mais de uma vez
por request (ex.: uma vez ao receber os dados, outra ao finalizar a
tarefa).

### Fase 14 — Modal 220V preso na tela errada + grid 4x2 + robustez extra do status (2026-09-21)

**Bug crítico corrigido: modal de confirmação 220V ficava preso invisível
na Configurações.** Causa raiz estrutural: `#modal-220v-backdrop` (e
`#ciclo-modal-backdrop`) viviam dentro de `<section id="screen-dashboard">`
desde a Fase 9 — mas o gatilho do modal 220V (toggle de atuador) mudou pra
dentro de `#screen-configuracoes` na Fase 10, uma `<section>` DIFERENTE.
Como `.screen` não-ativa tem `display:none`, e isso esconde TODO
descendente independente de `position`/z-index (`position:fixed` só
escapa do fluxo de LAYOUT do ancestral, não do `display:none` dele), o
modal nunca tinha chance de aparecer de verdade quando acionado de
Configurações — só quando (hipoteticamente) o dashboard estivesse ativo
por baixo. **Corrigido:** os dois modais viraram filhos diretos de `#app`,
fora de qualquer `<section class="screen">` — agora são overlays globais
de verdade, funcionam não importa qual tela esteja ativa. Testado num
preview isolado: modal 220V abre corretamente a partir de Configurações.

**Grid de atuadores: 2×4 → 4×2**, conforme pedido. `.atuador-item` mudou
de layout horizontal (nome+chave à esquerda, toggle à direita) pra
vertical (nome+chave em cima, toggle embaixo, tudo centralizado) — layout
horizontal não cabia em colunas 2x mais estreitas. Reserva 1/2 caem
naturalmente na última linha (ordem row-major padrão do grid). Em telas
≤720px volta pra 2 colunas (4 não cabe com texto legível num celular).

**Bug de CSS encontrado e corrigido na hora (no próprio processo desta
correção): a media query de fallback mobile não tinha efeito nenhum.**
Um primeiro fallback pra 2 colunas foi escrito dentro de uma media query
antiga (do layout do dashboard), que fisicamente vem ANTES da regra base
de 4 colunas no arquivo — com a mesma especificidade, a regra que aparece
por ÚLTIMO na cascata vence, mesmo dentro de uma media query satisfeita.
Resultado: a versão inicial do fallback mobile nunca aplicava, em nenhuma
largura de tela. Corrigido movendo a media query pra logo depois da regra
base (`.atuadores-grid`, seção "ATUADORES"). Confirmado com preview em
375px: 2 colunas de verdade depois do fix, 4 colunas antes (bug real,
pego e corrigido na mesma sessão).

**Heartbeat/status — mais uma rodada de robustez** (usuário reportou
persistência do falso-offline mesmo após a Fase 13; pediu explicitamente
5-10min de tolerância):
- `HEARTBEAT_STALE_MS`: 200s → **360s (6min)**.
- `reconferirStatusDoServidor()` agora também dispara em `window.focus`
  (antes só `visibilitychange`) — mais chance de pegar o retorno à aba em
  navegadores/contextos de PWA diferentes.
- **Novo:** a cada 5min, `escutarAtuadores()` é chamado de novo
  inteiro (cancela o listener anterior, abre um novo) — proteção extra
  contra o listener `onSnapshot` ficar "zumbi" (parado sem erro visível)
  por tempo demais, sem depender só de eventos de foco/visibilidade pra
  se recuperar.

**Status:** modal 220V e grid 4x2 testados visualmente em preview isolado
(desktop + mobile 375px), ambos confirmados funcionando. As mudanças de
heartbeat/listener não foram validadas em uso real prolongado — o
`reconferirStatusDoServidor()` já existia desde a Fase 13 e não resolveu
sozinho da vez passada, então vale observar se essa rodada (foco +
resubscribe periódico + limiar maior) resolve de vez, ou se aponta pra
uma causa ainda mais profunda no SDK/rede do navegador do usuário.

### Fase 15 — Deep sleep (Desligar/Ligar), reorganização da tela Sistema, forçar atualização (2026-09-21)

**Deep sleep de verdade** (`esp_deep_sleep_start()`, não o screensaver por
software já existente — CPU/WiFi/RAM tudo desliga, heartbeat para de vez):
- **Dois gatilhos, uma função compartilhada** (`entrarEmDeepSleep()`):
  - **Local** — botão "Desligar" novo na tela Sistema do firmware
    (`desligarLocal()`), sem confirmação extra (presença física já
    confirma a intenção, mesmo padrão direto do botão "Reiniciar").
  - **Remoto** — botão "🔌 Desligar" no menu do PWA, com modal de
    confirmação (é a ação mais séria que existe: deixa o dispositivo
    **totalmente inacessível** até alguém apertar um botão físico).
    Escreve `comandoDesligar: true` no Firestore; o firmware lê isso no
    mesmo poll dos atuadores (2.5s — reage rápido), e ao processar
    (`desligarRemoto()`) precisa **limpar o campo de volta pra `false`
    de forma SÍNCRONA** (espera a confirmação do patch, com timeout de
    5s, chamando `fbApp.loop()` manualmente num laço) **antes** de
    dormir — sem isso, ao acordar o firmware leria o campo ainda `true`
    no primeiro poll e voltaria a dormir na hora, num loop sem fim.
- **Acordar**: `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)` configurado
  antes de dormir — acorda ao pressionar o botão físico **BOOT**
  (GPIO0, nível baixo quando pressionado). O botão **RST** já funciona
  "de graça", sem nenhum código novo — é um reset de hardware completo,
  nem passa pelo mecanismo de wake do deep sleep. `esp_reset_reason()`
  (Fase 8) já reconhece e loga esse caso (`ESP_RST_DEEPSLEEP`), sem
  precisar de nada adicional.
- **Nota sobre GPIO0**: é pino de strapping (ver seção 2.4, pinos
  bloqueados) — mas usá-lo como fonte de wake do deep sleep é o padrão
  consagrado em projetos ESP32 (é literalmente pra isso que o botão BOOT
  costuma servir em runtime, fora do momento exato do boot) e não
  conflita com seu papel na seleção de modo de boot.
- Segurança: os 8 relés são desligados antes de dormir, mesmo padrão já
  usado no botão "Reiniciar".

**Tela Sistema reorganizada** — de lista corrida (10 linhas de texto
`label: valor`) pra grid de "tiles" 2×4 (mesmo estilo visual já usado em
Configurações), mais fácil de escanear. "Último reset" virou um banner
de destaque no topo (cor muda conforme normal/anormal). Botões de ação
lado a lado no rodapé: **Reiniciar | Desligar**.

**PWA — "Forçar atualização"**: botão novo no menu, escape hatch manual
pro cache do service worker (limpa `caches.keys()` + desregistra o SW +
recarrega). Hard refresh por atalho de teclado é trabalhoso no mobile.
Desde a Fase 12 o SW já é network-first, então isso raramente deveria
ser necessário — é mais rede de segurança que correção de um bug ativo.

**Status:** firmware compilado com sucesso. Toda a cadeia
(escrever comando → firmware detectar → confirmar limpeza → dormir →
acordar com BOOT) **não foi testada em hardware real** — é a primeira
vez que este firmware tenta deep sleep, e o botão físico BOOT nunca foi
confirmado fisicamente presente/funcional nesta placa especificamente
(assumido por ser padrão em boards ESP32-S3 devkit-style). Modal de
"Desligar" e botão "Forçar atualização" testados visualmente em preview
isolado do PWA.

### Fase 16 — Bug crítico: loop de deep sleep no boot físico + UI de boot sem tela preta (2026-09-21)

**Bug crítico corrigido: loop de deep sleep ao acordar via botão.** A
limpeza síncrona de `comandoDesligar` em `desligarRemoto()` (Fase 15) tem
um timeout de 5s — se o patch não confirmasse a tempo (rede ruim,
qualquer instabilidade), o firmware dormia **mesmo assim**, deixando o
campo ainda `true` no Firestore. Ao acordar via BOOT, o primeiro poll
normal (2.5s) leria esse `true` de novo e reentraria em deep sleep na
hora — loop sem fim, sem chance nem de ver a tela principal.

Corrigido com uma segunda camada de proteção (`limparComandoDesligarSeAcordouPorBotao()`,
chamada uma única vez no `setup()`, antes do `loop()` começar): usa
`esp_sleep_get_wakeup_cause()` pra checar se ESTE boot foi causado por
`ESP_SLEEP_WAKEUP_EXT0`/`EXT1` (botão físico) — se sim, espera o Firebase
ficar pronto (até 10s) e roda a limpeza síncrona **de novo**
(`limparComandoDesligarSincrono()`, refatorada de `desligarRemoto()` pra
ficar compartilhada entre os dois pontos), antes de qualquer poll normal
ter chance de rodar. RST/POWERON (boot normal) não passam por essa
checagem extra — só o wake por botão físico.

**Investigado e descartado**: não existe nenhum caminho no PWA que
reenviaria `comandoDesligar` automaticamente — o campo só é escrito uma
vez, no clique de "Confirmar" no modal. O loop era 100% do lado do
firmware; nenhuma mudança foi necessária no PWA para este item.

**UI de boot sem tela preta.** A antiga tela preta de "Conectando: SSID"
(bloqueava a UI por até 10s) foi removida — agora a **home é desenhada
primeiro** (`drawMainScreen()`, antes até de tentar `WiFi.begin()`), e o
ícone de WiFi no topo anima (barras acendendo 1→4 em sequência, azul
neutro, nova função `drawWifiConnectingIcon()`) enquanto a conexão está
em andamento, ao invés de travar numa tela vazia. Relógio do cabeçalho
também continua atualizando durante a espera (não fica "congelado").

**Status:** firmware compilado com sucesso. Nenhuma das duas correções
foi validada em hardware real — a próxima vez que o dispositivo dormir e
acordar via BOOT é o teste real do fix do loop; a UI de boot pode ser
validada em qualquer boot normal (bem menos arriscado de testar).

### Fase 17 — Bug crítico: "Desligar" parecia reiniciar em vez de dormir (GPIO0 flutuando) (2026-09-21)

**Sintoma reportado:** comando "Desligar" parecia reiniciar o firmware em
vez de entrar em deep sleep de verdade.

**Causa raiz confirmada:** nenhum `ESP.restart()` está no caminho do
código de desligar (só existe um, no botão "Reiniciar" — conferido).
O sintoma bate exatamente com a hipótese 3 do usuário: **GPIO0
flutuando durante o deep sleep**. O pull-up "normal" do GPIO (que o
framework Arduino/ESP-IDF mantém em operação normal) **não é preservado
durante o deep sleep** — o domínio de baixo consumo (RTC/LP-IO) tem sua
PRÓPRIA configuração de pull-up/pull-down, separada, que precisa ser
habilitada explicitamente pro pino de wake antes de dormir. Sem isso, o
nível do GPIO0 durante o sleep fica indefinido — se ler como LOW (ou
oscilar), o `ext0_wakeup` dispara sozinho, segundos depois de dormir,
que de fora parece um reinício.

**Corrigido:** `rtc_gpio_pullup_en(GPIO_NUM_0)` +
`rtc_gpio_pulldown_dis(GPIO_NUM_0)` chamados explicitamente logo antes de
`esp_sleep_enable_ext0_wakeup()`, em `entrarEmDeepSleep()`. Precisou de
`#include <driver/rtc_io.h>` novo.

**Sobre os outros dois pontos do pedido:**
- *"Garanta que o comando execute deep sleep, não restart"* — confirmado
  por inspeção: só existe um `ESP.restart()` no arquivo inteiro, e é do
  botão "Reiniciar" (zona de toque própria, sem relação com "Desligar").
  Não havia bug de código aqui — o comando sempre chamou
  `esp_deep_sleep_start()` de verdade; o sintoma observado era o efeito
  colateral do GPIO0 flutuando (acima).
- *"Evitar resets forçados pelo sistema"* — revisado o caminho de código
  (incluindo o laço bloqueante de `limparComandoDesligarSincrono()`, que
  usa `delay(10)` — cede a vez pro RTOS a cada iteração, não deveria
  disparar o Task Watchdog). Não identificado nenhum ponto que force um
  reset por watchdog/brownout nesse caminho. Se isso ainda acontecer na
  prática, `logResetReason()` (seção 8) já vai revelar a causa exata
  (`ESP_RST_TASK_WDT`, `ESP_RST_BROWNOUT`, etc.) no próximo boot — é a
  ferramenta certa pra confirmar ou descartar isso com dado real, em vez
  de adicionar código especulativo agora.

**Status:** firmware compilado com sucesso. Fix do GPIO0 flutuante é a
correção mais fundamentada tecnicamente desta sessão pro "Desligar", mas
segue **não validada em hardware real** — só um teste físico completo
(desligar → confirmar que a tela realmente apaga e fica apagada → acordar
com BOOT) confirma de vez.

### Fase 18 — Loop de deep sleep: garantia local, não depende mais de rede (2026-09-21)

Usuário reportou o mesmo sintoma da Fase 16 de novo (loop ao acordar via
botão) — com razão de desconfiar: o fix da Fase 16 (limpar
`comandoDesligar` no Firestore, com timeout de 10s pro Firebase +5s pro
patch) ainda era **uma corrida contra o tempo**. Se a rede estivesse
lenta logo após acordar (bem provável — TLS handshake, auth, tudo do
zero), a limpeza podia não confirmar a tempo, e o firmware dormia nesse
boot de novo, com o campo ainda `true` — loop.

**Corrigido com garantia de verdade, sem depender de rede nenhuma:**
- `ativarSupressaoDesligarSeAcordouPorBotao()` — chamada **bem no início
  do `setup()`**, antes de qualquer WiFi/Firebase, 100% local: se
  `esp_sleep_get_wakeup_cause()` for EXT0/EXT1, ativa
  `desligarSuprimidoNesteBoot = true` por `DESLIGAR_SUPRESSAO_MS` (30s).
- Em `applyActuatorsFromFirestore()`, o comando `comandoDesligar=true` só
  é processado (`desligarRemoto()`) se essa janela já tiver expirado —
  **mesmo que a limpeza no Firestore nunca confirme**, o loop não
  acontece, porque a decisão de ignorar o comando é inteiramente local.
- A limpeza no Firestore (`limparComandoDesligarSeAcordouPorBotao()`)
  continua existindo, mas agora é só "boa vontade"/higiene do dado na
  nuvem — reforçada com até 3 tentativas (era 1) — não é mais a defesa
  contra o loop.

**Trade-off consciente:** durante os 30s após acordar via botão, um
comando de desligar genuíno (ex.: usuário mandou de novo pelo PWA, sem
saber que o dispositivo tinha acabado de acordar) também seria ignorado
até a janela expirar. Aceitável — é uma janela curta, e evitar o loop é
mais importante que reagir instantaneamente a um desligar logo após ligar.

**Status:** firmware compilado com sucesso. Ainda não validado em
hardware real — mas agora a garantia contra o loop não depende de
nenhuma condição de rede, só de lógica local determinística, o que é
bem mais fácil de confiar sem testar do que os fixes anteriores.

---

### Fase 19 — Loop de boot físico (desligou/reiniciou sozinho 3x): diagnóstico + backoff (2026-09-21)

Usuário reportou um sintoma **diferente** do loop da Fase 16/18 (aquele
era sobre reentrar em deep sleep por causa do campo `comandoDesligar`
velho): desta vez, ao ligar fisicamente, a placa "desligou" (resetou)
sozinha 3 vezes seguidas antes de estabilizar. Hipóteses levantadas pelo
próprio usuário: Brownout Detector (BoD), Watchdog (WDT), ou corrupção de
estado em memória RTC.

**Investigação no código (sem acesso a hardware físico):**
- **Watchdog:** não há `esp_task_wdt` configurado em lugar nenhum do
  firmware, e não existe loop bloqueante longo o bastante no boot pra
  starvar a task idle. Pouco provável ser a causa.
- **"Memória RTC corrompida":** o projeto não usa `RTC_DATA_ATTR` em
  lugar nenhum (confirmado via busca no arquivo inteiro) até esta fase.
  O estado de "acordou via botão" não vem de nenhuma variável nossa — vem
  direto do registrador de hardware via `esp_sleep_get_wakeup_cause()`,
  que sobrevive a resets normais mas não é algo que nosso código possa
  corromper. Hipótese improvável de causa via software.
- **Brownout:** o candidato mais forte, e já havia um comentário próprio
  no código (`logResetReason()`, linha do case `ESP_RST_BROWNOUT`)
  apontando exatamente essa suspeita desde antes deste bug ser
  reportado: pico de corrente do rádio WiFi/TFT logo após acordar, num
  trilho de energia que pode não ter capacitância suficiente pra
  segurar a tensão durante o pico — clássico em placas ESP32 com fonte
  ou regulador no limite. Isso bateria exatamente com o padrão relatado
  (reseta, tenta nascer de novo, reseta de novo, até a Nª tentativa
  finalmente ficar de pé).

**O que foi implementado (mitigação em software — não substitui uma
correção de hardware se a causa for confirmada como Brownout):**
- `RTC_DATA_ATTR uint32_t bootLoopCounter` — sobrevive a resets (mas não
  a perda total de energia), incrementado em `logResetReason()` sempre
  que o motivo do reset for anormal (Brownout/Watchdog/Panic/Task WDT) e
  zerado assim que um boot normal acontecer. Serve como **diagnóstico
  objetivo**: da próxima vez que isso acontecer, dá pra ver no Serial
  (`pio device monitor`) ou na própria tela Sistema (banner "Último
  reset" mostra "(N seguidos)" quando N>1) exatamente quantos resets
  anormais consecutivos ocorreram e qual foi o motivo de cada um —
  incluindo se realmente foi Brownout, o que confirmaria/refutaria a
  hipótese sem depender de eu adivinhar.
- **Backoff de energia:** se `bootLoopCounter>0` (ou seja, o boot atual
  já é uma retentativa depois de reset(s) anormal(is)), a pausa de
  estabilização antes de inicializar TFT/WiFi (que já existia, 300ms)
  cresce +300ms por tentativa consecutiva, até +1500ms no máximo. Dá
  mais tempo pro trilho de energia se recuperar antes do próximo pico de
  corrente, reduzindo a chance da mesma sessão de energia reentrar no
  loop — sem precisar desabilitar o Brownout Detector (que é uma
  proteção real contra corrupção de flash, não algo pra simplesmente
  desligar por software).

**O que NÃO foi feito, e por quê:** desabilitar o BoD via registrador
(`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`) é uma "solução" comum em
projetos ESP32 com fonte fraca, mas eu evitei de propósito — ela remove
uma proteção real (o chip resetar antes da tensão cair a ponto de
corromper uma escrita em flash em andamento), e eu não tenho como testar
em hardware real se isso troca "reseta 3x" por "ocasionalmente corrompe
o LittleFS". Se o log confirmar Brownout e o backoff acima não for
suficiente, o próximo passo correto é hardware (capacitor maior perto do
VCC do display/relés, ou fonte mais robusta), não desligar a proteção.

**Status:** firmware compilado com sucesso. **Não validado em hardware
real.** Diferente da Fase 18, esta fase é parcialmente diagnóstica — o
próximo boot problemático vai finalmente dizer, via Serial ou tela
Sistema, qual é o motivo real (Brownout/Watchdog/Panic/outro), o que
falta pra confirmar a hipótese com certeza.

---

### Fase 20 — SHT31 inicializado no firmware, temperatura/umidade de verdade (2026-09-21)

A sonda SHT31 (IP68, comprada há algumas semanas — ver 2.3) chegou.
Junto chegou também a câmera OV5640 substituta (ver seção 4), mas por
pedido do usuário ela fica pra depois — esta fase é só o sensor.

**Fiação confirmada pelo usuário (print do anúncio AliExpress, modelo
"SHT30/PVC68"):** vermelho=VCC, preto=GND, amarelo e verde presentes mas
sem confirmação de qual é SCL/qual é SDA. Usei a convenção mais comum
pra esse tipo de sonda (amarelo=SCL, verde=SDA) — **isso é a única parte
não 100% confirmada**; errar SCL/SDA não tem risco nenhum (o barramento
I2C simplesmente não responde, dá pra inverter os fios com o
equipamento desligado e tentar de novo). VCC/GND (vermelho/preto) segue
convenção universal, risco baixo.

**O que foi implementado em `main.cpp`:**
- `Adafruit_SHT31 sht31` no mesmo barramento I2C já usado pela RTC
  (SDA=8, SCL=9 — nenhum pino novo, nenhum conflito).
- `sht31.begin(0x44)` no `setup()`, com log claro de sucesso/falha
  (mesmo padrão de honestidade do `rtcOK`/`wifiOK`) — se o endereço
  0x44 não responder, o log já sugere inverter amarelo/verde.
- `lerSht31()`, chamada a cada 5s no `loop()` (100% local/I2C, não
  depende de WiFi): atualiza `temperaturaAtual`/`umidadeAtual`. Só loga
  no Serial em transições de estado (ok→falha, falha→ok), não a cada
  leitura — em 5s isso viraria ruído (diferente do heartbeat, que é a
  30s e loga sempre).
- `drawTempUmidCards()`: os cards "Temperatura"/"Umidade" da tela
  principal, que antes eram texto estático `--.-C`/`--.-%`, agora
  mostram o valor real (ou `--.-` honesto enquanto não há leitura
  válida) — redesenho parcial (só as 2 células), mesmo padrão já usado
  em `drawCicloCard()`.
- `pushLeituraToFirestore()`: a cada 60s (bem mais espaçado que
  heartbeat/poll — temperatura de incubadora não muda rápido, e cada
  chamada cria um documento NOVO, diferente do padrão PATCH usado em
  atuadores/heartbeat/ciclo), cria um documento em
  `chocadeiras/{id}/leituras` com `temperatura`, `umidade`, `timestamp`.
  Só envia se já houver uma leitura válida do sensor E o NTP já tiver
  sincronizado (mesmo guard do heartbeat contra timestamp de 1970).

**Zero mudança necessária no PWA** — `app.js` já tinha
`escutarUltimaLeitura()`/`atualizarCardsComLeitura()` prontos, escutando
em tempo real o último documento de `leituras` (função já existia,
nunca tinha dado com o que popular porque o firmware nunca escrevia lá).
Card de CO2 continua `---` (sensor MH-Z19B ainda nem comprado).

**Status (atualizado 2026-09-22):** ✅ **validado em hardware real.**
Usuário conectou os 4 fios (fiação confirmada certa de primeira —
vermelho/preto/amarelo/verde = VCC/GND/SCL/SDA, sem precisar inverter
nada) e o Serial confirmou `[SHT31] Sensor de temperatura/umidade OK
(I2C 0x44)`.

**Efeito colateral descoberto: os brownouts pioraram muito depois do
SHT31 conectado.** Antes (Fase 19), o intervalo entre resets era de
~60-100s; com o sensor conectado, caiu pra ~10s (quase loop contínuo).
Usuário mediu a tensão do trilho de 3,3V com multímetro: **oscila entre
2,92V e 3,02V**. Isso confirma queda real de tensão sob carga — mas o
multímetro (resposta lenta, faz média) não pega os transitórios rápidos
(picos de corrente do TX do WiFi, chaveamento de relé) que
provavelmente derrubam a tensão bem mais fundo por microssegundos —
tempo suficiente pro comparador do BOD (que reage rápido) disparar, mas
rápido demais pro multímetro registrar. Ou seja: **2,92V medido é só o
"chão" visível; a queda real que causa o reset é mais profunda e mais
curta que isso.** Isso aponta pra capacitância de bulk insuficiente
(resposta a transitório), não só "tensão média baixa" — reforça que
capacitor é a correção certa, não mais delay de software (o backoff da
Fase 19 já demonstrou ser insuficiente sozinho).

**✅ RESOLVIDO (2026-09-22):** usuário parou de alimentar o ESP32 via USB
e passou a usar o conversor DC-DC 12/24V→5V 3A (mesmo que já alimentava
display/relés) pro pino 5V do ESP32. Tensão no trilho de 3,3V subiu de
2,92-3,02V para **3,15-3,18V**, e 30 minutos rodando sem nenhum reset
(era pra estar resetando a cada ~10s no estado anterior). Confirma a
causa raiz: USB (PC/carregador) como fonte do ESP32 não aguentava os
picos de corrente de WiFi TX + SHT31 + display juntos. Capacitor de
bulk (item 2 abaixo) e módulo regulador dedicado (item 3) ficam como
margem de segurança extra, não mais essenciais.

**Recomendação de hardware dada ao usuário** (ordem de custo):
1. Parar de alimentar o ESP32 via USB (PC/carregador) durante uso normal
   — rotear o pino 5V do ESP32 pro mesmo conversor DC-DC 12/24V→5V 3A
   que já alimenta display/relés (GND comum). Grátis, só fiação.
2. Capacitor de bulk (eletrolítico baixo ESR, 220-470µF/16V + cerâmico
   100nF em paralelo) direto nos pinos 3V3/GND do ESP32, o mais perto
   fisicamente possível do chip.
3. Se não bastar: módulo regulador step-down dedicado (tipo LM2596 ou
   MP1584EN, 2-3A, ajustável pra 3,3V ou 5V) alimentando o pino
   3V3/5V diretamente, no lugar do regulador linear pequeno que já vem
   na própria placa DevKit (baixa corrente, resposta ruim a
   transitório).

**⚠️ Regressão (2026-09-22): tela branca ao tentar compartilhar o SPI do SD com o do display — revertido.**
Descoberta física: o módulo TFT tem leitor de microSD embutido, mas os
4 pinos dele (`SD_CS`/`SD_MOSI`/`SD_MISO`/`SD_SCK`, no verso da placa)
**não têm trilha ligando ao conector principal** — precisam de jumper
manual até `SDI(MOSI)`/`SDO(MISO)`/`SCK` do conector principal (mesmo
barramento físico) + um fio dedicado de `SD_CS` até um GPIO livre
(`SD_CS_PIN=18` no código, **não confirmado como realmente exposto/livre
nesta placa física específica**).

Pra garantir que a lib `SD` usasse os mesmos pinos do display, foi
adicionado `SPI.begin(TFT_SCLK,TFT_MISO,TFT_MOSI,SD_CS_PIN)` explícito
antes do `SD.begin()`. No teste seguinte em hardware real (mesmo sem o
SD fisicamente ligado ainda) a tela ficou **branca e travada**.
Hipótese (não confirmada, sem forma de validar sem hardware): o
`TFT_eSPI` usa seu próprio driver SPI de baixo nível (via `esp-idf`,
não via `SPIClass` do Arduino), já configurado nos mesmos pinos físicos
pelo `tft.init()` no início do `setup()`; chamar `SPI.begin()` de novo
nesses pinos mais tarde pode "roubar" de volta o roteamento pro objeto
`SPI` global do Arduino, quebrando o handle que o TFT_eSPI já tinha.

**Revertido** — `SD.begin(SD_CS_PIN)` sozinho, sem `SPI.begin()`
próprio (volta a simplesmente falhar graciosamente com `sdOK=false` se
o barramento não bater, sem arriscar o display). Prioridade é nunca
quebrar o que já funciona. Compartilhar SPI entre TFT_eSPI e outra lib
é uma armadilha conhecida no ecossistema Arduino/ESP32 — a próxima
tentativa precisa ser incremental e testada com cuidado em hardware
real (ou considerar SD em SPI por software, em pinos totalmente
separados, se compartilhar o barramento continuar frágil).

**Variável de confusão:** no mesmo período, o usuário desconectou o
conversor DC-DC (que resolveu os brownouts na Fase 19/atualização
acima) e voltou pra alimentação só via USB (inclusive trocando de porta
do PC) enquanto mexia na fiação do SD — o que sozinho já seria
suficiente pra reproduzir o loop de brownout, independente do bug do
SPI. As duas mudanças aconteceram próximas uma da outra, então **a
causa exata da tela branca não está isolada ainda** entre "conflito de
SPI" vs. "brownout de energia" vs. os dois juntos. Próximo passo:
reconectar o conversor DC-DC (USB pode continuar plugado também, só
pra programação/serial — as duas fontes coexistem, era a combinação
original que tinha dado certo) e testar de novo com o firmware já
revertido, isolando as variáveis.

**✅ Causa raiz isolada (2026-09-22):** com o DC-DC reconectado e o
`SPI.begin()` extra já revertido, o `pio device monitor` mostrou o
firmware **100% saudável** (WiFi, Firebase, heartbeats, SHT31, boot
"Ligou normalmente", zero resets) — e a tela **continuava branca**. Isso
descartou energia E o `SPI.begin()` revertido como causa, sobrando só
uma possibilidade: o **`SD.begin(SD_CS_PIN)` sozinho** (sem nenhum
`SPI.begin()` explícito) já inicializa um barramento SPI por baixo dos
panos, e caiu no mesmo periférico que o `TFT_eSPI` (`USE_HSPI_PORT`)
já tinha reservado. Confirmado desativando `SD.begin()` por completo
(`sdOK=false` hardcoded) — display voltou ao normal imediatamente.

**Correção definitiva (Fase 22):** SD reprojetado pra rodar num
periférico SPI **completamente separado** (`SPIClass sdSPI(FSPI)` —
SPI2, diferente do SPI3/HSPI que o TFT_eSPI usa) **e** pinos
**totalmente dedicados**, sem nenhuma sobreposição com display/touch:
```
SD_CS_PIN=18, SD_SCK_PIN=22, SD_MISO_PIN=23, SD_MOSI_PIN=24
```
Isso elimina os dois jeitos possíveis do conflito acontecer: periférico
diferente (sem disputa de host) e pinos diferentes (sem disputa de
roteamento na matriz de GPIO). ⚠️ A recomendação anterior de emendar o
leitor SD do display (`SD_CS`/`SD_MOSI`/`SD_MISO`/`SD_SCK`, ver seção
2.4) nos pinos `SDI(MOSI)`/`SDO(MISO)`/`SCK` do conector principal
**está errada e foi abandonada** — não dá pra compartilhar esse
barramento com segurança. O leitor embutido no display fica sem uso;
se for usar SD, precisa de um módulo/leitor separado nos 4 pinos
dedicados acima (ainda não confirmado como fisicamente livres nesta
placa de 44 pinos — validar antes de soldar).

**Status:** firmware compilado e testado em hardware real com essa
versão (`sdOK` reportando corretamente "não encontrado" sem SD
conectado, sem quebrar o display). Ainda sem cartão físico plugado
nesta sessão — usuário aguardando essa confirmação antes de seguir
soldando.

---

### Fase 22 — Gráficos redesenhados (ciclo diário) + estrutura de Áudio/Alertas/Notificações (2026-09-22)

Dois pedidos do usuário nesta rodada, além da correção do SD acima.

**1. Gráficos (Temp/Umidade) — PWA e Firmware:**
- **Janela mudou de "últimas 24h rolantes" pra "hoje, desde 00:00 local"**
  — acumula o dia inteiro e reseta sozinho à meia-noite (reabrir depois
  da virada já busca a partir do novo 00:00). No firmware, isso é
  feito comparando o dia do mês via RTC (`resetarEstatisticasSensorSeVirouODia()`,
  chamada a cada leitura); sem RTC (`rtcOK=false`) cai pra "desde o
  boot" mesmo, com aviso honesto na tela.
- **PWA agora é ao vivo de verdade**: trocado `.get()` (uma busca) por
  `.onSnapshot()` (listener contínuo) — o gráfico atualiza sozinho
  conforme o firmware manda novas leituras, sem precisar reabrir o
  modal. `fecharModalSensor()` cancela o listener.
- **Layout redesenhado** (PWA): grade horizontal com valores no eixo Y,
  marcas de hora no eixo X, área sob a linha preenchida com gradiente,
  ponto destacado (com halo) no valor mais recente. Testado no browser
  pane com dados simulados — funcionando (ver print no histórico da
  sessão).
- **Firmware também ganhou gráfico** (não tinha nenhum antes, só
  texto): `drawSensorHistChart()`, versão enxuta na tela de detalhe do
  sensor — buffer de 80 amostras (`tempHistorico`/`umidHistorico`), 1
  amostra a cada 20min (~72/dia, cabe folgado), reseta junto com as
  estatísticas à meia-noite. RAM insignificante (640 bytes).

**2. Áudio, Alertas e Notificações — estrutura de UI/lógica, tudo "Em desenvolvimento":**
- **PWA**: 3 novas seções em Configurações (`Áudio`, `Alertas`,
  `Notificações`), cada uma com badge "EM DESENVOLVIMENTO" e controles
  reais porém desabilitados (`disabled`) — toggle "Habilitar áudio",
  slider de volume, botão "Testar som", toggles de notificação push/
  e-mail. "Alertas" mostra as faixas fixas atuais (mesmas do firmware,
  37,2–38,9°C / 45–65%) como referência.
- **Firmware**: grid de Configurações reapertado de 4 pra 5 linhas
  (altura de botão 40→36px) pra caber "Audio" e "Alertas" sem esconder
  nada que já existia. Duas telas novas (`drawAudioScreen()`,
  `drawAlertasScreen()`) com os mesmos controles "desenhados como
  desativados" (toggle sempre off, slider de volume estático, botão
  "Testar som" sem ação) — só o botão Voltar funciona nessas telas, de
  propósito.
- Preparação explícita pro hardware de áudio futuro: nota na tela
  lembrando que o MAX98357 já tem o CH6 do relé reservado (ver 2.4).

**Status:** firmware compilado (RAM 15,2%, Flash 21,2%) e PWA em
produção (deploy feito). Gráfico da PWA testado no browser; gráfico do
firmware e telas de Áudio/Alertas **ainda não testados em hardware
real** nesta sessão.

---

### Fase 23 — Bug real de escala descoberto por foto + gráficos otimizados (2026-09-22)

Usuário mandou foto real da tela de Umidade em hardware: gráfico quase
todo verde, linha real invisível (achatada no fundo). Causa: tanto o
gráfico do PWA quanto o do firmware **forçavam a escala visível a
sempre incluir a faixa ideal inteira** — com umidade em ~42% (ideal
45-65%), a escala esticava pra caber os dois, e a variação real (só
0,8 pontos percentuais) virava uma linha reta imperceptível no fundo de
uma escala de 24 pontos. Bug real, não só preferência estética.

**Corrigido nos dois lados:** a escala do eixo Y agora usa só a
variação real dos dados (± uma folga de 15%), nunca mais forçada a
cobrir a faixa ideal. A banda verde da faixa ideal é desenhada apenas
na porção que cai dentro da janela visível — pode até desaparecer da
tela se os dados estiverem bem longe do ideal, e isso é o comportamento
certo (o gráfico prioriza mostrar a variação real, não a referência).

**PWA — modal de sensor redesenhado:**
- Valor gigante do topo **removido** (redundante — o card do dashboard
  atrás do modal já mostra isso). Espaço virou gráfico maior (160px →
  260px de altura, modal mais largo: 380px → 560px).
- Rodapé "Hoje — N leituras, ao vivo" virou "N leituras" + indicador
  **LIVE** (ponto verde pulsante via CSS) — mais rápido de reconhecer
  que ler uma frase.
- Mais detalhe: 5 linhas de grade (era 4), 5 marcas de hora no eixo X
  (era só início/fim), valor mais recente anotado ao lado do ponto
  destacado (substitui a função do valor gigante removido).
- Testado no browser pane — funcionando.

**Firmware — gráfico otimizado pra 320×240:**
- Mesmo fix de escala (acima).
- Rótulos de min/max do eixo Y adicionados direto no gráfico (não tinha
  nenhum antes — só o texto grande acima, sem contexto de escala).
- Gráfico cresceu de 88px pra 110px de altura, layout comprimido
  (fontes compactas, textos combinados numa linha só) pra abrir espaço
  sem esconder nada.
- Avaliei trocar por barras/sparkline simplificada (sugestão do
  usuário) mas mantive linha+área — já é o formato mais compacto que
  ainda mostra tendência com um único desenho, `drawLine`/`fillRect` no
  TFT_eSPI são baratos (não há gargalo de SPI redesenhando só ao entrar
  na tela ou a cada amostra de 20min, não em loop). Mudar de formato só
  faria sentido se a legibilidade real em hardware mostrar problema —
  ainda não testado fisicamente nesta sessão.

**Status:** firmware compilado e gravado na placa (COM8). PWA em
produção (deploy feito). Firmware ainda sem confirmação visual do
usuário para esta versão específica.

---

### Fase 24 — 3 "bugs" reportados: 1 real (amostragem lenta), 2 explicados, 1 adiado por decisão do usuário (2026-09-22)

Usuário mandou foto do firmware mostrando "90 leituras hoje" nas
estatísticas mas o gráfico travado em **"Aguardando leituras..."**, e
listou 3 supostos bugs + um pedido de redesign de UI (PWA e firmware).

**Bug 1 (real) — amostragem 20min era lenta demais.** As estatísticas
(min/max/média) atualizam a cada leitura do SHT31 (5s), mas o buffer do
GRÁFICO só ganhava um ponto novo a cada 20min — e como o buffer zera a
cada boot (não é RTC_DATA_ATTR nem SD), qualquer reinício recente
(comum durante desenvolvimento) deixava o gráfico vazio por até 20min
mesmo com dezenas de leituras já registradas nas estatísticas.
**Corrigido:** intervalo caiu pra 5min, buffer subiu de 80 pra 300
amostras (cobre exatamente 24h com folga; RAM ainda insignificante,
+1,7KB). Chart também ganhou atualização "ao vivo" de verdade
(`refreshSensorDetailLive()`, chamada do `loop()` só quando
`appState==S_SENSOR_DETAIL`) — redesenha o gráfico só quando uma
amostra NOVA realmente entra no buffer (não a cada leitura de 5s, nem
em loop apertado), e o dot piscando é redesenhado sozinho (600ms) sem
tocar no resto da tela. Atende literalmente o pedido de "reduzir pontos
plotados, usar buffer, evitar redesenho completo a cada leitura".

**Bug 2 (não é bug) — valores do firmware "menores" que o PWA.** Não é
erro de escala/conversão: o firmware tinha acabado de reiniciar (só 90
leituras ≈ 7,5min de dados, todas próximas de 40.5°C), enquanto o PWA
mostrava o dia inteiro via Firestore (559 leituras, 40,1–42,3°C de
variação). Comparando o mesmo instante os dois batiam (40,5 vs
40,6°C — diferença de tempo entre leitura local e último push pro
Firestore, não erro). Resolve sozinho com o fix do Bug 1 + o
dispositivo rodando mais tempo sem reiniciar.

**Bug 3 (perda de dados no reboot) — adiado, decisão do usuário.**
Persistir o histórico (RTC_DATA_ATTR ou SD) fica pra depois: usuário
pediu explicitamente pra tratar isso só depois do padrão visual dos
gráficos estar fechado nos dois lados. Não implementado nesta fase.

**Redesign de UI — PWA:**
- "Faixa ideal" desceu do topo pro rodapé, cor azul clara (`--accent`
  `#38bdf8`, mesma da linha do gráfico).
- Linha/área do gráfico agora mudam de cor conforme o estado do valor
  mais recente: verde (ideal) / laranja (alerta) / vermelho (crítico) —
  mesmas 3 cores já usadas nos cards e no resto da UI (`corPorFaixa()`),
  reforçando visualmente o mesmo estado em vez de introduzir uma 4ª cor.
- Umidade usa o mesmo modal/função que Temperatura — mudança automática
  pros dois, sem código duplicado.

**Redesign de UI — Firmware (320×240):**
- Cabeçalho "< Voltar / Temperatura", valor gigante e instrução "Toque
  p/ voltar" **removidos** — o toque em qualquer lugar ainda volta (
  lógica inalterada), só não tem mais o texto dizendo isso.
- "Faixa ideal" desceu pro rodapé, junto do nome do sensor.
- Dot verde piscando no canto superior direito (`drawSensorLiveDot()`),
  equivalente ao indicador LIVE do PWA.
- Gráfico cresceu de 110px pra **186px** de altura (quase a tela
  inteira) — maior ganho de espaço de todas as fases.
- Avaliado trocar por barras/sparkline simplificada (sugestão do
  pedido anterior) — mantido linha+área: com o fix de amostragem, já
  é rápido e mostra tendência bem; mudar de formato só faria sentido
  se a legibilidade real em hardware mostrar problema.

**Status:** PWA testado no browser pane (cores dinâmicas + faixa no
rodapé confirmadas). Firmware compilado e gravado na placa (COM8) —
ainda sem confirmação visual do usuário desta versão específica.

---

### Fase 25 — mais um ajuste fino do gráfico + configurações vira 2 páginas + thresholds editáveis (2026-09-22)

Usuário mandou foto real confirmando a Fase 24 funcionando (gráfico de
Umidade populado, 481 leituras, cor verde correta) e pediu mais 3
ajustes, só no firmware desta vez.

**1. Gráfico — refinamentos:**
- Dot "ao vivo" foi pra DENTRO do gráfico (canto superior direito) —
  antes ficava numa tirinha acima. Parou de "apagar" pra piscar (viraria
  fantasma sobre a linha/banda) — agora pulsa entre 2 tons de verde,
  sempre visível.
- "Faixa ideal" e "N leituras" viraram uma linha só no rodapé.
- Min/Max/Méd saíram do rodapé e viraram um overlay no TOPO do próprio
  gráfico (fundo sólido pra legibilidade sobre a banda/linha).
- Eixo de horário adicionado (3 marcas: início/meio/agora) — calculado
  via RTC + o intervalo fixo de amostragem (o buffer só guarda valor,
  não timestamp por ponto, então cada índice representa um múltiplo
  fixo de `SENSOR_HIST_INTERVAL_MS` atrás de "agora").
- "Reduzir delay de refresh": o gráfico inteiro (linha/overlay/eixo)
  agora redesenha a cada leitura do SHT31 (5s) — antes só redesenhava
  quando uma amostra nova entrava no buffer (5min). Continua sem
  `fillScreen()` a cada redraw (só a área do gráfico).
- Resultado: gráfico cresceu de 186px pra **218px** de altura.

**2. Configurações virou 2 páginas.** A Fase 22 tinha apertado o grid
pra 5 linhas (40px→36px) pra caber Áudio/Alertas; não sobrava espaço
pra mais nada sem apertar de novo. Em vez disso, a página 0 voltou pro
layout original testado (4 linhas, botões de 40px — mais confortável de
tocar), e os itens mais recentes (Áudio, Alertas, e agora Ajustes
Finos) foram pra página 1. Setinhas nas bordas (pulsando, `settingsArrowOn`
alternando a cada 500ms) + bolinhas indicadoras no rodapé — atende o
pedido de "transição de páginas com ícone/efeito visual". Optei por
**não** fazer uma animação de slide de verdade (redesenhar frames
intermediários sem framebuffer, via SPI não-bufferizado, arrisca
flicker/lentidão) — a troca de página é instantânea, só o indicador que
anima.

**3. Nova tela "Ajustes Finos" — thresholds de Temp/Umid/CO2 editáveis.**
`TEMP_IDEAL_MIN/MAX` e `UMID_IDEAL_MIN/MAX`, que eram `#define` fixas
desde a Fase 21, viraram **variáveis** (nomes mantidos em maiúsculo de
propósito, pra minimizar risco de esquecer algum uso ao converter).
`CO2_IDEAL_MIN/MAX` entraram junto (sem sensor ainda — só ficam
guardados, prontos pro MH-Z19B). Nova tela com 6 steppers -/+ (mesmo
padrão já usado em "Tela"), passo 0,1 pra temperatura e 1/10 pra
umidade/CO2, com clamps simples pra nunca deixar a faixa invertida.
Botão "Salvar" persiste em `/thresholds.cfg` (LittleFS, mesmo padrão de
`loadDisplayCfg()`) — sem salvar, as mudanças ficam só na sessão atual
(revertem pro último valor salvo no próximo boot), mesmo comportamento
já estabelecido na tela "Tela". Acessível em Configurações → página 2 →
Ajustes.

**Status:** firmware compilado e gravado na placa (COM8). Ainda sem
confirmação visual do usuário desta versão específica.

---

### Fase 26 — flicker real encontrado e corrigido + mais 5 ajustes de UI do gráfico (2026-09-22)

Usuário mandou 3 fotos reais confirmando a Fase 25 funcionando (gráfico
crescendo ao longo do tempo, overlay Min/Max/Med, eixo de hora) e
reportou 6 problemas.

**Bug 1 (real, encontrado) — flicker do dot.** Causa raiz: em
`refreshSensorDetailLive()`, o pulso do dot (a cada 600ms) e o redraw
completo (a cada 5s) usavam a MESMA condição de saída
(`if(!precisaBlink && !precisaRedrawCompleto) return;`) — ou seja,
**qualquer um dos dois motivos disparava o redraw INTEIRO**
(`drawSensorHistChart()` completo, com `fillRect` limpando o gráfico e
redesenhando linha/grade/overlay/eixo). Na prática, a tela inteira
piscava a cada 600ms, não só o dot. Corrigido: extraído
`drawSensorLiveDotOnly()` (só um `fillCircle`, bem barato) pro caso do
pulso sozinho; o redraw pesado só roda mesmo a cada 5s, quando os dados
de verdade mudam.

**Bug 2 (real) — "Aguardando leituras" por minutos.** Duas causas
combinadas: (a) o código só desenhava algo com `n>=2` amostras — com 1
amostra só (bem comum logo após boot), ficava preso na mensagem de
espera; (b) intervalo de amostragem fixo em 5min (Fase 24) significa
esperar 5min pela 2ª amostra. Corrigido: **amostragem adaptativa** — as
10 primeiras amostras saem a cada 1min (cobre os primeiros ~10min bem
mais rápido, comparável ao PWA que já tem gráfico em ~1-2min via
Firestore), depois volta pro intervalo normal de 5min. E agora desenha
com **1 amostra só** (um ponto, sem linha) em vez de exigir 2. ⚠️
Trade-off assumido: o eixo de horário assume intervalo FIXO entre
amostras pra calcular os rótulos — durante essa janela adaptativa
inicial (~10min), os horários mostrados podem ficar levemente
imprecisos. Não critico (é só o texto do eixo, os dados em si estão
certos) e desaparece conforme os pontos mais antigos "rolam" pra fora
do buffer ou a amostragem normaliza.

**Bug 3 — fundo do overlay Min/Max/Med largo demais.** Trocado largura
fixa por `tft.textWidth()` — o retângulo agora acompanha exatamente o
texto, centralizado no espaço livre entre o rótulo do eixo Y e o dot.

**Bug 4 — cor do dot.** Verde → azul fraco (`#1a5276`, pedido
explícito), com uma variante mais escura pro pulso.

**Bug 5 — eixo X (hora) se misturando com eixo Y.** Cor trocada pra um
roxo/violeta discreto (`tft.color565(120,105,175)`), diferente do cinza
do eixo Y. Mais respiro da borda inferior (`y+h-24` em vez de `y+h-20`).

**Bug 6 — alinhamento dos rótulos do eixo Y.** Antes eram só números
"flutuando" perto do canto, sem nenhuma linha de referência real.
Adicionadas 3 linhas de grade de verdade (máx/meio/mín), com o texto
centralizado verticalmente EM CIMA de cada linha (`cursor_y = linha_y -
4`, com clamps pra não vazar pra fora do gráfico nas bordas).

**Status:** firmware compilado e gravado na placa (COM8). Ainda sem
confirmação visual do usuário desta versão específica.

---

### Fase 27 — SD resolvido de vez: `tft.getSPIinstance()` (2026-09-22)

Usuário foi fisicamente conferir os pinos 22/23/24 (necessários pra
Tentativa 2, SPI separado) e **não achou nenhum dos três** nos
conectores da placa (fotos reais de ambos os lados) — só confirmou o 18
livre. Essa placa de 44 pinos simplesmente não expõe GPIO22/23/24.

Antes de sugerir outra troca de pinos, procurei se a própria `TFT_eSPI`
tinha um jeito oficial de compartilhar o barramento SPI (em vez de
inicializar um segundo periférico, que foi a causa das duas tentativas
anteriores falharem). Achei: `TFT_eSPI::getSPIinstance()` é um método
público, e a **própria biblioteca tem um exemplo pronto** usando
exatamente esse padrão
(`examples/Generic/ESP32_SDcard_jpeg/ESP32_SDcard_jpeg.ino`):
```cpp
SD.begin(CS_PIN, tft.getSPIinstance());
```
Passar a mesma instância (em vez de deixar a lib `SD` criar uma por
conta própria) elimina o conflito de raiz: não existe mais um segundo
periférico disputando pino nenhum. Implementado:
- `SD_CS_PIN=18` (único pino novo de verdade).
- `SD.begin(SD_CS_PIN, tft.getSPIinstance())`, chamado depois do
  `tft.init()` (a instância só existe depois disso).
- `pinMode(SD_CS_PIN,OUTPUT); digitalWrite(SD_CS_PIN,HIGH);` bem cedo no
  `setup()` (antes do `tft.init()`), mesma cautela contra contenção de
  barramento que o exemplo oficial usa.
- Fiação **voltou a ser a mais simples**, exatamente a 1ª sugestão desta
  thread (antes de eu ter revertido por engano): `SD_MOSI`/`SD_MISO`/
  `SD_SCK` do leitor embutido no display emendados direto nos mesmos
  pontos de `SDI(MOSI)`/`SDO(MISO)`/`SCK` do conector principal — só o
  `SD_CS` precisa de um fio novo, até o GPIO18.

**Status (atualizado 2026-09-22):** ✅ **validado em hardware real, com
cartão físico.** Usuário emendou os 4 fios sem solda (pressão) e formatou
o cartão (512MB) como FAT32. Primeiro teste mostrou `[SD] Cartao OK,
logs serao gravados.` funcionando de ponta a ponta (log local +
`[Firebase] Leitura confirmada pelo servidor`), mas caiu de novo
("`NAO encontrado/montado`") após um **reset POWERON completo** (não
brownout/software) entre um boot e outro — mau contato mecânico mesmo,
não bug de fiação/pino: um fio se mexendo o suficiente pra interromper
e restaurar a energia de verdade por um instante. Corrigido pelo usuário
prendendo os 4 pontos de contato (sem solda) — confirmado estável desde
então.

---

### Fase 28 — "Reiniciar" remoto no PWA (comandoReiniciar) (2026-09-22)

Pedido: no modal de "Desligar" do PWA, adicionar uma 3ª opção —
"Reiniciar" (`ESP.restart()`, reset por software), diferente de
"Desligar" (deep sleep, Fase 15) que deixa o dispositivo inacessível até
alguém apertar um botão físico.

**Implementado espelhando o padrão já validado do `comandoDesligar`
(Fase 15/16/18), com a mesma disciplina de prevenção de loop:**
- PWA: modal `#modal-desligar-backdrop` ganhou um 3º botão
  ("Reiniciar", `.btn-warn` — âmbar, entre o neutro `.btn-secondary` e o
  vermelho `.btn-danger`), escreve `comandoReiniciar: true` no Firestore.
  Texto do modal reescrito pra explicar as duas opções lado a lado.
- Firmware: `comandoReiniciar` entrou no mesmo poll (2.5s) de
  `atuadores`/`comandoDesligar` (`DocumentMask`), processado em
  `applyActuatorsFromFirestore()`. Ao receber `true`, chama
  `reiniciarRemoto()`: desliga os 8 relés (segurança), limpa o campo de
  volta pra `false` (melhor esforço, síncrono) e chama `ESP.restart()`.
- **Prevenção de loop de reboot** — `ESP.restart()` é um reset por
  SOFTWARE, que `RTC_DATA_ATTR` sobrevive (diferente de deep sleep, que
  não tem uma "causa de wake" equivalente pra usar como sinal). Em vez
  de depender só da limpeza do campo no Firestore confirmar a tempo
  (podia falhar com rede lenta logo após reiniciar, mesma classe de bug
  da Fase 16), uma flag `RTC_DATA_ATTR bool
  reinicioRemotoAguardandoSupressao` é setada ANTES do `ESP.restart()` e
  lida bem cedo no boot seguinte (`ativarSupressaoReiniciarSeNecessario()`,
  100% local) pra ignorar qualquer `comandoReiniciar=true` residual por
  20s — mesmo raciocínio de "logical clock local, não depende de rede"
  já estabelecido pro `comandoDesligar` (Fase 18), aplicado aqui porque
  é literalmente a mesma classe de bug com o mesmo formato.
- O botão "Reiniciar" local já existente (tela Sistema) não foi tocado —
  ele nunca leu/escreveu `comandoReiniciar`, então não tem esse risco de
  loop (só reinicia uma vez, na hora do toque).

**Status:** firmware compilado (RAM 15,8%, Flash 21,4%) e gravado na
placa (COM8). PWA testado no browser pane com o stub do Firebase (modal
abre com as 3 opções, "Reiniciar" grava `comandoReiniciar: true` —
confirmado no console) e implantado em produção (deploy feito).

**🔴 Bug crítico encontrado em produção logo após o teste real (2026-09-22):
loop automático de reboot.** Usuário clicou "Reiniciar" no PWA e o
dispositivo entrou em loop — reiniciava sozinho repetidamente, sem
parar, sem precisar de nenhuma intervenção externa (bem pior que o
equivalente do "Desligar": lá, na pior hipótese, o dispositivo só volta
a dormir, não reinicia sozinho infinitamente).

**Causa raiz:** a supressão local (`reiniciarSuprimidoNesteBoot`) só
durava uma janela FIXA de 20s (`REINICIAR_SUPRESSAO_MS`) — mas a
limpeza de `comandoReiniciar` no Firestore, feita em `setup()` via
`limparComandoReiniciarSeNecessario()`, podia sozinha levar até ~26s no
pior caso (10s esperando `fbApp.ready()` + até 3 tentativas de patch de
5s cada). Como essa limpeza bloqueava o `setup()` inteiro (`loop()` só
começa depois), na pior hipótese a janela de 20s já tinha expirado
ANTES do primeiro poll em `loop()` rodar — e se o campo ainda estivesse
`true` (bem provável: a tentativa de limpeza feita dentro de
`reiniciarRemoto()`, ANINHADA dentro do callback assíncrono do próprio
poll que a disparou, quase certamente nunca confirmava de verdade — ver
abaixo), o primeiro poll reiniciava de novo imediatamente. Loop.

**Correção (Fase 28b):**
1. **Supressão não expira mais por tempo curto.** `reiniciarSuprimidoNesteBoot`
   fica ativo até `limparComandoReiniciarSincrono()` confirmar de
   verdade a limpeza no Firestore — sem essa confirmação, `comandoReiniciar`
   nunca é reprocessado, não importa quanto tempo passe.
   `REINICIAR_SUPRESSAO_VALVULA_MS` (3min) virou só uma válvula de
   segurança pro caso extremo de Firestore genuinamente inacessível por
   muito tempo (nesse caso arrisca 1 restart a cada 3min, não a cada
   ~20s — muito mais tolerável, e sinal claro de que algo mais grave
   está errado).
2. **Removida a tentativa de limpeza dentro de `reiniciarRemoto()`** —
   ela rodava aninhada dentro do callback assíncrono do poll
   (`fbGetCallback` → `applyActuatorsFromFirestore` → `reiniciarRemoto`),
   bombeando `fbApp.loop()` de forma reentrante dentro do próprio
   callback que o dispara. Padrão arriscado no `FirebaseClient` (o
   cliente async provavelmente não processa a nova tarefa nesse
   contexto) — na prática, essa tentativa quase certamente **nunca**
   confirmava a limpeza, mas o código seguia em frente pro
   `ESP.restart()` mesmo assim, sempre deixando o campo `true` de
   verdade pra trás.
3. **Limpeza real virou um retry periódico dentro de `loop()`** (a cada
   5s, só enquanto `reiniciarSuprimidoNesteBoot` estiver ativo), fora de
   qualquer callback async — mesmo padrão seguro já usado por todo o
   resto do polling normal. Assim que confirma, desativa a supressão.
4. Removida a limpeza bloqueante de `setup()`
   (`limparComandoReiniciarSeNecessario()`, ~26s de pior caso) — não é
   mais necessária (o retry em `loop()` cobre isso) e só atrasava o boot
   sem necessidade.

**Lição pra próxima vez que um padrão parecido for reaproveitado:** o
padrão de "supressão local após um comando remoto" (Fase 15/16/18, pro
`comandoDesligar`) tem uma suposição implícita que só é segura quando a
CONSEQUÊNCIA de errar é benigna — no caso do deep sleep, se a janela
expirar cedo demais, o pior que acontece é o dispositivo voltar a
dormir (precisa de um humano apertar um botão de novo pra sequer ter
chance de repetir). Reaproveitar o MESMO desenho (janela de tempo fixa)
pra uma ação que se AUTO-DISPARA de novo sozinha assim que volta
(`ESP.restart()`, sem precisar de nenhuma intervenção física) troca
"pode ficar dormindo até alguém notar" por "loop automático
ininterrupto" — categoria de risco bem mais séria, que exige a
supressão ser uma garantia de verdade (baseada em confirmação, não em
timer) desde a primeira versão, não como correção depois de um
incidente real.

**Status pós-fix (Fase 28b):** gravado na placa, mas o log real do
usuário mostrou que **o loop continuou** — a cada ~10-11s, um novo
`ESP.restart()`, sem NUNCA imprimir a mensagem de supressão
("[Reiniciar] Boot seguinte a um reinicio remoto...").

**🔴 2ª causa raiz encontrada (Fase 28c): `RTC_DATA_ATTR` não sobrevive
a `ESP.restart()` nesta placa.** A Fase 28b tinha copiado a suposição já
usada pro `bootLoopCounter` (Fase 19) de que `RTC_DATA_ATTR` sobrevive a
qualquer reset menos perda de energia — só que essa suposição vale pros
resets que o `bootLoopCounter` de fato observa (Brownout/Watchdog/Panic),
não necessariamente pro tipo específico que `ESP.restart()` gera
(`rst:0xc RTC_SW_CPU_RST`, confirmado no log real). O `wake cause` do
deep sleep (usado no `comandoDesligar`) é um **registrador de hardware**
de verdade, sempre confiável; a flag `RTC_DATA_ATTR` que a Fase 28b
introduziu é só uma variável em RAM que, empiricamente, **voltava pro
valor inicial (`false`) em todo `ESP.restart()`** — supressão nunca
ativava, campo `comandoReiniciar` nunca era protegido, loop total.

**Correção definitiva (Fase 28c):** trocado `RTC_DATA_ATTR` por um
**arquivo marcador na LittleFS** (`/reiniciando.flag`) — flash, não RAM,
sobrevive **qualquer** tipo de reset (e até perda de energia).
`reiniciarRemoto()` grava o arquivo antes do `ESP.restart()`;
`ativarSupressaoReiniciarSeNecessario()` (chamada logo após
`LittleFS.begin()` no `setup()`, span cedo, 100% local) checa a simples
presença do arquivo — se existir, apaga (consumido) e ativa a supressão.
Zero dependência de qual registrador de reset o `ESP.restart()` usa
internamente.

**Status:** ✅ **confirmado pelo usuário.** Gravado na placa (COM8) uma
2ª vez, interrompendo o loop ativo. Reiniciou mais UMA vez após o flash
(esperado — o firmware anterior nunca tinha criado o marcador em
LittleFS), e a partir daí estabilizou de vez. Ciclo completo do
"Reiniciar" remoto (PWA → firmware recebe → aplica uma única vez →
volta e fica estável) validado em hardware real.

---

### Fase 29 — Logs redesenhados: tela cheia, paginação, busca (PWA + Firmware) (2026-09-22)

Pedido do usuário (com foto da tela física de Logs em uso real): layout
mais prático nos dois lados, ícone de lixeira em vez de botão de texto,
setas de paginação, e busca por texto/data.

**Firmware (TFT 240×320):**
- Header encolhido de 28px pra 20px, botão "Limpar logs" (footer grande)
  virou ícone de lixeira no próprio header, ao lado de um novo ícone de
  lupa — ambos desenhados com primitivas (`drawTrashIcon`/`drawSearchIcon`,
  mesmo estilo de `drawXBtn` já usado no resto do app, sem fonte de
  ícones/bitmap). Isso abriu espaço pra **13 linhas por página** (era 11
  fixas, sem paginação nenhuma).
- **Paginação de verdade:** o log agora é lido do SD uma vez ao abrir a
  tela (`abrirTelaLogs()` → `carregarLogsBuffer()`, janela de leitura
  subiu de 4KB pra 12KB de cauda do arquivo — até 120 linhas em RAM),
  não a cada redraw — navegar entre páginas é só reindexar o buffer já
  carregado, sem tocar no SD de novo. Setas pulsantes no rodapé, mesmo
  padrão visual (`drawSettingsPageIndicator`) já usado na paginação de
  Configurações (Fase 25).
- **Busca:** ícone de lupa abre uma tela de teclado virtual dedicada
  (`drawLogsSearchScreen`/`handleLogsSearchKbTap`) — **duplicada** de
  propósito a partir do teclado do WiFi (mesmo grid/coordenadas,
  reaproveitando `drawKeyboard()`) em vez de generalizar o código
  existente, pra não arriscar nenhuma regressão no fluxo de WiFi já
  testado e em produção. Filtro (substring, case-insensitive, contra
  texto + timestamp da linha) aplicado em RAM sobre o buffer já
  carregado — instantâneo, sem reler o SD. Chip do filtro ativo aparece
  logo abaixo do header (tocável, limpa o filtro).
- RAM subiu de 15,8% pra 16,5% (buffer de 120 linhas), Flash 21,4%
  (praticamente igual). Compilado e **gravado na placa (COM8)**.

**PWA (Configurações → Logs):**
- Mesmos 3 recursos: ícones de lupa/lixeira no cabeçalho da seção
  (`.icon-btn`), busca inline com `<mark>` destacando o trecho
  encontrado (via nós de texto DOM, não `innerHTML`, pra não correr
  risco nenhum de injeção mesmo o conteúdo sendo sempre gerado pelo
  firmware), paginação com setas circulares (15 linhas/página).
- **Paginação e busca 100% client-side**: o listener (`onSnapshot`)
  passou a trazer um lote generoso (`LOGS_FETCH_LIMIT=200`, era 30) já
  ordenado do mais recente pro mais antigo; navegar página ou digitar um
  termo de busca só refiltra/refatiam esse array em memória — sem ida e
  volta de rede a cada toque, e sem lidar com cursors bidirecionais do
  Firestore (`startAfter`/`endBefore`), que são notoriamente inconvenientes
  de acertar com paginação pra frente E pra trás.
- **Bug de CSS real encontrado e corrigido na hora**: `.logs-search-row`
  e `.logs-pagination` ficavam **visíveis mesmo com o atributo `hidden`**
  — mesma classe de bug já documentada nas Lições Aprendidas (regra do
  autor com a mesma especificidade da regra padrão do navegador pro
  `[hidden]`, e por vir DEPOIS na cascata, vencia o empate). Corrigido
  com `.logs-search-row[hidden] { display: none; }` (especificidade
  maior, resolve o empate de propósito). Pego e corrigido durante o
  teste no browser pane, antes de qualquer deploy.
- **🔴 Bug de permissão real encontrado (não relacionado ao pedido, mas
  bloqueava a feature inteira): a subcoleção `chocadeiras/{id}/logs`
  NUNCA teve uma regra própria no `firestore.rules`.** Subcoleções não
  herdam `allow` do documento pai no Firestore — precisam de `match`
  explícito, exatamente como `leituras`/`configuracoes` já tinham. Sem
  isso, todo `onSnapshot` do PWA contra essa coleção batia em
  `permission-denied` (escondido — o app só loga no console, mostra
  "Nenhum evento ainda." como se estivesse vazio) — a seção Logs do PWA
  muito provavelmente **nunca mostrou dado real** desde a Fase 21,
  mesmo o firmware gravando perfeitamente via Admin SDK (que ignora
  Security Rules). Corrigido: novo bloco `match /logs/{logId}` — `read`
  liberado pro dono (resolve o bug), `create`/`update` continuam `false`
  (só o firmware, via Admin SDK, cria), `delete` liberado pro dono (novo,
  pro ícone de lixeira desta fase).
- **Ícone de lixeira** (novo — não existia antes): apaga o histórico
  SINCRONIZADO no Firestore (não o log local do SD do firmware, que tem
  seu próprio "Limpar" na tela física) via `batch().delete()` em todos
  os docs da subcoleção. Confirmação inline (mesmo padrão do "Parar
  ciclo"), não modal — ação séria mas recuperável (o SD continua com o
  histórico completo).
- Testado no browser pane com stub do Firebase (24 logs sintéticos,
  cobrindo paginação 2 páginas): busca filtra e destaca corretamente,
  paginação avança/volta com os botões corretos habilitados/desabilitados,
  limpar mostra confirmação e chama `batch.commit()` com os IDs certos.
  Testado também em viewport mobile (375px) — layout permanece legível,
  ícones com alvo de toque confortável. Deploy feito (hosting +
  firestore:rules).

**Status:** firmware compilado e gravado na placa (COM8) — ainda sem
confirmação visual do usuário desta tela específica em hardware real.
PWA testado no browser pane e implantado em produção (regras +
hosting).

---

### Fase 30 — Logs automáticos (cobertura ampliada) + Configurações agrupada + investigação do "Reiniciar" (2026-09-22)

Pedido grande, combinando 3 frentes. O usuário pediu explicitamente pra
**simplificar sempre que reduzisse risco de bug**, mantendo o estilo
atual do firmware — decisões de escopo abaixo seguem essa instrução.

**1. Logs automáticos — cobertura ampliada (Firmware):**
- `setRelayAndSync()` (toque local) e `applyActuatorsFromFirestore()`
  (toggle remoto, **não tinha NENHUM registro antes** — lacuna real)
  agora chamam `logEvento("Atuador", "NOME -> ON/OFF", true)` —
  `importante=true` (antes `false`, só SD) porque o pedido era ver esses
  eventos no histórico do PWA.
- Nova `logLeituraSensorPeriodica()` — registra `Temp/Umid` a cada 5min
  (`SENSOR_LOG_INTERVAL_MS`, chamada do `loop()`), separado do push de
  60s já existente (`pushLeituraToFirestore`, alimenta os cards/gráfico
  em tempo real — propósito diferente).
- Nova `checarAlertasSensor()` (chamada de dentro de `lerSht31()`) —
  registra "Alerta" só na **borda** (entrando/saindo da faixa ideal,
  `TEMP_IDEAL_MIN/MAX`/`UMID_IDEAL_MIN/MAX`), não a cada leitura — senão
  viraria ruído a cada 5s com o valor fora da faixa.
- `fbHeartbeatCallback()` — registra "Erro" também por borda
  (falhando→ok / ok→falhando), mesmo padrão já usado em WiFi
  caiu/restabelecida. Único ponto instrumentado pra "erros de
  sincronização" (não todo callback do FirebaseClient) — heartbeat já é
  o sinal de saúde de sync estabelecido no projeto desde a Fase 13.
- `logEvento("Boot", ...)` — `importante` virou sempre `true` (era só
  `resetFoiAnormal`) — "Reinícios do sistema" pedia TODOS, não só os
  anormais.
- **Simplificação assumida**: o schema do Firestore continua
  `categoria`+`mensagem` (2 campos), não 4 campos separados
  (tipo/componente/ação/valor) como o pedido detalhava literalmente —
  `categoria` já cobre "tipo" (Atuador/Sensor/Alerta/Erro/Boot/...) e
  `mensagem` já embute componente+ação+valor de forma legível (ex:
  `"Resistência -> ON"`, `"Temp 38.2C / Umid 55.0%"`). Migrar pra 4
  campos exigiria mudar o parser do PWA, o formato salvo no SD e o
  parser da tela Logs do firmware — risco bem maior pra um ganho
  pequeno (o filtro por tipo já funciona igual com `categoria`).

**2. Logs — Sincronizar / filtro / auto-refresh (PWA):**
- Botão "Sincronizar" (ícone, gira ao clicar) — reassina o listener na
  hora (`escutarLogs(id, false)`), preservando busca/filtro/página
  atuais. **Simplificação assumida**: não implementa um comando remoto
  novo pra "puxar o log cru do SD" (isso exigiria uma nova classe de
  comando Firestore→firmware→upload em lote, a MESMA categoria de
  mecanismo que já causou 2 incidentes reais nesta sessão —
  `comandoDesligar`/`comandoReiniciar`) — o listener já é ao vivo, então
  "sincronizar" aqui é reforço/reassinatura, não uma feature nova de
  rede.
- Filtro por tipo — `<select>` com as categorias reais já gravadas
  (Atuador/Sensor/Alerta/Erro/Boot/WiFi/Desligar/Reiniciar), combinado
  (E lógico) com a busca por texto já existente (Fase 29).
- Auto-refresh — reassina o listener a cada 60s
  (`LOGS_AUTO_REFRESH_MS`), mesma ideia já usada em `escutarAtuadores()`
  (Fase 14, contra listener "zumbi"). `aplicarFiltroLogs()` não zera
  mais a página sozinha (só quem muda um filtro de verdade zera) —
  senão o auto-refresh periódico jogaria quem estivesse lendo a página
  3 de volta pra página 1 a cada minuto.

**3. Configurações — atuadores agrupados por categoria (PWA):**
- `RELAY_GRUPOS` (novo, em `app.js`) mapeia cada canal a um grupo
  decorativo: Aquecimento 🔥 (Resistência), Umidificação 💧
  (Umidificador + Bomba Água), Ventilação 💨 (Bomba Ar), Iluminação 💡
  (LED Ovoscópio), Áudio 🔊, Reservas 🔌 (Reserva 1/2). Cabeçalho
  espanando a grade inteira (`grid-column: 1/-1`) antes de cada grupo +
  bolinha de cor no canto de cada card (`::before`, não conflita com o
  `::after` já usado pro brilho neon de ligado).
- **Importante**: o agrupamento é 100% decorativo (ícone + cor) — a
  lógica de liga/desliga, confirmação de 220V, bloqueio offline etc
  continua idêntica, por canal, sem nenhuma mudança.
- **Simplificações assumidas** (pedido explícito do usuário pra reduzir
  risco): sem toast notifications novas (reaproveitado
  `mostrarFeedbackHeader()`, já testado); sem loading state por-toggle
  durante a escrita no Firestore; sem haptic feedback (`navigator.vibrate`,
  "quando disponível" no pedido original); sem breakpoint extra de 3
  colunas pra tablet (ficou 4/2, já testado em produção); sem auditoria
  formal de contraste WCAG AA (paleta escura já em uso, não mudou).
  Nenhuma dessas ausências quebra nada — são features a mais que ficaram
  de fora, não bugs.

**4. "Reiniciar não funciona" — investigado, NÃO reproduzido ainda:**
Revisão de código não achou nenhuma regressão da Fase 29 no caminho de
`comandoReiniciar` (`applyActuatorsFromFirestore`, `reiniciarRemoto()`,
`ativarSupressaoReiniciarSeNecessario()`, `DocumentMask` do poll —
tudo intacto). Confirmado que o app.js **em produção** tem o listener
`btn-confirmar-reiniciar` correto (fetch direto do `/app.js` implantado).
Usuário confirmou que o status aparece Online e o botão não está
desabilitado (descarta a hipótese de falso-offline bloqueando o menu).
Reconstituindo a linha do tempo: como a Fase 28b nunca limpava
`comandoReiniciar` no Firestore (removido de propósito por rodar
aninhado num callback async, ver Fase 28b/c), o campo ficou `true` por
todo o incidente do loop — só foi confirmado limpo (`false`) depois que
o retry periódico da Fase 28c conseguiu confirmar a limpeza (usuário
relatou "estabilizou"). Não há evidência de que o mecanismo em si esteja
quebrado agora, mas depois de 2 incidentes reais na mesma feature nesta
sessão, decidido NÃO especular um 3º fix sem dado real — **pendente log
do Serial (`pio device monitor`) no momento exato de um clique novo em
"Reiniciar"** antes de qualquer mudança de código.

**Status:** firmware compilado e gravado na placa (COM8). PWA testado
no browser pane (filtro de tipo, sincronizar preservando filtro/página,
auto-refresh armado, atuadores agrupados renderizando corretamente) e
implantado em produção. "Reiniciar" — aguardando log do usuário.

---

### Fase 31 — Causa raiz real do "Reiniciar/atuadores não funcionam": cota diária de leitura do Firestore estourada (429) — causada pelo auto-refresh da Fase 30 (2026-09-22)

Usuário mandou o log real pedido na Fase 30, e ele finalmente resolveu o
mistério: **não é bug nenhum no código do `comandoReiniciar`/atuadores em
si** — é `[Firebase Get ERRO] HTTP Status 429, code: 429` em praticamente
TODO poll (`pollActuatorsFromFirestore` e `pollCicloFromFirestore`),
continuamente, inclusive logo após um boot totalmente fresco. 429 do
Firestore = `RESOURCE_EXHAUSTED`, e o padrão bate exatamente com a **cota
diária GRATUITA de leituras (50 mil/dia) estourada**: os `GET` (leituras)
falhavam quase sempre, enquanto `PATCH`/`CREATE` (heartbeat, leitura de
sensor) continuavam funcionando — Firestore trata cota de leitura e de
escrita separadamente, e só a de leitura tinha estourado.

**Causa raiz identificada: o "auto-refresh a cada 60s" que eu tinha
acabado de adicionar no PWA (Fase 30).** Ele reassinava o `onSnapshot`
dos Logs (`escutarLogs`) a cada 60s via `setInterval` — mas reassinar um
listener força uma releitura COMPLETA do resultado da query (até
`LOGS_FETCH_LIMIT`=200 documentos), não só o delta. Com o PWA aberto o
dia inteiro numa aba, isso sozinho soma **até 288 mil leituras/dia**
(200 docs × 1440 reassinaturas/dia) — muito além da cota de 50 mil,
somado ao poll do firmware (2,5s ≈ 34,5 mil/dia, já perto do limite
sozinho desde a Fase 13, de propósito, com margem). Assim que a cota do
dia estourava, **QUALQUER leitura no projeto inteiro parava de
funcionar** — não só o auto-refresh do PWA, mas também o poll do
firmware que lê `comandoReiniciar`/`comandoDesligar`/`atuadores`. Por
isso "nada acontecia" ao clicar Reiniciar/Desligar/qualquer atuador: o
comando ERA escrito no Firestore (escrita, cota diferente, ainda
funcionando), mas o firmware nunca conseguia LER esse valor de volta pra
agir nele.

Isso também explica retroativamente o "Reiniciar não funciona" relatado
2 rodadas atrás, antes até da Fase 30 existir — bem provável que a cota
já estivesse perto do limite (poll do firmware sozinho é ~69% da cota)
e algum outro uso do dia (o próprio teste indo e voltando, listeners do
PWA sendo abertos e fechados repetidas vezes) tenha sido o empurrão
final naquele dia específico. Não dava pra confirmar sem o log — por
isso não arrisquei um 3º fix especulativo até ter o dado real.

**Corrigido:** removido o `setInterval` de auto-refresh inteiro.
`onSnapshot` já empurra eventos novos em tempo real sozinho, sem
nenhum custo adicional de "ficar checando" — o auto-refresh nunca
tinha necessidade técnica nenhuma, só atendia ao pedido literal de
"atualizar a cada X segundos" de um jeito caro. O botão "Sincronizar"
continua existindo (ação manual, um clique por vez, barata de verdade).

**Lição importante pra qualquer feature futura que toque Firestore:**
reassinar um `onSnapshot` (`unsubscribe()` + nova chamada) **relê o lote
inteiro da query**, não é grátis como parece — só usar isso sob demanda
(clique do usuário), nunca em `setInterval`/polling automático, e sempre
somar mentalmente contra a cota de 50 mil leituras/dia do plano
gratuito antes de adicionar qualquer nova leitura periódica (firmware OU
PWA).

**Também revertido nesta rodada, a pedido explícito:** o agrupamento
visual dos atuadores por categoria (cabeçalhos coloridos, bolinha de cor
por card, ícones) da Fase 30 — usuário achou ruim ("ficou uma merda") e
pediu pra voltar exatamente como estava antes. `RELAY_GRUPOS`,
`.atuador-grupo-header`, a bolinha `::before` e as variáveis CSS
`--grp-*` foram removidos; `montarGridAtuadores()` voltou a ser a grade
plana original, sem nenhuma mudança na lógica de liga/desliga/220V (que
nunca tinha mudado mesmo). As melhorias de Logs (busca, paginação,
filtro por tipo, sincronizar, lixeira) NÃO foram revertidas — a
reclamação foi só sobre o estilo dos atuadores.

**Não investigado ainda (relato separado, sem log específico):** "no
smartphone o PWA não abre, ERR_CONNECTION_ABORTED". Esse erro acontece
na camada de conexão (TCP/TLS), não é um erro HTTP normal -- não bate
com o padrão de cota do Firestore estourada (isso afetaria leituras de
dados, não o carregamento dos arquivos estáticos do Hosting, que é uma
infraestrutura separada/CDN). Mais provável ser algo local do
aparelho/rede (dado móvel vs. WiFi, VPN, bloqueador de anúncio, DNS,
Service Worker corrompido nesse aparelho específico) do que um bug no
código. Pendente reprodução ou mais detalhes do usuário antes de
investigar mais a fundo.

**Status:** deploy feito (hosting). A cota de leitura estourada hoje
**só volta a permitir leituras depois do reset diário do Firestore**
(meia-noite Pacific Time) — mesmo com o fix já no ar, os botões
Reiniciar/Desligar/atuadores continuam sem efeito até lá, porque o
firmware genuinamente não consegue LER nenhum comando novo enquanto a
cota do dia não resetar. Isso não é mais um bug pendente de código — é
só esperar a cota renovar. A partir de amanhã, sem o auto-refresh
consumindo cota à toa, o consumo diário deve voltar pro patamar normal
(~35 mil/dia, dominado pelo poll do firmware, bem abaixo do limite).

**✅ CONFIRMADO (2026-09-23), com dado real:** cota resetou no novo dia
e o usuário mandou um log do Serial limpo (zero `429` do início ao fim)
mostrando **dois ciclos completos e independentes de "Reiniciar"
remoto** (via PWA) — cada um reiniciou uma única vez, sem loop nenhum,
com `[Reiniciar] comandoReiniciar confirmado limpo no Firestore --
supressao desativada.` aparecendo poucos segundos depois de cada boot.
Um "Desligar" (deep sleep) + acordar via botão físico também apareceu
no mesmo log, igualmente limpo (`comandoDesligar confirmado limpo`).
Confirma de vez a hipótese da Fase 31: o mecanismo do
`comandoReiniciar`/`comandoDesligar` sempre esteve correto (Fase 28c) —
os dois incidentes de "não funciona" (o de duas rodadas atrás e o de
ontem) foram 100% cota de leitura estourada, nunca um bug de lógica.
Painel de uso do Firestore (print do usuário) confirma consumo saudável
na última hora (466 leituras, 27 escritas, pico de 3 listeners) — bem
abaixo do limite diário, do patamar esperado sem o auto-refresh.
"Checklist do PWA" também passou, segundo o usuário. **Encerrado.**

---

### Fase 32 — Tela Sistema redesenhada: ações viraram ícones, temp. do ESP32, rodapé com 2 cards (2026-09-22)

Pedido: (1) trocar "Relés ligados" pela temperatura interna do ESP32
(Min/Méd/Max no mesmo campo compacto); (2) trocar os 2 botões grandes
"Reiniciar"/"Desligar" por ícones no canto superior direito; (3) criar
2 cards no rodapé, um deles com "Último reset"; (4) replicar no PWA "onde
for aplicável".

**Firmware:**
- `temperatureRead()` (API nativa do core Arduino-ESP32, sensor dedicado
  do chip — não disputa nada com WiFi/ADC) lida a cada 5s em background
  (`atualizarEspTempStats()`, chamada do `loop()` **sempre**, não só com
  a tela Sistema aberta — assim Min/Max refletem a sessão inteira desde
  o boot, não só o tempo que alguém passou olhando a tela). Tile novo
  (`drawEspTempTile`) substitui "Relés ligados" no mesmo lugar/tamanho
  do grid (166,133,146,30): 3 linhas compactas (label, valor atual
  colorido por faixa de calor, Min/Méd/Max) — cabe nos mesmos 30px que
  o tile antigo já usava.
- Ícones `drawRestartIcon`/`drawPowerIcon` (via `tft.drawArc()`, nativo
  do TFT_eSPI, já usado em gauges — não precisou desenhar arco na mão)
  no header, ao lado do título. Isso **liberou o rodapé inteiro** (antes
  ocupado pelos 2 botões grandes) pros 2 cards novos.
- Rodapé: card "Último reset" (era um banner de destaque logo abaixo do
  header — só mudou de lugar, mesma lógica de cor normal/anormal) +
  card novo "Cartão SD" (status que só existia na tela de Logs, agora
  também visível aqui — diagnóstico mais completo numa tela só).
- Grid de tiles subiu pra ocupar o espaço que o banner do topo liberou
  (linhas em y=34/67/100/133, era 58/91/124/157) — mesmo conteúdo dos
  outros 6 tiles (Firmware/ID/Chip/WiFi/Flash-PSRAM/RAM/Uptime),
  inalterados.
- Zonas de toque atualizadas: Reiniciar/Desligar agora respondem no
  header (`ty<28`, `tx` na faixa de cada ícone), não mais no rodapé.

**PWA:** não alterado nesta rodada — a tela "Sistema" do firmware não
tem um equivalente direto no PWA (que já resolve Desligar/Reiniciar via
modal, disparado por um item de menu com ícone+texto, não por 2 botões
grandes ocupando a tela). Perguntei ao usuário o que especificamente ele
quer replicado antes de mexer no PWA de novo — a Fase 30 já mostrou o
custo de adivinhar uma mudança visual no PWA sem confirmar primeiro
("ficou uma merda", revertido na Fase 31).

**Status:** compilado (RAM 16,5%, Flash 21,6%) e **gravado na placa
(COM8)**. Ainda sem confirmação visual do usuário em hardware real.
**PWA: confirmado pelo usuário que não precisa de nenhuma mudança nesta
rodada** ("Nada por agora") — o padrão de menu+modal já resolve
Desligar/Reiniciar de forma adequada pra tela grande.

---

### Fase 33 — Padronização visual da tela Sistema + Cooler dedicado no CH7 (2026-09-22)

Usuário mandou foto real da Fase 32 (grid renderizando certo, ícones e
cards no lugar) mas pediu padronização — os 8 tiles normais tinham só
contorno (sem preenchimento) enquanto os 2 cards do rodapé tinham só
preenchimento (sem contorno), lendo como 2 estilos diferentes na mesma
tela. Junto, pediu instrução pra conectar um cooler 12V dedicado.

**Padronização (firmware):**
- `SYS_TILE_BORDER` (mesmo azul dos tiles, `color565(46,46,90)`) virou o
  contorno de TODOS os 10 elementos da tela — os 2 cards do rodapé
  (Último reset, Cartão SD) ganharam `drawRoundRect` por cima do
  preenchimento colorido de status, em vez de ficar sem nenhuma borda.
- Cor do valor: Firmware/ID Disp. perderam o amarelo da marca (`cLogo`)
  sem motivo funcional — viraram branco, igual aos outros tiles
  neutros (Chip/Flash-PSRAM/RAM livre/Uptime). Cor só sobrou pra
  status de verdade (WiFi, Temp ESP32, Último reset, Cartão SD).
- Ícones Reiniciar/Desligar: cores trocadas pra bater com o MESMO
  significado já usado no modal do PWA (Fase 28) — Reiniciar = âmbar
  (`.btn-warn` lá, menos severo), Desligar = vermelho (`.btn-danger`
  lá, mais severo/deep sleep). Antes eram vermelho+roxo sem
  correspondência nenhuma com o outro lado do projeto; o roxo escuro
  também tinha baixo contraste com o fundo do header na foto real,
  quase sumindo.
- Não foi necessário adicionar uma 2ª página (usuário autorizou se
  precisasse) — a padronização era só cor/contorno, não precisou de
  mais espaço.

**Cooler 12V dedicado — CH7 (GPIO42) reaproveitado:**
Em vez de usar os pinos PWM reservados pra ventilação de incubação
(seção 9, GPIO4/5/6 -- projeto futuro maior, com MOSFET dedicado,
controle de velocidade e 3 fans), o pedido aqui é mais simples ("um
cooler dedicado ao sistema", liga/desliga, sem controle de velocidade)
— resolvido reaproveitando um canal de relé já cabeado e testado, sem
nenhum pino novo. `CH7`/`GPIO42` (era "Reserva 1") virou "Cooler
Sistema", `12V DC`. Renomeado em `RELAY_NAMES`/`RELAY_VOLTAGE` no
firmware E no `app.js` do PWA (mantidos sincronizados manualmente, como
o comentário do PWA já pedia). Zero mudança de lógica — o canal já
funciona exatamente como Bomba Água/Bomba Ar (mesma tensão, mesmo
padrão de relé).

**Status:** firmware compilado e gravado na placa (COM8). PWA
implantado em produção. Instruções de fiação do cooler dadas ao usuário
via chat (não fisicamente conectado ainda nesta sessão).

---

### Fase 34 — Rodapé sem verde, ações viraram tile na grade, página nova de armazenamento SD em Memória (2026-09-22)

Sem foto desta vez — pedido só por texto, então a interpretação abaixo é
a leitura mais coerente das instruções; vale confirmar com foto real
depois de gravado.

**Tela Sistema:**
- Os 2 cards do rodapé ("Último reset", "Cartão SD") **não usam mais
  fundo colorido** (`SYS_CARD_BG`, cinza-azulado neutro fixo) — status
  agora é só o CONTORNO + cor do texto (verde/vermelho/âmbar), nunca
  mais o card inteiro pintado.
- **Temperatura do ESP32 (Min/Méd/Max) mudou de lugar**: saiu do tile
  que tinha na grade (166,133) e passou a dividir o card "Cartão SD" no
  rodapé com ele (mesmo card, separador fino entre os dois blocos de
  info).
- **Isso abriu um tile vazio na grade** (onde a temperatura estava) —
  virou o novo local dos botões Reiniciar/Desligar
  (`drawSysAcoesTile()`): 2 botões de 68×30px lado a lado, MAIORES que
  os ícones pequenos do header (Fase 32/33) e com a mesma paleta já
  usada no modal do PWA (Reiniciar=âmbar, Desligar=vermelho). O header
  voltou a ter só "< Voltar" + título, sem ícone nenhum.
- Zonas de toque atualizadas de acordo (a ação agora responde em
  `ty 133-163`, não mais no header).

**Tela Memória — virou 2 páginas** (mesmo padrão de paginação já usado
em Configurações, Fase 25: bolinhas + setinhas pulsantes no rodapé):
- **Página 0** (inalterada): as 4 barras ao vivo (RAM heap, RAM livre
  mínima, Flash, PSRAM) que já existiam.
- **Página 1 (nova)**: card "Cartão SD" (status, mesmo estilo
  padronizado da tela Sistema) + card "Armazenamento (SD)" (usado/total
  via `SD.usedBytes()`/`SD.totalBytes()`, formatado em MB/GB
  automaticamente, com barrinha de progresso) + card "Maior bloco livre
  (heap)" (mudou pra cá, abrindo espaço no rodapé da página 0 pro
  indicador de página).
- Precisou de página nova mesmo — o pedido de "dados de armazenamento
  do microSD" não cabia mais na página 0 já cheia (4 barras ocupavam
  quase todo o espaço vertical disponível).
- `drawMemCurrentPageContent()` despacha pro conteúdo certo conforme
  `memPage` — usado no ponto de entrada, na troca de página e no
  acordar do screensaver (sem isso, acordar na página 1 ficaria com a
  área de conteúdo em branco, já que o refresh automático de 500ms só
  atualiza a página 0).

**Status:** compilado (RAM 16,5%, Flash 21,7%) e **gravado na placa
(COM8)**. Sem confirmação visual do usuário ainda — esta rodada foi só
por descrição em texto, não por foto, então há mais risco do que o
normal de precisar de um ajuste fino depois de ver o resultado real.

---

### Fase 35 — Cooler Sistema com controle automático por histerese (2026-09-23)

Pedido: ligar o cooler quando a temperatura atingir um máximo, desligar
quando cair até um mínimo — histerese clássica. Usuário já apontou o
problema em potencial antes de eu escrever qualquer código: usar o
Min/Méd/Max **ao vivo** da sessão (mostrado na tela Sistema) como
limiar de desligar é autorreferente — o Min foi capturado ANTES do
cooler agir, e assim que ele liga e começa a esfriar o ambiente,
alcançar aquele Min antigo de novo fica cada vez mais raro (o próprio
cooler "rouba" a queda de temperatura que faria o Min cair mais ainda).
Sintoma relatado: "Mn38 Md44 Mx44, cooler ligado há um tempo, nunca
alcançou mn38 de novo" — ou seja, travaria ligado pra sempre.

**Implementado:** `controlarCoolerPorHisterese()`, chamada a cada 5s
junto com `atualizarEspTempStats()`. Limiares **fixos**
(`ESP_TEMP_COOLER_ON=55°C`, `ESP_TEMP_COOLER_OFF=48°C`, 7°C de
histerese), totalmente independentes do Min/Méd/Max ao vivo — esse
continua sendo só um diagnóstico exibido na tela, nunca usado como
entrada de controle. 55°C bate com o limiar visual "âmbar" já existente
em `drawEspTempTile` (o mesmo ponto em que a tela já mostra a
temperatura ficando alaranjada); 48°C dá uma margem confortável de
histerese, alcançável a partir da faixa ociosa típica observada
(~38-44°C sem o cooler).

**Baseado na temperatura do CHIP ESP32** (`espTempAtual`, Fase 32), não
no SHT31 — o "Cooler Sistema" (CH7/GPIO42, Fase 33) é pra resfriar a
eletrônica, não pra incubação.

**Zona morta:** entre 48°C e 55°C, mantém o que já estava ligado —
isso É a histerese (sem ela, oscilar bem em cima de um único limiar
liga/desliga o relé repetidamente, desgastando-o à toa).

**Efeito colateral consciente, não implementado como proteção:** como o
controle roda a cada 5s incondicionalmente, um toggle MANUAL do canal
"Cooler Sistema" (touchscreen ou PWA) pode ser revertido pelo próximo
tick se a temperatura ainda pedir o estado oposto — comportamento
esperado de um controle tipo termostato, não um bug. Se o usuário quiser
um jeito de desativar o automático temporariamente (ex: manutenção),
isso ainda não existe e precisaria ser pedido à parte.

Cada toggle automático grava um log (`categoria="Sistema"`, `importante=
true`, ex: `"Cooler Sistema -> ON (automatico, 55.2C)"`) — sincroniza
pro PWA como qualquer outro evento, distinguível de um toggle manual
(que continua logado como `categoria="Atuador"`).

**Status:** compilado (RAM 16,5%, Flash 21,7%) e gravado na placa
(COM8). Não validado ainda com o chip realmente cruzando os dois
limiares em uso real (só compilado/gravado nesta sessão).

---

### Fase 36 — Cooler Sistema bloqueado pro toque manual (Firmware + PWA) (2026-09-23)

Usuário apontou a consequência direta da Fase 35 que eu tinha deixado
como "efeito colateral consciente, não implementado": se o canal
continua tocável, um toggle manual é revertido pelo próximo ciclo de
histerese (5s) sem aviso — confuso, parece bug. Corrigido bloqueando o
canal dos dois lados, em vez de só documentar o comportamento.

**Firmware:** `drawAtuadoresScreen()` — o canal `COOLER_CH` (índice 6)
não desenha mais o toggle normal (`drawToggle`), desenha um badge "AUTO"
fixo (cor conforme ligado/desligado, sem a bolinha arrastável que sugere
interatividade). `handleTap` — toque nessa linha específica não chama
`setRelayAndSync` — em vez disso, mostra um aviso rápido ("Controlado
automaticamente (histerese de temperatura)") por 900ms e volta.

**PWA:** `montarGridAtuadores()` — `input.disabled = true` +
`title` explicando o motivo (tooltip ao passar o mouse) + um badge
"AUTO" visual (`.atuador-auto-badge`, mesma linguagem do badge "EM
DESENVOLVIMENTO" já usado em Áudio/Alertas) abaixo do nome do canal.
`atualizarControlesDisponibilidade()` — que reabilitaria os toggles
quando o status volta a ficar "online" — agora pula explicitamente o
índice do Cooler (`COOLER_CH_INDEX`), senão o bloqueio "sumiria" assim
que a chocadeira ficasse online de novo.

**Testado** no browser pane: toggle do Cooler Sistema confirmado
`disabled=true` com o tooltip certo, resto da grade funcionando normal.

**Status:** firmware compilado e gravado na placa (COM8). PWA testado
no browser pane e implantado em produção.

### Fase 37 — Guia de incubação (Firmware + PWA) (2026-09-24)

Card "Guia" na Home dos dois lados + 10 tópicos em ordem cronológica:
Seleção/armazenamento, Preaquecimento, Momento de incubação, Operação,
Controle de temperatura, Umidade, Viragem, **Ovoscopia (novo — faltava)**,
Transferência (dia 18-19), Retirada do pintinho. Título 1 renomeado de
"Efeitos do armazenamento" pra "Seleção e armazenamento" (cobre também a
escolha dos ovos).

**Fonte única do conteúdo:** `tools/guia_content.json` + `node
tools/gerar_guia.js` gera `src/guia_data.h` (ASCII — a fonte do TFT não
tem acentos; o script valida resumo ≤40 e título ≤38 chars e falha em
caractere não-ASCII) e `frontend/guia-data.js` (UTF-8, com emoji). **Não
editar os gerados à mão** — editar o JSON e rodar o script.
Marcação do corpo: `# ` subtítulo, `- ` item.

**Firmware:** `S_GUIA` (lista de 7 linhas visíveis + setas ▲▼ na lateral
direita, sem rodapé) e `S_GUIA_TEXTO` (quebra de linha em runtime,
19 linhas/página, setas ◀▶ no cabeçalho + metade esquerda/direita do
texto; passar da última página avança pro tópico seguinte). Card na
Home em (4,156) — slot que estava livre. RAM 16,7 %, Flash 21,9 %.
**PWA:** card `#card-guia` (linha inteira da grade), `#screen-guia` com
lista + detalhe na mesma section, botões Anterior/Próximo. 100% local:
zero leituras no Firestore. `CACHE_VERSION` v4 + `guia-data.js` em
`CORE_ASSETS`.
Testado no browser pane (desktop + 375px); firmware compilado e gravado
(COM8), **sem confirmação visual em hardware ainda**.

### Fase 38 — Galeria "Fases do Pintinho" (Firmware + PWA) (2026-09-24)

11º item do Guia (`"galeria": true` em `tools/guia_content.json`). Só
imagens, sem texto (o dia já está desenhado nelas).
- **Imagens:** `frontend/guia/fases/*.jpg` (20 arquivos, 320×240; **falta o
  dia 20**; o dia 21 é retrato 240×320; o dia 10 se chama `...-day-10.jpg`
  em minúscula — a ordem natural pelo número do dia cuida disso, e o
  caminho no Hosting é case-sensitive, então o nome exato é usado).
- **PWA:** overlay tela cheia `#fases-overlay`, 3 zonas invisíveis de 33 %
  (anterior | fechar → volta ao Guia | próxima), setas do teclado/Esc,
  fade+slide (Web Animations, respeita `prefers-reduced-motion`),
  pré-carrega vizinhas. Lista vem de `window.GUIA_FASES` (gerada por
  `node tools/gerar_guia.js`, ordem natural). Troca de imagem usa
  `setTimeout`, **não** `onfinish` (travava com aba em segundo plano).
- **Firmware:** `S_FASES`. Imagens em **flash** (`src/fases_data.h`,
  ~284 KB) decodificadas com `bodmer/TJpg_Decoder`. **TJpg não lê JPEG
  progressivo** — os originais eram progressivos, por isso
  `python tools/gerar_fases.py` (requer `pip install pillow`) reconverte
  pra baseline 320×240 com barras pretas (dia 21 centralizado). Setas
  discretas nas laterais + mesmas zonas de 1/3 (centro volta ao Guia).
  **Sem fade/slide no TFT** (sem framebuffer). Ao adicionar/trocar
  imagens: rodar os DOIS scripts e recompilar.
  RAM 17,7 %, Flash 27,6 %.

### Fase 39 — Home mais aproveitada + wallpaper no screensaver (Firmware) (2026-09-24)

- **Home:** as 3 linhas de cards passaram de 56 → 64 px (`CARD_H`, `CARD_Y0/1/2`
  = 32/100/168; sobravam ~28 px embaixo) e a data solta virou um card
  "Data" (dd/mm + ano) na célula do meio da última linha (`drawDataCard()`,
  atualiza 1×/min). Zonas de toque em `handleTap` usam as mesmas constantes.
  Valor com >7 caracteres ("Em breve") cai pra fonte 1 (vazava da borda).
  Valor do card "Guia" agora é `cGreen`, igual ao Status.
- **Screensaver:** o fundo preto virou wallpaper escurecido
  (`tools/wallpaper_src.jpg` → `python tools/gerar_wallpaper.py [brilho]`,
  padrão 0,30; a "opacidade" é aplicada no arquivo, custo zero em runtime →
  `src/wallpaper_data.h`, 12 KB baseline). Decodificado uma vez pra um
  buffer RGB565 em **PSRAM** (150 KB, `wallBuf`); relógio e logo flutuante
  (mantidos) só restauram o retângulo de baixo (`wallRestore`) e desenham
  texto transparente (cor única). Sem PSRAM → cai no preto de antes.
  Para mudar brilho/imagem: rodar o script e recompilar.
- Interpretei "fundo preto" como o do screensaver (o único preto de
  verdade) e "manter hora + animação" como manter relógio e logo.
  RAM 17,8 %, Flash 27,8 %. Sem confirmação visual ainda.

### Fase 40 — Screensaver só com a hora (Firmware) (2026-09-24)

Pedido: tirar os segundos (bug), tirar o texto animado, melhorar a hora.
Removidos o logo "ChocaIoT" flutuante (e suas variáveis/`SAVER_*`) e os
segundos. Hora agora é `HH:MM` em **fonte 8** do TFT_eSPI (75 px, numérica;
`LOAD_FONT8` já estava ligado) centralizada, cor clara + sombra suave,
texto transparente sobre o wallpaper. Só redesenha quando o **minuto**
muda (checa 1×/s; `saverUltimoMinuto`). Compilado e gravado (COM8);
sem confirmação visual ainda.

### Fase 41 — Gerenciador de arquivos do SD (Firmware + PWA) (2026-09-24)

**Onde estão as imagens hoje:** nenhuma está no SD. Galeria "Fases do
Pintinho" → PWA em `frontend/guia/fases/` (Hosting) e firmware em **flash**
(`src/fases_data.h`, ~284 KB); wallpaper padrão → flash (`src/wallpaper_data.h`);
originais/fontes em `tools/` (`fases_fw/`, `wallpaper_src.jpg`). O SD só tinha
`/chocaiot.log` e `/chocaiot.old`. Cartão ~1,8 GB (ver `SD.totalBytes()`).

**Firmware** (Configurações → pág. 2 → "Arquivos"): navega pastas; toque no
nome abre pasta/menu do arquivo, "…" abre o menu de qualquer item. Ações:
Renomear (teclado virtual, 3ª cópia do padrão `handle*KbTap`), **Mover**
(modo de navegação + botão "Mover aqui"; recusa mover pasta pra dentro
dela mesma / destino existente), Deletar (confirmação; pasta só se vazia),
Nova pasta ("+"), **Wallpaper** (só .jpg/.jpeg). Wallpaper do SD:
`wallDefinirDoSD()` decodifica direto pro buffer em PSRAM (reduz 1/2/4/8 pra
caber em 320×240, centraliza, **escurece a 30 % na decodificação**), mostra
prévia 1,8 s e grava o caminho em LittleFS `/wallpaper.cfg`; se o arquivo
sumir/SD falhar, volta ao wallpaper embutido. **JPEG progressivo não decodifica**
(TJpg) → toast de erro.
**PWA** (Configurações → "Arquivos (cartão SD)"): lista, prévia
(imagem/texto), **Compartilhar** via Web Share API com arquivo; sem suporte
ou erro → **Baixar** automático.
**Como o PWA enxerga o SD (via Firestore, 100 % sob demanda):**
- `chocadeiras/{id}/arquivos/indice`: `json` (lista `{p,s}`, até 80 arquivos, 3
  níveis), `usadoMB`, `totalMB`, `atualizadoEm`. Publicado no boot (após 20 s),
  após mudança local e quando o PWA pede `|*`.
- PWA escreve `arquivoReq = "<id>|<caminho>"` no doc da chocadeira (entra no
  mesmo poll de 2,5 s: mesma máscara + `arquivoReq`, **zero leituras novas**).
  O parse fica no callback; o TRABALHO roda em `arqServico()` no `loop()` (3 s),
  nunca dentro do callback async (lição do comandoReiniciar). Sem loop de
  reboot possível (não reinicia nada); a 1ª leitura pós-boot só memoriza o
  valor (pedido antigo não é reatendido a cada reboot).
- Resposta em `arquivos/conteudo` (`reqId`, `path`, `mime`, `tam`, `truncado`,
  `erro`, `b64`); PWA escuta só durante o pedido e ignora `reqId` de outros.
  **Limite 150 KB** (doc Firestore ≤ 1 MB + base64 +33 %); texto/log maior vai
  pelo FIM (tail); imagem maior → mensagem "copie pelo leitor de cartão".
- Rules: `match /arquivos/{docId}` — dono só lê, `write: false` (firmware = Admin).
  Deploy de rules feito. Custo: 1 escrita + 1 leitura por arquivo aberto.
Testado: firmware compila/grava (RAM 18,9 %, Flash 28,2 %); PWA testado no
browser pane com stub (lista, prévia texto, fallback baixar). **Nada testado
ainda com arquivos reais no SD nem o upload real do firmware** — validar
colocando uma .jpg pequena no cartão e abrindo pelo PWA.

### Fase 42 — Ciclo v2: wizard, tela cheia do ciclo, tipos de ovos (Firmware + PWA) (2026-09-24)

**Wizard (4 etapas, tela cheia, "Etapa N de 4" + barra):** 1 nome do lote,
2 tipo (Galinha 21d / Codorna 17d / Personalizado = dias 1-60), 3 início
(PWA: `datetime-local`, firmware: steppers dia/mês/ano/hora/min; **não aceita
futuro**), 4 resumo + "Iniciar ciclo". Firmware sem nuvem (`fbApp.ready()`
falso) recusa iniciar (toast) — evita o ciclo "sumir" no poll seguinte.
**Tela do ciclo ativo (sem bordas):** nome, tipo, `dia/total`, % , início,
"Último dia começa às" (= início + (dias-1) dias; pausado → o fim anda junto
com a pausa). 3 ações com ícone e efeito de toque: **Pausar/Retomar**,
**Parar** (DESABILITADO até "Último dia começa às"; firmware/PWA mostram
"Parar libera em Xd Yh") e **Lixeira** (confirmação). Parar e Lixeira hoje
fazem a MESMA escrita (apagam `cicloIniciadoEm/PausadoEm/DuracaoDias/Nome/TipoNome`)
— só a confirmação/log ("encerrado" x "excluido") muda; não existe histórico
de lotes ainda. Campo "Duração do ciclo" removido (a duração vem do tipo).
**Firestore (doc da chocadeira), campos novos:** `cicloNome`, `cicloTipoNome`,
`tiposOvos` (string compacta `nome;dias;temp;umid;umidEcl;viraAte|...` —
escolhida porque arrays de mapas do REST são péssimos de parsear na mão no
ESP32). Sem `tiposOvos` valem os padrões Galinha/Codorna. Entram na máscara
do poll do ciclo (30 s) → **zero leituras novas**.
**Tipos (CRUD):** PWA = card "Tipos de ovos" em Configurações (modal com
nome, dias, temp/umid ideais, umid eclosão, viragem até dia; valida faixas e
nome único; mantém ≥1 tipo; máx. 8). Firmware = Configurações > pág. 2 >
"Tipos de ovos" (lista, "+" novo, editar com steppers, deletar em 2 toques).
Espelho local `/tipos.cfg` (LittleFS); firmware→nuvem com `lastLocalTiposMs`
(logical clock). Nomes sem `; | " \`. **Firmware não tem acentos**: nomes vindos
da nuvem são "dobrados" pra ASCII (`asciiFold`) — editar um tipo com acento pelo
firmware regrava sem acento.
**Firmware — arquitetura:** `S_CICLO` (vazio/ativo/confirmação), `S_CICLO_WIZ`,
`S_KBTXT` (teclado de texto genérico: 4ª cópia do padrão `handle*KbTap`, agora
parametrizada por buffer/callbacks), `S_TIPOS`, `S_TIPO_EDIT`. A tela do ciclo
redesenha a cada 30 s (contagem regressiva). Hora local = RTC; UTC = `time()`;
`tzOffsetSeg()` arredonda a diferença pra múltiplos de 15 min.
Testado: firmware compila/grava (RAM 19,0 %, Flash 28,5 %); PWA testado no
browser (fluxo completo do wizard, Parar habilitando no último dia, confirmações,
CRUD de tipos, mobile 375 px) e implantado. **Firmware não testado no display**.

### Fase 43 — Tipos "Ovos" completos + fim do "Ajustes Finos" (Firmware + PWA) (2026-09-24)

Resposta a foto do display: (1) botão **"Agora"** (etapa 3) só preenche data/hora
com o momento atual — dica na tela agora explica; (2) o "17" em "Viragem ate o dia"
era o **último dia** de viragem, não a quantidade — confundia. Agora são campos
separados: **Viragens por dia**, **Virar até o dia**, **1ª viragem (hora/min)** e
os horários resultantes (N por dia em intervalos iguais a partir da 1ª). **Só
guarda os parâmetros — não existe mecanismo/automação de viragem ainda.**
- Tipo ganhou **mínima/ideal/máxima** de temperatura e de umidade + os campos de
  viragem. Formato compacto agora tem 13 campos
  (`nome;dias;temp;umid;umidEcl;viraAte;tempMin;tempMax;umidMin;umidMax;viraPorDia;viraHH;viraMM`);
  registros antigos de 6 campos ainda são lidos (o resto é derivado).
- **Restaurar parâmetros** (ícone ↺ no cabeçalho da edição / botão "↺ Restaurar" no
  PWA): Galinha/Codorna voltam ao padrão de fábrica (`tipoFabrica()`); tipos
  próprios voltam aos valores de quando a edição abriu. No PWA precisa clicar em Salvar.
- Firmware: edição em **3 páginas** (1 nome/dias/temperaturas, 2 umidades,
  3 viragem + horários), setas < > e Salvar/Deletar na barra inferior; "novo tipo"
  usa as mesmas páginas.
- **"Ajustes Finos" removido** (tela, `/thresholds.cfg`, botão). A faixa ideal de
  Temp/Umid (cores dos cards, gráficos, alertas) agora vem da mínima/máxima do
  **tipo do ciclo ativo** (`aplicarLimitesDoTipo()` no firmware, `aplicarFaixasDoTipo()`
  no PWA; card Alertas do PWA mostra os valores atuais). Sem ciclo: faixa geral
  37,2–38,9 °C / 45–65 %. Botão de Configurações agora se chama **"Ovos"**.
- Guia (Controle de temperatura) atualizado pra falar de Configurações > Ovos.
Testado: PWA no browser (horários, restaurar, validações, faixa por tipo); firmware
compila/grava (RAM 19,0 %, Flash 28,5 %), **sem confirmação visual das 3 páginas**.

### Fases 44–50 — Feedback do SD, wallpapers, PIN, histórico, relatório, push e câmera (2026-09-24)

**Fase 44 — arquivos (feedback do usuário).** Sintomas: tocar num arquivo não abria; PWA "ainda não publicou a lista".
- **Causa real (achada lendo o serial):** a biblioteca FirebaseClient **NÃO escapa strings** (`Values::StringValue` só
  põe aspas em volta) → o índice do SD (`"p":...`) virava JSON inválido → HTTP 400 → **a lista nunca subiu desde a Fase 41**.
  Solução: `jsonEsc()` (main.cpp) em TODO texto que vai pro Firestore (índice, relatório, nome do lote, mensagens de log/push).
  **Regra: nunca passar texto livre pra StringValue/string_t sem `jsonEsc()`.** Também: publicação do índice só limpa o
  "sujo" quando realmente publicou (antes, sem hora NTP, o aviso era descartado) + backoff de 30 s em erro.
- **Máscara do poll (`pollActuatorsFromFirestore`) tem limite prático:** com 8 campos deu HTTP 400 (a mesma REST aceita — é a
  lib). Campos novos de comando/config vão no **mapa `cmd`** (`cmd.pushTeste`, `cmd.camReq`, `cmd.emailRec`, `cmd.pinRec`),
  máscara fica `atuadores,comandoDesligar,comandoReiniciar,arquivoReq,cmd`. Chaves procuradas por nome em `applyActuatorsFromFirestore`.
- Firmware: tocar num arquivo **abre** (texto → leitor paginado `S_TXTVIEW`, últimos 12 KB de logs; `.jpg` → visualizador
  `S_IMGVIEW`; pasta entra). Os "⋯" / arquivos de outros tipos abrem o menu (Abrir | Renomear | Mover | Deletar | Definir wallpaper).
- PWA: barra de **progresso com estimativa** (segundos, "costuma levar ~N s", Cancelar, timeout 45 s com mensagem);
  pede a lista sozinho na 1ª vez se o aparelho nunca publicou. (`extras.js` `criarProgresso`)

**Fase 45 — wallpapers (Config. > Tela).** Dois slots: **0 = tela de bloqueio/screensaver**, **1 = tela principal** (home).
Cada um: nenhum / padrão embutido / imagem do SD (menu do arquivo > "Definir como wallpaper" > escolhe o destino) +
**escurecimento 0–90 % ajustável** (barra ou −/+; aplica na hora; `/wall.cfg` no LittleFS). Guarda original (`src`) e escurecida
(`disp`) em PSRAM (150 KB cada). `wallpaper_data.h` agora é o JPEG **sem** escurecer (`gerar_wallpaper.py` padrão 1.0).
Home com wallpaper: `homeFill()/homeTexto()` (cards transparentes sobre a imagem); padrões: bloqueio 70 %, principal 75 %.

**Fase 46 — Segurança / PIN.** Config. > Segurança: Nenhum | PIN de 4 dígitos (hash SHA-256 com sal em `/seg.cfg`).
**Só ativa o PIN com e-mail de recuperação vinculado** — vinculado no PWA (Config. > Segurança, e-mail da conta logada → `cmd.emailRec`).
Bloqueia no boot e ao voltar do screensaver (`S_LOCK`, teclado numérico sobre o wallpaper de bloqueio; 5 erros = 30 s de espera).
**Recuperação ("Esqueci o PIN")**: o aparelho mostra um código de 6 dígitos; no PWA (logado com o e-mail vinculado) digita-se o código
→ `cmd.pinRec` → o aparelho confere e pede o PIN novo. **Não há envio de e-mail** (sem servidor): a prova do e-mail é o login da conta
no PWA + presença diante do aparelho. `pinAtivo` é publicado no doc (impede desvincular o e-mail com PIN ativo). **Não testado no display.**

**Fase 47 — histórico T/U.** SD `/hist/AAAAMMDD.csv` (`slot,t*10,u*10`, 1 ponto/10 min = média) + Firestore `historico/AAAA-MM-DD`
(campo `pts`, 1 doc/dia, 144 escritas/dia). Firmware: botão "7 dias" no rodapé da tela do sensor. PWA: abas Hoje | 7 dias no modal
(7 leituras). Regras: `historico` só leitura.

**Fase 48 — relatório de ciclo.** Acumula estatísticas enquanto há ciclo ativo (`/cicloStats.bin`); ao terminar (Parar/Lixeira, aqui ou no PWA)
gera `/relatorios/AAAAMMDD_HHMM_lote.txt` no SD + doc `ciclos/{auto}` (PWA lista/compartilha/exclui). "Retomar" desloca o início: tratado
como o mesmo lote (`cs.pausado`). Firmware: Config. > Relatórios (lista + leitor).

**Fase 49 — push (FCM).** O **próprio aparelho envia** (FCM v1 com a conta de serviço; sem Cloud Functions). PWA registra o token em
`pushTokens` (array); o aparelho lê o campo só quando há algo a avisar. Eventos: temp/umid fora/normalizada, último dia do ciclo, teste.
Alertas com **debounce de 60 s + histerese** (antes oscilavam a cada 10 s em cima do limite) e cooldown de 15 min por tipo.
Config. > Alertas no aparelho (liga/desliga eventos + teste). **Validado ponta a ponta com token falso** (mensagem chega ao FCM,
404 esperado); falta um teste com celular real. Service worker (`service-worker.js` v5) mostra as mensagens `data`.

**Fase 50 — câmera (preparação).** Arquitetura sem custo de cota: a câmera roda servidor HTTP local (`chocaiot-camera` env
`esp32-s3-cam-servidor`); o controlador a acha por mDNS, faz ping/`/foto` numa task e sobe a foto (VGA) em `camera/ultima`; PWA tem painel
"Tirar foto". Ver `chocaiot-camera/docs/INTEGRACAO.md`. **Nada testado com hardware (aguardando placa nova).**

**Fase 51 — envio de mídia PWA → aparelho + depuração por serial (2026-09-25).** Botão "Enviar imagem para o aparelho" (PWA, card Arquivos):
converte no navegador pra JPEG baseline ≤ 80 KB → `uploads/atual` + `cmd.upReq`; o aparelho lê (1 leitura), grava em **`/download/`** no SD e
responde em `uploads/atual.status` (regras: `uploads` dono lê/escreve). Validado até "grava no SD" (o teste falhou só porque o **SD estava
ausente**). Wallpaper do SD agora em **tela cheia** (cover: reduz 1/2/4/8 e recorta pelo centro). **Depuração pela serial** (COM8, 115200):
`p` = "print screen" (lê o TFT com readRect e despeja hex — `scratchpad/shot.py` converte; **precisa trocar os bytes** ao decodificar),
`s` = entra no screensaver, `x` = sai. Suspeita de "tela branca/imagem corrompida": SD com fio solto — MOSI/MISO/SCK do leitor **dividem o
barramento com o display**; sem SD detectado no boot ("[SD] NAO encontrado") verificar cabos antes de culpar o código.

**Fase 52 — Automação (2026-09-25).** Config. > Automação (firmware, 2 páginas de relés + tela de edição em 2 páginas) e card "Automação" no PWA.
Uma regra por relé: Manual · Aquecer · Resfriar · Umidificar · Desumidificar/ventilar · Periódico (N s a cada M min, sincronizado com o relógio) ·
Horário (janela hh:mm–hh:mm) + "desliga sozinho após N min" (qualquer modo). Alvo "do tipo do ciclo" (temp ideal / umidade ideal, **umidade de eclosão
nos 3 últimos dias**) ou personalizado; histerese; chaveamento mínimo de 30 s. **Padrão: desativada** (`autoAtivo=false`) e "só com ciclo em andamento".
Segurança: sensor sem leitura >2 min desliga aquecedor/umidificador; temp ≥ máxima+0,5 °C desliga o aquecedor (+push); toque manual/PWA num relé pausa
a automação dele por 5 min; desativar a automação desliga o que ela ligou. CH7 (cooler) segue a histerese do chip (não editável). Config em `/auto.cfg` e
`cmd.auto` (string `A1|ativo|soCiclo|r0..r7`, r = modo,alvoCustom,alvo10,hist10,onSeg,cadaMin,hIni,hFim,autoOffMin) — mesma serialização no PWA (`extras.js`)
e firmware (`autoSerialize/autoParse`). Logs na categoria "Automacao". Testado: sincronização PWA→aparelho via serial (com automação DESLIGADA); **os relés
não foram acionados em teste** (carga real — testar com cuidado, começando por CH4/CH5).

**Fase 53 — Segurança, notificações e histórico (2026-09-25).**
- **E-mail no aparelho:** Config. > Segurança > botão do e-mail abre o teclado de texto (`emailAbrirEdicao`), valida, grava em `emailRec` e sobe `cmd.emailRec`
  (relógio lógico `lastLocalEmailMs` evita o poll antigo desfazer). Só edita com o PIN desativado. `cmd.pinOff` (PWA) desativa o PIN remotamente.
- **PWA — bloqueio do app** (`extras.js`): celular = biometria (WebAuthn, autenticador de plataforma), desktop = PIN (SHA-256+sal em localStorage); trava na abertura,
  ao voltar após 30 s e após 5 min parado; "Esqueci/sair" limpa e faz signOut. **Toggle independente** do PIN do aparelho (que só se define no aparelho). Biometria não testada em celular real.
- **Notificações por tipo:** `pushTokens` guarda `"token|flags"` (1 energia · 2 internet · 4 faixa T/U · 8 fim de ciclo; sem `|` = tudo); o firmware só envia a quem marcou
  (`pushBitDoTag`). **Queda de energia / internet são avisos pós-fato** (tags 6/7): no boot por POWERON/BROWNOUT e quando o WiFi volta após ≥ 2 min. PWA aberto também avisa na hora que o aparelho some.
  ⚠️ Abrir a serial (COM8) **reseta o ESP** (POWERON) → dispara "queda de energia" para todos os celulares — só acontece em teste.
- **Histórico:** página dedicada (PWA `screen-historico`: 1/7/14/30 dias + período personalizado até 90 dias + CSV; firmware `S_HIST`: mesmos períodos + De/Até por passos de dias).
  Gravação **reprojetada**: SD só *append* em `/hist/AAAAMMDD.csv`; nuvem em `historico/AAAA-MM-DD.m` (mapa `sN`="t10,h10", `updateMask m.sN`) — nunca sobrescreve o dia; fila em RAM (320) publica em lotes de 12
  quando há WiFi; **SD com mau contato**: falha de escrita marca `sdOK=false` e `sdManter()` remonta a cada 30 s (histórico segue subindo pra nuvem). Serial `h` fecha o slot na hora (teste). Validado: slot publicado no mapa `m`.

**Correção (2026-09-25) — recuperação de PIN não destravava.** Causa (achada com serial + REST): o código chegava (`[PIN] codigo recebido ... ACEITO`), mas o
`segTick` só tratava a confirmação com a tela de recuperação aberta; enquanto o usuário ia ao PWA digitar o código o **screensaver entrava** (estado ≠ `S_PIN`), o
código era ignorado e, ao tocar, `exitScreensaver` voltava ao bloqueio comum. Agora: confirmação vale em qualquer estado, a recuperação **mantém a tela acordada**
(`lastActivityMs`) e `exitScreensaver` não pede o PIN de novo durante a recuperação (`pinEmRecuperacao`). Serial de teste: `r` inicia a recuperação e imprime o código.

**Fase 54 — ciclo, tipos, alertas e som (2026-09-25).**
- **Bug de datas no PWA:** `extras.js` definia `fmtDataHora(d)` e **sobrescrevia** a de mesmo nome do `app.js` (que recebe ms) → datas do wizard/ciclo saíam como "1.790.338.980.000". Renomeada para `fmtDH`. **Cuidado com funções globais de mesmo nome entre `app.js` e `extras.js`.**
- **Ovoscopia/eclosão:** o tipo ganhou `ovo1`, `ovo2` (0 = nenhuma) e `eclDia` (formato compacto agora com 16 campos; registros antigos derivam marcos: dias/3, 2·dias/3, dias−3).
  Editor: firmware ganhou a **4ª página** ("1ª/2ª ovoscopia" e "início da eclosão"); PWA, grupo "Ovoscopia e eclosão". Defaults: Galinha 7/14/18, Codorna 5/10/14.
- **Marcos na barra do ciclo (PWA e firmware):** losangos nos dias de ovoscopia/eclosão (passado = cheio com halo; **próximo pulsa**), legenda com datas, e **a cor do número do dia, do % e da barra muda a cada marco**
  (verde → ciano após a 1ª ovoscopia → laranja após a 2ª → rosa na eclosão; pausado = cinza). Tela do ciclo do firmware redesenhada: sem "<" e sem textos repetidos; **tocar acima dos botões volta ao início**.
- **Wizard, etapa 2, "Personalizado":** abre o **editor completo de tipo** (PWA: modal; firmware: editor de 4 páginas) e o tipo novo **fica salvo com os demais** e já selecionado. **Enter** no nome do lote fecha o teclado e avança (PWA); Enter em qualquer campo fecha o teclado.
- **Alertas seguem o tipo do ciclo automaticamente:** sem ciclo ativo (ou tipo desconhecido) **não há alerta** de T/U; na eclosão (a partir de `eclDia`) a faixa de umidade vira `umidEcl−5 … umidEcl+10` (firmware `aplicarLimitesDoTipo`, PWA `aplicarFaixasDoTipo`).
- **Som (PWA):** Config. > Notificações > "Som e vibração" (liga/desliga, volume, tom Suave/Alerta/Sirene, vibrar, testar). Web Push não permite som customizado: **app aberto** toca o tom escolhido (Web Audio); em segundo plano a notificação usa `silent:false` + vibração e o som segue o **canal de notificação do Android** (se estiver mudo, liberar em Configurações do celular). Preferências vão para o SW via Cache "choca-prefs".
- Tela Segurança do firmware: o texto saía em tamanho 2 (`arqBotao` deixa a fonte em 2) — corrigido. Serial de teste: `c` tela do ciclo, `C` ciclo falso local (não toca na nuvem), `g` segurança, `e` editor de tipo.

**Fase 55 — nome da chocadeira, layout mobile, ciclo flutuante e histórico por ciclo (2026-09-25).**
- **Nome da chocadeira:** PWA (Config. > Chocadeira) e firmware (Config. pág. 2 > Nome, teclado virtual). Sincroniza por `nome` (top-level, lido pelo PWA) + `cmd.nomeChoc` (lido pelo aparelho no poll rápido); relógio lógico `lastLocalNomeMs`.
- **PWA mobile:** card do Guia com o mesmo tamanho dos demais (📖 + nome dentro); o grid de cards tinha padding duplicado e ficava mais estreito que o card da câmera (`.cards{padding:0}` em ≤720px).
  **Firmware:** card Guia mostra um **livro aberto** desenhado (`drawLivroAberto`) no lugar do texto "Abrir".
- **Ciclo flutuante (PWA, ≤640px):** `#screen-ciclo` vira cartão sobre o painel com `backdrop-filter: blur` — `showScreen("ciclo")` mantém o dashboard "ativo" por baixo só no mobile; toque no fundo fecha. Se ficar bom, estender às demais telas.
- **Histórico por CICLO (PWA e firmware):** lista de ciclos (em andamento + encerrados) com filtros **Todos / Em andamento / Concluídos / Interrompidos / Excluídos**, ordenação (recentes, antigos, nome, duração no PWA) e busca; abrir um ciclo mostra os gráficos T/U do período dele.
  PWA lê `ciclos/*` + dias de `historico/*` do período; firmware guarda **`/ciclos.idx`** no SD (`iniYmd,fimYmd,sit,nome`) e desenha o gráfico a partir de `/hist/*.csv`. **"Excluído"** = Lixeira: o PWA escreve `cmd.fimTipo="lixeira|<ms>"` ao encerrar (o aparelho lê no poll) e o firmware marca `cs.excluido`;
  relatórios ganharam `situacao: "excluido"`. ⚠️ Mudei `CicloStats` (novos bytes) sem versionar: o `/cicloStats.bin` antigo foi lido desalinhado uma vez (o ciclo de teste "Teste rei" ficou com nome "e rei"/status trocado) — em mudanças futuras, versionar o arquivo.
- Serial de teste novos: `l` histórico, `k` gráfico do 1º ciclo, `m` home, `n` config. pág. 2, `i` imprime `/ciclos.idx`.

**Fase 56 — PWA: menu como cartão, Guia flutuante, Conta (2026-09-25).** Menu do topo (hambúrguer) redesenhado como cartão compacto com fundo desfocado (`body.menu-aberto::after` + blur; ligado por MutationObserver no `extras.js`):
status + "Configurações" + 3 **ícones com efeito** (Atualizar gira, Desligar pulsa, Sair desliza) — ids `btn-forcar-atualizacao/btn-desligar/btn-logout` mantidos. O e-mail saiu do menu para o card **"Conta"** em Configurações
(e-mail, e-mail de recuperação, Copiar, "Usar como recuperação", "Trocar de conta" = signOut + seletor de contas Google via `prompt: select_account`). Botão "Iniciar/Dia N" do topo escondido (`hidden`; o JS ainda o atualiza — o card Ciclo faz a mesma ação).
Guia e modais (T/U, arquivos, tipos…) também ganharam o estilo flutuante/desfocado no celular (`showScreen` mantém o dashboard "ativo" por baixo para "ciclo" e "guia" só em ≤640px).
Testes de layout: o painel embutido do Claude tem emulação de celular (`resize_window` 375×812 + screenshot) — **não precisa de smartphone por USB** (só biometria/push/som reais exigem o aparelho de verdade).

**Fase 57 — regras avançadas de automação + galeria desfocada (2026-09-25).**
- **Regras avançadas (PWA e firmware):** `SE <condição> ENTÃO <ações> SENÃO <ações>` — qualquer sensor (temperatura, umidade, temp. do chip) comanda **qualquer combinação de relés**. Condição de ligar `c1 [E|OU c2]`; **desligar por condição própria** (histerese, ex.: liga em T≥37,5 e só desliga em T≤37,3)
  ou inverso; ações por relé = nada/liga/desliga (2 bits/relé); janela horária opcional; até 8 regras. Formato: `cmd.auto` = `A1|...(8 regras por relé)|X|regra|regra` (16 campos por regra; ver `advParse/advSerializar` no firmware e `autoParse/autoSerialize` em `extras.js`).
  Segurança: nunca liga o CH1 (aquecedor) em sobretemperatura; toque manual pausa 5 min; sensor sem leitura inibe a regra e desliga aquecedor/umidificador que a automação ligou; chaveamento mínimo 30 s. As regras avançadas rodam **depois** das por relé (prevalecem se conflitarem). Firmware: Config. > Automação, **página 3/3** → editor de 6 páginas (SE · E/OU · DESLIGAR quando · ENTÃO · SENÃO · horário/estado).
- Firmware: na tela de modos do relé a leitura dos sensores sobrepunha os botões — movida pro 8º quadrado livre. PWA: galeria "Fases do pintinho" (item 11 do Guia) usa uma cópia desfocada da própria imagem no fundo em vez do preto sólido. Serial de teste: `a` regras avançadas, `d` modos do CH1, `w` editor (avança a página).

Compilação: RAM 25,5 %, Flash 30,4 %. Testado no aparelho via serial: índice do SD, poll, push até o FCM, histórico criado no SD.

---

## 7. Energia do display

**Confirmado pelo usuário (2026-09-17):** VCC e backlight do display
foram migrados do trilho de 3,3V para 5V, como descrito no checkpoint
(flicker eliminado, 5,09V estável medido). O teste de continuidade feito
nesta sessão em 16/09 (pino "LED" no 3V3 ao lado do RST) refletia a
fiação **antes** dessa migração — não é mais o estado atual. De qualquer
forma, a conclusão prática não muda: o backlight ainda não passa por
nenhum GPIO, só por um trilho de alimentação fixo (agora 5V em vez de
3,3V) — PWM de brilho continua exigindo o módulo MOSFET planejado na
seção 9 (`SIG→GPIO47`).

### O que já foi implementado como solução (funciona hoje)
Sem GPIO no backlight, dimming real (PWM) não é possível via software.
A solução implementada é o **screensaver** da seção 3: tela preta
(bloqueio de luz pixel a pixel do próprio LCD, funciona com backlight
sempre aceso) + relógio + logo flutuante, depois de inatividade
configurável. Substitui a ideia anterior de usar o comando `SLEEP` do
controlador ILI9341 (`0x10`/`0x28`), que dependeria do comportamento do
painel com o backlight sempre ligado (resultado incerto) e não permitia
seguir desenhando uma animação por cima enquanto o painel estivesse
dormindo.

---

## 8. Problema ativo — resets aleatórios (DOIS problemas confirmados por log real)

**Sintomas reportados (2026-09-20):** flickering, imagem corrompida com
artefatos, distorção visual na inicialização/rotação, tela branca após
reiniciar — junto com o padrão mais antigo de reset sozinho depois de
ficar ligado um tempo.

**✅ Diagnóstico instrumentado e JÁ CONFIRMADO** via `logResetReason()`
(`esp_reset_reason()` no início do `setup()`, logado no Serial e exibido
na tela Sistema como "Último reset:"). O usuário rodou com essa
instrumentação e capturou um log real de `pio device monitor` que
mostrou **dois problemas genuinamente distintos acontecendo na mesma
sessão**:

### 8.1 Confirmado: BROWNOUT_RST real (queda de tensão)
O log mostrou `rst:0xf (BROWNOUT_RST)` seguido de
`Motivo do ultimo reset: BROWNOUT - queda de tensao`. Não é mais
hipótese — a placa está sofrendo queda de tensão real o suficiente pra
disparar o detector de brownout do próprio chip. Suspeito nº1 continua
sendo o inrush do relé de 8 canais no mesmo trilho de 5V do display.
**Próximo passo (hardware, não testado ainda):** capacitor de
desacoplamento maior (ex: 470–1000µF eletrolítico + 100nF cerâmico) perto
do VCC do display e do ESP32, e considerar um capacitor de bulk também
na entrada do trilho de 5V que alimenta as bobinas do relé.

### 8.2 Confirmado: heap corrompido, crash dentro do driver de WiFi
O log também mostrou um crash real: `CORRUPT HEAP: multi_heap.c:96`,
`abort()`, reset por `PANIC (crash/excecao)`. Decodificado o backtrace
com `xtensa-esp32s3-elf-addr2line` contra o `firmware.elf`:

```
ppTask → sta_rx_cb → sta_input → esf_buf_recycle → free() → heap_caps_free
  → multi_heap_free → multi_heap_free_impl → assert_valid_block (CORROMPIDO AQUI)
```

`ppTask`/`sta_rx_cb`/`sta_input`/`esf_buf_recycle` são internos do driver
de WiFi (binário fechado da Espressif, sem símbolos/linha) — a
corrupção do heap **aconteceu antes, em outro lugar**; o driver de WiFi
só "tropeçou" nela ao tentar liberar um buffer de recepção qualquer.
Não dá pra apontar a linha exata do nosso código só com esse backtrace.

**Mitigação aplicada** (bem documentada como causa comum desse
exato padrão de crash — WiFi RX + heap — em projetos ESP32 com
polling HTTPS/TLS frequente): `WiFi.setSleep(false)` no lugar de
`WiFi.setSleep(true)` nos dois pontos onde conecta ao WiFi. O
dispositivo é alimentado pela rede elétrica (sem bateria), então o modem
sleep só trazia risco (rádio entra/sai de economia de energia entre
polls de 4s do Firestore) sem nenhum benefício real de consumo aqui.

**Diagnóstico adicional pra próxima ocorrência:** `heap_caps_check_integrity_all(true)`
rodando a cada 10s no `loop()` — se a corrupção acontecer de novo, o aviso
vai aparecer bem mais perto do momento real (não só quando o WiFi
"tropeçar" nela minutos depois), o que ajuda a cruzar com o que rodou
nos ~10s anteriores (poll do Firestore? heartbeat? NTP?) pra achar a
causa raiz de verdade se `WiFi.setSleep(false)` não resolver sozinho.

### 8.3 Falso positivo removido: self-check de Display ID
O `tftRespondeu()`/retry adicionado antes desta descoberta (lia o
registrador Display ID via comando `0x04` pra confirmar que o `tft.init()`
funcionou) **sempre "falhava"**, inclusive em boots claramente normais
com o display funcionando perfeitamente depois — confirmado pelo próprio
log ("Display nao respondeu... tentativa 1... tentativa 2" em toda
inicialização, sem exceção). Esse ILI9341 (breakout barato, 4 fios)
provavelmente não devolve leitura válida nesse comando. Removido: só
adicionava ~300ms de delay em todo boot e ruído enganoso no log, sem
detectar nada de real.

### 8.4 SPI a 27MHz e rotação — mantidos como estavam
`SPI_FREQUENCY` continua reduzido de 40MHz → 27MHz (`User_Setup.h`,
margem de segurança contra EMI do relé de 220V AC — ainda vale, é
independente dos dois problemas acima). Rotação (`setRotation(1)`, fixa)
**não foi mexida** — já calibrada junto com o touch e funcionando em
produção há meses; nenhum código atual troca de rotação em runtime, então
o item "distorção em modo retrato" do relato original não correspondia a
nada existente no firmware.

**Status:** as duas causas raiz têm mitigação aplicada mas **ainda não
validada em uso real prolongado** — precisa gravar na placa e observar
(minutos a horas, como o padrão original) se os resets pararam ou só
diminuíram.

---

## 9. Controle PWM planejado — backlight e fans (especificado, não comprado)

### Backlight (1 módulo MOSFET IRF520)
Corrente medida do backlight: 30mA. Fiação planejada: `SIG→GPIO47`,
`VCC→3V3`, `GND→comum`, `V+→5V`, `V-OUT→fio do LED do display`.
```cpp
analogWrite(BACKLIGHT_PIN, 230);  // operação normal
analogWrite(BACKLIGHT_PIN, 65);   // standby
analogWrite(BACKLIGHT_PIN, 0);    // desligado (mantém touch ativo)
```
Isso **substituiria** o screensaver por dimming real de 3 níveis quando
implementado — ver ponto em aberto na seção 7 antes de assumir GPIO47
como certo.

### Fans (3 módulos MOSFET IRF520, um por fan)
Controle independente é requisito — a chocadeira deve poder ajustar cada
fan individualmente (ex: reduzir a velocidade de uma fan específica).
Usa os pinos já reservados: PWM em GPIO4/5/6, tacômetro em GPIO15/16/17.

### Ideia avaliada e descartada — desligar o display via relé
Motivos: (1) precisaria de contato NC pra manter a tela ligada por
padrão em caso de falha, invertendo a lógica de segurança usada nos
outros 8 canais; (2) cortar o VCC inteiro desligaria também o touch
(XPT2046), impossibilitando "tocar pra acender de novo"; (3) cortar só
o LED via relé seria redundante com o MOSFET/PWM já planejado. Decisão
final: usar exclusivamente MOSFET/PWM pro backlight; os 2 canais de
reserva do relé continuam livres.

---

## 10. LGPD & Segurança — checklist criado, ainda não executado

Uma aba com 10 itens (lgpd1-lgpd10) foi adicionada a um checklist
interativo (`chocaiot_checklist.html`, fora deste repo): mapeamento de
dados pessoais, política de privacidade, base legal do consentimento,
exclusão de conta em cascata, Firestore Rules "nega tudo" padrão,
auditoria de segredos no GitHub, rotação de credencial, integridade de
OTA, resposta a incidentes, proteção contra abuso de cota. Nenhum item
foi revisitado desde a criação — vale retomar antes de qualquer uso fora
do ambiente de teste.

---

## 11. Pendências ativas (ordem sugerida)

1. ✅ ~~Adicionar `esp_reset_reason()` no `setup()`~~ — feito e **já
   confirmou 2 causas reais** de reset (brownout + heap corrompido no
   driver de WiFi, seção 8). Falta gravar as mitigações na placa
   (`WiFi.setSleep(false)`, `heap_caps_check_integrity_all()` periódico)
   e observar se os resets pararam
2. ✅ ~~Inicializar o SHT31 no firmware, e popular `leituras/` no
   Firestore~~ — feito na Fase 20 (seção 6, 2026-09-21). Compilado, mas
   **ainda não testado com o sensor fisicamente conectado** — falta
   ligar os 4 fios na placa e confirmar no Serial/tela
3. ⏳ Câmera OV5640 substituta chegou (2026-09-21) — próximo item pedido
   pelo usuário, depois do SHT31. Bring-up + teste térmico cuidadoso
   antes de deixar rodando sem supervisão (a 1ª unidade superaqueceu)
4. ⏳ Comprar e instalar os 4 módulos MOSFET IRF520 (1 backlight + 3 fans)
6. ⏳ Testar relé com carga real (bombas 12V primeiro, antes de 220V)
7. ⏳ Implementar e testar PWM dos 3 fans (independente)
8. ⏳ Retomar checklist de LGPD/Segurança
9. ⏳ Comprar sensores restantes: ADXL345, MH-Z19B, DS18B20, TCRT5000×2
10. ⏳ Implementar "Perfil de Incubação" e "Limites de Segurança" (tela
    "Automação" no firmware) — depende dos itens 2 e 4 acima

---

## 12. Lições aprendidas (acumulado)

- Sempre verificar a porta de upload/monitor correta — placas com
  USB-OTG + USB-UART (CH343P) precisam de `ARDUINO_USB_CDC_ON_BOOT`
  **desligada** pra Serial sair pela porta monitorada
- TFT_eSPI no ESP32-S3 exige `USE_HSPI_PORT`
- Bibliotecas de nicho (NimBLE, FirebaseClient) mudam nomes de
  classe/método entre versões — verificar exemplo oficial atual antes
  de escrever código
- Gestos de swipe não são confiáveis neste touchscreen — só toque seco
- Nunca usar append (`>>`) num arquivo sem garantir que o conteúdo
  original completo já está presente
- Componentes de câmera barata podem vir defeituosos — aquecimento
  imediato e severo é motivo pra trocar a peça, não insistir no código
- Contato NC vs NO em relé muda o comportamento de falha — NC mantém o
  circuito ativo por padrão (bom pra manter algo visível/ligado), NO
  mantém desligado por padrão (bom pra manter algo seguro/desligado)
- Antes de rotear um sinal por um relé pra "desligar algo", checar se
  esse algo compartilha alimentação com outra função que precisa ficar
  ativa (ex: touch controller e VCC do display)
- **JSON da API REST do Firestore vem pretty-printed** (espaços/quebras
  de linha entre chave e valor) — parser manual precisa tolerar isso,
  não pode buscar uma sequência exata de caracteres
- **CSS `@property` com `inherits:false` não propaga o valor pra
  pseudo-elementos** (`::before`/`::after`) mesmo quando o elemento pai
  tem o valor setado via `style.setProperty()` — precisa `inherits:true`
  quando o pseudo-elemento depende do valor do elemento real
- **`getBoundingClientRect()` serializa como `{}` vazio** via
  `JSON.stringify` (as propriedades são getters no protótipo, não
  próprias do objeto) — não é bug, é limitação de serialização, não
  indica que o elemento é `null`
- **CSS Grid com `auto-fit`/`minmax()` pode desalinhar linhas** quando o
  número de colunas que cabem varia com a largura — a última linha pode
  sobrar com menos itens sem alinhar embaixo dos de cima. Colunas fixas
  (`repeat(N, 1fr)`) evitam isso quando o layout deve ficar sempre igual
- **`Values::TimestampValue(String)` no FirebaseClient** aceita
  RFC3339 direto (`"%Y-%m-%dT%H:%M:%SZ"` via `strftime`+`gmtime`) —
  não precisa de `FieldValue.serverTimestamp()` (isso é só do SDK JS)
  pra escrever um timestamp do firmware via REST
- Testar contato elétrico "encostando com o dedo" não é confiável — só
  solda ou jacaré de teste valem como teste real; pra identificar qual
  pino é qual, multímetro em modo continuidade é o método correto
- **SPI a 40MHz é otimista pra ILI9341 em fiação de protótipo (jumper/
  dupont), ainda mais perto de um relé chaveando 220V AC** — EMI do
  chaveamento pode corromper bytes na linha, causando flicker/artefatos
  visuais que parecem aleatórios (piores quando os relés de carga alta
  comutam). 27MHz é o teto seguro mais citado pra esse driver nessas
  condições
- **`esp_reset_reason()` (via `<esp_system.h>`) devolve o motivo exato do
  último boot** (brownout, watchdog, panic, software, power-on) sem
  precisar de nenhum hardware extra — deveria ser adicionado desde o
  primeiro sintoma de reset aleatório, não como último recurso
- **`tft.readcommand8(0x04, N)` (Display ID) NÃO é confiável em todo
  módulo ILI9341** — nesta placa (breakout barato, 4 fios) sempre
  devolvia leitura "zerada" mesmo com o display funcionando normalmente
  depois. Tentado como self-check de boot e removido — nem todo clone
  implementa/expõe leitura de registrador via MISO de forma usável
- **Decodificar um backtrace de crash do ESP32 sem símbolos**: o monitor
  serial mostra só endereços hex (`Backtrace: 0x40377dea:0x3fca2cc0 ...`).
  Rodar `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <endereços>`
  (toolchain em
  `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/`, usando o PC de
  cada par, não o SP) resolve nome de função/arquivo/linha — desde que
  seja o mesmo `.elf` que estava rodando na placa (mesma sessão de
  compile+upload). Endereços em `0x420…` sem símbolo geralmente caem no
  binário fechado do driver de WiFi/PHY (sem debug info disponível)
- **Corrupção de heap descoberta dentro do driver de WiFi
  (`sta_rx_cb`/`esf_buf_recycle`/`free`) não significa que o bug está no
  WiFi** — o `assert_valid_block` só detecta a corrupção no próximo
  `free()` que tocar aquele bloco, que pode ser bem depois e em código
  completamente não relacionado à causa real. `heap_caps_check_integrity_all(true)`
  chamado periodicamente no `loop()` encurta essa janela pra diagnóstico
  futuro
- **`WiFi.setSleep(true)` (modem sleep) é suspeito documentado de
  instabilidade de heap/buffer de RX no ESP32** sob polling HTTPS/TLS
  frequente — em dispositivo alimentado pela rede elétrica (sem bateria),
  não há motivo pra correr esse risco só pra economizar energia
- **Um script que embute a chave privada do Service Account
  (`firebase_credentials.h`) pra consultar o Firestore direto é bloqueado
  pelo classificador de segurança do Claude Code** ("Credential
  Leakage"), mesmo sendo uma credencial já existente do próprio projeto
  usada só localmente/read-only. Não vale insistir tentando contornar —
  a alternativa mais simples pra ver o valor real de um campo é abrir o
  Console do Firebase manualmente (não exige nenhuma permissão especial)
- **Uma tarefa async (heartbeat, patch, get) que falha silenciosamente
  não é a mesma coisa que uma que nunca é enviada** — sem um callback
  dedicado logando sucesso (não só erro), não dá pra distinguir "está
  funcionando mas eu não tô vendo" de "parou de verdade" só olhando o
  Serial. Vale sempre logar o caminho de sucesso também, não só o de erro
- **NTP que falha uma única vez no boot, sem retry, pode travar o relógio
  do sistema no epoch (1970) pro resto da sessão** — qualquer timestamp
  gravado a partir daí (heartbeat, etc.) fica permanentemente "no
  passado", mesmo com o dispositivo funcionando perfeitamente bem depois
- **"Most vexing parse" do C++ em construtores aninhados com identificador
  solto**: `Tipo var(OutroTipo(identificador))` (ou até
  `OutroTipo(identificador)` sozinho como argumento) pode ser interpretado
  pelo compilador como **declaração de função**, não chamada de
  construtor — erro de compilação confuso ("no matching function", "note:
  candidate expects N arguments"). Acontece com `DocumentMask`/
  `Precondition` do FirebaseClient quando o argumento é uma variável nua
  em vez de uma expressão (string literal, concatenação, chamada
  aninhada). Fix: isolar em uma variável nomeada antes (`String s(var);
  Tipo obj(s);`) — nunca passar `Tipo(identificador)` cru como argumento
  de outro construtor
- **Um campo do Firestore que está no `updateMask` de um PATCH mas
  ausente do corpo (`fields`) do documento é APAGADO** — é assim que se
  deleta um campo via REST puro (FirebaseClient no firmware), sem
  precisar de `FieldValue.delete()` (que é só do SDK JS usado no PWA).
  Um `Document<Values::Value>` vazio (sem nenhum `.add()`) com mask
  cobrindo N campos apaga todos eles de uma vez
- **Bloquear uma ação por "dispositivo offline" só faz sentido se essa
  ação tiver efeito físico de verdade** — o botão de ciclo tinha sido
  bloqueado offline por padrão geral de segurança (Fase 9), mas
  iniciar/pausar/parar o ciclo é só Firestore, sem depender do ESP32
  estar alcançável; o bloqueio só criava um bug (impossível até parar um
  ciclo) sem nenhum ganho de segurança real
- **Service worker com cache-first pro app shell pode mascarar deploys
  reais indefinidamente** — ele só re-executa `install` (que repopula o
  cache) quando o PRÓPRIO arquivo `service-worker.js` muda de bytes; se
  `CACHE_VERSION` não subir, o cache antigo (de antes de qualquer fix)
  continua sendo servido pra sempre, e cada novo `firebase deploy`
  parece "não fazer nada" pra quem já tinha o app aberto — mesmo o deploy
  tendo funcionado perfeitamente no servidor. Pra um app em
  desenvolvimento ativo, network-first (com fallback pro cache só quando
  offline de verdade) é mais seguro que cache-first, mesmo custando uma
  ida à rede a mais quando online
- **Um padrão de correção (ex.: guard de "logical clock" pra corrida
  poll-vs-ação-local, Fase 9) precisa ser aplicado em TODO lugar que tem
  a mesma forma, não só onde foi descoberto primeiro** — a mesma corrida
  dos atuadores existia, idêntica, no código do ciclo (adicionado depois,
  na Fase 10/11), e só foi pega quando o sintoma apareceu de novo. Ao
  corrigir uma classe de bug, vale grep no arquivo inteiro por outros
  pollers/appliers com a mesma estrutura antes de dar por resolvido
- **Múltiplos pollers periódicos compartilhando um único cliente de rede
  (`fbClient`) competem entre si** — quanto mais tarefas assíncronas
  disputando o mesmo cliente, maior a chance de uma tarefa
  tempo-sensível (heartbeat) atrasar o suficiente pra passar de um limiar
  de "stale" em quem consome esse dado. Mitigar com: menos polls
  desnecessariamente frequentes, prioridade explícita na ordem do
  `loop()`, e margem de tolerância generosa no lado que consome (nunca
  cravar o limiar exatamente igual ao intervalo nominal)
- **⚠️ REGRA PERMANENTE (Fase 30/31) — antes de adicionar QUALQUER
  leitura nova no Firestore (poll novo no firmware, `onSnapshot` novo no
  PWA, ou qualquer coisa que reassine um listener existente), calcular
  mentalmente o impacto na cota diária GRATUITA (50 mil leituras/dia,
  20 mil escritas/dia — reseta à meia-noite Pacific Time).** O poll de
  atuadores do firmware (2,5s) sozinho já consome ~35 mil/dia — perto do
  teto, de propósito, com pouca margem sobrando. Já causou 2 incidentes
  reais nesta sessão: um "auto-refresh" no PWA que reassinava um
  `onSnapshot` de 200 docs a cada 60s (até 288 mil leituras/dia sozinho)
  estourou a cota e derrubou TODA leitura do projeto (inclusive o poll
  do firmware que lê `comandoReiniciar`/`comandoDesligar`/`atuadores`),
  fazendo os botões "pararem de funcionar" sem nenhum erro óbvio — só
  `HTTP 429` no Serial, fácil de não perceber sem olhar o log. Lição:
  (1) reassinar um `onSnapshot` relê o lote inteiro da query, não é
  grátis; (2) preferir listener ao vivo (`onSnapshot`) a
  polling/reassinatura sempre que possível — ele já empurra mudanças
  sozinho; (3) qualquer "atualizar a cada X segundos" pedido por
  usuário deve ser questionado/orçado antes de implementar, não
  implementado literalmente sem checar o custo; (4) um 429 nesta cota
  não avisa nada na tela — só aparece como "os botões pararam de
  funcionar", quase indistinguível de um bug de lógica. Sempre que um
  comando remoto (`comandoX`) parecer "não fazer nada", checar o Serial
  por `HTTP Status 429` ANTES de suspeitar de bug no código do comando
  em si.

---

## 13. Decisões de design — referência rápida

| Decisão | Motivo |
|---|---|
| ESP32-S3 (não ESP32 clássico) | Aceleração de hardware pra IA/visão computacional (ESP-DL) |
| N16R8 (16MB flash + 8MB PSRAM) | Buffer de câmera de maior resolução exige mais RAM |
| Bluetooth removido do firmware | ESP32-S3 não tem rádio Bluetooth Clássico; periféricos do usuário são Clássicos, não BLE |
| Relé ACTIVE LOW, init HIGH primeiro | Segurança — evita acionamento acidental no boot |
| Paginação por botão, não swipe | Touchscreen não detecta arrasto de forma confiável |
| Service Account (não user auth) no ESP32 | Dispositivo não pode "logar" como usuário final; Service Account ignora Security Rules |
| Polling (não listener) no firmware pra Firestore | Realtime listening não existe pra dispositivos embarcados no Firestore |
| Screensaver por software (tela preta + relógio), não sleep do ILI9341 | Backlight sem GPIO de controle confirmado; tela preta bloqueia luz pixel a pixel, funciona independente da fiação do backlight |
| Backlight via MOSFET/PWM planejado, não relé | Preserva alimentação do touch controller (compartilha VCC); PWM cobre "desligar" sem gastar canal de relé nem desgaste mecânico |
| 1 módulo MOSFET por fan (3 no total) | Controle independente é requisito — cada fan ajustado conforme sua posição/função |
| Fases isoladas antes de integrar (câmera, Firebase) | Evita múltiplas rodadas de debug confuso |
| Card único "Atuadores" no PWA (não 8 cards) | Reduz peso visual, agrupa os canais como uma seção coesa |
| Abas Sensores/Atuadores no mobile do PWA | Evita ter que rolar pra ver os atuadores em telas pequenas |
| Vínculo por Device ID (não "criar chocadeira" livre no PWA) | O documento já existe (criado junto com o firmware); deixar o PWA criar documentos novos geraria chocadeiras "fantasma" sem hardware nenhum atrás |
| Device ID = ID do documento do Firestore (não MAC gravado à parte) | Zero escrita nova no firmware — reaproveita o mesmo valor já fixo em `FIRESTORE_DOCUMENT_PATH`; copiado (não digitado à mão), então não precisa ser "bonito" |
| Bloqueio proativo de atuadores/ciclo quando offline (não só feedback de erro depois) | Bug crítico real: a escrita no Firestore tinha sucesso mesmo sem o dispositivo acessível, fazendo o app "mentir" sucesso por alguns segundos antes do erro aparecer |
| SPI do display a 27MHz (não 40MHz) | Sinal mais robusto contra EMI do relé de 220V AC no mesmo gabinete — bate com o padrão de flicker/artefatos relatado |
| `esp_reset_reason()` logado (Serial + tela Sistema) desde o início do `setup()` | Diagnóstico de reset aleatório exige saber o motivo (brownout vs. watchdog vs. panic) — sem isso é só hipótese. Já provou o valor: achou 2 causas reais no primeiro log |
| `WiFi.setSleep(false)` (não `true`) | Dispositivo é alimentado pela rede elétrica, sem bateria — modem sleep só tinha risco (suspeito documentado de instabilidade de heap/RX no ESP32 sob polling HTTPS) sem nenhum benefício de consumo aqui |
| Self-check de Display ID via `tft.readcommand8(0x04,…)` — tentado e removido | Sempre "falhava" nesse módulo ILI9341 específico mesmo com o display funcionando normalmente depois; só adicionava delay e ruído no log sem detectar nada real |
| "Logical clock" por canal (`lastLocalTouchMs` vs. `fbPollIssuedAtMs`), não travar toque durante poll | Resolve a corrida sem inventar um lock/mutex — só compara "esta leitura é mais velha que o último toque deste canal?" |
| Confirmação de 220V só ao LIGAR, só no PWA (não no firmware) | Desligar é sempre seguro; a tela física já exige presença no local, então não tem o mesmo risco que o controle remoto |
| Watchdogs de WiFi (10s) e NTP (30s) no `loop()`, não só no boot | Sem eles, um rádio que cai sozinho ou um NTP que falha uma vez travava o heartbeat pra sempre — dispositivo continua funcionando localmente mas o PWA nunca mais sabe disso |

---

*Este arquivo é carregado automaticamente pelo Claude Code no início de
sessões neste repositório. Mantenha-o atualizado conforme o projeto
evolui — é a fonte de verdade mais rápida de reconstruir depois de uma
pausa longa.*

**Fase 58 — correções da automação (2026-09-25).** (1) **Bug PWA:** `extras.js` chamava `advRender()` (via `autoRender()` no carregamento) antes de `const advLista` ser avaliada (TDZ) → o arquivo inteiro parava e o botão "+ Nova regra avançada" nunca ganhava listener; agora o elemento é buscado dentro da função. (2) **"?" de ajuda** em cada campo/função da Automação (PWA): cartão flutuante com fundo desfocado, toque em qualquer lugar fecha (`AJUDA`, `ajudaBtn` em `extras.js`). (3) **Firmware:** o CH1 ligava a 36,6 °C porque a regra POR RELÉ padrão do CH1 é "Aquecer" (termostato: liga ABAIXO do alvo−histerese) — não era a regra avançada. Agora um relé citado por regra avançada ATIVA ignora a regra por relé dele (`advUsaRele`), e "ENTÃO liga" sem ação no SENÃO **desliga ao inativar** (a menos que outra regra ativa o queira ligado).

**Fase 59 — CO2 e Viragem na automação (2026-09-25).** (1) **CO2** virou o 4º sensor das regras avançadas (`s=3`, valor em **ppm direto**, não em décimos; `advRef`). Não há sensor ainda: `co2Atual`/`lastCo2OkMs` (main.cpp) ficam sem leitura e a regra que usa CO2 fica inibida — ao instalar o MH-Z19B basta preencher essas duas variáveis. (2) **Viragem** = modo 7 (`AM_VIRAR`) da regra por relé: pulso de `onSeg` s (padrão 30) N vezes/dia a partir de `viraHH:viraMM`, até o dia `viraAte`, tudo do **tipo de ovo** do ciclo ativo (sem ciclo/tipo: fica desligado). Info dos sensores da tela de modos foi para a linha de baixo (o 8º quadrado agora é Viragem). PWA: mesmos itens + "?" de ajuda. Backup: `main_pre_fase58.bk`. Nada disso foi testado acionando relé/motor.

**Fase 60 — offline (2026-09-25).** Com o WiFi fora a tela parava de responder por 4–8 s (o cliente async do Firebase tentava conectar de forma bloqueante): `fbApp.loop()` agora só roda com WiFi conectado (medido com o log `[LOOP] travado N ms` e a serial `o`, que simula WiFi fora). **Ciclo funciona sem internet**: iniciar/pausar/retomar/encerrar valem na hora e ficam `cicloSyncPend`; ao voltar a conexão `cicloSyncTick()` reenvia o estado inteiro. Poll de atuadores 2,5 s → **4 s** (`FB_POLL_INTERVAL_MS`): o serial mostrou HTTP 429 (cota diária de leituras) de novo em 2026-09-25 — com PWA + listeners + testes o consumo passou dos 50 mil/dia.

**Fase 61 — WiFi redesenhado (2026-09-25).** Tocar no ícone de conexão da home abre a lista de redes (`wifiFromHome`: "<" volta à home). Detalhes em **tela cheia**: dados da rede + **QR da senha** (`WIFI:T:WPA;S:..;P:..;;`, lib `ricmoo/QRCode`; decodificado com OpenCV na captura) + botões-ícone com efeito (Mostrar/Ocultar senha e QR, Desconectar, Esquecer). **Desconectar** só desliga (rede continua salva; `wifiManualOff` impede o watchdog de reconectar) e fica na lista; antes usava `WiFi.disconnect(true)` (rádio desligado) e o watchdog tentava reconectar, deixando o aparelho sem resposta. Tocar numa rede salva desconectada reconecta sem pedir senha. Conexão: animação de arcos, toque cancela, ✓/✗ animados e volta à lista (não à home). Serial de teste: `q`/`Q` detalhes (com/sem senha), `y` reconecta, `o` simula WiFi fora.

**Fase 62/63 — Configurações em grade + economia de cota (2026-09-26).** (62) Config. em tela cheia, grade 3×4 de ícones com 1 palavra (pág. 1: WiFi Data Atuadores Tela Audio Memoria Sistema Logs Alertas Automacao Ovos Arquivos; pág. 2: Seguranca Relatorios Nome Historico), sem "Voltar"/rodapé: faixa fina no topo — casinha/título volta ao início, "1/2 >" troca de página (`handleSettingsTap`, `SET_PAG0/1`). Serial `n`/`N` = pág. 1/2.
(63) Painel do Firebase mostrou **52 mil leituras/dia** (limite 50 mil) e 4,9 mil escritas. Medidas: poll inteligente — o PWA aberto grava `cmd.vivo` (string que muda, a cada 5 min e a cada comando); o aparelho faz poll a **3 s** enquanto isso (8 min de validade) e **15 s** sem app aberto (`fbPollIntervaloMs`); poll do ciclo 30 s / 120 s. Heartbeat 30→60 s e agora leva `tempAtual/umidAtual` (PWA lê os cards do próprio documento; listener de `leituras/` removido); push de `leituras/` 60 s→5 min (gráfico do dia); log periódico de sensor 5→30 min. PWA: sem `setInterval` de 45 s (era ~1,9 mil leituras/dia por app aberto), listeners soltos após 90 s em segundo plano, Logs só abre com Configurações e busca 60 docs (era 200). Batch write **não** reduz cobrança (cada documento conta); o ganho real foi fundir escritas (heartbeat+leitura).

**Fase 64 — padrão visual novo (2026-09-26).** Helpers `uiTopo/uiVoltar/uiCard` (faixa de 18 px no topo: seta + título; **voltar = tocar na faixa, lado esquerdo**; sem botão "Voltar" nem rodapé; cartões como em "Arquivos"). Telas refeitas: **Memória** (3 páginas: ao vivo · ARQUIVOS DO SISTEMA em âmbar = firmware + LittleFS com cadeado · ARQUIVOS DO USUÁRIO em azul = SD + botão "Abrir arquivos"), **Sistema** (crédito "Erick - ErDigital2022"), **Logs** (chips Data/Tipo; hora LOCAL; páginas em "< n/N >" no topo ou tocando nos lados; lupa limpa o filtro de texto), **Alertas** (botão Testar pequeno), **Segurança** (agora contém o Nome), **Data/Hora, Atuadores, Áudio**. Auditoria de timestamp: o log gravava UTC (mostrado 3 h adiantado) e "sem-hora" antes do NTP → agora exibe hora local e, sem NTP, grava com o RTC marcado "L" (`logEvento`, `logParse`). Serial de teste: `1..3` Memória, `4` Sistema, `5` Logs, `6` Alertas, `7` Segurança, `8` Data/Hora, `9` Atuadores, `0` Áudio. Backup: `main_pre_fase64.bk`. Faltam no padrão novo: Tela, Ovos, Arquivos, Relatórios, Histórico, Automação, Guia.

**Fase 65 — visão computacional leve, sem modelo treinado (2026-09-26).** Pedido original era um sistema autônomo completo (contagem/classificação de ovos por TensorFlow Lite Micro, máquina de estados de fases, auto-ajuste de temp/umidade pela imagem). Escopo combinado com o usuário antes de implementar (câmera ainda sem placa substituta confirmada; sem fotos reais para treinar/calibrar qualquer modelo):
- **Sem modelo treinado.** Heurística: diferença de brilho (luma) entre o quadro atual e o anterior, numa grade 80×60 (`camVisAnalisar`, `TJpg_Decoder` já usado nos wallpapers/galeria, escala 8 do JPEG VGA). Dá "movimento" (0 = quadros idênticos) e "brilho médio" — **não conta nem classifica ovos**. Contagem de ovos exposta como "indisponível" nos dois lados, com nota explícita de que precisa de fotos reais para calibrar.
- **Só alerta/loga — nunca ajusta atuador.** Dentro da janela de eclosão (`faseCicloAtual()`, rótulo derivado do dia do ciclo vs. `eclDia` do tipo — não muda nenhuma automação existente): "sem movimento há 6h" e "variação grande entre fotos (possível eclosão/quebra)", sempre marcados como estimativa. Categoria de log/push nova "Visao" (tag 8/9 → bit 16 de preferência, **desligado por padrão** — checkbox própria em Notificações do PWA).
- **Captura periódica (timelapse):** 30 min em incubação normal, 10 min nos 2 últimos dias, 5 min na janela de eclosão; 0 (só sob pedido) sem ciclo ativo ou com a captura automática desligada. Toggle "Captura automática" na tela Câmera do firmware e no card do PWA (`cmd.camAuto` / campo `camAutoAtivo`, mesmo padrão lógico do nome da chocadeira). Fotos vão para `/timelapse/AAAAMMDD_HHMMSS.jpg` no SD do controlador (reaproveitando o navegador de Arquivos já existente — nenhuma galeria nova no PWA); limpa o arquivo mais antigo se o SD passar de 90% de uso.
- **Custo de Firestore:** cada captura (manual ou automática) grava o mesmo doc `camera/ultima` de sempre, só com 4 campos a mais (`camMov`, `camBrilho`, `camFase`, `camSemMovH`) — nenhuma leitura nova; escritas na pior hipótese (eclosão, captura a cada 5 min) ficam bem abaixo da cota gratuita.
- **Fallback:** se a câmera não responder, `cameraServico()` simplesmente não dispara a captura automática nem a análise — sensores e automação (Fase 43/52/57/59) continuam intactos, como já era.
- Serial de teste: `v` roda a análise + gravação no SD usando o **wallpaper embutido como substituto de foto** (só valida o cano decode→diff→SD; não testa detecção real, que não existe). `z` abre a tela Câmera.
**Status:** compilado, gravado (COM8) e testado só com essa foto substituta (pipeline não trava, grava no SD, publica os campos). PWA implantado. **Nada disso foi testado com a câmera real** (placa ainda sem confirmação de funcionamento).

**Fase 66 — vídeo ao vivo (streaming MJPEG) + reorganização das pastas da câmera (2026-09-27).** Pedido: câmera ao vivo no PWA e no aparelho, sem custo de Firebase. Câmera ainda sem hardware confirmado (usuário segue sem a placa em mãos) — tudo abaixo segue o mesmo padrão de honestidade das fases anteriores: compilado, mas **não testado numa câmera real**.
- **`chocaiot-camera` (env `esp32-s3-cam-servidor`):** novo `WebServer` na **porta 81**, `/stream` — MJPEG multipart (`x-mixed-replace`), handler bloqueante que envia quadro a quadro até o cliente desconectar, o chip esquentar (mesmo limite do `/foto`, 75 °C) ou passar **5 min** (teto duro — precisa reabrir depois; defesa extra contra o superaquecimento já visto uma vez neste projeto, ver §2.2). `cameraEmUso` impede `/foto` e `/stream` de brigar pelo sensor. `Access-Control-Allow-Origin: *` no stream, pra o PWA poder ler pixels num `<canvas>` (detecção de movimento). Ver `docs/INTEGRACAO.md`.
- **O vídeo nunca passa pela nuvem/Firebase.** O controlador só publica `cameraIP` (string, resolvida por mDNS, mesmo mecanismo que já existia pra `/status`/`/foto`) junto de `cameraOnline`; o PWA monta `http://<cameraIP>:81/stream` sozinho. **Só funciona com o celular na mesma rede Wi-Fi/local da câmera** — fora de casa o vídeo fica inacessível (é a arquitetura, não bug); "Tirar foto" continua funcionando de qualquer lugar.
- **"Ao vivo" local no aparelho** (tela Camera, novo toggle): enquanto ligado E a tela aberta, pede 1 quadro a cada 5 s (reaproveitando o mesmo `camReqPend`/`camFotoBuf` do resto da câmera, origem `"live"`) e decodifica **em tela cheia** (`TJpgDec` escala 2: VGA 640×480 → 320×240, exato). Esses quadros **nunca** vão pra nuvem nem pro SD (sem `cameraEnviarResultado`/`camSalvarFoto`) — só decodificam e descartam. Sair da tela desliga o "ao vivo" sozinho (mesma disciplina de "nunca deixar a câmera ligada sem supervisão"). Testado só com o **wallpaper embutido como substituto de foto** (serial `V`) — valida o decode em tela cheia + redesenho do cabeçalho/rodapé por cima, não a câmera de verdade.
- **PWA:** `<img>` apontando pro stream (com `crossorigin="anonymous"`), badge "AO VIVO", botão "Capturar" (baixa o quadro atual pro celular, 100% local, `<canvas>.toBlob`), detecção de movimento local (mesma heurística de diferença de brilho da Fase 65, agora em JS/canvas, só decorativa/estimativa), auto-desliga após **2 min sem interação** no card ou ao esconder a aba, e um link **"Abrir numa aba nova"** sempre visível — página HTTPS embutindo um recurso HTTP (mixed content) pode ser bloqueada por alguns navegadores mesmo sendo "passivo"; abrir como página própria não sofre essa restrição. `cmd.camStreamPing` avisa o controlador (1 escrita, sem imagem nenhuma) quando o app abre o vídeo, só pra aparecer no log ("Stream ao vivo acessado pelo app").
- **Timelapse simplificado:** virou fixo, **5 min sempre que há ciclo ativo** (substitui a escala por fase 30/10/5 min da Fase 65 — pedido explícito desta rodada).
- **Pastas do SD reorganizadas** (`/cam/timelapse`, `/cam/manual`, `/cam/alertas` — era só `/timelapse`): timelapse automático, "Tirar foto" (aparelho ou app) e o quadro que disparou um aviso de visão (Fase 65), respectivamente. Continuam navegáveis pelo Arquivos existente (Fase 41), sem componente novo.
- **Testado:** os dois projetos compilam (`esp32-s3-cam-servidor` só compila — sem placa pra gravar; `esp32-s3-controller` compilado e **gravado na COM8**). PWA testado no browser pane (toggle liga/desliga, botões, erro de rede tratado, mobile 375px sem sobreposição) e implantado. **Nada testado com a câmera real** — MJPEG, CORS, temperatura em stream contínuo, tudo isso só quando a placa chegar.

**Fase 67 — detecção de SD mais precisa, animações de desligar/reiniciar e miniatura no card Guia (2026-09-27).**
- **SD:** antes só descobria a remoção de forma reativa (quando uma escrita programada — histórico/log — calhava de falhar, o que podia demorar minutos sem detectar nada). Agora, com o cartão "OK", um teste ativo (`SD.open("/")`, barato) roda a cada 8 s mesmo sem ninguém tentando escrever nada — detecta a remoção rápido e de propósito. Ao detectar: log (`categoria="SD"`, vai pro Firestore), push (`tag 10`, sem checkbox própria — mesmo tratamento de "teste"/eventos default) e um aviso na tela (`arqToast` + `redrawCurrentScreen()`, restaura a tela de onde saiu). Mesmo aviso ao reconectar. **Não testado com remoção física real** — só a lógica/compilação; puxar o cartão de verdade com o aparelho ligado é o teste que falta.
- **Animações de Desligar/Reiniciar:** trocado o texto estático por efeitos com mais fluidez, reaproveitando `drawArc`/interpolação de cor (RGB565) já usada nos ícones — sem framebuffer/alpha real, cada quadro redesenha a forma inteira numa cor interpolada. **Desligar:** anel de energia "encolhendo/apagando" (16 quadros) → ícone de energia → colapso estilo "TV antiga desligando" (barras fecham até uma linha, que encolhe a um ponto) antes do `esp_deep_sleep_start()`. **Reiniciar** (local e remoto, mesma função `efeitoSpinnerReiniciar`): seta girando (20 quadros) + pulso de confirmação antes do `ESP.restart()`. Serial de teste `D`/`R` rodam só a animação, sem dormir/reiniciar de verdade — confirmado que rodam do início ao fim sem travar (log `[TESTE] animacao ... concluida`), mas **não dá pra tirar print no meio da animação** (o comando de print-screen só é lido pelo `loop()` depois que o `delay()` da animação termina — limitação do jeito como o debug por serial funciona, não um bug da animação em si).
- **Card "Guia" (home):** o ícone de livro aberto (`drawLivroAberto`, Fase 55) virou uma **miniatura da última imagem da galeria "Fases do Pintinho"** (dia 21, o pintinho já nascido — `FASES[FASES_N-1]`, decodificada em escala 8 direto pro card, ~40×30 px com moldura verde). Confirmado por captura de tela real.
**Status:** compilado e gravado na COM8. SD e miniatura conferidos (miniatura por captura real; SD só por leitura de código — falta puxar o cartão de verdade). Animações confirmadas rodando sem erro, mas sem confirmação visual quadro a quadro.

**Fase 68 — cargas reais ligadas: CH1 resistência, CH2 umidificador, CH8 Fans (2026-10-03).** Usuário conectou fisicamente a resistência (CH1), o umidificador (CH2) e as ventoinhas (CH8, antes "Reserva 2"). Só renomeação: CH8 agora é **"Fans"** (12V DC — tensão assumida, ajustar `RELAY_VOLTAGE` se for outra) em `RELAY_NAMES`/`RELAY_VOLTAGE`/`advChips` (firmware) e `app.js` (PWA, `CACHE_VERSION` v11). Nenhuma regra de automação nova: CH8 segue **Manual** por padrão (sem modo "sempre ligado" — `AM_HORARIO` com hIni==hFim nunca liga); a automação continua **desativada por padrão** (`autoAtivo=false`) e precisa ser ligada em Configurações > Automação antes do ciclo de teste. Pendente de decisão: intertravamento "aquecedor só com Fans ligados". Primeiro teste com carga real — nada acionado ainda por mim.

**Fase 69 — temperatura de eclosão, "ambiente pronto" e automação junto com o ciclo (2026-10-03).** Primeiro ciclo de teste com carga real (CH1 resistência, CH2 umidificador). Achados: (a) o ciclo de teste do usuário estava **pausado** e a automação **desligada** (por isso "nada aconteceu": o ciclo sozinho não comanda relé nenhum — quem comanda é a Automação, que só roda com a chave ligada); (b) a temperatura ficava em ~35 °C e o ciclo contava dias mesmo com o ambiente fora da faixa.
- **Tipo de ovo ganhou `tempEcl`** (temperatura ideal a partir do dia `eclDia`): 17º campo do formato compacto (`...;eclDia;tempEcl`; registros antigos de 16 campos assumem `tempEcl = temp`, ou seja, **nada muda** até o usuário editar). Editor do PWA e do aparelho: grupo/pág. "Ovoscopia e eclosão" agora tem **Temp. e Umid. na eclosão** (a umidade saiu do topo/pág. 2). Na eclosão a **faixa de alerta de temperatura desloca** junto (mesma largura min/max, `aplicarLimitesDoTipo` / `aplicarFaixasDoTipo`) e o **alvo da automação "do tipo do ciclo"** usa `tempEcl`. ⚠️ Corrigido de passagem: o alvo de **umidade** da automação usava "3 últimos dias" fixos enquanto os alertas usavam `eclDia` — agora ambos usam `eclDia`. Alvo "Personalizado" (como o CH1 do usuário, 37,5) **ignora** o tipo, inclusive na eclosão.
- **`ambienteTick()` (aparelho, a cada 2 s):** ciclo NOVO (início nas últimas 12 h, nunca visto — estado em `/amb.cfg` = `epochInicio|pronto`): (1) se a automação estava desligada, **liga junto** (`autoSoCiclo=true`) e põe CH1/CH2 em "automático, do tipo" **só se estavam em Manual**; (2) push/log/toast "NÃO coloque os ovos ainda" (tag 11); (3) após temperatura **e** umidade dentro da faixa do tipo por **10 min seguidos** → push/log/toast "ambiente pronto" (tag 12); (4) se com 15 min e com 1 h ainda não estabilizou, push com o provável motivo (sensor sem leitura / automação desligada / temp baixa-alta / umidade baixa-alta, tag 13). Lote retroativo (início > 12 h atrás) nunca passa por isso. "Retomar" depois de pausa desloca o início, mas **não** reinicia o aviso (`ambEpoch != 0` = mesmo lote; só zera quando o ciclo termina e o poll já chegou). A contagem de dias **continua do horário de início declarado** (não é deslocada ao ficar pronto).
- **Estado visível:** card Status da home do aparelho vira "Aguarde / sem ovos ainda" (âmbar); o aparelho publica `ambiente` ("aguardando"/"pronto"/"") no heartbeat (zero escritas/leituras novas) e o PWA mostra "AGUARDE" + legenda no card Status. Resumo do wizard (PWA e aparelho) avisa para só colocar os ovos após o "ambiente pronto" e mostra a temp/umid de eclosão.
- **Testado:** compila; gravado (COM8); PWA em produção (v12) e verificado no browser (modal com 375 px, serialização com valor padrão para registro antigo, card Aguarde). **Não testado:** o fluxo completo do `ambienteTick` (o ciclo do usuário estava pausado — só roda ao retomar/criar ciclo novo), editor do aparelho pág. 4, card Status do aparelho em "Aguarde". Backups: `main_pre_fase69.bk`, `*.pre69.bk` no frontend.

**Fase 70 — Fans automáticas, aquecedor com antecipação, umidificador com histerese de 1% (2026-10-03).** Feedback do primeiro ciclo real: Fans (CH8) não ligavam, a temperatura passou de 40 °C mesmo com o CH1 desligando no alvo (inércia térmica da resistência de 200 W) e o umidificador ficava parado a até 3 pontos do ideal.
- **Novo modo `AM_CIRCULAR` (=8, "Circular")**: o relé fica ligado enquanto a automação estiver ativa. Padrão do CH8 (firmware e PWA), e o auto-ligar da Fase 69 também o aplica. Tela de modos do aparelho passou a 9 botões (passo 36 px, altura 32).
- **Aquecedor (AM_AQUECER):** desliga pela temperatura **prevista** = atual + inclinação (°C/min, janela de ~3 min, `tempSlopeAtualizar`) × `AQ_ANTECIPA_MIN` (3 min), além de atual ≥ alvo. E **não liga sem as Fans** (se existir um relé em modo Circular e ele estiver desligado, o CH1 espera — "aguardando as Fans" no log). Mitigação, **não cura**: a causa é o excesso de potência/falta de troca de ar; o sobretemperatura (máx+0,5 °C) continua desligando o CH1, mas nada *resfria* a câmara além do ar ambiente (a Bomba de ar CH4 não está ligada a nenhuma regra de temperatura).
- **Umidificador:** histerese padrão 3% → **1%** (liga abaixo de ideal−1, desliga ao atingir o ideal), no firmware, no PWA e no auto-ligar. **Migração única** (`migracao70`, marcador `/mig70.flag`) da config já salva: Fans Manual → Circular e hist 3% → 1% (só se o alvo for "do tipo"); registrada no log.
- **Validado no aparelho (serial):** migração rodou, Fans → ON, Umidificador ON e OFF ao atingir o ideal. **Não validado:** o efeito da antecipação sobre o overshoot (precisa de uma subida de temperatura real, observar nos próximos minutos/horas) e o intertravamento CH1×Fans. Backups: `main_pre_fase70.bk`, `extras.js.pre70.bk`. PWA `CACHE_VERSION` v13.

**Fase 71 — controle por pulsos adaptativo (CH1/CH2) e card Status explicativo (2026-10-03).**
- **Pulsos adaptativos** (`autoPulsoTick`, substitui o termostato liga/desliga dos modos Aquecer e Umidificar): longe do alvo (>3 °C / >12 %) fica ligado direto; perto, dá um **pulso** de N s e **espera o efeito** no sensor (soak: aquecedor 90 s, umidificador 45 s; 20 s se ainda falta muito), mede quanto subiu naquele pulso e **aprende o ganho** do recipiente (unidade/s ligado; média móvel 0,6/0,4; inicial 0,010 °C/s e 0,05 %/s, primeiros 2 pulsos limitados a 20 s/15 s). Próximo pulso = metade do que falta ÷ ganho (5–90 s aquecedor, 5–60 s umidificador). Corta o pulso na hora se a **previsão** (valor + inclinação × 3 min) passar do alvo; não inicia pulso se a previsão já passa; aquecedor só com as Fans (modo Circular) ligadas. Logs de pulso vão **só para o SD**, e a cada 15 min sai um resumo na nuvem ("N pulsos, ganho aprendido") — as escritas de estado do relé (`pushActuatorToFirestore`) continuam a cada toggle, necessárias porque o poll aplica o estado da nuvem nos relés (quota: ~2 escritas/pulso, cada pulso ≥ ~2-3 min). Ganho aprendido fica só em RAM (reaprende em ~3 pulsos após reboot). Os modos Resfriar/Desumidificar seguem liga/desliga normal.
- **"Ambiente pronto" agora em 5 min** (era 10) e com **tolerância**: sair da faixa por ≤ 1 min não zera a contagem (`AMB_FORA_TOL_MS`). O heartbeat publica `ambiente = "aguardando;<seg estáveis>;<seg necessários>"` (sem escrita extra). Na Fase 69 o relógio zerava a cada leitura fora da faixa mín–máx **do tipo** (37,2–37,8 °C / 45–55 %), por isso "ficou verde e o status não mudou".
- **Card Status é clicável** (PWA: janela com ✓/✗ de temperatura, umidade, automação e "estável há X de 5 min"; aparelho: toast "Ajustando: estável X de 5 min"). `AJUDA.statusAmb` é montado dinamicamente em `extras.js`.
- **Validado:** compila e grava; pulso real visto na serial (CH1 ON "pulso de 12s" → OFF "fim do pulso"); janela do Status no navegador. **Não validado:** o aprendizado do ganho e a manutenção da temperatura ao longo de horas (observar o resumo de 15 min no log). Conhecido: após reboot os avisos de 15 min/1 h do `ambienteTick` podem repetir. Backups: `main_pre_fase71.bk`, `*.pre71.bk`. PWA `CACHE_VERSION` v14.

**Fase 72 — controle por pulsos reescalado para caixa pequena (2026-10-03).** Descoberta: o ambiente de teste é uma **caixa de isopor ~10×20 cm com 1 resistência de 100 W e 1 fan** (~3 L; o regime precisa de ~5–10 W → a resistência tem ~10× a potência necessária). A Fase 71 foi dimensionada para uma câmara maior (pulso mínimo 5 s, espera de 90 s, previsão de 3 min — com `prev ≥ alvo` bloqueando pulsos quase sempre) e não conseguiria segurar a faixa 5 min seguidos. Não deu para ler o log: a COM8 parou de enumerar (placa desconectada do USB; `pio device list` não mostra o CH343).
- **Escala automática:** pulso mínimo 0,5 s (aquecedor) / 2 s (umidificador), máximo 3 s/15 s nos 2 primeiros pulsos e 60 s depois; se o pulso não produz subida mensurável (< 0,04 °C / 0,4 %), o ganho cai pela metade (busca geométrica para cima). **Aprende também o LAG** (tempo entre desligar e o pico no sensor; inicial 20 s/45 s): a espera após o pulso termina assim que o pico passa (ou em 2×lag) e a previsão é `valor + inclinação × lag` (antes: 3 min fixos).
- **Sensor rápido:** `automacaoTick` roda a **1 s** (era 2 s) e lê o SHT31 a 1 Hz só para o controle (`ctlT/ctlU`); o resto do app segue com a leitura de 5 s. `pulsoFimRapido()` no `loop()` encerra o pulso com precisão de ms. Trava: aquecimento contínuo no máximo 8 min direto.
- **Nuvem:** sem escrita por pulso. O PWA/Firestore vê só a **sessão** (relé "ligado" enquanto houver pulso nos últimos 60 s: `pulsoSyncNuvem`); `applyActuatorsFromFirestore` ignora o valor que o próprio controle escreveu (`pulsoGerencia/pulsoNuvemVal`) e só aceita mudança feita pelo usuário (que pausa a automação do canal por 5 min, como antes). Pulsos individuais: só SD; resumo a cada 15 min na nuvem (pulsos, ganho, lag).
- **Estado:** compilado (RAM 31,6 %, Flash 31,9 %); **NÃO gravado** (COM8 indisponível). A placa segue com a Fase 71 até alguém reconectar o USB e rodar `pio run -e esp32-s3-controller -t upload`. Backup: `main_pre_fase72.bk`. Hardware: resistência de 100 W é grande demais para a caixa — recomendado ~15–25 W ou SSR; pulsos de 0,5 s aumentam o ciclo do relé mecânico (estimativa ≤ ~6 mil acionamentos/dia no pior caso).

**Fase 73 — aprendizado que muda de método sozinho (2026-10-03).** Pedido: o sistema deve alterar automaticamente os métodos de aprendizagem (a caixa de isopor é só um teste).
- **Agressividade auto-ajustável** (`PulsoCtl.agr`, 0,12–0,9, inicial 0,5): fração do que falta que cada pulso tenta cobrir. Se o pico passa do alvo (> 0,15 °C / 2 %) → ×0,75 (mais cauteloso); se o pulso não foi cortado e não chegou nem na banda → ×1,2. Cada mudança vira linha no log do SD ("ajuste automatico da agressividade a -> b").
- **Reaprendizado rápido:** se o ganho medido for > 2,5× ou < 0,4× o ganho aprendido (caixa/resistência trocada, tampa aberta), o ganho é substituído de uma vez em vez de convergir pela média móvel. Teto do pulso passa a ser `lag × 1,5` (60–600 s): sistemas lentos/grandes ganham pulsos longos sozinhos.
- **Memória:** `/pulso.cfg` (LittleFS) guarda ganho, lag e agressividade por canal (a cada ≤ 5 min, se mudou); no boot recarrega com `n=1` — os 2 primeiros pulsos continuam curtos (3 s/15 s) para revalidar o modelo antes de confiar nele.
- **Estado:** compila (RAM 31,6 %, Flash 31,9 %). **NÃO gravado:** a COM8 enumera (CH343 "OK") mas não abre ("Um dispositivo conectado ao sistema não está funcionando") — falha de USB/cabo; reiniciar o driver exige admin (não tentei contornar). A placa roda a Fase 71 (pulso mínimo 5 s, previsão 3 min) até regravar. Backups: `main_pre_fase72.bk`, `main_pre_fase73.bk`.
**Fase 73 — atualização (2026-10-04, 00:2x UTC):** a COM8 voltou a abrir após o usuário religar o cabo; Fase 73 **gravada**. Observado na serial: umidificador "pulso de 15.0s" cortado por "alvo previsto" → "ajuste automatico da agressividade 0.50 -> 0.38 (passou do alvo)" (auto-ajuste funcionando); sobretemperatura 38,5 °C (herdada da versão antiga) desligou o CH1; depois "Resistencia -> ON (pulso de 0.5s)" → OFF em 1 s. **Ainda não confirmado:** o status chegar a "pronto" e a estabilidade a longo prazo. ⚠️ A COM8 voltou a falhar ~7 min depois ("dispositivo não está funcionando") — suspeita de EMI do chaveamento do relé/resistência no cabo USB (a placa é alimentada pelo DC-DC, o USB só serve para gravar/depurar).

**Fase 74 — SD instável, aprendizado do pulso com tempo morto grande (2026-10-04).** Contexto de teste: caixa de isopor ~10×20 cm, resistência de **100 W (lâmpada cerâmica infravermelha)** num extremo, sensor SHT31 no outro, pote com umidificador e 1 fan no meio.
- **Bug achado nos logs:** com o sensor longe da resistência o calor demora a chegar (lag de minutos). A Fase 73 concluía "não subiu" cedo demais (janela encurtada quando "faltava muito"), halvava o ganho a cada tentativa até pulsos de 60 s → **sobretemperatura 38,3–38,7 °C** duas vezes. Correções: (1) janela de aprendizado nunca encurtada; sem resposta → `lagS ×1,5`, `k ×0,7`, `n=1`, `fails++` (teto de pulso não validado cresce ×1,6 por falha, máx. 12 s com `fails<6`); (2) **SÉRIE de pulsos** (`serie, x0Base, onAcum`): enquanto o calor de pulsos anteriores ainda não chegou ao sensor, a subida não é atribuída ao último pulso — o ganho só é calculado quando a resposta aparece e depois do **pico**, como `(subida total)/(segundos ligados totais)`; enquanto sobe, espera até 2,5× a janela; (3) auto-ajuste da agressividade só com resposta medida; (4) umidificador: soak mínimo 25 s (antes pulsava 2 s a cada ~10 s por ruído do sensor); (5) arquivo de aprendizado renomeado para `/pulso2.cfg` (descarta valores corrompidos).
- **SD com mau contato:** `sdManter` só declara "removido" após **3 falhas seguidas** (rechecando a cada 3 s) e só o 1º aviso de cada **15 min** vira push/toast/log na nuvem (quedas seguintes: log só no SD; reconexão idem quando instável).
- **Estado:** gravado. Observado: após a correção, pulsos do CH1 crescem em série (1 s → 4,8 s → 7,7 s) com espera entre eles. Ainda não confirmado que a temperatura estabiliza na faixa. Backups: `main_pre_fase74.bk`, `main_pre_fase74b.bk`. Recomendação de hardware mantida: resistência menor (15–25 W) ou SSR, e sensor mais perto/dentro do fluxo da fan.

**Fase 75 — "o firmware desliga sozinho": comandoDesligar antigo na nuvem (2026-10-04).** Diagnóstico inicial ERRADO (retratado): atribuí os reinícios a uma queda de energia ao ligar o CH1. A tela Logs do aparelho (serial `5`) mostrou a causa real: `Desligar — Comando remoto recebido -- entrando em deep sleep` às 08:26:28, 08:46:58 e 08:48:37 (hora local), cada uma ~35 s após um boot "Normal" (RST/POWERON/abrir a serial). Os "Boot Normal" eram o usuário/serial religando; o aparelho lia `comandoDesligar=true` ainda gravado na nuvem e dormia de novo.
- **Causa raiz:** `desligarRemoto()` limpa `comandoDesligar` DENTRO do callback async do poll (mesmo erro da Fase 28b/Reiniciar: o patch aninhado nunca confirma), então o campo fica `true` para sempre. A defesa da Fase 18 só vale para wake pelo botão BOOT (`ESP_SLEEP_WAKEUP_EXT0`); qualquer outro boot lia o `true` antigo → loop de deep sleep.
- **Correção:** a 1ª leitura de `comandoDesligar` depois de cada boot, se vier `true`, é tratada como **antiga**: ignorada, `desligarLimpezaPend=true` e supressão (válvula de 3 min), e a limpeza roda por retry de 5 s no top-level do `loop()` (`limparComandoDesligarSincrono`). Ao confirmar, `desligarLimpoEmMs` descarta leituras de polls disparados antes da limpeza (resposta atrasada não pode re-disparar o sleep). Depois da 1ª leitura `false`/ausente, um `true` volta a ser comando novo de verdade (dorme). Logs: "Comando antigo de desligar encontrado ao ligar" / "limpo na nuvem".
- **Também nesta rodada:** push "queda de energia" no máximo 1× a cada 30 min (`/queda.cfg`; reinícios repetidos viram só log) e linha `[CTL]` de diagnóstico a cada 30 s na serial (T, U, inclinação, estado/ganho/lag/agressividade do CH1 e CH2, relés).
- **Estado:** gravado, **mas não consegui verificar**: a COM8 voltou a falhar logo após gravar ("dispositivo não está funcionando"). Backups: `main_pre_fase75.bk`. Se ainda dormir: ver Configurações > Logs no aparelho.

**Fase 76 — aquecedor por controle PI de ciclo fixo (2026-10-04).** Sintoma: a temperatura não parava de cair (34,1 °C às 09:46, alvo 37,5) e o status nunca virava "pronto". Dados reais lidos do app (Firestore `logs`, via sessão do usuário no Chrome): resumos de 15 min do CH1 — 09:13 `ganho 0.068 C/s lag 300s agress 0.90`; 09:28 `6 pulsos, ganho 0.003, lag 73s` (T=37,1 °C); 09:43 `3 pulsos, ganho 0.001 C/s, lag 265s, agress 0.90`.
- **Causa raiz (defeito de projeto do método da Fase 71–74):** numa caixa que PERDE calor o tempo todo, um pulso pequeno só *reduz a queda*; o aprendizado por resposta ao pulso concluía "não subiu", derrubava o ganho (×0,7 por janela), inflava o lag (×1,5 até 300 s → janela de espera 2×lag = 10 min; 25 min se já subindo) e a agressividade saturava em 0,9 → 3 pulsos em 15 min → a temperatura caía. O método confundia "perda de calor" com "pouca resposta".
- **Solução:** o aquecedor (AM_AQUECER) passou a `autoPidTick`: **PI de ciclo fixo (20 s)**, `D = Kp·e + I`, `e = alvo − (T + inclinação×45 s, limitado a ±1 °C)`, `Kp = 0,12·esc` (1 °C de erro → 12 % = 2,4 s a cada 20 s), `Ti = 240 s`; o integral aprende sozinho o duty que compensa a perda de calor, em qualquer caixa. Frações de duty acumulam (`acc`) até 0,5 s. Longe do alvo (`e > 1,2 °C`) liga direto (máx. 8 min); pulso cortado na hora se a previsão passar do alvo; sem as Fans (modo Circular) não aquece. **Auto-ajuste** a cada 10 min: ≥3 cruzamentos do alvo (±0,25 °C) → ganhos ×0,75; erro médio > 0,4 °C abaixo do alvo sem cruzar → ×1,3 (escala `esc` 0,35–3,0). Memória em `/pid.cfg` (I e `esc`). Resumo de 15 min na nuvem: "Resistencia (PI): duty medio X%…". Serial: linha `[PI]` junto do `[CTL]`.
- **Umidificador** segue no aprendizado por pulsos (`autoPulsoTick`) — funcionava e a umidade estava ACIMA do máximo (61 %), mas é vulnerável ao mesmo tipo de colapso; reavaliar se a umidade também não estabilizar.
- **Estado:** compila (RAM 31,8 %, Flash 32,1 %); **NÃO gravado** — COM8 indisponível ("dispositivo não está funcionando"). A placa continua com a Fase 75 (aprendizado por pulsos, com o defeito acima) até regravar. Backup: `main_pre_fase76.bk`. Os ganhos 0,12 / Ti 240 s são estimativas conservadoras (ganho da planta ~0,07–0,17 °C por segundo ligado) **sem validação em hardware**.

**Fase 77 — ciclo em PREPARO, confirmação de ovos, volume da caixa, status completo e tela "manter ligada" (2026-10-04).** Pedido (depois do PI funcionar: 37,3–37,5 °C, "Ambiente pronto" apareceu): o ciclo só deve começar quando o usuário confirmar que colocou os ovos; card Status com efeito de borda baseado nos 5 min; janela de status completa; etapa de volume da caixa; "manter tela ligada".
- **Modelo:** campo novo do doc `cicloExtra = "preparo;litros"` (ausente = ciclo normal, sem preparo). Ciclo em preparo: `cicloIniciadoEm` guarda só o instante em que o preparo começou, o **dia fica em 1 e a contagem parada**, a automação roda (`ativo` = ciclo ativo) e o relatório/estatísticas **não** contam o preparo. **"Ovos colocados"** (aparelho ou PWA) grava `cicloIniciadoEm = agora` + `cicloExtra="0;litros"` → dia 1 começa nesse instante. Máscara do poll do ciclo agora tem 7 campos (limite prático ~8). Ciclos antigos (sem `cicloExtra`) continuam iguais e o Status mostra "OK".
- **Status (home e PWA):** `OFF` verde (sem ciclo) · `Aguarde` âmbar com **borda que enche** (sentido horário, progresso = tempo estável/5 min; firmware `bordaProgresso`, PWA `--sp` + conic-gradient como a borda neon dos atuadores) · `Pronto` verde com borda cheia (preparo estável, falta confirmar) · `OK` (ciclo em andamento). Legenda "sem ovos ainda" removida. Card Ciclo mostra `Preparo` (âmbar) durante o preparo. No PWA a barra interpola o tempo entre os heartbeats (o aparelho informa a cada ~1 min).
- **Janela do Status (tela cheia no aparelho `S_STATUS`, overlay no PWA):** temperatura/umidade/automação com ✓/✗ e faixa, barra "estável há m:ss de 5:00", caixa em litros, mensagem ("NÃO coloque os ovos ainda"/"Pode colocar os ovos") e botão **Ovos colocados** (confirmar antes do "Pronto" pede 2º toque / confirmação) + Cancelar ciclo. O card **Ciclo** em preparo abre a mesma janela (no aparelho `drawCicloScreen` → `drawStatusDetalhe`; no PWA bloco `#ciclo-preparo`).
- **Assistente (4 etapas):** 1 nome · 2 tipo · **3 volume da caixa** (3/10/30/60/100 L ou valor livre 1–500; lembra o último em `/vol.cfg`/localStorage) · 4 resumo com as instruções. Aparelho: sempre inicia o **preparo** (a etapa de data foi retirada). PWA: etapa 3 "Caixa e início" tem o modo **"Os ovos já estão na chocadeira"** (informa data/hora) para lotes já em andamento.
- **Volume → controle:** `pidVolEsc() = clamp((V/3)^0.6, 0.6, 6)` (3 L = referência onde os ganhos foram validados) escala o `Kp` do PI, o duty inicial `I` e o tempo de previsão (`×√esc`); `/pid.cfg` guarda o volume e descarta o duty aprendido se a caixa mudar >30 %. **Heurística**: não conhece a potência da resistência; é só um ponto de partida melhor para estabilizar mais rápido.
- **Tela (Config.):** agora no padrão novo (tela cheia, faixa `uiTopo`, voltar pelo topo), **sem as legendas do rodapé** e com o interruptor **"Manter tela ligada"** (`telaSempreLigada`, 2ª linha de `/display.cfg`): enquanto ligado o screensaver nunca entra; o "Apagar após" fica esmaecido.
- **Estado:** firmware compila (RAM 31,8 %, Flash 32,2 %) mas **NÃO foi gravado** — COM8 falhou ("dispositivo não está funcionando"). PWA em produção (v15), testado no navegador (assistente etapa 3 e 4, janela do Status, borda de progresso, tela do ciclo em preparo). **Não testado no aparelho:** nada da parte nova do firmware. Backups: `main_pre_fase77.bk`, `*.pre78.bk` (frontend).

## Fase 78 — Hardware, PI unificado (temp + umidade), alvo coerente com o ciclo (2026-10-04)
**Investigação do teste 4 (preparo eterno):** a regra do CH1 tinha alvo PERSONALIZADO 37,5 °C, mas o tipo "Rei" (40,5 °C/72 %, faixa 40–41) -> o ambiente nunca entrava na faixa. Além disso o PI antigo tinha 3 falhas (simulação em planta calibrada com os logs, `scratchpad/sim_*.py`): (1) limite do termo I = 0,5 e D = 0,8 (duty de regime real 55–70 %) -> sempre abaixo do alvo e o auto-ajuste inflava o ganho x3; (2) corte antecipado do pulso por "temperatura prevista" distorcia o PI (erro permanente abaixo do alvo, oscilação 2–3 min fora da faixa); (3) chaveamento do relé a cada 1–3 s no limiar de 1,2 C (aquecimento direto <-> PI). Queda de USB (COM8) coincide com o desligamento do relé do aquecedor (EMI da carga de 100 W).
**Firmware:** `HwCfg hw` (volume, material, tipo/potência/qtd do aquecedor, relé mecânico/SSR, umidificador tipo/ml-h/qtd, fans) — tela Configurações > pág. 2 > Hardware (`S_HARDWARE`), salva sozinha 3 s depois, sincroniza com o PWA no token `H,...` da string `cmd.auto` (entre as regras e o `X`; sem campo novo no Firestore). `autoPidTick(ch,temp,...)` agora serve aquecedor E umidificador: período do ciclo/Kp saem de potência/volume (ondulação ~ P·período/C) e são refinados pela taxa de subida MEDIDA nos aquecimentos diretos (`rMeas`); duty de regime inicial por perda da caixa; I reescalado se o alvo muda; histerese direto(>1,2)/PI(<=0,5); I e D até 1,0; sem corte antecipado (só alvo+0,3 C / +3 %); relé mecânico ciclo >= 15 s, SSR >= 3 s; aviso "aquecedor/umidificador no limite" após 10 min saturado; `/pid2.cfg`. `autoAlvoTemp/Umid`: alvo personalizado fora da faixa do ciclo (±0,3 / ±3) é ignorado com log 1x por ciclo. Volume do assistente = `hw.volL`. `autoDoPoll` tolera PWA antigo sem token H.
**PWA (v16):** card "Hardware" em Configurações (selects/números + resumo com estimativas e avisos), `hwParse/hwSerializar`, `autoParse/autoSerialize` carregam `hw`. Backups `*.pre79.bk`.
**Estado:** firmware COMPILA; ver resultado da gravação no relatório da sessão (COM8 caía). Não verificado em hardware.

## Fase 79 — ciclo que "voltava", manter o ambiente sem ciclo, bug do aviso na tela (2026-10-04)
**Causa do ciclo que voltava:** `pushCicloParar()` mandava PATCH com corpo vazio + updateMask; a biblioteca devolvia "OK" mas a nuvem NUNCA apagava os campos (confirmado: `cicloIniciadoEm` de 11:51 seguia no Firestore apos 3 "Parar" bem sucedidos) -> o proximo poll ressuscitava o ciclo. Correcao: campos viram `NullValue` (aparelho `extrairCampo*` e PWA tratam null como ausente); idem `cicloPausadoEm` em Iniciar/Retomar. Tambem: `cicloAcaoEncerrar` agora marca `cicloSyncPend` se o Firebase nao estava pronto, e `cicloParouMs` (90 s) impede que um poll que ainda traz o ciclo o ressuscite (reenvia o Parar). O PATCH chegava a levar ~30 s para completar.
**Manter o ambiente:** `manterAtivo` (token `M,0|1` em cmd.auto, depois do `H`) + `ultTipoNome` (/ult.cfg, gravado quando o poll ve um ciclo). Ao terminar/cancelar o ciclo (aqui ou no PWA) `manterAtivo=true`: `automacaoTick` segue ativo com `ambManterOn()`, `autoTipoAtual()`/`aplicarLimitesDoTipo()` usam o ultimo tipo (dia -1: sem eclosao; viragem NAO roda). `ambEstado` 3 = mantem (na faixa >= 1 min), 4 = ajuste. Card Status: "Mantem" (verde) / "Ajuste" (ambar); janela do Status: botao "Parar manutencao" (2 toques) ou, parado, "Manter ambiente". Heartbeat `ambiente`: `manter;1|0;<tipo>` / `parado;<tipo>`. PWA v17: mesmo estado no card e na janela (`manterAlternar` reescreve cmd.auto com M).
**Bug de tela:** `arqToast` desenha por cima e nao apaga; o toast "Aguarde: ajustando ambiente" ficava pela metade entre os cards (os cards Status/Ciclo redesenhavam por cima so das laterais). `ambToast()` redesenha a tela depois.
**Aviso de limite** agora conta o tempo ligado (>=90 % de 10 min com erro > 0,5 C / 5 %), inclusive no aquecimento direto. Teclas de depuracao serial: `Q` encerra o ciclo (testa o Parar).
**Observado no teste 4:** com 100 W o aquecedor sobe ate ~39,5-39,9 C e fica no limite (alvo do tipo "Rei" = 40,5 C/72 %); umidade 55 % com o umidificador ligado. Limite fisico da caixa de teste; o aviso "no limite" deve disparar.

## Fase 80 — Status OFF com CH1/CH2 ligados, diagnostico do ambiente, teclado no wizard (2026-10-04)
**Status OFF + CH1/CH2 ligados:** a chave "So com ciclo em andamento" estava DESMARCADA (`autoSoCiclo=false`), entao a automacao seguia ativa sem ciclo (CH1 com alvo custom 37,5 / CH2 com a faixa geral) mesmo com o Status em OFF. Agora os termostatos (Aquecer/Resfriar/Umidificar/Desumidificar) so agem com ciclo (nao pausado) OU `ambManterOn()`, independente da chave (`termoAtivo` em `automacaoTick`); so desliga o que a propria automacao ligou (`autoUltimo`), sem brigar com o manual. A "sessao de pulsos" (`pulsoNuvemVal`) e zerada na nuvem quando a automacao para (senao o poll religava o rele). Sobretemperatura so notifica com ciclo/manutencao. Apos o boot, a 1a leitura da nuvem de um canal termostato com ON e rele local OFF e tratada como sessao antiga (antes religava o aquecedor sozinho e passava do alvo).
**Diagnostico (ambDiagTick/ambDiagCalcular):** se em 10 min (desde o inicio do preparo/manutencao ou da ultima vez na faixa) T/U nao estao na faixa: etiqueta curta no card Status + texto completo na janela do Status, no heartbeat (4o campo de `ambiente`), no log e em push (so quando a causa muda, >= 5 min). Ordem: sensor · automacao desligada · CH1/CH2 em Manual · Fans · queda brusca (tampa aberta: dT <= -0,9 C/min ou dU <= -6 %/min nos ultimos 8 min) · temp. alta · taxa de subida MEDIDA vs prevista pelo Hardware (>1,8x = volume menor / aquecedor mais forte que o informado; <0,45x = volume/potencia altos demais ou tampa) · aquecedor no limite (+ aviso se o tipo pede > 39 C) · caindo com aquecedor ligado (resistencia/fonte/rele) · sem umidificador · umidificador no limite · sem agua · umidade alta · "ainda ajustando". O aviso antigo de 15/60 min foi desativado.
**PWA (v20):** wizard com teclado (Enter/seta direita avancam, seta esquerda/Esc voltam, setas cima/baixo escolhem tipo / volume); volume do wizard vem de Configuracoes > Hardware (default 3 L, antes 10 L) e atualiza o Hardware; card Status e janela mostram a causa provavel; ajuda da chave "so com ciclo" atualizada.

## Fase 81 — picos de temperatura/umidade e alertas em excesso (2026-10-05)
**Sintoma (graficos do dia):** rajadas de oscilacao +-0,7 C com periodo ~10 min (a leitura de 5 em 5 min so mostra o zigue-zague 38,0/36,6/38,0...) alternando com trechos calmos; dezenas de alertas "fora da faixa/normalizada". A umidade segue a temperatura (UR cai ~3 pontos a cada +1 C).
**Causa:** (1) periodo do ciclo do PI alongava para 28 s porque a taxa medida (liquida, em temperatura alta) substituia a estimada -> ondulacao maior; (2) o Hardware estava com **volume 10 L** (caixa real = 3 L): escalava ganho/previsao errado (o teste do wizard no PWA gravou 10 L — ver abaixo); (3) **identificacao da planta a partir do log real** (`scratchpad`: ajuste de modelo aos pulsos do CH1 x temperatura, erro 0,034 C): potencia efetiva ~72 W, perda ~3,5 W/C, capacidade ~4100 J/C, **atraso morto ~77 s + inercia do sensor ~100 s**. O PI antigo (Kp 0,3, Ti 200->450, antecipacao 30-60 s) e instavel para esse atraso (simulacao: amplitude 1,7 C na planta lenta); a temperatura continua subindo ~80 s depois do ultimo pulso.
**Correcao (Fase 81/81b):** periodo = 0,15/max(taxa prevista, medida) (a medida so encurta); Kp = 0,0021/ron (~0,15 p/ 100 W em 3 L), **Ti 700 s**, **antecipacao 150 s** (x volume^0,25 x tipo de aquecedor, 60-240 s, limite +-1,5 C); auto-ajuste 0,5-1,3 (oscilou >= 2 cruzamentos -> x0,8). Simulado nas 4 plantas (rapida/media/identificada/lenta): amplitude ~0,12 C em todas. Alertas T/U: precisa **3 min** fora (era 1) e 3 min bem dentro para normalizar; histerese de temperatura 0,20 C.
**Estado:** Fase 81 (Ti 450, antecipacao 60 s) esta gravada; **81b (Ti 700, antecipacao 150 s) COMPILADA mas NAO gravada** — a COM8 caiu de novo ("dispositivo nao esta funcionando"). Gravar com `pio run -e esp32-s3-controller -t upload`.
**Incidente (meu):** um teste automatizado do teclado do wizard no PWA apertou Enter na etapa 4 e **criou de verdade o ciclo "teste teclado"** (Galinha, 21 d, `cicloExtra "0;10"`, inicio 14:45 de 04/10), substituindo o ciclo em preparo que o usuario criou as 14:05 (nome original perdido; estava em "Pronto"). O Hardware tambem ficou com volume 10 L (corrigido de volta para 3 L). Regra: nunca disparar o envio final do wizard em testes automatizados.

## Fase 82 — pausa nao desliga o controle, tampa aberta autonoma, overshoot (2026-10-05)
**Pedido:** ao pausar o ciclo (para pôr agua no umidificador) o ambiente saiu do ideal (33,9 C); na ovoscopia o usuario abre a tampa — nao pode ser obrigado a pausar sempre.
**Pausa:** `ativo`/`termoAtivo` agora = ciclo ativo (pausado ou nao) ou manutencao. Pausar so congela a contagem de dias (Retomar desloca o inicio); o aquecedor/umidificador seguem controlando (os ovos estao la dentro).
**Tampa aberta (autonomo):** `tampaTick()` (1 Hz). Detecta pela queda brusca (temp <= -0,9 C/min ou umid <= -8 %/min, >= 8 s, so apos 3 min ligado; nao vale no preparo) ou pelo aviso "Vou abrir a caixa" (aparelho: janela do Status; PWA: janela do Status, `tampaComando`, token `L,1|2` em cmd.auto que o aparelho zera de volta). ABERTA: sem alertas e sem diagnostico, integral do PI congelado, umidificador em pausa, aquecedor limitado a 60 % e sem aquecimento direto, relatorio nao conta o tempo fora da faixa. Fechada (temperatura voltou a subir >= 0,12 C/min por 20 s, "Fechei a caixa" ou 15 min): RECUPERANDO ate 1 min na faixa (umidificador entra direto 3 % antes); log "Ambiente recuperado: tampa aberta Xs, recuperacao Ym". Card Status "Tampa"/"Recup.", heartbeat `ambiente="tampa;1|2;<seg>"`. Falso positivo no boot corrigido (janela de inclinacao ainda se formando).
**Overshoot:** na partida a frio (33,9 -> aquecimento direto) a caixa foi a **41,7 C** (seguranca desligou o CH1 em 38,8) e depois +2 C a cada rajada. Causa: atraso total sensor ~2-4 min (SHT31 em sonda selada) e o termo de antecipacao (150 s) virava o termo dominante -> **ciclo limite** (rajada de ~65 s de aquecedor -> +1,3 C por 4 min -> queda por 7 min). Dados reais (log 12:03-12:24): ganho ~0,019-0,023 C por segundo ligado, perda tau~1700 s, duty de regime ~0,3. Correcoes: (a) **governador de energia** (`govBloqueia`): segundos ligado na janela de 260 s menos duty de regime x janela x (P/C) = subida ainda "em transito"; nao aquece se isso cobre >= 70 % do que falta; (b) previsao completa sem limite para sair do aquecimento direto, corte do pulso e trava em subida rapida (>= 0,3 C/min); (c) Kp 0,0014/ron (~0,1), **Ti 900 s**, antecipacao **45 s**, corte duro de 0,3 -> 0,8 C (o corte virava termostato), integral x3 quando parado fora do alvo; `/pid3.cfg`. Simulacao com a replica do firmware em 9 variacoes de planta: oscilacao <= 0,34 C, pico <= +0,85 C, desvio medio <= 0,04 C apos +3 C de ambiente. No aparelho: amplitude caiu de ~1,3 C para <0,1 C em regime (offset de +0,7 C ainda corrigindo pelo integral lento).
**Incidente:** o teste de teclado do wizard criou o ciclo "teste teclado" (ver Fase 81). O ciclo atual no aparelho: t7 (Galinha) pausado.

## Fase 83 — bomba de ar (CH4) com cooler, ventilação inútil na chuva, água do umidificador (2026-10-06)
**Pedido:** com chuva a umidade da caixa subiu; o usuário ligou o CH4 (bomba de ar 3,7 V) à mão e a umidade continuou subindo (desligou). A bomba esquenta e fica perto do ESP32 -> o cooler do sistema deve ligar junto. Bomba de água (reservatório externo 5 L -> interno 150 ml) ainda não existe: marcar "em desenvolvimento"; se o aparelho tenta subir a umidade e ela não sobe = acabou a água -> pulsos curtos da bomba até a umidade reagir; sem efeito -> alerta.
**Bomba de ar (`bombaArTick`, 1 Hz, vale mesmo com a automação desligada):** (1) `coolerPorBomba()`: o Cooler Sistema (CH7) liga junto com o CH4 e fica 2 min depois de desligar (log "Cooler Sistema -> ON (bomba de ar ligada)"); (2) máx. 20 min seguidos ligada (esquenta) -> desliga + log/push; (3) ligada >= 4 min com umidade ACIMA do máximo, subindo >= 1 ponto e `humSlopeMin>0,2` = ar de fora mais úmido: desliga, log/push "ventilar não ajuda" e o modo Desumidificar/ventilar não religa por 30 min (`ventInefAte`). Tensão do CH4 agora "3.7V DC" (firmware e PWA).
**Água do umidificador (`aguaTick`, 1 Hz; `aguaEst` 0 ok · 1 testando bomba · 2 SEM ÁGUA):** janela de 5 min (amostra a cada 10 s): umidificador ligado >= 60 % do tempo, umidade >= 3 pontos abaixo do alvo e ganho compensado pela temperatura (`ΔU + 3·ΔT`) <= +0,6, em 3 avaliações seguidas (~6,5 min) = suspeita. Não roda com tampa aberta/recuperando, sensor sem leitura, regra do CH2 fora do modo Umidificar, ou 5 min após toque manual no CH2. **Sem bomba (padrão):** ALERTA na hora (card Status "sem agua", janela do Status, log "Alerta", push tag 15, toast), umidificador desligado (não deixa seco) e SONDA de 150 s a cada 10 min (1ª após 5 min); quando o ganho >= +0,8 o alerta encerra sozinho ("Agua detectada"); também encerra se a umidade voltar ao alvo (-1) por 2 min; re-avisa a cada 60 min. **Com bomba** (Config. > Hardware > "Bomba de água (CH3) · EM DESENVOLVIMENTO", token `H,...,bomba`, padrão = não instalada): até 5 pulsos de 4 s do CH3 (`autoAplica`, vai ao log da nuvem), cada um seguido de 150 s de observação com o umidificador ligado; se a umidade reage = "reservatorio reabastecido (N pulsos)"; senão alerta de reservatório externo/bomba/mangueira; no máximo 1 sequência a cada 30 min. O PI do umidificador fica em pausa (`aguaPausa`) durante teste/alerta. Estado mostrado só pelo texto de diagnóstico existente (sem campo novo no Firestore). PWA v23: card Hardware ganhou o grupo "Bomba de água (CH3) · EM DESENVOLVIMENTO" + ajuda `hwBomba`.
**Depuração serial:** `W` simula a suspeita (alerta -> sonda de 20/30 s -> "agua detectada" porque o umidificador tem água), `B` alterna "bomba de água instalada" (clica o CH3 nos testes com bomba). Log `[AGUA] ligado X% ganho +Y falta Z suspeitas N` a cada 30 s.
**Estado:** compila (RAM 32,2 %, Flash 32,6 %); PWA em produção (v23). **Firmware NÃO gravado:** COM8 caiu de novo ("Um dispositivo conectado ao sistema não está funcionando") — religar o cabo USB e rodar `pio run -e esp32-s3-controller -t upload`. **Nada testado em hardware** (nem o cooler com o CH4, nem a detecção, nem os pulsos). Limiares (3 pts, 60 %, +0,6, 3 avaliações) são estimativas: se der falso alarme, relaxar em `aguaTick`. Backup: `main_pre_fase83.bk`; PWA `*.pre83*.bk`.

## Fase 84 — umidade fora da faixa (44–57 %) em 07/10: causa = hora sem água + ganho aprendido errado (2026-10-07)
**Dados reais (Firestore `leituras`/`logs` + arquivo `/chocaiot.log` do SD pedido pelo PWA):** temperatura excelente o dia todo (37,3–37,8 °C); umidade 49–53 % de madrugada (alvo 50). **13:00–14:05 umidade 44–45 % = reservatório do umidificador SEM ÁGUA** (resumo do PI: duty 100 % às 13:26/13:41; só o aviso genérico "no limite" às 13:59). Usuário encheu às 14:05 (tampa aberta; T 35,4 °C) e depois a umidade oscilou 45 <-> 54.
**Bug achado:** durante a hora sem água o aprendizado `rMeas` do umidificador gravou o PISO (0,05 %/s) em vez de ~2,7 (estimativa do Hardware) -> `Kp = 1/(k·90)` ficou ~25x maior (0,1 em vez de 0,004), o integral encheu (I 0,15 -> 0,66, `/pid3.cfg`) e o PI passou a mandar rajadas (pulsos de 90 s cortados em ~10 s por "acima do alvo", resumo "duty medio 293 %" = contagem de pulsos cortados). **Correção (Fase 84):** `pidFimDireto` só aprende a taxa da umidade se `aguaEst==0` e `rPico >= 25 % de hwKumPrior()`; `pidCarregar` descarta UMA vez o registro do CH2 (marcador `/mig83.flag`) -> semente nova (I 0,03, ganho do Hardware).
**Não resolvido / hipóteses:** (1) picos de +5 pts (até 57 %) a cada ~45 min de manhã com duty 10–15 %: provavelmente a nuvem de névoa passando pela sonda (a leitura do SHT31 pula logo após o pulso e cai em 1–3 min); precisa de log de 1 Hz da umidade (serial) para confirmar — se for isso, filtrar a leitura de controle por ~25 s depois de cada pulso e/ou afastar a sonda do bico do umidificador; (2) 13:15 sobretemperatura 38,3 °C (desligou o CH1) com duty de 12–18 %: provável tampa aberta sem aviso (umidade caiu 55 -> 47 entre 12:36 e 12:46) e o calor acumulado ao fechar; a detecção automática só vê queda brusca (>= 0,9 °C/min ou 8 %/min), o botão "Vou abrir a caixa" cobre; (3) segundo SHT31 externo: não é necessário para isso; útil como referência do ambiente (chuva/ventilação, compensar perda de calor) — usar endereço 0x45 (módulo com ADDR em VCC), o sonda selada atual é 0x44.
**Estado:** Fase 83 (água) GRAVADA às 14:52; Fase 84 compilada, **NÃO gravada** (COM8 caiu de novo; religar o USB e `pio run -e esp32-s3-controller -t upload`). Backup `main_pre_fase83b.bk`.

## Fase 85 — bomba de água real (VARIALL VA-BA01, AC 110–220 V) e travas contra transbordar (2026-10-07)
**Hardware informado pelo usuário (fotos):** bomba submersa **VARIALL VA-BA01, AC 110–220 V / 50–60 Hz / 3 W, 180 L/h máx., Hmax 0,35 m, IPX8, classe II (sem terra)**, líquido ≤ 35 °C, regulagem de vazão no mínimo ("−"). Vai no galão de 5 L ao lado da caixa; cabo elétrico + mangueira até o reservatório interno (~150 ml) do umidificador. **Atenção: é carga de REDE (220 V), não 12 V** — `RELAY_VOLTAGE[2]` agora "220V AC" (firmware e PWA; o PWA passa a pedir a confirmação de 220 V para ligar o CH3 remotamente). O relé do módulo (SRD-05VDC-SL-C, 10 A/250 V) aguenta; ligar só no NA do relé, na FASE.
**Risco principal = transbordar** (180 L/h = até ~50 ml/s; reservatório de 150 ml; sem sensor de nível). Camadas no firmware (`aguaBombaFimRapido`, bloco da Fase 85 em `aguaTick`): (1) **bomba DESATIVADA até informar a vazão medida** (Hardware > "Vazão da bomba", ml/s; token `H,...,bomba,bombaMlS10`; 0 = alerta "bomba sem calibrar"); (2) pulso = 15 ml ÷ vazão (aceita 0,3–8 s, senão alerta); (3) até 4 pulsos/sequência (60 ml), cada um seguido de 150 s de observação; para ao 1º sinal de resposta da umidade; (4) 1 sequência/h e 120 ml/dia, **persistidos em `/bomba.cfg`** e gravados ANTES de ligar; sem hora válida (NTP) não bombeia; (5) só após ~6,5 min de umidificador ligado sem resposta, tampa fechada; (6) **vigia de 8 s no relé CH3 por qualquer origem** (automático, PWA, toque) com precisão de ms no `loop()` + log/push; o fim do pulso também é feito lá (não depende do tick de 1 s). Sem a bomba instalada nada muda (só o alerta de água da Fase 83).
**Instalação (recomendações dadas ao usuário):** ponta da mangueira ACIMA do nível máximo de água do galão (senão sifão: a água escoa sozinha para a caixa) + válvula de retenção/anel anti-sifão; ladrão (dreno) no reservatório que volte ao galão; primeiro teste com a ponta num copo FORA da caixa, medindo ml em ~5 s; fusível/DR no circuito da bomba, emenda isolada, laço de gotejamento no cabo, cabo/relé longe da água. PWA v24: campo "Vazão medida (ml/s)" + ajuda (como medir, sifão, ladrão).
**Estado:** Fases 83 + 84 + 85 compiladas (RAM 32,2 %, Flash 32,7 %) mas **NÃO gravadas** (a Fase 83 inicial está no aparelho desde 14:52 de 07/10; COM8 caiu de novo na gravação). Backups: `main_pre_fase85.bk`, `*.pre85.bk`. **Nada testado com a bomba.** Teste sugerido depois de gravar: `B` (liga "bomba instalada") + vazão informada + `W` (simula a falta de água) com a ponta da mangueira num copo.

## Fase 86 — tela Hardware redesenhada + revisão do CH3 (2026-10-07)
**Tela Hardware (firmware):** 6 cartões por página (36 px, `HW_Y0=21`, `HW_PITCH=36`, `HW_N=6`): nome pequeno em cima, **valor em fonte 2** embaixo, botões −/+ de 38×26 px; **todos os textos de explicação removidos** (rodapé e linha "Total W / ciclo"). Pág. 1: volume, material, tipo/potência/quantidade do aquecedor, relé do aquecedor. Pág. 2: tipo/vazão/quantidade do umidificador, quantidade de fans, **Bomba de água CH3 (EM DESENV.)** ("Instalada"/"Nao") e **Vazão medida da bomba** (valor + "ml/s  pulso X s"; "SEM MEDIDA" em âmbar quando a bomba está marcada e a vazão é 0). Zonas de toque: − em x 220–268, + em x 268–316. Teclas de depuração: `j` = Hardware pág. 1, `J` = pág. 2 (com `p` tira o print).
**Revisão do CH3 (só código):** nome "Bomba Agua", tensão "220V AC" (faixa vermelha na tela Atuadores, toggle vermelho ao ligar), PWA pede a confirmação de 220 V. O vigia de 8 s (`aguaBombaFimRapido`) vale também para o toque na tela: um teste manual de vazão com mais de 8 s é cortado (e a automação do CH3 pausa 5 min) — medir com ≤ 5 s.
**Pendente (testes do usuário antes de mexer na lógica):** sifão com a bomba desligada (ponta da mangueira acima do nível do galão), escorrimento da mangueira após o pulso (25 ml na mangueira de 1,37 m; sem válvula e sem ladrão, parte pode descer ao umidificador), pulso de 2,3 s a 6,4 ml/s, mangueira "seca" após ficar parada. Depois: trocar "vazão × tempo" por tabela medida (ml entregues incluindo o escorrimento) e descontar o escorrimento do pulso.
**Estado:** compilado (RAM 32,2 %, Flash 32,7 %); **NÃO gravado** (COM8 caiu no upload; religar o USB). Backup anterior: `main_pre_fase85.bk`.

## Fase 87 — CH3 ligava sozinho no reinício, chave que não voltava, opção da bomba que revertia (2026-10-07/08)
**Relato:** ao gravar o firmware o CH3 ligou e a água transbordou o reservatório do umidificador (teste fora da caixa); a chave do CH3 em Atuadores não voltava a "off" após 8 s; a opção "Bomba de água" na tela Hardware não "pegava". Medido pelo usuário a 30 cm de altura: ~4 s até a água sair; ao parar, a água da mangueira **volta para a bomba** (sem válvula) — ou seja, cada pulso gasta ~4 s só enchendo a mangueira (25 ml ÷ 6,4 ml/s) e o que está na mangueira não escorre para o umidificador depois.
**Causas (lendo o código):**
1. A nuvem guardava `atuadores.ch2=true` (toque na tela/PWA empurrava `true` para o Firestore). Após reiniciar, o 1º poll aplicava `ch2=true` e a bomba ligava (e voltava a ligar logo após o vigia de 8 s desligar, porque a escrita de `false` na nuvem demora e o poll seguinte relia o `true`).
2. A tela Atuadores só redesenhava no toque; o vigia desligava o relé mas a chave ficava desenhada "on".
3. `autoDoPoll` aplicava a configuração antiga da nuvem (`cmd.auto`, com `bomba=0`) durante os 3 s entre a edição na tela Hardware e o salvar/enviar → a edição era desfeita.
**Correções:** (1) **CH3 nunca é ligado nem restaurado pela nuvem**: `applyActuatorsFromFirestore` ignora o canal 2 (se a nuvem diz `true`, marca `bombaNuvemLimpar` e o `loop()` reescreve `false`, no máx. a cada 30 s); `setRelayAndSync` e `autoAplica` **não enviam mais o estado do CH3 à nuvem**. PWA (v25): toggle do CH3 sempre desabilitado, selo "SÓ NO APARELHO". (2) `aguaBombaFimRapido` redesenha a tela Atuadores quando qualquer relé muda. (3) `autoDoPoll` ignora a nuvem enquanto `hwSujo` ou até 10 s depois da última edição local. Serial: `[HW] toque x y pag linha dir` ao tocar na tela Hardware (para conferir o mapeamento do toque).
**Pendente / decisões:** (a) pulso dosado: com mangueira que esvazia, entrega ≈ vazão × (t − tempo de enchimento); falta o usuário medir (balança) para eu trocar a conta "15 ml ÷ vazão" por "tempo de enchimento + 15 ml ÷ vazão" (≤ 8 s do vigia); (b) se o CH3 ainda estalar no primeiro segundo do reset (antes do firmware iniciar), é hardware — pull-up no IN3 do módulo de relé; (c) COM8 continua caindo (USB) durante gravações — religar o cabo.
**Estado:** firmware Fase 87 compilado (RAM 32,2 %, Flash 32,7 %), **NÃO gravado** (COM8 caiu de novo no upload; o aparelho segue com a Fase 86, que ainda religa o CH3 pela nuvem). PWA v25 em produção. Backup: `main_pre_fase87.bk`.

## Fase 88 — compartilhamento da chocadeira (PWA + regras + firmware), bomba com tempo de enchimento, umidade (2026-10-08)
**Medidas da bomba (usuário, balança):** mangueira vazia → 3 s até a água sair; 6 s de bomba → 45 ml; mangueira ainda cheia 10 s depois do pulso anterior → 38 ml. Sem válvula a mangueira esvazia de volta para a bomba (nada escorre para o umidificador depois do pulso). **Firmware:** `HwCfg.bombaPrimeS10` (token `H,...,bomba,bombaMlS10,bombaPrimeS10`); `bombaPulsoMs() = primeS + 15 ml ÷ vazão` (aceito 0,3–8 s, vigia de 8 s do CH3 continua). Tela Hardware agora tem **3 páginas** (3ª = bomba: Instalada · Vazão com mangueira cheia · Tempo até a água sair · "Duração de cada pulso" calculada). Teclas `j`/`J`/`u` = páginas 1/2/3. **Valores sugeridos (não aplicados — o usuário digita):** tempo de enchimento 3,0 s e vazão ~13–15 ml/s (45 ml ÷ 3 s) → pulso ≈ 4 s ≈ 13–15 ml. A vazão antiga (6,4 ml/s) foi medida em outra montagem; confirmar com 2 medições (4 s e 8 s).
**Compartilhamento (Firestore):** `chocadeiras/{id}/shared_with/{e-mail minúsculo}` = `{email, permissoes[], status pendente|aceito|recusado, token(24), dataConvite, donoNome, donoEmail, chocNome, uid, respondidoEm, tokenAceite, ultimoAcesso, nomeConvidado, permAlteradaEm}`; `usuarios/{uid}.acessos[]` lista os acessos aceitos. Permissões: `ver` · `ch0..ch7` · `ciclo` · `total`. **Regras** (`firestore.rules`, 63 testes no emulador — `scratchpad/rt/test.js`, rodar com `firebase emulators:exec --only firestore ...`): convidado aceito (e-mail verificado + uid) lê a chocadeira e leituras/logs/histórico/ciclos/câmera; `arquivos` só com `total`; escreve só o que a permissão deixa (`atuadores.chN` marcados, `cmd.vivo`/`cmd.convite`, campos do ciclo com `ciclo`, `cmd.fimTipo` com `ciclo`; `total` = tudo menos donoId/pushTokens/convidados); aceitar exige e-mail igual ao convidado + token do link; convidado não muda permissões nem se reaceita depois de revogado/removido. Bomba (CH3) e Cooler (CH7) nunca são liberados.
**PWA (v28, `share.js` novo):** Config. > "Usuários compartilhados" (dono: lista, convidar, permissões, reenviar, remover, último acesso) e "Acesso compartilhado" (convidado: dono, permissões, sair). Convite = link `/?convite=<idChoc>&t=<token>` (guardado em localStorage até logar; tela "Aceitar convite" com dono/chocadeira/permissões; e-mail diferente → "trocar de conta"). **Envio por e-mail automático NÃO existe** (sem Cloud Functions/servidor de e-mail): botões E-mail (`mailto:` com a mensagem pronta), WhatsApp (`wa.me`), Compartilhar (Web Share) e Copiar. UI do convidado: cadeado + "Sem permissão" nos atuadores sem permissão, cartões de configuração que exigem `total` travados, Desligar/câmera/limpar logs travados, botões de ciclo ocultos sem `ciclo`. Notificações: **dentro do app** (dono: convite aceito/recusado, inclusive o que ocorreu com o app fechado; convidado: permissões alteradas / acesso removido). **Push em segundo plano:** o convidado grava `cmd.convite` ao aceitar e o **firmware** envia push ao dono (`pushEnfileirar(3,...)` + log "Compartilhamento"); recusa e mudança de permissão não geram push (sem servidor). `app.js`: `entrarNaChocadeira(chocadeira, papel, conv)`, `state.papel/perm/conv`.
**Bug de infraestrutura achado e corrigido:** a rota `/` não casa com o `Cache-Control` do hosting; o service worker usava `fetch()` com cache HTTP e podia servir `index.html` antigo com `app.js` novo (tela em branco). SW agora faz `fetch(..., {cache:"no-cache"})`.
**Testado:** regras (emulador) + produção com a conta do dono: criar convite (link/mailto/wa.me), tela de aceitar, aceitar (status, `usuarios.acessos`, `cmd.convite`), UI de convidado simulada com a própria conta (permissões, cartões travados); dados de teste removidos. **Não testado:** convite de verdade com 2ª conta Google, push de aceite (firmware não gravado), notificação de mudança de permissões entre dois aparelhos.
**Umidade (08/10):** médias de 10 min do dia ficaram em 49–54 %; leituras de 5 min > 55 % só às 15:40–15:50 (57,1 %) e 16:35 — coincidem com a regravação do firmware (CH3 ligou e encheu o reservatório do umidificador) e com o SD reconectado. O PI do umidificador estava com ganhos no piso (x0,50) e I subindo 0,15→0,24. Mudança conservadora: corte do pulso em alvo+2 (era +3) e piso dos ganhos da umidade 0,35 (era 0,5). **Sem validação em hardware** — observar o gráfico de amanhã; se persistir, logar umidade a 1 Hz na serial (hipótese da nuvem de névoa na sonda).
**Estado:** firmware Fase 88 compilado (RAM 32,3 %, Flash 32,7 %), **NÃO gravado** (COM8 caiu; religar o USB). O aparelho ainda roda a Fase 86 (religa o CH3 pela nuvem) — manter a bomba fora da tomada. PWA v28 + regras publicadas. Backups: `main_pre_fase88.bk`, `*.pre88.bk`, `firestore.rules.pre88.bk`.

## Fase 89 — auditoria "quem fez" nos logs, Hardware compacto, e-mail HiveMQ verificado (2026-10-09)
**Convite funcionou ponta a ponta:** o log do app mostrou `COMPARTILHAMENTO: convidado@exemplo.com aceitou o convite...` (aceite por 2ª conta real + push/log do firmware via `cmd.convite`).
**Auditoria:** toda ação feita pelo APP (dono ou convidado) grava uma linha em `chocadeiras/{id}/logs` com `{categoria, mensagem, ator(e-mail), papel(dono|convidado), origem:"app", timestamp}` (`registrarAcao()` em `app.js`). Categorias: `Atuador` (ligou/desligou), `Ciclo` (iniciar/pausar/retomar/encerrar/excluir, ovos colocados, abrir/fechar caixa, manter ambiente), `Comando` (desligar/reiniciar/tipos de ovos/nome), `Automacao` (automação e hardware). **Regras** (`logAppValido`): o ator tem de ser o e-mail verificado da própria conta (não dá para assinar como outro), timestamp = hora do servidor, campos fixos; convidado só registra o que a permissão deixa (`Ciclo` com `ciclo`, `Atuador` com algum `chN`, o resto só `total`). 75 testes no emulador (`scratchpad/rt/test.js`). O firmware continua gravando a própria linha "(remoto)" do atuador (confirma que o aparelho aplicou); a linha do app diz quem pediu. Logs no PWA: 1ª linha = hora + tipo + 👤 e-mail (âmbar "convidado"); 2ª linha = mensagem com a largura toda (quebra de linha); busca também acha pelo e-mail; filtros novos Comando / Automação / Compartilhamento.
**Hardware (PWA):** rótulos/opções mais curtos ("?" explicam), grade `auto-fit minmax(100px)` (4 colunas no desktop, 2 no celular), `min-width:0`/`width:100%` nos campos (o select "Mecânico (módulo de relé comum)" e a Potência estouravam o cartão). Verificado no navegador: 0 campos fora do cartão em 943 px e em 375 px (iframe).
**E-mail "HiveMQ Cloud Serverless será desativado em 31/12/2026":** lido o original na caixa seu-email@exemplo.com (a cópia encaminhada está na conta do convidado, que não acessei). Remetente `app@hivemq.com` (mesmo domínio dos e-mails reais "Welcome to HiveMQ" de 07/04/2026, `cloud@hivemq.com`, e das newsletters `events@hivemq.com`), links de rastreio do HubSpot (usado pela HiveMQ), nada pede senha/pagamento. Conclusão: aviso legítimo; **o projeto não usa HiveMQ** (sem referência no firmware; só um comentário/bypass no service worker). Nenhuma ação necessária; opcional apagar o cluster/conta HiveMQ não usado, entrando direto em console.hivemq.cloud (sem clicar nos links do e-mail). Não foi possível checar SPF/DKIM (a ferramenta só mostra o conteúdo).
**Estado:** PWA v30 + regras publicados; firmware sem mudanças nesta fase (Fase 88 ainda é a versão a gravar se a COM8 permitir; o log mostra que o firmware com `cmd.convite` já está gravado). Backups `*.pre89.bk`.

## Nota 09/10 — bomba (CH3) liga DURANTE A GRAVAÇÃO = hardware; ADXL345 chegou
**Bomba ligando ao gravar:** o usuário viu o CH3 ligar de novo durante a gravação (firmware Fase 88, que já ignora o CH3 vindo da nuvem). Conferidos 700 logs (07/10 21:02 → 09/10): os únicos "Bomba Agua -> ON" são os testes manuais de 08/10 08:48–08:52 (cada um cortado em 8 s); **nenhum "ON" no horário das gravações** (nem em 08/10 ~15:40, o 1º incidente). O firmware não mandou ligar → a causa é o pino do CH3 (GPIO38) "solto" enquanto o ESP32 está no bootloader/ROM de gravação (antes de `initRelays()` rodar) e o módulo de relé aceitando isso como nível baixo. Software não resolve. Correções físicas: resistor de pull-up de ~10 kΩ entre IN3 e **3V3** (nunca 5V: o GPIO só tolera 3,6 V) — idealmente em todos os INs (CH1 resistência também é 220 V) — e, até lá, **tirar a bomba (e a resistência) da tomada antes de gravar**. Teste para confirmar (bomba fora da tomada, olhando o LED do CH3 no módulo): apertar RST, e depois gravar; se o LED acende só na gravação = pino flutuando. O que foi corrigido na Fase 87 continua valendo (nuvem não liga/restaura o CH3; tela atualiza; opção Hardware não reverte), mas eram problemas diferentes deste.
**ADXL345 (recebido):** módulo azul com regulador (SOT-223) e pinos SCL SDA SDO INT2 INT1 / 5V 3V3 GND VS? CS (rótulo "VS" duvidoso na foto). I2C 0x53 (SDO=GND) no MESMO barramento do SHT31 (0x44) e do DS3231 (0x68, e EEPROM 0x57 do módulo RTC): SDA=GPIO8, SCL=GPIO9. Ligar: 5V (do 5V do DC-DC) no pino 5V do módulo OU 3V3 no 3V3 (só UM), GND, SDA, SCL, **CS ao 3V3 do módulo (CS alto = modo I2C)**, **SDO ao GND**; INT1/INT2 livres. Não precisa esperar o MCP23017 (também I2C, 0x20, mesmo barramento; PWM/tacômetro dos fans devem ficar em GPIO do ESP32, não no MCP). Firmware ainda NÃO tem driver do ADXL345 (a implementar: leitura 10 Hz, inclinação/vibração, detecção de tampa).
**Compras a caminho (3–24/out):** módulos IRF520 (backlight/fans), MH-Z19B (CO2, UART + 5 V) e um módulo I2C (provável MCP23017).

## Fase 90 — esquemas de ligação (ADXL345) + coleta de dados de acesso do PWA (segurança/LGPD) (2026-10-09)
**Esquemas (imagens):** `docs/esquemas/01_ligacao_ADXL345.png` (ADXL345 no barramento I2C com RTC DS3231, SHT31, DC-DC, GND/5V comuns; tabela de ligação; montagem na tampa) e `02_visao_geral_sistema.png` (ESP32, I2C, SPI display/touch/SD, módulo de relé e cargas CH1–CH8, itens futuros tracejados). Gerados por `docs/esquemas/gerar_esquemas.py` + `converter.sh` (SVG → PNG via Chrome headless). As ligações dos itens já instalados vêm deste CLAUDE.md (pinout); a alimentação exata do RTC/SHT31 (3V3) é suposição a confirmar. O usuário fixará o ADXL em massa pesada em cima da tampa (detecção de abertura por inclinação) — driver ainda NÃO existe no firmware.
**Acessos (PWA, `acessos.js` novo):** `chocadeiras/{id}/acessos/{auto}` = `{evento: abertura|novo_dispositivo|negado|convite_aceito|saida, ator(e-mail), uid, papel, deviceId(aleatório do navegador), dispositivo{plataforma,navegador,instalado,idioma,fuso,tela}, novo, detalhe, timestamp(servidor)}`. **Não coleta** IP, localização, contatos, publicidade nem impressão digital. Regras (`acessoValido`, 97 testes no emulador): só grava sobre si mesmo (e-mail verificado = ator, uid, hora do servidor, campos fixos); dono lê/apaga tudo; cada pessoa lê (com filtro `uid`) e apaga só o seu; ninguém edita. Gravação: `abertura` ≤ 1 a cada 30 min por aparelho; `novo_dispositivo` na 1ª vez do aparelho para a conta; `negado` (permission-denied em update/set/delete, ≤ 1 a cada 20 s); `convite_aceito`; `saida`. **Aviso de transparência** (modal na 1ª vez por conta). **Cartão "Acessos e privacidade"** em Config.: carregar sob demanda (150 docs), resumo (aparelhos/novos/bloqueios), filtro, CSV, apagar tudo (dono) / apagar meu histórico (convidado), retenção de 90 dias (o dono apaga >90 dias ao abrir). **Alertas:** dono recebe aviso no app (listener `limit(1)`, ~1 leitura por evento) de "novo aparelho acessou" e "ação bloqueada" de outros; aviso no celular via firmware (`cmd.seg = "<ms>|<email>|novodisp|negado"` → log "Seguranca" + push; ≥ 3 bloqueios em 10 min). Usado `cmd.seg` (não `cmd.convite`) para o firmware antigo não confundir com "convite aceito". Limite conhecido: sem servidor não dá para revogar sessões Google nem capturar IP; "novo aparelho" é por navegador (limpar dados do navegador gera novo aviso).
**Estado:** PWA v32 + regras publicados e testados com a conta do dono (aviso, cartão, registro, lista, evento "negado" via wrapper; registro de teste apagado). Firmware compilado com o tratamento de `cmd.seg` (RAM 32,3 %, Flash 32,7 %), **NÃO gravado** — até gravar, os avisos de aparelho novo/bloqueio só aparecem dentro do app. Observação: durante o teste, o PWA ainda escreveu 1× `cmd.convite` (antes da troca para `cmd.seg`): o firmware gravado pode ter mostrado um push falso "aceitou o convite" com o e-mail do próprio dono. Backups: `*.pre90.bk`, `main_pre_fase90.bk`.

## Nota 09/10 (2) — ADXL345: esquemático do fabricante CONFIRMA as ligações (corrige recomendações anteriores)
Esquemático e tabela do anúncio (módulo com regulador U2 ME6206-33): J1 = 5V (entrada externa 5 V) · 3V3 (**SAÍDA** 3,3 V) · GND · VS (pé no ar) · CS (ligar ao pino 3V3, como no exemplo do fabricante); J2 = SCL · SDA · SDO (pé no ar) · INT2 · INT1 (rótulos IN2/IN1). Internamente: R2/R3 10 kΩ pull-ups SCL/SDA em 3,3 V; **R4 0 Ω prende o SDO ao GND (endereço 0x53)**; **R5 0 Ω liga Vs ao 3,3 V**; R6 10 kΩ puxa CS a 3,3 V; D1+R1 = LED de energia. Portanto: **SDO e VS ficam LIVRES** (não ligar SDO ao GND nem a nada; ligar SDO ao 3V3 curto-circuitaria o 3,3 V no GND pelo R4); **não** existe "alimentação alternativa pelo 3V3 do ESP32" (o pino 3V3 do módulo é saída do regulador); o teste do VS com multímetro não é mais necessário. Ligação final: 5V ← +5 V do DC-DC, GND, SDA→GPIO8, SCL→GPIO9, CS→pino 3V3 do módulo; I2C 0x53. Imagem atualizada: `docs/esquemas/01_ligacao_ADXL345.png`.

## Fase 91 — driver de diagnóstico do ADXL345 (compilado, NÃO gravado) (2026-10-09)
Usuário instalou o módulo numa caixinha preta **na vertical** (aprovado: qualquer posição serve se fixa; recalibrar ao mover). `adxlInit()` (no `setup()` depois do SHT31): varre o I2C (log `[I2C] dispositivos: 0x44 0x53 0x68…`), procura DEVID 0xE5 em 0x53/0x1D, configura 100 Hz / resolução total ±2 g / medição; `adxlTick()` lê a 10 Hz (EMA 0,3); teclas seriais `A` (x/y/z, |a| e desvio em graus da posição fechada), `K` (calibra a posição atual como "fechada", salva em `/adxl.cfg`), `I` (varre I2C e reinicia o ADXL). Ainda **não comanda nada** — a detecção de tampa aberta será ligada ao `tampaTick` depois de validar a leitura e saber como a tampa abre (gira na dobradiça / levanta reto / desliza). Backup `main_pre_fase91.bk`. RAM 32,3 %, Flash 32,8 %.


## Fase 92 — sem internet: relogio pelo RTC, Firebase se recupera, ciclo local, ADXL ao vivo (2026-10-09)
- **Incidente:** WiFi ligado, internet fora. NTP falhou, relogio em 1970, Firebase "JWT time was not set" e a biblioteca (FirebaseApp::setTime) CONTA a partir da hora recebida: nunca se corrigia sozinha. Tambem: tela inicial sem ciclo (o ciclo so vinha do poll da nuvem) e o aquecedor entrou em modo "manter".
- **Relogio:** `relogioDoRtcAplicar()` (setup, depois do RTC; so se `rtcHoraConfiavel`=sem lostPower e ano>=2024) poe o relogio do sistema pelo RTC (`TZ_OFFSET_SEC`). Callback SNTP `ntpNotifCb` -> `ntpSincronizou`. `ntpWatchdog` (30 s) continua tentando enquanto `relogioDoRtc`; quando o NTP chega: `syncRtcFromNtp()` + `fbApp.setTime(time(nullptr))`. `syncTimeAndStartFirebase` espera o callback (nao getLocalTime).
- **Ciclo local:** `/ciclo.cfg` (ativo, pausado, preparo, volL, iniciadoEm, pausadoEm, duracao, nome, tipo). `cicloRestaurar()` no setup; `cicloPersistTick()` no loop grava so quando a assinatura muda. O poll da nuvem continua mandando (`cicloPollRecebido` fica false ate a 1a resposta).
- **Aquecedor:** `partidaIncerta` em `autoPidTick`: nos primeiros 5 min do boot so liga DIRETO se alvo-T >= 3 C (resistencia ainda quente de antes do boot/gravacao somava calor em transito). Causa provavel do 38,7 C: resistencia ligada pelo pino flutuante durante a gravacao (mesmo mecanismo da bomba) -- NAO confirmado; falta o log de 18:02-18:10.
- **ADXL:** I2C achou 0x44 0x53 0x57 0x68, `[ADXL345] OK (0x53)`, **sem calibracao ainda**. Teclas: A (uma leitura), **L (liga/desliga leitura ao vivo 5x/s com barra e "fechada/ABERTA?")**, K (calibra a posicao fechada), I (reinicia).
- **Licao:** abrir a COM8 por programa (PowerShell/pyserial) REINICIA o ESP32 (CH343 DTR/RTS) -> push falso "queda de energia". Nao abrir a serial sem o usuario pedir; so ele, no monitor.
- **Estado:** compilado (RAM 32,3 %, Flash 32,8 %), **NAO gravado**. Antes de gravar: resistencia e bomba fora da tomada. Backup `main_pre_fase92.bk`.


## Fase 93 — tela "Tampa" (ADXL345) no touch + deteccao por movimento (2026-10-09)
- **Uso real:** o modulo fica em cima da tampa como peso e e TIRADO dali para abrir a caixa (nao gira com ela). Por isso a deteccao e por MOVIMENTO (variacao entre leituras > sensibilidade por >= 0,3 s) e POSICAO (fora da tolerancia, parado). Estados: 1 sem calibracao · 2 FECHADA · 3 MOVENDO · 4 ABERTA. Volta a FECHADA parado >= 4 s dentro da tolerancia; 5 s de carencia depois de fechar/calibrar/ligar.
- **Ligacao ao ciclo:** `adxlEvento()` poe `tampaCmd=1/2` (so se `adxlDet` ligado e o `tampaTick` permitir: ciclo em andamento fora do preparo, ou modo manter). `tampaOrigemAdxl` troca o texto do log ("sensor ADXL: modulo retirado ou movido"). Aviso manual na tela do Status continua valendo. Limite: modulo pousado noutro lugar com a mesma inclinacao so se separa pela tolerancia; o `tampaTick` tambem fecha pela temperatura.
- **Tela:** Configuracoes pag. 2 > "Tampa" (id 17, `S_ADXL`): cartao ao vivo (estado, graus, x/y/z, barras de posicao e movimento com marca do limite, 5x/s), linhas - / + (Detectar abertura Ligado/Desligado · Tolerancia 1-30 graus · Sensibilidade 20-300 mg, passo 10) e botao CALIBRAR POSICAO FECHADA. A tela nao deixa o protetor de tela entrar. Serial (A/K/I/L) continua, opcional.
- **Persistencia:** `/adxl.cfg` linha 1 = referencia (ou `-`), linha 2 = `det,tol,sens`. Padroes: deteccao DESLIGADA, 5 graus, 60 mg.
- **Estado:** compilado (RAM 32,3 %, Flash 32,9 %), **NAO gravado**; layout calculado a mao (nao visto no display): conferir ao gravar. Backup `main_pre_fase93.bk`.


## Fase 94 — bomba de ar (CH4) automatica contra umidade alta + logs por dia no PWA (2026-10-10)
- **Dados (Firestore `leituras`/`logs`, lidos pela sessao do dono no PWA):** 09/10 a umidade subiu de 50 % (20:00) a 58 % (21:28) com o umidificador em duty 0 % (a fonte e o ar de fora/chuva, nao o aparelho) e T estavel 37,5 C. Usuario ligou o CH4 21:29-21:42 (13 min; cooler junto): 58 -> 56 (21:38) -> 55 (21:43), ~-0,2 pt/min contra +0,09 pt/min antes; nao voltou a subir (55-56 ate 22:03, 54 depois). Sem a bomba, a umidade desceu sozinha de madrugada (55 -> 51 ate as 09:00). Excursoes > 55 % nos ultimos dias: 6/10 15 leituras, 7/10 23, 8/10 10, 9/10 10 (leituras de 5 min; picos 57-58 %). Fisica: ar da sala aquecido a 37,5 C fica bem mais seco que a caixa (umidade absoluta menor), entao ventilar quase sempre seca, exceto chuva forte. 1 experimento, sem controle: efeito plausivel e modesto, nao prova.
- **Configuracao aplicada (sem mudar firmware):** `cmd.auto` do doc da chocadeira, so o 4o canal: `4,1,520,20,120,60,480,1080,0` = modo Desumidificar/ventilar (`AM_DESUMIDIFICAR`), alvo personalizado 52 %, histerese 2 pts -> liga acima de 54 %, desliga em <= 52 %. O umidificador (PI, alvo 50) fica separado por 2 pts. Valem as travas da Fase 83: cooler junto, maximo 20 min seguidos, 'ventilar nao ajuda' (>= 4 min ligado, umidade > maximo e ainda subindo) bloqueia 30 min, so com ciclo/manutencao, pausa 5 min apos toque manual. Reverter: PWA > Automacao > CH4 > Manual (ou serializar o canal como `0,0,375,2,120,60,480,1080,0`). Log "Automacao" registrado com o e-mail do dono.
- **PWA (v33):** Config. > Logs ganhou seletor de DIA (consulta por intervalo, ate 300 eventos, 1 leitura sob demanda, botao x volta aos mais recentes). Causa de "nao vejo os logs de ontem": o listener ao vivo traz so os 60 mais recentes e o firmware grava resumos/alertas a cada 15 min, entao ontem ficava fora do lote. Confirmado em producao: 09/10 = 283 eventos.


## Fase 95 — tampa automatica de verdade: assinatura de vibracao, calibracao FORA, Status sem o botao enorme (2026-10-10)
- **Relato:** o modulo tirado da caixa e pousado em qualquer superficie plana fazia o aparelho "entender" que a tampa estava fechada; o rotulo "sensor ok" aparecia mesmo parecendo nao funcionar; o usuario queria o Status automatico, sem o botao "Vou abrir a caixa". Nos logs de 10/10 a deteccao foi ligada 10:48/10:51 e nao ha NENHUM evento "Tampa aberta (sensor ADXL)" (ciclo t7 ativo, fora do preparo). Causa de fundo: a gravidade e a mesma em qualquer superficie plana -> angulo sozinho nao separa tampa x mesa (limite que ja estava descrito na Fase 93).
- **Solucao (firmware):** (1) **vibracao** em mg (|leitura - baseline lento 0,25|, EMA ~1 s): sobre a caixa em funcionamento (fans/bomba/rele) o modulo treme, na mesa nao; e gravada na calibracao. (2) segunda calibracao **FORA** (posicao+vibracao na mesa). (3) `adxlAvalia()`: `dentro` = angulo <= tolerancia E mais parecido com FECHADA do que com FORA; `confiavel` = FECHADA com vibracao >= 12 mg, ou FORA calibrada e separada (>= 1,5 graus ou vibracao distinta). (4) **FECHADA so e declarada (evento + `tampaCmd=2`) quando `confiavel`**; sem isso o estado fica ABERTA e o fechamento vem do `tampaTick` (temperatura voltando a subir) — o aparelho nao inventa que fechou. (5) Se a caixa ja esta normal (`tampaEst==0`) e o modulo parado `dentro` ha 30 s, adota FECHADA sem evento. (6) **Alarme falso:** abertura pelo sensor sem queda de T/U (sT <= -0,3 C/min ou sH <= -2 %/min) em 4 min e descartada (`tampaTick`, log "Alarme falso do sensor ADXL").
- **Saude do sensor:** rotulo do topo da tela Tampa so diz "sensor ok" com leitura viva e |a| ~ 1 g; senao "leitura travada" (3 eixos identicos por 10 s -> reafirma POWER_CTL/DATA_FORMAT/BW_RATE, que tambem sao reafirmados a cada 10 s) ou "|a| fora de 1 g". Eventos do sensor sem efeito viram log "Sensor" (<= 1/20 s).
- **Status automatico:** `adxlAutoAtivo()` (deteccao ligada + FECHADA calibrada + sensor vivo). No aparelho a janela do Status troca o botao enorme por um cartao fino "Tampa - automatica (sensor ADXL): FECHADA/ABERTA" + botao manual pequeno. O heartbeat leva o campo final `A` em `ambiente` (`modo;seg;tot;diag;A`); o PWA (`statusAmbInfo().tampaAuto`) mostra uma nota e um botao pequeno "Avisar manualmente". PWA v34.
- **Tela Tampa:** 3 barras (posicao, movimento, vibracao com marca branca = FECHADA e laranja = FORA), `vib N mg` na linha de x/y/z, dois botoes CALIBRAR FECHADA / FORA (exigem 3 s parado). `/adxl.cfg`: linha 1 `x,y,z,vib`|`-`, linha 2 `det,tol,sens`, linha 3 FORA `x,y,z,vib`|`-` (arquivo antigo de 3 valores ainda carrega).
- **Estado:** compilado (RAM 32,3 %, Flash 33,0 %), **NAO gravado**; PWA v34 em producao (parser testado com 5 strings). Backup `main_pre_fase95.bk`. Limite que continua: se tampa e mesa tiverem a mesma inclinacao E a mesma vibracao, o fechamento so vem da temperatura (ou do botao manual) — a tela avisa "Fechar so pela temperatura: calibre FORA".


## Fase 96 — "trancos" do sensor viravam tampa aberta; cronometro da recuperacao parado (2026-10-10)
- **Relato (tela Tampa):** a cada 1:15-3:30 min as barras iam ao limite e voltavam, FECHADA -> MOVENDO -> FECHADA; isso fazia o Status do ambiente entrar em "tampa aberta" e depois "recuperando", e o cronometro da recuperacao nao andava.
- **Causa provavel (picos curtos periodicos):** clique de rele (resistencia/umidificador/cooler/bomba de ar) -> choque na caixa e/ou EMI no I2C. A Fase 93/95 anunciava a abertura apos so 3 amostras (0,3 s) acima do limiar, e a "recuperacao" vinha do `tampaCmd=2` quando o modulo voltava. NAO confirmado qual rele; ver `ign` na tela.
- **Correcoes (firmware):** (1) **mediana de 5 amostras** nas leituras de posicao/movimento (tira picos de 1-2 amostras); a vibracao continua pelo bruto. (2) **Episodio de movimento**: a abertura so e anunciada (evento + `tampaCmd=1`) quando o movimento dura >= 3 s, ou quando o modulo assenta FORA da posicao; um tranco que termina com o modulo `dentro` (posicao+assinatura) e descartado ("tranco de N ms ignorado") e conta em `adxlIgnorados` (tela: `ign N`). (3) `tampaTick`: se a abertura veio do sensor, nunca houve queda de T/U e o modulo voltou, **nao entra em "recuperando"** (log "Alarme falso do sensor ADXL ... sem recuperacao"). (4) Status do aparelho: o cronometro de tampa aberta/recuperando passou a fazer parte da assinatura de redesenho (antes so atualizava quando outra coisa mudava).
- **PWA (v35):** o cronometro da tampa/recuperacao interpola entre os avisos do aparelho (~1 min) e redesenha a cada 1 s; `ambRecebidoEm` so e reiniciado quando o aparelho publica um valor novo de `ambiente` (antes qualquer escrita no documento zerava a interpolacao, inclusive a do "Aguarde").
- **Estado:** firmware compilado (RAM 32,3 %, Flash 33,0 %), **NAO gravado**. Backup `main_pre_fase96.bk`.


## Fase 97 — assinatura fraca (tampa ~ mesa): mensagem errada, abertura fantasma e demora para voltar (2026-10-10)
- **Fotos do usuario:** tela Tampa com FECHADA e FORA ja calibradas mostrava "Fechar so pela temperatura: calibre FOR(A)" (texto cortado e enganoso); vibracao calibrada FECHADA ~9 mg e FORA ~5 mg (marcas branca/laranja), angulo 0,0 graus -> `confiavel` falso (exige vib >= 12 mg ou |ln((vc+3)/(va+3))| >= 0,5 ou separacao angular >= 1,5 graus). Status mostrava "Tampa aberta ha 2:10" com T 37,6 / U 53 % (caixa NAO aberta); sensibilidade estava em 30 mg (limiar muito proximo do ruido+vibracao: raw |delta| ~8-20 mg).
- **Causas:** (1) mensagem nao distinguia "FORA nao calibrada" de "calibrada, mas tampa e mesa parecidas"; (2) com assinatura nao confiavel o ADXL nunca emite "fechada" e a abertura (episodio de movimento >= 3 s) ficava ate `quedaVista` (4 min) / 15 min -> abertura fantasma longa; (3) deadlock: o ADXL so adota FECHADA com `tampaEst==0` e o `tampaTick` so zera `tampaEst` por temperatura ou pelos 4 min.
- **Correcoes:** (a) mensagem agora mostra "sep X.Xgr vib A/Bmg: so temperatura" (separacao e vibracoes medidas) ou "Calibre FORA para o sensor separar"; (b) sem assinatura confiavel o episodio precisa durar **>= 15 s** (ou o modulo assentar FORA do lugar) para anunciar abertura — com assinatura confiavel continua 3 s; (c) `tampaTick`: abertura de origem ADXL + nenhuma queda de T/U + modulo `adxlAssentado()` (parado 8 s dentro da tolerancia) + >= 30 s aberta -> cancela como alarme falso e chama `adxlTampaCancelada()` (volta a FECHADA); fallback de 4 min mantido para o caso do modulo nao assentar.
- **Limite fisico:** nesta caixa a vibracao da tampa (~9 mg) e da mesa (~5 mg) e baixa e a inclinacao e quase igual -> o fechamento so vem da temperatura (ou do botao manual). Para separar: um modulo apoiado onde sinta mais vibracao (ex. encostado na parede da caixa/perto da fan, ou fixo ao corpo da caixa e nao a tampa) ou uma superficie de apoio de mesa inclinada (>= 2 graus) para a calibracao FORA.
- **Estado:** compilado (RAM 32,3 %, Flash 33,1 %), **NAO gravado**. Backup `main_pre_fase97.bk`.


## Fase 98 — tela "Tampa" ao vivo no PWA + atalhos do menu + Acessos com carregamento automatico (2026-10-11)
- **Acessos e privacidade "sem dados":** havia 7 registros em `acessos` (2 aparelhos, 09-11/10) — o cartao so carregava ao tocar em "Ver acessos". Agora `acAutoCarregar()` carrega ate 50 registros uma vez por sessao ao abrir Configuracoes; o botao virou "Atualizar (ate 150)". **Menu (hamburguer)**: novos atalhos "Usuarios compartilhados", "Tampa (sensor ADXL)" e "Acessos e privacidade" (`irParaCartaoConfig`: abre Configuracoes, rola ate o cartao e o destaca por 2,4 s). Caminho manual: ☰ > Configuracoes > rolar ate "Usuarios compartilhados" e "Acessos e privacidade".
- **Tela "Tampa" (PWA, `tampa.js`, `#screen-tampa`):** cena SVG animada (a tampa levanta quando `tampaEst==1`; o modulo balanca em MOVENDO e vai para a mesa em ABERTA; ondas de vibracao; brilho por estado), medidor de posicao com arco de tolerancia, barras de movimento/vibracao com marcas FECHADA (branca) e FORA (laranja), grafico dos ultimos ~2 min, pilulas (saude do sensor, assinatura confiavel/tampa~mesa, trancos ignorados, status automatico/manual) e controles remotos: detectar abertura, tolerancia, sensibilidade, CALIBRAR FECHADA / FORA. Convidados sem `total` veem so leitura.
- **Protocolo (firmware Fase 98):** aparelho -> app: campo `adxlLive` = `v1|up|est|ang10|mov|vib|saude|det|tol|sens|temRef|temAw|vibRef|vibAw|ign|conf|dentro|sep10|auto|tampaEst|msgSeq|msg`, publicado a cada 2,5 s SO enquanto `cmd.adxlVivo` (renovado a cada 40 s pela tela aberta) for recente (<= 90 s). app -> aparelho: `cmd.adxlCfg="<ms>|det|tol|sens"` e `cmd.adxlCal="<ms>|F"` (fechada) | `"<ms>|O"` (fora), processados em `adxlComandosTick()` (loop, fora de callback; a 1a leitura pos-boot so memoriza). Custo: 1 escrita/2,5 s com a tela aberta; 0 com ela fechada.
- **Atalho no Status (PWA):** o campo do estado da tampa na janela do Status virou botao (`.tp-atalho`: "Tampa fechada/aberta ha m:ss · automatica (sensor ADXL345) · toque para ver o sensor ao vivo ›") que abre a tela Tampa.
- **Estado:** firmware compilado (RAM 32,4 %, Flash 33,1 %), **NAO gravado** — a tela do PWA so mostra telemetria depois de gravar (ate la fica "aguardando"). PWA v36/v37 em producao (testado com telemetria simulada: FECHADA, MOVENDO, ABERTA + tampa aberta, atalho do Status, sem escrever no Firestore). Backup `main_pre_fase98.bk`.

## Fase 99 — MH-Z19B (CO2) e PWM dos IRF520 PREPARADOS (nenhum hardware instalado) (2026-10-11)
- **CO2 (MH-Z19B):** UART1 9600 8N1, **RX=GPIO16 (<- TX do sensor), TX=GPIO15 (-> RX do sensor)**, Vin 5 V, GND. Le a cada 5 s (`FF 01 86 ... 79`, checksum conferido), ignora os primeiros 3 min (aquecimento) e valores fora de 300-10000 ppm, **desliga o ABC uma vez** (`FF 01 79 ... 86`; caixa fechada com ovos quase nunca volta a 400 ppm). Preenche `co2Atual`/`lastCo2OkMs` (a regra avancada com CO2 passa a funcionar) e o heartbeat leva `co2Atual` (o card CO2 do PWA mostra). Sem sensor: so uma linha no Serial ("sem resposta ... confira 5 V, GND e TX/RX cruzados"). **Conflito consciente:** GPIO15/16 estavam reservados ao tacometro dos fans — o tacometro fica so no GPIO17 (1 fan) ou via MCP23017; gravar "0" em `/co2.cfg` libera os pinos.
- **PWM (IRF520):** fans GPIO4/5/6 e backlight GPIO47, 1 kHz, 8 bits, `/pwm.cfg` = `ativo|fan1|fan2|fan3|bl` (%). **Desligado por padrao** (nenhum pino tocado); tecla serial `P` liga/desliga; API `pwmSet(i,pct)` (i 0-2 fans, 3 backlight) para a futura tela/automacao. Aviso: o IRF520 nao e logic-level, em 3,3 V muitos modulos conduzem mal; medir a tensao na carga a 100 %.
- **Esquema do pull-up:** `docs/esquemas/03_pullup_rele.png` (`gerar_pullup.py`; `converter.sh` agora gera os 3): 3V3 -> COMUM da rede SIP 10 kΩ, cada resistor -> fio GPIO->IN. Minimo util: IN1 (resistencia) e IN3 (bomba).
- **Estado:** compilado (RAM 32,4 %, Flash 33,3 %), **NAO gravado**. Backup `main_pre_fase99.bk`.

## Fase 100 — chegaram IRF520, MCP23017 e MH-Z19**C** (2026-10-14); esquema do IRF520
- **Fotos do usuário (anúncios do ML sem informação útil):** (1) módulo **IRF520 "MOS Module" marca MH**, placa vermelha, cabeçalho SIG/VCC/GND, bornes azuis U+ U− (saída p/ carga) e VIN GND (fonte), 2 resistores 102 (1 kΩ) + LED; anúncio "5 pcs". (2) **MCP23017 Waveshare "IO Expansion Board"**: conector de 6 vias (VCC GND SDA SCL INTA INTB, cabo incluso), pinos PA0–PA7/PB0–PB7, solda A0/A1/A2 (padrão 0x20), 3,3/5 V, já traz resistores 10 kΩ (103). (3) **sensor etiquetado "MH-Z19C 400-5000PPM"** — NÃO é o B: mesmo protocolo UART (0xFF 0x01 0x86, 9600 8N1, nível 3,3 V, alimentação 5 V; faixa 400–5000 ppm), mas o cabo tem 7 fios coloridos (amarelo, verde, azul, rosa, preto, branco, marrom) e **nenhuma fonte achada mapeia cor→pino** (datasheet Winsen só mostra as versões com pinos: HD SR Tx Rx Vo | PWM AOT GND Vin). Identificar com multímetro/continuidade antes de energizar (5 V só em Vin).
- **Esquema entregue:** `docs/esquemas/04_irf520_pwm.png` (`gerar_irf520.py`; `converter.sh` agora gera 4): GPIO4 → SIG, GND comum, VCC do cabeçalho SOLTO, fan em U+/U−, fonte 12 V em VIN/GND, conferências com multímetro e teste de tensão (≥ 11 V a 100 %). Suposição a conferir por continuidade: U+ = VIN (ligado) e o MOSFET chaveia o lado negativo (U−). IRF520 não é lógico: pode não saturar em 3,3 V.
- **Pendente:** esquema do MCP23017 (I2C 0x20 no SDA=GPIO8/SCL=GPIO9, sem driver ainda) e do MH-Z19C (após mapear os fios); firmware das Fases 92–99 continua NÃO gravado.

## Fase 101 — brilho do backlight por PWM (GPIO47 -> pino LED do display), SEM IRF520 (2026-10-14)
- **Foto do verso do display (módulo ILI9341 vermelho):** perto do pino LED há **Q1 (SOT-23, marcação "J3Y" ≈ S8050 NPN)**, **R6 "102" (1 kΩ)** ligando o pino LED à base, **R5 "6R2" (6,2 Ω)** como limite de corrente do backlight, J1 (jumper de solda) e U1 (regulador 3,3 V "662K"). Usuário: com tudo desligado não há continuidade LED↔GND (consistente com base de transistor via resistor); o pino LED hoje está no +5 V do DC-DC, por isso o backlight fica sempre aceso. **Conclusão:** o pino LED é uma ENTRADA LÓGICA (alto = acende); o IRF520 NÃO é necessário para o backlight — basta GPIO47 direto no pino LED (3,3 V → ~2,6 mA de base, satura Q1 com folga para ~30 mA). Identificação do transistor por marcação: alta confiança, não medida.
- **Firmware:** `blInit()` (início do `setup()`, LEDC canal 4, 5 kHz, 8 bits, 100 %), `brilhoPct` 10–100 % (mínimo 10 % para nunca apagar a tela), persistido na 3ª linha de `/display.cfg`; Configurações > **Tela** ganhou o cartão "Brilho da tela" (− / + passo 10 %) e as linhas de wallpaper encolheram (`TELA_ROW_H` 72→66). `pwmSet(3,%)` agora ajusta o backlight; os fans (IRF520) ficam só nos índices 0–2 (GPIO4/5/6).
- **Ordem obrigatória na montagem:** (1) gravar este firmware; (2) DESLIGAR tudo; (3) **retirar o fio do +5 V do pino LED**; (4) ligar o GPIO47 ao pino LED. Nunca os dois juntos (GPIO do ESP32 tolera no máximo 3,6 V). Ligar o GPIO47 ANTES de gravar deixa o pino flutuando (tela escura/oscilando).
- **Estado:** compilado (RAM 32,4 %, Flash 33,3 %), **NÃO gravado**. Backup `main_pre_fase101.bk`.
- **Fase 101b — screensaver reduz o backlight (2026-10-14):** ao entrar no screensaver o PWM cai para `min(brilho, BL_REPOUSO_PCT=20 %)` (`blRepouso`); `exitScreensaver()` restaura ANTES de desenhar (inclusive a tela de PIN) e o `loop()` tem rede de segurança (fora do screensaver o brilho nunca fica reduzido). "Manter tela ligada" continua sem screensaver. Constante fixa (não editável pela tela). Compilado, NÃO gravado. Backup `main_pre_fase101b.bk`.
- **Por que não foi feito antes:** não foi questão de autorização — até a foto do verso do display (Fase 101) o backlight estava ligado a um trilho fixo de 5 V e o plano (seção 9) dependia de comprar o módulo IRF520 (SIG→GPIO47); o screensaver (tela preta/wallpaper escurecido) foi a solução provisória (seção 7). A foto mostrou que o pino LED já é entrada lógica de um transistor da placa.

## Fase 102 — tela "Fans" (PWM dos IRF520) no aparelho (2026-10-14)
- **Relato:** fans ligados em 3 módulos IRF520 (fiação em cabo de alarme, ~1 m, aprovada: + comum e 1 negativo por fan) não giravam; VIN/GND ~12 V, V+/V− sem tensão. **Esperado:** o PWM (Fase 99) é DESLIGADO por padrão (GPIO4/5/6 intocados → gate em baixo → MOSFET aberto → V+/V− ≈ 0 V com o fan ligado) e não havia tela/PWA para ligá-lo (só a tecla serial `P`).
- **Firmware:** Configurações pág. 2 > **Fans** (id 18, `S_FANS`, `fansDraw/fansTap`): chave geral (liga/desliga o PWM, mesmo efeito da tecla `P`) + 3 linhas Fan 1/2/3 (GPIO4/5/6) com barra e − / + de 10 % (0 % desliga; `pwmSet` grava em `/pwm.cfg`). Ligar a chave aplica os % salvos (padrão 100 %). Ponto de estado verde no ícone quando ativo. O backlight segue em Tela (Fase 101).
- **PWA:** ainda NÃO existe card de Fans (protocolo `cmd.fans` a definir; aguardando confirmação do layout com o usuário).
- **Pendente:** relação com o CH8 "Fans" (relé) — se os fans agora são alimentados só pelos IRF520, o CH8 vira só o "estado lógico" usado pelo modo Circular/intertravamento do aquecedor; decidir com o usuário. Compilado (RAM 32,4 %, Flash 33,3 %), NÃO gravado. Backup `main_pre_fase102.bk`.

## Fase 103 — tempos da tela: reduz o brilho e depois apaga (2026-10-14)
- **Pedido:** sem toque por X s o brilho cai; sem toque por mais Y s a tela apaga.
- **Firmware:** `blEstagio` 0 normal · 1 reduzido (`min(brilho, BL_REPOUSO_PCT=20 %)`, a tela continua a mesma) · 2 apagado (backlight 0 %, é o screensaver: relógio/wallpaper não ficam visíveis; o toque acorda, restaura o brilho ANTES de desenhar e segue o PIN). `loop()` calcula o estágio 1 a cada volta: `dimSegundos>0 && ocioso > dimSegundos` (qualquer atividade que atualize `lastActivityMs` restaura). Estágio 2 = `enterScreensaver()` após `screenOffTimeoutSec`. "Manter tela ligada" desliga os dois. Padrões: reduzir em 30 s, apagar em 120 s; `dimSegundos` sempre ≤ apagar − 10 s.
- **Tela (Config. > Tela) reorganizada:** Brilho da tela · Reduzir brilho após (0 = nunca, passo 10 s) · Apagar a tela após (30–600 s, passo 15 s) · Manter tela ligada · "Papel de parede >" (subtela com as 2 linhas de wallpaper; `telaSub`). `/display.cfg`: linhas `apagar`, `manter`, `brilho`, `reduzir`.
- **Atenção hardware:** o usuário escreveu "conectei GPIO43"; o firmware usa **GPIO47** (BL_PIN). GPIO43 é o TX da UART0 (serial/USB) — ficaria sempre em nível alto no repouso (backlight "funciona" sempre aceso, sem controle de brilho) e piscaria com cada mensagem da serial. Confirmar com o usuário qual pino foi ligado.
- **Estado:** compilado (RAM 32,4 %, Flash 33,3 %), NÃO gravado. Backup `main_pre_fase103.bk`.

## Fase 104 — fans de 4 fios (PWM 25 kHz no fio 4), relógio sempre ligado e cartões Tela/Fans no PWA (2026-10-14)
- **Relato:** com o IRF520, V+/V− media ~11,98 V (VIN/GND 14,36 V) e o fan de PC (4 fios, todos os fios pretos) não girava. Pinagem Intel: 1 GND (fio com a listra/marca "1") · 2 +12 V · 3 tacômetro · 4 PWM. **Causa provável:** fan de 4 fios tem driver próprio e quer 12 V FIXO; chavear o + a 1 kHz (e com gate de 3,3 V no IRF520, que perde ~2,4 V) não parte/zumbe. **Novo método:** +12 V no pino 2, GND no pino 1, GPIO4/5/6 → 1 kΩ → pino 4 (PWM 25 kHz, 3,3 V ok), pino 3 livre; IRF520 saem do circuito (reserva). Esquema `docs/esquemas/05_fan_4fios_pwm.png` (`gerar_fan4fios.py`; `converter.sh` gera 5). Fan de 3 fios continua pelo esquema 04.
- **Firmware:** `PWM_FREQ_HZ` 1000 → **25000** (fans); display.cfg ganhou a 5ª linha `relogioSempre`; Tela: nova linha "Relogio sempre ligado (repouso)" — com ela o screensaver entra em `blEstagio=1` (relógio sobre o wallpaper, brilho ≤ 20 %) em vez de 2 (backlight 0 %); `Manter tela ligada` continua desligando reduzir/apagar. **Protocolo PWA:** heartbeat leva o campo único `painel` = `T|brilho|reduzirSeg|apagarSeg|manter|relogio#F|ativo|f1|f2|f3` (um campo só, para manter a máscara do PATCH curta); app→aparelho `cmd.telaSet="<ms>|brilho|reduzirSeg|apagarSeg|manter|relogio"` e `cmd.fansSet="<ms>|ativo|f1|f2|f3"` (parse no callback, trabalho em `telaComandosTick()` no loop; a 1ª leitura pós-boot só memoriza; log "Automacao").
- **PWA (v38, `painel.js` novo):** Configurações → cartões "Tela do aparelho" (brilho, reduzir após, apagar após, relógio sempre ligado, manter ligada) e "Fans (velocidade)" (chave PWM + 3 sliders). Aplica sozinho 0,7 s após mexer; controles ficam desabilitados até o aparelho publicar `painel` (firmware novo gravado) ou sem permissão `total`. Zero leituras novas. Testado em página isolada (parser, limites reduzir ≤ apagar−10, envio dos 2 comandos, esmaecer com "manter ligada", mobile 375 px). Deploy feito.
- **Hardware:** o usuário confirmou o backlight no **GPIO47** (o "43" foi erro de digitação).
- **Estado:** firmware compilado (RAM 32,5 %, Flash 33,4 %), **NÃO gravado**; Fases 92–104 acumuladas. Backups `main_pre_fase104.bk`, `frontend/*.pre104.bk`.

## Fase 105 — tacômetro (RPM) do fan 1 no GPIO17, aparelho e PWA (2026-10-14)
- **Relato/decisões:** com 0 % o fan de 4 fios continua girando (mínimo exigido pelo padrão Intel: 20–30 %) — para parar de verdade é preciso cortar o +12 V (relé CH8 "Fans" no contato NA; opção futura: desligar o CH8 quando os 3 fans estiverem em 0 %). O IRF520 não é necessário com fans de 4 fios (nem para o backlight); a recomendação anterior veio do plano original (seção 9) sem eu perguntar o tipo de fan.
- **Firmware:** `TACH_PIN 17` (fio 3 do fan 1, coletor aberto, `INPUT_PULLUP`; recomendado pull-up externo de 10 kΩ ao 3V3, NUNCA a 12 V), interrupção FALLING com filtro de 1,5 ms, janela de 2 s, 2 pulsos/volta → `fanRpm` (−1 = nunca houve pulso = "sem sinal"; 0 = parado). Tela Fans: a linha do fan 1 mostra "NNNN rpm"/"sem sinal" (atualiza a 1 Hz só quando muda). `painel` ganhou `#R|rpm` no final. **Ao vivo:** `cmd.fansVivo` (PWA com o cartão Fans visível, renovado a cada ≥35 s) liga `fansPublicaTick()` = 1 escrita de `painel` a cada 5 s por ≤ 90 s; sem isso o rpm segue o heartbeat (~1 min).
- **PWA:** cartão "Fans (velocidade)" mostra o rpm do fan 1 ao lado do nome (verde quando > 0; "sem sinal do tacômetro"; "—" com firmware antigo). O pedido ao vivo só é feito por dono/permissão `total` (evita registro de "negado" nos acessos). IntersectionObserver não dispara no painel embutido do Claude → usei `offsetParent` + `visibilityState`.
- **Limite:** só 1 tacômetro (GPIO17 é o único pino livre; GPIO15/16 = CO₂; strapping não usar). Fans 2/3: MCP23017 (INTA) ou realocar o CO₂.
- **Estado:** firmware compilado (RAM 32,7 %, Flash 33,5 %), **NÃO gravado**; PWA publicado. Backup `main_pre_fase105.bk`, `painel.js` novo na v38 do cache.

## Fase 106 — "desligar" no PWA não pegava durante manutenção + RPM inventado com o fio solto (2026-10-14)
- **Relato:** (a) o RPM do fan 1 já contava antes de ligar o fio 3 ao GPIO17; (b) em manutenção, desligar resistência (CH1) e bomba de ar (CH4) pelo PWA "não fazia nada".
- **(a) Causa:** pino solto com `INPUT_PULLUP` fraco capta ruído (inclusive o PWM de 25 kHz dos GPIO4/5/6 vizinhos); o filtro de 1,5 ms deixava passar centenas de "pulsos". **Correção:** filtro 3 ms + janela só vale com ≥ 4 pulsos, intervalos regulares (maior ≤ 2,5× o menor) e 60–6000 rpm; pulsos irregulares por 2 janelas = "sem sinal" (`fanRpm=-1`). Com o fio 3 conectado e pull-up externo de 10 kΩ ao 3V3 o sinal é limpo.
- **(b) Causa (lendo o código, NÃO reproduzido no aparelho):** CH1/CH2 sob PI (`pulsoGerencia`) só publicam na nuvem a "sessão" de pulsos; um "desligar" do PWA com o relé entre dois pulsos (já desligado) não mudava `relayState`, então nada disparava a pausa da automação e o PI religava e republicava ON (o botão voltava). Nos canais sem PI (CH4 "Desumidificar") o desligar funcionava, mas a automação retomava após só 5 min. **Correção:** `manualHold(ch,on)` — todo comando do usuário (toque na tela ou PWA; `applyActuatorsFromFirestore` e `setRelayAndSync`, exceto o cooler CH7 que é automático): DESLIGAR = pausa a automação do canal por **2 h** (`MANUAL_OFF_HOLD_MS`), abandona pulso em andamento, zera o integral do PI e publica OFF; LIGAR = pausa de 5 min como antes e conta como sessão ligada. Log "Automacao: <canal> desligado manualmente: automacao do canal em pausa por 2 h". As travas de segurança (sobretemperatura, sensor sem leitura) continuam valendo. Para manutenção longa/definitiva: desativar "Automação ativa" (PWA > Automação).
- **Não descartado:** se após gravar ainda não desligar, checar `HTTP 429` (cota de leituras do Firestore) no Serial e se o PWA mostra a chocadeira online (atuadores ficam desabilitados offline).
- **Estado:** compilado (RAM 32,7 %, Flash 33,5 %), **NÃO gravado**. Backup `main_pre_fase106.bk`.

## Fase 107 — pinos travados no deep sleep (LEDs/fans/backlight) + diagnóstico do tacômetro (2026-10-14)
- **Foto da tela Fans:** Fan 1 a 0 % mostrando "sem sinal" com o fio 3 já no GPIO17. Hipóteses (nada medido): (a) a 0 % o fan para/gira tão devagar que não há pulsos válidos — testar a 100 %; (b) o fio ligado não é o pino 3 (ou está no GND/+12 V); (c) filtro rejeitando: faixa antiga 60–6000 rpm (fans pequenos passam disso) → agora até 9000 rpm.
- **Diagnóstico:** `[TACH] GPIO17 n=… min=… max=… us nivel=… rpm=…` na serial (a cada 10 s sem sinal válido) e, na tela Fans, quando não há rpm: "Tach GPIO17: N pulsos/2s nivel=X". Leitura: N=0 e nivel=1 → sem pulsos (fan parado ou fio errado/solto); N=0 e nivel=0 → pino preso em GND (fio errado); N>0 mas "sem sinal" → pulsos irregulares/fora de 60–9000 rpm (ruído).
- **LEDs no desligar:** firmware só controla os LEDs dos relés (acendem com IN=LOW) e o backlight; LED de energia de ESP32/ADXL/MCP23017/DC-DC/módulo de relé é fixo no trilho e só some cortando a alimentação ou retirando o LED/resistor. Para os controláveis, `entrarEmDeepSleep()` agora trava com `gpio_hold_en` + `gpio_deep_sleep_hold_en`: 8 relés em HIGH (desligado), GPIO47 (backlight) e GPIO4/5/6 (PWM dos fans) em LOW; `initRelays()` libera (`gpio_hold_dis`) no boot. **Não testado.**
- **Estado:** compilado (RAM 32,7 %, Flash 33,5 %), NÃO gravado. Backup `main_pre_fase107.bk`.

## Fase 108 — tacômetro oscilando entre ~8500 rpm e "sem sinal" a 100 % (2026-10-14)
- **Relato:** fio 3 confirmado no GPIO17; a 100 % o RPM alternava entre ~8500 e "sem sinal". **Causa provável (não medida):** o filtro de 3 ms (Fase 106) descartava pulsos de um fan de ~8500 rpm (intervalo médio ~3,5 ms; com jitter vários ficam < 3 ms) → intervalos dobrados → janela reprovada pela checagem de regularidade. **Correção:** filtro 1,2 ms; regularidade tolera 4× (era 2,5×); faixa 60–12000 rpm; "sem sinal" só após 3 janelas ruins seguidas (6 s, antes mantém o último valor). Rodapé da tela Fans sem rpm: `Tach: Np/2s minXus maxYus nL` (pulsos por janela de 2 s, menor/maior intervalo em µs, nível do pino).
- **A verificar:** 2 pulsos/volta é a suposição (padrão Intel). Se o valor real do fan (etiqueta) for metade/dobro, ajustar `n*30000/dt` (ex.: 4 pulsos/volta → `n*15000/dt`). Pull-up externo de 10 kΩ ao 3V3 deixa as bordas mais limpas.
- **Estado:** compilado, NÃO gravado. Backup `main_pre_fase108.bk`.

## Fase 109 — tacômetro era ruído (fan 500–2000 rpm) + CH8 manual não ficava ligado com a automação desligada (2026-10-14)
- **Fan identificado (fotos):** MSI **APA1225M12**, 120×120×25 mm, 12 V 0,35 A, **500–2000 ±10 % rpm**, ARGB (retirado de um water cooler). Diagnóstico da tela Fans a 100 %: `1147–1185 pulsos/2 s, intervalo mín 1200 µs (= o próprio filtro), máx 5,7–6,7 ms`. Um fan de 2000 rpm a 2 pulsos/volta dá ~133 pulsos em 2 s (15 ms entre pulsos) — ou seja **os ~8500 rpm vistos antes eram ruído** (~575 pulsos/s, ~17× o esperado), não rotação. Provável acoplamento de comutação do motor/PWM numa linha de tacômetro de alta impedância (pull-up interno de ~45 kΩ).
- **Firmware:** interrupção nas DUAS bordas; só conta pulso se o nível BAIXO durar ≥ 800 µs (`TACH_LOW_MIN_US`) e houver ≥ 4 ms desde o anterior (`TACH_DEAD_US`); `TACH_PPR=2`. Rodapé sem rpm: `Tach: Np Rr wLARGURA iMIN-MAX nL` (aceitos, rejeitados por estreitos, largura média do nível baixo, intervalos). Serial: `[TACH] … aceitos= rejeitados= largura= intervalo= nivel=`.
- **Hardware recomendado (resolve na raiz):** pull-up externo de 4,7–10 kΩ do GPIO17 ao 3V3 + capacitor de 10 nF do GPIO17 ao GND (filtro RC ~50–100 µs); manter o fio 3 longe do fio 4 (PWM) e do +12 V.
- **CH8 manual:** `manualHold` (Fase 106) marcava `autoTinhaUltimo=true` e `pulsoNuvemVal=on`; com a automação DESLIGADA o bloco "automacao desativada" desligava o canal (ou republicava OFF na nuvem, que o poll aplicava). Agora o canal manual deixa de ser "da automação" (`autoTinhaUltimo=false`) e `pulsoNuvemVal` só muda se o canal estiver sob PI. Efeito: CH8 (e qualquer canal) pode ser ligado/desligado à mão com a automação desligada.
- **Estado:** compilado (RAM 32,7 %, Flash 33,5 %), NÃO gravado. Backup `main_pre_fase109.bk`.

## Fase 110 — RPM no card Status, chave da automação na tela Atuadores (aparelho + PWA), tacômetro por contagem (2026-10-14)
- **Fotos da tela Fans (Fase 109 gravada):** rpm válido 1497 a 60 % e 1301 a 50 % (fan MSI 500–2000 rpm: coerente); a 40 % "sem sinal" com `77p 30830r w815 i4602-27840`: ~30 mil rejeitados por 2 s = comutação/PWM de 25 kHz acoplada ao fio 3 (só aparece com PWM < 100 %), largura média aceita ~815 µs (colada no limite de 800 µs). **Correção:** a regularidade (maior/menor ≤ 4×) reprovava janelas cuja contagem estava certa (90 pulsos/2 s ≈ 1350 rpm vs 1300) — agora vale ≥ 4 pulsos, 60–12000 rpm e **mediana de 3 janelas**. Hardware (RC no GPIO17) segue recomendado e não foi feito. Obs.: a 25 kHz o ISR de 2 bordas custa alguns % de CPU.
- **RPM no card Status:** home do aparelho (linha própria "Fan N rpm" em azul, y+54; redesenha quando o rpm muda de faixa de 50) e card Status do PWA (`#card-status-rpm`, preenchido por `painel.js` a partir de `#R`; atualiza no heartbeat ~1 min ou a cada 5 s com o cartão Fans visível). Só aparece com rpm > 0.
- **Chave da automação em Atuadores:** firmware — cartão "Automacao ATIVA/DESLIGADA" no topo (toque alterna; mesma variável `autoAtivo` de "Automacao ativa"); linhas passaram de 52 → 42 px (`AT_ROW_Y0=56`, 4 canais/pág.); aviso amarelo no rodapé quando desligada com ciclo ativo. PWA — linha "Automação" no topo do card Atuadores (`#atu-auto`, espelha `auto-ativo`; desabilitada para convidado sem `total`).
- **Inteligência (pedido do usuário):** `manualHold` (toque na tela ou PWA) em canal que a AUTOMAÇÃO controla (regra por relé ≠ Manual, ou citado por regra avançada; exceto CH3 bomba de água e CH7 cooler) **desliga a chave da automação** (`autoManualPend` → no `automacaoTick`: `autoPersistir()`, log "Automacao DESLIGADA: <canal> alterado manualmente…", redesenha). O `cmd.auto` volta ao PWA e a chave cai lá também. Ao religar a chave (qualquer origem) as pausas manuais (`autoPausaAte`) são zeradas e a automação reassume. Canal em Manual (ex.: LED ovoscópio) não mexe na chave. **Atenção incubação:** com a chave desligada nada controla temperatura/umidade — por isso o aviso na tela; sem religamento automático (decisão do usuário: "ele pode ligar a chave novamente").
- **Estado:** firmware compilado (RAM 32,7 %, Flash 33,5 %), NÃO gravado; PWA publicado. Backups `main_pre_fase110.bk`, `frontend/*.pre104.bk`.

## Fase 111 — automação religa sozinha, rpm verde, tampa não vira "OK" com módulo fora do lugar, CH8 desliga com fans em 0 % (2026-10-15)
- **Religar sozinha:** `autoRetomaMin` (padrão **10 min**, 0 = nunca; token `R,<min>` entre `M,` e `L,` na string `cmd.auto`; `/auto.cfg`). Com `fbCicloAtivo` (inclui pausado e preparo) e a chave da automação desligada por mais de N min, o `automacaoTick` liga a chave, persiste (`autoPersistir`), loga "Automacao religada sozinha apos N min…" e a borda de subida zera as pausas manuais. Sem ciclo nada religa. Editável: aparelho em Config. > Automação, página 3/3 (seletor 0·5·10·15·20·30·45·60·90·120); PWA no card Automação ("Religar a automação sozinha após"). PWA antigo sem `R,` não sobrescreve (`autoDoPoll` ignora a diferença). Tela Atuadores mostra "volta sozinha em até N min" no rodapé.
- **RPM no card Status:** "N rpm" em **verde**, fonte 2 do TFT_eSPI (home; o diagnóstico curto subiu de y+46 → y+40) e `.card-rpm` verde 15 px no PWA.
- **Status "OK" com a tampa aberta — causas (código):** `tampaTick` inferia o fechamento só pela temperatura: (1) `fechou` = temperatura subindo ≥ 0,12 °C/min por 20 s — o aquecedor compensando faz isso mesmo com a tampa aberta; (2) após **15 min** aberta ia para "recuperando" e então "OK"; (3) se a queda nunca passou de −0,9 °C/min (caixa pequena + aquecedor) a abertura nem era detectada. **Correção parcial:** com o sensor ADXL calibrado e vivo (`adxlAutoAtivo()`) dizendo **ABERTA** (módulo fora do lugar), nem `fechou` nem os 15 min tiram o estado "tampa aberta". Sem o sensor (ou com tampa ≈ mesa) continua inferência por temperatura — limite físico já documentado nas Fases 95–97.
- **CH8 e fans:** com o PWM ligado e os 3 fans em 0 %, `fansCh8Tick()` (1 s, só na transição) desliga o CH8 (`autoAplica` + log "todos os fans em 0%"); qualquer fan > 0 % religa se foi essa regra que desligou. O modo Circular não religa enquanto `fansTodosZero()`. Consequência: sem CH8 o aquecedor também para (intertravamento "só aquece com as Fans"). Aviso na tela Fans e no card do PWA. Texto "0% desliga o fan" corrigido (0 % = rotação mínima do fan).
- **Pergunta em aberto (resposta no chat):** automação do PWM por fan — critérios/sensores propostos, aguardando a decisão do usuário sobre o papel de cada fan.
- **Estado:** firmware compilado (RAM 32,7 %, Flash 33,6 %), NÃO gravado; PWA publicado. Backup `main_pre_fase111.bk`.

## Fase 112 — função por fan + regras (aparelho e PWA) e Status com o motivo real (2026-10-15)
- **Pedido:** cada fan recebe uma FUNÇÃO (exemplo do usuário: fan 1 no piso = circulação, empurrando o calor das 2 resistências para as paredes; fan 2 a 5 cm do piso empurrando para cima = resfriamento; fan 3 no alto, soprando para a parede com 3 furos = renovação); as regras ficam numa tela própria e são aplicadas na tela Fans. CO₂ INATIVO.
- **Modelo:** `fanModo[3]` (0 Manual · 1 Circulação · 2 Resfriamento · 3 Renovação) e `FanRegras fr` compartilhada por função; `pwmDutyEf[3]` = duty efetivo (manual ou calculado) que `pwmAplica` usa; `fansTodosZero()`/CH8 agora usam o efetivo. Persistência `/fanrules.cfg` (`F1|m0,m1,m2|cMin,cMax|rMin,rMax,rIni10,rFull10|vMin,vMax,vIni,vFull|co2On,co2Ini,co2Full`). Funções só valem com a **chave da automação ligada** e ciclo ativo (ou manter ambiente); fora disso o fan volta ao % manual. Tampa aberta/recuperando ou sensor sem leitura = mínimo da função. Cálculo a cada 5 s com rampa de ±10 pontos.
  * Circulação: `min + (max−min)·heaterEma` (EMA de ~50 s do CH1 ligado). Padrão 40–100 %.
  * Resfriamento: sobe quando T passa do ponto médio da faixa do tipo (+0,0 °C → min; +1,0 °C → max). Padrão 0–100 %.
  * Renovação: sobe com a umidade acima do máximo da faixa (+0 → +5 pts) e, se `co2On` (INATIVO, fixo em 0), com o CO₂ (3000 → 5000 ppm). Padrão 20–100 %.
- **Aparelho:** tela Fans — tocar no NOME do fan cicla a função; −/+ só em Manual; barra azul = % calculado; "Regras >" no topo abre a tela de regras (3 páginas, reaproveita os cartões −/+ da tela Tela). **PWA:** select de função em cada fan + card "Regras dos fans" (envio automático 0,9 s depois; `cmd.fansRegras="<ms>|m0,m1,m2|…"`; `painel` ganhou `#G|modos|efetivos|c|r|v|co2`). Nota: no PWA as regras ficam num card (padrão do Hardware/Automação), não numa tela separada.
- **Status com o motivo real:** `statusMotivoCiclo()` → OK · Auto OFF · Sensor! · Fora T · Fora U · Fora T/U (cartão Status da home e tela Status; antes mostrava "OK" mesmo com a automação desligada). A tela Status explica "Automação DESLIGADA: nada controla o ambiente (volta sozinha em até N min)". O cartão da tampa não afirma mais "FECHADA": mostra ABERTA / MODULO FORA / SEM ABERTURA, e FECHADA só com assinatura confiável. PWA: card Status e janela com a mesma lógica; "Nenhuma abertura detectada" no lugar de "Tampa fechada".
- **Cuidado:** funções com mínimo 0 % nos 3 fans levam o CH8 a desligar (e o aquecedor a parar).
- **Estado:** firmware compilado (RAM 32,8 %, Flash 33,7 %), NÃO gravado; PWA publicado. Backups `main_pre_fase112.bk`, `frontend/*.pre112.bk`.

## Nota 15/10 — Fase 112 GRAVADA e validada pelo usuário; reset súbito em investigação
- **Validado em hardware (usuário):** atuadores (desligar derruba a chave da automação, religar ok), funções dos fans, Status com motivo real, tela (reduzir/apagar), tampa (ADXL), convite com 2ª conta Google, push "novo aparelho", bomba de ar, campo "tempo do pulso" da bomba. RPM do fan 1 correto sem o filtro RC no GPIO17 (RC segue recomendado). No momento **só a bomba de água está ligada** de carga (resistência/outros fora).
- **Pedido novo:** LED do sensor ADXL piscando com a tampa aberta. **Não é possível pelo firmware:** é o LED de energia do módulo (D1+R1 direto no 3,3 V do regulador). Alternativas: aviso piscando na tela (software) ou LED externo num pino livre/MCP23017 (hardware).
- **Reset súbito sem aparecer no log (aberto):** começou depois de trocar o fio do pino LED do display (+5 V → GPIO47). Multímetro: **3,10 V** entre GND e 3V3 do ESP32 (antes 3,15–3,18 V na Fase 20; abaixo do esperado de um LDO de 3,3 V). Hipóteses, em ordem: (1) fio do GPIO47 encostando em pino vizinho (RST/GPIO0/45/48) ou mau contato → reset externo; (2) trilho de 3,3 V no limite → brownout/POWERON (aparece como "Normal"); (3) ondulação de 5 kHz do PWM do backlight (teste: brilho 100 % = DC); (4) software (Panic/Watchdog). Primeiro dado a coletar: **Config. > Sistema > "Último reset"** (Normal/Externo/Software/Panic/Watchdog/Brownout + "N seguidos").
- **Decisão sobre o próximo item:** MH-Z19C antes do MCP23017 (o driver UART, as regras e o card do PWA já existem; falta só mapear os 7 fios e o esquema). O MCP23017 só se justifica por LED externo / tacômetro de fans 2 e 3 (usuário só quer RPM de 1 fan).

## Fase 113 — relatório do crash (core dump) no log (2026-10-15)
- **Dado novo do usuário:** "Último reset" = **Panic!** (falha de software, não de energia); brilho em 100 % não mudou nada; o 0,64 V medido entre GND e o pino 5V do adaptador é o diodo de proteção já documentado (seção 2.6), não a tensão real; ADXL 3V3 ok. Hipóteses de energia/PWM descartadas.
- **Mecanismo:** a partição `coredump` (64 KB, 0xff0000) e `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` já existiam; faltava ler. `coredumpRelatar()` (logo após o log "Boot") lê `esp_core_dump_get_summary()`, imprime `[CRASH] ...` na serial e grava no log (categoria "Panic", vai ao SD e à nuvem): `Crash task=<tarefa> pc=<hex> causa=<n> vaddr=<hex> bt=<hex,...>`; depois apaga a partição. O dump do crash JÁ ocorrido continua na flash (o upload não toca nessa partição): o 1º boot depois de gravar deve reportá-lo.
- **Decodificar:** `xtensa-esp32s3-elf-addr2line -pfiaC -e main_fase112_flashed.elf <pc> <bt...>` (toolchain em `~/.platformio/packages/toolchain-xtensa-esp32s3/bin`). **`main_fase112_flashed.elf` é cópia do ELF do build da Fase 112 (o que crashou)** — não apagar; o `firmware.elf` atual já é outro build. Crashes futuros: usar o ELF do build gravado na época.
- **Causa do crash:** ainda desconhecida. Estado: compilado (RAM 32,8 %, Flash 33,7 %), NÃO gravado. Backup `main_pre_fase113.bk`.

## Fase 114 — MH-Z19C: fonte de verdade dos pinos e ficha de identificação dos fios (2026-10-15)
- **Fontes lidas:** manual Winsen MH-Z19C v1.0 (PDF enviado pelo usuário, 7 páginas) e a página da Sigmanortec (aberta pelo navegador embutido; WebFetch deu 403). A página do manuals.plus bloqueia com verificação anti-robô (Cloudflare) — não contornada; o conteúdo equivalente veio do PDF e dos prints do usuário.
- **Conector de 7 vias (versão "terminal"):** 1 HD (calibração zero, LOW ≥ 7 s) · 2 **Reservado** (a página da Sigmanortec chama de "Vo analógico 0,4–2 V", copiado do MH-Z19B; no manual do C é "Reserved") · 3 GND · 4 Vin (+5,0 V ±0,1 V, ≥ 150 mA, média < 40 mA, pico 125 mA) · 5 RXD (entrada UART, 3,3 V) · 6 TXD (saída UART) · 7 PWM (período 1004 ms, `C = 2000·(TH−2)/(TH+TL−4)`, só para 400–2000 ppm). Versão de pinos (furos): Hd/SR/Tx/Rx/Vo à esquerda e PWM/AOT/GND/Vin à direita.
- **Protocolo:** 9600 8N1; leitura `FF 01 86 00 00 00 00 00 79` → `FF 86 HH LL … CS`, ppm = HH·256+LL; ABC: `FF 01 79 A0|00 …` (A0 liga, 00 desliga; padrão de fábrica = LIGADO, calibra a cada 24 h tomando 400 ppm como zero — inadequado para incubadora, o firmware desliga). Faixa 400–5000 ppm, ±(50 ppm + 5 %), pré-aquecimento 1 min, T90 < 120 s, −10…50 °C, 0–95 % UR sem condensação. **O código de checksum do manual tem bug** (variável não inicializada); o firmware calcula `0xFF − soma(1..7) + 1` corretamente. Nada disso exige mudar o firmware da Fase 99 (RX=GPIO16 ← TX do sensor, TX=GPIO15 → RX).
- **Cores dos fios:** nenhuma fonte mapeia cor→pino; usuário identifica por continuidade contra os furos marcados na placa. Cores do cabo: amarelo, verde, azul, rosa, preto, branco, marrom.
- **Entregue:** `docs/esquemas/06_mhz19c_identificar_fios.png/.svg` (`gerar_mhz19c.py`; `converter.sh` agora gera 6): pinos 7…1 com caixas para anotar a cor, procedimento de continuidade, regra de segurança (só o +5 V no pino errado queima), ligação final com DC-DC e capacitor de 220–470 µF + 100 nF recomendado.
- **Pendente:** usuário preencher a tabela e confirmar; gravar a Fase 113 (relatório do crash). Firmware sem mudança nesta fase.

## Fase 114b — MH-Z19C: fios identificados pela posição (2026-10-15)
- **Fotos do usuário:** módulo "terminal" (7 pads soldados a um conector branco de 7 vias; **sem marcação nenhuma** de pinos na placa, etiqueta "MH-Z19C 400-5000PPM 20260616"). Ordem dos fios no conector, etiqueta para cima, esquerda→direita: **marrom, branco, preto, rosa, azul, verde, amarelo**.
- **Dedução:** o pino 4 do manual (Vin +5 V) é o do MEIO do conector = fio **vermelho** em qualquer orientação. GND (pino 3) e RX (pino 5) são os vizinhos do rosa: preto (hipótese A, pino 1 = marrom) ou azul (hipótese B, pino 1 = amarelo). Medida do usuário: vermelho–preto = 671 Ω, sem bipe (normal: o bipe é < ~50 Ω; indício de par Vin–GND, não prova).
- **A decidir (sem energia):** continuidade de preto e de azul com a carcaça dourada/solda dos cantos (GND) e resistência rosa–vizinhos nos dois sentidos. Depois: A → vermelho +5 V, preto GND, azul←GPIO15, verde→GPIO16; B → vermelho +5 V, azul GND, preto←GPIO15, branco→GPIO16. Fios restantes isolados.
- **Imagem:** `docs/esquemas/07_mhz19c_fios_sem_marcacao.png/.svg` (`gerar_mhz19c_fios.py`; `converter.sh` agora gera 7). A ficha 06 continua válida para pinagem/segurança, mas seu passo "ache as marcações dos furos" não se aplica a este módulo.

## Fase 114c — MH-Z19C ligado, sem resposta; diagnóstico no Log (2026-10-15)
- **Ligação feita pelo usuário (hipótese A confirmada por continuidade: carcaça dourada + soldas dos cantos apitam com o PRETO):** vermelho→+5 V, preto→GND, azul→GPIO15, verde→GPIO16. Resultado: lâmpada IR acesa (a "luz vermelha" é a fonte infravermelha, normal), **4,5 V no sensor** (spec: 5,0 ±0,1 V, ≥ 150 mA), 5 min de espera, TX/RX já trocados uma vez: **sem leitura de CO₂ no PWA**.
- **Revisão do driver (`co2Init/co2Tick`) e do caminho PWA:** sem defeito encontrado (UART1 9600 8N1, RX=16/TX=15, checksum `0xFF−Σ(1..7)+1`, ignora 3 min, `co2Atual` vai no heartbeat e `app.js` lê `dados.co2Atual`). Suspeitas, por ordem: (1) 4,5 V no sensor (queda no fio/DC-DC; a lâmpada de 125 mA derruba a tensão e o MCU interno pode reiniciar); (2) fios nos pinos rotulados "RX"/"TX" da placa (GPIO44/43) em vez dos números 15/16; (3) fio marrom (HD) encostando no GND = calibração de zero.
- **Diagnóstico novo (compilado, NÃO gravado):** `co2BytesRx` conta os bytes recebidos no GPIO16; categoria de log **"CO2"** (SD + nuvem): "Sensor respondeu: N ppm", "Sem resposta (N bytes recebidos no GPIO16)…" aos ~1 min e ~6 min, "Quadro invalido (… bytes): XX XX XX XX". N = 0 → o TX do sensor não chega ao GPIO16 (fio/pino/alimentação); N > 0 e quadro inválido → ruído/velocidade. Backup `main_pre_fase114.bk`.
- **Achado (foto do usuário, 15/10):** os fios UART do MH-Z19C estavam nos terminais rotulados **"RX" e "TX"** do adaptador de bornes (= GPIO44/GPIO43, **UART0 = ponte USB CH343**), não nos terminais **15 e 16**. Isso explica (a) o sensor sem resposta no firmware (que usa UART1 em GPIO15/16) e (b) a falha de gravação: `Failed to connect to ESP32-S3: Download mode successfully detected, but getting no sync reply: The serial TX path seems to be down` (a linha TX0/RX0 do USB fica carregada/disputada pelo sensor). Correção: mover os fios para os bornes 15 e 16; **nenhum fio em RX/TX do adaptador** (nem sensores) durante gravação/monitor serial.
- **Medidas do usuário:** TX do sensor × GND = 2,9–3,1 V (sensor vivo, idle alto); **saída do conversor DC-DC e Vin do sensor oscilando 4,8–5,7 V** (com a bomba de ar desligada). 5,7 V extrapola a especificação (5,0 ±0,1 V) e o limite típico de módulos de 5 V. Recomendação: regulador dedicado de 5,0 V para o sensor (mini-buck MP1584/LM2596 ajustado com multímetro, ou AMS1117-5.0/7805 a partir dos 12 V) + 470 µF; investigar o DC-DC principal (trimpot, tensão de entrada de 12 V, ondulação sob carga: relés/fans/backlight).

## Fase 114d — CO₂ funcionando; card de CO₂ do aparelho ligado (2026-10-15)
- **Resultado em hardware (fotos do usuário):** com os fios nos bornes 16 (TX do sensor) e 15 (RX do sensor), o Log mostra `CO2: Sem resposta (0 bytes recebidos no GPIO16)` às 10:33:57 (ainda nos bornes errados) e **`CO2: Sensor respondeu: 500 ppm` às 10:38:36** (13 s após o boot); o card CO₂ do PWA passou a mostrar **642 ppm** (heartbeat). Valores plausíveis para ar de sala; validação com sopro/ventilação ainda pendente. O usuário confirmou que GPIO15 e 16 estavam livres (a hipótese do IN8 no borne 15 não procedia). Rastro dos logs de crash (Fase 113) ainda não conferido.
- **Bug achado nas fotos:** o card "CO2" da home do aparelho era estático ("---"; `card(216,…,"CO2","---")`) — o valor só ia para o PWA. Agora `drawTempUmidCards()` desenha o CO₂ (ppm, verde ≤ 1500, amarelo ≤ 3000, vermelho acima; "---" sem leitura válida há 180 s), mesma cadência do SHT31. Compilado, NÃO gravado. Backup `main_pre_fase114b.bk`.
- **Observação de operação:** durante o teste a automação estava DESLIGADA (Status "Auto OFF", 33,0 °C) desde 10:13; reinícios por gravação reiniciam a contagem do "religar sozinha".
- **Fase 114e — atraso do CO₂ (pedido do usuário: "notei um delay"):** causas: (1) o firmware ignorava a leitura por 3 min depois de cada boot do ESP (o manual pede 1 min); (2) o valor só chega ao PWA no heartbeat (60 s); (3) leitura a cada 5 s; (4) T90 do sensor < 120 s (físico). Mudanças: pré-aquecimento 180→60 s, leitura 5→3 s, heartbeat de 60→20 s **somente com o PWA aberto** (`pwaAtivo()`; +~2,9 mil escritas/dia no pior caso, de 20 mil/dia). Sem PWA aberto nada muda. Backup `main_pre_fase114c.bk`.

## Fase 115 — tela do CO₂, histórico, alertas e card Status rotativo (2026-10-15)
- **Pedido:** (1) tela ao tocar no card CO₂ (equivalente à do SHT31: gráfico, histórico), com faixas decididas por mim; (2) alertas; (3) card Status com dois textos redundantes ("Auto OFF" + "automacao off"): remover o mais longo e, havendo vários avisos, mostrar um de cada vez por 5 s com fonte maior.
- **Faixas de CO₂ (ppm) por fase** (`co2Limites()`; conservadoras, ajustáveis no código): incubação **≤ 2000 ok · > 3000 alto**; pré-eclosão/eclosão (a partir de eclDia−2) **≤ 3000 ok · > 4500 alto** (sensor satura em 5000). Cor: verde/amarelo/vermelho (`co2Cor`) na home, no gráfico e na linha.
- **Tela do CO₂ (aparelho):** toque no card abre `S_SENSOR_DETAIL` com `sensorDetalheCo2`: gráfico do dia (buffer `co2Historico[300]`, 1 min nas 10 primeiras amostras, depois 5 min; reset à meia-noite junto de T/U), overlay Min/Max/Med em ppm, ponto "ao vivo", rodapé "CO2 - ideal ate N ppm - N leit. hoje" e botão **7 dias ↔ Hoje** (não abre a lista por ciclo, que é só T/U). `drawSensorHistChart` ganhou formato sem casas decimais para `"ppm"`.
- **Histórico:** o slot de 10 min agora leva o CO₂ médio. SD: `slot,t10,h10[,ppm]` (4ª coluna opcional; leitores antigos ignoram). Nuvem: `historico/AAAA-MM-DD.m.sN = "t10,h10[,ppm]"` — o PWA atual usa só os 2 primeiros valores (compatível). `histCarregar()` monta `h7C` (série própria do CO₂ nos 7 dias).
- **Alertas:** `co2AlertaTick()` — só com ciclo em andamento e fora do preparo: CO₂ > limite alto por ≥ 10 min → log "Alerta" + push (tag 16 → mesma preferência "faixa T/U" do PWA; re-avisa a cada 30 min); volta a ≤ ok por ≥ 5 min → "CO2 normalizado". Item "CO2 alto" também entra na rotação do card Status.
- **Card Status (home):** `statusItens()` monta a lista (estado principal · diagnóstico curto se não repetir o principal · "CO2 alto"); com mais de 1 item alterna a cada 5 s (pontinhos no canto mostram quantos/qual); fonte maior (texto > 8 caracteres usa a fonte 2 de 16 px; muito longo quebra em 2 linhas). RPM fica fixo embaixo. Etiquetas do diagnóstico encurtadas: "auto off", "tampa?", "volume?", "vol/tampa?", "aquec. max", "sem umidif", "umid. max", "umidific.".
- **Pendente (aguarda confirmação do layout, regra do usuário para o PWA):** card CO₂ clicável no PWA com modal (Hoje / 7 dias; o dado já sobe em `historico.m.sN` 3º valor; `leituras/` ainda não leva `co2`), e o card Status do PWA com a mesma rotação.
- **Estado:** compilado (RAM 34,2 %, Flash 33,8 %), NÃO gravado. Backup `main_pre_fase115.bk`.
- **Fase 115b — PWA do CO₂ (publicado, `CACHE_VERSION` v39):** card **CO₂ clicável** (`data-sensor="co2"`) abre o mesmo modal de temperatura/umidade, com `FAIXAS.co2` (por fase, igual ao firmware: ≤ 2000 ok / > 3000 alto; a partir de eclDia−2: ≤ 3000 / > 4500), cor do card por faixa e valores sem casas decimais. **Bug achado:** a aba "Histórico" do modal lia só o campo legado `pts` do documento `historico/AAAA-MM-DD`, mas desde a Fase 53 os dados vão no mapa `m` ("sN" = "t10,h10[,co2]") → a aba vinha sempre vazia ("não existe 7 dias"). Agora lê `pts` **e** `m`, a aba se chama "**7 dias**" e vale para temperatura, umidade e CO₂ (3º valor do slot). Firmware: `leituras/` passa a levar o campo `co2` (mesma escrita de 5 min, sem custo extra) — compilado, **não gravado**; até gravar, "Hoje" de CO₂ no PWA fica vazio e "7 dias" só enche com os novos slots. Testado em harness com dados sintéticos (Hoje: 40 leituras; 7 dias: 168 pontos). Backups `frontend/*.pre115.bk`.
- **Ainda sem rotação no card Status do PWA** (só o aparelho): aguarda decisão do usuário.
- **Fase 115c — card Status do PWA: uma informação por vez (publicado, `CACHE_VERSION` v40):** o card tinha 3 linhas (estado + "⚠ toque: causa provável" + "N rpm") e esticava a linha inteira da grade, deixando Status/Viragem/Ciclo MAIORES que Temperatura/Umidade/CO₂. Agora tem uma única linha de valor que **rotaciona a cada 5 s** entre: estado principal · causa provável (o diagnóstico curto do aparelho, em maiúsculas, se não repetir o estado) · "CO₂ ALTO" (`state.co2Atual` > faixa de alerta, só com ciclo e fora do preparo) · "N rpm" (verde, vindo do `painel`). Pontinhos ○● no canto do rótulo mostram quantos avisos e qual está na tela; texto > 8 caracteres usa fonte menor (sem estouro). Os elementos `#card-status-diag`/`#card-status-rpm` foram removidos. Medido em harness a 375 px: os 6 cards ficam com 64 px; rotação conferida com 2 e com 4 itens. Backups `frontend/*.pre115c.bk`.

## Fase 116 — firmware: Status "uma informação por vez" com rpm, data no card Ciclo, card Data vazio (2026-10-15)
- **Card Status (home do aparelho):** o rpm do fan 1 saiu da linha fixa e entrou na rotação de 5 s (número grande + "rpm" pequeno, verde), igual ao PWA; o card mostra sempre UM item (estado · diagnóstico curto · "CO2 alto" · rpm), com pontinhos indicando quantos.
- **Card Data:** ficou vazio (só a moldura, `drawDataCard`), reservado para uso futuro. **Card Ciclo:** alterna a cada 5 s entre "dia/total" e a data ("dd/mm" + ano, rótulo "Data"); sem RTC não alterna. O redesenho do card Ciclo agora roda a cada 5 s (antes 30 s só com ciclo).
- **Alerta de CO₂:** não conta tempo com a tampa aberta/recuperando (a leitura deixa de representar a caixa).
- Estado: compilado (RAM 34,2 %, Flash 33,8 %), NÃO gravado. Backup `main_pre_fase116.bk`.
- **Fase 116b — Renovação por CO₂ ATIVA (pedido do usuário):** `fr.co2On=1`, começa em **1500 ppm** e vai ao máximo em **3000 ppm**, para o(s) fan(s) com a função **Renovação** (só vale com a automação ligada e ciclo/manter; sensor sem leitura ou tampa aberta = só a umidade). Na pré-eclosão/eclosão os limites sobem junto com as faixas de alerta (**+1000 / +1500 ppm**, via `co2Limites()`). `fanRegrasParse` deixou de forçar `co2On=0` e limita 500–4900 / +100–5000. **Migração única** (`/mig116.flag`): a config já salva em `/fanrules.cfg` (com CO₂ inativo, 3000/5000) é reescrita para ligado 1500/3000. Aparelho: tela Regras agora tem **4 páginas** (a 4ª = CO₂: liga/desliga, começa em, máximo em; passo 100 ppm). PWA (v41): o card "Regras dos fans" tem o grupo "Renovação por CO₂" (liga/desliga + dois campos) e `enviarRegras` envia esses valores (antes mandava fixo `0,3000,5000`, o que desligaria a regra). Testado em harness: carrega do painel (`1,1800,3200`) e envia `1,2000,3200` ao editar. Firmware compilado (RAM 34,2 %, Flash 33,8 %), NÃO gravado. Backups `main_pre_fase116b.bk`, `frontend/*.pre116b.bk`.

## Fase 117 — esquema do MCP23017 (2026-10-15)
- **Pergunta do usuário:** "preciso mesmo instalar o MCP23017? por quê?" Resposta registrada: **não é necessário para nenhuma função existente**; justifica-se por (a) dar o **LED externo "tampa aberta"** que o usuário pediu (o LED do ADXL é de energia e não é controlável), (b) buzzer, (c) futuros TCRT5000 do ovoscópio, boia de nível de água, mais entradas/saídas — o ESP32 ficou sem GPIO livre (restam só strapping/USB). **Não é bom para tacômetros** (pulsos de ~70 Hz por polling I2C; INTA exigiria um GPIO que não existe).
- **Hardware (fotos do usuário, Fase 100):** Waveshare "IO Expansion Board" MCP23017: conector de 6 vias (VCC GND SDA SCL INTA INTB) com cabo, pinos PA0–PA7/PB0–PB7, pontes A0/A1/A2 (0x20 de fábrica), pull-ups de 10 kΩ nas linhas I2C. **Alimentar em 3V3** (5 V levaria 5 V a SDA/SCL pelos pull-ups).
- **Entregue:** `docs/esquemas/08_mcp23017_i2c.png/.svg` (`gerar_mcp23017.py`; `converter.sh` agora gera 8): ligação ao I2C (SDA = GPIO8, SCL = GPIO9) junto de SHT31 0x44 / ADXL345 0x53 / EEPROM 0x57 / DS3231 0x68 (sem conflito), tabela de endereços, exemplo PA0 → 330 Ω → LED → GND e conferências (VCC–GND sem curto, scan I2C pela serial `I` deve listar 0x20, fios curtos, longe do 220 V).
- **Firmware:** ainda SEM driver (planejado: probe do 0x20, IODIRA = saída, OLATA; pisca o LED com `tampaEst==1`; ativo só se o chip responder). Aguardando o usuário decidir instalar.
- **Pull-up dos relés:** usuário ainda NÃO fez o pedido dos resistores (SIP 10 kΩ) — item de segurança continua aberto (esquema `03_pullup_rele.png`).

## Fase 118 — preparação da publicação no GitHub (2026-10-15)
- **ACHADO CRÍTICO:** o repositório do PWA (`<SEU_CAMINHO>`, remote `origin` = `Erdigital2022/EggBr`) tinha um **token de acesso do GitHub (ghp_…) embutido na URL do remote** em `.git/config` (texto claro; a pasta está no OneDrive). Não foi enviado ao GitHub, mas deve ser **revogado** e o remote reescrito sem token. O valor não é reproduzido aqui.
- **Estado dos repositórios:** firmware = sem git; câmera = sem git; EggBr = git com 6 commits e 79 alterações pendentes. `src/firebase_credentials.h` (chave privada da conta de serviço) nunca foi versionado e está no `.gitignore`.
- **Pipeline de publicação (repositório novo, sem histórico antigo):** `tools/publicar/construir_publico.py` monta `<SEU_CAMINHO>` a partir das pastas privadas (lista branca; sem `.pio`, `.bk`, `.elf`, credenciais; imagens de origem incerta trocadas por substitutos), troca dados pessoais/IDs/chaves web por marcadores, gera os `.example`, copia os modelos de `tools/publicar/modelos/raiz` (README, docs/01–07, SECURITY, CONTRIBUTING, .gitignore, .gitattributes) e roda uma **varredura de segredos** (sai com erro se achar algo). Preserva o `.git` da pasta de saída entre execuções.
- **Verificado:** varredura limpa; 78 arquivos no índice; links relativos dos .md sem quebra; o firmware público **compila** (Flash 27,9 %) com `firebase_credentials.example.h` copiado como `firebase_credentials.h`.
- **Pendentes (decisão do usuário):** público ou privado; licença; nome do repositório; identidade do autor nos commits (usar e-mail noreply do GitHub); manter ou não o histórico de desenvolvimento; imagens da galeria (excluídas por origem desconhecida); instalar `gh`/autenticar para eu publicar; revogar o token vazado.
- **Publicado (2026-10-15):** `https://github.com/Erdigital2022/chocaiot` — **público**, licença MIT, branch `main`, 2 commits (autor `Erdigital2022 <…@users.noreply.github.com>`), tópicos esp32/esp32-s3/incubator/firebase/pwa/platformio/iot/pid-controller. Criado com `gh` (login web do usuário; nenhum token passou pelo chat). Atualizar = rodar `python tools/publicar/construir_publico.py` (varredura de segredos) e fazer commit/push em `…/chocaiot-publico`.
- **Higiene feita:** remote do `EggBr` local reescrito sem token (`.git/config` sem `ghp_`). **Falta (ações de segurança/exclusão que cabem ao usuário):** revogar o token classic `ghp_…` em github.com/settings/tokens; excluir (ou arquivar) o repositório antigo `Erdigital2022/EggBr`; ativar push protection/2FA/proteção de branch.
