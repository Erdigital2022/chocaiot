// ChocaIoT — main.cpp — Fase 8b: Atuadores controlados pela nuvem (Firebase)
// Base: Fase 7 rev2 (boot + RTC + WiFi + Settings + Atuadores locais)
// Novo: stream do Realtime Database aplica /devices/chocaiot-01/actuators
//       (nuvem -> ESP32) em cima do controle local por toque, ja existente.

#include <Arduino.h>
#include <stdarg.h>
#include <Wire.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <Adafruit_SHT31.h>
#include <SPI.h>
#include <SD.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_sntp.h>   // Fase 92 -- callback de 'NTP sincronizou'
#include <driver/rtc_io.h>
#include "esp_core_dump.h"   // Fase 113 -- le o core dump (partição coredump) do ultimo Panic

#include <WiFiClientSecure.h>

#define ENABLE_SERVICE_AUTH
#define ENABLE_FIRESTORE
#define ENABLE_MESSAGING   // Fase 49 -- notificacoes push (FCM v1) enviadas pelo proprio aparelho
#include <FirebaseClient.h>

#include "firebase_credentials.h"
#include "guia_data.h"   // Fase 37 -- conteudo do Guia (gerado por tools/gerar_guia.js)
#include "fases_data.h"  // Fase 38 -- imagens da galeria (JPEG baseline, gerado por tools/gerar_fases.py)
#include <TJpg_Decoder.h>
#include <qrcode.h>   // Fase 61 -- QR da senha do WiFi (ricmoo/QRCode)
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>   // Fase 46 -- hash do PIN
#include <ESPmDNS.h>            // Fase 50 -- acha o modulo de camera na rede local (chocaiot-cam.local)
#include <HTTPClient.h>   // Fase 41 -- conteudo de arquivo do SD pro PWA
#include "wallpaper_data.h"  // Fase 39 -- wallpaper escurecido do screensaver (tools/gerar_wallpaper.py)

// Documento da chocadeira "Cobaia" no Firestore (mesmo doc que o PWA le/escreve
// em app.js — campo "atuadores.chN"). Trocar aqui se um dia houver mais de 1 chocadeira.
#define FIRESTORE_DOCUMENT_PATH "chocadeiras/ID_DA_SUA_CHOCADEIRA"

void syncTimeAndStartFirebase();  // definida na secao "FIREBASE — NUVEM <-> ATUADORES"
void setRelayAndSync(int ch, bool on);  // idem — usada pelo toque na tela de Atuadores
void logEvento(const char* categoria, const char* mensagem, bool importante);  // Fase 21 — definida na secao SD/LOGS

// ── Hardware ──────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;
uint16_t calData[5] = {418, 3396, 359, 3391, 7};

// Fase 20 — SHT31 (temperatura/umidade, sonda IP68). Mesmo barramento I2C
// da RTC (SDA=8, SCL=9 -- ver 2.4 do CLAUDE.md), endereco padrao 0x44.
// Sonda especifica comprada: fios vermelho=VCC, preto=GND (certeza, e o
// padrao universal) e amarelo/verde para SCL/SDA -- convencao mais comum
// pra esse tipo de sonda e amarelo=SCL, verde=SDA, mas ISSO NAO FOI
// CONFIRMADO no datasheet especifico dessa sonda. Se o sensor nao
// aparecer no barramento (sht31OK=false no Serial), a primeira coisa a
// tentar e inverter amarelo<->verde -- I2C errado so falha a
// comunicacao, nao tem risco de queimar nada (diferente de VCC/GND
// trocados, que sim, podem danificar o sensor).
Adafruit_SHT31 sht31 = Adafruit_SHT31();
bool sht31OK = false;
float temperaturaAtual = NAN, umidadeAtual = NAN;
unsigned long lastSht31OkMs = 0;   // Fase 52 -- ultima leitura valida (automacao desliga saidas se ficar velha)

// Fase 21 — limites (thresholds) de incubação, usados pra colorir os
// cards/tela de detalhe (verde=ideal, amarelo=alerta leve, vermelho=fora).
// Fase 25 — deixaram de ser #define fixas e viraram variáveis (nomes em
// MAIÚSCULO mantidos de propósito, mesmo não sendo mais constantes --
// minimiza risco de esquecer algum uso ao converter), editáveis na nova
// tela "Ajustes Finos" e persistidas em /thresholds.cfg (LittleFS, mesmo
// padrão de loadDisplayCfg()/saveDisplayCfg()). CO2 entrou junto (sem
// sensor ainda -- ver ADXL345/MH-Z19B em 2.3 -- os limites só ficam
// guardados, prontos pro dia que o sensor chegar). Margens de alerta
// continuam fixas por enquanto (não pedidas como configuráveis).
float TEMP_IDEAL_MIN = 37.2f;
float TEMP_IDEAL_MAX = 38.9f;
#define TEMP_ALERTA_MARGEM 1.0f   // ate 1C fora do ideal = amarelo; alem disso = vermelho
float UMID_IDEAL_MIN = 45.0f;
float UMID_IDEAL_MAX = 65.0f;
#define UMID_ALERTA_MARGEM 8.0f
float CO2_IDEAL_MIN = 400.0f;
float CO2_IDEAL_MAX = 1000.0f;

// Estatísticas do dia (Fase 22 -- antes era "desde o boot", agora reseta
// sozinho à meia-noite, igual ao gráfico do PWA -- ver
// resetarEstatisticasSensorSeVirouODia()). Sem RTC, fica "desde o boot"
// mesmo (fallback honesto, sem hora não dá pra saber quando é meia-noite).
float temperaturaMin=NAN, temperaturaMax=NAN, temperaturaSoma=0; uint32_t temperaturaN=0;
float umidadeMin=NAN, umidadeMax=NAN, umidadeSoma=0; uint32_t umidadeN=0;

// Fase 32 — temperatura interna do chip ESP32-S3 (temperatureRead(), API
// nativa do core Arduino, sensor dedicado -- nao disputa nada com
// WiFi/ADC). Min/Max/Media desde o BOOT (nao desde a meia-noite como o
// SHT31 acima -- isso aqui e diagnostico de hardware, nao dado de
// incubacao). Tela Sistema mostra os 3 juntos no lugar de "Reles ligados".
float espTempAtual=NAN, espTempMin=NAN, espTempMax=NAN, espTempSoma=0; uint32_t espTempN=0;

// Fase 22 — histórico do dia pra desenhar um gráfico simples na tela de
// detalhe (mesma ideia do sparkline do PWA, versão enxuta pro touchscreen).
// Fase 24 — intervalo de 20min era grande demais: o usuário mandou foto
// mostrando "90 leituras hoje" nas estatísticas (que atualizam a cada
// 5s) mas o gráfico travado em "Aguardando leituras..." -- só 1 amostra
// tinha sido capturada, porque o dispositivo tinha acabado de
// reiniciar (o buffer zera a cada boot) e nem 20min tinham se passado
// ainda. Reduzido pra 5min -> 288 amostras cobrem exatamente 24h; buffer
// arredondado pra 300 (folga pequena). RAM ainda insignificante:
// 300*4bytes*2 = 2,4KB.
#define SENSOR_HIST_MAX 300
#define SENSOR_HIST_INTERVAL_MS (5UL*60UL*1000UL)
float tempHistorico[SENSOR_HIST_MAX];
float umidHistorico[SENSOR_HIST_MAX];
int sensorHistCount = 0;
float co2Historico[SENSOR_HIST_MAX]; int co2HistCount=0;   // Fase 115 -- CO2 do dia (mesma cadencia do grafico de T/U)
float co2Min=NAN, co2Max=NAN, co2Soma=0; uint32_t co2N=0;
int diaEstatisticasAtual = -1;  // dia do mes (1-31) do ultimo reset; -1 = nunca resetou
// Fase 47 -- grafico de 7 dias (dados de /hist/*.csv agrupados em colunas) -- ver hist7Carregar()
#define H7_COLS 316
float h7T[H7_COLS], h7H[H7_COLS]; int h7N=0; uint32_t h7Pts=0;
float h7TMin=NAN,h7TMax=NAN,h7TSoma=0,h7HMin=NAN,h7HMax=NAN,h7HSoma=0;
float h7C[H7_COLS]; int h7CN=0; uint32_t h7CPts=0; float h7CMin=NAN,h7CMax=NAN,h7CSoma=0;   // Fase 115 -- CO2 dos ultimos 7 dias
bool sensorChart7d=false;     // tela do sensor: false = hoje (RAM), true = ultimos 7 dias (SD)
bool chartEixo7d=false;       // rotulos do eixo X em dias (usado por drawSensorHistChart)

// Tela de detalhe (Fase 21) -- toque no card Temperatura ou Umidade da home.
bool sensorDetalheEhTemp = true;
bool sensorDetalheCo2 = false;   // Fase 115 -- toque no card CO2
// Tela de Logs (Fase 21) -- mesmo padrao do cicloConfirmandoParar: true
// mostra a sub-tela de confirmacao de "Limpar" em vez do painel normal.
bool logsConfirmandoLimpar = false;
bool sdOK = false;  // declarado aqui (cedo) pq drawSettingsPanel() ja mostra o status

// Fase 29 — Logs em tela cheia, com paginacao e busca. Buffer carregado
// do SD ao ABRIR a tela (nao a cada redraw -- ver abrirTelaLogs()); mais
// recente primeiro (invertido em relacao ao lerUltimasLinhasLog(), que
// devolve em ordem cronologica). logsFiltradoIdx guarda os indices de
// logsBufLinhas que batem com logsSearchAtivo (vazio = tudo), paginados
// em grupos de LOGS_LINHAS_TELA.
#define LOGS_BUF_MAX_LINHAS 120
String logsBufLinhas[LOGS_BUF_MAX_LINHAS];
int logsBufCount = 0;
int logsFiltradoIdx[LOGS_BUF_MAX_LINHAS];
int logsFiltradoCount = 0;
int logsPage = 0;
#define LOGS_LINHAS_TELA 13
bool logsArrowOn = true;   // pulso das setinhas de paginacao (mesmo padrao da Fase 25 em Configuracoes)

bool logsBuscando = false;         // true = mostra campo de busca + teclado
char logsSearchBuf[24] = "";       // texto sendo digitado (antes de confirmar)
int logsSearchLen = 0;
char logsSearchAtivo[24] = "";     // filtro realmente aplicado
int logsSearchAtivoLen = 0;

// Fase 25 — paginação de Configurações (grid virou 2 páginas pra caber
// "Ajustes Finos" sem re-apertar tudo de novo -- página 0 voltou ao
// layout original de 4 linhas/botão maior, página 1 tem os itens mais
// novos). settingsArrowOn pulsa as setinhas de navegação (efeito visual
// pedido: "setas animadas").
int settingsPage = 0;
#define SETTINGS_TOTAL_PAGES 2
bool settingsArrowOn = true;

// Motivo do último reset — ver logResetReason() (diagnóstico de reboot
// aleatório), preenchido no início do setup() e mostrado na tela Sistema.
char lastResetReasonShort[16] = "Normal";
bool resetFoiAnormal = false;

// Fase 18 — supressão ABSOLUTA (local, em memória, não depende de rede
// nenhuma) de comandoDesligar logo após acordar via botão físico. Corrige
// o loop de deep sleep de vez: o fix anterior (Fase 16) só limpava o
// campo no Firestore, e se isso falhasse (rede lenta/instável logo após
// acordar), o poll normal lia o "true" antigo de novo e reentrava em
// sleep -- ainda uma corrida contra o tempo. Esta flag garante que, MESMO
// SE a limpeza no Firestore falhar, nenhum comando de desligar é
// processado durante a janela de graça (ver DESLIGAR_SUPRESSAO_MS) --
// zero dependência de rede pra evitar o loop.
#define DESLIGAR_SUPRESSAO_MS 30000
bool desligarSuprimidoNesteBoot = false;
unsigned long desligarSupressaoAteMs = 0;
// Fase 75 -- comandoDesligar que ja estava "true" na nuvem quando o aparelho ligou e STALE (a limpeza feita dentro do callback do
// poll nunca confirmava, como no Reiniciar da Fase 28b): ignora e limpa por retry no loop(). Antes, qualquer boot que nao fosse
// pelo botao BOOT (RST, queda de energia, abrir a serial) lia o true antigo e voltava a dormir, em loop.
bool desligarLimpezaPend = false, desligarPrimeiraLeitura = true;
unsigned long desligarLimpoEmMs = 0;

// Fase 19 — contador de resets anormais consecutivos (Brownout/Watchdog/Panic),
// em RTC_DATA_ATTR pra sobreviver a um reset (diferente das globais normais
// acima, que zeram). Serve pra 2 coisas: (1) diagnostico objetivo -- da pra
// ver na tela Sistema/Serial se o boot esta mesmo entrando num loop e quantas
// vezes; (2) mitigacao -- se ha resets anormais seguidos, o boot seguinte
// espera mais antes de ligar TFT/WiFi (maior consumidores de corrente), pra
// dar mais tempo do trilho de energia estabilizar. So RTC_DATA_ATTR sobrevive
// a isso; nao ha estado nenhum em RTC_DATA_ATTR usado pra logica de deep
// sleep (o "acordou via botao" vem do proprio hardware via
// esp_sleep_get_wakeup_cause(), nao de uma variavel nossa -- entao nao tem
// como esse contador ou qualquer outra variavel nossa "corromper" isso).
RTC_DATA_ATTR uint32_t bootLoopCounter = 0;

// Fase 28c (2ª correção — a 1ª, Fase 28b, ainda loopava em hardware real)
// — "Reiniciar" remoto (comandoReiniciar via Firestore).
//
// Fase 28b tinha assumido que RTC_DATA_ATTR sobrevive a ESP.restart(),
// igual bootLoopCounter (Fase 19) sobrevive a Brownout/Watchdog/Panic --
// só que o log real do usuário provou o contrario: a cada
// "Reset por software" (rst:0xc RTC_SW_CPU_RST, exatamente o que
// ESP.restart() produz), a flag RTC_DATA_ATTR voltava pro valor inicial
// (false) -- a mensagem de supressao nunca aparecia no Serial, e o
// dispositivo reiniciava nca hora de novo, todo boot. Ou seja: pro tipo
// especifico de reset que ESP.restart() gera nesta placa, RTC_DATA_ATTR
// NAO e confiavel (ao contrario do wake-cause do deep sleep, que e um
// registrador de HARDWARE de verdade, nao uma variavel em RAM).
//
// Corrigido usando LittleFS em vez de RTC_DATA_ATTR -- um arquivo
// marcador (`/reiniciando.flag`) sobrevive QUALQUER reset (e ate perda de
// energia), por ser flash, nao RAM. Grava-se o marcador antes do
// ESP.restart(); no boot seguinte (logo apos LittleFS.begin()), a mera
// PRESENCA do arquivo e suficiente pra saber "este boot e consequencia de
// um reinicio remoto", sem depender de nenhum registrador/variavel de
// reset especifico -- e apagar o arquivo e uma operacao local, instantanea,
// que nao depende de rede nenhuma (diferente da limpeza do campo no
// Firestore, que continua sendo so' "boa vontade"/higiene do dado na
// nuvem, feita depois, sem pressa).
#define REINICIANDO_FLAG_PATH "/reiniciando.flag"
#define REINICIAR_SUPRESSAO_VALVULA_MS (3UL*60UL*1000UL)  // 3min -- so' valvula de seguranca (Firestore inacessivel), nao o mecanismo principal
bool reiniciarSuprimidoNesteBoot = false;
unsigned long reiniciarSupressaoAteMs = 0;

// ── Relé 8 canais — ACTIVE LOW (confirmado na Fase 1/hardware) ─
#define PIN_RELE_CH1  1   // Resistência 220V/200W
#define PIN_RELE_CH2  2   // Umidificador 5V
#define PIN_RELE_CH3  38  // Bomba de água 12V
#define PIN_RELE_CH4  39  // Bomba de ar/O2 12V
#define PIN_RELE_CH5  40  // LED ovoscópio 220V
#define PIN_RELE_CH6  41  // Áudio MAX98357
#define PIN_RELE_CH7  42  // Fase 33 -- era "Reserva", virou Cooler do Sistema (12V DC)
#define PIN_RELE_CH8   7  // Fase 68 -- era "Reserva 2", virou Fans (ventilacao interna)

const int   RELAY_PINS[8]    = {PIN_RELE_CH1,PIN_RELE_CH2,PIN_RELE_CH3,PIN_RELE_CH4,
                                 PIN_RELE_CH5,PIN_RELE_CH6,PIN_RELE_CH7,PIN_RELE_CH8};
const char* RELAY_NAMES[8]   = {"Resistencia","Umidificador","Bomba Agua","Bomba Ar",
                                 "LED Ovoscopio","Audio","Cooler Sistema","Fans"};
const char* RELAY_VOLTAGE[8] = {"220V AC","5V DC","220V AC","3.7V DC",   // Fase 85: CH3 = bomba submersa VARIALL VA-BA01 (AC 110-220 V, 3 W, IPX8, 180 L/h, Hmax 0,35 m)
                                 "220V AC","5V DC","12V DC","12V DC"};
bool relayState[8] = {false,false,false,false,false,false,false,false};

// Fase 35 — controle automático do "Cooler Sistema" (CH7, índice 6) por
// histerese, baseado na temperatura do CHIP ESP32 (não no SHT31 -- este
// cooler é pro sistema/eletrônica, não pra incubação). Ver
// controlarCoolerPorHisterese() mais abaixo pro porquê dos limiares
// serem FIXOS e não vindos do Min/Max ao vivo da sessão.
#define COOLER_CH 6
#define ESP_TEMP_COOLER_ON  55.0f   // liga ao ATINGIR/passar disso
#define ESP_TEMP_COOLER_OFF 48.0f   // só desliga ao CAIR até aqui (histerese de 7C -- evita liga/desliga repetido perto de um único limiar)

// Fase 9 — coexistência toque local x nuvem: guarda quando cada canal foi
// tocado fisicamente pela última vez, pra comparar contra o instante em
// que um poll do Firestore foi DISPARADO (fbPollIssuedAtMs). Sem isso, um
// GET assíncrono que saiu ANTES de um toque mas cuja resposta chega DEPOIS
// carrega um valor velho e desfaz o toque que acabou de acontecer — ver
// applyActuatorsFromFirestore().
unsigned long lastLocalTouchMs[8] = {0,0,0,0,0,0,0,0};

// ═══════════════════════════════════════════════════════════════
// CICLO DE INCUBAÇÃO — espelha os mesmos campos do Firestore que o PWA
// escreve (cicloIniciadoEm/cicloPausadoEm/cicloDuracaoDias, ver app.js).
// O firmware so LE (nunca escreve) esses campos por enquanto — o botao de
// ciclo continua sendo do PWA; isso so alimenta o card "Ciclo" na home
// (bug relatado 2026-09-20: "campo Timer existe mas nao mostra progresso").
// Preenchido por applyCicloFromFirestore() (secao FIREBASE, mais abaixo).
// ═══════════════════════════════════════════════════════════════
bool fbCicloAtivo=false, fbCicloPausado=false;
bool cicloPollRecebido=false;   // Fase 48 -- ja veio ao menos 1 resposta do poll do ciclo desde o boot
time_t fbCicloIniciadoEm=0, fbCicloPausadoEm=0;
int fbCicloDuracaoDias=21;

// Fase 12 — bug do ciclo "reiniciando sozinho" ~15-30s depois de parar
// localmente: mesma corrida de dados já corrigida pros atuadores na Fase 9
// (lastLocalTouchMs/fbPollIssuedAtMs), mas nunca aplicada ao ciclo. Um
// pollCicloFromFirestore() disparado ANTES de um Parar/Pausar/Iniciar
// local, com a resposta chegando DEPOIS, carregava o estado antigo (ciclo
// ainda ativo) e "ressuscitava" o ciclo que acabou de ser parado. Ver
// applyCicloFromFirestore() e o handleTap() de S_CICLO.
unsigned long lastLocalCicloActionMs = 0;
unsigned long fbCicloPollIssuedAtMs = 0;

// calcularDiaCicloAtual()/drawCicloCard()/refreshCicloCardSeVisivel() ficam
// definidas mais abaixo (perto de drawMainScreen), pois dependem das cores
// (cBg/cSub/...) que só são declaradas depois deste ponto no arquivo.
void refreshCicloCardSeVisivel();
void drawLogsScreen();   // Fase 21 — definida na secao SD/LOGS, usada antes por redrawCurrentScreen()
void drawArqLista(); void drawArqAcao(); void drawArqNome(); void drawArqConfirmaDel();   // Fase 41
void drawWiz(); void drawKbTxtScreen(); void drawTiposLista(); void drawTipoEdit();   // Fase 42
void drawAudioScreen();   // Fase 22 — idem, definidas mais abaixo
void drawAlertasScreen();
extern bool autoAtivo, autoSoCiclo;
void abrirKbTxt(const char* titulo,const char* dica,int etapa,char* buf,int max,void(*ok)(),void(*voltar)());
void drawKbTxtScreen();
void drawAdv(); void drawAdvEdit(); void handleAdvTap(int tx,int ty); void handleAdvEditTap(int tx,int ty); void abrirAdv();
void drawAuto(); void drawAutoEdit(); void handleAutoTap(int tx,int ty); void handleAutoEditTap(int tx,int ty);   // Fase 52
void autoDoPoll(const String& au,unsigned long pollIssuedAtMs); void automacaoTick(); void abrirAuto(); void autoCarregarCfg();
void drawHardwareScreen(); void handleHardwareTap(int tx,int ty);   // Fase 78
extern float co2Atual; extern unsigned long lastCo2OkMs;   // Fase 99 -- MH-Z19B (definidos mais abaixo)
void pwmAlternar();   // Fase 99 -- tecla serial 'P'
bool adxlAutoAtivo();   // Fase 95 -- deteccao da tampa pelo ADXL ligada, calibrada e saudavel (Status vira automatico)
int adxlSettingsDot();   // Fase 93 -- ponto de estado do icone "Tampa" nas Configuracoes
void drawTxtView(); void drawImgView(); void drawArqWall(); void drawSeg(); void drawPin(); void drawLock(); void drawRels();   // Fases 44-48

// ── Estados ───────────────────────────────────────────────────
enum AppState {
  S_MAIN, S_SETTINGS,
  S_WIFI_LIST, S_WIFI_PASS, S_WIFI_DETAIL, S_WIFI_ADD,
  S_SYS_INFO, S_ATUADORES, S_CONNECTING,
  S_DATETIME, S_MEMORIA, S_DISPLAY, S_CAMERA, S_SCREENSAVER, S_CICLO,
  S_SENSOR_DETAIL, S_LOGS,   // Fase 21
  S_AUDIO, S_ALERTAS,        // Fase 22
  S_GUIA, S_GUIA_TEXTO,      // Fase 37 -- Guia de incubacao (lista | texto)
  S_FASES,                   // Fase 38 -- galeria "Fases do Pintinho"
  S_ARQ, S_ARQ_ACAO, S_ARQ_NOME, S_ARQ_DEL,  // Fase 41 -- gerenciador de arquivos do SD
  S_CICLO_WIZ, S_KBTXT, S_TIPOS, S_TIPO_EDIT, // Fase 42 -- wizard do ciclo, teclado de texto, tipos de ovos
  S_TXTVIEW, S_IMGVIEW, S_ARQ_WALL,           // Fase 44/45 -- leitor de texto, visualizador de imagem, destino do wallpaper
  S_SEG, S_PIN, S_LOCK,                       // Fase 46 -- seguranca, entrada de PIN, tela de bloqueio
  S_RELS,                                     // Fase 48 -- relatorios de ciclo
  S_AUTO, S_AUTO_EDIT,                        // Fase 52 -- automacao (lista de reles | edicao de regra)
  S_HIST, S_HISTL,                            // Fase 53/55 -- grafico de um ciclo | lista de ciclos (historico)
  S_ADV, S_ADV_EDIT,                          // Fase 57 -- regras avancadas (lista | editor)
  S_STATUS,                                   // Fase 77 -- detalhe do status do ambiente (+ confirmar ovos colocados)
  S_HARDWARE,                                 // Fase 78 -- Configuracoes > Hardware (aquecedor, umidificador, caixa)
  S_ADXL,                                     // Fase 93 -- Configuracoes > Tampa (sensor ADXL345)
  S_FANS,                                     // Fase 102 -- Configuracoes > Fans (PWM dos IRF520)
  S_FANREGRAS                                 // Fase 112 -- regras das funcoes dos fans
};
AppState appState = S_MAIN;
bool pinEmRecuperacao(AppState st);   // Fase 53
extern bool pinAtivo;                              // Fase 46 -- bloqueio por PIN ativo
void entrarBloqueio(AppState destinoAposDesbloquear);
extern bool homeWallAtiva;                          // Fase 45 -- wallpaper da tela principal
void homeFill(int x,int y,int w,int h);
void homeTexto(uint16_t fg);
bool wallGarantir(int slot);
void fansDraw(); void fansTap(int tx,int ty); bool fansAtivo();   // Fase 102
void fanRegrasDraw(); void fanRegrasTap(int tx,int ty); void fansRegrasTick();   // Fase 112
void telaComandosTick(); String painelCfgStr();   // Fase 104
int fanRpmAtual();   // Fase 110 -- rpm do fan 1 (card Status)
extern uint16_t autoRetomaMin; bool fansTodosZero();   // Fase 111
void drawDisplayScreen(); void handleDisplayTap(int tx,int ty); void abrirArquivos(); void abrirRelatorios();
void arqToast(const char* msg,bool ok);
static void sanitizarNome(char* s);
void nomeDoPoll(const String& n); void nomeAbrirEdicao(); void abrirHistLista(bool temp); void handleHistListaTap(int tx,int ty); void drawHistLista(); void abrirHistCiclo();
void cicloIdxGravar(time_t ini,time_t fim,char sit,const char* nome);
long tzOffsetSeg();
extern char fbCicloNome[24], fbCicloTipo[20];
String fimTipoRecebido="";   // Fase 55 -- "parar|ms" ou "lixeira|ms" escrito pelo PWA (cmd.fimTipo)
extern char chocNome[25];
bool cicloComTipoValido();   // Fase 54
void cicloPulso(); void debugTela(char c);
void redrawCurrentScreen();   // Fase 67 -- sdManter() (bem antes na secao SD) precisa chamar ao detectar remocao/reconexao
bool debugCicloFalso=false;   // so depuracao (serial 'C'): ciclo de mentira local, sem tocar na nuvem nem gerar alertas
void aplicarLimitesDoTipo();
void co2Limites(float& okMax,float& altoMin);   // Fase 115
bool extrairCampoTimestamp(const String &json, const char* nomeCampo, String &saida);
time_t parseRfc3339(const String &s);
void cameraPedirFoto(); void handleCameraTap(int tx,int ty); bool cameraOnline();
static bool faseJpgOut(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bmp);   // Fase 38 -- definida mais abaixo; usada cedo pelo card Guia (drawGuiaThumb)
void drawGuiaThumb(int x,int y);   // Fase 67 -- miniatura do dia 21 no lugar do livro aberto
#define AMB_ESTAVEL_MS (5UL*60UL*1000UL)   // Fase 71: tempo dentro da faixa (era 10 min) ate "ambiente pronto"
#define AMB_FORA_TOL_MS 60000UL            // saidas da faixa menores que isso nao zeram o relogio
int autoManualPend=-1;   // Fase 110 -- canal automatizado alterado a mao: automacao desligada (persistencia roda no loop, fora de callback)
void manualHold(int ch,bool on);   // Fase 106 -- usuario mexeu num canal: a automacao respeita (definida junto do controle por pulsos)
bool pulsoGerencia[8]={false}, pulsoNuvemVal[8]={false};   // Fase 72 -- canais sob controle por pulsos (a nuvem so ve a "sessao", nao cada pulso)
unsigned ambEstavelSeg=0;                  // segundos estaveis ate agora (vai no heartbeat -> PWA mostra o progresso)
// Fase 77 -- ciclo em PREPARO: a chocadeira estabiliza SEM ovos; o ciclo so comeca (data/hora, dia 1) quando o usuario confirma "ovos colocados"
bool manterAtivo=false, manterSyncPend=false; char ultTipoNome[20]=""; unsigned long cicloParouMs=0; extern bool cicloSyncPend;   // Fase 79 -- manter o ambiente do ultimo ciclo
int tampaEst=0; unsigned long tampaIniMs=0; uint8_t tampaCmd=0, tampaSyncPend=0; bool tampaOrigemAdxl=false; unsigned tampaSeg=0;   // Fase 82 -- tampa: 0 fechada · 1 aberta · 2 recuperando
uint8_t aguaEst=0; bool aguaPausa=false; char aguaTxt[132]="";   // Fase 83 -- agua do umidificador: 0 ok · 1 testando a bomba · 2 SEM AGUA (alerta); aguaPausa = o PI nao comanda o umidificador
void debugHardware(int pag);   // Fase 86 (88: 3 paginas)
// Fase 98 -- tela "Tampa" do PWA: cmd.adxlVivo (a tela esta aberta: publicar adxlLive a cada 2,5 s), cmd.adxlCfg ("<ms>|det|tol|sens"), cmd.adxlCal ("<ms>|F" = FECHADA, "<ms>|O" = FORA)
String adxlVivoUlt="", adxlCfgUlt="", adxlCalUlt=""; bool adxlVivoInit=false, adxlCfgInit=false, adxlCalInit=false; unsigned long adxlVivoMs=0; char adxlCfgPend[48]="", adxlCalPend[24]="";
String fansRegUlt=""; bool fansRegInit=false; char fansRegPend[150]="";   // Fase 112 -- cmd.fansRegras do PWA
String telaSetUlt="", fansSetUlt="", fansVivoUlt=""; bool telaSetInit=false, fansSetInit=false, fansVivoInit=false; char telaSetPend[48]="", fansSetPend[40]=""; unsigned long fansVivoMs=0;   // Fase 104 -- cmd.telaSet / cmd.fansSet do PWA
String conviteUlt="", segUlt=""; bool conviteInit=false, segInit=false; char convitePend[96]="";   // Fase 88 -- aviso de convite aceito (cmd.convite)
bool bombaNuvemLimpar=false;   // Fase 87 -- a nuvem ainda guarda CH3=true: reescrever false (feito no loop, fora de callback)
void aguaSimular(); void aguaAlternaBomba();   // Fase 83 (teclas de depuracao W e B)
char ambDiagTxt[132]="", ambDiagCurto[18]="";   // Fase 80 -- diagnostico do ambiente (texto completo / etiqueta curta do card Status)
bool canalTermostato(int ch);   // Fase 80
bool fbCicloPreparo=false; int fbCicloVolL=0;   // volume da caixa em litros (0 = nao informado)
void drawStatusDetalhe(bool full); void handleStatusTap(int tx,int ty); void cicloConfirmarOvos();
bool telaSempreLigada=false;   // Fase 77 -- "manter tela ligada" (nunca entra no screensaver)
uint8_t brilhoPct=100;         // Fase 101 -- brilho do backlight (10..100 %), PWM no GPIO47 -> pino LED do display
uint16_t dimSegundos=30;       // Fase 103 -- sem toque por N s: reduz o brilho (0 = nunca); sempre < screenOffTimeoutSec
static int telaSub=0;          // Fase 103 -- 0 = tela Tela principal, 1 = subtela Papel de parede
bool relogioSempre=false;      // Fase 104 -- repouso mostra o relogio (brilho reduzido) em vez de apagar a tela
int ambEstado=0;   // Fase 69 -- 0 sem ciclo/nao se aplica, 1 aguardando o ambiente estabilizar (NAO por os ovos), 2 pronto
void drawStatusCard(); bool ambManterOn(); bool ultTipoValido();
// Fase 65 -- visao computacional leve (heuristica, SEM modelo treinado -- nao ha fotos reais da chocadeira
// ainda pra calibrar contagem/classificacao de ovos, ver CLAUDE.md) + rotulo de fase do ciclo. So gera
// ALERTA/LOG (nunca muda temperatura/umidade/viragem sozinha -- isso continua vindo so de sensores +
// parametros do tipo de ovo). Declarado cedo porque drawCameraScreen() e applyActuatorsFromFirestore()
// (bem antes da secao completa da camera, mais abaixo) ja leem/escrevem estas variaveis.
enum FaseCiclo : uint8_t { FASE_SEM_CICLO, FASE_INCUBACAO, FASE_PRE_ECLOSAO, FASE_ECLOSAO };
FaseCiclo faseCicloAtual();
const char* faseCicloNome(FaseCiclo f);
void camAutoCfgSalvar();
bool camAutoAtivo=true;           // captura periodica alem do pedido manual/PWA -- controlavel (tela Camera + cmd.camAuto)
unsigned long lastLocalCamAutoMs=0;
float camVisUltimoScore=-1, camVisBrilho=-1;   // -1 = sem dado ainda (nenhuma foto analisada nesta sessao)
unsigned long camVisSemMovimentoDesdeMs=0;     // 0 = "com movimento" (ou ainda sem dado)
// Fase 66 -- "ao vivo" local (tela do aparelho): pede 1 quadro a cada poucos segundos so enquanto a tela
// Camera esta aberta E o toggle "Ao vivo" ligado -- decodifica em tela cheia, NUNCA vai pra nuvem/SD
// (ver cameraServico(), origem "live"). Streaming de verdade (continuo) e so no navegador, direto na
// camera (porta 81) -- o aparelho, com 1 CPU dividida com toda a chocadeira, so amostra devagar.
bool camLiveAtivo=false;
volatile bool camLiveTemQuadro=false;
extern uint8_t* camFotoBuf; extern volatile size_t camFotoLen;   // definidos mais abaixo, na secao completa da camera
static void arqBotao(int x,int y,int w,int h,const char* txt,uint16_t bg,uint16_t fg,uint16_t brd);
String camReqPend="", camReqUltimo=""; bool camReqInit=false;   // Fase 50 -- pedido de foto vindo do PWA (campo cameraReq)
volatile bool camOnlineLan=false;                                // modulo de camera respondendo na rede local
bool pinOffPend=false; String pinOffUltimo=""; bool pinOffInit=false;   // Fase 53 -- desativar o PIN pelo PWA (cmd.pinOff)
String upReqPend="", upReqUltimo=""; bool upReqInit=false;      // Fase 51 -- envio de midia do PWA (cmd.upReq)
void uploadServico(); void serialDebug();
void cameraServico();
void histAmostra(float t,float h); void sdManter(); void histForcar();                 // Fase 47/53
void drawHist(); void handleHistTap(int tx,int ty);
extern char histRotIni[10], histRotMid[10], histRotFim[10];
void cicloStatsAmostra(float t,float h); void cicloContaAlerta(bool temp); void cicloContaCooler();   // Fase 48
void pushEnfileirar(int tipo,const char* titulo,const char* corpo);   // Fase 49
void handleAlertasTap(int tx,int ty); void handleRelsTap(int tx,int ty);
void handleSegTap(int tx,int ty); void handlePinTap(int tx,int ty); void handleLockTap(int tx,int ty);
void handleTxtViewTap(int tx,int ty); void handleImgViewTap(int tx,int ty); void handleArqWallTap(int tx,int ty);
void segTick(); void segServico(); void histServico(); void pushServico(); void cicloRelatorioServico();
void alertCfgCarregar(); void cicloStatsCarregar();
extern bool cicloPollRecebido;
void pushTesteRecebido(const String& id,bool presente);   // Fase 49
void segEmailAtualiza(const String* em); void recVerificar(const String& cod);   // Fase 46 (chamadas do poll)
void wallSlotRestore(int slot,int x,int y,int w,int h,uint16_t corLisa);

// Controle Local do Ciclo (Fase 11): true = mostrando a sub-tela de
// confirmação de "Parar" dentro de S_CICLO, em vez do painel normal.
bool cicloConfirmandoParar = false;

// ── WiFi ──────────────────────────────────────────────────────
char savedSSID[33]="", savedPass[65]="", selSSID[33]="";
bool wifiManualOff=false;   // Fase 61: "Desconectar" -- o watchdog nao reconecta ate a pessoa escolher a rede de novo
char passBuf[65]=""; int passLen=0;
char manualSSID[33]=""; int manualSSIDLen=0;
bool manualIsOpen=false, showPass=false, detailShowPass=false;
bool wifiOK=false; int numNets=0, listOff=0;
bool typingSSID=false;

// ── Teclado virtual ───────────────────────────────────────────
uint8_t kbMode=0;
const char KBL[3][11]={"qwertyuiop","asdfghjkl","zxcvbnm"};
const char KBU[3][11]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
const char KBN[3][11]={"1234567890","!@#$%^&*()","_-=+,./;:'"};
#define KH 26
#define KG  2
#define KR1Y  92
#define KR2Y (KR1Y+KH+KG)
#define KR3Y (KR2Y+KH+KG)
#define KR4Y (KR3Y+KH+KG)

// ── RTC ───────────────────────────────────────────────────────
bool rtcOK=false; bool rtcHoraConfiavel=false; char clockStr[10]="--:--:--";   // rtcHoraConfiavel: DS3231 nao perdeu a hora (Fase 92)
unsigned long lastClkMs=0, uptimeStart=0;

// ── Ajuste manual de Data/Hora ──────────────────────────────────
int editYear=2026, editMonth=1, editDay=1, editHour=0, editMinute=0;

// ── Memória — refresh periodico da tela ─────────────────────────
unsigned long lastMemRefreshMs=0;
#define MEM_REFRESH_MS 500

// ── Energia da tela — screensaver, nao sleep ─────────────────────
// Testado por continuidade (16/09/2026): o pino LED do backlight esta
// ligado direto no 3V3 do ESP32-S3, sem GPIO no caminho — nao da pra
// fazer PWM de brilho nem apagar o backlight via software. Em vez disso,
// depois de inativo a tela fica preta (LCD bloqueia a luz de fundo pixel
// a pixel, entao fica escura de verdade mesmo com o backlight sempre
// aceso) com um relogio central discreto + o logo "ChocaIoT" flutuando
// (estilo screensaver de DVD). Qualquer toque volta pra tela de antes.
unsigned long lastActivityMs=0;
unsigned long screenOffTimeoutSec=120;   // ajustavel na tela "Tela" das Configuracoes
AppState stateBeforeSaver=S_MAIN;
unsigned long saverLastClockMs=0;
int saverUltimoMinuto=-1;   // Fase 40 -- ultimo minuto desenhado no screensaver

// ── Touch ─────────────────────────────────────────────────────
bool touching=false;
uint16_t txS=0,tyS=0,txL=0,tyL=0;
unsigned long tDown=0, lastTap=0;
#define DEBOUNCE_MS 200

// ── WiFi icon anim ────────────────────────────────────────────
bool wifiIconPhase=true, prevWifiState=false;
unsigned long lastWifiBlink=0;
#define WIFI_ICX 148
#define WIFI_ICY   7
#define MENU_BTN_X 276

// ── Atuadores — layout ────────────────────────────────────────
#define AT_VIS     4    // canais visiveis por pagina (mesmo padrao da lista WiFi)
#define AT_ROW_H   42   // Fase 110: 52 -> 42 para caber a chave da automacao no topo (4 canais por pagina continuam)
#define AT_ROW_Y0  56
int atPageOff = 0;

void nextAtuadoresPage(){
  atPageOff += AT_VIS;
  if(atPageOff >= 8) atPageOff = 0;   // cicla de volta pro inicio
}

// ── Cores ─────────────────────────────────────────────────────
uint16_t cBg,cCard,cLogo,cSub,cAccent,cGreen,cRed,cKey,cSp,cShift,cClk,cBt;
void initColors(){
  cBg    =tft.color565(  8,  8, 20); cCard  =tft.color565( 18, 18, 45);
  cLogo  =tft.color565(255,210, 40); cSub   =tft.color565(145,145,145);
  cAccent=tft.color565( 55, 85,200); cGreen =tft.color565( 45,190, 65);
  cRed   =tft.color565(195, 48, 48); cKey   =tft.color565( 46, 46, 76);
  cSp    =tft.color565( 62, 62,102); cShift =tft.color565( 92, 92,150);
  cClk   =tft.color565( 80,195,255); cBt    =tft.color565(120,160,255);
}

// Fase 21 — cor por faixa (verde=ideal, amarelo=alerta leve, vermelho=fora
// da margem). Usado pelos cards de Temperatura/Umidade e pela tela de
// detalhe. cAmarelo nao e uma cor global (so usada aqui), por isso local.
uint16_t corPorFaixa(float v, float idealMin, float idealMax, float margem){
  if(isnan(v)) return cSub;
  if(v>=idealMin && v<=idealMax) return cGreen;
  float fora = (v<idealMin) ? (idealMin-v) : (v-idealMax);
  return (fora<=margem) ? tft.color565(230,180,60) : cRed;
}

// ═══════════════════════════════════════════════════════════════
// RELÉ — SEGURANÇA: chamado ANTES de qualquer outra coisa no setup
// ═══════════════════════════════════════════════════════════════
void initRelays() {
  // Fase 107: solta os pinos travados antes de dormir (gpio_hold) -- sem isso o firmware nao consegue mais comandar relés/backlight/fans depois de acordar
  gpio_deep_sleep_hold_dis();
  for (int i = 0; i < 8; i++) gpio_hold_dis((gpio_num_t)RELAY_PINS[i]);
  { const int ex[4]={47,4,5,6}; for(int i=0;i<4;i++) gpio_hold_dis((gpio_num_t)ex[i]); }
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH);   // ACTIVE LOW -> HIGH = desligado
  }
}
void setRelay(int ch, bool on) {
  if (ch < 0 || ch > 7) return;
  relayState[ch] = on;
  digitalWrite(RELAY_PINS[ch], on ? LOW : HIGH);  // ACTIVE LOW
}

// ═══════════════════════════════════════════════════════════════
// BOOT ANIMATION
// ═══════════════════════════════════════════════════════════════
void drawStars(){randomSeed(42);for(int i=0;i<55;i++){uint8_t b=random(80,255);tft.drawPixel(random(320),random(240),tft.color565(b,b,b));}}
void drawEgg(bool cr){
  uint16_t g1=tft.color565(60,50,10),g2=tft.color565(100,85,20),eg=tft.color565(252,246,228),ck=tft.color565(90,75,55);
  tft.drawEllipse(160,105,50,64,g1);tft.drawEllipse(160,105,48,62,g1);tft.drawEllipse(160,105,46,60,g2);
  tft.fillEllipse(160,105,44,57,eg);for(int i=0;i<6;i++) tft.drawEllipse(160,105,44-i,57-i,tft.color565(252-i*8,246-i*8,228-i*12));
  tft.fillEllipse(148,88,10,7,TFT_WHITE);tft.fillEllipse(148,88,7,4,TFT_WHITE);
  if(!cr) return;
  tft.drawLine(160,50,155,62,ck);tft.drawLine(155,62,165,74,ck);tft.drawLine(165,74,157,86,ck);tft.drawLine(157,86,162,98,ck);
  tft.drawLine(155,62,147,68,ck);tft.drawLine(165,74,173,79,ck);
}
void drawChick(int p){
  int cx=160,cy=130;uint16_t y1=tft.color565(255,210,0),y2=tft.color565(240,175,0),or_=tft.color565(255,120,0);
  if(p>=1) tft.fillCircle(cx,cy,32,y1);
  if(p>=2){tft.fillCircle(cx,cy-42,22,y1);tft.fillCircle(cx-9,cy-45,4,TFT_BLACK);tft.fillCircle(cx+9,cy-45,4,TFT_BLACK);tft.fillCircle(cx-8,cy-46,2,TFT_WHITE);tft.fillCircle(cx+10,cy-46,2,TFT_WHITE);tft.fillTriangle(cx-5,cy-40,cx+5,cy-40,cx,cy-32,or_);}
  if(p>=3){tft.fillEllipse(cx-42,cy-5,18,11,y2);tft.fillEllipse(cx+42,cy-5,18,11,y2);for(int d=-1;d<=1;d++) tft.drawLine(cx+d*12,cy+32,cx+d*16,cy+46,or_);}
  if(p>=4){tft.drawLine(cx,cy-64,cx-4,cy-74,y1);tft.drawLine(cx,cy-64,cx+5,cy-76,y1);tft.drawLine(cx,cy-64,cx,cy-78,y1);tft.fillCircle(cx-4,cy-75,3,y1);tft.fillCircle(cx+5,cy-77,3,y1);tft.fillCircle(cx,cy-79,3,y1);}
}
void bootAnimation(){
  tft.fillScreen(cBg);drawStars();delay(250);drawEgg(false);delay(500);drawEgg(true);delay(400);
  tft.fillScreen(TFT_WHITE);delay(70);tft.fillScreen(cBg);drawStars();delay(50);tft.fillScreen(TFT_WHITE);delay(50);tft.fillScreen(cBg);drawStars();delay(60);
  for(int p=1;p<=4;p++){drawChick(p);delay(180);}
  delay(300);tft.setTextColor(cLogo,cBg);tft.setTextSize(3);tft.setCursor(55,188);tft.print("ChocaIoT");
  delay(400);tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(82,218);tft.print("Sistema iniciado");delay(1200);
}

// ═══════════════════════════════════════════════════════════════
// LITTLEFS
// ═══════════════════════════════════════════════════════════════
void loadWifiCreds(){
  fs::File f=LittleFS.open("/wifi.cfg","r");if(!f) return;
  String s=f.readStringUntil('\n');s.trim();s.toCharArray(savedSSID,33);
  s=f.readStringUntil('\n');s.trim();s.toCharArray(savedPass,65);f.close();
}
void saveWifiCreds(const char* ss,const char* pw){
  fs::File f=LittleFS.open("/wifi.cfg","w");if(!f) return;f.println(ss);f.println(pw);f.close();
}
// Fase 101 -- backlight por PWM. O pino LED do display NAO e o LED: e a entrada (base, via R6 1 kOhm) de um transistor NPN (Q1) da propria placa,
// que chaveia o backlight pelo lado negativo (R5 limita a corrente). Logo o GPIO47 liga DIRETO no pino LED (3,3 V satura o transistor).
// NAO manter +5 V no pino LED junto com o GPIO47. 5 kHz / 8 bits (canal LEDC 4; os canais 0-2 sao dos fans).
#define BL_PIN 47
#define BL_CH  4
#define BL_REPOUSO_PCT 20   // Fase 101b/103 -- brilho maximo quando reduzido por falta de toque
static bool blIniciado=false; static uint8_t blEstagio=0;   // Fase 103: 0 normal · 1 reduzido (sem toque) · 2 apagado (screensaver)
void blAplica(){ if(!blIniciado) return; uint32_t p=brilhoPct; if(blEstagio==1 && p>BL_REPOUSO_PCT) p=BL_REPOUSO_PCT; if(blEstagio==2) p=0; ledcWrite(BL_CH,p*255U/100U); }
void blInit(){ ledcSetup(BL_CH,5000,8); ledcAttachPin(BL_PIN,BL_CH); blIniciado=true; blAplica(); }
void loadDisplayCfg(){
  fs::File f=LittleFS.open("/display.cfg","r");if(!f) return;
  unsigned long v=f.readStringUntil('\n').toInt(); String l2=f.readStringUntil('\n'); String l3=f.readStringUntil('\n'); String l4=f.readStringUntil('\n'); String l5=f.readStringUntil('\n'); f.close();
  if(v>=15 && v<=3600) screenOffTimeoutSec=v;   // ignora arquivo corrompido/fora da faixa
  telaSempreLigada=(l2.toInt()==1);
  l3.trim(); if(l3.length()){ int b=l3.toInt(); if(b>=10 && b<=100) brilhoPct=(uint8_t)b; }   // Fase 101
  l4.trim(); if(l4.length()){ int d=l4.toInt(); if(d>=0 && d<=3600) dimSegundos=(uint16_t)d; }   // Fase 103
  relogioSempre=(l5.toInt()==1);   // Fase 104
  if(dimSegundos>0 && screenOffTimeoutSec>10 && dimSegundos>screenOffTimeoutSec-10) dimSegundos=(uint16_t)(screenOffTimeoutSec-10);
}
void saveDisplayCfg(){
  fs::File f=LittleFS.open("/display.cfg","w");if(!f) return;f.println(screenOffTimeoutSec);f.println(telaSempreLigada?1:0);f.println(brilhoPct);f.println(dimSegundos);f.println(relogioSempre?1:0);f.close();
}

// ═══════════════════════════════════════════════════════════════
// ÍCONES
// ═══════════════════════════════════════════════════════════════
void drawWifiIcon(int x,int y,bool connected,bool show){
  tft.fillRect(x,y,18,14,cCard);if(!show) return;
  uint16_t c=connected?cGreen:cRed;
  int hs[]={4,7,10,13};
  for(int i=0;i<4;i++) tft.fillRect(x+i*5,y+14-hs[i],3,hs[i],(connected||i==0)?c:tft.color565(36,36,62));
  if(!connected){tft.drawLine(x,y+12,x+16,y+2,cRed);tft.drawLine(x,y+13,x+17,y+2,cRed);}
}

// Animação "conectando" (Fase 16) — substitui a antiga tela preta de boot
// com "Conectando: SSID". As barras acendem uma de cada vez em loop, num
// azul neutro (nem vermelho=erro, nem verde=sucesso), enquanto WiFi.begin()
// ainda não resolveu.
void drawWifiConnectingIcon(int x,int y,int frame){
  tft.fillRect(x,y,18,14,cCard);
  uint16_t c=tft.color565(90,170,255);
  int hs[]={4,7,10,13};
  int lit=(frame%4)+1;
  for(int i=0;i<4;i++) tft.fillRect(x+i*5,y+14-hs[i],3,hs[i],(i<lit)?c:tft.color565(36,36,62));
}
void drawMenuIcon(int x,int y,int w,int h,uint16_t bg,uint16_t fg){
  tft.fillRoundRect(x,y,w,h,4,bg);for(int i=0;i<3;i++) tft.fillRect(x+7,y+5+i*6,w-14,2,fg);
}
void drawXBtn(int cx,int cy,int r,uint16_t c){for(int d=0;d<2;d++){tft.drawLine(cx-r+d,cy-r,cx+r+d,cy+r,c);tft.drawLine(cx+r+d,cy-r,cx-r+d,cy+r,c);}}
void drawClockIcon(int cx,int cy,uint16_t c){tft.drawCircle(cx,cy,8,c);tft.drawLine(cx,cy,cx,cy-5,c);tft.drawLine(cx,cy,cx+4,cy+2,c);}

// Toggle grande — alvo de toque confortavel para dedo (sem caneta)
void drawToggle(int x,int y,bool on,uint16_t onColor){
  uint16_t bg = on ? onColor : tft.color565(60,60,60);
  tft.fillRoundRect(x,y,64,28,14,bg);
  tft.drawRoundRect(x,y,64,28,14,tft.color565(20,20,20));
  int cx = on ? x+48 : x+16;
  tft.fillCircle(cx,y+14,12,TFT_WHITE);
  tft.drawCircle(cx,y+14,12,tft.color565(190,190,190));
}

// ═══════════════════════════════════════════════════════════════
// TELA DE ATUADORES
// ═══════════════════════════════════════════════════════════════
// ── Fase 64: padrao visual novo (tela cheia, faixa fina no topo, cartoes como em "Arquivos") ──
// Voltar = tocar na faixa do topo (lado esquerdo). Nada de botao "Voltar" nem rodape.
static void uiTopo(const char* titulo,const char* dir=nullptr,uint16_t dirCor=0){
  uint16_t bg=tft.color565(8,8,26);
  tft.fillRect(0,0,320,18,bg);
  tft.fillTriangle(10,4,10,14,3,9,tft.color565(140,140,200));   // seta de voltar
  tft.setTextSize(1); tft.setTextColor(cLogo,bg); tft.setCursor(16,5); tft.print(titulo);
  if(dir){ tft.setTextColor(dirCor?dirCor:cClk,bg); tft.setCursor(314-tft.textWidth(dir),5); tft.print(dir); }
}
static bool uiVoltar(int tx,int ty){ return ty<20 && tx<110; }
static void uiCard(int x,int y,int w,int h,bool press=false){
  tft.fillRoundRect(x,y,w,h,8,press?tft.color565(40,40,90):tft.color565(16,16,46));
  tft.drawRoundRect(x,y,w,h,8,tft.color565(46,46,100));
}

void drawAtuadoresScreen(){
  tft.fillScreen(cBg);
  char pg[10]; snprintf(pg,sizeof(pg),"%d/2 >",(atPageOff/AT_VIS)+1);
  uiTopo("ATUADORES",pg);
  // Fase 110 -- chave da AUTOMACAO (mesma funcao de "Automacao ativa"): desliga sozinha quando o usuario mexe num canal automatizado
  { uint16_t bgA=autoAtivo?tft.color565(10,36,16):tft.color565(40,30,10), brA=autoAtivo?cGreen:tft.color565(200,150,50);
    tft.fillRoundRect(6,22,308,30,9,bgA); tft.drawRoundRect(6,22,308,30,9,brA);
    tft.setTextColor(TFT_WHITE,bgA);tft.setTextSize(2);tft.setCursor(16,30);tft.print("Automacao");
    tft.setTextSize(1);tft.setTextColor(autoAtivo?cGreen:tft.color565(255,190,90),bgA);tft.setCursor(150,34);tft.print(autoAtivo?"ATIVA":"DESLIGADA");
    drawToggle(236,23,autoAtivo,cGreen); }
  for(int i=0;i<AT_VIS && (atPageOff+i)<8; i++){
    int ch = atPageOff+i;
    int y = AT_ROW_Y0 + i*AT_ROW_H;
    bool on=relayState[ch];
    bool is220 = (strstr(RELAY_VOLTAGE[ch],"220")!=nullptr);
    bool is12  = (strstr(RELAY_VOLTAGE[ch],"12") !=nullptr);
    bool is5   = (strstr(RELAY_VOLTAGE[ch],"5")  !=nullptr) && !is12;
    uint16_t vc = is220 ? tft.color565(230,90,70) : is12 ? tft.color565(85,150,225) : is5 ? tft.color565(95,195,95) : tft.color565(120,120,120);
    uint16_t bg = on ? tft.color565(10,36,16) : tft.color565(16,16,46);
    tft.fillRoundRect(6,y,308,AT_ROW_H-4,9,bg);
    tft.drawRoundRect(6,y,308,AT_ROW_H-4,9,on?(is220?tft.color565(200,70,60):cGreen):tft.color565(46,46,100));
    tft.fillRoundRect(6,y,6,AT_ROW_H-4,3,vc);   // faixa colorida = tensao do canal
    tft.setTextColor(TFT_WHITE,bg);tft.setTextSize(2);tft.setCursor(20,y+4);tft.print(RELAY_NAMES[ch]);
    tft.setTextColor(vc,bg);tft.setTextSize(1);tft.setCursor(20,y+24);tft.print(RELAY_VOLTAGE[ch]);
    if(ch==COOLER_CH){   // Fase 36: automatico por histerese -- indicador travado
      uint16_t tb = on ? tft.color565(14,42,14) : tft.color565(42,42,42);
      tft.fillRoundRect(236,y+5,64,28,14,tb); tft.drawRoundRect(236,y+5,64,28,14,tft.color565(20,20,20));
      tft.setTextColor(on?cGreen:cSub,tb);tft.setTextSize(1);tft.setCursor(247,y+15);tft.print("AUTO");
    }else{
      drawToggle(236, y+5, on, on && is220 ? cRed : cGreen);
    }
  }
  if(!autoAtivo && fbCicloAtivo){   // aviso: ciclo em andamento e nada controlando o ambiente
    tft.setTextSize(1);tft.setTextColor(tft.color565(255,190,90),cBg);tft.setCursor(8,AT_ROW_Y0+AT_VIS*AT_ROW_H+2);
    { char w[64]; if(autoRetomaMin>0) snprintf(w,sizeof(w),"Automacao desligada: volta sozinha em ate %u min",(unsigned)autoRetomaMin); else snprintf(w,sizeof(w),"Automacao desligada: ambiente NAO controlado"); tft.print(w); }
  }
}

// ═══════════════════════════════════════════════════════════════
// TELA PRINCIPAL
// ═══════════════════════════════════════════════════════════════
void updateClockHeader(){
  if(!rtcOK) return;DateTime n=rtc.now();snprintf(clockStr,9,"%02d:%02d:%02d",n.hour(),n.minute(),n.second());
  tft.setTextColor(cClk,cCard);tft.setTextSize(2);tft.setCursor(172,7);tft.print(clockStr);
}
void drawFullHeader(){
  tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(5,7);tft.print("ChocaIoT");
  drawWifiIcon(WIFI_ICX,WIFI_ICY,wifiOK,true);
  tft.setTextColor(cClk,cCard);tft.setTextSize(2);tft.setCursor(172,7);tft.print(clockStr);
  drawMenuIcon(MENU_BTN_X,3,40,22,tft.color565(28,28,72),tft.color565(130,130,210));
}

// Mesma conta que calcularDiaAtual() no app.js do PWA: dia atual (1..duracao),
// congelado na hora da pausa se estiver pausado. -1 = sem ciclo ativo.
int calcularDiaCicloAtual(){
  if(!fbCicloAtivo) return -1;
  if(fbCicloPreparo) return 1;   // Fase 77: ainda sem ovos -- a contagem so comeca na confirmacao
  time_t referencia = fbCicloPausado ? fbCicloPausadoEm : time(nullptr);
  long dias = (long)((referencia - fbCicloIniciadoEm) / 86400L) + 1;
  if(dias<1) dias=1;
  int total = fbCicloDuracaoDias>0 ? fbCicloDuracaoDias : 21;
  if(dias>total) dias=total;
  return (int)dias;
}

// Desenha só a célula (mesma posição/estilo do card "Timer" antigo, que
// ficava estático em "--:--"). Chamada tanto no desenho completo da home
// quanto sozinha (refreshCicloCardSeVisivel) pra atualizar sem redesenhar
// a tela toda.
// Fase 39 — 3 linhas de card mais altas (aproveita os ~28px que sobravam
// embaixo) + card "Data" na celula do meio da ultima linha.
#define CARD_H  64
#define CARD_Y0 32
#define CARD_Y1 100
#define CARD_Y2 168
void drawDataCard(){   // Fase 116: card vazio -- a data agora alterna com o ciclo no card "Ciclo" (a cada 5 s)
  int x=112,y=CARD_Y2,w=100,h=CARD_H;
  homeFill(x+1,y+1,w-2,h-2);
  tft.drawRoundRect(x,y,w,h,5,tft.color565(46,46,90));
}

// Fase 77 -- borda que PREENCHE (sentido horario, a partir do canto superior esquerdo) conforme o tempo estavel / 5 min
static void bordaProgresso(int x,int y,int w,int h,float p,uint16_t cor){
  if(p<=0) return; if(p>1) p=1;
  int a=w-8, b=h-8; int n=(int)((2*a+2*b)*p); int seg;
  seg=n<a?n:a; if(seg>0) tft.fillRect(x+4,y,seg,2,cor); n-=seg; if(n<=0) return;
  seg=n<b?n:b; if(seg>0) tft.fillRect(x+w-2,y+4,2,seg,cor); n-=seg; if(n<=0) return;
  seg=n<a?n:a; if(seg>0) tft.fillRect(x+w-4-seg,y+h-2,seg,2,cor); n-=seg; if(n<=0) return;
  seg=n<b?n:b; if(seg>0) tft.fillRect(x,y+h-4-seg,2,seg,cor);
}
// Fase 112 -- motivo REAL do status com ciclo em andamento (antes: sempre "OK"): 0 OK · 1 automacao desligada · 2 sensor sem leitura · 3 temperatura fora · 4 umidade fora · 5 ambas
static int statusMotivoCiclo(){
  if(!autoAtivo) return 1;
  bool lido=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (millis()-lastSht31OkMs)<120000UL;
  if(!lido) return 2;
  bool tOk=temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX, uOk=umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
  if(!tOk && !uOk) return 5; if(!tOk) return 3; if(!uOk) return 4; return 0;
}
static const char* const STATUS_MOTIVO[6]={"OK","Auto OFF","Sensor!","Fora T","Fora U","Fora T/U"};
// "OFF" (verde) sem ciclo · "Aguarde" (ambar, borda enchendo) preparo instavel · "Pronto" (verde, borda cheia) preparo estavel · "OK" ciclo em andamento
// Fase 115 -- o card passa a ROTACIONAR os avisos (5 s cada) em vez de empilhar texto: estado principal, diagnostico curto
// (se nao repetir o principal) e "CO2 alto". Com 1 item so, fica fixo. A fonte e maior (texto longo usa a fonte 2 de 16 px).
struct StItem{ char txt[18]; uint16_t cor; char unid[6]; };   // unid: sufixo pequeno (ex.: "rpm")
static int statusItens(StItem* it,float* prog,bool* prep){
  const char* pal="OFF"; uint16_t cor=cGreen; float pg=0; bool pr=fbCicloAtivo&&fbCicloPreparo;
  uint16_t amb=tft.color565(230,180,60);
  if(fbCicloAtivo){
    if(pr){ if(ambEstado==2){ pal="Pronto"; pg=1; } else { pal="Aguarde"; cor=amb; pg=ambEstavelSeg/(AMB_ESTAVEL_MS/1000.0f); } }
    else { int mo=statusMotivoCiclo(); pal=STATUS_MOTIVO[mo]; if(mo) cor=amb; }   // Fase 112
  }else if(ambEstado==3) pal="Mantem";
  else if(ambEstado==4){ pal="Ajuste"; cor=amb; }
  if(ambEstado==5){ pal="Tampa"; cor=amb; } else if(ambEstado==6){ pal="Recup."; cor=amb; }   // Fase 82
  int n=0;
  strlcpy(it[n].txt,pal,sizeof(it[n].txt)); it[n].cor=cor; it[n].unid[0]=0; n++;
  if(ambDiagCurto[0] && strcasecmp(ambDiagCurto,pal)!=0 && !(strncasecmp(pal,"Auto",4)==0 && strncasecmp(ambDiagCurto,"auto",4)==0)){
    strlcpy(it[n].txt,ambDiagCurto,sizeof(it[n].txt)); it[n].cor=tft.color565(255,150,70); it[n].unid[0]=0; n++;   // Fase 80
  }
  if(fbCicloAtivo && !fbCicloPreparo && !isnan(co2Atual) && lastCo2OkMs && millis()-lastCo2OkMs<180000UL){
    float ok,al; co2Limites(ok,al);
    if(co2Atual>al){ strlcpy(it[n].txt,"CO2 alto",sizeof(it[n].txt)); it[n].cor=tft.color565(240,80,80); it[n].unid[0]=0; n++; }
  }
  { int r=fanRpmAtual();   // Fase 116: o rpm do fan 1 entra na rotacao (uma informacao por vez, como no PWA)
    if(r>0 && n<4){ snprintf(it[n].txt,sizeof(it[n].txt),"%d",r); it[n].cor=cGreen; strcpy(it[n].unid,"rpm"); n++; } }
  *prog=pg; *prep=pr; return n;
}
static void statusTexto(int x,int y,const char* t,uint16_t cor,const char* unid=""){
  homeTexto(cor);
  if(unid && unid[0]){   // numero em fonte grande + unidade pequena logo depois
    tft.setTextFont(1); tft.setTextSize(2); tft.setCursor(x+5,y+24); tft.print(t);
    int cx=tft.getCursorX(); tft.setTextSize(1); tft.setCursor(cx+3,y+32); tft.print(unid);
    return;
  }
  if(strlen(t)<=8){ tft.setTextFont(1); tft.setTextSize(2); tft.setCursor(x+5,y+24); tft.print(t); return; }
  tft.setTextFont(2); tft.setTextSize(1);
  if(tft.textWidth(t)<=90){ tft.setCursor(x+5,y+24); tft.print(t); tft.setTextFont(1); return; }
  tft.setTextFont(1); tft.setTextSize(1);   // muito longo: duas linhas pequenas, quebrando no ultimo espaco
  char a[18]; strlcpy(a,t,sizeof(a)); char* sp=strrchr(a,' ');
  if(sp){ *sp=0; tft.setCursor(x+6,y+26); tft.print(a); tft.setCursor(x+6,y+37); tft.print(sp+1); }
  else { tft.setCursor(x+6,y+30); tft.print(t); }
}
void drawStatusCard(){
  int x=4,y=CARD_Y1,w=100,h=CARD_H;
  homeFill(x+1,y+1,w-2,h-2);
  tft.drawRoundRect(x,y,w,h,5,tft.color565(46,46,90));
  tft.setTextSize(1);homeTexto(cSub);tft.setCursor(x+6,y+6);tft.print("Status");
  StItem it[4]; float prog=0; bool prep=false; int n=statusItens(it,&prog,&prep);
  int k=(n>1)?(int)((millis()/5000UL)%n):0;
  statusTexto(x,y,it[k].txt,it[k].cor,it[k].unid);
  if(n>1){ for(int i=0;i<n;i++){ int dx=x+w-8-(n-1-i)*7; tft.fillCircle(dx,y+9,i==k?2:1,i==k?it[k].cor:cSub); } }   // pontinhos: quantos avisos e qual esta na tela
  if(prep) bordaProgresso(x,y,w,h,prog,it[0].cor);
}

void drawCicloCard(){
  int x=216,y=CARD_Y1,w=100,h=CARD_H;
  homeFill(x+1,y+1,w-2,h-2);
  tft.drawRoundRect(x,y,w,h,5,tft.color565(46,46,90));
  bool mostraData = rtcOK && (((millis()/5000UL)%2)==1);   // Fase 116: a cada 5 s alterna "dia/total" <-> data de hoje
  tft.setTextSize(1);homeTexto(cSub);tft.setCursor(x+6,y+6);tft.print(mostraData?"Data":"Ciclo");
  if(mostraData){
    DateTime n=rtc.now(); char d[8]; snprintf(d,8,"%02d/%02d",n.day(),n.month());
    tft.setTextSize(2);homeTexto(cClk);tft.setCursor(x+5,y+24);tft.print(d);
    tft.setTextSize(1);homeTexto(cSub);tft.setCursor(x+6,y+48);tft.printf("%04d",n.year());
    return;
  }

  char val[8];
  uint16_t vc;
  if(!fbCicloAtivo){
    strcpy(val,"--"); vc=tft.color565(70,210,255);
  }else{
    int dia=calcularDiaCicloAtual();
    int total=fbCicloDuracaoDias>0?fbCicloDuracaoDias:21;
    if(fbCicloPreparo){ strcpy(val,"Preparo"); vc=tft.color565(230,180,60); }   // Fase 77: sem ovos ainda
    else{
      snprintf(val,8,"%d/%d",dia,total);
      vc = fbCicloPausado ? tft.color565(230,180,60) : tft.color565(120,220,120);
    }
  }
  tft.setTextSize(2);homeTexto(vc);tft.setCursor(x+5,y+24);tft.print(val);
  if(fbCicloAtivo && fbCicloPausado){tft.setTextSize(1);homeTexto(tft.color565(230,180,60));tft.setCursor(x+6,y+48);tft.print("Pausado");}
}

static uint16_t co2Cor(float v){   // Fase 115: verde ate o limite ideal, amarelo ate o de alerta, vermelho acima
  float ok,al; co2Limites(ok,al);
  return v<=ok ? tft.color565(120,220,120) : (v<=al ? tft.color565(240,200,60) : tft.color565(240,80,80));
}
// Fase 20 — redesenha só as celulas Temperatura/Umidade da home (mesmo
// padrao de drawCicloCard(): fillRect local, sem tft.fillScreen()). Mostra
// "--.-" (honesto, mesmo texto que ja existia como placeholder estatico)
// enquanto o SHT31 nao tiver uma leitura valida ainda.
void drawTempUmidCards(){
  auto cardVal=[&](int x,int y,int w,int h,const char* lb,const char* vl,uint16_t vc){
    homeFill(x+1,y+1,w-2,h-2);
    tft.drawRoundRect(x,y,w,h,5,tft.color565(46,46,90));
    tft.setTextSize(1);homeTexto(cSub);tft.setCursor(x+6,y+6);tft.print(lb);
    tft.setTextSize(2);homeTexto(vc);tft.setCursor(x+5,y+24);tft.print(vl);
  };
  char vt[8],vu[8];
  if(isnan(temperaturaAtual)) strcpy(vt,"--.-C"); else snprintf(vt,8,"%.1fC",temperaturaAtual);
  if(isnan(umidadeAtual))     strcpy(vu,"--.-%"); else snprintf(vu,8,"%.0f%%",umidadeAtual);
  // Fase 21 — cor por faixa em vez de azul fixo (verde=ideal dentro da
  // faixa de incubacao, amarelo/vermelho conforme distancia).
  cardVal(4,CARD_Y0,100,CARD_H,"Temperatura",vt,corPorFaixa(temperaturaAtual,TEMP_IDEAL_MIN,TEMP_IDEAL_MAX,TEMP_ALERTA_MARGEM));
  cardVal(112,CARD_Y0,100,CARD_H,"Umidade",vu,corPorFaixa(umidadeAtual,UMID_IDEAL_MIN,UMID_IDEAL_MAX,UMID_ALERTA_MARGEM));
  // Fase 114 -- CO2 (MH-Z19C) no aparelho: antes o card ficava fixo em "---" (o valor so ia para o PWA).
  bool co2Ok=(!isnan(co2Atual) && lastCo2OkMs && millis()-lastCo2OkMs<180000UL);
  char vc2[8]; uint16_t cc2=tft.color565(70,210,255);
  if(co2Ok){
    snprintf(vc2,8,"%d",(int)(co2Atual+0.5f));
    cc2 = co2Cor(co2Atual);   // Fase 115: faixas por fase do ciclo
  }else strcpy(vc2,"---");
  cardVal(216,CARD_Y0,100,CARD_H,"CO2",vc2,cc2);
  if(co2Ok){ tft.setTextSize(1); homeTexto(cSub); tft.setCursor(216+6,CARD_Y0+46); tft.print("ppm"); }
}

// Fase 21 — tela de detalhe do sensor (toque no card Temperatura/Umidade
// da home). Estatisticas sao "desde o boot" (RAM comum, zera em qualquer
// reset) -- historico completo de verdade so existe no PWA, via Firestore
// (leituras/), que nao tem esse limite. Rotulo honesto sobre isso na tela.
// Fase 22 — grafico simples de linha (sem lib), mesma ideia do sparkline
// do PWA, comprimido pra caber no touchscreen. Usa o buffer sensorHist*
// (1 amostra/20min, reseta a meia-noite -- ver resetarEstatisticasSensorSeVirouODia).
// Fase 23 — escala do eixo Y prioriza a variacao REAL dos dados (antes
// forcava a janela visivel a cobrir a faixa ideal inteira -- exatamente
// o bug que apareceu numa foto real do usuario: umidade em 42%, ideal
// 45-65%, o grafico esticava a escala pra caber os dois e a variacao
// real (41-42%) ficava achatada numa linha reta no fundo, praticamente
// invisivel). Agora a faixa ideal so e desenhada onde cai dentro da
// janela visivel -- pode nem aparecer se os dados estiverem longe do
// ideal, e esta certo: o grafico prioriza mostrar a variacao real.
// Rotulos de min/max do eixo Y (compactos, size 1) pro mesmo nivel de
// detalhe do grafico do PWA, sem lotar a tela pequena.
// Fase 25 — mais uma rodada de ajuste fino do gráfico, pedida depois de
// ver foto real em hardware: dot "ao vivo" pra DENTRO do gráfico (era
// numa tirinha acima), "Faixa ideal" pra mesma linha de "N leituras"
// (rodapé virou 1 linha só), Min/Max/Méd viraram um overlay no TOPO do
// gráfico (deixa de gastar linha própria), e eixo de horário (3 marcas:
// inicio/meio/agora, calculadas a partir do RTC e do intervalo fixo de
// amostragem -- SENSOR_HIST_INTERVAL_MS -- já que o buffer não guarda
// timestamp por amostra, só o valor). Resultado: gráfico ainda maior
// (218px de altura, era 186px).
// Fase 26 — cor do dot "ao vivo": era verde, virou azul fraco (pedido
// explicito, pra nao confundir com "ideal" -- ver corPorFaixa). Dois
// tons pro pulso (nunca apaga, so alterna, evita fantasma).
#define COR_DOT_ACESO  tft.color565(26,82,118)
#define COR_DOT_APAGADO tft.color565(10,32,46)
// Cor do eixo de hora, DIFERENTE do cinza dos rotulos do eixo Y (Fase 26
// -- pedido explicito: "nao se misturar visualmente" com o eixo de valor).
#define COR_EIXO_HORA tft.color565(120,105,175)

// Redesenha SÓ o dot -- usado no pulso de 600ms (ver refreshSensorDetailLive).
// Fase 26: extraído do redraw completo, que antes rodava inteiro a cada
// pulso e causava o flicker reportado (fillRect limpando o gráfico
// inteiro + redesenhando textos a cada 600ms).
void drawSensorLiveDotOnly(int x,int y,int w, bool dotOn){
  tft.fillCircle(x+w-10,y+8,4, dotOn?COR_DOT_ACESO:COR_DOT_APAGADO);
}

void drawSensorHistChart(int x,int y,int w,int h, float* hist,int n, float idealMin,float idealMax, uint16_t lineColor, const char* unidade, float mn,float mx,float med, bool dotOn){
  tft.fillRect(x+1,y+1,w-2,h-2,cBg);
  tft.drawRect(x,y,w,h,tft.color565(36,36,78));
  drawSensorLiveDotOnly(x,y,w,dotOn);

  // Fase 26 — Bug 2: so bloqueia o desenho com ZERO amostras (era n<2,
  // ficava "Aguardando leituras..." por minutos ate a 2a amostra
  // aparecer). Com 1 amostra so, desenha um unico ponto (sem linha).
  if(n<1){
    tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(x+w/2-60,y+h/2-4);
    tft.print("Aguardando leituras...");
    return;
  }
  float dataMin=hist[0], dataMax=hist[0];
  for(int i=1;i<n;i++){ if(hist[i]<dataMin) dataMin=hist[i]; if(hist[i]>dataMax) dataMax=hist[i]; }
  bool ppm=(strcmp(unidade,"ppm")==0);   // Fase 115: CO2 sem casas decimais
  float folga = (dataMax-dataMin)*0.15f; float fmin=ppm?30.0f:0.2f; if(folga<fmin) folga=fmin;
  float yMin=dataMin-folga, yMax=dataMax+folga;
  float span = (yMax-yMin); if(span<0.1f) span=0.1f;
  auto yFor=[&](float v)->int{ return y+h-2-(int)(((v-yMin)/span)*(h-4)); };
  float yMid = (yMax+yMin)/2.0f;

  // Fase 26 — Bug 6: linhas de grade de verdade (3, no max/meio/min),
  // com os rotulos numericos centralizados verticalmente NA linha (antes
  // o rotulo so "flutuava" perto do canto, sem nenhuma linha de
  // referencia pra alinhar com o olho).
  tft.setTextSize(1);
  auto gridLine=[&](float v,const char* lbl){
    int ly=yFor(v);
    tft.drawFastHLine(x+30,ly,w-32,tft.color565(30,34,46));
    int textY=ly-4;  // centraliza o texto (8px) NA linha, em vez de flutuar perto dela
    if(textY < y+13) textY = y+13;      // nao deixa vazar por cima (colide com overlay Min/Max/Med)
    if(textY > y+h-11) textY = y+h-11;  // nem por baixo (colide com o eixo de hora)
    tft.setTextColor(cSub,cBg);tft.setCursor(x+3,textY);tft.print(lbl);
  };
  char lbMax[8],lbMid[8],lbMin[8];
  const char* ff=ppm?"%.0f":"%.1f"; snprintf(lbMax,8,ff,yMax); snprintf(lbMid,8,ff,yMid); snprintf(lbMin,8,ff,yMin);
  gridLine(yMax,lbMax); gridLine(yMid,lbMid); gridLine(yMin,lbMin);

  // Banda da faixa ideal -- só a parte que cai dentro da janela visivel
  if(idealMax>=yMin && idealMin<=yMax){
    int yTop=yFor(min(idealMax,yMax)), yBot=yFor(max(idealMin,yMin));
    tft.fillRect(x+31,yTop,w-33,yBot-yTop,tft.color565(14,42,20));
  }

  if(n<2){
    // Só 1 amostra: um ponto, sem linha (o loop abaixo, i<n, nao roda
    // mesmo -- mas o calculo de "xl" com (n-1)/(n-1) daria 0/0 se nao
    // tratado à parte).
    int xl=x+1, yl=yFor(hist[0]);
    tft.fillCircle(xl,yl,3,lineColor);
  }else{
    // Linha
    for(int i=1;i<n;i++){
      int x0=x+1+(int)((float)(i-1)/(n-1)*(w-2));
      int x1=x+1+(int)((float)i/(n-1)*(w-2));
      tft.drawLine(x0,yFor(hist[i-1]),x1,yFor(hist[i]),lineColor);
    }
    // Ponto no ultimo valor + valor anotado do lado
    int xl=x+w-1;
    int yl=yFor(hist[n-1]);
    tft.fillCircle(xl,yl,3,lineColor);
    char vs[12]; snprintf(vs,12,ppm?"%.0f%s":"%.1f%s",hist[n-1],unidade);
    int tw=(int)strlen(vs)*6;
    tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cBg);
    if(xl+6+tw < x+w-14) tft.setCursor(xl+6,yl-4); else tft.setCursor(xl-6-tw,yl-4);
    tft.print(vs);
  }

  // Fase 26 — Bug 3: fundo do overlay Min/Max/Med agora usa
  // tft.textWidth() pra acompanhar SO o texto, em vez de uma largura
  // fixa exagerada. Centralizado no espaco livre entre o rotulo do
  // eixo Y e o dot.
  tft.setTextSize(1);
  char ov[48]; snprintf(ov,48,ppm?"Min %.0f%s  Max %.0f%s  Med %.0f%s":"Min %.1f%s  Max %.1f%s  Med %.1f%s",mn,unidade,mx,unidade,med,unidade);
  int ovW = tft.textWidth(ov);
  int ovX = x + (w-ovW)/2;
  if(ovX < x+34) ovX = x+34;                 // nao colidir com o rotulo do eixo Y
  if(ovX+ovW > x+w-20) ovX = x+w-20-ovW;      // nao colidir com o dot
  tft.fillRect(ovX-3,y+1,ovW+6,11,tft.color565(8,8,20));
  tft.setTextColor(TFT_WHITE,tft.color565(8,8,20));tft.setCursor(ovX,y+3);tft.print(ov);

  // Eixo de horario (Fase 25/26) -- 3 marcas (inicio/meio/agora),
  // calculadas a partir do RTC + do intervalo de amostragem (o buffer só
  // guarda valor, nao timestamp por ponto). ⚠️ Fase 26 adicionou
  // amostragem adaptativa (1min nos primeiros ~10min, 5min depois) --
  // esse eixo assume intervalo FIXO entre amostras, entao durante essa
  // janela inicial os horarios mostrados podem ficar um pouco
  // imprecisos (a distorção some depois que os primeiros 10 pontos
  // "rolam" pra fora ou a amostragem normaliza). Cor diferente do eixo Y
  // (pedido explicito) e mais respiro da borda inferior.
  if(rtcOK && n>=1){
    DateTime agora=rtc.now();
    auto horaDoIndice=[&](int idx)->String{
      if(chartEixo7d) return idx==0?String(histRotIni):(idx==n-1?String(histRotFim):String(histRotMid));   // Fase 47/53: rotulos do periodo escolhido
      long minAtras=(long)(n-1-idx)*(SENSOR_HIST_INTERVAL_MS/60000UL);
      DateTime t=agora - TimeSpan(0,0,(int16_t)minAtras,0);
      char b[6]; snprintf(b,6,"%02d:%02d",t.hour(),t.minute()); return String(b);
    };
    tft.setTextColor(COR_EIXO_HORA,cBg);
    tft.setCursor(x+5,y+h-24);tft.print(horaDoIndice(0));
    if(n>2){ String meio=horaDoIndice((n-1)/2); tft.setCursor(x+w/2-12,y+h-24);tft.print(meio); }
    String agr=horaDoIndice(n-1); tft.setCursor(x+w-5-(int)agr.length()*6,y+h-24);tft.print(agr);
  }
}

// Rodape da tela do sensor (Fase 47): texto curto + botao Hoje/7 dias (toque em x>=262, y>=222).
void drawSensorFooter(bool isTemp,float idealMin,float idealMax,const char* unidade,uint32_t n){
  tft.fillRect(0,222,320,18,cBg);
  tft.setTextSize(1);tft.setTextColor(cClk,cBg);tft.setCursor(6,228);
  if(sensorChart7d) tft.printf("%s - ideal %.1f a %.1f%s - 7 dias (%u pts)",isTemp?"Temp.":"Umid.",idealMin,idealMax,unidade,(unsigned)h7Pts);
  else              tft.printf("%s - ideal %.1f a %.1f%s - %u leit. %s",isTemp?"Temp.":"Umid.",idealMin,idealMax,unidade,(unsigned)n,rtcOK?"hoje":"(sem RTC)");
  tft.fillRoundRect(264,224,52,14,4,cKey);
  tft.setTextColor(TFT_WHITE,cKey);
  tft.setCursor(272,228);tft.print("Hist. >");
}

static void drawCo2Footer(){
  float okMax,alto; co2Limites(okMax,alto);
  tft.fillRect(0,222,320,18,cBg);
  tft.setTextSize(1);tft.setTextColor(cClk,cBg);tft.setCursor(6,228);
  if(sensorChart7d) tft.printf("CO2 - ideal ate %.0f ppm - 7 dias (%u pts)",okMax,(unsigned)h7CPts);
  else              tft.printf("CO2 - ideal ate %.0f ppm - %u leit. %s",okMax,(unsigned)co2N,rtcOK?"hoje":"(sem RTC)");
  tft.fillRoundRect(264,224,52,14,4,cKey);
  tft.setTextColor(TFT_WHITE,cKey);
  tft.setCursor(272,228);tft.print(sensorChart7d?"Hoje >":"7 dias");
}
static void drawCo2DetailScreen(){
  float okMax,alto; co2Limites(okMax,alto);
  float* hist=co2Historico; int n=co2HistCount; float mn=co2Min,mx=co2Max,med=co2N?co2Soma/co2N:NAN;
  if(sensorChart7d){ hist=h7C; n=h7CN; mn=h7CMin; mx=h7CMax; med=h7CPts?h7CSoma/h7CPts:NAN; }
  chartEixo7d=sensorChart7d;
  tft.fillScreen(cBg);
  uint16_t lc=(n>0)?co2Cor(hist[n-1]):cClk;
  drawSensorHistChart(2,2,316,218,hist,n,0,okMax,lc,"ppm",mn,mx,med,true);
  drawCo2Footer();
}
void drawSensorDetailScreen(){
  if(sensorDetalheCo2){ drawCo2DetailScreen(); return; }   // Fase 115
  bool isTemp = sensorDetalheEhTemp;
  float mn  = isTemp ? temperaturaMin   : umidadeMin;
  float mx  = isTemp ? temperaturaMax   : umidadeMax;
  float soma= isTemp ? temperaturaSoma  : umidadeSoma;
  uint32_t n= isTemp ? temperaturaN     : umidadeN;
  float idealMin = isTemp ? TEMP_IDEAL_MIN : UMID_IDEAL_MIN;
  float idealMax = isTemp ? TEMP_IDEAL_MAX : UMID_IDEAL_MAX;
  const char* unidade = isTemp ? "C" : "%";
  int histN = sensorHistCount;  // mesmo contador serve pra temp e umidade (empurrados juntos em lerSht31())
  float* hist = isTemp ? tempHistorico : umidHistorico;
  float med = n>0?(soma/n):NAN;
  if(sensorChart7d){   // Fase 47 -- ultimos 7 dias (arquivos /hist do SD)
    histN = h7N; hist = isTemp ? h7T : h7H;
    mn  = isTemp ? h7TMin : h7HMin; mx = isTemp ? h7TMax : h7HMax;
    med = h7Pts>0 ? (isTemp ? h7TSoma : h7HSoma)/h7Pts : NAN;
  }
  chartEixo7d = sensorChart7d;

  tft.fillScreen(cBg);

  uint16_t lineColor = (histN>0) ? corPorFaixa(hist[histN-1],idealMin,idealMax, isTemp?TEMP_ALERTA_MARGEM:UMID_ALERTA_MARGEM) : cClk;

  // Fase 25 — grafico ainda maior (186px -> 218px): rodape agora e uma
  // unica linha (faixa ideal + contagem juntos), Min/Max/Med viraram
  // overlay dentro do proprio grafico (ver drawSensorHistChart).
  drawSensorHistChart(2,2,316,218, hist, histN, idealMin,idealMax, lineColor, unidade, mn,mx, med, true);
  drawSensorFooter(isTemp,idealMin,idealMax,unidade,n);
}

// Fase 25 — atualizacao periodica "ao vivo": o dot pulsa (nunca some,
// só alterna 2 tons de verde -- evita fantasma sobre a linha/banda,
// problema que existia quando o dot ficava FORA do grafico e podia
// simplesmente virar cBg) a cada 600ms, redesenhado sozinho (fillCircle
// bem barato). O grafico inteiro (linha, overlay, eixo de hora) agora
// redesenha a cada SENSOR_HIST_INTERVAL_MS OU a cada leitura nova do
// SHT31 quando o overlay Min/Max/Med muda -- na pratica, a cada leitura
// de 5s, porque Min/Max/Med sao recalculados a cada leitura (ver
// lerSht31()), entao "atraso de refresh" pedido pelo usuario cai de
// "só quando entra amostra nova no buffer" pra "a cada 5s", sem
// precisar redesenhar em loop apertado (fillScreen() nunca é chamado
// de novo aqui, só a área do gráfico).
// Fase 26 — Bug 1 (flicker): antes, QUALQUER motivo pra rodar esta
// funcao (inclusive só o pulso do dot, a cada 600ms) disparava o
// drawSensorHistChart() INTEIRO -- fillRect limpando o gráfico inteiro
// e redesenhando linha/grade/overlay/eixo toda vez, causando o flicker
// reportado nos textos. Agora os dois casos são tratados separados: o
// pulso do dot (600ms) SÓ redesenha o dot (drawSensorLiveDotOnly, um
// fillCircle bem barato); o redraw completo (5s, quando os dados de
// verdade mudam) continua fazendo o trabalho pesado, mas só a cada 5s.
void refreshSensorDetailLive(){
  if(sensorChart7d) return;   // Fase 47 -- grafico de 7 dias e estatico
  static unsigned long lastBlinkMs=0;
  static bool dotOn=true;
  static unsigned long lastFullRedrawMs=0;

  bool precisaBlink = millis()-lastBlinkMs>600;
  bool precisaRedrawCompleto = millis()-lastFullRedrawMs>5000UL;  // mesmo valor de SHT31_READ_INTERVAL_MS (definida so mais abaixo no arquivo)
  if(!precisaBlink && !precisaRedrawCompleto) return;

  if(precisaBlink) dotOn=!dotOn;

  if(precisaRedrawCompleto){
    lastFullRedrawMs=millis();
    if(precisaBlink) lastBlinkMs=millis();

    if(sensorDetalheCo2){   // Fase 115
      float ok,al; co2Limites(ok,al);
      uint16_t lc=(co2HistCount>0)?co2Cor(co2Historico[co2HistCount-1]):cClk;
      drawSensorHistChart(2,2,316,218,co2Historico,co2HistCount,0,ok,lc,"ppm",co2Min,co2Max,co2N?co2Soma/co2N:NAN,dotOn);
      chartEixo7d=false; drawCo2Footer();
      return;
    }
    bool isTemp = sensorDetalheEhTemp;
    float idealMin = isTemp ? TEMP_IDEAL_MIN : UMID_IDEAL_MIN;
    float idealMax = isTemp ? TEMP_IDEAL_MAX : UMID_IDEAL_MAX;
    const char* unidade = isTemp ? "C" : "%";
    float* hist = isTemp ? tempHistorico : umidHistorico;
    float mn  = isTemp ? temperaturaMin   : umidadeMin;
    float mx  = isTemp ? temperaturaMax   : umidadeMax;
    float soma= isTemp ? temperaturaSoma  : umidadeSoma;
    uint32_t n= isTemp ? temperaturaN     : umidadeN;

    uint16_t lineColor = (sensorHistCount>0) ? corPorFaixa(hist[sensorHistCount-1],idealMin,idealMax, isTemp?TEMP_ALERTA_MARGEM:UMID_ALERTA_MARGEM) : cClk;
    drawSensorHistChart(2,2,316,218, hist, sensorHistCount, idealMin,idealMax, lineColor, unidade, mn,mx, n>0?(soma/n):NAN, dotOn);

    chartEixo7d=false;
    drawSensorFooter(isTemp,idealMin,idealMax,unidade,n);
  }else if(precisaBlink){
    lastBlinkMs=millis();
    drawSensorLiveDotOnly(2,2,316,dotOn);  // SÓ o dot -- nada mais na tela é tocado
  }
}

// Livro aberto (48x32): duas paginas inclinadas, lombada central e linhas de texto.
void drawLivroAberto(int x,int y,uint16_t c){
  uint16_t escuro=tft.color565(8,20,10), luz=tft.color565(190,240,200);
  // capa (borda) e paginas
  tft.fillTriangle(x,y+6,x+24,y+2,x+24,y+30,c); tft.fillTriangle(x,y+6,x+24,y+30,x,y+28,c);
  tft.fillTriangle(x+48,y+6,x+24,y+2,x+24,y+30,c); tft.fillTriangle(x+48,y+6,x+24,y+30,x+48,y+28,c);
  tft.fillTriangle(x+2,y+8,x+22,y+5,x+22,y+27,luz); tft.fillTriangle(x+2,y+8,x+22,y+27,x+2,y+26,luz);
  tft.fillTriangle(x+46,y+8,x+26,y+5,x+26,y+27,luz); tft.fillTriangle(x+46,y+8,x+26,y+27,x+46,y+26,luz);
  tft.drawFastVLine(x+24,y+2,29,escuro);   // lombada
  for(int i=0;i<4;i++){ tft.drawLine(x+5,y+11+i*4,x+20,y+9+i*4,escuro); tft.drawLine(x+28,y+9+i*4,x+43,y+11+i*4,escuro); }
}
void drawMainScreen(){
  homeWallAtiva=wallGarantir(1);homeFill(0,0,320,240);drawFullHeader();tft.drawFastHLine(0,28,320,tft.color565(36,36,78));
  auto card=[&](int x,int y,int w,int h,const char* lb,const char* vl,uint16_t vc){
    tft.drawRoundRect(x,y,w,h,5,tft.color565(46,46,90));tft.setTextSize(1);homeTexto(cSub);tft.setCursor(x+6,y+6);tft.print(lb);
    tft.setTextSize(strlen(vl)>7?1:2);homeTexto(vc);tft.setCursor(x+5,y+(strlen(vl)>7?26:24));tft.print(vl);
  };
  uint16_t cC=tft.color565(70,210,255);
  drawTempUmidCards();  // Fase 20 -- SHT31, ver funcao (redesenha so essas 2 celulas)
  drawStatusCard();card(112,CARD_Y1,100,CARD_H,"Viragem","---",cC);
  drawCicloCard();  // card "Ciclo" (era "Timer" estatico) -- ver secao CICLO DE INCUBACAO

  // Atuadores saiu da home (foi pra dentro de Configuracoes — Fase 10, ver
  // CLAUDE.md); a celula fica livre, so a Camera ocupa a linha de baixo.
  card(216,CARD_Y2,100,CARD_H,"Camera",cameraOnline()?"Online":"Em breve",cameraOnline()?cGreen:cSub);
  card(  4,CARD_Y2,100,CARD_H,"Guia","",cGreen);   // Fase 37 -- atalho do Guia
  drawGuiaThumb(34,190);                           // Fase 67 -- miniatura do dia 21 no lugar do livro aberto (era Fase 55)
  drawDataCard();                                        // Fase 39 -- ocupa a celula que era so uma data solta

}

void refreshCicloCardSeVisivel(){
  if(appState==S_MAIN) drawCicloCard();
}

// ═══════════════════════════════════════════════════════════════
// TELA CICLO — controle local (Fase 11). Aberta tocando no card "Ciclo"
// da home; espelha o modal "Configurações do Ciclo" do PWA (progresso,
// duração, pausar/retomar, parar com confirmação).
// ═══════════════════════════════════════════════════════════════
void drawCicloScreen();   // Fase 42 -- reescrita (definida no bloco CICLO v2, mais abaixo)

// ═══════════════════════════════════════════════════════════════
// CÂMERA — tela cheia (placeholder). Mesma linguagem visual do
// painel da câmera no PWA (borda tracejada + icone + "em desenvolvimento"):
// o modulo de camera e outra placa (ESP32-CAM, projeto separado — ver
// platformio.ini), esta tela so reserva o espaco/fluxo de navegacao.
// ═══════════════════════════════════════════════════════════════
bool cameraOnline(){ return camOnlineLan; }   // atualizado pela task de rede local (ver bloco CAMERA)
#define CAM_LIVE_ROW_Y 190
void drawCameraLiveOverlayRodape(){
  bool on=cameraOnline();
  tft.fillRect(0,CAM_LIVE_ROW_Y,320,240-CAM_LIVE_ROW_Y,cBg);
  uint16_t cb=tft.color565(16,16,46);
  tft.fillRoundRect(4,CAM_LIVE_ROW_Y,150,36,8,cb); tft.drawRoundRect(4,CAM_LIVE_ROW_Y,150,36,8,tft.color565(46,46,100));
  tft.setTextSize(1); tft.setTextColor(TFT_WHITE,cb); tft.setCursor(12,CAM_LIVE_ROW_Y+8); tft.print("Ao vivo");
  drawToggle(112,CAM_LIVE_ROW_Y+8,camLiveAtivo,cGreen);
  uint16_t bg=on?tft.color565(10,50,20):tft.color565(14,14,26), br=on?cGreen:tft.color565(36,36,60), fg=on?cGreen:tft.color565(70,70,70);
  arqBotao(160,CAM_LIVE_ROW_Y,156,36,"Tirar foto",bg,fg,br);
}
void drawCameraScreen(){
  tft.fillScreen(cBg);
  uiTopo("CAMERA");
  bool on=cameraOnline();
  uint16_t cb=tft.color565(16,16,46);
  if(camLiveAtivo){
    // Fase 66 -- "ao vivo" local: a area do card vira o quadro mais recente (decodificado por camLiveTick());
    // ate o 1o quadro chegar, so um aviso. Nunca ha custo de nuvem/SD aqui (ver cameraServico(), origem "live").
    tft.fillRect(0,20,320,CAM_LIVE_ROW_Y-20,TFT_BLACK);
    if(!camFotoBuf || camFotoLen==0){
      tft.setTextSize(1); tft.setTextColor(cSub,TFT_BLACK); const char* m="Aguardando o 1o quadro...";
      tft.setCursor(160-tft.textWidth(m)/2,100); tft.print(m);
    }
    uint16_t dotc=on?cGreen:cRed; tft.fillCircle(300,28,4,dotc);
    drawCameraLiveOverlayRodape();
    return;
  }
  uiCard(6,22,308,74);
  int cx=60,cy=59;   // icone de camera, compacto no canto do card
  tft.drawRoundRect(cx-22,cy-14,44,28,5,cSub);
  tft.fillTriangle(cx-10,cy-14,cx-2,cy-14,cx-6,cy-20,cSub);
  tft.drawCircle(cx,cy+1,9,cSub);tft.drawCircle(cx,cy+1,6,cSub);
  tft.setTextSize(1);tft.setTextColor(on?cGreen:cSub,cb);
  tft.setCursor(96,34);tft.print(on?"Modulo conectado":"Modulo nao conectado");
  { char fs[40]; snprintf(fs,sizeof(fs),"Fase do ciclo: %s",faseCicloNome(faseCicloAtual())); tft.setTextColor(cSub,cb); tft.setCursor(96,48); tft.print(fs); }
  if(camVisUltimoScore>=0){ char v[40]; snprintf(v,sizeof(v),"Movimento: %.1f  Brilho: %.0f",camVisUltimoScore,camVisBrilho); tft.setTextColor(TFT_WHITE,cb); tft.setCursor(96,62); tft.print(v); }
  else { tft.setTextColor(cSub,cb); tft.setCursor(96,62); tft.print("Sem analise de imagem ainda"); }
  tft.setTextColor(tft.color565(120,120,160),cb); tft.setCursor(16,82); tft.print("Contagem de ovos: indisponivel (precisa calibrar");
  tft.setCursor(16,92); tft.print("com fotos reais assim que a camera funcionar)");

  uiCard(6,102,308,36); tft.setTextSize(1); tft.setTextColor(TFT_WHITE,cb); tft.setCursor(16,116); tft.print("Captura automatica (timelapse + visao)");
  drawToggle(246,106,camAutoAtivo,cGreen);

  drawCameraLiveOverlayRodape();
  tft.setTextSize(1);   // arqBotao (dentro do overlay acima) deixa a fonte em 2
  tft.setTextColor(tft.color565(110,110,150),cBg);
  tft.setCursor(10,232-16);tft.print("Fotos ficam no SD (/cam/manual, /cam/timelapse).");
}

// ═══════════════════════════════════════════════════════════════
// GUIA DE INCUBACAO (Fase 37) — 2 telas cheias, SEM rodape:
//  - S_GUIA: lista compacta (7 linhas visiveis, scroll por setas na
//    lateral direita — gesto de arrasto nao e confiavel neste touch)
//  - S_GUIA_TEXTO: texto do topico com quebra de linha em runtime,
//    paginado; setas no cabecalho + toque nas metades esquerda/direita
//    do texto. Passar da ultima pagina vai pro topico seguinte.
// Conteudo em src/guia_data.h (ASCII, gerado — ver tools/gerar_guia.js).
// ═══════════════════════════════════════════════════════════════
#define GUIA_ROW_Y0     26
#define GUIA_ROW_H      30
#define GUIA_VIS        7
#define GUIA_COLS       51   // caracteres por linha (fonte 6px, margem 6px)
#define GUIA_COLS_BUL   49   // itens "- " tem recuo
#define GUIA_MAX_LINHAS 90
#define GUIA_LINHAS_PAG 19
int faseIdx=0;   // Fase 38 -- imagem atual da galeria
struct GuiaLinha{ uint16_t off; uint8_t len; uint8_t kind; };  // kind: 0 texto,1 subtitulo,2 item,3 continuacao de item,4 vazia
GuiaLinha guiaLinhas[GUIA_MAX_LINHAS];
int guiaNLinhas=0, guiaScroll=0, guiaTopico=0, guiaPagina=0;

void drawGuiaTexto();
void drawFaseScreen();
static void guiaAddLinha(int off,int len,int kind){
  if(guiaNLinhas>=GUIA_MAX_LINHAS) return;
  guiaLinhas[guiaNLinhas].off=(uint16_t)off; guiaLinhas[guiaNLinhas].len=(uint8_t)len; guiaLinhas[guiaNLinhas].kind=(uint8_t)kind;
  guiaNLinhas++;
}

void guiaMontarLinhas(const char* body){
  guiaNLinhas=0;
  const char* p=body;
  while(*p){
    const char* e=strchr(p,'\n');
    int blen=e?(int)(e-p):(int)strlen(p);
    int kind=0; const char* t=p; int tl=blen;
    if(blen>=2&&p[0]=='#'&&p[1]==' '){ kind=1; t=p+2; tl=blen-2; if(guiaNLinhas>0) guiaAddLinha(0,0,4); }
    else if(blen>=2&&p[0]=='-'&&p[1]==' '){ kind=2; t=p+2; tl=blen-2; }
    int maxc=(kind==2)?GUIA_COLS_BUL:GUIA_COLS;
    int pos=0; bool primeira=true;
    while(pos<tl){
      int rem=tl-pos, take=rem<=maxc?rem:maxc;
      if(rem>maxc){ int b=take; while(b>0&&t[pos+b]!=' ') b--; if(b>0) take=b; }
      guiaAddLinha((int)(t+pos-body),take,kind==2?(primeira?2:3):kind);
      pos+=take; while(pos<tl&&t[pos]==' ') pos++;
      primeira=false;
    }
    p=e?e+1:p+blen;
  }
}

int guiaTotalPaginas(){ int n=(guiaNLinhas+GUIA_LINHAS_PAG-1)/GUIA_LINHAS_PAG; return n<1?1:n; }

void drawGuiaLista(){
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  tft.fillScreen(cBg);
  tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,8);tft.print("< Voltar");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(60,4);tft.print("Guia de Incubacao");

  for(int i=0;i<GUIA_VIS;i++){
    int idx=guiaScroll+i; if(idx>=GUIA_N) break;
    int y=GUIA_ROW_Y0+i*GUIA_ROW_H;
    tft.fillRoundRect(4,y,278,GUIA_ROW_H-2,5,rowBg);tft.drawRoundRect(4,y,278,GUIA_ROW_H-2,5,rowBrd);
    tft.fillCircle(20,y+14,10,cLogo);
    char nb[4]; snprintf(nb,4,"%d",idx+1);
    tft.setTextColor(tft.color565(20,20,40),cLogo);tft.setTextSize(1);
    tft.setCursor(20-(int)strlen(nb)*3,y+11);tft.print(nb);
    tft.setTextColor(TFT_WHITE,rowBg);tft.setCursor(38,y+5);tft.print(GUIA_TOPICOS[idx].titulo);
    tft.setTextColor(cSub,rowBg);tft.setCursor(38,y+17);tft.print(GUIA_TOPICOS[idx].resumo);
  }

  // Setas de rolagem na lateral direita (sem rodape)
  bool podeCima=guiaScroll>0, podeBaixo=guiaScroll<GUIA_N-GUIA_VIS;
  uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.drawRoundRect(288,GUIA_ROW_Y0,30,100,5,rowBrd);
  tft.fillTriangle(303,GUIA_ROW_Y0+34,292,GUIA_ROW_Y0+66,314,GUIA_ROW_Y0+66,podeCima?on:off);
  tft.drawRoundRect(288,GUIA_ROW_Y0+108,30,100,5,rowBrd);
  tft.fillTriangle(303,GUIA_ROW_Y0+108+66,292,GUIA_ROW_Y0+108+34,314,GUIA_ROW_Y0+108+34,podeBaixo?on:off);
}

void handleGuiaListaTap(int tx,int ty){
  if(ty<24){ if(tx<62){ appState=S_MAIN; drawMainScreen(); } return; }
  if(tx>=286){
    int maxScroll=GUIA_N-GUIA_VIS; if(maxScroll<0) maxScroll=0;
    if(ty<GUIA_ROW_Y0+104){ if(guiaScroll>0){ guiaScroll--; drawGuiaLista(); } }
    else { if(guiaScroll<maxScroll){ guiaScroll++; drawGuiaLista(); } }
    return;
  }
  int row=(ty-GUIA_ROW_Y0)/GUIA_ROW_H;
  int idx=guiaScroll+row;
  if(row>=0&&row<GUIA_VIS&&idx<GUIA_N){
    guiaTopico=idx; guiaPagina=0;
    if(GUIA_TOPICOS[idx].galeria){ faseIdx=0; appState=S_FASES; drawFaseScreen(); }   // Fase 38
    else { appState=S_GUIA_TEXTO; drawGuiaTexto(); }
  }
}

void drawGuiaTexto(){
  const GuiaTopico& t=GUIA_TOPICOS[guiaTopico];
  guiaMontarLinhas(t.corpo);
  int np=guiaTotalPaginas();
  if(guiaPagina>=np) guiaPagina=np-1;
  if(guiaPagina<0) guiaPagina=0;

  tft.fillScreen(cBg);
  tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char tb[40]; snprintf(tb,40,"%d. %.28s",guiaTopico+1,t.titulo);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print(tb);
  char pb[8]; snprintf(pb,8,"%d/%d",guiaPagina+1,np);
  tft.setTextColor(cSub,cCard);tft.setCursor(216,8);tft.print(pb);

  bool temPrev=(guiaPagina>0)||(guiaTopico>0), temNext=(guiaPagina<np-1)||(guiaTopico<GUIA_N-1&&!GUIA_TOPICOS[guiaTopico+1].galeria);
  uint16_t btnBg=tft.color565(30,30,70), on=cLogo, off=tft.color565(70,62,28);
  tft.fillRoundRect(244,2,34,20,4,btnBg);
  tft.fillTriangle(255,12,264,6,264,18,temPrev?on:off);
  tft.fillRoundRect(282,2,34,20,4,btnBg);
  tft.fillTriangle(307,12,298,6,298,18,temNext?on:off);

  uint16_t cTxt=tft.color565(225,225,240);
  int ini=guiaPagina*GUIA_LINHAS_PAG, fim=min(guiaNLinhas,ini+GUIA_LINHAS_PAG);
  char buf[64];
  for(int l=ini;l<fim;l++){
    const GuiaLinha& gl=guiaLinhas[l];
    int y=28+(l-ini)*11;
    if(gl.kind==4||gl.len==0) continue;
    int n=gl.len<63?gl.len:63; memcpy(buf,t.corpo+gl.off,n); buf[n]=0;
    tft.setTextSize(1);
    if(gl.kind==1){ tft.setTextColor(cLogo,cBg); tft.setCursor(6,y); }
    else if(gl.kind==2||gl.kind==3){
      if(gl.kind==2) tft.fillCircle(9,y+3,2,cClk);
      tft.setTextColor(cTxt,cBg); tft.setCursor(16,y);
    } else { tft.setTextColor(cTxt,cBg); tft.setCursor(6,y); }
    tft.print(buf);
  }
}

// dir<0: pagina/topico anterior; dir>0: proxima/seguinte. Sem rodape:
// as setas ficam no cabecalho e o texto todo e zona de toque.
void guiaNavegar(int dir){
  int np=guiaTotalPaginas();
  if(dir<0){
    if(guiaPagina>0) guiaPagina--;
    else if(guiaTopico>0){ guiaTopico--; guiaPagina=999; }   // drawGuiaTexto() limita na ultima pagina
    else return;
  }else{
    if(guiaPagina<np-1) guiaPagina++;
    else if(guiaTopico<GUIA_N-1&&!GUIA_TOPICOS[guiaTopico+1].galeria){ guiaTopico++; guiaPagina=0; }
    else return;
  }
  drawGuiaTexto();
}

void handleGuiaTextoTap(int tx,int ty){
  if(ty<24){
    if(tx<40){
      if(guiaTopico<guiaScroll) guiaScroll=guiaTopico;
      if(guiaTopico>=guiaScroll+GUIA_VIS) guiaScroll=guiaTopico-GUIA_VIS+1;
      appState=S_GUIA; drawGuiaLista();
    }
    else if(tx>=240&&tx<280) guiaNavegar(-1);
    else if(tx>=280) guiaNavegar(+1);
    return;
  }
  guiaNavegar(tx<160?-1:+1);
}

// ═══════════════════════════════════════════════════════════════
// GALERIA "FASES DO PINTINHO" (Fase 38) — imagens JPEG (baseline, 320x240,
// ja com barras pretas quando a original e retrato) guardadas em flash
// (fases_data.h) e decodificadas direto pro TFT via TJpg_Decoder. Sem texto
// extra: as proprias imagens ja trazem o dia. Como a tela e pequena, alem
// das zonas de toque (1/3 esq = anterior, 1/3 dir = proxima, centro = voltar
// ao Guia) ha setas discretas nas laterais. Sem fade/slide: sem framebuffer,
// o TFT so redesenha a imagem nova por cima (varredura rapida do decoder).
// ═══════════════════════════════════════════════════════════════
static bool faseJpgOut(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bmp){
  if(y>=240) return false;
  tft.pushImage(x,y,w,h,bmp);
  return true;
}
// Fase 67 -- miniatura do card "Guia" na home: a ULTIMA imagem da galeria (dia 21, o pintinho ja
// nascido -- ver tools/gerar_fases.py, ordenado por dia) no lugar do icone de livro aberto. Decodificada
// em escala 8 (640x480->80x60 nas fotos comuns; aqui 320x240->40x30, ja que fases_data.h ja veio em
// 320x240 com barras pretas) direto pro retangulo do card -- mesmo padrao de drawJpg ja usado no
// screensaver/wallpaper.
void drawGuiaThumb(int x,int y){
  tft.drawRoundRect(x-2,y-2,44,34,4,cGreen);
  TJpgDec.setJpgScale(8); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(faseJpgOut);
  TJpgDec.drawJpg(x,y,FASES[FASES_N-1].dados,FASES[FASES_N-1].tam);
  TJpgDec.setJpgScale(1);
}

void drawFaseSeta(int x,bool paraDireita){
  uint16_t bg=tft.color565(10,10,20), brd=tft.color565(110,110,130), fg=tft.color565(210,210,225);
  tft.fillRoundRect(x,103,18,34,5,bg);tft.drawRoundRect(x,103,18,34,5,brd);
  if(paraDireita) tft.fillTriangle(x+6,112,x+6,128,x+13,120,fg);
  else            tft.fillTriangle(x+12,112,x+12,128,x+5,120,fg);
}

void drawFaseScreen(){
  if(faseIdx<0) faseIdx=0;
  if(faseIdx>=FASES_N) faseIdx=FASES_N-1;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(faseJpgOut);
  TJpgDec.drawJpg(0,0,FASES[faseIdx].dados,FASES[faseIdx].tam);
  if(faseIdx>0)           drawFaseSeta(3,false);
  if(faseIdx<FASES_N-1)   drawFaseSeta(299,true);
}

void handleFaseTap(int tx,int ty){
  (void)ty;
  if(tx<107){ if(faseIdx>0){ faseIdx--; drawFaseScreen(); } }
  else if(tx>=213){ if(faseIdx<FASES_N-1){ faseIdx++; drawFaseScreen(); } }
  else{
    if(guiaTopico<guiaScroll) guiaScroll=guiaTopico;
    if(guiaTopico>=guiaScroll+GUIA_VIS) guiaScroll=guiaTopico-GUIA_VIS+1;
    appState=S_GUIA; drawGuiaLista();
  }
}

// ═══════════════════════════════════════════════════════════════
// PAINEL DE CONFIGURAÇÕES
// ═══════════════════════════════════════════════════════════════
// Fase 25 — virou 2 páginas: a Fase 22 tinha apertado pra 5 linhas
// (36px) pra caber Audio/Alertas, mas aí não sobrava espaço pra
// "Ajustes Finos" sem apertar de novo. Em vez disso, a página 0 voltou
// pro layout original testado (4 linhas, botões de 40px, mais
// confortável) e os itens mais novos foram pra página 1. Setinhas
// pulsantes + bolinhas indicam que há mais de uma página (pedido:
// "transição de páginas com ícone/efeito visual").
// ── Fase 62: Configuracoes em tela cheia (grade 3x4 de icones com uma palavra; sem "Voltar" nem rodape) ──
// Faixa fina no topo: tocar na esquerda volta ao inicio; tocar em "1/2 >" troca de pagina.
static const char* SET_LABEL[19]={"WiFi","Data","Atuadores","Tela","Audio","Memoria","Sistema","Logs","Alertas","Automacao","Ovos","Arquivos","Seguranca","Relatorios","Nome","Historico","Hardware","Tampa","Fans"};
static const uint8_t SET_PAG0[12]={0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t SET_PAG1[6]={12,13,15,16,17,18};
#define SET_CX0 4
#define SET_CY0 20
#define SET_CW  100
#define SET_CH  51
#define SET_GX  6
#define SET_GY  4
static int settingsItensPagina(const uint8_t** ids){ if(settingsPage==0){ *ids=SET_PAG0; return 12; } *ids=SET_PAG1; return 6; }
static void setIcone(int id,int cx,int cy,uint16_t c,uint16_t bg){
  switch(id){
    case 0:   // WiFi
      tft.fillCircle(cx,cy+9,2,c); for(int i=0;i<3;i++){ int r=6+i*6; tft.drawArc(cx,cy+9,r+3,r,135,225,c,bg,false); } break;
    case 1:   // Data (calendario)
      tft.drawRoundRect(cx-10,cy-8,20,18,3,c); tft.fillRect(cx-10,cy-8,20,5,c); tft.fillRect(cx-6,cy-11,2,5,c); tft.fillRect(cx+4,cy-11,2,5,c);
      for(int i=0;i<3;i++) for(int j=0;j<2;j++) tft.fillRect(cx-7+i*5,cy+j*5,3,3,c); break;
    case 2:   // Atuadores (chave)
      tft.drawRoundRect(cx-11,cy-6,22,12,6,c); tft.fillCircle(cx+5,cy,4,c); tft.fillRect(cx-8,cy-1,6,2,c); break;
    case 3:   // Tela
      tft.drawRoundRect(cx-11,cy-9,22,14,2,c); tft.fillRect(cx-2,cy+5,4,4,c); tft.fillRect(cx-6,cy+9,12,2,c); tft.fillRect(cx-8,cy-6,10,3,c); break;
    case 4:   // Audio (alto-falante)
      tft.fillRect(cx-10,cy-3,5,7,c); tft.fillTriangle(cx-5,cy-3,cx+2,cy-9,cx+2,cy+9,c); tft.fillTriangle(cx-5,cy+4,cx+2,cy-9,cx+2,cy+9,c);
      tft.drawArc(cx+3,cy,8,6,45,135,c,bg,false); tft.drawArc(cx+3,cy,12,10,45,135,c,bg,false); break;
    case 5:   // Memoria (chip)
      tft.drawRect(cx-7,cy-7,14,14,c); tft.fillRect(cx-3,cy-3,6,6,c);
      for(int i=0;i<3;i++){ tft.drawFastHLine(cx-11,cy-5+i*5,4,c); tft.drawFastHLine(cx+8,cy-5+i*5,4,c); tft.drawFastVLine(cx-5+i*5,cy-11,4,c); tft.drawFastVLine(cx-5+i*5,cy+8,4,c); } break;
    case 6:   // Sistema (engrenagem)
      for(int k=0;k<8;k++){ float a=k*0.7854f; tft.fillCircle(cx+(int)(cosf(a)*9),cy+(int)(sinf(a)*9),2,c); }
      tft.fillCircle(cx,cy,7,c); tft.fillCircle(cx,cy,3,bg); break;
    case 7:   // Logs
      tft.drawRoundRect(cx-8,cy-10,16,20,2,c); for(int i=0;i<4;i++) tft.drawFastHLine(cx-5,cy-6+i*4,(i==3)?6:10,c); break;
    case 8:   // Alertas (sino)
      tft.fillCircle(cx,cy-2,7,c); tft.fillRect(cx-7,cy-2,15,8,c); tft.fillRect(cx-10,cy+6,21,2,c); tft.fillCircle(cx,cy+10,2,c); tft.fillRect(cx-1,cy-11,3,3,c); break;
    case 9:   // Automacao (raio)
      tft.fillTriangle(cx+3,cy-11,cx-6,cy+2,cx+1,cy+2,c); tft.fillTriangle(cx-3,cy+11,cx+6,cy-3,cx-1,cy-3,c); break;
    case 10:  // Ovos
      tft.fillEllipse(cx,cy+1,8,11,c); tft.fillEllipse(cx-3,cy-3,2,3,bg); break;
    case 11:  // Arquivos (pasta)
      tft.fillRoundRect(cx-11,cy-6,22,16,2,c); tft.fillRect(cx-11,cy-9,10,4,c); tft.drawFastHLine(cx-8,cy-1,16,bg); break;
    case 12:  // Seguranca (cadeado)
      tft.drawArc(cx,cy-2,9,6,90,270,c,bg,false); tft.fillRoundRect(cx-8,cy-1,16,12,3,c); tft.fillCircle(cx,cy+4,2,bg); break;
    case 13:  // Relatorios (barras)
      tft.fillRect(cx-9,cy+1,5,9,c); tft.fillRect(cx-2,cy-5,5,15,c); tft.fillRect(cx+5,cy-10,5,20,c); tft.drawFastHLine(cx-11,cy+11,22,c); break;
    case 14:  // Nome (etiqueta)
      tft.fillRoundRect(cx-11,cy-7,22,14,3,c); tft.fillCircle(cx-6,cy,2,bg); tft.drawFastHLine(cx-1,cy-2,8,bg); tft.drawFastHLine(cx-1,cy+2,6,bg); break;
    case 15:  // Historico (relogio)
      tft.drawCircle(cx,cy,10,c); tft.drawCircle(cx,cy,9,c); tft.drawLine(cx,cy,cx,cy-6,c); tft.drawLine(cx,cy,cx+5,cy+2,c); break;
    case 16:  // Hardware (resistencia em ziguezague)
      tft.drawFastHLine(cx-12,cy,5,c); tft.drawLine(cx-7,cy,cx-5,cy-7,c); tft.drawLine(cx-5,cy-7,cx-2,cy+7,c); tft.drawLine(cx-2,cy+7,cx+1,cy-7,c);
      tft.drawLine(cx+1,cy-7,cx+4,cy+7,c); tft.drawLine(cx+4,cy+7,cx+6,cy,c); tft.drawFastHLine(cx+6,cy,6,c); tft.drawRect(cx-13,cy-10,26,20,c); break;
    case 18:  // Fans (helice de 3 pas dentro de um aro)
      tft.drawCircle(cx,cy,11,c); tft.fillCircle(cx,cy,2,c); for(int k=0;k<3;k++){ float a=k*2.0944f-1.5708f; tft.fillCircle(cx+(int)(cosf(a)*6),cy+(int)(sinf(a)*6),3,c); } break;
    case 17:  // Tampa (caixa com a tampa levantada + sensor)
      tft.drawRect(cx-11,cy+1,22,10,c); tft.drawLine(cx-11,cy+1,cx-7,cy-9,c); tft.drawLine(cx-7,cy-9,cx+9,cy-9,c); tft.drawLine(cx+9,cy-9,cx+11,cy+1,c); tft.fillRect(cx-3,cy-6,6,4,c); break;
  }
}
static uint16_t setAcento(int id){
  static const uint8_t R[19]={90,255,255,140,255,120,170,190,255,255,255,255,120,110,255,150,255,110,120};
  static const uint8_t G[19]={190,200,170,200,190,230,170,200,110,225,210,200,230,190,140,170,130,225,215};
  static const uint8_t B[19]={255,80,80,255,60,150,255,120,110,60,140,70,130,255,210,255,70,205,255};
  return tft.color565(R[id],G[id],B[id]);
}
static void settingsDesenhaCelula(int idx,int id,bool press){
  int c=idx%3, r=idx/3, x=SET_CX0+c*(SET_CW+SET_GX), y=SET_CY0+r*(SET_CH+SET_GY);
  uint16_t cardBg=tft.color565(16,16,46), brd=tft.color565(46,46,100), ac=setAcento(id);
  if(press){ cardBg=tft.color565(40,40,90); brd=ac; }
  tft.fillRoundRect(x,y,SET_CW,SET_CH,9,cardBg); tft.drawRoundRect(x,y,SET_CW,SET_CH,9,brd);
  setIcone(id,x+SET_CW/2,y+20,ac,cardBg);
  tft.setTextSize(1); tft.setTextColor(press?TFT_WHITE:tft.color565(210,210,235),cardBg);
  tft.setCursor(x+(SET_CW-tft.textWidth(SET_LABEL[id]))/2,y+39); tft.print(SET_LABEL[id]);
  // ponto de estado (canto)
  int nOn=0; for(int i=0;i<8;i++) if(relayState[i]) nOn++;
  int st=-1;   // -1 nada · 0 apagado · 1 ok · 2 atencao
  if(id==0) st=wifiOK?1:0; else if(id==2) st=nOn?2:-1; else if(id==9) st=autoAtivo?1:0; else if(id==12) st=pinAtivo?1:-1;
  else if(id==7||id==11) st=sdOK?1:0; else if(id==4) st=2; else if(id==17) st=adxlSettingsDot(); else if(id==18) st=fansAtivo()?1:-1;
  if(st>=0) tft.fillCircle(x+SET_CW-10,y+9,3,st==1?cGreen:st==2?tft.color565(255,170,60):tft.color565(120,60,60));
}
void drawSettingsPageIndicator(){   // faixa do topo: casa (inicio) + titulo + "pagina >" pulsante
  uint16_t bg=tft.color565(8,8,26);
  tft.fillRect(0,0,320,18,bg);
  tft.setTextSize(1); tft.setTextColor(cLogo,bg); tft.setCursor(22,5); tft.print("CONFIGURACOES");
  uint16_t c=tft.color565(140,140,200);   // casinha
  tft.fillTriangle(6,9,12,3,18,9,c); tft.fillRect(8,9,8,6,c);
  char pg[10]; snprintf(pg,sizeof(pg),"%d/%d >",settingsPage+1,SETTINGS_TOTAL_PAGES);
  tft.setTextColor(settingsArrowOn?cLogo:tft.color565(120,100,30),bg); tft.setCursor(314-tft.textWidth(pg),5); tft.print(pg);
}
void drawSettingsPanel(){
  tft.fillRect(0,0,320,240,tft.color565(8,8,26));
  drawSettingsPageIndicator();
  const uint8_t* ids; int n=settingsItensPagina(&ids);
  for(int i=0;i<n;i++) settingsDesenhaCelula(i,ids[i],false);
}

// Snackbar estilo Android — mensagem temporaria sobre a tela atual,
// some sozinha depois de um tempo, depois volta pro painel de config.
void toastEmDesenvolvimento(const char* nome){
  tft.fillRoundRect(20,192,280,38,8,tft.color565(50,40,10));
  tft.drawRoundRect(20,192,280,38,8,tft.color565(200,150,40));
  tft.setTextColor(tft.color565(255,210,120),tft.color565(50,40,10));
  tft.setTextSize(1);
  tft.setCursor(30,204);
  tft.printf("%s: em desenvolvimento", nome);
  delay(1300);
  drawSettingsPanel();
}

// ─────────────────────────────────────────────────────────────
// Tela — tempo ate apagar por inatividade. O nivel de brilho de 50% NAO
// e possivel neste hardware: testado por continuidade em 16/09/2026, o
// pino LED do display esta ligado direto no 3V3 do ESP32-S3 (ao lado do
// RST), sem GPIO no caminho. So daria pra fazer PWM cortando essa trilha
// e religando o pino LED num GPIO livre (o transistor Q1 que ja existe
// na placa do display serviria de driver).
// ─────────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════
// SISTEMA
// ═══════════════════════════════════════════════════════════════
// ID do dispositivo pro vinculo com o PWA (tela "Vincular Chocadeira") —
// e so o ID do documento do Firestore (parte depois de "chocadeiras/"),
// nao precisa de nenhuma escrita nova: ja e o mesmo valor fixo usado em
// FIRESTORE_DOCUMENT_PATH pra todo o sync de atuadores.
const char* getDeviceId(){
  const char* slash = strchr(FIRESTORE_DOCUMENT_PATH, '/');
  return slash ? slash+1 : FIRESTORE_DOCUMENT_PATH;
}

// Fase 32 — media da temperatura interna do ESP32 desde o boot.
float espTempMedia(){ return espTempN>0 ? espTempSoma/espTempN : NAN; }

// Chamada periodicamente do loop() (nao so quando a tela Sistema esta
// aberta), pra Min/Max refletirem a sessao inteira, nao so o tempo que
// alguem passou olhando a tela.
void atualizarEspTempStats(){
  float t = temperatureRead();  // API nativa do core Arduino p/ ESP32-S3, sensor dedicado
  if(isnan(t)) return;
  espTempAtual = t;
  if(isnan(espTempMin) || t<espTempMin) espTempMin=t;
  if(isnan(espTempMax) || t>espTempMax) espTempMax=t;
  espTempSoma += t; espTempN++;
}

// Fase 32 — icones simples desenhados com primitivas (mesmo espirito de
// drawXBtn/drawSearchIcon/drawTrashIcon ja usados no resto do app).
// drawArc() e nativo do TFT_eSPI (usado em gauges) -- 0 graus = topo,
// cresce no sentido horario.
void drawRestartIcon(int cx,int cy,uint16_t c,uint16_t bg){
  tft.drawArc(cx,cy,8,6,30,300,c,bg,true);
  // seta na ponta aberta do anel (perto de 300 graus)
  float rad = 300.0f*3.14159265f/180.0f;
  int ax = cx + (int)(8*sinf(rad));
  int ay = cy - (int)(8*cosf(rad));
  tft.fillTriangle(ax-4,ay-1, ax+3,ay-4, ax+1,ay+4, c);
}
void drawPowerIcon(int cx,int cy,uint16_t c,uint16_t bg){
  tft.drawArc(cx,cy,8,6,40,320,c,bg,true);  // anel com abertura no topo
  tft.drawFastVLine(cx,cy-9,7,c);           // "haste" atravessando a abertura
}

// Fase 34 — cor de contorno padrão de TODOS os tiles/cards desta tela.
#define SYS_TILE_BORDER tft.color565(46,46,90)
// Fase 34 — fundo neutro (nao mais colorido) dos 2 cards do rodape --
// pedido explicito: "esses 2 cards do rodape nao pode ter fundo verde".
// Status agora passa pelo CONTORNO + cor do texto, nao pelo preenchimento
// inteiro do card.
#define SYS_CARD_BG tft.color565(16,16,36)

// Fase 34 — botões Reiniciar/Desligar viraram um "tile de ações" dentro
// da própria grade (166,133,146,30 -- onde antes ficava "Temp. ESP32",
// que se mudou pro card "Cartão SD" no rodapé, ver drawSysInfo). Cada
// metade do tile é um botão (68px), grande o bastante pro dedo, com a
// MESMA paleta já usada no modal do PWA (Reiniciar=âmbar, Desligar=
// vermelho) -- eram ícones pequenos no header antes (Fase 32/33), essa
// versão é maior e mais fácil de acertar/reconhecer.
void drawSysAcoesTile(int x,int y){
  uint16_t rbg=tft.color565(58,42,10), rfg=tft.color565(255,190,90);
  uint16_t dbg=tft.color565(52,14,14), dfg=tft.color565(255,140,140);
  tft.fillRoundRect(x,y,68,30,6,rbg);
  drawRestartIcon(x+34,y+15,rfg,rbg);
  tft.fillRoundRect(x+78,y,68,30,6,dbg);
  drawPowerIcon(x+112,y+15,dfg,dbg);
}

// Fase 32 — redesenhada na Fase 33/34: acoes agora moram na propria
// grade (ver drawSysAcoesTile) e o rodape virou 2 cards de fundo neutro
// (Ultimo reset + Cartao SD/Temp ESP32 combinados) -- ver comentarios
// acima em SYS_CARD_BG.
void drawSysInfo(){
  tft.fillScreen(cBg); uiTopo("SISTEMA");
  { const char* cr="Desenvolvido por Erick - ErDigital2022"; tft.setTextSize(1); tft.setTextColor(tft.color565(255,200,90),cBg); tft.setCursor(160-tft.textWidth(cr)/2,22); tft.print(cr); }

  // Cor do VALOR padronizada: branco pra dado neutro, cor só quando é
  // genuinamente um status/saúde (WiFi, reset, SD).
  auto tile=[&](int x,int y,int w,int h,const char* lb,const char* vl,uint16_t vc=TFT_WHITE){
    tft.drawRoundRect(x,y,w,h,5,SYS_TILE_BORDER);
    tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(x+6,y+5);tft.print(lb);
    tft.setTextColor(vc,cBg);tft.setCursor(x+6,y+17);tft.print(vl);
  };

  tile(8,34,146,30,"Firmware","ChocaIoT v1.2");
  tile(166,34,146,30,"ID Disp.",getDeviceId());

  char cb[24];sprintf(cb,"S3 rev%d",ESP.getChipRevision());
  tile(8,67,146,30,"Chip",cb);
  tile(166,67,146,30,"WiFi",wifiOK?WiFi.localIP().toString().c_str():"Sem conexao",wifiOK?cGreen:cRed);

  char fpb[24];sprintf(fpb,"%uMB / %uKB",ESP.getFlashChipSize()/1048576,ESP.getPsramSize()/1024);
  tile(8,100,146,30,"Flash / PSRAM",fpb);
  char rb[16];sprintf(rb,"%u KB",ESP.getFreeHeap()/1024);
  tile(166,100,146,30,"RAM livre",rb);

  unsigned long up=(millis()-uptimeStart)/1000;char ub[16];sprintf(ub,"%02lu:%02lu:%02lu",up/3600,(up%3600)/60,up%60);
  tile(8,133,146,30,"Uptime",ub);
  drawSysAcoesTile(166,133);

  // Rodapé — card "Último reset" (fundo neutro, status pelo contorno/texto).
  uint16_t resetAccent = resetFoiAnormal ? cRed : cGreen;
  tft.fillRoundRect(8,172,146,58,6,SYS_CARD_BG);
  tft.drawRoundRect(8,172,146,58,6,resetAccent);
  tft.setTextSize(1);tft.setTextColor(cSub,SYS_CARD_BG);tft.setCursor(14,180);tft.print("Ultimo reset");
  tft.setTextColor(resetAccent,SYS_CARD_BG);tft.setCursor(14,194);tft.print(lastResetReasonShort);
  // Fase 19 — se ha resets anormais em sequencia nesta sessao de energia
  // (loop de boot), mostra quantos, direto na tela -- sem precisar de Serial.
  if(bootLoopCounter>1){
    char lb[24];sprintf(lb,"(%u seguidos)",(unsigned)bootLoopCounter);
    tft.setTextColor(cSub,SYS_CARD_BG);tft.setCursor(14,206);tft.print(lb);
  }

  // Rodapé — card "Cartão SD" + "Temp. ESP32" combinados (Fase 34 -- a
  // temperatura saiu da grade, ver drawSysAcoesTile acima, e passou a
  // dividir este card com o status do SD).
  uint16_t sdAccent = sdOK ? cGreen : tft.color565(255,180,80);
  tft.fillRoundRect(166,172,146,58,6,SYS_CARD_BG);
  tft.drawRoundRect(166,172,146,58,6,sdAccent);
  tft.setTextColor(cSub,SYS_CARD_BG);tft.setCursor(172,178);tft.print("Cartao SD");
  tft.setTextColor(sdAccent,SYS_CARD_BG);tft.setCursor(266,178);tft.print(sdOK?"OK":"Falha");
  tft.drawFastHLine(172,190,134,SYS_TILE_BORDER);
  tft.setTextColor(cSub,SYS_CARD_BG);tft.setCursor(172,196);tft.print("Temp. ESP32");
  uint16_t tempAccent = isnan(espTempAtual) ? cSub : (espTempAtual>=70?cRed:(espTempAtual>=55?tft.color565(255,170,80):cGreen));
  char vb[10]; if(isnan(espTempAtual)) strlcpy(vb,"--.-C",10); else snprintf(vb,10,"%.1fC",espTempAtual);
  tft.setTextColor(tempAccent,SYS_CARD_BG);tft.setCursor(172,206);tft.print(vb);
  char mb[28];
  if(espTempN>0) snprintf(mb,28,"Mn%.0f Md%.0f Mx%.0f",espTempMin,espTempMedia(),espTempMax);
  else strlcpy(mb,"aguardando...",28);
  tft.setTextColor(cSub,SYS_CARD_BG);tft.setCursor(172,217);tft.print(mb);
}

// ═══════════════════════════════════════════════════════════════
// AJUSTE MANUAL DE DATA/HORA — botão que existia na tela de
// Configurações mas nunca tinha sido ligado a nada (bug corrigido aqui).
// Útil sem WiFi/NTP (chocadeira pode operar totalmente offline).
// ═══════════════════════════════════════════════════════════════
#define DT_ROW_H 34
#define DT_ROW_Y0 22
const char* DT_LABELS[5] = {"Dia","Mes","Ano","Hora","Minuto"};

int* dtField(int i){
  switch(i){case 0:return &editDay;case 1:return &editMonth;case 2:return &editYear;
            case 3:return &editHour;default:return &editMinute;}
}
void dtClamp(int i){
  int* v=dtField(i);
  int lo=(i==2)?2024:(i==0||i==1)?1:0;
  int hi=(i==0)?31:(i==1)?12:(i==2)?2099:(i==3)?23:59;
  if(*v<lo) *v=hi; if(*v>hi) *v=lo;   // cicla nas pontas, mais rapido de ajustar no touch
}

void loadEditFromRtc(){
  DateTime n = rtcOK ? rtc.now() : DateTime(2026,1,1,0,0,0);
  editDay=n.day(); editMonth=n.month(); editYear=n.year(); editHour=n.hour(); editMinute=n.minute();
}

void drawDateTimeScreen(){
  tft.fillScreen(cBg);
  uiTopo("DATA / HORA","toque em Salvar",cSub);
  for(int i=0;i<5;i++){
    int y=DT_ROW_Y0+i*DT_ROW_H;
    uiCard(6,y,308,DT_ROW_H-3);
    tft.setTextColor(cSub,tft.color565(16,16,46));tft.setTextSize(2);tft.setCursor(16,y+8);tft.print(DT_LABELS[i]);
    tft.fillRoundRect(150,y+3,34,DT_ROW_H-9,6,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setTextSize(2);tft.setCursor(163,y+8);tft.print("-");
    tft.fillRoundRect(270,y+3,34,DT_ROW_H-9,6,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setCursor(283,y+8);tft.print("+");
    char vb[6];
    if(i==2) snprintf(vb,6,"%04d",*dtField(i)); else snprintf(vb,6,"%02d",*dtField(i));
    tft.setTextColor(cClk,tft.color565(16,16,46));tft.setTextSize(2);tft.setCursor(198,y+8);tft.print(vb);
  }
  tft.fillRoundRect(90,DT_ROW_Y0+5*DT_ROW_H+4,140,30,8,cGreen);
  tft.setTextColor(tft.color565(10,30,10));tft.setTextSize(2);tft.setCursor(118,DT_ROW_Y0+5*DT_ROW_H+11);tft.print("Salvar");
}

void handleDateTimeTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_SETTINGS; drawSettingsPanel(); return; }   // volta sem salvar

  int saveY=DT_ROW_Y0+5*DT_ROW_H+4;
  if(ty>=saveY && ty<=saveY+30 && tx>=90 && tx<=230){
    if(rtcOK) rtc.adjust(DateTime(editYear,editMonth,editDay,editHour,editMinute,0));
    tft.fillRoundRect(60,100,200,40,8,tft.color565(10,45,10));
    tft.drawRoundRect(60,100,200,40,8,cGreen);
    tft.setTextColor(cGreen,tft.color565(10,45,10));tft.setTextSize(2);tft.setCursor(80,112);tft.print("Hora ajustada!");
    delay(1000);
    appState=S_SETTINGS; drawSettingsPanel();
    return;
  }

  int rowIdx=(ty-DT_ROW_Y0)/DT_ROW_H;
  if(ty<DT_ROW_Y0||rowIdx<0||rowIdx>=5) return;
  int* v=dtField(rowIdx);
  if(tx>=150&&tx<184){ (*v)--; dtClamp(rowIdx); drawDateTimeScreen(); }
  else if(tx>=270&&tx<304){ (*v)++; dtClamp(rowIdx); drawDateTimeScreen(); }
}

// ═══════════════════════════════════════════════════════════════
// MEMÓRIA — RAM/Heap/Flash/PSRAM em tempo real (atualiza sozinha
// enquanto a tela estiver aberta, ver loop()). Fase 34 — virou 2
// páginas (mesmo padrão de paginação já usado em Configurações, Fase
// 25): página 0 são as 4 barras ao vivo, inalteradas; página 1 é nova
// (pedido explícito) -- armazenamento do cartão SD, com o MESMO estilo
// de card/contorno já padronizado na tela Sistema (Fase 33/34).
// ═══════════════════════════════════════════════════════════════
int memPage = 0;
#define MEM_TOTAL_PAGES 3
bool memArrowOn = true;

static void memTag(int x,int y,const char* t,uint16_t bg,uint16_t fg){
  int w=tft.textWidth(t)+14; tft.fillRoundRect(x,y,w,14,7,bg); tft.setTextSize(1); tft.setTextColor(fg,bg); tft.setCursor(x+7,y+3); tft.print(t);
}
void drawMemBar(int y,const char* label,uint32_t used,uint32_t total,uint16_t barColor){
  tft.fillRect(0,y,320,42,cBg);   // limpa so a faixa dessa metrica (evita flicker no resto da tela)
  uiCard(6,y,308,40);
  uint16_t cb=tft.color565(16,16,46);
  tft.fillRoundRect(6,y,6,40,3,barColor);
  tft.setTextColor(TFT_WHITE,cb);tft.setTextSize(1);tft.setCursor(20,y+6);tft.print(label);
  char vb[40];
  if(total==0) snprintf(vb,40,"indisponivel");
  else snprintf(vb,40,"%u / %u KB",(unsigned)(used/1024),(unsigned)(total/1024));
  tft.setTextColor(cSub,cb);tft.setCursor(314-tft.textWidth(vb)-8,y+6);tft.print(vb);
  tft.drawRoundRect(20,y+22,284,10,4,tft.color565(60,60,90));
  if(total>0){
    float pct = (float)used/(float)total; if(pct>1) pct=1;
    int w=(int)((284-4)*pct);
    if(w>0) tft.fillRoundRect(22,y+24,w,6,2,barColor);
  }
}

// "1,2 GB"/"320 MB", escolhe a unidade sozinho.
void formatBytesHuman(uint64_t bytes, char* buf, size_t n){
  if(bytes >= 1073741824ULL) snprintf(buf,n,"%.1f GB", bytes/1073741824.0);
  else if(bytes >= 1048576ULL) snprintf(buf,n,"%u MB", (unsigned)(bytes/1048576ULL));
  else snprintf(buf,n,"%u KB", (unsigned)(bytes/1024ULL));
}
static void fmtTam(uint32_t b,char* o,size_t n){ if(b>=1048576UL) snprintf(o,n,"%.1fM",b/1048576.0); else if(b>=1024) snprintf(o,n,"%uK",(unsigned)(b/1024)); else snprintf(o,n,"%uB",(unsigned)b); }

void drawMemPageIndicator(){   // "1/3 >" pulsante na faixa do topo
  static const char* T[3]={"MEMORIA","SISTEMA","USUARIO"};
  char pg[10]; snprintf(pg,sizeof(pg),"%d/%d >",memPage+1,MEM_TOTAL_PAGES);
  uiTopo(T[memPage],pg,memArrowOn?cLogo:tft.color565(120,100,30));
}

void drawMemoriaScreen(){
  tft.fillScreen(cBg);
  drawMemPageIndicator();
  lastMemRefreshMs=0;   // força o primeiro desenho das barras logo em seguida
}

void updateMemoriaValues(){
  if(memPage!=0) return;   // paginas 2 e 3 sao estaticas
  uint32_t heapTotal=ESP.getHeapSize(), heapFree=ESP.getFreeHeap();
  drawMemBar(22,"RAM (heap) em uso", heapTotal-heapFree, heapTotal, tft.color565(80,195,255));
  uint32_t heapMin=ESP.getMinFreeHeap();
  drawMemBar(68,"RAM - pico de uso (minimo livre)", heapTotal-heapMin, heapTotal, tft.color565(200,140,40));
  uint32_t psramTotal=ESP.getPsramSize(), psramFree=ESP.getFreePsram();
  drawMemBar(114,"PSRAM (imagens / wallpapers)", psramTotal-psramFree, psramTotal, tft.color565(95,195,95));
  uiCard(6,160,308,34);
  uint16_t cb=tft.color565(16,16,46);
  tft.setTextSize(1);tft.setTextColor(cSub,cb);tft.setCursor(16,168);tft.print("Maior bloco livre (heap)");
  char mb[24]; snprintf(mb,24,"%u KB",(unsigned)(ESP.getMaxAllocHeap()/1024));
  tft.setTextColor(TFT_WHITE,cb);tft.setTextSize(2);tft.setCursor(16,178);
  tft.fillRect(16,178,120,16,cb); tft.print(mb);
  tft.setTextSize(1);tft.setTextColor(tft.color565(90,90,130),cBg);tft.setCursor(10,204);tft.print("Atualiza ao vivo. Toque em 1/3 > (topo) p/ trocar.");
}

// Pagina 2 -- ARQUIVOS DO SISTEMA (flash): firmware + arquivos de configuracao do aparelho (LittleFS). Cor ambar = nao mexer.
static void drawMemSistemaPage(){
  uint16_t am=tft.color565(255,180,70), amBg=tft.color565(50,36,10);
  memTag(8,22,"ARQUIVOS DO SISTEMA",amBg,am);
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(160,25);tft.print("somente leitura");
  uint32_t su=ESP.getSketchSize(), sf=ESP.getFreeSketchSpace();
  drawMemBar(40,"Firmware (programa na flash)", su, su+sf, tft.color565(150,120,255));
  drawMemBar(86,"Configuracoes (LittleFS)", LittleFS.usedBytes(), LittleFS.totalBytes(), am);
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(10,132);tft.print("Arquivos internos:");
  File root=LittleFS.open("/"); int n=0; int y=144;
  if(root){
    File f=root.openNextFile();
    while(f && n<8){
      char nm[26]; snprintf(nm,sizeof(nm),"%.24s",f.name());
      tft.fillRect(8,y,304,11,cBg);
      tft.fillRect(10,y+3,6,5,am); tft.drawRect(11,y+1,4,3,am);   // cadeadinho
      tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(22,y+1);tft.print(nm);
      char sz[12]; fmtTam(f.size(),sz,sizeof(sz)); tft.setTextColor(cSub,cBg);tft.setCursor(312-tft.textWidth(sz),y+1);tft.print(sz);
      y+=11; n++; f=root.openNextFile();
    }
    if(!n){ tft.setTextColor(cSub,cBg);tft.setCursor(22,y+1);tft.print("(vazio)"); }
  }
}
// Pagina 3 -- ARQUIVOS DO USUARIO (cartao SD): armazenamento + amostra da raiz + atalho pro gerenciador de arquivos.
static void drawMemUsuarioPage(){
  uint16_t az=tft.color565(110,190,255), azBg=tft.color565(10,30,58);
  memTag(8,22,"ARQUIVOS DO USUARIO",azBg,az);
  tft.setTextSize(1);tft.setTextColor(sdOK?cGreen:tft.color565(255,180,80),cBg);tft.setCursor(170,25);tft.print(sdOK?"Cartao SD ok":"SD indisponivel");
  if(sdOK) drawMemBar(40,"Cartao SD (usuario)", (uint32_t)min<uint64_t>(SD.usedBytes(),0xFFFFFFFFULL), (uint32_t)min<uint64_t>(SD.totalBytes(),0xFFFFFFFFULL), az);
  else { uiCard(6,40,308,40); tft.setTextSize(1);tft.setTextColor(cSub,tft.color565(16,16,46));tft.setCursor(16,56);tft.print("Sem cartao montado - veja a tela Sistema."); }
  if(sdOK){
    uint64_t tot=SD.totalBytes(), us=SD.usedBytes(); char ub[16],tb[16]; formatBytesHuman(us,ub,16); formatBytesHuman(tot,tb,16);
    char l[48]; snprintf(l,sizeof(l),"%s de %s usados (%d%%)",ub,tb,tot?(int)((us*100)/tot):0);
    tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(10,86);tft.print(l);
    File root=SD.open("/"); int n=0; int y=100;
    if(root){
      File f=root.openNextFile();
      while(f && n<7){
        if(!strcmp(f.name(),"System Volume Information")){ f=root.openNextFile(); continue; }   // pasta do Windows, nao interessa
        bool dir=f.isDirectory(); char nm[26]; snprintf(nm,sizeof(nm),"%.24s",f.name());
        tft.fillRect(8,y,304,11,cBg);
        if(dir){ tft.fillRect(10,y+2,12,8,az); tft.fillRect(10,y,5,3,az); } else { tft.drawRect(11,y,9,11,tft.color565(150,150,180)); }
        tft.setTextColor(dir?az:TFT_WHITE,cBg);tft.setCursor(26,y+1);tft.print(nm);
        if(!dir){ char sz[12]; fmtTam(f.size(),sz,sizeof(sz)); tft.setTextColor(cSub,cBg);tft.setCursor(312-tft.textWidth(sz),y+1);tft.print(sz); }
        y+=12; n++; f=root.openNextFile();
      }
      if(!n){ tft.setTextColor(cSub,cBg);tft.setCursor(26,y+1);tft.print("(cartao vazio)"); }
    }
  }
  uiCard(20,192,280,34); tft.setTextSize(2);tft.setTextColor(az,tft.color565(16,16,46));
  const char* b="Abrir arquivos"; tft.setCursor(160-tft.textWidth(b)/2,201); tft.print(b);
}
void drawMemCurrentPageContent(){
  if(memPage==0) updateMemoriaValues();
  else if(memPage==1) drawMemSistemaPage();
  else drawMemUsuarioPage();
}
// Fase 34 — despacha pro conteudo certo conforme memPage. Usada no ponto
// de entrada, ao trocar de pagina, e por redrawCurrentScreen() (acordar
// do screensaver) -- sem isso, acordar na pagina 1 (SD) ficaria com a
// area de conteudo em branco (updateMemoriaValues() sozinha so' desenha
// a pagina 0).

// ═══════════════════════════════════════════════════════════════
// WIFI
// ═══════════════════════════════════════════════════════════════
void drawSignalBars(int x,int y,int rssi){int hs[]={4,7,10,13},th[]={-90,-75,-60,-45};for(int i=0;i<4;i++){uint16_t c=(rssi>=th[i])?cGreen:tft.color565(34,34,58);tft.fillRect(x+i*6,y+13-hs[i],4,hs[i],c);}}
void drawWifiList(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(90,6);tft.print("Redes WiFi");
  tft.fillRoundRect(220,4,96,20,4,tft.color565(28,65,28));tft.setTextColor(cGreen,tft.color565(28,65,28));tft.setTextSize(1);tft.setCursor(228,11);tft.print("+ Add rede");
  int vis=4,rh=44,sy=38;if(numNets<=0){tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(72,115);tft.print("Nenhuma rede encontrada.");return;}
  if(listOff>0){tft.fillRect(110,28,100,10,tft.color565(26,26,68));tft.setTextColor(TFT_WHITE,tft.color565(26,26,68));tft.setTextSize(1);tft.setCursor(136,30);tft.print("^ cima ^");}
  String cur=wifiOK?WiFi.SSID():""; String salva=(!wifiOK&&savedSSID[0])?String(savedSSID):String("");
  for(int i=0;i<min(vis,numNets-listOff);i++){
    int ni=i+listOff,y=sy+i*rh;bool isCur=wifiOK&&(WiFi.SSID(ni)==cur);
    uint16_t rb=isCur?tft.color565(8,30,10):((i%2==0)?tft.color565(12,12,34):tft.color565(18,18,48));
    tft.fillRect(0,y,318,rh-2,rb);tft.drawFastHLine(0,y+rh-2,318,tft.color565(34,34,68));
    tft.setTextColor(isCur?cGreen:TFT_WHITE,rb);tft.setTextSize(2);String ss=WiFi.SSID(ni);if(ss.length()>13) ss=ss.substring(0,12)+"~";tft.setCursor(8,y+4);tft.print(ss);
    tft.setTextSize(1);if(isCur){tft.setTextColor(cGreen,rb);tft.setCursor(8,y+28);tft.print("  Conectada > detalhes");}
    else if(salva.length()&&WiFi.SSID(ni)==salva){tft.setTextColor(cClk,rb);tft.setCursor(8,y+28);tft.print("  Salva > toque p/ reconectar");}
    else{bool lk=(WiFi.encryptionType(ni)!=WIFI_AUTH_OPEN);tft.setTextColor(lk?tft.color565(250,192,42):tft.color565(90,195,90),rb);tft.setCursor(8,y+28);tft.print(lk?"  Protegida":"  Aberta");}
    drawSignalBars(292,y+10,WiFi.RSSI(ni));
  }
  int by=sy+vis*rh;if(listOff+vis<numNets){tft.fillRect(110,by,100,10,tft.color565(26,26,68));tft.setTextColor(TFT_WHITE,tft.color565(26,26,68));tft.setTextSize(1);tft.setCursor(132,by+2);tft.print("v mais v");}
  tft.setTextSize(1);tft.setTextColor(tft.color565(36,36,72),cBg);tft.setCursor(48,228);tft.print("Toque em uma rede para conectar");
}
void startWifiScan(){tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(90,6);tft.print("Redes WiFi");tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(72,112);tft.print("Buscando redes...");WiFi.mode(WIFI_STA);listOff=0;numNets=WiFi.scanNetworks();appState=S_WIFI_LIST;drawWifiList();}
// ── Fase 61: WiFi redesenhado (detalhes em tela cheia, QR da senha, icones com efeito, conexao discreta) ──
bool wifiFromHome=false;   // aberto pelo icone de conexao da tela inicial: "<" volta pra la
static void wifiGlifo(int cx,int cy,int lit,uint16_t on,uint16_t off,bool slash){   // leque de 3 arcos + ponto (lit 0..4)
  tft.fillCircle(cx,cy+12,3,lit>=1?on:off);
  for(int i=0;i<3;i++){ int r=11+i*9; tft.drawArc(cx,cy+12,r+4,r,135,225,lit>=i+2?on:off,cBg,false); }
  if(slash){ for(int d=-1;d<=1;d++) tft.drawLine(cx-16+d,cy-8,cx+16+d,cy+20,TFT_WHITE); }
}
static void wifiBotaoIcone(int tipo,int cx,int cy,uint16_t c,bool ativo){   // 0 olho · 1 desconectar · 2 lixeira
  if(tipo==0){
    tft.drawEllipse(cx,cy,14,8,c); tft.drawEllipse(cx,cy,13,7,c); tft.fillCircle(cx,cy,4,c);
    if(!ativo){ for(int d=0;d<2;d++) tft.drawLine(cx-13,cy+10+d,cx+13,cy-10+d,c); }
  }else if(tipo==1){
    for(int i=0;i<3;i++){ int r=7+i*6; tft.drawArc(cx,cy+8,r+3,r,135,225,c,cBg,false); }
    tft.fillCircle(cx,cy+8,2,c); for(int d=0;d<2;d++) tft.drawLine(cx-13,cy-8+d,cx+13,cy+14+d,c);
  }else{
    tft.fillRect(cx-9,cy-4,18,15,c); tft.fillRect(cx-11,cy-8,22,3,c); tft.fillRect(cx-3,cy-11,6,3,c);
    for(int i=-1;i<=1;i++) tft.drawFastVLine(cx+i*5,cy-1,10,tft.color565(20,20,30));
  }
}
static void wifiBotao(int x,int y,int tipo,const char* lb,uint16_t bg,uint16_t fg,bool press,bool ativo){
  int w=96,h=54;
  if(press) bg=tft.color565(min(255,((bg>>11)&31)*8+80),min(255,((bg>>5)&63)*4+80),min(255,(bg&31)*8+80));
  tft.fillRoundRect(x,y,w,h,10,bg); tft.drawRoundRect(x,y,w,h,10,press?TFT_WHITE:fg);
  wifiBotaoIcone(tipo,x+w/2,y+20,press?TFT_WHITE:fg,ativo);
  tft.setTextSize(1); tft.setTextColor(press?TFT_WHITE:fg,bg); tft.setCursor(x+(w-tft.textWidth(lb))/2,y+41); tft.print(lb);
}
#define WD_BY 182
static void wifiBotoesDetalhe(int pressed){
  wifiBotao(8,WD_BY,0,detailShowPass?"Ocultar":"Mostrar",tft.color565(16,30,58),tft.color565(110,190,255),pressed==0,detailShowPass);
  wifiBotao(112,WD_BY,1,"Desconectar",tft.color565(58,40,6),tft.color565(255,190,60),pressed==1,true);
  wifiBotao(216,WD_BY,2,"Esquecer",tft.color565(60,14,14),tft.color565(255,100,100),pressed==2,true);
}
static void wifiEscQr(String& o,const char* s){ for(;*s;s++){ if(*s=='\\'||*s==';'||*s==','||*s==':'||*s=='"') o+='\\'; o+=*s; } }
static void wifiDesenhaQr(int x,int y,int lado){   // painel branco com o QR "WIFI:T:WPA;S:..;P:..;;" (ou cadeado se a senha estiver oculta)
  tft.fillRoundRect(x,y,lado,lado,8,detailShowPass?TFT_WHITE:tft.color565(16,16,40));
  if(!detailShowPass){
    tft.drawRoundRect(x,y,lado,lado,8,tft.color565(55,55,120));
    int cx=x+lado/2, cy=y+lado/2-8; uint16_t c=tft.color565(120,120,190);
    tft.drawArc(cx,cy-6,13,10,90,270,c,cBg,false); tft.drawArc(cx,cy-6,13,10,270,360,c,cBg,false); tft.drawArc(cx,cy-6,13,10,0,90,c,cBg,false);
    tft.fillRoundRect(cx-15,cy,30,22,4,c);
    tft.setTextSize(1); tft.setTextColor(tft.color565(110,110,160),tft.color565(16,16,40));
    const char* t1="QR e senha ocultos"; tft.setCursor(x+(lado-tft.textWidth(t1))/2,y+lado-26); tft.print(t1);
    const char* t2="Toque em Mostrar"; tft.setCursor(x+(lado-tft.textWidth(t2))/2,y+lado-14); tft.print(t2);
    return;
  }
  String p="WIFI:T:"; p+=savedPass[0]?"WPA":"nopass"; p+=";S:"; wifiEscQr(p,savedSSID); if(savedPass[0]){ p+=";P:"; wifiEscQr(p,savedPass); } p+=";;";
  static uint8_t qbuf[512]; QRCode qr; int ver=0;
  for(int v=3;v<=8;v++){ if(qrcode_getBufferSize(v)>(int)sizeof(qbuf)) break; if(qrcode_initText(&qr,qbuf,v,ECC_LOW,p.c_str())==0){ ver=v; break; } }
  if(!ver){ tft.setTextSize(1); tft.setTextColor(TFT_BLACK,TFT_WHITE); tft.setCursor(x+8,y+lado/2); tft.print("Senha longa demais p/ QR"); return; }
  int n=qr.size, m=(lado-8)/(n+2); if(m<1) m=1;
  int tot=(n+2)*m, ox=x+(lado-tot)/2+m, oy=y+(lado-tot)/2+m;
  for(int r=0;r<n;r++) for(int c=0;c<n;c++) if(qrcode_getModule(&qr,c,r)) tft.fillRect(ox+c*m,oy+r*m,m,m,TFT_BLACK);
}
void drawWifiDetail(){
  tft.fillScreen(cBg); tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard); tft.setTextSize(1); tft.setCursor(5,10); tft.print("< Voltar");
  String ss=savedSSID; if(ss.length()>14) ss=ss.substring(0,13)+"~";
  tft.setTextColor(cGreen,cCard); tft.setTextSize(2); tft.setCursor(70,6); tft.print(ss);
  drawSignalBars(292,7,WiFi.RSSI());
  uint16_t pn=tft.color565(14,14,38), br=tft.color565(46,46,90);
  tft.fillRoundRect(4,34,168,142,8,pn); tft.drawRoundRect(4,34,168,142,8,br);
  int y=40;
  auto row=[&](const char* l,const char* v,uint16_t vc){ tft.setTextSize(1); tft.setTextColor(cSub,pn); tft.setCursor(10,y); tft.print(l); tft.setTextColor(vc,pn); tft.setCursor(64,y); tft.print(v); y+=16; };
  row("IP",WiFi.localIP().toString().c_str(),cClk);
  row("Gateway",WiFi.gatewayIP().toString().c_str(),TFT_WHITE);
  row("DNS",WiFi.dnsIP().toString().c_str(),TFT_WHITE);
  row("Mascara",WiFi.subnetMask().toString().c_str(),TFT_WHITE);
  { String mac=WiFi.macAddress(); row("MAC",mac.c_str(),tft.color565(200,200,100)); }
  { char rs[20]; snprintf(rs,sizeof(rs),"%d dBm",WiFi.RSSI()); row("Sinal",rs,TFT_WHITE); }
  { char sp[24]; if(detailShowPass) snprintf(sp,sizeof(sp),"%.16s",savedPass[0]?savedPass:"(aberta)"); else { int k=0; for(;savedPass[k]&&k<12;k++) sp[k]='*'; sp[k]=0; if(!k) strcpy(sp,"(aberta)"); } row("Senha",sp,detailShowPass?TFT_WHITE:cSub); }
  int passY=y-16; (void)passY;
  wifiDesenhaQr(178,34,138);
  wifiBotoesDetalhe(-1);
}
static void wifiToast(const char* msg,bool ok){   // aviso discreto no rodape (nao troca de tela)
  uint16_t bg=ok?tft.color565(10,45,10):tft.color565(60,30,8), fg=ok?cGreen:tft.color565(255,170,60);
  tft.fillRoundRect(40,204,240,24,8,bg); tft.drawRoundRect(40,204,240,24,8,fg);
  tft.setTextSize(1); tft.setTextColor(fg,bg); tft.setCursor(160-tft.textWidth(msg)/2,212); tft.print(msg);
  delay(1100);
}
void disconnectWifi(){   // so desliga a conexao: a rede continua salva e a pessoa continua na pagina de WiFi
  WiFi.disconnect(false,false); wifiOK=false; wifiManualOff=true;
  logEvento("WiFi","Desconectado manualmente",true);
  appState=S_WIFI_LIST; drawWifiList(); wifiToast("Desconectado - rede continua salva",false);
  drawWifiList();
}
void forgetWifi(){
  if(wifiOK&&String(WiFi.SSID())==String(savedSSID)){ WiFi.disconnect(false,false); wifiOK=false; }
  LittleFS.remove("/wifi.cfg"); memset(savedSSID,0,33); memset(savedPass,0,65); wifiManualOff=false;
  appState=S_WIFI_LIST; drawWifiList(); wifiToast("Rede esquecida",false); drawWifiList();
}
void drawSSIDField(){tft.fillRect(5,40,278,24,tft.color565(5,5,18));tft.drawRect(5,40,278,24,tft.color565(75,75,170));int mx=44;char* fr=manualSSID;if(manualSSIDLen>mx) fr=manualSSID+(manualSSIDLen-mx);tft.setTextColor(TFT_WHITE,tft.color565(5,5,18));tft.setTextSize(1);tft.setCursor(9,48);tft.print(fr);tft.drawFastVLine(9+min(manualSSIDLen,mx)*6,42,18,TFT_WHITE);}
void drawPassField(){tft.fillRect(5,40,278,24,tft.color565(5,5,18));tft.drawRect(5,40,278,24,tft.color565(75,75,170));char d[65]={};for(int i=0;i<passLen;i++) d[i]=showPass?passBuf[i]:'*';int mx=44;const char* fr=d;if(passLen>mx) fr=d+(passLen-mx);tft.setTextColor(TFT_WHITE,tft.color565(5,5,18));tft.setTextSize(1);tft.setCursor(9,48);tft.print(fr);tft.drawFastVLine(9+min(passLen,mx)*6,42,17,TFT_WHITE);uint16_t shb=tft.color565(24,24,55);tft.fillRect(285,40,30,24,shb);tft.setTextColor(tft.color565(105,105,205),shb);tft.setCursor(287,48);tft.print(showPass?"HIDE":"SHOW");}
void drawKey(int x,int y,int w,const char* lbl,uint16_t bg){tft.fillRoundRect(x,y,w,KH,3,bg);tft.drawRoundRect(x,y,w,KH,3,tft.color565(68,68,108));tft.setTextColor(TFT_WHITE,bg);tft.setTextSize(1);int lw=(int)strlen(lbl)*6;tft.setCursor(x+(w-lw)/2,y+(KH-8)/2);tft.print(lbl);}
void drawKeyboard(){const char(*KB)[11]=(kbMode==0?KBL:(kbMode==1?KBU:KBN));tft.fillRect(0,KR1Y-4,320,240-(KR1Y-4),tft.color565(10,10,26));for(int i=0;i<10;i++){char l[2]={KB[0][i],0};drawKey(5+i*31,KR1Y,29,l,cKey);}for(int i=0;i<9;i++){char l[2]={KB[1][i],0};drawKey(20+i*31,KR2Y,29,l,cKey);}drawKey(4,KR3Y,42,kbMode==1?"[^]":"^",kbMode==1?cShift:cKey);for(int i=0;i<7;i++){char l[2]={KB[2][i],0};drawKey(50+i*30,KR3Y,28,l,cKey);}drawKey(262,KR3Y,54,"<--",tft.color565(76,20,20));drawKey(4,KR4Y,55,kbMode==2?"ABC":"123",cSp);drawKey(63,KR4Y,190,"ESPACO",cSp);drawKey(257,KR4Y,59,"OK",cGreen);}
void drawPassScreen(){tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,5);tft.print("< Voltar");tft.setTextColor(cLogo,cCard);String ss=String(selSSID);if(ss.length()>16) ss=ss.substring(0,15)+"~";tft.setCursor(70,5);tft.printf("Senha: %s",ss.c_str());tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(5,32);tft.print("Senha WiFi:");tft.setTextColor(tft.color565(58,58,98),cBg);tft.setCursor(225,32);tft.printf("%d/64",passLen);drawPassField();drawKeyboard();}
void drawAddNetScreen(){tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(76,6);tft.print("Add rede");tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(5,32);tft.print("Nome da rede (SSID):");drawSSIDField();tft.fillRoundRect(6,68,92,20,4,manualIsOpen?cGreen:tft.color565(38,38,68));tft.setTextColor(TFT_WHITE,manualIsOpen?cGreen:tft.color565(38,38,68));tft.setTextSize(1);tft.setCursor(14,75);tft.print("Aberta");tft.fillRoundRect(104,68,92,20,4,!manualIsOpen?cAccent:tft.color565(38,38,68));tft.setTextColor(TFT_WHITE,!manualIsOpen?cAccent:tft.color565(38,38,68));tft.setTextSize(1);tft.setCursor(112,75);tft.print("WPA2/WPA3");}

void connectWifi(const char* ss,const char* pw){
  appState=S_CONNECTING; wifiManualOff=false;
  String nome=ss; if(nome.length()>20) nome=nome.substring(0,19)+"~";
  uint16_t pn=tft.color565(14,14,38);
  auto quadro=[&](){ tft.fillScreen(cBg); tft.fillRoundRect(24,40,272,150,14,pn); tft.drawRoundRect(24,40,272,150,14,tft.color565(46,46,90)); };
  quadro();
  tft.setTextSize(2); tft.setTextColor(TFT_WHITE,pn); tft.setCursor(160-tft.textWidth(nome.c_str())/2,140); tft.print(nome);
  WiFi.disconnect(false,false); delay(100); WiFi.begin(ss,pw);
  unsigned long t0=millis(), tf=0; int frame=0; bool cancelou=false;
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){
    if(millis()-tf>=260){ tf=millis();
      int lit=(frame++%5);   // 0..4: ponto, arcos acendendo em sequencia
      wifiGlifo(160,66,lit,tft.color565(90,170,255),tft.color565(36,44,80),false);
      tft.fillRect(40,166,240,16,pn); tft.setTextSize(1); tft.setTextColor(cSub,pn);
      const char* m="Conectando..."; tft.setCursor(160-tft.textWidth(m)/2,168); tft.print(m);
    }
    uint16_t a,b; if(millis()-t0>900 && tft.getTouch(&a,&b)){ cancelou=true; break; }   // toque cancela
    delay(10);
  }
  if(WiFi.status()==WL_CONNECTED){
    wifiOK=true; WiFi.setSleep(false); strlcpy(savedSSID,ss,33); strlcpy(savedPass,pw,65); saveWifiCreds(ss,pw); syncTimeAndStartFirebase();
    quadro();
    for(int r=2;r<=26;r+=3){ tft.fillCircle(160,78,r,tft.color565(10,60,20)); tft.drawCircle(160,78,r,cGreen); delay(14); }   // anel verde cresce
    for(int d=0;d<3;d++){ tft.drawLine(147,79+d,157,89+d,cGreen); delay(35); }
    for(int d=0;d<3;d++){ tft.drawLine(157,89+d,175,68+d,cGreen); delay(18); }
    tft.setTextSize(2); tft.setTextColor(cGreen,pn); const char* ok="Conectado"; tft.setCursor(160-tft.textWidth(ok)/2,116); tft.print(ok);
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE,pn); tft.setCursor(160-tft.textWidth(nome.c_str())/2,142); tft.print(nome);
    String ip="IP "+WiFi.localIP().toString(); tft.setTextColor(cClk,pn); tft.setCursor(160-tft.textWidth(ip.c_str())/2,158); tft.print(ip);
    char rs[24]; snprintf(rs,sizeof(rs),"Sinal %d dBm",WiFi.RSSI()); tft.setTextColor(cSub,pn); tft.setCursor(160-tft.textWidth(rs)/2,172); tft.print(rs);
    delay(1300);
  }else{
    WiFi.disconnect(false,false); wifiOK=false;
    quadro();
    uint16_t rd=tft.color565(255,90,90);
    for(int r=2;r<=26;r+=3){ tft.fillCircle(160,78,r,tft.color565(60,14,14)); tft.drawCircle(160,78,r,rd); delay(14); }
    drawXBtn(160,78,10,rd);
    tft.setTextSize(2); tft.setTextColor(rd,pn); const char* f=cancelou?"Cancelado":"Nao conectou"; tft.setCursor(160-tft.textWidth(f)/2,116); tft.print(f);
    tft.setTextSize(1); tft.setTextColor(cSub,pn); const char* d=cancelou?"Conexao interrompida.":"Verifique a senha e o sinal da rede."; tft.setCursor(160-tft.textWidth(d)/2,146); tft.print(d);
    delay(1500);
  }
  appState=S_WIFI_LIST;
  if(numNets<=0){ startWifiScan(); } else drawWifiList();   // volta pra lista de redes (nao expulsa pra inicial)
}

// ═══════════════════════════════════════════════════════════════
// FIREBASE — NUVEM <-> ATUADORES (Fase 8b)
// Mesmo documento Firestore que o PWA (docs/app.js) le/escreve:
// chocadeiras/{id}.atuadores.chN (boolean). Sem "stream" nativo pra
// Firestore nesta lib (so existe SSE pra Realtime Database), entao
// a nuvem->ESP32 e feita por polling periodico; ESP32->nuvem e
// imediato, via patch, so no campo do canal que mudou (igual o PWA).
// ═══════════════════════════════════════════════════════════════
WiFiClientSecure fbSsl;
using FbAsyncClient = AsyncClientClass;
FbAsyncClient fbClient(fbSsl);

FirebaseApp fbApp;
Firestore::Documents fbDocs;
Messaging fbMsg;              // Fase 49 -- FCM (push)
bool fbStarted = false;
unsigned long fbLastPollMs = 0;
// Reduzido de 4000 -> 2500ms (2026-09-20): latência PWA->firmware pro
// toggle de atuador é diretamente esse intervalo (o firmware só vê o
// comando no próximo poll). Não desce mais que isso pra não competir
// demais com o heartbeat pelo mesmo cliente async (fbClient) -- ver
// prioridade do heartbeat no loop() -- nem se aproximar do limite de
// leituras do plano gratuito do Firestore (a 2500ms dá ~34,5 mil
// leituras/dia, contra 50 mil do limite).
#define FB_POLL_INTERVAL_MS 4000UL          // (legado) valor "ativo" usado como base; ver fbPollIntervaloMs()
// Fase 63 -- polling inteligente: o PWA aberto grava "cmd.vivo" (valor que muda) a cada poucos minutos; o aparelho so acelera enquanto
// alguem esta usando o app. Sem app aberto: 15 s (5,7 mil leituras/dia) em vez de 4 s (21,6 mil).
unsigned long pwaVivoMs=0; String pwaVivoUltimo;
static bool pwaAtivo(){ return pwaVivoMs && millis()-pwaVivoMs<8UL*60UL*1000UL; }
static unsigned long fbPollIntervaloMs(){ return pwaAtivo()?3000UL:15000UL; }
unsigned long fbLastHeartbeatMs = 0;
#define FB_HEARTBEAT_INTERVAL_MS 60000   // ver HEARTBEAT_STALE_MS no app.js do PWA (~6-7x isso)
#define FB_LEITURA_PUSH_INTERVAL_MS 300000  // Fase 20 -- cadencia do push de temperatura/umidade (SHT31)

void fbAuthCallback(AsyncResult &aResult){
  if(aResult.isError())
    Serial.printf("[Firebase Auth ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
}
void fbPatchCallback(AsyncResult &aResult){
  if(aResult.isError())
    Serial.printf("[Firebase Patch ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
  else if(aResult.isResult()) Serial.printf("[Firebase Patch OK] %s\n", aResult.uid().c_str());
}

// Documento vem no formato do Firestore REST, PRETTY-PRINTED (com espacos/quebras
// de linha entre chave e valor):
// "atuadores": { "mapValue": { "fields": { "ch0": {\n   "booleanValue": false\n },...
// Por isso o parser nao pode depender de uma sequencia exata de caracteres — busca
// a chave "chN", depois o proximo "booleanValue" (com limite de distancia pra nao
// pular pro canal seguinte), e pula espacos/quebras ate achar o valor.
//
// pollIssuedAtMs = instante em que O GET QUE GEROU ESTE JSON foi disparado
// (nao "agora"). Se um toque físico em algum canal aconteceu DEPOIS desse
// instante, este JSON é uma foto de ANTES do toque — aplicar o valor dele
// desfaria o toque que já rolou. Por canal: só aplica o valor da nuvem se
// nenhum toque local aconteceu depois que este poll foi disparado; o poll
// seguinte (disparado depois do toque) vai refletir o valor certo.
void desligarRemoto();  // definida mais abaixo (secao FIREBASE / DESLIGAR) -- precisa de tipos do FirebaseClient
void reiniciarRemoto();  // definida mais abaixo (secao FIREBASE / REINICIAR), mesmo motivo

// ─── Fase 41 — pedidos de arquivo vindos do PWA (ver bloco GERENCIADOR DE ARQUIVOS) ───
// O PWA escreve "arquivoReq" = "<idDoPedido>|<caminho no SD>" (ou "<id>|*" pra
// republicar o indice). O parse acontece aqui, no callback do poll; o TRABALHO
// (ler SD + subir pro Firestore) fica pendente e roda em loop() -- nunca dentro
// do callback async (mesma licao do comandoReiniciar, Fase 28b).
String arqReqPend = "";            // pedido novo aguardando processamento
String arqReqUltimo = "";          // ultimo valor visto (evita reprocessar o mesmo)
bool   arqReqInicializado = false; // 1a leitura pos-boot so memoriza: um pedido velho nao e reatendido a cada reboot
bool   arqIndiceSujo = false;      // indice do SD mudou -> republicar no Firestore
unsigned long arqIndiceProximoMs = 0;   // backoff apos falha (Fase 44)

bool extrairCampoString(const String &json, const char* nomeCampo, String &saida){
  char keyTag[40]; snprintf(keyTag,sizeof(keyTag),"\"%s\"",nomeCampo);
  int idx = json.indexOf(keyTag);
  if(idx < 0) return false;
  int svIdx = json.indexOf("\"stringValue\"", idx);
  if(svIdx < 0 || svIdx-idx > 60) return false;
  int colon = json.indexOf(':', svIdx);
  if(colon < 0) return false;
  int q1 = json.indexOf('"', colon+1);
  if(q1 < 0) return false;
  int q2 = json.indexOf('"', q1+1);
  if(q2 < 0) return false;
  saida = json.substring(q1+1,q2);
  return true;
}

// A biblioteca NAO escapa strings (Values::StringValue so coloca aspas em volta): aspas, barras, quebras de
// linha ou tabs dentro do texto quebram o JSON e o Firestore devolve HTTP 400. Todo texto que pode ter
// esses caracteres (indice do SD, relatorio, nome do lote, mensagens) passa por aqui antes de subir.
String jsonEsc(const String &in){
  String o; o.reserve(in.length()+8);
  for(size_t i=0;i<in.length();i++){
    char c=in[i];
    if(c=='"'||c=='\\'){ o+='\\'; o+=c; }
    else if(c=='\n') o+="\\n";
    else if(c=='\r'){ }
    else if(c=='\t') o+="\\t";
    else if((unsigned char)c<0x20) o+=' ';
    else o+=c;
  }
  return o;
}

void applyActuatorsFromFirestore(const String &json, unsigned long pollIssuedAtMs){
  bool changed=false;
  for(int ch=0; ch<8; ch++){
    if(lastLocalTouchMs[ch] >= pollIssuedAtMs) continue;  // toque local mais recente que esta leitura -- ignora, está desatualizada pra este canal

    char keyTag[8]; snprintf(keyTag,sizeof(keyTag),"\"ch%d\"",ch);
    int idx = json.indexOf(keyTag);
    if(idx < 0) continue;
    int bvIdx = json.indexOf("\"booleanValue\"", idx);
    if(bvIdx < 0 || bvIdx - idx > 60) continue;
    int colon = json.indexOf(':', bvIdx);
    if(colon < 0) continue;
    int p = colon + 1;
    while(p < (int)json.length() && isspace((unsigned char)json[p])) p++;
    bool val = json.startsWith("true", p);
    if(ch==2){ if(val) bombaNuvemLimpar=true; continue; }   // Fase 87: a bomba de agua NUNCA e ligada/restaurada pela nuvem (ligava sozinha apos reiniciar e transbordou); so toque local ou a logica da agua. Nuvem = sempre OFF
    { static bool vistoPrimeira[8]={false};   // Fase 80: apos o boot a nuvem ainda tem a "sessao de pulsos" ON do canal: nao religa o aquecedor/umidificador sozinho (esquentou alem do alvo)
      if(!vistoPrimeira[ch]){ vistoPrimeira[ch]=true; if(canalTermostato(ch) && val && !relayState[ch]){ pulsoNuvemVal[ch]=true; continue; } } }
    if(pulsoGerencia[ch]){ if(val==pulsoNuvemVal[ch]) continue; }   // Fase 72: sem mudanca do usuario, o rele segue o controle por pulsos
    // Fase 106: chegou ate aqui = o USUARIO pediu (valor diferente do que o controle publicou, ou rele diferente da nuvem). Antes, um "desligar"
    // no PWA com o rele entre dois pulsos (ja desligado) nao mudava nada e o PI voltava a ligar; agora a automacao do canal fica em pausa.
    bool usuarioMudou=(pulsoGerencia[ch] || relayState[ch]!=val);
    if(usuarioMudou && ch!=COOLER_CH){
      if(relayState[ch]!=val) setRelay(ch, val);
      manualHold(ch, val);
      changed=true;
      Serial.printf("[Firebase] Canal %d (%s) -> %s (comando da nuvem)\n", ch, RELAY_NAMES[ch], val?"ON":"OFF");
      char msg2[48]; snprintf(msg2,48,"%s -> %s (remoto)",RELAY_NAMES[ch],val?"ON":"OFF");
      logEvento("Atuador",msg2,true);
      continue;
    }
    if(relayState[ch] != val){
      setRelay(ch, val);
      changed=true;
      Serial.printf("[Firebase] Canal %d (%s) -> %s (comando da nuvem)\n", ch, RELAY_NAMES[ch], val?"ON":"OFF");
      // Fase 30 -- mesmo registro do toque local (setRelayAndSync), so
      // que esse canal aqui nunca tinha nenhum -- toggle vindo do PWA
      // ficava totalmente fora do historico de Logs.
      char msg[48]; snprintf(msg,48,"%s -> %s (remoto)",RELAY_NAMES[ch],val?"ON":"OFF");
      logEvento("Atuador",msg,true);
    }
  }

  // Comando remoto de desligar (Fase 15) -- PWA escreve comandoDesligar=true
  // no Firestore; ao ver isso aqui, o firmware entra em deep sleep. Checado
  // ANTES do "if(!changed) return" abaixo, pois o comando pode chegar sem
  // nenhum atuador ter mudado junto.
  int idxDesligar = json.indexOf("\"comandoDesligar\"");
  if(idxDesligar < 0) desligarPrimeiraLeitura = false;   // campo ausente = nunca foi usado
  if(idxDesligar >= 0){
    int bvIdx = json.indexOf("\"booleanValue\"", idxDesligar);
    if(bvIdx >= 0 && bvIdx - idxDesligar <= 60){
      int colon = json.indexOf(':', bvIdx);
      if(colon >= 0){
        int p = colon + 1;
        while(p < (int)json.length() && isspace((unsigned char)json[p])) p++;
        if(json.startsWith("true", p) && pollIssuedAtMs <= desligarLimpoEmMs){
          Serial.println("[Firebase] comandoDesligar=true de leitura anterior a limpeza -- ignorado.");
        }else if(json.startsWith("true", p) && desligarPrimeiraLeitura){
          desligarPrimeiraLeitura = false;
          desligarLimpezaPend = true; desligarSuprimidoNesteBoot = true; desligarSupressaoAteMs = millis() + 180000UL;
          Serial.println("[Firebase] comandoDesligar=true JA estava na nuvem ao ligar (antigo) -- ignorado e sera limpo.");
          logEvento("Desligar","Comando antigo de desligar encontrado ao ligar -- ignorado e limpo",true);
        }else if(json.startsWith("true", p)){
          // Fase 18 -- supressao local (nao depende de rede): ignora
          // qualquer comandoDesligar=true durante a janela de graca pos-
          // wake-por-botao, mesmo que a limpeza no Firestore ainda nao
          // tenha confirmado. E isso que impede o loop de deep sleep.
          if(desligarSuprimidoNesteBoot && millis() < desligarSupressaoAteMs){
            Serial.println("[Firebase] comandoDesligar=true ignorado -- ainda na janela de supressao pos-wake (evita loop de deep sleep).");
          }else{
            desligarSuprimidoNesteBoot = false;
            Serial.println("[Firebase] Comando remoto de desligar recebido -- entrando em deep sleep.");
            logEvento("Desligar","Comando remoto recebido -- entrando em deep sleep",true);
            desligarRemoto();  // nunca retorna (dorme)
          }
        }else{ desligarPrimeiraLeitura = false; }   // 1a leitura veio "false": a partir daqui um "true" e comando novo de verdade
      }
    }
  }

  // Comando remoto de reiniciar (Fase 28) -- mesmo padrao do comandoDesligar
  // acima, mas com ESP.restart() (software reset) em vez de deep sleep: o
  // dispositivo volta sozinho em poucos segundos, sem precisar de nenhum
  // botao fisico.
  int idxReiniciar = json.indexOf("\"comandoReiniciar\"");
  if(idxReiniciar >= 0){
    int bvIdx = json.indexOf("\"booleanValue\"", idxReiniciar);
    if(bvIdx >= 0 && bvIdx - idxReiniciar <= 60){
      int colon = json.indexOf(':', bvIdx);
      if(colon >= 0){
        int p = colon + 1;
        while(p < (int)json.length() && isspace((unsigned char)json[p])) p++;
        if(json.startsWith("true", p)){
          // Fase 28b -- supressao NAO expira por tempo curto (isso causou
          // o loop real de reboot): fica ativa ate a limpeza no Firestore
          // ser confirmada de verdade (retry periodico em loop()). So'
          // ignora o comando alem da valvula de seguranca de 3min se algo
          // estiver MUITO errado (Firestore realmente inacessivel).
          if(reiniciarSuprimidoNesteBoot){
            if(millis() < reiniciarSupressaoAteMs){
              Serial.println("[Firebase] comandoReiniciar=true ignorado -- aguardando confirmacao de limpeza no Firestore (evita loop de reboot).");
            }else{
              Serial.println("[Firebase] AVISO: supressao de comandoReiniciar excedeu a valvula de seguranca (3min) sem confirmar a limpeza no Firestore -- processando mesmo assim.");
              reiniciarSuprimidoNesteBoot = false;
              logEvento("Reiniciar","Comando remoto (pos-valvula de seguranca) -- reiniciando (ESP.restart)",true);
              reiniciarRemoto();  // nunca retorna (reinicia)
            }
          }else{
            Serial.println("[Firebase] Comando remoto de reiniciar recebido -- reiniciando (software reset).");
            logEvento("Reiniciar","Comando remoto recebido -- reiniciando (ESP.restart)",true);
            reiniciarRemoto();  // nunca retorna (reinicia)
          }
        }
      }
    }
  }

  // Fase 41 -- pedido de arquivo do PWA (so memoriza aqui; o trabalho roda em loop())
  String rq;
  if(extrairCampoString(json,"arquivoReq",rq)){
    if(!arqReqInicializado){ arqReqInicializado=true; arqReqUltimo=rq; }
    else if(rq!=arqReqUltimo){ arqReqUltimo=rq; arqReqPend=rq; }
  }else arqReqInicializado=true;

  // Fase 46 -- e-mail de recuperacao do PIN e codigo de recuperacao (so guardam; o trabalho roda em loop())
  String pr;
  if(extrairCampoString(json,"pinRec",pr)){ int b=pr.indexOf('|'); recVerificar(b>0?pr.substring(0,b):pr); }
  String em;
  if(extrairCampoString(json,"emailRec",em)) segEmailAtualiza(&em);
  else if(json.indexOf("\"fields\"")>=0) segEmailAtualiza(nullptr);
  { String po; bool tem=extrairCampoString(json,"pinOff",po);
    if(!pinOffInit){ pinOffInit=true; if(tem) pinOffUltimo=po; } else if(tem && po!=pinOffUltimo){ pinOffUltimo=po; pinOffPend=true; } }
  // Fase 55 -- nome da chocadeira (cmd.nomeChoc) e como o ciclo terminou no PWA (cmd.fimTipo)
  { String nc; if(extrairCampoString(json,"nomeChoc",nc)) nomeDoPoll(nc); String ft; if(extrairCampoString(json,"fimTipo",ft)) fimTipoRecebido=ft; }
  // Fase 65 -- cmd.camAuto ("0"/"1"): toggle de captura automatica escrito pelo PWA. Mesmo "logical clock" da Fase 9/55:
  // uma mudanca local (tela Camera) recente vence uma leitura da nuvem mais antiga.
  { String ca; if(extrairCampoString(json,"camAuto",ca) && lastLocalCamAutoMs<pollIssuedAtMs){
      bool novo=(ca=="1"); if(novo!=camAutoAtivo){ camAutoAtivo=novo; camAutoCfgSalvar(); } } }
  // Fase 66 -- o PWA grava isso ao abrir o stream ao vivo direto na camera (a nuvem nunca ve o video em si,
  // so este avisinho -- ver item "logs de quando a camera foi acessada" do pedido).
  { static bool spInit=false; static String spUlt; String sp;
    if(extrairCampoString(json,"camStreamPing",sp)){ if(!spInit){ spInit=true; spUlt=sp; } else if(sp!=spUlt){ spUlt=sp; logEvento("Camera","Stream ao vivo acessado pelo app",false); } } }
  // Fase 52 -- configuracao de automacao editada no PWA (cmd.auto)
  String au;
  if(extrairCampoString(json,"auto",au)) autoDoPoll(au,pollIssuedAtMs);
  // Fase 50 -- pedido de foto do PWA (so memoriza; o trabalho roda em loop(), ver cameraServico())
  String cq;
  if(extrairCampoString(json,"camReq",cq)){
    if(!camReqInit){ camReqInit=true; camReqUltimo=cq; }
    else if(cq!=camReqUltimo){ camReqUltimo=cq; camReqPend=cq; }
  }else camReqInit=true;   // campo ainda nao existe: a 1a vez que aparecer ja e um pedido novo
  String uq;
  if(extrairCampoString(json,"upReq",uq)){
    if(!upReqInit){ upReqInit=true; upReqUltimo=uq; }
    else if(uq!=upReqUltimo){ upReqUltimo=uq; upReqPend=uq; }
  }else upReqInit=true;
  { String vv; if(extrairCampoString(json,"vivo",vv) && vv!=pwaVivoUltimo){ pwaVivoUltimo=vv; pwaVivoMs=millis(); } }   // Fase 63: PWA aberto
  // Fase 88 -- um convidado aceitou o convite de compartilhamento (cmd.convite = "<epoch ms>|<email>|aceito"): so memoriza; o push sai no pushServico() (fora do callback)
  { String cv; if(extrairCampoString(json,"convite",cv)){ if(!conviteInit){ conviteInit=true; conviteUlt=cv; } else if(cv!=conviteUlt){ conviteUlt=cv; strlcpy(convitePend,cv.c_str(),sizeof(convitePend)); } } else conviteInit=true; }
  // Fase 90 -- aviso de seguranca do PWA (cmd.seg = "<ms>|<email>|novodisp|negado"): so memoriza; o push sai no pushServico()
  { String sg; if(extrairCampoString(json,"seg",sg)){ if(!segInit){ segInit=true; segUlt=sg; } else if(sg!=segUlt){ segUlt=sg; strlcpy(convitePend,sg.c_str(),sizeof(convitePend)); } } else segInit=true; }
  // Fase 98 -- tela Tampa do PWA (so memoriza; o trabalho roda em adxlComandosTick()/adxlPublicaTick() no loop)
  { String av; if(extrairCampoString(json,"adxlVivo",av)){ if(!adxlVivoInit){ adxlVivoInit=true; adxlVivoUlt=av; } else if(av!=adxlVivoUlt){ adxlVivoUlt=av; adxlVivoMs=millis(); } } else adxlVivoInit=true; }
  { String ac; if(extrairCampoString(json,"adxlCfg",ac)){ if(!adxlCfgInit){ adxlCfgInit=true; adxlCfgUlt=ac; } else if(ac!=adxlCfgUlt){ adxlCfgUlt=ac; strlcpy(adxlCfgPend,ac.c_str(),sizeof(adxlCfgPend)); } } else adxlCfgInit=true; }
  // Fase 104 -- ajustes de Tela e Fans do PWA (so memoriza; o trabalho roda em telaComandosTick() no loop)
  { String ts; if(extrairCampoString(json,"telaSet",ts)){ if(!telaSetInit){ telaSetInit=true; telaSetUlt=ts; } else if(ts!=telaSetUlt){ telaSetUlt=ts; strlcpy(telaSetPend,ts.c_str(),sizeof(telaSetPend)); } } else telaSetInit=true; }
  { String fs; if(extrairCampoString(json,"fansSet",fs)){ if(!fansSetInit){ fansSetInit=true; fansSetUlt=fs; } else if(fs!=fansSetUlt){ fansSetUlt=fs; strlcpy(fansSetPend,fs.c_str(),sizeof(fansSetPend)); } } else fansSetInit=true; }
  { String fg; if(extrairCampoString(json,"fansRegras",fg)){ if(!fansRegInit){ fansRegInit=true; fansRegUlt=fg; } else if(fg!=fansRegUlt){ fansRegUlt=fg; strlcpy(fansRegPend,fg.c_str(),sizeof(fansRegPend)); } } else fansRegInit=true; }   // Fase 112
  { String fv; if(extrairCampoString(json,"fansVivo",fv)){ if(!fansVivoInit){ fansVivoInit=true; fansVivoUlt=fv; } else if(fv!=fansVivoUlt){ fansVivoUlt=fv; fansVivoMs=millis(); } } else fansVivoInit=true; }   // Fase 105: PWA com o cartao Fans visivel
  { String ak; if(extrairCampoString(json,"adxlCal",ak)){ if(!adxlCalInit){ adxlCalInit=true; adxlCalUlt=ak; } else if(ak!=adxlCalUlt){ adxlCalUlt=ak; strlcpy(adxlCalPend,ak.c_str(),sizeof(adxlCalPend)); } } else adxlCalInit=true; }
  // Fase 49 -- pedido de notificacao de teste vindo do PWA
  String pt;
  pushTesteRecebido(pt, extrairCampoString(json,"pushTeste",pt));

  if(!changed) return;
  if(appState==S_ATUADORES) drawAtuadoresScreen();
  else if(appState==S_MAIN) drawMainScreen();
}

// Instante em que o GET em andamento foi disparado (setado em
// pollActuatorsFromFirestore, lido aqui quando a resposta chega — pode ser
// bem depois, é assíncrono). Ver applyActuatorsFromFirestore().
unsigned long fbPollIssuedAtMs = 0;

void fbGetCallback(AsyncResult &aResult){
  if(!aResult.isResult()) return;
  if(aResult.isError()){
    Serial.printf("[Firebase Get ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
    return;
  }
  if(!aResult.available()) return;
  applyActuatorsFromFirestore(aResult.c_str(), fbPollIssuedAtMs);
}

// Grava so o campo do canal que mudou (patch com updateMask), igual
// alternarAtuador() no app.js — nao mexe nos outros 7 canais nem em leituras/nome/etc.
void pushActuatorToFirestore(int ch, bool on){
  if(!fbStarted || !fbApp.ready()) return;
  Values::MapValue mapV("ch" + String(ch), Values::BooleanValue(on));
  Document<Values::Value> doc("atuadores", Values::Value(mapV));
  PatchDocumentOptions patchOptions(DocumentMask("atuadores.ch" + String(ch)), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbPatchTask");
}

// Usada pelo toque na tela de Atuadores: aplica no rele local E propaga pra nuvem.
// Marca lastLocalTouchMs[ch] ANTES de tudo -- é essa marca que faz um poll
// já em andamento (disparado antes deste toque) ser ignorado quando a
// resposta dele chegar (ver applyActuatorsFromFirestore).
void setRelayAndSync(int ch, bool on){
  lastLocalTouchMs[ch] = millis();
  setRelay(ch, on);
  if(ch!=COOLER_CH) manualHold(ch, on);   // Fase 106
  if(ch!=2) pushActuatorToFirestore(ch, on);   // Fase 87: o estado da bomba de agua nunca vai para a nuvem (evita a nuvem religar a bomba)
  // Fase 30 -- importante=true (era false/so-SD): toggles de atuador sao
  // raros o bastante (acao humana, nao automatica) pra nao pesar no
  // Firestore, e o pedido explicito era ver esses eventos no historico do
  // PWA, nao so no SD local.
  char msg[40]; snprintf(msg,40,"%s -> %s",RELAY_NAMES[ch],on?"ON":"OFF");
  logEvento("Atuador",msg,true);
}

// Fase 35 — histerese do Cooler Sistema, pedido explícito: "liga se Temp
// >= Máxima, desliga se Temp <= Mínima". Chamada do loop() a cada 5s,
// junto com atualizarEspTempStats().
//
// IMPORTANTE -- por que os limiares são #define fixos (ESP_TEMP_COOLER_ON/
// OFF) e NÃO vêm de espTempMin/espTempMax (o Min/Méd/Max ao vivo mostrado
// na tela Sistema): usar o Min/Max da sessão criaria um problema
// autorreferente. O "Min" é capturado ANTES do cooler agir; assim que ele
// liga e começa a esfriar o ambiente, alcançar de novo aquele Min antigo
// fica cada vez mais raro (o próprio cooler já está "roubando" a queda de
// temperatura que faria o Min cair mais ainda) -- na prática o cooler
// nunca desligaria sozinho. Foi exatamente o sintoma relatado pelo
// usuário ("Mn38 Md44 Mx44, cooler ligado há um tempo, nunca alcançou
// mn38 de novo"). Limiares fixos, independentes da própria ação do
// cooler, evitam esse loop de vez.
//
// Zona morta entre OFF e ON: mantém o que já estava (isso É a histerese
// -- sem ela, oscilar bem em cima de um único limiar liga/desliga o
// cooler repetidamente, desgastando o relé à toa).
// Fase 83 -- a bomba de ar (CH4, 3,7 V) esquenta e fica fisicamente perto do ESP32: enquanto ela estiver ligada (e por 2 min depois de
// desligar) o Cooler Sistema liga junto, independente da temperatura do chip. bombaArOffMs e atualizado em bombaArTick() (1 Hz).
static unsigned long bombaArOffMs=0;
static bool coolerPorBomba(){ return relayState[3] || (bombaArOffMs && millis()-bombaArOffMs<120000UL); }
void controlarCoolerPorHisterese(){
  bool porBomba=coolerPorBomba();
  if(isnan(espTempAtual) && !porBomba) return;
  bool estadoAtual = relayState[COOLER_CH];
  bool deveLigar = estadoAtual;
  if(!isnan(espTempAtual)){
    if(espTempAtual >= ESP_TEMP_COOLER_ON) deveLigar = true;
    else if(espTempAtual <= ESP_TEMP_COOLER_OFF) deveLigar = false;
  }
  if(porBomba) deveLigar = true;   // Fase 83

  if(deveLigar == estadoAtual) return;  // nada a fazer

  lastLocalTouchMs[COOLER_CH] = millis();
  setRelay(COOLER_CH, deveLigar);
  pushActuatorToFirestore(COOLER_CH, deveLigar);
  char msg[80]; if(porBomba && deveLigar) snprintf(msg,sizeof(msg),"Cooler Sistema -> ON (bomba de ar ligada)"); else snprintf(msg,sizeof(msg),"Cooler Sistema -> %s (automatico, %.1fC)", deveLigar?"ON":"OFF", espTempAtual);
  logEvento("Sistema", msg, true);
  if(deveLigar) cicloContaCooler();   // Fase 48
}

// Diagnostico (2026-09-20): millis() da ultima vez que um heartbeat foi
// CONFIRMADO com sucesso pelo servidor -- usado em diagnosticoStatusPeriodico()
// mais abaixo, pra mostrar "ha quanto tempo" numa unica linha de status.
unsigned long lastHeartbeatOkMs = 0;

// Callback dedicado (nao o fbPatchCallback generico) so pra ter visibilidade
// clara no Serial se o heartbeat especificamente esta sendo confirmado pelo
// servidor ou falhando -- sem isso nao dava pra distinguir "heartbeat ok" de
// "heartbeat falhando silenciosamente" so olhando o log.
// Fase 30 — "Erro" no historico de Logs quando o heartbeat (sinal de
// saude da sincronizacao com a nuvem) comeca/para de falhar. Por BORDA
// (so na transicao), igual ao padrao ja usado em WiFi caiu/restabelecida
// -- o heartbeat roda a cada 30s, logar toda falha viraria ruido se a
// rede ficar instavel por um tempo.
bool heartbeatFalhandoAnterior = false;
void fbHeartbeatCallback(AsyncResult &aResult){
  if(aResult.isError()){
    Serial.printf("[Firebase Heartbeat ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
    if(!heartbeatFalhandoAnterior){
      heartbeatFalhandoAnterior = true;
      char msg[80]; snprintf(msg,80,"Heartbeat falhou: %s", aResult.error().message().c_str());
      logEvento("Erro", msg, true);
    }
    return;
  }
  if(aResult.isResult()){
    if(heartbeatFalhandoAnterior){
      heartbeatFalhandoAnterior = false;
      logEvento("Erro","Heartbeat voltou a funcionar",true);
    }
    lastHeartbeatOkMs = millis();
    Serial.println("[Firebase] Heartbeat confirmado pelo servidor");
  }
}

// 2024-01-01T00:00:00Z em epoch -- se o relogio do sistema estiver ANTES
// disso, o NTP nunca sincronizou de verdade (ficou no epoch padrao,
// 1970). Ver ntpWatchdog() no loop(): sem essa checagem, um NTP que falha
// uma vez no boot (ex: DNS lento) trava o heartbeat com timestamp de 1970
// PRA SEMPRE (parece "sempre antigo" pro PWA, mesmo o dispositivo estando
// online e enviando heartbeats normalmente) -- exatamente o tipo de bug
// que faz o PWA mostrar "offline" quando na verdade nao esta.
#define EPOCH_2024_01_01 1704067200UL

// Heartbeat — grava a hora atual (UTC, formato RFC3339) num campo separado do
// documento, a cada FB_HEARTBEAT_INTERVAL_MS (chamado do loop()). O PWA le
// esse campo e considera a chocadeira "offline" se ele ficar velho demais —
// sem isso o indicador de status so refletia a rede do PROPRIO navegador,
// nao se a placa estava ligada de verdade.
void pushHeartbeatToFirestore(){
  // Diagnostico (2026-09-20): loga TODA tentativa, mesmo quando pulada --
  // sem isso, silencio no Serial nao distinguia "nao tentou" de "tentou e
  // nao apareceu resposta ainda". Cruzar com fbHeartbeatCallback (sucesso/
  // erro) e com diagnosticoStatusPeriodico() (WiFi/Firebase) revela se a
  // chocadeira esta offline de verdade ou so o PWA que esta enganado.
  if(!fbStarted || !fbApp.ready()){
    Serial.printf("[Heartbeat] PULADO -- fbStarted=%s fbApp.ready()=%s\n", fbStarted?"sim":"nao", (fbStarted&&fbApp.ready())?"sim":"nao");
    return;
  }
  time_t now = time(nullptr);
  if(now < (time_t)EPOCH_2024_01_01){
    Serial.println("[Firebase] Heartbeat NAO enviado -- relogio do sistema ainda invalido (NTP nao sincronizou), ver ntpWatchdog()");
    return;
  }
  struct tm *utc = gmtime(&now);
  char ts[25];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  Serial.printf("[Heartbeat] Enviando... ts=%s\n", ts);

  Document<Values::Value> doc("firmwareLastSeen", Values::Value(Values::TimestampValue(String(ts))));
  String hbMask="firmwareLastSeen";
  if(!isnan(temperaturaAtual)&&!isnan(umidadeAtual)){   // Fase 63: leitura ao vivo vai junto do heartbeat (1 escrita em vez de 2; o app le do proprio documento)
    doc.add("tempAtual", Values::Value(Values::DoubleValue(number_t(temperaturaAtual, 2))));
    doc.add("umidAtual", Values::Value(Values::DoubleValue(number_t(umidadeAtual, 1))));
    hbMask+=",tempAtual,umidAtual";
  }
  if(!isnan(co2Atual) && lastCo2OkMs && millis()-lastCo2OkMs<180000UL){   // Fase 99: CO2 (MH-Z19B) vai junto do heartbeat; o PWA mostra no card CO2
    doc.add("co2Atual", Values::Value(Values::DoubleValue(number_t(co2Atual, 0))));
    hbMask+=",co2Atual";
  }
  { String pn=painelCfgStr(); doc.add("painel", Values::Value(Values::StringValue(pn))); hbMask+=",painel"; }   // Fase 104: estado de Tela/Fans para o PWA
  String ambStr=ambEstado==1?(String("aguardando;")+ambEstavelSeg+";"+(unsigned)(AMB_ESTAVEL_MS/1000UL)):ambEstado==2?(fbCicloPreparo?String("pronto"):String("")):ambEstado==5?(String("tampa;1;")+tampaSeg):ambEstado==6?(String("tampa;2;")+tampaSeg):ambEstado==3?(String("manter;1;")+ultTipoNome):ambEstado==4?(String("manter;0;")+ultTipoNome):((!fbCicloAtivo&&ultTipoNome[0]&&ultTipoValido())?(String("parado;")+ultTipoNome):String(""));   // Fase 69/71/77 (fora do preparo o PWA mostra OK/OFF)
  if(ambDiagTxt[0]){ if(ambStr.length()==0) ambStr=";;"; else if(ambStr.indexOf(';')<0) ambStr+=";;"; ambStr+=";"; ambStr+=ambDiagTxt; ambStr.replace('|',' '); }   // Fase 80: 4o campo = diagnostico (texto completo)
  if(adxlAutoAtivo()){ int sc=0; for(unsigned i=0;i<ambStr.length();i++) if(ambStr[i]==';') sc++; while(sc<3){ ambStr+=';'; sc++; } ambStr+=";A"; }   // Fase 95: ultimo campo "A" = tampa automatica (sensor ADXL)
  doc.add("ambiente", Values::Value(Values::StringValue(ambStr)));
  hbMask+=",ambiente";
  DocumentMask hbDm(hbMask);
  PatchDocumentOptions patchOptions(hbDm, DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbHeartbeatCallback, "fbHeartbeatTask");
}

// Diagnostico (2026-09-20): uma linha de status a cada 10s no Serial,
// pra ver de relance se o WiFi esta de pe, o RSSI, se o Firebase esta
// "pronto" (fbApp.ready()) e ha quanto tempo o ultimo heartbeat foi
// CONFIRMADO com sucesso -- tudo num lugar só, sem precisar juntar linhas
// espalhadas de log.
void diagnosticoStatusPeriodico(){
  bool wifiUp = (WiFi.status()==WL_CONNECTED);
  Serial.printf("[DIAG] WiFi=%s RSSI=%ddBm IP=%s | fbApp.ready()=%s | ultimo heartbeat OK ha %lus | heap=%u | uptime=%lus\n",
    wifiUp ? "OK" : "CAIU",
    wifiUp ? WiFi.RSSI() : 0,
    wifiUp ? WiFi.localIP().toString().c_str() : "-",
    fbApp.ready() ? "sim" : "nao",
    lastHeartbeatOkMs ? (millis()-lastHeartbeatOkMs)/1000 : 999999,
    ESP.getFreeHeap(),
    (millis()-uptimeStart)/1000);
}

// ═══════════════════════════════════════════════════════════════
// SHT31 — TEMPERATURA/UMIDADE (Fase 20)
// Leitura local via I2C (sem rede, roda mesmo offline) a cada
// SHT31_READ_INTERVAL_MS; o valor fica em temperaturaAtual/umidadeAtual
// pra UI (drawTempUmidCards) e pra push periodico no Firestore
// (pushLeituraToFirestore). So loga no Serial em TRANSICOES de estado
// (ok->falha, falha->ok) pra nao poluir o log a cada 5s -- diferente do
// heartbeat (30s, log todo envio), aqui o intervalo e curto o bastante
// pra virar ruido se logasse sempre.
// ═══════════════════════════════════════════════════════════════
#define SHT31_READ_INTERVAL_MS 5000
bool sht31LeituraOkAnterior = true;  // true no boot pra so avisar quando FALHAR pela primeira vez

// Fase 22 — reseta estatisticas/historico do dia quando o dia do mes vira
// (comparado via RTC). Sem RTC (rtcOK=false), nunca reseta sozinho --
// vira "desde o boot" mesmo, e isso e dito honestamente na tela.
void resetarEstatisticasSensorSeVirouODia(){
  if(!rtcOK) return;
  int diaAtual = rtc.now().day();
  if(diaEstatisticasAtual == -1){ diaEstatisticasAtual = diaAtual; return; }  // 1a chamada, so marca
  if(diaAtual != diaEstatisticasAtual){
    diaEstatisticasAtual = diaAtual;
    temperaturaMin=NAN; temperaturaMax=NAN; temperaturaSoma=0; temperaturaN=0;
    umidadeMin=NAN; umidadeMax=NAN; umidadeSoma=0; umidadeN=0;
    sensorHistCount=0;
    co2Min=NAN; co2Max=NAN; co2Soma=0; co2N=0; co2HistCount=0;   // Fase 115
    Serial.println("[Sensor] Meia-noite -- estatisticas e historico do dia resetados.");
    logEvento("Sensor","Estatisticas diarias resetadas (meia-noite)",false);
  }
}

// Fase 30 — alerta quando temperatura/umidade sai da faixa ideal
// (TEMP_IDEAL_MIN/MAX, UMID_IDEAL_MIN/MAX -- configuraveis em Ajustes
// Finos). Registrado por BORDA (só no instante em que entra ou sai do
// estado de alerta), não a cada leitura -- senão viraria ruído a cada 5s
// enquanto o valor estiver fora. Chamado de dentro de lerSht31() depois
// de cada leitura valida.
bool tempForaDaFaixa = false;
bool umidForaDaFaixa = false;
// Fase 49 -- debounce + histerese: o alerta so DISPARA depois de 60s seguidos fora da faixa e so
// NORMALIZA depois de 60s seguidos bem dentro (com folga de 0,15C / 1,5%). Sem isso, um valor
// oscilando em cima do limite (ex.: 37,2C) gerava alerta/normalizado a cada 10s (log e push em rajada).
#define ALERTA_ESPERA_MS  180000UL   // Fase 81: 3 min (era 1): oscilacao curta em torno do limite nao vira alerta
#define ALERTA_HIST_TEMP  0.20f
#define ALERTA_HIST_UMID  1.5f
static bool alertaTransicao(bool& estado,unsigned long& desde,bool disparo,bool bemDentro){
  unsigned long agora=millis();
  bool cond = estado ? bemDentro : disparo;    // condicao pra MUDAR de estado
  if(!cond){ desde=0; return false; }
  if(!desde) desde=agora?agora:1;
  if(agora-desde<ALERTA_ESPERA_MS) return false;
  desde=0; estado=!estado; return true;
}
void checarAlertasSensor(float t, float h){
  static unsigned long tDesde=0, hDesde=0;
  // Fase 54: alertas seguem os parametros do TIPO DE OVO do ciclo ativo. Sem ciclo (ou tipo desconhecido) nao ha alerta:
  // a faixa geral so serve pra colorir os cards, nunca pra avisar (antes avisava com os numeros da galinha mesmo com outro tipo).
  if(debugCicloFalso || !cicloComTipoValido()){ tempForaDaFaixa=false; umidForaDaFaixa=false; tDesde=0; hDesde=0; return; }
  if(tampaEst!=0) return;   // Fase 82: tampa aberta/recuperando nao gera alerta
  bool tFora=(t < TEMP_IDEAL_MIN || t > TEMP_IDEAL_MAX);
  bool tDentro=(t >= TEMP_IDEAL_MIN+ALERTA_HIST_TEMP && t <= TEMP_IDEAL_MAX-ALERTA_HIST_TEMP);
  if(alertaTransicao(tempForaDaFaixa,tDesde,tFora,tDentro)){
    char msg[72];
    if(tempForaDaFaixa) snprintf(msg,72,"Temperatura fora da faixa: %.1fC (ideal %.1f-%.1fC)", t, TEMP_IDEAL_MIN, TEMP_IDEAL_MAX);
    else snprintf(msg,72,"Temperatura normalizada: %.1fC", t);
    logEvento("Alerta", msg, true);
    if(tempForaDaFaixa){ cicloContaAlerta(true); pushEnfileirar(0,"ChocaIoT: temperatura fora da faixa",msg); }
    else pushEnfileirar(4,"ChocaIoT: temperatura normalizada",msg);
  }
  bool hFora=(h < UMID_IDEAL_MIN || h > UMID_IDEAL_MAX);
  bool hDentro=(h >= UMID_IDEAL_MIN+ALERTA_HIST_UMID && h <= UMID_IDEAL_MAX-ALERTA_HIST_UMID);
  if(alertaTransicao(umidForaDaFaixa,hDesde,hFora,hDentro)){
    char msg[72];
    if(umidForaDaFaixa) snprintf(msg,72,"Umidade fora da faixa: %.1f%% (ideal %.1f-%.1f%%)", h, UMID_IDEAL_MIN, UMID_IDEAL_MAX);
    else snprintf(msg,72,"Umidade normalizada: %.1f%%", h);
    logEvento("Alerta", msg, true);
    if(umidForaDaFaixa){ cicloContaAlerta(false); pushEnfileirar(1,"ChocaIoT: umidade fora da faixa",msg); }
    else pushEnfileirar(5,"ChocaIoT: umidade normalizada",msg);
  }
}

// Fase 30 — registro periodico da leitura (5min, independente do push
// pro Firestore de 60s em pushLeituraToFirestore -- aquele e pra
// popular o grafico/card em tempo real, este e' o "log" pedido
// explicitamente pro historico). Chamado do loop(), nao daqui.
#define SENSOR_LOG_INTERVAL_MS (30UL*60UL*1000UL)
void logLeituraSensorPeriodica(){
  if(isnan(temperaturaAtual) || isnan(umidadeAtual)) return;
  char msg[48];
  snprintf(msg,48,"Temp %.1fC / Umid %.1f%%", temperaturaAtual, umidadeAtual);
  logEvento("Sensor", msg, true);
}

// ═══════════════════════════════════════════════════════════════
// Fase 91/93/95 -- ADXL345 (acelerometro 3 eixos, I2C 0x53: o SDO ja vai ao GND por um resistor de 0 ohm na placa) = SENSOR DA TAMPA.
// O modulo fica em cima da tampa (peso) e e TIRADO dali para abrir a caixa. O acelerometro sozinho NAO diferencia "plano na tampa" de
// "plano na mesa" (a gravidade e a mesma), entao a decisao combina tres evidencias:
//   1) MOVIMENTO: variacao entre leituras acima da sensibilidade por >= 0,3 s  -> a tampa foi aberta (aviso na hora);
//   2) POSICAO: angulo em relacao a posicao calibrada FECHADA (tolerancia) -> e, se houver, a posicao calibrada FORA (mesa);
//   3) VIBRACAO: o modulo sobre a caixa em funcionamento (fans, bomba, rele) treme; na mesa, nao. A vibracao de cada posicao e
//      gravada na calibracao (mg) e vira a "assinatura" que separa tampa x mesa.
// FECHADA so volta a ser declarada (evento + tampaCmd=2) quando a assinatura e CONFIAVEL (vibracao distinguivel, ou posicao FORA
// calibrada e separada >= 1,5 graus). Sem isso o aparelho nao "inventa" que fechou: o tampaTick fecha pela temperatura voltando a subir.
// Estados: 1 sem calibracao · 2 FECHADA · 3 MOVENDO · 4 ABERTA. Uma abertura sem queda de T/U em 4 min e descartada (alarme falso).
// Teclas seriais (opcionais): A imprime · K calibra FECHADA · I reinicia · L ao vivo. Tela: Configuracoes > Tampa.
// ═══════════════════════════════════════════════════════════════
static bool adxlOK=false; static uint8_t adxlAddr=0x53; static uint8_t adxlFalhas=0;
static float adxlX=0,adxlY=0,adxlZ=0;                                   // g, filtrado
static float adxlRefX=0,adxlRefY=0,adxlRefZ=1; static bool adxlTemRef=false; static float adxlVibRef=0;   // FECHADA (na tampa)
static float adxlAwX=0,adxlAwY=0,adxlAwZ=1;     static bool adxlTemAw=false;  static float adxlVibAw=0;    // FORA (mesa etc.)
static unsigned long adxlUltMs=0; static bool adxlLive=false; static unsigned long adxlLiveMs=0;
static bool adxlDet=false; static uint8_t adxlTolDeg=5; static uint16_t adxlMovMg=60;                      // ajustes (/adxl.cfg)
static uint8_t adxlEst=0;            // 0 sem sensor · 1 sem calibracao · 2 fechada · 3 movendo · 4 aberta
static float adxlMov=0, adxlVib=0;   // g de movimento · mg de vibracao (depois de tirar o baseline lento)
static float adxlPx=0,adxlPy=0,adxlPz=0, adxlSlX=0,adxlSlY=0,adxlSlZ=0; static bool adxlTemPrev=false;
static unsigned long adxlEstDesde=0, adxlParadoDesde=0; static uint8_t adxlMovSeq=0; static bool adxlAbertoNotif=false;
static bool adxlAngOk=false, adxlSigOk=true, adxlDentro=false, adxlConfiavel=false;                        // avaliacao atual
static uint16_t adxlIguais=0; static int16_t adxlR0=0,adxlR1=0,adxlR2=0;                                   // leitura congelada?
static unsigned long adxlReafirmaMs=0, adxlUltEvLogMs=0;
static char adxlMsg[28]=""; static unsigned long adxlMsgAte=0; static uint16_t adxlMsgSeq=0;   // adxlMsgSeq: o PWA mostra cada mensagem nova uma vez
static bool adxlLeReg(uint8_t reg,uint8_t* b,uint8_t n){
  Wire.beginTransmission(adxlAddr); Wire.write(reg); if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom((int)adxlAddr,(int)n)!=(int)n) return false;
  for(uint8_t i=0;i<n;i++) b[i]=Wire.read();
  return true;
}
static bool adxlEscReg(uint8_t reg,uint8_t v){ Wire.beginTransmission(adxlAddr); Wire.write(reg); Wire.write(v); return Wire.endTransmission()==0; }
static void adxlConfigura(){
  adxlEscReg(0x2C,0x0A);   // BW_RATE: 100 Hz
  adxlEscReg(0x31,0x08);   // DATA_FORMAT: resolucao total, +-2 g (3,9 mg/LSB)
  adxlEscReg(0x2D,0x08);   // POWER_CTL: medicao
}
static void i2cScanLog(){
  char l[96]; int n=0; l[0]=0; strlcpy(l,"[I2C] dispositivos:",sizeof(l));
  for(uint8_t a=0x08;a<0x78;a++){ Wire.beginTransmission(a); if(Wire.endTransmission()==0){ char t[8]; snprintf(t,sizeof(t)," 0x%02X",a); strlcat(l,t,sizeof(l)); n++; } }
  if(!n) strlcat(l," nenhum (confira SDA/SCL e a alimentacao)",sizeof(l));
  Serial.println(l);
}
static void adxlCfgSalvar(){   // linha 1 = FECHADA (x,y,z,vib | "-") · linha 2 = det,tol,sens · linha 3 = FORA (x,y,z,vib | "-")
  File f=LittleFS.open("/adxl.cfg","w"); if(!f) return;
  if(adxlTemRef) f.printf("%.4f,%.4f,%.4f,%.1f\n",adxlRefX,adxlRefY,adxlRefZ,adxlVibRef); else f.print("-\n");
  f.printf("%d,%u,%u\n",adxlDet?1:0,(unsigned)adxlTolDeg,(unsigned)adxlMovMg);
  if(adxlTemAw) f.printf("%.4f,%.4f,%.4f,%.1f\n",adxlAwX,adxlAwY,adxlAwZ,adxlVibAw); else f.print("-\n");
  f.close();
}
static void adxlRefCarregar(){
  File f=LittleFS.open("/adxl.cfg","r"); if(!f) return; String l1=f.readStringUntil('\n'), l2=f.readStringUntil('\n'), l3=f.readStringUntil('\n'); f.close();
  float x,y,z,v=0; int n=sscanf(l1.c_str(),"%f,%f,%f,%f",&x,&y,&z,&v);
  if(n>=3){ adxlRefX=x; adxlRefY=y; adxlRefZ=z; adxlVibRef=(n>=4)?v:0; adxlTemRef=true; }
  int d; unsigned t,m; if(sscanf(l2.c_str(),"%d,%u,%u",&d,&t,&m)==3){ adxlDet=(d==1); if(t>=1&&t<=30) adxlTolDeg=t; if(m>=20&&m<=300) adxlMovMg=m; }
  v=0; n=sscanf(l3.c_str(),"%f,%f,%f,%f",&x,&y,&z,&v);
  if(n>=3){ adxlAwX=x; adxlAwY=y; adxlAwZ=z; adxlVibAw=(n>=4)?v:0; adxlTemAw=true; }
}
static float adxlAng2(float ax,float ay,float az,float bx,float by,float bz){   // graus entre dois vetores
  float n1=sqrtf(ax*ax+ay*ay+az*az), n2=sqrtf(bx*bx+by*by+bz*bz);
  if(n1<0.2f||n2<0.2f) return 0;
  float c=(ax*bx+ay*by+az*bz)/(n1*n2); if(c>1) c=1; if(c<-1) c=-1;
  return acosf(c)*57.2958f;
}
static float adxlAngulo(){ return adxlAng2(adxlX,adxlY,adxlZ,adxlRefX,adxlRefY,adxlRefZ); }   // 0 = igual a FECHADA
static void adxlAvalia(){   // posicao + assinatura de vibracao: "parece FECHADA?" e "isso e confiavel?"
  float tc=adxlAngulo();
  adxlAngOk=(tc<=(float)adxlTolDeg);
  float sg=(float)adxlTolDeg/2.0f; if(sg<1.0f) sg=1.0f;
  bool vibUtil=(adxlVibRef>=12.0f) || (adxlTemAw && fabsf(logf((adxlVibRef+3.0f)/(adxlVibAw+3.0f)))>=0.5f);   // 12 mg ~ 2x o ruido do chip
  float lc=logf((adxlVib+3.0f)/(adxlVibRef+3.0f));
  float dc2=(tc/sg)*(tc/sg)+(vibUtil?(lc/0.45f)*(lc/0.45f):0.0f);
  if(adxlTemAw){
    float ta=adxlAng2(adxlX,adxlY,adxlZ,adxlAwX,adxlAwY,adxlAwZ), la=logf((adxlVib+3.0f)/(adxlVibAw+3.0f));
    float da2=(ta/sg)*(ta/sg)+(vibUtil?(la/0.45f)*(la/0.45f):0.0f);
    adxlSigOk=(dc2<=da2);                        // mais parecido com FECHADA do que com FORA
    float sep=adxlAng2(adxlRefX,adxlRefY,adxlRefZ,adxlAwX,adxlAwY,adxlAwZ);
    adxlConfiavel=(sep>=1.5f) || vibUtil;       // as duas posicoes se distinguem por angulo ou por vibracao
  }else{
    adxlSigOk=(!vibUtil) || fabsf(lc)<=0.9f;    // sem FORA calibrada: vibracao < 40 % ou > 2,5x da calibrada nao e a tampa
    adxlConfiavel=vibUtil;
  }
  adxlDentro=adxlAngOk && adxlSigOk;
}
static void adxlInit(){
  i2cScanLog(); adxlOK=false; adxlFalhas=0; adxlEst=0; adxlTemPrev=false; adxlMov=0; adxlVib=0; adxlMovSeq=0; adxlParadoDesde=0; adxlAbertoNotif=false; adxlIguais=0;
  const uint8_t cand[2]={0x53,0x1D};   // 0x1D = SDO em nivel alto
  for(int i=0;i<2 && !adxlOK;i++){
    adxlAddr=cand[i]; uint8_t id=0;
    if(adxlLeReg(0x00,&id,1) && id==0xE5){ adxlConfigura(); adxlOK=true; }
  }
  if(adxlOK){ adxlRefCarregar(); adxlEst=adxlTemRef?2:1; adxlEstDesde=millis(); Serial.printf("[ADXL345] OK (I2C 0x%02X)%s%s%s.\n",adxlAddr,adxlTemRef?", FECHADA calibrada":", sem calibracao (Configuracoes > Tampa)",adxlTemAw?", FORA calibrada":"",adxlDet?", deteccao LIGADA":""); }
  else Serial.println("[ADXL345] NAO encontrado (0x53/0x1D): confira 5V, GND, SDA->GPIO8, SCL->GPIO9 e o jumper CS->3V3 (LED D1 deve estar aceso).");
}
static const char* adxlSaude(){   // o rotulo do topo da tela: "sensor ok" so quando a leitura esta viva e plausivel
  if(!adxlOK) return "sem sensor";
  if(adxlIguais>=100) return "leitura travada";
  float m=sqrtf(adxlX*adxlX+adxlY*adxlY+adxlZ*adxlZ);
  if(adxlMov*1000.0f<adxlMovMg*0.5f && (m<0.75f||m>1.25f)) return "|a| fora de 1 g";
  return "sensor ok";
}
bool adxlAutoAtivo(){ return adxlDet && adxlTemRef && adxlOK && adxlIguais<100; }
bool adxlAssentado(){ return adxlOK && adxlTemRef && adxlEst!=3 && adxlDentro && adxlParadoDesde && millis()-adxlParadoDesde>=8000UL; }   // Fase 97: parado ha 8 s na posicao da tampa
void adxlTampaCancelada(){ if(adxlEst>=2){ adxlEst=2; adxlEstDesde=millis(); } adxlAbertoNotif=false; }   // Fase 97: o tampaTick descartou a abertura (alarme falso)
int adxlSettingsDot(){ return !adxlOK?0:(adxlEst==3||adxlEst==4||strcmp(adxlSaude(),"sensor ok")!=0)?2:1; }
static void adxlEvento(bool abriu){
  adxlAbertoNotif=abriu;
  Serial.printf("[ADXL345] %s%s\n",abriu?"modulo retirado/movido: tampa ABERTA":"modulo de volta na posicao FECHADA (assinatura confiavel)",adxlDet?"":" (deteccao desligada: so informativo)");
  if(!adxlDet) return;
  bool aplica=false;
  if(abriu && tampaEst==0){ tampaCmd=1; tampaOrigemAdxl=true; aplica=true; }
  else if(!abriu && tampaEst==1){ tampaCmd=2; tampaOrigemAdxl=true; aplica=true; }
  if(!aplica && millis()-adxlUltEvLogMs>=20000UL){   // fica registrado no PWA mesmo quando o tampaTick nao vai agir
    adxlUltEvLogMs=millis();
    char m[110]; snprintf(m,sizeof(m),"Sensor ADXL: %s -- sem efeito (tampa %s%s)",abriu?"modulo retirado/movido":"modulo de volta",tampaEst==0?"ja fechada":tampaEst==1?"ja aberta":"recuperando",((fbCicloAtivo&&!fbCicloPreparo)||ambManterOn())?"":", sem ciclo em andamento");
    logEvento("Sensor",m,true);
  }
}
static unsigned long adxlMovIniMs=0; static uint16_t adxlIgnorados=0;   // inicio do episodio de movimento · trancos curtos descartados
static void adxlMaquina(float gx,float gy,float gz,float mx,float my,float mz,unsigned long agora){
  // gx..gz = leitura bruta (vibracao) · mx..mz = mediana de 5 (movimento e posicao: sem os "picos" de 1-2 amostras do clique do rele / EMI)
  if(adxlTemPrev){
    float dx=mx-adxlPx,dy=my-adxlPy,dz=mz-adxlPz; adxlMov=0.6f*adxlMov+0.4f*sqrtf(dx*dx+dy*dy+dz*dz);
    float hx=gx-adxlSlX,hy=gy-adxlSlY,hz=gz-adxlSlZ; adxlVib=0.9f*adxlVib+0.1f*(sqrtf(hx*hx+hy*hy+hz*hz)*1000.0f);   // vibracao em mg
    adxlSlX+=0.25f*(mx-adxlSlX); adxlSlY+=0.25f*(my-adxlSlY); adxlSlZ+=0.25f*(mz-adxlSlZ);
  }else{ adxlSlX=mx; adxlSlY=my; adxlSlZ=mz; }
  adxlPx=mx; adxlPy=my; adxlPz=mz; adxlTemPrev=true;
  if(!adxlTemRef){ adxlEst=1; return; }
  adxlAvalia();
  bool mov=(adxlMov>adxlMovMg/1000.0f);
  if(mov){ if(adxlMovSeq<250) adxlMovSeq++; adxlParadoDesde=0; } else { adxlMovSeq=0; if(!adxlParadoDesde) adxlParadoDesde=agora; }
  unsigned long parado=adxlParadoDesde?agora-adxlParadoDesde:0;
  if(adxlEst<2){ adxlEst=adxlDentro?2:4; adxlEstDesde=agora; return; }   // primeira avaliacao: sem avisar
  if(adxlEst==2){
    if(agora-adxlEstDesde>=5000UL){   // 5 s de carencia depois de fechar/calibrar/ligar
      if(adxlMovSeq>=3){ adxlEst=3; adxlEstDesde=agora; adxlMovIniMs=agora; }   // comeca um EPISODIO de movimento; o aviso so sai depois (abaixo)
      else if(!mov && parado>=4000UL && (!adxlAngOk || (adxlTemAw && !adxlSigOk))){ adxlEst=4; adxlEstDesde=agora; adxlEvento(true); }   // parado fora do lugar
    }
  }else if(adxlEst==3){
    unsigned long dur=agora-adxlMovIniMs;
    if(!adxlAbertoNotif && dur>=(adxlConfiavel?3000UL:15000UL)) adxlEvento(true);   // Fase 96/97: retirar o modulo leva >= 3 s; um tranco dura menos. Sem assinatura confiavel (tampa ~ mesa) exige 15 s
    if(!mov && parado>=1500UL){
      if(!adxlAbertoNotif){
        if(adxlDentro){   // episodio curto que terminou no lugar da tampa: nao abriu nada
          adxlEst=2; adxlEstDesde=agora; adxlIgnorados++;
          Serial.printf("[ADXL345] tranco de %lu ms ignorado (o modulo continua na posicao da tampa) -- total %u\n",dur,(unsigned)adxlIgnorados);
          return;
        }
        adxlEvento(true);   // pousou fora do lugar antes dos 3 s
      }
      if(adxlDentro && adxlConfiavel){ adxlEst=2; adxlEstDesde=agora; adxlEvento(false); }
      else { adxlEst=4; adxlEstDesde=agora; }   // pousado: so "fecha" com assinatura confiavel (ou quando a temperatura confirmar)
    }
  }else{   // 4 = ABERTA
    if(adxlMovSeq>=3){ adxlEst=3; adxlEstDesde=agora; adxlMovIniMs=agora; }
    else if(!mov && parado>=4000UL && adxlDentro && adxlConfiavel){ adxlEst=2; adxlEstDesde=agora; if(adxlAbertoNotif) adxlEvento(false); }
    else if(!mov && parado>=10000UL && adxlDentro && tampaEst==0 && agora-adxlEstDesde>=30000UL){   // a caixa ja esta normal (temperatura): adota FECHADA, sem evento
      adxlEst=2; adxlEstDesde=agora; adxlAbertoNotif=false;
    }
  }
}
static float adxlMed5(const float* v){   // mediana de 5
  float a[5]; for(int k=0;k<5;k++) a[k]=v[k];
  for(int k=1;k<5;k++){ float x=a[k]; int m=k-1; while(m>=0 && a[m]>x){ a[m+1]=a[m]; m--; } a[m+1]=x; }
  return a[2];
}
static void adxlTick(){   // chamado a cada volta do loop(); le a cada 100 ms
  if(!adxlOK){ adxlEst=0; return; }
  unsigned long agora=millis(); if(agora-adxlUltMs<100) return; adxlUltMs=agora;
  if(agora-adxlReafirmaMs>=10000UL || adxlIguais==100){ adxlReafirmaMs=agora; adxlConfigura(); }   // reafirma o modo de medicao (o chip pode ter sido resetado pela alimentacao)
  uint8_t b[6];
  if(!adxlLeReg(0x32,b,6)){ if(++adxlFalhas>=20){ adxlOK=false; adxlEst=0; Serial.println("[ADXL345] perdeu a comunicacao (20 falhas seguidas): confira cabo/contatos. Tecla I tenta de novo."); } return; }
  adxlFalhas=0;
  int16_t r0=(int16_t)((b[1]<<8)|b[0]), r1=(int16_t)((b[3]<<8)|b[2]), r2=(int16_t)((b[5]<<8)|b[4]);
  if(r0==adxlR0 && r1==adxlR1 && r2==adxlR2){ if(adxlIguais<60000) adxlIguais++; } else { adxlIguais=0; adxlR0=r0; adxlR1=r1; adxlR2=r2; }
  float gx=r0*0.0039f, gy=r1*0.0039f, gz=r2*0.0039f;
  static float hX[5],hY[5],hZ[5]; static uint8_t hN=0, hI=0;   // Fase 96: mediana de 5 -- tira os picos de 1-2 amostras (clique do rele, EMI no I2C)
  hX[hI]=gx; hY[hI]=gy; hZ[hI]=gz; hI=(hI+1)%5; if(hN<5) hN++;
  float mx=gx, my=gy, mz=gz;
  if(hN>=5){ mx=adxlMed5(hX); my=adxlMed5(hY); mz=adxlMed5(hZ); }
  if(adxlX==0&&adxlY==0&&adxlZ==0){ adxlX=mx; adxlY=my; adxlZ=mz; } else { adxlX+=0.3f*(mx-adxlX); adxlY+=0.3f*(my-adxlY); adxlZ+=0.3f*(mz-adxlZ); }
  adxlMaquina(gx,gy,gz,mx,my,mz,agora);
  if(adxlLive && agora-adxlLiveMs>=200){ adxlLiveMs=agora;
    float ang=adxlAngulo(); int n=(int)(ang*40.0f/30.0f+0.5f); if(n>40) n=40; char bar[44]; for(int i=0;i<40;i++) bar[i]=(i<n)?'#':'.'; bar[40]=0;
    Serial.printf("[ADXL] x=%+.3f y=%+.3f z=%+.3f g | %5.1f graus mov=%3.0fmg vib=%3.0fmg [%s] est %u dentro=%d conf=%d ign=%u\n",adxlX,adxlY,adxlZ,ang,adxlMov*1000.0f,adxlVib,bar,(unsigned)adxlEst,(int)adxlDentro,(int)adxlConfiavel,(unsigned)adxlIgnorados);
  }
}
static void adxlImprime(){
  if(!adxlOK){ Serial.println("[ADXL345] nao iniciado (tecla I tenta de novo)."); return; }
  Serial.printf("[ADXL345] %s | x=%+.3f y=%+.3f z=%+.3f g | |a|=%.3f | desvio %.1f graus | mov %.0f mg | vib %.0f mg (FECHADA %.0f%s, FORA %.0f%s) | estado %u | dentro=%d confiavel=%d | deteccao %s (tol %u, sens %u)\n",
    adxlSaude(),adxlX,adxlY,adxlZ,sqrtf(adxlX*adxlX+adxlY*adxlY+adxlZ*adxlZ),adxlAngulo(),adxlMov*1000.0f,adxlVib,adxlVibRef,adxlTemRef?"":" -",adxlVibAw,adxlTemAw?"":" -",(unsigned)adxlEst,(int)adxlDentro,(int)adxlConfiavel,adxlDet?"LIGADA":"desligada",(unsigned)adxlTolDeg,(unsigned)adxlMovMg);
}
static void adxlMsgPor(const char* m){ strlcpy(adxlMsg,m,sizeof(adxlMsg)); adxlMsgAte=millis()+2500UL; adxlMsgSeq++; }
static void adxlCalibrar(bool fora){
  if(!adxlOK){ Serial.println("[ADXL345] nao iniciado."); adxlMsgPor("Sensor nao encontrado"); return; }
  if(!adxlParadoDesde || millis()-adxlParadoDesde<2500UL){ adxlMsgPor("Segure parado por 3 s"); return; }   // calibrar com a mao tremendo grava lixo
  if(fora){ adxlAwX=adxlX; adxlAwY=adxlY; adxlAwZ=adxlZ; adxlVibAw=adxlVib; adxlTemAw=true; }
  else{ adxlRefX=adxlX; adxlRefY=adxlY; adxlRefZ=adxlZ; adxlVibRef=adxlVib; adxlTemRef=true;
        adxlEst=2; adxlEstDesde=millis(); adxlAbertoNotif=false; adxlMov=0; adxlMovSeq=0; adxlParadoDesde=0; }
  adxlCfgSalvar();
  Serial.printf("[ADXL345] posicao %s calibrada: %+.3f %+.3f %+.3f g, vibracao %.0f mg\n",fora?"FORA":"FECHADA",adxlX,adxlY,adxlZ,adxlVib);
  adxlMsgPor(fora?"Posicao FORA salva!":"Posicao FECHADA salva!");
}

// ── Fase 98 -- comandos e telemetria ao vivo para a tela "Tampa" do PWA ──
static void adxlComandosTick(){   // no loop(), fora de qualquer callback
  if(adxlCfgPend[0]){
    char buf[48]; strlcpy(buf,adxlCfgPend,sizeof(buf)); adxlCfgPend[0]=0;
    int d=0,t=5,m=60;
    if(sscanf(buf,"%*[^|]|%d|%d|%d",&d,&t,&m)==3){
      adxlDet=(d==1); if(t<1) t=1; if(t>30) t=30; adxlTolDeg=(uint8_t)t; if(m<20) m=20; if(m>300) m=300; adxlMovMg=(uint16_t)((m/10)*10);
      adxlCfgSalvar();
      char lg[90]; snprintf(lg,sizeof(lg),"Sensor da tampa ajustado pelo app: deteccao %s, tolerancia %u graus, sensibilidade %u mg",adxlDet?"ligada":"desligada",(unsigned)adxlTolDeg,(unsigned)adxlMovMg);
      logEvento("Automacao",lg,true); adxlMsgPor("Ajustes salvos");
    }
  }
  if(adxlCalPend[0]){
    char buf[24]; strlcpy(buf,adxlCalPend,sizeof(buf)); adxlCalPend[0]=0;
    const char* q=strchr(buf,'|');
    if(q && (q[1]=='F' || q[1]=='O')){
      adxlCalibrar(q[1]=='O');
      logEvento("Automacao",q[1]=='O'?"Sensor da tampa: posicao FORA calibrada pelo app":"Sensor da tampa: posicao FECHADA calibrada pelo app",true);
    }
  }
}
static bool adxlPubOcupado=false; static unsigned long adxlPubMs=0;
static void fbAdxlCallback(AsyncResult &r){ adxlPubOcupado=false; if(r.isError()) Serial.printf("[ADXL345] publicacao ao vivo ERRO: %s\n",r.error().message().c_str()); }
static void adxlPublicaTick(){   // so enquanto a tela "Tampa" do PWA esta aberta (cmd.adxlVivo renovado a cada ~40 s): 1 escrita a cada 2,5 s
  unsigned long agora=millis();
  if(!adxlVivoMs || agora-adxlVivoMs>90000UL) return;
  if(!fbStarted || !fbApp.ready()) return;
  if(adxlPubOcupado && agora-adxlPubMs<8000UL) return;
  if(agora-adxlPubMs<2500UL) return;
  adxlPubMs=agora; adxlPubOcupado=true;
  const char* sd=adxlSaude(); int sc=!adxlOK?1:(adxlIguais>=100?2:(strcmp(sd,"sensor ok")==0?0:3));
  float sep=(adxlTemRef&&adxlTemAw)?adxlAng2(adxlRefX,adxlRefY,adxlRefZ,adxlAwX,adxlAwY,adxlAwZ):0.0f;
  char b[210];
  snprintf(b,sizeof(b),"v1|%lu|%u|%d|%d|%d|%d|%d|%u|%u|%d|%d|%d|%d|%u|%d|%d|%d|%d|%d|%u|%s",
    agora/1000UL,(unsigned)adxlEst,(int)lroundf(adxlAngulo()*10.0f),(int)lroundf(adxlMov*1000.0f),(int)lroundf(adxlVib),sc,adxlDet?1:0,(unsigned)adxlTolDeg,(unsigned)adxlMovMg,
    adxlTemRef?1:0,adxlTemAw?1:0,(int)lroundf(adxlVibRef),(int)lroundf(adxlVibAw),(unsigned)adxlIgnorados,adxlConfiavel?1:0,adxlDentro?1:0,(int)lroundf(sep*10.0f),
    adxlAutoAtivo()?1:0,(int)tampaEst,(unsigned)adxlMsgSeq,adxlMsg);
  Document<Values::Value> doc("adxlLive", Values::Value(Values::StringValue(String(b))));
  DocumentMask dm("adxlLive");
  PatchDocumentOptions po(dm, DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH, po, doc, fbAdxlCallback, "fbAdxlTask");
}

// ── Tela Configuracoes > Tampa (ADXL345) ── ao vivo (5x/s) + ajustes com - / + + calibrar FECHADA / FORA
#define AX_CARD_Y 21
#define AX_ROW_Y0 89
#define AX_ROW_P  36
#define AX_BTN_Y  198
#define AX_BAR_X  64
#define AX_BAR_W  244
static uint16_t adxlCorEst(){
  if(!adxlOK) return cRed;
  switch(adxlEst){ case 2: return cGreen; case 3: return tft.color565(230,180,60); case 4: return tft.color565(255,130,50); default: return tft.color565(230,180,60); }
}
static const char* adxlNomeEst(){
  if(!adxlOK) return "SEM SENSOR";
  switch(adxlEst){ case 2: return "FECHADA"; case 3: return "MOVENDO"; case 4: return "ABERTA"; default: return "SEM CALIBRACAO"; }
}
static void adxlBarra(int y,float v,float vmax,float marca,uint16_t cor,float marca2=-1.0f){
  float f=v/vmax; if(f<0) f=0; if(f>1) f=1; int fw=(int)(AX_BAR_W*f);
  uint16_t vazio=tft.color565(30,30,70);
  if(fw>0) tft.fillRect(AX_BAR_X,y,fw,6,cor);
  if(fw<AX_BAR_W) tft.fillRect(AX_BAR_X+fw,y,AX_BAR_W-fw,6,vazio);
  float m=marca/vmax; if(m>1) m=1; int mx=AX_BAR_X+(int)(AX_BAR_W*m); if(mx>AX_BAR_X+AX_BAR_W-2) mx=AX_BAR_X+AX_BAR_W-2;
  tft.fillRect(mx,y-2,2,10,TFT_WHITE);
  if(marca2>=0){ float m2=marca2/vmax; if(m2>1) m2=1; int mx2=AX_BAR_X+(int)(AX_BAR_W*m2); if(mx2>AX_BAR_X+AX_BAR_W-2) mx2=AX_BAR_X+AX_BAR_W-2; tft.fillRect(mx2,y-2,2,10,tft.color565(255,170,60)); }
}
static void adxlTelaLive(){   // so a area dinamica (texto com fundo = sem piscar)
  uint16_t bg=tft.color565(16,16,46), bgT=tft.color565(8,8,26); char b[44];
  float ang=adxlAngulo(); uint16_t cor=adxlCorEst();
  const char* sd=adxlSaude(); uint16_t cs=(strcmp(sd,"sensor ok")==0)?cGreen:cRed;   // rotulo do topo honesto
  tft.fillRect(176,0,144,18,bgT); tft.setTextSize(1); tft.setTextColor(cs,bgT); tft.setCursor(314-tft.textWidth(sd),5); tft.print(sd);
  snprintf(b,sizeof(b),"%-14s",adxlNomeEst()); tft.setTextSize(2); tft.setTextColor(cor,bg); tft.setCursor(12,AX_CARD_Y+5); tft.print(b);
  if(adxlOK && adxlTemRef){ snprintf(b,sizeof(b),"%4.1f",ang); tft.setTextSize(2); tft.setTextColor(TFT_WHITE,bg); tft.setCursor(224,AX_CARD_Y+5); tft.print(b);
    tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(274,AX_CARD_Y+12); tft.print("graus"); }
  else{ tft.setTextSize(2); tft.setTextColor(cSub,bg); tft.setCursor(224,AX_CARD_Y+5); tft.print("--.-"); tft.setTextSize(1); tft.setCursor(274,AX_CARD_Y+12); tft.print("     "); }
  tft.setTextSize(1);
  if(adxlMsgAte && millis()<adxlMsgAte){ snprintf(b,sizeof(b),"%-40s",adxlMsg); tft.setTextColor(cGreen,bg); }
  else{ adxlMsgAte=0;
    if(!adxlOK) snprintf(b,sizeof(b),"%-40s","Confira 5V, GND, SDA e SCL do modulo");
    else if(adxlTemRef && !adxlConfiavel){   // Fase 97: mostra POR QUE (separacao medida) em vez de mandar calibrar de novo
      if(adxlTemAw) snprintf(b,sizeof(b),"sep %.1fgr vib %.0f/%.0fmg: so temperatura        ",adxlAng2(adxlRefX,adxlRefY,adxlRefZ,adxlAwX,adxlAwY,adxlAwZ),adxlVibRef,adxlVibAw);
      else snprintf(b,sizeof(b),"%-40s","Calibre FORA para o sensor separar");
    }
    else snprintf(b,sizeof(b),"%+.2f %+.2f %+.2f g  vib%.0fmg ign%u        ",adxlX,adxlY,adxlZ,adxlVib,(unsigned)adxlIgnorados);
    tft.setTextColor(adxlTemRef&&adxlOK&&!adxlConfiavel?tft.color565(255,170,60):cSub,bg); }
  b[39]=0; tft.setCursor(12,AX_CARD_Y+25); tft.print(b);
  adxlBarra(AX_CARD_Y+35,adxlOK?ang:0,30.0f,(float)adxlTolDeg,adxlOK&&adxlTemRef&&!adxlAngOk?tft.color565(255,130,50):cGreen);
  adxlBarra(AX_CARD_Y+45,adxlOK?adxlMov*1000.0f:0,300.0f,(float)adxlMovMg,adxlOK&&adxlMov*1000.0f>adxlMovMg?tft.color565(230,180,60):tft.color565(70,160,230));
  adxlBarra(AX_CARD_Y+55,adxlOK?adxlVib:0,60.0f,adxlTemRef?adxlVibRef:0.0f,tft.color565(150,110,230),adxlTemAw?adxlVibAw:-1.0f);   // branco = FECHADA, laranja = FORA
}
static void adxlRow(int y,const char* lb,const char* val,uint16_t cor){
  uint16_t bg=tft.color565(16,16,46), nb=tft.color565(40,40,110), bd=tft.color565(90,90,180);
  uiCard(4,y,312,33);
  tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(12,y+4); tft.print(lb);
  tft.setTextSize(2); tft.setTextColor(cor,bg); tft.setCursor(12,y+15); tft.print(val);
  tft.fillRoundRect(228,y+3,38,27,6,nb); tft.drawRoundRect(228,y+3,38,27,6,bd); tft.fillRect(238,y+15,18,3,TFT_WHITE);
  tft.fillRoundRect(272,y+3,38,27,6,nb); tft.drawRoundRect(272,y+3,38,27,6,bd); tft.fillRect(282,y+15,18,3,TFT_WHITE); tft.fillRect(289,y+9,3,15,TFT_WHITE);
}
void drawAdxlScreen(){
  tft.fillScreen(cBg);
  uiTopo("TAMPA - SENSOR ADXL345");
  uiCard(4,AX_CARD_Y,312,64);
  tft.setTextSize(1); tft.setTextColor(cSub,tft.color565(16,16,46));
  tft.setCursor(12,AX_CARD_Y+35); tft.print("Posicao");
  tft.setCursor(12,AX_CARD_Y+45); tft.print("Movim.");
  tft.setCursor(12,AX_CARD_Y+55); tft.print("Vibrac.");
  char v[20];
  adxlRow(AX_ROW_Y0,"Detectar abertura da tampa",adxlDet?"Ligado":"Desligado",adxlDet?cGreen:cSub);
  snprintf(v,sizeof(v),"%u graus",(unsigned)adxlTolDeg); adxlRow(AX_ROW_Y0+AX_ROW_P,"Tolerancia da posicao fechada",v,TFT_WHITE);
  snprintf(v,sizeof(v),"%u mg",(unsigned)adxlMovMg);   adxlRow(AX_ROW_Y0+2*AX_ROW_P,"Sensib. ao movimento (menor=mais)",v,TFT_WHITE);
  bool precisa=!adxlTemRef;
  uint16_t b1=precisa?tft.color565(150,100,20):tft.color565(30,80,150), l1c=precisa?tft.color565(255,200,90):tft.color565(120,180,255);
  uint16_t b2=adxlTemAw?tft.color565(50,50,70):tft.color565(70,40,100), l2c=tft.color565(190,150,240);
  tft.fillRoundRect(4,AX_BTN_Y,152,38,8,b1);  tft.drawRoundRect(4,AX_BTN_Y,152,38,8,l1c);
  tft.fillRoundRect(164,AX_BTN_Y,152,38,8,b2); tft.drawRoundRect(164,AX_BTN_Y,152,38,8,l2c);
  tft.setTextSize(2); tft.setTextColor(TFT_WHITE,b1); const char* t1="CALIBRAR"; tft.setCursor(4+(152-tft.textWidth(t1))/2,AX_BTN_Y+4); tft.print(t1);
  tft.setTextColor(TFT_WHITE,b2); tft.setCursor(164+(152-tft.textWidth(t1))/2,AX_BTN_Y+4); tft.print(t1);
  tft.setTextSize(1); tft.setTextColor(l1c,b1); const char* s1="FECHADA (na tampa)"; tft.setCursor(4+(152-tft.textWidth(s1))/2,AX_BTN_Y+25); tft.print(s1);
  tft.setTextColor(l2c,b2); const char* s2=adxlTemAw?"FORA (na mesa) - feito":"FORA (na mesa)"; tft.setCursor(164+(152-tft.textWidth(s2))/2,AX_BTN_Y+25); tft.print(s2);
  adxlTelaLive();
}
void handleAdxlTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_SETTINGS; drawSettingsPanel(); return; }
  if(ty>=AX_BTN_Y){ adxlCalibrar(tx>=160); adxlTelaLive(); drawAdxlScreen(); return; }
  if(ty<AX_ROW_Y0) return;
  int linha=(ty-AX_ROW_Y0)/AX_ROW_P; if(linha<0||linha>2) return;
  int dir=(tx>=220&&tx<268)?-1:(tx>=268&&tx<316)?+1:0; if(!dir) return;
  if(linha==0) adxlDet=!adxlDet;
  else if(linha==1){ int v=(int)adxlTolDeg+dir; if(v<1) v=1; if(v>30) v=30; adxlTolDeg=v; }
  else { int v=(int)adxlMovMg+dir*10; if(v<20) v=20; if(v>300) v=300; adxlMovMg=v; }
  adxlCfgSalvar();
  if(linha==0){ logEvento("Automacao",adxlDet?"Deteccao de tampa pelo sensor ADXL: ligada":"Deteccao de tampa pelo sensor ADXL: desligada",true); if(adxlDet && !adxlTemRef) adxlMsgPor("Calibre a posicao fechada"); }
  drawAdxlScreen();
}
void adxlTelaTick(){ if(appState!=S_ADXL) return; lastActivityMs=millis(); static unsigned long u=0; if(millis()-u>=200){ u=millis(); adxlTelaLive(); } }   // tela ao vivo: nao deixa o protetor de tela entrar

void lerSht31(){
  if(!sht31OK) return;
  resetarEstatisticasSensorSeVirouODia();
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  bool ok = !isnan(t) && !isnan(h);
  if(ok){
    temperaturaAtual = t; umidadeAtual = h; lastSht31OkMs = millis();
    checarAlertasSensor(t, h);
    histAmostra(t, h);            // Fase 47
    cicloStatsAmostra(t, h);      // Fase 48
    // Fase 21/22 — estatisticas do dia (min/max/media), pra tela de
    // detalhe. isnan() no min/max inicial funciona como "ainda nao setado".
    if(isnan(temperaturaMin)||t<temperaturaMin) temperaturaMin=t;
    if(isnan(temperaturaMax)||t>temperaturaMax) temperaturaMax=t;
    temperaturaSoma+=t; temperaturaN++;
    if(isnan(umidadeMin)||h<umidadeMin) umidadeMin=h;
    if(isnan(umidadeMax)||h>umidadeMax) umidadeMax=h;
    umidadeSoma+=h; umidadeN++;
    if(!sht31LeituraOkAnterior) Serial.println("[SHT31] Leitura voltou a funcionar.");

    // Fase 22/26 — amostra pro grafico da tela de detalhe. Cadencia
    // ADAPTATIVA (Fase 26): logo apos o boot (ou virada de dia), as 10
    // primeiras amostras saem a cada 1min em vez de 5min -- fecha a
    // "Aguardando leituras..." bem mais rapido (usuario comparou com o
    // PWA, que ja tem grafico populado em ~1-2min via Firestore).
    // Depois da 10a amostra (~10min cobertos), volta pro intervalo
    // normal de 5min (senao o buffer de 300 nao cobriria o dia inteiro).
    static unsigned long lastHistSampleMs = 0;
    unsigned long intervaloAtual = (sensorHistCount<10) ? 60000UL : SENSOR_HIST_INTERVAL_MS;
    if(sensorHistCount==0 || millis()-lastHistSampleMs>intervaloAtual){
      lastHistSampleMs = millis();
      if(sensorHistCount < SENSOR_HIST_MAX){
        tempHistorico[sensorHistCount] = t;
        umidHistorico[sensorHistCount] = h;
        sensorHistCount++;
      }
    }
  }else{
    if(sht31LeituraOkAnterior) Serial.println("[SHT31] Falha ao ler (NaN) -- sensor respondeu no begin() mas leitura falhou. Verifique conexao/alimentacao.");
    // Mantem o ultimo valor valido na tela em vez de virar "--.-" numa
    // falha isolada -- só perde o dado se a falha persistir e alguem
    // olhar a tela (o Serial ja avisou a transicao acima).
  }
  sht31LeituraOkAnterior = ok;
}

void fbLeituraCallback(AsyncResult &aResult){
  if(aResult.isError())
    Serial.printf("[Firebase Leitura ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
  else if(aResult.isResult())
    Serial.println("[Firebase] Leitura (temp/umidade) confirmada pelo servidor");
}

// Cria um NOVO documento (ID automatico) em chocadeiras/{id}/leituras --
// e essa subcolecao, ordenada por timestamp desc, que o PWA escuta em
// tempo real (escutarUltimaLeitura() no app.js) pra popular os cards
// Temperatura/Umidade. Diferente do heartbeat/atuadores (que fazem PATCH
// num campo do documento principal), aqui e createDocument mesmo.
void pushLeituraToFirestore(){
  if(!fbStarted || !fbApp.ready()) return;
  if(isnan(temperaturaAtual) || isnan(umidadeAtual)) return;  // sem leitura valida ainda, nao envia lixo
  time_t now = time(nullptr);
  if(now < (time_t)EPOCH_2024_01_01) return;  // mesmo guard do heartbeat -- NTP ainda nao sincronizou
  struct tm *utc = gmtime(&now);
  char ts[25]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);

  Document<Values::Value> doc("temperatura", Values::Value(Values::DoubleValue(number_t(temperaturaAtual, 2))));
  doc.add("umidade", Values::Value(Values::DoubleValue(number_t(umidadeAtual, 1))));
  if(!isnan(co2Atual) && lastCo2OkMs && millis()-lastCo2OkMs<180000UL)   // Fase 115: CO2 (ppm) no mesmo documento, sem escrita extra
    doc.add("co2", Values::Value(Values::DoubleValue(number_t(co2Atual, 0))));
  doc.add("timestamp", Values::Value(Values::TimestampValue(String(ts))));

  String leituraPath = String(FIRESTORE_DOCUMENT_PATH) + "/leituras";
  fbDocs.createDocument(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), leituraPath,
                         DocumentMask(), doc, fbLeituraCallback, "fbLeituraTask");
}

// ═══════════════════════════════════════════════════════════════
// HISTORICO DE TEMPERATURA/UMIDADE (Fase 47, reprojetado na Fase 53)
// Uma amostra a cada 10 min (media do intervalo). Dois destinos independentes:
//  * SD: /hist/AAAAMMDD.csv, uma linha "slot,temp*10,umid*10" (slot = minuto do dia/10, 0..143), so ANEXA (append) --
//    barato, sem manter nada na RAM, sobrevive a reboot. Alimenta a tela Historico do aparelho.
//  * Nuvem: chocadeiras/{id}/historico/AAAA-MM-DD, mapa "m" com UM campo por slot ("s123" = "t10,h10"). Cada escrita mexe
//    so nos slots enviados (updateMask "m.s123,..."), entao nunca sobrescreve o dia (a versao antiga gravava a string
//    inteira "pts" e perderia dados se o aparelho reiniciasse sem SD). Slots que nao puderam subir (sem WiFi/nuvem) ficam
//    numa fila em RAM (ate 320 = ~2 dias) e sobem em lotes de ate 12. O PWA le o mapa "m" (e o "pts" legado dos dias antigos).
//  * SD com mau contato: se uma gravacao falha, o cartao e dado como ausente e reconectado sozinho a cada 30 s (sdManter);
//    enquanto isso o historico continua subindo pra nuvem normalmente.
// ═══════════════════════════════════════════════════════════════
#ifndef SD_CS_PIN
#define SD_CS_PIN   18
#endif
#define HIST_DIR        "/hist"
#define HIST_SLOT_MIN   10
#define HIST_SLOTS_DIA  144
#define HIST_FILA       320
#define HIST_LOTE       12
struct HistPend{ int32_t ymd; int16_t slot, t10, h10, c; };   // Fase 115: c = CO2 medio do slot em ppm (-1 = sem leitura)
static int   histSlotAtual=-1, histYmd=0, histN=0;
static float histSomaT=0, histSomaH=0, histSomaC=0; static int histNC=0;
static HistPend histFila[HIST_FILA]; static int histFilaN=0;
static HistPend histLote[HIST_LOTE];  static int histLoteN=0;

static int histYmdDe(const DateTime& d){ return d.year()*10000+d.month()*100+d.day(); }
static void histArq(char* out,size_t n,int ymd){ snprintf(out,n,"%s/%08d.csv",HIST_DIR,ymd); }
static void histEnfileira(const HistPend& p){
  if(histFilaN>=HIST_FILA){ memmove(histFila,histFila+1,(HIST_FILA-1)*sizeof(HistPend)); histFilaN--; }
  histFila[histFilaN++]=p;
}
static void histGravaSlot(int ymd,int slot,float t,float h,int c=-1){
  HistPend p; p.ymd=ymd; p.slot=(int16_t)slot; p.t10=(int16_t)lroundf(t*10.0f); p.h10=(int16_t)lroundf(h*10.0f); p.c=(int16_t)c;
  histEnfileira(p);                                    // nuvem (independe do SD)
  if(!sdOK) return;
  if(!SD.exists(HIST_DIR)) SD.mkdir(HIST_DIR);
  char path[32]; histArq(path,sizeof(path),ymd);
  File f=SD.open(path,FILE_APPEND);
  if(!f){ sdOK=false; Serial.println("[Historico] falha ao abrir o arquivo do dia -- SD dado como ausente (reconecta sozinho)."); return; }
  size_t w=(c>=0)?f.printf("%d,%d,%d,%d\n",slot,(int)p.t10,(int)p.h10,c):f.printf("%d,%d,%d\n",slot,(int)p.t10,(int)p.h10); f.close();
  if(w==0){ sdOK=false; Serial.println("[Historico] escrita no SD falhou -- SD dado como ausente."); }
}
// Chamado a cada leitura valida do SHT31 (5s). Nao exige SD: so o relogio.
void histAmostra(float t,float h){
  if(!rtcOK) return;
  DateTime n=rtc.now(); int ymd=histYmdDe(n); int slot=(n.hour()*60+n.minute())/HIST_SLOT_MIN;
  if(histSlotAtual<0){ histSlotAtual=slot; histYmd=ymd; }
  if(slot!=histSlotAtual||ymd!=histYmd){
    if(histN>0) histGravaSlot(histYmd,histSlotAtual,histSomaT/histN,histSomaH/histN,histNC?(int)lroundf(histSomaC/histNC):-1);
    histSlotAtual=slot; histYmd=ymd; histN=0; histSomaT=0; histSomaH=0; histSomaC=0; histNC=0;
  }
  histSomaT+=t; histSomaH+=h; histN++;
  if(!isnan(co2Atual) && lastCo2OkMs && millis()-lastCo2OkMs<120000UL){ histSomaC+=co2Atual; histNC++; }   // Fase 115
}
// Depuracao (serial 'h'): fecha o slot atual agora, sem esperar os 10 min
void histForcar(){
  if(histSlotAtual>=0&&histN>0){ histGravaSlot(histYmd,histSlotAtual,histSomaT/histN,histSomaH/histN,histNC?(int)lroundf(histSomaC/histNC):-1); histN=0; histSomaT=0; histSomaH=0; histSomaC=0; histNC=0; Serial.printf("[Historico] slot %d fechado manualmente (fila %d)\n",histSlotAtual,histFilaN); }
  else Serial.printf("[Historico] nada a fechar (slot %d, n=%d, rtcOK=%d)\n",histSlotAtual,histN,(int)rtcOK);
}
void fbHistCallback(AsyncResult &aResult){
  if(aResult.isError()){
    Serial.printf("[Firebase Historico ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
    for(int i=0;i<histLoteN;i++) histEnfileira(histLote[i]);   // devolve o lote pra fila
    histLoteN=0;
  }
}
// Chamado de loop() a cada 3s (fora de callbacks).
void histServico(){
  if(histFilaN==0||!fbStarted||!fbApp.ready()) return;
  time_t now=time(nullptr); if(now<(time_t)EPOCH_2024_01_01) return;
  int ymd=histFila[0].ymd, n=0;
  while(n<histFilaN && n<HIST_LOTE && histFila[n].ymd==ymd) n++;
  Values::MapValue mv; String mask;
  for(int i=0;i<n;i++){
    char k[12], v[24]; snprintf(k,sizeof(k),"s%d",(int)histFila[i].slot);
    if(histFila[i].c>=0) snprintf(v,sizeof(v),"%d,%d,%d",(int)histFila[i].t10,(int)histFila[i].h10,(int)histFila[i].c); else snprintf(v,sizeof(v),"%d,%d",(int)histFila[i].t10,(int)histFila[i].h10);   // Fase 115: 3o valor = CO2 (PWA antigo ignora)
    mv.add(String(k),Values::StringValue(String(v)));
    if(i) mask+=','; mask+="m."; mask+=k;
  }
  struct tm* utc=gmtime(&now); char ts[25]; strftime(ts,sizeof(ts),"%Y-%m-%dT%H:%M:%SZ",utc);
  Document<Values::Value> doc("m",Values::Value(mv));
  doc.add("atualizadoEm",Values::Value(Values::TimestampValue(String(ts)))); mask+=",atualizadoEm";
  DocumentMask dmask(mask);
  PatchDocumentOptions opt(dmask,DocumentMask(),Precondition());
  char id[12]; snprintf(id,sizeof(id),"%04d-%02d-%02d",ymd/10000,(ymd/100)%100,ymd%100);
  String path=String(FIRESTORE_DOCUMENT_PATH)+"/historico/"+id;
  memcpy(histLote,histFila,n*sizeof(HistPend)); histLoteN=n;
  memmove(histFila,histFila+n,(histFilaN-n)*sizeof(HistPend)); histFilaN-=n;
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),path,opt,doc,fbHistCallback,"fbHistTask");
  Serial.printf("[Historico] %s: %d slot(s) enviados (fila %d)\n",id,n,histFilaN);
}
// Cartao que caiu (mau contato) volta sozinho: tenta remontar a cada 30 s. Chamado de loop().
// Fase 67 -- antes so DESCOBRIA a remocao de forma reativa, quando uma escrita programada (historico/log)
// calhava de falhar -- podia demorar minutos sem detectar nada se nada tivesse motivo pra escrever nesse
// meio tempo. Agora, com o cartao "OK", um teste ativo e barato (abrir a raiz) roda a cada 8s -- detecta
// a remocao rapido e de propósito, e avisa (tela + log + push), em vez de só descobrir por acidente.
unsigned long sdChecagemMs=0, sdAvisoUltMs=0; uint8_t sdQuedasRecentes=0;
void sdManter(){
  if(sdOK){
    if(millis()-sdChecagemMs<8000UL) return;
    sdChecagemMs=millis();
    File raiz=SD.open("/");
    bool viva=raiz && raiz.isDirectory();
    if(raiz) raiz.close();
    // Fase 74 -- mau contato: uma falha isolada nao conta; so declara "removido" apos 3 falhas seguidas (rechecando a cada 3 s)
    static uint8_t falhasSeguidas=0;
    if(viva){ falhasSeguidas=0; return; }
    if(++falhasSeguidas<3){ sdChecagemMs=millis()-5000UL; return; }   // proxima checagem em ~3 s
    falhasSeguidas=0;
    sdOK=false;
    Serial.println("[SD] Cartao nao responde -- dado como removido.");
    sdQuedasRecentes++;
    // Fase 74 -- anti-spam: cartao com mau contato cai e volta o tempo todo; so o 1o aviso de cada 15 min vira push/toast/log na nuvem
    if(millis()-sdAvisoUltMs>=900000UL || sdAvisoUltMs==0){
      sdAvisoUltMs=millis(); sdQuedasRecentes=1;
      logEvento("SD","Cartao removido ou sem contato",true);
      pushEnfileirar(10,"ChocaIoT: cartao SD removido","O cartao microSD foi removido ou perdeu contato. Historico e fotos param de ser salvos ate reconectar.");
      arqToast("Cartao SD removido!",false);
      redrawCurrentScreen();
    }else logEvento("SD","Cartao caiu de novo (mau contato?)",false);
    return;
  }
  static unsigned long ult=0;
  if(millis()-ult<30000UL) return;
  ult=millis();
  if(SD.begin(SD_CS_PIN,tft.getSPIinstance())){
    sdOK=true; sdChecagemMs=millis();
    Serial.println("[SD] Cartao reconectado.");
    if(millis()-sdAvisoUltMs<900000UL && sdQuedasRecentes>=2){   // instavel: nao repete aviso/push a cada queda
      logEvento("SD","Cartao reconectado (instavel: varias quedas, verifique o contato)",false);
    }else{
      logEvento("SD","Cartao reconectado",true);
      pushEnfileirar(10,"ChocaIoT: cartao SD reconectado","O cartao microSD voltou a responder.");
      arqToast("Cartao SD reconectado",true);
      redrawCurrentScreen();
    }
    arqIndiceSujo=true;
  }
}

// ── Carga de um periodo para o grafico (arquivos /hist -> 316 colunas) ──
char histRotIni[10]="", histRotMid[10]="", histRotFim[10]="";
static void histRotulo(char* out,size_t n,int diasAtras){
  DateTime d=rtc.now()-TimeSpan(diasAtras,0,0,0); snprintf(out,n,"%02d/%02d",d.day(),d.month());
}
// nDias dias terminando "fimAtras" dias atras (0 = termina hoje). Preenche h7T/h7H (colunas) e as estatisticas h7*.
void histCarregar(int nDias,int fimAtras){
  static float bt[H7_COLS], bh[H7_COLS], bc[H7_COLS]; static uint16_t bn[H7_COLS], bnc[H7_COLS];
  memset(bt,0,sizeof(bt)); memset(bh,0,sizeof(bh)); memset(bn,0,sizeof(bn)); memset(bc,0,sizeof(bc)); memset(bnc,0,sizeof(bnc));
  h7N=0; h7Pts=0; h7TMin=h7TMax=h7HMin=h7HMax=NAN; h7TSoma=h7HSoma=0;
  h7CN=0; h7CPts=0; h7CMin=h7CMax=NAN; h7CSoma=0;   // Fase 115
  if(nDias<1) nDias=1;
  if(rtcOK){
    histRotulo(histRotIni,sizeof(histRotIni),fimAtras+nDias-1); histRotulo(histRotMid,sizeof(histRotMid),fimAtras+(nDias-1)/2); histRotulo(histRotFim,sizeof(histRotFim),fimAtras);
    if(nDias==1){ strcpy(histRotIni,"00:00"); strcpy(histRotMid,"12:00"); strcpy(histRotFim,"24:00"); }
  }
  if(!sdOK||!rtcOK) return;
  DateTime hoje=rtc.now();
  for(int d=nDias-1; d>=0; d--){
    DateTime dd=hoje-TimeSpan(fimAtras+d,0,0,0);
    char p[32]; histArq(p,sizeof(p),histYmdDe(dd));
    File f=SD.open(p,FILE_READ); if(!f) continue;
    while(f.available()){
      String ln=f.readStringUntil('\n'); int a=ln.indexOf(','), b=ln.indexOf(',',a+1); if(a<0||b<0) continue;
      int c3=ln.indexOf(',',b+1);   // Fase 115: 4a coluna opcional = CO2 (ppm)
      int slot=ln.substring(0,a).toInt(); float t=ln.substring(a+1,b).toInt()/10.0f, h=(c3>0?ln.substring(b+1,c3):ln.substring(b+1)).toInt()/10.0f;
      int cv=(c3>0)?ln.substring(c3+1).toInt():-1;
      if(slot<0||slot>=HIST_SLOTS_DIA) continue;
      long gi=(long)(nDias-1-d)*HIST_SLOTS_DIA+slot; int bin=(int)(gi*H7_COLS/((long)nDias*HIST_SLOTS_DIA)); if(bin>=H7_COLS) bin=H7_COLS-1;
      bt[bin]+=t; bh[bin]+=h; bn[bin]++; h7Pts++;
      if(isnan(h7TMin)||t<h7TMin) h7TMin=t; if(isnan(h7TMax)||t>h7TMax) h7TMax=t; h7TSoma+=t;
      if(isnan(h7HMin)||h<h7HMin) h7HMin=h; if(isnan(h7HMax)||h>h7HMax) h7HMax=h; h7HSoma+=h;
      if(cv>0){ bc[bin]+=cv; bnc[bin]++; h7CPts++; if(isnan(h7CMin)||cv<h7CMin) h7CMin=cv; if(isnan(h7CMax)||cv>h7CMax) h7CMax=cv; h7CSoma+=cv; }
    }
    f.close();
  }
  bool ini=false; float ut=0,uh=0;   // descarta o vazio inicial; buracos no meio repetem o ultimo valor
  for(int i=0;i<H7_COLS;i++){
    if(bn[i]){ ut=bt[i]/bn[i]; uh=bh[i]/bn[i]; ini=true; }
    if(ini){ h7T[h7N]=ut; h7H[h7N]=uh; h7N++; }
  }
  { bool iniC=false; float uc=0;   // Fase 115: serie propria do CO2 (comeca na 1a coluna com leitura)
    for(int i=0;i<H7_COLS;i++){ if(bnc[i]){ uc=bc[i]/bnc[i]; iniC=true; } if(iniC){ h7C[h7CN]=uc; h7CN++; } } }
}

// ── Historico POR CICLO (Fase 55) ──
// /ciclos.idx (SD): uma linha por ciclo ENCERRADO: "iniYmd,fimYmd,sit,nome" (sit: c concluido, i interrompido, x excluido).
// O ciclo em andamento entra na lista direto do estado ('a'). Tela 1 = lista com filtros e ordem; tela 2 = grafico do ciclo.
#define CIDX_MAX 40
struct CicloIdx{ int32_t ini, fim; char sit; char nome[24]; };
CicloIdx cidx[CIDX_MAX]; int cidxN=0;
int cidxVis[CIDX_MAX]; int cidxVisN=0;
int histFiltro=0;      // 0 todos · 1 andamento · 2 concluidos · 3 interrompidos · 4 excluidos
int histOrdem=0;       // 0 recentes · 1 antigos · 2 nome
int histScroll=0, histSel=-1;
bool histTemp=true;

static int ymdDeEpoch(time_t utc){ time_t l=utc+tzOffsetSeg(); struct tm t; gmtime_r(&l,&t); return (t.tm_year+1900)*10000+(t.tm_mon+1)*100+t.tm_mday; }
static long diasDoYmd(int ymd){ DateTime d(ymd/10000,(ymd/100)%100,ymd%100,0,0,0); return (long)(d.unixtime()/86400UL); }
// Chamado ao encerrar um ciclo (cicloRelatorioGerar).
void cicloIdxGravar(time_t ini,time_t fim,char sit,const char* nome){
  if(!sdOK) return;
  File f=SD.open("/ciclos.idx",FILE_APPEND); if(!f) return;
  char n[24]; strlcpy(n,nome,sizeof(n)); for(char* p=n;*p;p++) if(*p==','||*p=='\n'||*p=='\r') *p=' ';
  f.printf("%d,%d,%c,%s\n",ymdDeEpoch(ini),ymdDeEpoch(fim),sit,n); f.close();
}
static void cidxCarregar(){
  cidxN=0;
  if(fbCicloAtivo && fbCicloIniciadoEm>0){
    CicloIdx& c=cidx[cidxN++]; c.ini=ymdDeEpoch(fbCicloIniciadoEm); c.fim=ymdDeEpoch(time(nullptr)); c.sit='a';
    strlcpy(c.nome,fbCicloNome[0]?fbCicloNome:"Ciclo atual",sizeof(c.nome));
  }
  if(!sdOK||!SD.exists("/ciclos.idx")) return;
  File f=SD.open("/ciclos.idx",FILE_READ); if(!f) return;
  while(f.available()){
    String ln=f.readStringUntil('\n'); ln.trim(); if(!ln.length()) continue;
    CicloIdx c; int a=0,b=0; char sit=0; char nm[24]="";
    if(sscanf(ln.c_str(),"%d,%d,%c,%23[^\n]",&a,&b,&sit,nm)<3) continue;
    c.ini=a; c.fim=b; c.sit=sit; strlcpy(c.nome,nm,sizeof(c.nome));
    if(cidxN>=CIDX_MAX){ int drop=(cidx[0].sit=='a')?1:0; memmove(cidx+drop,cidx+drop+1,(CIDX_MAX-1-drop)*sizeof(CicloIdx)); cidxN=CIDX_MAX-1; }   // cheio: descarta o mais antigo
    cidx[cidxN++]=c;
  }
  f.close();
}
static void cidxAplicar(){
  static const char filtroSit[5]={0,'a','c','i','x'};
  cidxVisN=0;
  for(int i=0;i<cidxN;i++) if(!filtroSit[histFiltro]||cidx[i].sit==filtroSit[histFiltro]) cidxVis[cidxVisN++]=i;
  for(int i=1;i<cidxVisN;i++){   // insercao (max 40)
    int v=cidxVis[i], j=i-1;
    auto antes=[&](int x,int y)->bool{   // x deve vir antes de y?
      if(histOrdem==2) return strcasecmp(cidx[x].nome,cidx[y].nome)<0;
      if(histOrdem==1) return cidx[x].ini<cidx[y].ini;
      return cidx[x].ini>cidx[y].ini;
    };
    while(j>=0 && antes(v,cidxVis[j])){ cidxVis[j+1]=cidxVis[j]; j--; }
    cidxVis[j+1]=v;
  }
}
static void cidxDataTxt(char* out,size_t n,int ymd){ snprintf(out,n,"%02d/%02d/%02d",ymd%100,(ymd/100)%100,(ymd/10000)%100); }
static const char* cidxSitTxt(char s){ return s=='a'?"Em andamento":s=='c'?"Concluido":s=='i'?"Interrompido":"Excluido"; }
static uint16_t cidxSitCor(char s){ return s=='a'?cGreen:s=='c'?tft.color565(70,190,255):s=='i'?tft.color565(240,170,50):tft.color565(240,110,110); }

#define HL_ROW_Y0 52
#define HL_ROW_H  28
#define HL_VIS    6
void drawHistLista(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print("Historico de ciclos");
  static const char* ordTxt[3]={"Recentes","Antigos","A-Z"};
  tft.fillRoundRect(238,2,78,20,5,tft.color565(30,30,70));tft.drawRoundRect(238,2,78,20,5,tft.color565(55,55,120));
  tft.setTextColor(TFT_WHITE,tft.color565(30,30,70));tft.setTextSize(1);tft.setCursor(238+(78-tft.textWidth(ordTxt[histOrdem]))/2,8);tft.print(ordTxt[histOrdem]);
  static const char* nomes[5]={"Todos","Andam.","Concl.","Interr.","Excl."};
  for(int i=0;i<5;i++){
    int x=4+i*63; bool sel=(histFiltro==i); uint16_t bg=sel?tft.color565(10,50,20):tft.color565(16,16,46);
    tft.fillRoundRect(x,27,60,20,5,bg);tft.drawRoundRect(x,27,60,20,5,sel?cGreen:tft.color565(55,55,120));
    tft.setTextSize(1);tft.setTextColor(sel?cGreen:TFT_WHITE,bg);tft.setCursor(x+(60-tft.textWidth(nomes[i]))/2,33);tft.print(nomes[i]);
  }
  if(cidxVisN==0){
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);
    tft.setCursor(50,110);tft.print(cidxN?"Nenhum ciclo com esse filtro.":"Nenhum ciclo ainda.");
    if(!cidxN){ tft.setCursor(50,126);tft.print("Os ciclos encerrados aparecem aqui."); }
  }
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  for(int i=0;i<HL_VIS;i++){
    int k=histScroll+i; if(k>=cidxVisN) break;
    const CicloIdx& c=cidx[cidxVis[k]]; int y=HL_ROW_Y0+i*HL_ROW_H;
    tft.fillRoundRect(4,y,282,HL_ROW_H-2,5,rowBg);tft.drawRoundRect(4,y,282,HL_ROW_H-2,5,rowBrd);
    tft.setTextSize(1);tft.setTextColor(TFT_WHITE,rowBg);tft.setCursor(10,y+10);
    char nm[21]; strlcpy(nm,c.nome,sizeof(nm)); tft.print(nm);
    char d1[10],d2[10]; cidxDataTxt(d1,sizeof(d1),c.ini); cidxDataTxt(d2,sizeof(d2),c.fim);
    char dt[24]; snprintf(dt,sizeof(dt),"%.5s-%.5s",d1,d2);
    tft.setTextColor(cSub,rowBg);tft.setCursor(140,y+10);tft.print(dt);
    uint16_t cor=cidxSitCor(c.sit); const char* tx=c.sit=='a'?"AND":c.sit=='c'?"CON":c.sit=='i'?"INT":"EXC";
    tft.fillRoundRect(240,y+5,42,16,4,rowBg);tft.drawRoundRect(240,y+5,42,16,4,cor);
    tft.setTextColor(cor,rowBg);tft.setCursor(240+(42-tft.textWidth(tx))/2,y+10);tft.print(tx);
  }
  bool cima=histScroll>0, baixo=histScroll<cidxVisN-HL_VIS; uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.drawRoundRect(290,HL_ROW_Y0,26,80,5,rowBrd);   tft.fillTriangle(303,HL_ROW_Y0+22,294,HL_ROW_Y0+52,312,HL_ROW_Y0+52,cima?on:off);
  tft.drawRoundRect(290,HL_ROW_Y0+88,26,80,5,rowBrd); tft.fillTriangle(303,HL_ROW_Y0+88+58,294,HL_ROW_Y0+88+28,312,HL_ROW_Y0+88+28,baixo?on:off);
}
void abrirHistLista(bool temp){
  histTemp=temp; cidxCarregar(); cidxAplicar(); histScroll=0; appState=S_HISTL; drawHistLista();
}
void handleHistListaTap(int tx,int ty){
  if(ty<24){
    if(tx<40){ appState=S_SENSOR_DETAIL; drawSensorDetailScreen(); return; }
    if(tx>=238){ histOrdem=(histOrdem+1)%3; cidxAplicar(); histScroll=0; drawHistLista(); }
    return;
  }
  if(ty>=27&&ty<47){ int i=(tx-4)/63; if(i>=0&&i<5&&i!=histFiltro){ histFiltro=i; cidxAplicar(); histScroll=0; drawHistLista(); } return; }
  if(tx>=288){
    int maxS=cidxVisN-HL_VIS; if(maxS<0) maxS=0;
    if(ty<HL_ROW_Y0+84){ if(histScroll>0){ histScroll--; drawHistLista(); } } else if(histScroll<maxS){ histScroll++; drawHistLista(); }
    return;
  }
  int row=(ty-HL_ROW_Y0)/HL_ROW_H, k=histScroll+row;
  if(ty>=HL_ROW_Y0&&row<HL_VIS&&k<cidxVisN){ histSel=cidxVis[k]; abrirHistCiclo(); }
}

// ── Grafico de UM ciclo ──
void histCarregar(int nDias,int fimAtras);
void abrirHistCiclo(){
  const CicloIdx& c=cidx[histSel];
  long d0=diasDoYmd(c.ini), d1=diasDoYmd(c.fim), hoje=rtcOK?diasDoYmd(histYmdDe(rtc.now())):d1;
  int n=(int)(d1-d0+1); if(n<1) n=1; if(n>62) n=62;
  int fimAtras=(int)(hoje-d1); if(fimAtras<0) fimAtras=0;
  histCarregar(n,fimAtras);
  appState=S_HIST; drawHist();
}
void drawHist(){
  const CicloIdx& c=cidx[histSel];
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char nm[18]; strlcpy(nm,c.nome,sizeof(nm));
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print(nm);
  arqBotao(150,2,80,20,"Temp",histTemp?tft.color565(10,50,20):tft.color565(16,16,46),histTemp?cGreen:TFT_WHITE,histTemp?cGreen:tft.color565(55,55,120));
  arqBotao(236,2,80,20,"Umid",!histTemp?tft.color565(10,50,20):tft.color565(16,16,46),!histTemp?cGreen:TFT_WHITE,!histTemp?cGreen:tft.color565(55,55,120));
  char d1[10],d2[10]; cidxDataTxt(d1,sizeof(d1),c.ini); cidxDataTxt(d2,sizeof(d2),c.fim);
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(6,29);tft.printf("%s a %s",d1,c.sit=='a'?"hoje":d2);
  tft.setTextColor(cidxSitCor(c.sit),cBg);tft.setCursor(316-tft.textWidth(cidxSitTxt(c.sit)),29);tft.print(cidxSitTxt(c.sit));
  int cy=42, ch=180;
  if(!sdOK){
    tft.setTextColor(tft.color565(230,180,60),cBg);tft.setCursor(20,cy+ch/2-10);tft.print("Cartao SD indisponivel -- sem dados locais.");
    tft.setCursor(20,cy+ch/2+4);tft.print("O app (Historico) tem o mesmo grafico.");
    return;
  }
  chartEixo7d=true;
  float* hist=histTemp?h7T:h7H; float mn=histTemp?h7TMin:h7HMin, mx=histTemp?h7TMax:h7HMax;
  float med=h7Pts>0?((histTemp?h7TSoma:h7HSoma)/h7Pts):NAN;
  float imin=histTemp?TEMP_IDEAL_MIN:UMID_IDEAL_MIN, imax=histTemp?TEMP_IDEAL_MAX:UMID_IDEAL_MAX;
  uint16_t lc=h7N>0?corPorFaixa(hist[h7N-1],imin,imax,histTemp?TEMP_ALERTA_MARGEM:UMID_ALERTA_MARGEM):cClk;
  drawSensorHistChart(2,cy,316,ch,hist,h7N,imin,imax,lc,histTemp?"C":"%",mn,mx,med,true);
  tft.setTextSize(1);tft.setTextColor(cClk,cBg);tft.setCursor(6,228);
  tft.printf("%u pontos (1 a cada 10 min)",(unsigned)h7Pts);
  chartEixo7d=false;
}
void handleHistTap(int tx,int ty){
  if(ty<24){
    if(tx<40){ appState=S_HISTL; drawHistLista(); return; }
    if(tx>=150&&tx<230){ if(!histTemp){ histTemp=true; drawHist(); } }
    else if(tx>=236){ if(histTemp){ histTemp=false; drawHist(); } }
  }
}

// ═══════════════════════════════════════════════════════════════
// SD — LOGS (Fase 21, reprojetada 2x em 2026-09-22 depois da tela branca)
//
// TENTATIVA 1 (revertida): SD no MESMO barramento fisico do display, via
// SD.begin(pin) simples (usa o objeto SPI global do Arduino por baixo
// dos panos). Quebrou o display mesmo sem cartao conectado -- SD.begin()
// inicializava um SEGUNDO periferico SPI que brigava com o do TFT_eSPI
// pelos mesmos pinos fisicos (a matriz de GPIO so deixa 1 "dono" por vez).
//
// TENTATIVA 2 (revertida): periferico SPI totalmente separado (FSPI/SPI2)
// em 4 pinos dedicados (18/22/23/24). Evitava o conflito, mas o usuario
// foi conferir os pinos fisicos da placa (fotos reais) e 22/23/24 NAO
// aparecem em lugar nenhum dos conectores -- essa placa de 44 pinos
// simplesmente nao os expoe. Sem esses 3 pinos, essa tentativa nao dava
// pra fiar de jeito nenhum.
//
// TENTATIVA 3 (esta, definitiva) — compartilhar o MESMO periferico SPI
// que o TFT_eSPI ja usa, do jeito OFICIAL: `TFT_eSPI` expõe
// `getSPIinstance()` justamente pra isso, e a propria biblioteca tem um
// exemplo pronto fazendo exatamente isso
// (examples/Generic/ESP32_SDcard_jpeg/ESP32_SDcard_jpeg.ino):
//   SD.begin(CS_PIN, tft.getSPIinstance());
// Passar a MESMA instancia (em vez de deixar a lib SD criar/inicializar
// uma por conta propria) elimina o conflito das duas tentativas
// anteriores: nao ha um segundo periferico brigando por pino nenhum, e
// os fios MOSI/MISO/SCK do leitor SD podem ser emendados direto nos
// mesmos pontos do conector do display (SDI/SDO/SCK) -- exatamente a
// fiacao mais simples, sugerida na 1a vez, so que agora com o codigo
// certo por baixo. Único pino novo de verdade: o CS (18, confirmado
// visivel/livre pelo usuario nas fotos da placa fisica).
// ═══════════════════════════════════════════════════════════════
#define SD_CS_PIN   18
#define SD_LOG_PATH "/chocaiot.log"
#define SD_LOG_MAX_BYTES 200000UL   // rotaciona pra .old ao passar disso

void fbLogCallback(AsyncResult &aResult){
  if(aResult.isError())
    Serial.printf("[Firebase Log ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
}

// Sincroniza um evento "importante" pro Firestore -- so quando chamado
// com importante=true em logEvento() e a rede/Firebase estiverem prontos.
void pushLogEventoToFirestore(const char* categoria, const char* mensagem){
  if(!fbStarted || !fbApp.ready()) return;
  time_t now = time(nullptr);
  if(now < (time_t)EPOCH_2024_01_01) return;
  struct tm *utc = gmtime(&now);
  char ts[25]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);

  Document<Values::Value> doc("categoria", Values::Value(Values::StringValue(String(categoria))));
  doc.add("mensagem", Values::Value(Values::StringValue(jsonEsc(String(mensagem)))));
  doc.add("timestamp", Values::Value(Values::TimestampValue(String(ts))));

  String logPath = String(FIRESTORE_DOCUMENT_PATH) + "/logs";
  fbDocs.createDocument(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), logPath,
                         DocumentMask(), doc, fbLogCallback, "fbLogTask");
}

// Registra um evento: sempre no Serial + SD (se sdOK); no Firestore
// tambem SE importante=true. E o ponto unico usado pelos pontos-chave do
// firmware (boot/reset, WiFi, comando de desligar, toggle de atuador).
void logEvento(const char* categoria, const char* mensagem, bool importante){
  char ts[25]="sem-hora";
  time_t now = time(nullptr);
  if(now >= (time_t)EPOCH_2024_01_01){
    struct tm *utc = gmtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  }else if(rtcOK){   // Fase 64: antes do NTP o log saia "sem-hora"; agora usa o RTC (hora LOCAL, marcada com "L" no lugar do "Z")
    DateTime r=rtc.now();
    snprintf(ts,sizeof(ts),"%04d-%02d-%02dT%02d:%02d:%02dL",r.year(),r.month(),r.day(),r.hour(),r.minute(),r.second());
  }
  Serial.printf("[LOG] [%s] %s: %s\n", ts, categoria, mensagem);

  if(sdOK){
    File f = SD.open(SD_LOG_PATH, FILE_APPEND);
    if(f){
      if(f.size() > SD_LOG_MAX_BYTES){
        f.close();
        SD.remove("/chocaiot.old");
        SD.rename(SD_LOG_PATH, "/chocaiot.old");
        f = SD.open(SD_LOG_PATH, FILE_APPEND);
      }
      if(f){
        f.printf("[%s] %s: %s\n", ts, categoria, mensagem);
        f.close();
      }
    }else{
      Serial.println("[SD] Falha ao abrir o arquivo de log para escrita.");
    }
  }

  if(importante) pushLogEventoToFirestore(categoria, mensagem);
}

// Le so o FIM do arquivo (tail), ate maxBytes -- evita carregar um log
// grande inteiro na RAM so pra mostrar as ultimas linhas na tela de
// 320x240. Retorna as ultimas ateLinhas linhas encontradas nesse trecho.
// Fase 29 — janela aumentada de 4KB pra 12KB pra alimentar a paginacao
// (LOGS_BUF_MAX_LINHAS=120) com historico de verdade, nao so a ultima
// tela.
#define LOGS_TAIL_MAX_BYTES 12000
int lerUltimasLinhasLog(String linhas[], int maxLinhas){
  if(!sdOK) return 0;
  File f = SD.open(SD_LOG_PATH, FILE_READ);
  if(!f) return 0;
  size_t tamanho = f.size();
  size_t inicio = (tamanho > LOGS_TAIL_MAX_BYTES) ? (tamanho - LOGS_TAIL_MAX_BYTES) : 0;
  f.seek(inicio);
  String buf = f.readString();
  f.close();

  int n=0; int from=0;
  // Quebra em linhas descartando a primeira (provavelmente truncada pelo seek).
  bool primeira = (inicio>0);
  for(int i=0;i<(int)buf.length();i++){
    if(buf[i]=='\n'){
      String linha = buf.substring(from,i);
      from = i+1;
      if(primeira){ primeira=false; continue; }
      if(linha.length()==0) continue;
      if(n<maxLinhas){ linhas[n++]=linha; }
      else { for(int k=1;k<maxLinhas;k++) linhas[k-1]=linhas[k]; linhas[maxLinhas-1]=linha; }
    }
  }
  return n;
}

// Fase 29 — icones simples desenhados com primitivas (mesmo estilo de
// drawXBtn/drawWifiIcon), sem nenhuma fonte de icones/bitmap.
void drawSearchIcon(int cx,int cy,uint16_t c){
  tft.drawCircle(cx-2,cy-2,5,c);
  tft.drawLine(cx+2,cy+2,cx+6,cy+6,c);
  tft.drawLine(cx+3,cy+2,cx+6,cy+5,c);
}
void drawTrashIcon(int x,int y,uint16_t c){
  tft.drawFastHLine(x,y+2,14,c);
  tft.drawFastHLine(x+4,y,6,c);
  tft.drawRect(x+2,y+4,10,10,c);
  tft.drawFastVLine(x+5,y+6,6,c);
  tft.drawFastVLine(x+8,y+6,6,c);
}

// Carrega logsBufLinhas do SD (mais recente primeiro -- lerUltimasLinhasLog
// devolve em ordem cronologica, invertido aqui) e reaplica o filtro ativo.
// Chamada so' ao ABRIR a tela (ver abrirTelaLogs()) e apos "Apagar" -- NAO
// a cada redraw, pra nao ficar re-lendo o SD toda vez que a paginacao muda.
void aplicarFiltroLogs();  // forward -- definida logo abaixo
// Fase 64 -- filtros de Data e Tipo + carimbo de hora em horario LOCAL (o arquivo guarda UTC "...Z"; antes do NTP, "...L" = hora local do RTC)
static int32_t logsDatas[8]; static int logsNDatas=0, logsFiltroDia=-1;      // dias (aaaammdd local) presentes no buffer; -1 = todos
static char logsTipos[10][12]; static int logsNTipos=0, logsFiltroTipo=-1;   // categorias presentes; -1 = todos
static int32_t logDiaLinha[LOGS_BUF_MAX_LINHAS]; static int8_t logTipoLinha[LOGS_BUF_MAX_LINHAS];
struct LogTs{ int ymd,hh,mi,ss; char cat[14]; int msgIdx; bool ok; };
static LogTs logParse(const String& l){
  LogTs r; r.ymd=0; r.hh=r.mi=r.ss=0; r.cat[0]=0; r.msgIdx=0; r.ok=false;
  int rb=l.indexOf(']'); if(l.length()<4||l[0]!='['||rb<2) return r;
  int c1=l.indexOf(':',rb); if(c1>rb){ String c=l.substring(rb+2,c1); strlcpy(r.cat,c.c_str(),sizeof(r.cat)); r.msgIdx=c1+2; } else r.msgIdx=rb+2;
  String ts=l.substring(1,rb); int Y,M,D,h,m,s;
  if(ts.length()==20 && sscanf(ts.c_str(),"%d-%d-%dT%d:%d:%d",&Y,&M,&D,&h,&m,&s)==6){
    char z=ts[19];
    if(z=='Z'){
      time_t t=parseRfc3339(ts)+tzOffsetSeg(); struct tm g; gmtime_r(&t,&g);
      Y=g.tm_year+1900; M=g.tm_mon+1; D=g.tm_mday; h=g.tm_hour; m=g.tm_min; s=g.tm_sec;
    }
    r.ymd=Y*10000+M*100+D; r.hh=h; r.mi=m; r.ss=s; r.ok=true;
  }
  return r;
}
static void logsIndexar(){   // monta as listas de datas/categorias do buffer
  logsNDatas=0; logsNTipos=0; logsFiltroDia=-1; logsFiltroTipo=-1;
  for(int i=0;i<logsBufCount;i++){
    LogTs t=logParse(logsBufLinhas[i]); logDiaLinha[i]=t.ymd; logTipoLinha[i]=-1;
    if(t.ymd){ bool ja=false; for(int k=0;k<logsNDatas;k++) if(logsDatas[k]==t.ymd) ja=true; if(!ja && logsNDatas<8) logsDatas[logsNDatas++]=t.ymd; }
    if(t.cat[0]){ int k=0; for(;k<logsNTipos;k++) if(!strcmp(logsTipos[k],t.cat)) break; if(k==logsNTipos && logsNTipos<10) strlcpy(logsTipos[logsNTipos++],t.cat,12); if(k<logsNTipos) logTipoLinha[i]=k; }
  }
}

int carregarLogsBuffer(){
  int n = lerUltimasLinhasLog(logsBufLinhas, LOGS_BUF_MAX_LINHAS);
  for(int i=0;i<n/2;i++){ String tmp=logsBufLinhas[i]; logsBufLinhas[i]=logsBufLinhas[n-1-i]; logsBufLinhas[n-1-i]=tmp; }
  logsBufCount = n;
  logsIndexar();
  aplicarFiltroLogs();
  return n;
}

bool logLinhaContemFiltro(const String& linha, const char* termo){
  if(termo[0]==0) return true;
  String a=linha; a.toLowerCase();
  String b=termo; b.toLowerCase();
  return a.indexOf(b) >= 0;
}

void aplicarFiltroLogs(){
  logsFiltradoCount=0;
  for(int i=0;i<logsBufCount;i++){
    if(logsFiltroDia>=0 && logDiaLinha[i]!=logsDatas[logsFiltroDia]) continue;
    if(logsFiltroTipo>=0 && logTipoLinha[i]!=logsFiltroTipo) continue;
    if(logLinhaContemFiltro(logsBufLinhas[i], logsSearchAtivo)) logsFiltradoIdx[logsFiltradoCount++]=i;
  }
  logsPage=0;
}

int logsTotalPaginas(){
  if(logsFiltradoCount==0) return 1;
  return (logsFiltradoCount+LOGS_LINHAS_TELA-1)/LOGS_LINHAS_TELA;
}

// Campo de texto do filtro (mesmo visual de drawSSIDField/drawPassField).
void drawLogsSearchField(){
  tft.fillRect(5,40,278,24,tft.color565(5,5,18));
  tft.drawRect(5,40,278,24,tft.color565(75,75,170));
  tft.setTextColor(TFT_WHITE,tft.color565(5,5,18));tft.setTextSize(1);
  tft.setCursor(9,48);tft.print(logsSearchBuf);
  tft.drawFastVLine(9+logsSearchLen*6,42,18,TFT_WHITE);
}
void appendLogsSearchChar(char c){
  if(logsSearchLen<23){ logsSearchBuf[logsSearchLen++]=c; logsSearchBuf[logsSearchLen]=0; }
  drawLogsSearchField();
}
void deleteLogsSearchChar(){
  if(logsSearchLen>0){ logsSearchBuf[--logsSearchLen]=0; }
  drawLogsSearchField();
}

// Sub-tela de busca (logsBuscando==true) -- mesmo teclado virtual generico
// (drawKeyboard()) ja usado em WiFi, so' que escrevendo em logsSearchBuf
// em vez de passBuf/manualSSID. handleLogsSearchKbTap() (mais abaixo, perto
// de handleTap) espelha handleKbTap() com o mesmo grid de teclas, mas o "OK"
// aplica o filtro nos logs em vez de conectar no WiFi.
void drawLogsSearchScreen(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(90,6);tft.print("Buscar");
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(5,32);tft.print("Texto ou data (ex: 09-22):");
  drawLogsSearchField();
  drawKeyboard();
}

static uint16_t logCorCategoria(const char* c){
  if(!strcmp(c,"Erro")) return tft.color565(255,100,100);
  if(!strcmp(c,"Alerta")) return tft.color565(255,190,80);
  if(!strcmp(c,"Atuador")) return tft.color565(110,200,255);
  if(!strcmp(c,"Automacao")) return tft.color565(120,230,140);
  if(!strcmp(c,"Boot")||!strcmp(c,"Sistema")) return tft.color565(190,160,255);
  if(!strcmp(c,"WiFi")) return tft.color565(90,170,255);
  return TFT_WHITE;
}
void drawLogsPageFooter(){   // rotulo "< 2/5 >" na faixa do topo (pulsa)
  uint16_t bg=tft.color565(8,8,26); int totalPag=logsTotalPaginas();
  tft.fillRect(96,0,110,18,bg);
  char b[20]; snprintf(b,sizeof(b),"< %d/%d >",logsFiltradoCount==0?0:(logsPage+1),totalPag);
  tft.setTextSize(1); tft.setTextColor(logsArrowOn?cLogo:tft.color565(120,100,30),bg); tft.setCursor(150-tft.textWidth(b)/2,5); tft.print(b);
}
static void logsChip(int x,int w,const char* rot,const char* val,bool ativo){
  uint16_t bg=ativo?tft.color565(40,30,10):tft.color565(16,16,46), br=ativo?tft.color565(200,150,40):tft.color565(46,46,100);
  tft.fillRoundRect(x,20,w,16,8,bg); tft.drawRoundRect(x,20,w,16,8,br);
  tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(x+8,24); tft.print(rot);
  tft.setTextColor(ativo?tft.color565(255,210,120):TFT_WHITE,bg); tft.setCursor(x+8+tft.textWidth(rot)+4,24); tft.print(val);
}
void drawLogsScreen(){
  if(logsBuscando){ drawLogsSearchScreen(); return; }

  tft.fillScreen(cBg);
  uiTopo("LOGS");
  drawSearchIcon(258,10, logsSearchAtivoLen>0?cLogo:TFT_WHITE);
  drawTrashIcon(288,4, sdOK?TFT_WHITE:tft.color565(60,60,60));

  if(logsConfirmandoLimpar){
    tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(1);tft.setCursor(20,70);
    tft.print("Apagar todo o log local do SD?");
    tft.setTextColor(cSub,cBg);tft.setCursor(20,86);
    tft.print("(nao afeta o historico ja sincronizado no app)");
    tft.fillRoundRect(30,120,110,40,6,tft.color565(52,14,14));
    tft.setTextColor(TFT_WHITE,tft.color565(52,14,14));tft.setTextSize(2);tft.setCursor(48,132);tft.print("Apagar");
    tft.fillRoundRect(180,120,110,40,6,tft.color565(16,16,46));
    tft.setTextColor(TFT_WHITE,tft.color565(16,16,46));tft.setTextSize(2);tft.setCursor(210,132);tft.print("Voltar");
    return;
  }
  if(!sdOK){
    tft.setTextColor(cRed,cBg);tft.setTextSize(1);tft.setCursor(20,60);tft.print("Cartao SD indisponivel.");
    tft.setTextColor(cSub,cBg);tft.setCursor(20,78);tft.print("Verifique fiacao/SD_CS_PIN -- ver Serial no boot.");
    return;
  }
  drawLogsPageFooter();
  { char dv[12]="Todas"; if(logsFiltroDia>=0){ int d=logsDatas[logsFiltroDia]; snprintf(dv,sizeof(dv),"%02d/%02d",(d/100)%100,d%100); }
    logsChip(4,150,"Data:",dv,logsFiltroDia>=0); }
  logsChip(162,154,"Tipo:",logsFiltroTipo>=0?logsTipos[logsFiltroTipo]:"Todos",logsFiltroTipo>=0);

  tft.setTextSize(1);
  if(logsFiltradoCount==0){
    tft.setCursor(20,70);tft.setTextColor(cSub,cBg);
    tft.print(logsBufCount==0 ? "Log vazio ainda." : "Nenhuma linha bate com os filtros.");
  }else{
    int y=40;
    int inicio = logsPage*LOGS_LINHAS_TELA;
    int fim = min(inicio+LOGS_LINHAS_TELA, logsFiltradoCount);
    for(int i=inicio;i<fim;i++){
      const String& l = logsBufLinhas[logsFiltradoIdx[i]];
      LogTs t=logParse(l);
      if(t.ok){
        char hb[20]; snprintf(hb,sizeof(hb),"%02d/%02d %02d:%02d:%02d",(t.ymd/100)%100,t.ymd%100,t.hh,t.mi,t.ss);
        tft.setTextColor(cSub,cBg); tft.setCursor(4,y); tft.print(hb);
        String m=l.substring(t.msgIdx>0?t.msgIdx:0); if(m.length()>36) m=m.substring(0,36);
        tft.setTextColor(logCorCategoria(t.cat),cBg); tft.setCursor(92,y); tft.print(t.cat[0]?t.cat:"");
        int cw=tft.textWidth(t.cat[0]?t.cat:"")+6;
        tft.setTextColor(TFT_WHITE,cBg); tft.setCursor(92+cw,y); String mm=m; int maxc=(228-cw)/6; if((int)mm.length()>maxc) mm=mm.substring(0,maxc); tft.print(mm);
      }else{
        String r=l; if(r.length()>52) r=r.substring(0,52); tft.setTextColor(cSub,cBg); tft.setCursor(4,y); tft.print(r);
      }
      y+=14;
    }
  }
}

// Abre a tela de Logs "do zero": recarrega o buffer do SD e limpa
// qualquer filtro/pagina de uma visita anterior. Ponto de entrada unico
// (chamado de Configuracoes) -- redrawCurrentScreen() (acordar do
// screensaver) so' chama drawLogsScreen() direto, sem recarregar nada.
void abrirTelaLogs(){
  logsConfirmandoLimpar=false;
  logsBuscando=false;
  logsSearchBuf[0]=0; logsSearchLen=0;
  logsSearchAtivo[0]=0; logsSearchAtivoLen=0;
  carregarLogsBuffer();
  appState=S_LOGS;
  drawLogsScreen();
}

// Fase 22 — badge "EM DESENVOLVIMENTO", mesmo estilo visual usado no botao
// "Automacao" das Configuracoes (fundo/borda amarelados).
void drawEmDevBadge(int x,int y){
  tft.fillRoundRect(x,y,150,18,9,tft.color565(50,40,10));
  tft.drawRoundRect(x,y,150,18,9,tft.color565(200,150,40));
  tft.setTextColor(tft.color565(255,210,120),tft.color565(50,40,10));
  tft.setTextSize(1);tft.setCursor(x+8,y+5);tft.print("EM DESENVOLVIMENTO");
}

// Tela Audio (Configuracoes -> Audio). Estrutura de UI real (nao so um
// texto), preparada pro modulo MAX98357 (ja reservado no CH6 do rele) --
// mas tudo desenhado como desativado ate a logica de verdade existir.
// Nenhum toque nos controles faz nada (so o botao Voltar funciona), de
// proposito -- honesto sobre nao ser funcional ainda.
void drawAudioScreen(){
  tft.fillScreen(cBg); uiTopo("AUDIO");
  uint16_t cb=tft.color565(16,16,46);
  drawEmDevBadge(160,22);
  uiCard(6,44,308,40); tft.setTextSize(2);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,56);tft.print("Audio");
  drawToggle(236,50,false,cGreen);
  uiCard(6,90,308,52); tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,98);tft.print("Volume");
  tft.drawRoundRect(16,116,286,14,7,tft.color565(60,60,60));
  tft.fillRoundRect(16,116,130,14,7,tft.color565(60,60,60));
  tft.fillCircle(146,123,9,TFT_WHITE);tft.drawCircle(146,123,9,tft.color565(190,190,190));
  uiCard(6,148,308,40); tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,162);tft.print("Som de alerta");
  tft.fillRoundRect(216,156,88,24,6,tft.color565(30,30,50)); tft.drawRoundRect(216,156,88,24,6,tft.color565(60,60,90));
  tft.setTextColor(cSub,tft.color565(30,30,50));tft.setCursor(232,164);tft.print("Testar");
  tft.setTextColor(tft.color565(110,110,150),cBg);tft.setCursor(10,200);tft.print("Aguardando modulo MAX98357A + alto-falante.");
  tft.setCursor(10,212);tft.print("(CH6 do rele fica livre; o modulo usa 3 pinos I2S)");
}

// Tela Alertas (Configuracoes -> Alertas). Mostra as faixas fixas (mesmas
// do card/tela de sensor) como referencia, e um toggle de notificacao
// desativado -- preparacao pra quando existir "Perfil de Incubacao"
// configuravel (ver Automacao).
void pollActuatorsFromFirestore(){
  fbPollIssuedAtMs = millis();
  // "comandoDesligar"/"comandoReiniciar" no mesmo poll (2.5s) dos atuadores
  // -- reage rápido sem precisar de um poll dedicado só pra isso.
  GetDocumentOptions options(DocumentMask("atuadores,comandoDesligar,comandoReiniciar,arquivoReq,cmd"));
  fbDocs.get(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
             options, fbGetCallback, "fbGetTask");
}

// ═══════════════════════════════════════════════════════════════
// CICLO DE INCUBAÇÃO — leitura (nunca escreve, isso continua sendo do PWA)
// dos campos cicloIniciadoEm/cicloPausadoEm/cicloDuracaoDias, só pra
// alimentar o card "Ciclo" na home (globals/drawCicloCard no topo do
// arquivo). Poll bem mais espaçado que o de atuadores — ciclo muda raro
// (iniciar/pausar/retomar/parar). Subido de 15s -> 30s (2026-09-20): três
// operações periódicas (poll atuadores 4s, heartbeat 30s, poll ciclo)
// competindo pelo mesmo cliente async (fbClient) é suspeito de atrasar o
// heartbeat o suficiente pra passar do limite de "stale" no PWA de vez em
// quando — ciclo não precisa ser mais frequente que o próprio heartbeat.
// ═══════════════════════════════════════════════════════════════
#define FB_CICLO_POLL_INTERVAL_MS 30000   // ativo; parado = 4x (fbCicloIntervaloMs)
unsigned long fbLastCicloPollMs = 0;
static unsigned long fbCicloIntervaloMs(){ return pwaAtivo()?FB_CICLO_POLL_INTERVAL_MS:4UL*FB_CICLO_POLL_INTERVAL_MS; }

// Extrai o valor bruto de um campo timestampValue (RFC3339, ex.
// "2026-09-20T18:52:49.123Z") do JSON pretty-printed da API REST do
// Firestore. Mesma técnica tolerante de applyActuatorsFromFirestore.
bool extrairCampoTimestamp(const String &json, const char* nomeCampo, String &saida){
  char keyTag[40]; snprintf(keyTag,sizeof(keyTag),"\"%s\"",nomeCampo);
  int idx = json.indexOf(keyTag);
  if(idx < 0) return false;
  int tvIdx = json.indexOf("\"timestampValue\"", idx);
  if(tvIdx < 0 || tvIdx-idx > 60) return false;
  int q1 = json.indexOf('"', tvIdx+16);
  if(q1 < 0) return false;
  int q2 = json.indexOf('"', q1+1);
  if(q2 < 0) return false;
  saida = json.substring(q1+1,q2);
  return true;
}

// Idem, pra campo integerValue (a API REST do Firestore devolve como
// STRING, ex. "integerValue": "21").
bool extrairCampoInteiro(const String &json, const char* nomeCampo, int &saida){
  char keyTag[40]; snprintf(keyTag,sizeof(keyTag),"\"%s\"",nomeCampo);
  int idx = json.indexOf(keyTag);
  if(idx < 0) return false;
  int ivIdx = json.indexOf("\"integerValue\"", idx);
  if(ivIdx < 0 || ivIdx-idx > 60) return false;
  int colon = json.indexOf(':', ivIdx);
  if(colon < 0) return false;
  int p = colon+1;
  while(p < (int)json.length() && (isspace((unsigned char)json[p]) || json[p]=='"')) p++;
  saida = json.substring(p).toInt();
  return true;
}

// Converte RFC3339 pra epoch UTC. configTime() foi chamado com offset 0
// (syncTimeAndStartFirebase), entao o "local" do sistema JA e UTC -- mktime()
// aqui devolve epoch UTC de verdade, sem conversao extra.
time_t parseRfc3339(const String &s){
  struct tm tmv={};
  int y,mo,d,h,mi,se;
  if(sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y,&mo,&d,&h,&mi,&se) != 6) return 0;
  tmv.tm_year=y-1900; tmv.tm_mon=mo-1; tmv.tm_mday=d;
  tmv.tm_hour=h; tmv.tm_min=mi; tmv.tm_sec=se;
  return mktime(&tmv);
}

// ─── Fase 42 — Tipos de ovos (CRUD) + dados extras do ciclo (nome do lote, tipo) ───
// Tipos ficam num campo string do doc da chocadeira ("tiposOvos"), formato compacto
// "nome;dias;temp;umid;umidEclosao;viragemAte|nome;..." -- facil de parsear no firmware
// (o REST do Firestore devolve arrays de mapas em JSON aninhado, bem pior de parsear
// na mao) e no PWA. Sem o campo no Firestore valem os padroes (Galinha/Codorna).
// Espelho local em LittleFS (/tipos.cfg) pra funcionar offline.
struct TipoOvo{
  char nome[20]; uint8_t dias; int16_t temp10, tempMin10, tempMax10;
  uint8_t umid, umidMin, umidMax, umidEcl, viraAte, viraPorDia, viraHH, viraMM;   // viragem: N por dia, 1a as HH:MM, ate o dia viraAte
  uint8_t ovo1, ovo2, eclDia;   // Fase 54: dia da 1a e da 2a ovoscopia (0 = nenhuma) e dia em que comeca a eclosao
  int16_t tempEcl10;            // Fase 69: temperatura IDEAL a partir do dia de eclosao (decimos de C); 0 = igual a ideal normal
};
// Padroes de fabrica (Galinha/Codorna) -- usados no 1o boot e no botao "restaurar" da edicao.
static TipoOvo tipoFabrica(int i){
  TipoOvo t; memset(&t,0,sizeof(t));
  if(i==0){ strcpy(t.nome,"Galinha"); t.dias=21; t.temp10=375; t.tempMin10=372; t.tempMax10=378; t.viraAte=17; t.ovo1=7; t.ovo2=14; t.eclDia=18; }
  else    { strcpy(t.nome,"Codorna"); t.dias=17; t.temp10=377; t.tempMin10=375; t.tempMax10=380; t.viraAte=14; t.ovo1=5; t.ovo2=10; t.eclDia=14; }
  t.umid=50; t.umidMin=45; t.umidMax=55; t.umidEcl=70; t.viraPorDia=4; t.viraHH=8; t.viraMM=0;
  t.tempEcl10=t.temp10;   // Fase 69: por padrao a temperatura nao muda na eclosao (editavel)
  return t;
}
static bool tipoFabricaPorNome(const char* n,TipoOvo* out){
  if(strcasecmp(n,"Galinha")==0){ *out=tipoFabrica(0); return true; }
  if(strcasecmp(n,"Codorna")==0){ *out=tipoFabrica(1); return true; }
  return false;
}
#define TIPOS_MAX 8
TipoOvo tipos[TIPOS_MAX];
int tiposN=0;
unsigned long lastLocalTiposMs=0;   // "logical clock" (mesmo padrao do ciclo/atuadores): toque local mais novo que o poll -> ignora o poll
char fbCicloNome[24]="", fbCicloTipo[20]="";

// Fonte do TFT nao tem acentos: dobra UTF-8 latino (a maioria do PT-BR) pra ASCII.
String asciiFold(const String& in){
  static const char* U="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  String out; out.reserve(in.length());
  for(unsigned i=0;i<in.length();i++){
    uint8_t c=in[i];
    if(c<0x80) out+=(char)c;
    else if(c==0xC3 && i+1<in.length()){ uint8_t d=in[++i]; out+=(d>=0x80&&d<=0xBF)?U[d-0x80]:'?'; }
    else if((c&0xC0)==0x80) {}          // byte de continuacao solto: ignora
    else out+='?';
  }
  return out;
}

void tiposDefaults(){
  tiposN=2; tipos[0]=tipoFabrica(0); tipos[1]=tipoFabrica(1);
}
String tiposSerialize(){
  String s; char b[190];
  for(int i=0;i<tiposN;i++){
    const TipoOvo& t=tipos[i];
    snprintf(b,sizeof(b),"%s;%d;%.1f;%d;%d;%d;%.1f;%.1f;%d;%d;%d;%d;%d;%d;%d;%d;%.1f",t.nome,t.dias,t.temp10/10.0f,t.umid,t.umidEcl,t.viraAte,
             t.tempMin10/10.0f,t.tempMax10/10.0f,t.umidMin,t.umidMax,t.viraPorDia,t.viraHH,t.viraMM,t.ovo1,t.ovo2,t.eclDia,(t.tempEcl10>0?t.tempEcl10:t.temp10)/10.0f);
    if(i) s+='|';
    s+=b;
  }
  return s;
}
// Formato: nome;dias;temp;umid;umidEcl;viraAte;tempMin;tempMax;umidMin;umidMax;viraPorDia;viraHH;viraMM;ovo1;ovo2;eclDia;tempEcl
// (registros antigos com so 6, 13 ou 16 campos continuam validos: o resto ganha valores derivados)
bool tiposParse(const String& srcRaw){
  String src=asciiFold(srcRaw);
  TipoOvo tmp[TIPOS_MAX]; int n=0; int pos=0;
  while(pos<=(int)src.length() && n<TIPOS_MAX){
    int fim=src.indexOf('|',pos); if(fim<0) fim=src.length();
    String reg=src.substring(pos,fim); pos=fim+1;
    int p1=reg.indexOf(';');
    if(p1>0){
      String nome=reg.substring(0,p1); nome.trim();
      int dias=0,umid=0,umidEcl=0,vira=0,umin=0,umax=0,porDia=4,hh=8,mm=0,o1=0,o2=0,ecl=0; float temp=0,tmin=0,tmax=0,tecl=0;
      int got=sscanf(reg.c_str()+p1+1,"%d;%f;%d;%d;%d;%f;%f;%d;%d;%d;%d;%d;%d;%d;%d;%f",&dias,&temp,&umid,&umidEcl,&vira,&tmin,&tmax,&umin,&umax,&porDia,&hh,&mm,&o1,&o2,&ecl,&tecl);
      if(got>=5 && nome.length()>0 && dias>=1 && dias<=60){
        TipoOvo& t=tmp[n]; memset(&t,0,sizeof(t));
        strlcpy(t.nome,nome.c_str(),sizeof(t.nome));
        t.dias=dias; t.temp10=(int16_t)lroundf(temp*10); t.umid=umid; t.umidEcl=umidEcl; t.viraAte=vira>dias?dias:vira;
        if(got>=12){ t.tempMin10=(int16_t)lroundf(tmin*10); t.tempMax10=(int16_t)lroundf(tmax*10); t.umidMin=umin; t.umidMax=umax; t.viraPorDia=porDia<1?1:(porDia>24?24:porDia); t.viraHH=hh%24; t.viraMM=mm%60; }
        if(got>=15){ t.ovo1=o1>dias?dias:o1; t.ovo2=o2>dias?dias:o2; t.eclDia=ecl>dias?dias:ecl; }
        else { t.ovo1=dias/3; t.ovo2=(dias*2)/3; t.eclDia=dias>3?dias-3:dias; }   // registro antigo: marcos derivados
        t.tempEcl10=(got>=16&&tecl>0)?(int16_t)lroundf(tecl*10):t.temp10;   // Fase 69
        if(got<12){ t.tempMin10=t.temp10-3; t.tempMax10=t.temp10+3; t.umidMin=umid>5?umid-5:0; t.umidMax=umid+5>100?100:umid+5; t.viraPorDia=4; t.viraHH=8; t.viraMM=0; }
        n++;
      }
    }
    if(fim>=(int)src.length()) break;
  }
  if(n==0) return false;
  memcpy(tipos,tmp,sizeof(TipoOvo)*n); tiposN=n;
  return true;
}
void tiposSalvarCfg(){
  File f=LittleFS.open("/tipos.cfg","w"); if(!f) return; f.print(tiposSerialize()); f.close();
}
void tiposCarregarCfg(){
  tiposDefaults();
  if(!LittleFS.exists("/tipos.cfg")) return;
  File f=LittleFS.open("/tipos.cfg","r"); if(!f) return;
  String s=f.readString(); f.close();
  tiposParse(s);   // arquivo corrompido -> mantem os padroes
}

// Fase 43 -- os limites (faixa ideal) de temperatura/umidade dos cards, graficos e alertas
// vem do TIPO do ciclo ativo (minima/maxima do tipo). Sem ciclo (ou tipo apagado): faixa geral.
const TipoOvo* cicloTipoAtualPtr(){   // tipo do ciclo ativo (nullptr = sem ciclo ou tipo desconhecido)
  if(!fbCicloAtivo||!fbCicloTipo[0]) return nullptr;
  for(int i=0;i<tiposN;i++) if(strcasecmp(tipos[i].nome,fbCicloTipo)==0) return &tipos[i];
  return nullptr;
}
bool cicloComTipoValido(){ return cicloTipoAtualPtr()!=nullptr; }
// Fase 79 -- sem ciclo, o aparelho MANTEM o ambiente do ULTIMO tipo (temperatura/umidade/fans) ate o usuario parar pelo card Status.
// Antes: ao terminar/cancelar o ciclo a automacao desligava e a caixa esfriava. Flag manterAtivo viaja em cmd.auto ("M,0|1").
static const TipoOvo* manterTipoPtr(){
  if(!ultTipoNome[0]) return nullptr;
  for(int i=0;i<tiposN;i++) if(strcasecmp(tipos[i].nome,ultTipoNome)==0) return &tipos[i];
  return nullptr;
}
bool ambManterOn(){ return !fbCicloAtivo && manterAtivo && manterTipoPtr()!=nullptr; }
bool ultTipoValido(){ return manterTipoPtr()!=nullptr; }
static void ultSalvar(){ File f=LittleFS.open("/ult.cfg","w"); if(!f) return; f.println(ultTipoNome); f.close(); }
static void ultCarregar(){ File f=LittleFS.open("/ult.cfg","r"); if(!f) return; String t=f.readStringUntil('\n'); f.close(); t.trim(); strlcpy(ultTipoNome,t.c_str(),sizeof(ultTipoNome)); }

FaseCiclo faseCicloAtual(){
  const TipoOvo* t=cicloTipoAtualPtr(); if(!t) return FASE_SEM_CICLO;
  int dia=calcularDiaCicloAtual();
  if(t->eclDia>0 && dia>=t->eclDia) return FASE_ECLOSAO;
  if(t->eclDia>0 && dia>=t->eclDia-2) return FASE_PRE_ECLOSAO;
  return FASE_INCUBACAO;
}
const char* faseCicloNome(FaseCiclo f){
  switch(f){ case FASE_INCUBACAO: return "Incubacao"; case FASE_PRE_ECLOSAO: return "Pre-eclosao"; case FASE_ECLOSAO: return "Eclosao"; default: return "Sem ciclo"; }
}
void aplicarLimitesDoTipo(){
  TEMP_IDEAL_MIN=37.2f; TEMP_IDEAL_MAX=38.9f; UMID_IDEAL_MIN=45.0f; UMID_IDEAL_MAX=65.0f;
  const TipoOvo* t=cicloTipoAtualPtr(); if(!t && ambManterOn()) t=manterTipoPtr(); if(!t) return;
  TEMP_IDEAL_MIN=t->tempMin10/10.0f; TEMP_IDEAL_MAX=t->tempMax10/10.0f;
  UMID_IDEAL_MIN=t->umidMin; UMID_IDEAL_MAX=t->umidMax;
  // Fase 54: na ECLOSAO (a partir do dia eclDia) a faixa de umidade passa a girar em torno da umidade de eclosao
  int dia=calcularDiaCicloAtual();
  if(t->eclDia>0 && t->umidEcl>0 && dia>=t->eclDia){ UMID_IDEAL_MIN=t->umidEcl-5; UMID_IDEAL_MAX=(t->umidEcl+10>100)?100:t->umidEcl+10; }
  // Fase 69: a faixa de temperatura acompanha a temperatura de eclosao (mesma largura min/max do tipo, deslocada)
  if(t->eclDia>0 && t->tempEcl10>0 && dia>=t->eclDia){ float d=(t->tempEcl10-t->temp10)/10.0f; TEMP_IDEAL_MIN+=d; TEMP_IDEAL_MAX+=d; }
}
// Fase 115 -- faixas de CO2 (ppm) por fase do ciclo: ok = verde ate okMax; alerta acima de altoMin.
// Incubacao: <= 2000 ok, > 3000 alto. Pre-eclosao/eclosao (metabolismo do embriao no pico): <= 3000 ok, > 4500 alto.
// (o sensor satura em 5000 ppm). Sao valores conservadores de literatura de incubacao; ajustaveis aqui.
void co2Limites(float& okMax,float& altoMin){
  FaseCiclo f=faseCicloAtual();
  if(f==FASE_ECLOSAO||f==FASE_PRE_ECLOSAO){ okMax=3000.0f; altoMin=4500.0f; } else { okMax=2000.0f; altoMin=3000.0f; }
}
bool cicloTelaSuja=false;   // Fase 42 -- poll mudou algo enquanto a tela do ciclo esta aberta
void applyCicloFromFirestore(const String &json, unsigned long pollIssuedAtMs){
  // Corrida corrigida (Fase 12): se uma ação local (Iniciar/Pausar/Retomar/
  // Parar) aconteceu DEPOIS que este poll foi disparado, este JSON é uma
  // foto de ANTES da ação -- aplicá-lo desfaria a ação que já rolou. O
  // próximo poll (disparado depois da ação) vai refletir o valor certo.
  if(debugCicloFalso) return;
  if(lastLocalCicloActionMs >= pollIssuedAtMs) return;
  cicloPollRecebido = true;

  String iniciadoStr, pausadoStr;
  bool temIniciado = extrairCampoTimestamp(json, "cicloIniciadoEm", iniciadoStr);
  bool temPausado  = extrairCampoTimestamp(json, "cicloPausadoEm", pausadoStr);
  int duracao=21; extrairCampoInteiro(json, "cicloDuracaoDias", duracao);

  // Fase 79 -- o "Parar" feito aqui nao chegou na nuvem (Firebase nao estava pronto): a nuvem ainda tem o ciclo. Nao ressuscita; reenvia.
  if(cicloParouMs){
    if(!temIniciado) cicloParouMs=0;
    else if(millis()-cicloParouMs<90000UL){ cicloSyncPend=true; Serial.println("[Ciclo] a nuvem ainda tem o ciclo apos Parar: reenviando"); return; }
    else cicloParouMs=0;
  }
  fbCicloAtivo = temIniciado;
  fbCicloPausado = temPausado;
  fbCicloIniciadoEm = temIniciado ? parseRfc3339(iniciadoStr) : 0;
  fbCicloPausadoEm  = temPausado  ? parseRfc3339(pausadoStr)  : 0;
  fbCicloDuracaoDias = duracao>0 ? duracao : 21;

  // Fase 42 -- nome do lote, tipo e lista de tipos de ovos
  String sv;
  if(temIniciado && extrairCampoString(json,"cicloNome",sv))     strlcpy(fbCicloNome,asciiFold(sv).c_str(),sizeof(fbCicloNome)); else fbCicloNome[0]=0;
  if(temIniciado && extrairCampoString(json,"cicloTipoNome",sv)) strlcpy(fbCicloTipo,asciiFold(sv).c_str(),sizeof(fbCicloTipo)); else fbCicloTipo[0]=0;
  if(lastLocalTiposMs < pollIssuedAtMs && extrairCampoString(json,"tiposOvos",sv)){ if(tiposParse(sv)) tiposSalvarCfg(); }
  fbCicloPreparo=false; fbCicloVolL=0;   // Fase 77: "preparo;litros" -- preparo=1 enquanto espera o usuario colocar os ovos
  if(temIniciado && extrairCampoString(json,"cicloExtra",sv)){ int pp=0,vv=0; if(sscanf(sv.c_str(),"%d;%d",&pp,&vv)>=1){ fbCicloPreparo=(pp==1); fbCicloVolL=vv>0?vv:0; } }
  if(temIniciado && fbCicloTipo[0] && strcasecmp(fbCicloTipo,ultTipoNome)!=0){ strlcpy(ultTipoNome,fbCicloTipo,sizeof(ultTipoNome)); ultSalvar(); }
  { static bool estavaAtivo=false, antLog=false;
    if(estavaAtivo && !temIniciado && ultTipoNome[0] && !manterAtivo){ manterAtivo=true; manterSyncPend=true; Serial.println("[Ciclo] terminou: mantendo o ambiente do ultimo tipo"); }
    estavaAtivo=temIniciado;
    if(temIniciado!=antLog){ antLog=temIniciado; Serial.printf("[Ciclo] poll: ciclo ativo=%d preparo=%d\n",(int)temIniciado,(int)fbCicloPreparo); } }
  aplicarLimitesDoTipo();
  if(appState==S_CICLO) cicloTelaSuja=true;

  refreshCicloCardSeVisivel();
}

// Fase 92 -- CICLO LOCAL. O ciclo so existia na nuvem (poll do Firestore): sem internet no boot a tela inicial mostrava "Ciclo --" e o aparelho
// esquecia que havia ovos (dia do ciclo, faixa de temperatura do tipo). Agora o estado do ciclo e gravado em /ciclo.cfg quando muda e restaurado no boot;
// o primeiro poll da nuvem (cicloPollRecebido) continua mandando: se o ciclo acabou/mudou no app, ele corrige.
#define CICLO_CFG_PATH "/ciclo.cfg"
static String cicloAssinatura(){
  char b[220]; snprintf(b,sizeof(b),"%d|%d|%d|%d|%ld|%ld|%d|%s|%s",(int)fbCicloAtivo,(int)fbCicloPausado,(int)fbCicloPreparo,fbCicloVolL,(long)fbCicloIniciadoEm,(long)fbCicloPausadoEm,fbCicloDuracaoDias,fbCicloNome,fbCicloTipo);
  return String(b);
}
static String cicloUltAssin;
static void cicloSalvarLocal(){
  File f=LittleFS.open(CICLO_CFG_PATH,"w"); if(!f) return;
  f.printf("%d\n%d\n%d\n%d\n%ld\n%ld\n%d\n",(int)fbCicloAtivo,(int)fbCicloPausado,(int)fbCicloPreparo,fbCicloVolL,(long)fbCicloIniciadoEm,(long)fbCicloPausadoEm,fbCicloDuracaoDias);
  String n=String(fbCicloNome), t=String(fbCicloTipo); n.replace('\n',' '); t.replace('\n',' ');
  f.println(n); f.println(t); f.close();
}
void cicloRestaurar(){
  File f=LittleFS.open(CICLO_CFG_PATH,"r");
  if(f){
    String l[9]; int n=0; while(f.available() && n<9){ l[n]=f.readStringUntil('\n'); l[n].trim(); n++; } f.close();
    if(n>=7 && l[0]=="1" && l[4].toInt()>1704067200L){
      fbCicloAtivo=true; fbCicloPausado=(l[1]=="1"); fbCicloPreparo=(l[2]=="1"); fbCicloVolL=l[3].toInt();
      fbCicloIniciadoEm=(time_t)l[4].toInt(); fbCicloPausadoEm=(time_t)l[5].toInt(); fbCicloDuracaoDias=l[6].toInt()>0?l[6].toInt():21;
      if(n>=8) strlcpy(fbCicloNome,l[7].c_str(),sizeof(fbCicloNome)); if(n>=9) strlcpy(fbCicloTipo,l[8].c_str(),sizeof(fbCicloTipo));
      aplicarLimitesDoTipo();
      Serial.printf("[Ciclo] restaurado do aparelho (sem esperar a nuvem): \"%s\" tipo %s, %d dias\n",fbCicloNome,fbCicloTipo,fbCicloDuracaoDias);
    }
  }
  cicloUltAssin=cicloAssinatura();
}
void cicloPersistTick(){   // chamado do loop(); grava so quando algo mudou
  static unsigned long ult=0; if(millis()-ult<15000UL) return; ult=millis();
  if(debugCicloFalso) return;
  String a=cicloAssinatura(); if(a==cicloUltAssin) return;
  cicloUltAssin=a; cicloSalvarLocal();
}

void fbCicloGetCallback(AsyncResult &aResult){
  if(!aResult.isResult()) return;
  if(aResult.isError()){
    Serial.printf("[Firebase Ciclo ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
    return;
  }
  if(!aResult.available()) return;
  applyCicloFromFirestore(aResult.c_str(), fbCicloPollIssuedAtMs);
}

void pollCicloFromFirestore(){
  fbCicloPollIssuedAtMs = millis();
  GetDocumentOptions options(DocumentMask("cicloIniciadoEm,cicloPausadoEm,cicloDuracaoDias,cicloNome,cicloTipoNome,tiposOvos,cicloExtra"));
  fbDocs.get(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
             options, fbCicloGetCallback, "fbCicloGetTask");
}

// ═══════════════════════════════════════════════════════════════
// CONTROLE LOCAL DO CICLO (Fase 11) — até aqui o firmware só LIA os campos
// de ciclo; agora a tela "Ciclo" (aberta pelo card na home) também
// ESCREVE, espelhando exatamente o que o modal "Configurações do Ciclo"
// do PWA já faz. Truque de Firestore REST usado abaixo: um campo listado
// no updateMask mas AUSENTE do corpo do Document é APAGADO — é assim que
// se apaga cicloPausadoEm sem precisar de um "FieldValue.delete()" (isso
// é só do SDK JS; aqui é REST puro via FirebaseClient).
// ═══════════════════════════════════════════════════════════════

// Grava um único campo timestamp (usado por Iniciar e Pausar).
void pushCicloTimestampField(const char* campo, time_t valorUtc){
  if(!fbStarted || !fbApp.ready()) return;
  struct tm *utc = gmtime(&valorUtc);
  char ts[25]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  Document<Values::Value> doc(campo, Values::Value(Values::TimestampValue(String(ts))));
  // DocumentMask(campo) (ou DocumentMask(String(campo))) direto no
  // construtor daria "most vexing parse" (C++ interpreta como declaração
  // de função, não chamada de construtor, porque "campo" é um identificador
  // solto) -- passar por uma String já pronta evita a ambiguidade.
  String campoStr(campo);
  DocumentMask mask(campoStr);
  PatchDocumentOptions patchOptions(mask, DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbCicloTask");
}

// Duração do ciclo (stepper 15-35 dias, mesmos limites do PWA).
void pushCicloDuracao(int dias){
  if(!fbStarted || !fbApp.ready()) return;
  Document<Values::Value> doc("cicloDuracaoDias", Values::Value(Values::IntegerValue(dias)));
  PatchDocumentOptions patchOptions(DocumentMask("cicloDuracaoDias"), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbCicloTask");
}

// Retomar: seta cicloIniciadoEm (deslocado pelo tempo pausado, pra não
// pular dias) E apaga cicloPausadoEm num único patch (mascara cobre os
// dois campos; só cicloIniciadoEm está no corpo, então cicloPausadoEm
// é removido).
void pushCicloRetomar(time_t novoInicioUtc){
  if(!fbStarted || !fbApp.ready()) return;
  struct tm *utc = gmtime(&novoInicioUtc);
  char ts[25]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  Document<Values::Value> doc("cicloIniciadoEm", Values::Value(Values::TimestampValue(String(ts))));
  doc.add("cicloPausadoEm", Values::Value(Values::NullValue()));   // Fase 79: apagar = nulo
  PatchDocumentOptions patchOptions(DocumentMask("cicloIniciadoEm,cicloPausadoEm"), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbCicloTask");
}

// Parar: apaga cicloIniciadoEm e cicloPausadoEm (corpo vazio -- Document()
// sem nenhum .add() -- os dois ficam só na mascara, então os dois somem).
void pushCicloParar(){
  Serial.printf("[Ciclo] Parar -> nuvem (pronto=%d)\n",(int)(fbStarted && fbApp.ready()));
  if(!fbStarted || !fbApp.ready()) return;
  // Fase 79: o corpo vazio + mascara NAO apagava os campos nesta biblioteca (a nuvem seguia com o ciclo e ele "voltava" em <1 min).
  // Valor nulo apaga na pratica: o aparelho (extrairCampo*) e o PWA tratam null como campo ausente.
  Document<Values::Value> doc("cicloIniciadoEm", Values::Value(Values::NullValue()));
  doc.add("cicloPausadoEm", Values::Value(Values::NullValue()));
  doc.add("cicloDuracaoDias", Values::Value(Values::NullValue()));
  doc.add("cicloNome", Values::Value(Values::NullValue()));
  doc.add("cicloTipoNome", Values::Value(Values::NullValue()));
  doc.add("cicloExtra", Values::Value(Values::NullValue()));
  PatchDocumentOptions patchOptions(DocumentMask("cicloIniciadoEm,cicloPausadoEm,cicloDuracaoDias,cicloNome,cicloTipoNome,cicloExtra"), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbCicloTask");
}

// Fase 42 — Iniciar ciclo (wizard): grava inicio escolhido, duracao, nome do lote e tipo;
// cicloPausadoEm fica so na mascara (corpo sem ele) -> e apagado, ciclo comeca rodando.
void pushCicloIniciar(const char* nome,const char* tipo,int dias,time_t inicioUtc,const char* extra=""){
  if(!fbStarted || !fbApp.ready()) return;
  struct tm *utc = gmtime(&inicioUtc);
  char ts[25]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  Document<Values::Value> doc("cicloIniciadoEm", Values::Value(Values::TimestampValue(String(ts))));
  doc.add("cicloDuracaoDias", Values::Value(Values::IntegerValue(dias)));
  doc.add("cicloNome", Values::Value(Values::StringValue(String(nome))));
  doc.add("cicloTipoNome", Values::Value(Values::StringValue(String(tipo))));
  doc.add("cicloExtra", Values::Value(Values::StringValue(String(extra))));   // Fase 77: "preparo;litros"
  doc.add("cicloPausadoEm", Values::Value(Values::NullValue()));   // Fase 79: ciclo novo comeca rodando (apagar = nulo)
  PatchDocumentOptions patchOptions(DocumentMask("cicloIniciadoEm,cicloPausadoEm,cicloDuracaoDias,cicloNome,cicloTipoNome,cicloExtra"), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbCicloTask");
}
// Fase 60 -- ciclo funciona SEM internet: a acao local vale na hora e fica "pendente"; quando o WiFi/nuvem voltam o estado inteiro e reenviado
bool cicloSyncPend=false;
static unsigned long cicloSyncTentouMs=0;
void cicloReenviarEstado(){
  lastLocalCicloActionMs=millis();
  if(fbCicloAtivo){
    { char ex[16]; snprintf(ex,sizeof(ex),"%d;%d",fbCicloPreparo?1:0,fbCicloVolL); pushCicloIniciar(fbCicloNome,fbCicloTipo,fbCicloDuracaoDias,fbCicloIniciadoEm,ex); }
    if(fbCicloPausado) pushCicloTimestampField("cicloPausadoEm",fbCicloPausadoEm);
  }else pushCicloParar();
}
void cicloSyncTick(){
  if(!cicloSyncPend||WiFi.status()!=WL_CONNECTED||!fbStarted||!fbApp.ready()) return;
  if(millis()-cicloSyncTentouMs<8000UL) return;
  cicloSyncTentouMs=millis(); cicloReenviarEstado(); cicloSyncPend=false;
  Serial.println("[Ciclo] estado local reenviado a nuvem (voltou a conexao).");
}
void pushTiposToFirestore(){
  if(!fbStarted || !fbApp.ready()) return;
  Document<Values::Value> doc("tiposOvos", Values::Value(Values::StringValue(tiposSerialize())));
  PatchDocumentOptions patchOptions(DocumentMask("tiposOvos"), DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbPatchCallback, "fbTiposTask");
}


// ═══════════════════════════════════════════════════════════════
// DESLIGAR (Fase 15) — deep sleep de verdade (esp_deep_sleep_start),
// não um "apagar tela" por software como o screensaver. CPU, WiFi, RAM
// (exceto RTC) tudo desliga; o heartbeat para de vez até acordar. Acorda
// com RST (reset de hardware normal, não precisa de nenhum código — já
// funciona "de graça") ou com o botão BOOT (GPIO0), configurado abaixo
// via ext0 wakeup. GPIO0 é pino de strapping (ver CLAUDE.md, pinout) mas
// usá-lo como fonte de wake é o padrão consagrado em projetos ESP32 —
// não conflita com o papel dele na seleção de modo de boot normal.
// ═══════════════════════════════════════════════════════════════
// Fase 67 -- efeitos reutilizados nas animacoes de Desligar/Reiniciar (mais fluidez, sem depender de
// framebuffer/alpha real: interpola a COR (RGB565) entre dois tons e redesenha a forma inteira a cada
// quadro -- e assim que o resto do app ja faz "fade" de estado, so generalizado pra virar uma sequencia).
static uint16_t lerp565(uint16_t a,uint16_t b,float t){
  if(t<0)t=0; else if(t>1)t=1;
  int ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
  int br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
  int r=ar+(int)((br-ar)*t), g=ag+(int)((bg-ag)*t), bl=ab+(int)((bb-ab)*t);
  return (uint16_t)((r<<11)|(g<<5)|bl);
}
// Anel de energia "encolhendo/apagando" (Desligar) -- 16 quadros, anel some de fora pra dentro
// enquanto a cor esmaece pro fundo, terminando bem no centro onde o icone de energia aparece.
static void efeitoAnelDesligar(uint16_t corBase){
  int cx=160,cy=90;
  for(int i=0;i<16;i++){
    float t=i/15.0f;
    int r=36-(int)(t*20);
    uint16_t c=lerp565(corBase,cBg,t*0.85f);
    tft.fillCircle(cx,cy,40,cBg);
    tft.drawArc(cx,cy,r,r-7,0,360,c,cBg,false);
    delay(16);
  }
  drawPowerIcon(cx,cy,corBase,cBg);
}
// Colapso estilo "TV antiga desligando": topo/base fecham ate virar uma linha fina, que encolhe ate um
// ponto no centro -- efeito clássico de desligamento, roda em ~500ms.
static void efeitoColapsoCRT(uint16_t corLinha){
  int cy=120;
  for(int y=0;y<=cy;y+=8){ tft.fillRect(0,0,320,y,TFT_BLACK); tft.fillRect(0,240-y,320,y,TFT_BLACK); delay(9); }
  tft.fillRect(0,cy-2,320,4,corLinha); delay(130);
  for(int w=320;w>=0;w-=20){ tft.fillRect(0,cy-2,320,4,TFT_BLACK); if(w>0){ int x0=(320-w)/2; tft.fillRect(x0,cy-2,w,4,corLinha); } delay(11); }
  tft.fillScreen(TFT_BLACK);
}
// Seta girando (Reiniciar) -- mesmo desenho do drawRestartIcon, so que animado e maior, com um pulso
// final "confirmado" antes do ESP.restart().
static void efeitoSpinnerReiniciar(uint16_t corBase){
  int cx=160,cy=90,raio=28;
  for(int f=0;f<20;f++){
    int ang=(f*48)%360, fim=ang+250;
    tft.fillCircle(cx,cy,raio+8,cBg);
    tft.drawArc(cx,cy,raio,raio-7,ang,fim,corBase,cBg,false);
    float rad=fim*3.14159265f/180.0f;
    int ax=cx+(int)(raio*sinf(rad)), ay=cy-(int)(raio*cosf(rad));
    tft.fillTriangle(ax-5,ay-2, ax+4,ay-5, ax+2,ay+5, corBase);
    delay(40);
  }
  for(int i=0;i<3;i++){ tft.fillCircle(cx,cy,raio+8,lerp565(cBg,corBase,0.55f)); delay(70); tft.fillCircle(cx,cy,raio+8,cBg); delay(70); }
  drawRestartIcon(cx,cy,corBase,cBg);
}

void entrarEmDeepSleep(){
  for(int i=0;i<8;i++) setRelay(i,false);  // seguranca, mesmo padrao do botao Reiniciar

  tft.fillScreen(cBg);
  efeitoAnelDesligar(cRed);
  tft.setTextColor(cLogo,cBg);tft.setTextSize(2);
  { const char* m="Desligando..."; tft.setCursor(160-tft.textWidth(m)/2,130); tft.print(m); }
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);
  { const char* m2="Pressione BOOT ou RST na placa para ligar"; tft.setCursor(160-tft.textWidth(m2)/2,152); tft.print(m2); }
  delay(500);
  efeitoColapsoCRT(cRed);

  // Bug crítico corrigido (Fase 17): sem isso, o GPIO0 podia ficar "flutuando"
  // durante o deep sleep e disparar o ext0_wakeup sozinho, segundos depois
  // de dormir -- do lado de fora, parecia que o dispositivo tinha reiniciado
  // em vez de desligado. Causa: o pull-up interno "normal" do GPIO (ligado
  // pelo Arduino/framework em operação normal) NÃO é preservado durante o
  // deep sleep -- o domínio de baixo consumo (RTC/LP-IO) tem sua PRÓPRIA
  // configuração de pull-up/pull-down, independente, que precisa ser
  // habilitada explicitamente pra esse pino específico antes de dormir.
  // Fase 107: durante o deep sleep os pinos digitais ficam SOLTOS. Travar (gpio_hold) os 8 reles em HIGH (desligado: LEDs do modulo apagados),
  // o backlight em LOW (tela apagada) e os PWM dos fans em LOW (pino solto = o fan acelera a 100 %).
  for(int i=0;i<8;i++){ pinMode(RELAY_PINS[i],OUTPUT); digitalWrite(RELAY_PINS[i],HIGH); gpio_hold_en((gpio_num_t)RELAY_PINS[i]); }
  { const int lo[4]={47,4,5,6}; for(int i=0;i<4;i++){ ledcDetachPin(lo[i]); pinMode(lo[i],OUTPUT); digitalWrite(lo[i],LOW); gpio_hold_en((gpio_num_t)lo[i]); } }
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // acorda quando BOOT for pressionado (nivel baixo)
  esp_deep_sleep_start();   // a tela ja esta preta (colapso acima) -- sem precisar de mais delay
}

// Acionado pelo botao "Desligar" na tela Sistema -- presenca fisica ja
// confirmada pelo toque, sem necessidade de confirmar de novo (mesmo
// padrao direto do botao "Reiniciar" ja existente).
void desligarLocal(){
  entrarEmDeepSleep();
}

// Acionado remotamente via comandoDesligar=true no Firestore (ver
// applyActuatorsFromFirestore). Precisa limpar esse campo de volta pra
// false ANTES de dormir, de forma SINCRONA (esperando a confirmacao) --
// sem isso, ao acordar o firmware leria o campo ainda "true" no proximo
// poll e entraria em deep sleep nca hora de novo, num loop sem fim.
bool fbDesligarClearOk = false;
void fbDesligarClearCallback(AsyncResult &aResult){
  if(aResult.isResult()) fbDesligarClearOk = true;
}

// Limpa comandoDesligar -> false de forma SÍNCRONA (espera a confirmação,
// bombeando fbApp.loop() manualmente, com timeout). Usada em dois pontos:
// (1) antes de dormir (desligarRemoto) e (2) logo no boot, se a causa do
// wake foi o botão físico (bug crítico corrigido — Fase 16, ver
// limparComandoDesligarSeAcordouPorBotao). Retorna false se o Firebase não
// estava pronto ou se o patch não confirmou a tempo.
bool limparComandoDesligarSincrono(){
  if(!fbStarted || !fbApp.ready()) return false;
  Document<Values::Value> doc("comandoDesligar", Values::Value(Values::BooleanValue(false)));
  PatchDocumentOptions patchOptions(DocumentMask("comandoDesligar"), DocumentMask(), Precondition());
  fbDesligarClearOk = false;
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbDesligarClearCallback, "fbDesligarClearTask");
  unsigned long t0 = millis();
  while(!fbDesligarClearOk && millis()-t0 < 5000){ fbApp.loop(); delay(10); }
  return fbDesligarClearOk;
}

void desligarRemoto(){
  limparComandoDesligarSincrono();
  entrarEmDeepSleep();
}

// Limpeza no Firestore em si (Fase 16, reforçada na Fase 18 com retries) —
// AGORA É SÓ LIMPEZA DE BOA VONTADE (higiene do dado na nuvem), não a
// garantia contra o loop: essa garantia é 100% da flag local
// desligarSuprimidoNesteBoot (ativada bem antes, ver
// ativarSupressaoDesligarSeAcordouPorBotao), que não depende de nenhuma
// rede pra funcionar. Mesmo assim, vale tentar limpar de verdade — várias
// vezes, não só uma — pra não deixar `comandoDesligar: true` esquecido no
// documento indefinidamente. Chamada uma única vez no setup(), depois que
// o WiFi/Firebase já tiveram chance de conectar.
void limparComandoDesligarSeAcordouPorBotao(){
  if(!desligarSuprimidoNesteBoot) return;  // so roda se a flag local ja foi ativada (boot por botao)

  Serial.println("[DeepSleep] Tentando confirmar a limpeza de comandoDesligar no Firestore (best-effort, ate 3 tentativas)...");
  unsigned long t0 = millis();
  while(!(fbStarted && fbApp.ready()) && millis()-t0 < 10000){ fbApp.loop(); delay(20); }

  bool ok=false;
  for(int tentativa=1; tentativa<=3 && !ok; tentativa++){
    ok = limparComandoDesligarSincrono();
    if(!ok && tentativa<3){ Serial.printf("[DeepSleep] Tentativa %d falhou, tentando de novo...\n", tentativa); delay(500); }
  }
  Serial.println(ok
    ? "[DeepSleep] comandoDesligar confirmado limpo no Firestore."
    : "[DeepSleep] Nao deu pra confirmar a limpeza no Firestore apos 3 tentativas -- sem problema, a supressao local (30s) ja garante que nao reentra em sleep de qualquer forma.");
}

// ═══════════════════════════════════════════════════════════════
// REINICIAR remoto (Fase 28) — comandoReiniciar=true no Firestore,
// ESP.restart() (software reset), mesmo espirito do bloco DESLIGAR acima
// mas o dispositivo volta sozinho em poucos segundos, sem precisar de
// nenhum botao fisico. Loop-prevention no mesmo padrao (RTC_DATA_ATTR em
// vez de causa-de-wake, ja que ESP.restart() nao passa por deep sleep).
// ═══════════════════════════════════════════════════════════════
bool fbReiniciarClearOk = false;
void fbReiniciarClearCallback(AsyncResult &aResult){
  if(aResult.isResult()) fbReiniciarClearOk = true;
}

// Limpa comandoReiniciar -> false de forma SÍNCRONA, mesmo padrão de
// limparComandoDesligarSincrono() acima.
bool limparComandoReiniciarSincrono(){
  if(!fbStarted || !fbApp.ready()) return false;
  Document<Values::Value> doc("comandoReiniciar", Values::Value(Values::BooleanValue(false)));
  PatchDocumentOptions patchOptions(DocumentMask("comandoReiniciar"), DocumentMask(), Precondition());
  fbReiniciarClearOk = false;
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH,
               patchOptions, doc, fbReiniciarClearCallback, "fbReiniciarClearTask");
  unsigned long t0 = millis();
  while(!fbReiniciarClearOk && millis()-t0 < 5000){ fbApp.loop(); delay(10); }
  return fbReiniciarClearOk;
}

void reiniciarRemoto(){
  for(int i=0;i<8;i++) setRelay(i,false);  // seguranca, mesmo padrao do Reiniciar local/Desligar

  // Fase 28c -- marcador em LittleFS (nao RTC_DATA_ATTR, ver comentario
  // grande la em cima) -- sobrevive de verdade ao ESP.restart() logo
  // abaixo. E a PRESENCA deste arquivo no proximo boot, nao a limpeza no
  // Firestore, que garante que nao reentra em loop de reboot.
  { File f = LittleFS.open(REINICIANDO_FLAG_PATH, "w"); if(f) f.close(); }

  // Fase 28b: NAO tenta mais limpar comandoReiniciar aqui antes de
  // reiniciar. A 1a versao chamava limparComandoReiniciarSincrono() (que
  // bombeia fbApp.loop() num laco bloqueante) de DENTRO desta funcao --
  // que por sua vez e chamada de dentro de applyActuatorsFromFirestore(),
  // ou seja, de dentro do CALLBACK assincrono do proprio poll. Chamar
  // fbApp.loop() de forma aninhada dentro do callback que ele mesmo
  // dispara e um padrao arriscado (o cliente async pode nao processar a
  // nova tarefa nesse contexto) -- na pratica, o campo quase certamente
  // NUNCA era limpo por aqui, sobrando 100% do trabalho pro retry
  // periodico em loop() (fora de qualquer callback, seguro). Remover essa
  // tentativa arriscada nao piora nada e simplifica o caminho critico.
  tft.fillScreen(cBg);
  efeitoSpinnerReiniciar(cLogo);
  tft.setTextColor(cLogo,cBg);tft.setTextSize(1);
  { const char* m="Reiniciando (comando remoto)..."; tft.setCursor(160-tft.textWidth(m)/2,140); tft.print(m); }
  delay(300);
  ESP.restart();
}

void startFirebase(){
  fbSsl.setInsecure();

  ServiceAuth service_auth(FIREBASE_CLIENT_EMAIL, FIREBASE_PROJECT_ID,
                            FIREBASE_PRIVATE_KEY, 3000 /* expira em 3000s */);
  fbApp.setTime(time(nullptr));
  initializeApp(fbClient, fbApp, getAuth(service_auth), fbAuthCallback, "fbAuthTask");

  fbApp.getApp<Firestore::Documents>(fbDocs);
  fbApp.getApp<Messaging>(fbMsg);   // Fase 49
}

// Bug relatado (2026-09-20): "hora incorreta mesmo com bateria" -- o RTC
// (DS3231 + bateria LIR2032 de backup) e o relogio do sistema (NTP) eram
// DOIS relogios independentes, nunca reconciliados. Se a bateria de backup
// falhar/estiver fraca (rtc.lostPower()==true), o RTC so era ajustado com
// a hora de COMPILACAO do firmware (F(__DATE__)/F(__TIME__)) -- podendo
// ficar dias/semanas errado, e nunca mais corrigido depois disso.
//
// TZ_OFFSET_SEC: o RTC e o relogio mostrado nas telas guardam HORA LOCAL
// (mesma convencao ja usada pela tela manual de Data/Hora e pelo fallback
// de hora de compilacao) -- NAO UTC. So o heartbeat/Firestore usam UTC
// puro (time(nullptr) + gmtime(), sem esse offset). Ajustar aqui se o
// dispositivo for usado fora do Brasil ou o pais adotar horario de verao.
#define TZ_OFFSET_SEC (-3L*3600L)

// Corrige o RTC a partir do NTP sempre que o relogio do sistema estiver
// valido -- assim o NTP funciona de fato como fonte de verdade quando
// tem internet, e o RTC (com bateria) so precisa segurar essa hora
// correta enquanto o dispositivo ficar offline (fallback de verdade, nao
// um relogio paralelo que nunca é corrigido). So reajusta se a diferenca
// for > 2 min (evita ficar reescrevendo o RTC a toda hora por causa da
// imprecisao normal de leitura) -- e ISSO é o sinal de integridade da
// bateria: se essa divergencia grande aparecer em TODO boot, a bateria
// de backup provavelmente esta fraca ou morta.
void syncRtcFromNtp(){
  if(!rtcOK) return;
  time_t nowUtc = time(nullptr);
  if(nowUtc < (time_t)EPOCH_2024_01_01) return;  // NTP ainda nao valido

  DateTime ntpLocal((uint32_t)(nowUtc + TZ_OFFSET_SEC));
  DateTime rtcAgora = rtc.now();
  long diffSec = (long)ntpLocal.unixtime() - (long)rtcAgora.unixtime();
  if(diffSec < 0) diffSec = -diffSec;

  if(diffSec > 120){
    Serial.printf("[RTC] Divergencia de %ld s entre RTC e NTP -- corrigindo RTC pelo NTP. "
                  "Se isso acontecer em todo boot, a bateria de backup do RTC pode estar fraca/morta.\n", diffSec);
    rtc.adjust(ntpLocal);
  }
}

// Fase 92 -- RELOGIO SEM INTERNET. Em 09/10 o WiFi estava ligado mas a internet caiu: o NTP falhou, o relogio do sistema ficou em 1970,
// o Firebase nao conseguiu assinar o JWT ("time was not set or not valid") e a biblioteca guarda a hora que recebeu em setTime() e CONTA a partir dela:
// mesmo com o NTP voltando depois, ela nunca mais se corrigia ate reiniciar. Agora:
//  (1) se o NTP falha e o RTC (DS3231, bateria ok) tem hora valida, o relogio do sistema nasce do RTC (dia do ciclo, historico, logs continuam certos);
//  (2) o callback do SNTP avisa quando o NTP de verdade chega; ai o RTC e conferido e o Firebase recebe a hora certa (fbApp.setTime).
static volatile bool ntpSincronizou=false;   // callback do SNTP
static bool relogioDoRtc=false;              // true = a hora do sistema veio do RTC e ainda nao foi confirmada pelo NTP
static void ntpNotifCb(struct timeval* tv){ (void)tv; ntpSincronizou=true; }
void relogioDoRtcAplicar(){
  if(!rtcOK || !rtcHoraConfiavel) return;
  if(time(nullptr) >= (time_t)EPOCH_2024_01_01) return;   // ja tem hora valida
  DateTime n=rtc.now(); if(n.year()<2024) return;
  struct timeval tv; tv.tv_sec=(time_t)((long)n.unixtime()-TZ_OFFSET_SEC); tv.tv_usec=0; settimeofday(&tv,nullptr);
  relogioDoRtc=true;
  Serial.printf("[Relogio] sem NTP: hora do sistema vinda do RTC (%02d/%02d/%04d %02d:%02d). O NTP corrige quando a internet voltar.\n",n.day(),n.month(),n.year(),n.hour(),n.minute());
}

// Chamado sempre que o WiFi conecta (boot ou manualmente pela tela) —
// so inicia o Firebase uma vez (fbStarted evita reinício em reconexões).
void syncTimeAndStartFirebase(){
  if(fbStarted) return;
  Serial.print("Sincronizando hora (NTP) para o Firebase");
  ntpSincronizou=false; sntp_set_time_sync_notification_cb(ntpNotifCb);
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  int tries=0;
  while(!ntpSincronizou && tries<20){ delay(500); Serial.print("."); tries++; }   // (nao usa getLocalTime: com a hora do RTC ele daria "ok" sem NTP)
  Serial.println();
  if(ntpSincronizou){ relogioDoRtc=false; syncRtcFromNtp(); }
  else relogioDoRtcAplicar();
  startFirebase();
  fbStarted = true;
}

// Se o NTP falhar na tentativa unica do boot (ex: DNS lento, internet
// ainda instavel logo apos conectar no WiFi), o relogio do sistema fica
// preso no epoch (1970) PRA SEMPRE -- syncTimeAndStartFirebase() so roda
// uma vez (guard fbStarted) e nunca mais tenta de novo. Isso faz TODO
// heartbeat futuro ficar com timestamp de 1970, que o PWA sempre vai ver
// como "extremamente antigo" -> "chocadeira offline", mesmo com o
// dispositivo ligado e enviando heartbeats normalmente (bug relatado
// pelo usuario em 2026-09-20). Chamado do loop() a cada 30s, so continua
// tentando enquanto o relogio ainda parecer invalido OU vier so do RTC.
void ntpWatchdog(){
  if(!wifiOK) return;
  bool valido = time(nullptr) >= (time_t)EPOCH_2024_01_01;
  if(valido && !relogioDoRtc) return;   // relogio ja confirmado, nada a fazer
  if(relogioDoRtc && ntpSincronizou){   // Fase 92: o NTP de verdade chegou
    relogioDoRtc=false; syncRtcFromNtp(); fbApp.setTime(time(nullptr));
    Serial.println("[NTP] Internet voltou: relogio confirmado pelo NTP e Firebase reajustado."); return;
  }
  Serial.println("[NTP] Relogio ainda nao confirmado -- tentando ressincronizar...");
  sntp_set_time_sync_notification_cb(ntpNotifCb);
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  if(relogioDoRtc){ Serial.println("[NTP] Sem resposta ainda (usando a hora do RTC) -- tenta de novo no proximo ciclo."); return; }
  struct tm ti;
  if(getLocalTime(&ti, 2000)){ Serial.println("[NTP] Relogio ressincronizado com sucesso."); syncRtcFromNtp(); fbApp.setTime(time(nullptr)); }
  else Serial.println("[NTP] Ainda falhou -- tenta de novo no proximo ciclo.");
}

// Sem isso, nada no loop() percebia se o radio WiFi caisse sozinho (ex:
// interferencia do rele de 220V chaveando por perto) -- wifiOK ficava
// "true" pra sempre (so e setado no boot ou nas telas manuais de WiFi),
// e o poll/heartbeat do Firebase simplesmente parava de ser enviado, sem
// nenhuma tentativa de reconectar. Do ponto de vista do usuario: o
// dispositivo continua funcionando localmente (touchscreen, reles), so o
// WiFi que caiu -- exatamente o cenario que faz o PWA mostrar "chocadeira
// offline" quando ela nao esta, de fato, "desligada". Chamado do loop()
// a cada 10s.
bool debugWifiOff=false;   // teste: serial "o" simula WiFi fora (nao reconecta)
unsigned long wifiCaiuMs=0;   // Fase 53 -- quando o WiFi caiu (0 = conectado)
void wifiWatchdog(){
  if(strlen(savedSSID)==0) return;  // nunca configurou WiFi, nada a fazer
  if(wifiManualOff){ wifiOK=false; return; }   // Fase 61: desconectado de proposito
  bool conectadoAgora = (WiFi.status()==WL_CONNECTED);
  if(!conectadoAgora){
    if(wifiOK){ Serial.println("[WiFi] Conexao caiu -- tentando reconectar..."); logEvento("WiFi","Conexao caiu",true); }
    if(!debugWifiOff) WiFi.reconnect();
    if(wifiOK) wifiCaiuMs=millis();
  }else if(!wifiOK){
    logEvento("WiFi","Conexao restabelecida",true);  // Fase 21
    if(wifiCaiuMs && millis()-wifiCaiuMs>=120000UL){   // Fase 53: internet ficou fora >= 2 min -> avisa (pos-fato: sem rede nao da pra avisar antes)
      char m[80]; snprintf(m,sizeof(m),"A internet ficou fora por %lu min e voltou.",(millis()-wifiCaiuMs)/60000UL);
      pushEnfileirar(7,"ChocaIoT: perda de conexao",m);
    }
    wifiCaiuMs=0;
  }
  wifiOK = conectadoAgora;
}

// ═══════════════════════════════════════════════════════════════
// ENERGIA DA TELA — screensaver por inatividade (ver comentario em
// screenOffTimeoutSec: sem GPIO no backlight, entao a "tela apagada" e
// software — fundo preto real via LCD, nao um comando de sleep do
// controlador). Qualquer toque volta pra tela de onde o usuario saiu.
// ═══════════════════════════════════════════════════════════════
void redrawCurrentScreen(){
  switch(appState){
    case S_MAIN:        drawMainScreen(); break;
    case S_SETTINGS:     drawSettingsPanel(); break;
    case S_SYS_INFO:     drawSysInfo(); break;
    case S_MEMORIA:      drawMemoriaScreen(); drawMemCurrentPageContent(); break;
    case S_DATETIME:     drawDateTimeScreen(); break;
    case S_DISPLAY:      drawDisplayScreen(); break;
    case S_HARDWARE:     drawHardwareScreen(); break;
    case S_ADXL:         drawAdxlScreen(); break;   // Fase 93
    case S_FANS:         fansDraw(); break;         // Fase 102
    case S_FANREGRAS:    fanRegrasDraw(); break;    // Fase 112
    case S_CAMERA:       drawCameraScreen(); break;
    case S_ATUADORES:    drawAtuadoresScreen(); break;
    case S_CICLO:        drawCicloScreen(); break;
    case S_STATUS:       drawStatusDetalhe(true); break;   // Fase 77
    case S_WIFI_LIST:    drawWifiList(); break;
    case S_WIFI_DETAIL:  drawWifiDetail(); break;
    case S_WIFI_PASS:    drawPassScreen(); break;
    case S_WIFI_ADD:     drawAddNetScreen(); break;
    case S_SENSOR_DETAIL: drawSensorDetailScreen(); break;  // Fase 21
    case S_LOGS:          drawLogsScreen(); break;          // Fase 21
    case S_AUDIO:          drawAudioScreen(); break;         // Fase 22
    case S_ALERTAS:         drawAlertasScreen(); break;      // Fase 22
    case S_GUIA:            drawGuiaLista(); break;          // Fase 37
    case S_GUIA_TEXTO:      drawGuiaTexto(); break;          // Fase 37
    case S_FASES:           drawFaseScreen(); break;         // Fase 38
    case S_ARQ:             drawArqLista(); break;           // Fase 41
    case S_ARQ_ACAO:        drawArqAcao(); break;
    case S_ARQ_NOME:        drawArqNome(); break;
    case S_ARQ_DEL:         drawArqConfirmaDel(); break;
    case S_CICLO_WIZ:       drawWiz(); break;                // Fase 42
    case S_KBTXT:           drawKbTxtScreen(); break;
    case S_TIPOS:           drawTiposLista(); break;
    case S_TIPO_EDIT:       drawTipoEdit(); break;
    case S_TXTVIEW:         drawTxtView(); break;            // Fase 44
    case S_IMGVIEW:         drawImgView(); break;
    case S_ARQ_WALL:        drawArqWall(); break;
    case S_SEG:             drawSeg(); break;                // Fase 46
    case S_PIN:             drawPin(); break;
    case S_LOCK:            drawLock(); break;
    case S_RELS:            drawRels(); break;               // Fase 48
    case S_AUTO:            drawAuto(); break;               // Fase 52
    case S_AUTO_EDIT:       drawAutoEdit(); break;
    case S_HIST:            drawHist(); break;               // Fase 53
    case S_HISTL:           drawHistLista(); break;          // Fase 55
    case S_ADV:             drawAdv(); break;                // Fase 57
    case S_ADV_EDIT:        drawAdvEdit(); break;
    default: break;
  }
}

// Fase 39/45 — wallpapers. Dois "slots": 0 = tela de bloqueio/screensaver, 1 = tela principal.
// Cada slot guarda a imagem ORIGINAL (src, RGB565 em PSRAM, 150KB) e a versao ESCURECIDA
// (disp) que e a desenhada; o escurecimento (0..90%) e ajustavel em Config. > Tela e
// recalcula disp a partir de src sem reler o JPEG. modo: 0=nenhum (cor lisa),
// 1=padrao embutido, 2=imagem do SD. Sem PSRAM/alocacao -> tudo cai na cor lisa.
struct WallSlot{ uint16_t* src; uint16_t* disp; uint8_t modo; uint8_t escuro; char path[100]; bool pronto; };
WallSlot wall[2]={ {nullptr,nullptr,1,70,"",false}, {nullptr,nullptr,1,75,"",false} };
#define WALL_CFG_PATH   "/wall.cfg"        // LittleFS: 2 linhas "modo;escuro;caminho" (bloqueio, principal)
#define WALL_CFG_LEGADO "/wallpaper.cfg"   // Fase 41: so o caminho (vira o slot de bloqueio)
uint16_t* wallDecDest=nullptr; int wallDecW=320, wallDecH=240;   // buffer de destino do decoder e suas dimensoes
uint16_t* wallDispPtr(int slot){ return wall[slot].pronto?wall[slot].disp:nullptr; }

static bool wallJpgOut(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bmp){
  if(!wallDecDest) return false;
  for(int r=0;r<h;r++){
    int yy=y+r; if(yy<0||yy>=wallDecH) continue;
    for(int c=0;c<w;c++){
      int xx=x+c; if(xx<0||xx>=wallDecW) continue;
      wallDecDest[yy*wallDecW+xx]=bmp[r*w+c];
    }
  }
  return true;
}
static bool wallAlocarSlot(int i){
  if(!wall[i].src)  wall[i].src =(uint16_t*)ps_malloc(320*240*2);
  if(!wall[i].disp) wall[i].disp=(uint16_t*)ps_malloc(320*240*2);
  return wall[i].src && wall[i].disp;
}
static bool wallDecodificarPadrao(uint16_t* dest){
  memset(dest,0,320*240*2);
  wallDecDest=dest; wallDecW=320; wallDecH=240;
  TJpgDec.setJpgScale(1); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(wallJpgOut);
  return TJpgDec.drawJpg(0,0,WALLPAPER_JPG,WALLPAPER_JPG_LEN)==0;
}
// JPEG baseline do SD em TELA CHEIA ("cover"): reduz por 1/2/4/8 mantendo >= 320x240, depois amplia/recorta
// pelo centro ate preencher a tela inteira (sem barras pretas). Bytes ficam no formato "trocado" do decoder.
static bool wallDecodificarSD(const char* caminho,uint16_t* dest){
  if(!sdOK) return false;
  File f=SD.open(caminho,FILE_READ);
  if(!f||f.isDirectory()){ if(f) f.close(); return false; }
  size_t n=f.size();
  if(n==0||n>800000){ f.close(); return false; }
  uint8_t* buf=(uint8_t*)ps_malloc(n);
  if(!buf){ f.close(); return false; }
  size_t got=f.read(buf,n); f.close();
  if(got!=n){ free(buf); return false; }
  uint16_t w=0,h=0;
  if(TJpgDec.getJpgSize(&w,&h,buf,n)!=0||w==0||h==0){ free(buf); return false; }
  int sc=1; while(sc<8 && w/(sc*2)>=320 && h/(sc*2)>=240) sc*=2;
  int sw=(w+sc-1)/sc, sh=(h+sc-1)/sc;
  uint16_t* tmp=(uint16_t*)ps_malloc((size_t)sw*sh*2);
  if(!tmp){ free(buf); return false; }
  memset(tmp,0,(size_t)sw*sh*2);
  wallDecDest=tmp; wallDecW=sw; wallDecH=sh;
  TJpgDec.setJpgScale(sc); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(wallJpgOut);
  int r=TJpgDec.drawJpg(0,0,buf,n);
  TJpgDec.setJpgScale(1);
  free(buf);
  wallDecW=320; wallDecH=240;
  if(r!=0){ free(tmp); return false; }
  // cover: fator f = max(320/sw, 240/sh); recorte centralizado, vizinho mais proximo
  float fc=max(320.0f/sw,240.0f/sh);
  float ox=(sw-320.0f/fc)/2.0f, oy=(sh-240.0f/fc)/2.0f;
  for(int y=0;y<240;y++){
    int sy=(int)(oy+(y+0.5f)/fc); if(sy<0) sy=0; if(sy>=sh) sy=sh-1;
    const uint16_t* lin=tmp+(size_t)sy*sw;
    for(int x=0;x<320;x++){
      int sx=(int)(ox+(x+0.5f)/fc); if(sx<0) sx=0; if(sx>=sw) sx=sw-1;
      dest[y*320+x]=lin[sx];
    }
  }
  free(tmp);
  return true;
}
// disp = src escurecida: brilho = (100-escuro)%. Bytes vem trocados (setSwapBytes) -> destroca, escala por canal, troca de novo.
void wallAplicarEscuro(int i){
  WallSlot& w=wall[i]; if(!w.src||!w.disp) return;
  uint32_t k=((uint32_t)(100-w.escuro)*256)/100;
  for(int n=0;n<320*240;n++){
    uint16_t p=w.src[n]; uint16_t v=(p>>8)|(p<<8);
    uint16_t R=(((v>>11)&31)*k)>>8, G=(((v>>5)&63)*k)>>8, B=((v&31)*k)>>8;
    v=(R<<11)|(G<<5)|B; w.disp[n]=(v>>8)|(v<<8);
  }
}
static bool wallCarregarSlot(int i){
  WallSlot& w=wall[i];
  if(!wallAlocarSlot(i)){ Serial.println("[Wallpaper] sem PSRAM -- fundo liso."); return false; }
  bool ok=false;
  if(w.modo==2 && w.path[0]) ok=wallDecodificarSD(w.path,w.src);
  if(!ok){ if(w.modo==2) Serial.printf("[Wallpaper] slot %d: imagem do SD indisponivel -- usando o padrao.\n",i); ok=wallDecodificarPadrao(w.src); }
  if(ok) wallAplicarEscuro(i);
  return ok;
}
// true = existe imagem pronta pra desenhar naquele slot (carrega sob demanda, uma vez)
bool wallGarantir(int i){
  if(wall[i].modo==0) return false;
  if(!wall[i].pronto) wall[i].pronto=wallCarregarSlot(i);
  return wall[i].pronto;
}
void wallCfgSalvar(){
  File c=LittleFS.open(WALL_CFG_PATH,"w"); if(!c) return;
  for(int i=0;i<2;i++) c.printf("%u;%u;%s\n",(unsigned)wall[i].modo,(unsigned)wall[i].escuro,wall[i].path);
  c.close();
}
void wallCfgCarregar(){
  if(LittleFS.exists(WALL_CFG_PATH)){
    File c=LittleFS.open(WALL_CFG_PATH,"r"); if(!c) return;
    for(int i=0;i<2;i++){
      String l=c.readStringUntil('\n'); l.trim(); if(!l.length()) continue;
      int a=l.indexOf(';'), b=l.indexOf(';',a+1); if(a<0||b<0) continue;
      int m=l.substring(0,a).toInt(), e=l.substring(a+1,b).toInt();
      if(m>=0&&m<=2) wall[i].modo=m;
      if(e>=0&&e<=90) wall[i].escuro=e;
      strlcpy(wall[i].path,l.substring(b+1).c_str(),sizeof(wall[i].path));
    }
    c.close();
  }else if(LittleFS.exists(WALL_CFG_LEGADO)){
    File c=LittleFS.open(WALL_CFG_LEGADO,"r"); String p=c.readStringUntil('\n'); c.close(); p.trim();
    if(p.length()){ wall[0].modo=2; strlcpy(wall[0].path,p.c_str(),sizeof(wall[0].path)); }
  }
}
// Chamada pelo gerenciador de arquivos. Em falha, volta ao que estava configurado.
bool wallDefinirDoSD(const char* caminho,int slot){
  if(slot<0||slot>1||!wallAlocarSlot(slot)) return false;
  if(!wallDecodificarSD(caminho,wall[slot].src)){ wall[slot].pronto=false; wallGarantir(slot); return false; }
  wall[slot].modo=2; strlcpy(wall[slot].path,caminho,sizeof(wall[slot].path));
  wallAplicarEscuro(slot); wall[slot].pronto=true;
  wallCfgSalvar();
  return true;
}
// Troca o modo (0 nenhum, 1 padrao) sem mexer no arquivo escolhido.
void wallSetModo(int slot,uint8_t modo){
  wall[slot].modo=modo; if(modo==1) wall[slot].path[0]=0; wall[slot].pronto=false;
  wallGarantir(slot); wallCfgSalvar();
}
void wallSetEscuro(int slot,int pct){
  if(pct<0) pct=0; if(pct>90) pct=90;
  wall[slot].escuro=pct; if(wall[slot].pronto) wallAplicarEscuro(slot);
  wallCfgSalvar();
}
// Repinta um retangulo com o wallpaper do slot (ou com a cor lisa, se nao houver).
void wallSlotRestore(int slot,int x,int y,int w,int h,uint16_t corLisa){
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x+w>320) w=320-x; if(y+h>240) h=240-y;
  if(w<=0||h<=0) return;
  if(!wallGarantir(slot)){ tft.fillRect(x,y,w,h,corLisa); return; }
  if(x==0&&w==320){ tft.pushImage(0,y,320,h,wall[slot].disp+y*320); return; }   // faixa inteira: 1 chamada so
  for(int r=0;r<h;r++) tft.pushImage(x,y+r,w,1,wall[slot].disp+(y+r)*320+x);
}
void wallRestore(int x,int y,int w,int h){ wallSlotRestore(0,x,y,w,h,TFT_BLACK); }   // tela de bloqueio/screensaver
// Tela principal: fundo do slot 1 quando ativo, senao cBg. homeWallAtiva e atualizada em drawMainScreen().
bool homeWallAtiva=false;
void homeFill(int x,int y,int w,int h){
  if(homeWallAtiva) wallSlotRestore(1,x,y,w,h,cBg); else tft.fillRect(x,y,w,h,cBg);
}
void homeTexto(uint16_t fg){ if(homeWallAtiva) tft.setTextColor(fg); else tft.setTextColor(fg,cBg); }   // sem fundo quando ha wallpaper

// ── Tela (Fase 45): apagar apos X s + wallpapers (bloqueio e principal) com escurecimento ajustavel ──
// Tudo aplica e grava na hora (sem botao Salvar). Escurecimento 0..90% em passos de 5 (barra ou -/+).
#define TELA_ROW0_Y 26
#define TELA_ROW_H  66
#define TELA_BAR_X  88
#define TELA_BAR_W  170
static void telaThumb(int slot,int x,int y){
  static uint16_t th[72*54];
  if(wallGarantir(slot)){
    for(int r=0;r<54;r++) for(int c=0;c<72;c++) th[r*72+c]=wall[slot].disp[((r*240)/54)*320+((c*320)/72)];
    tft.pushImage(x,y,72,54,th);
  }else{
    tft.fillRect(x,y,72,54,slot==0?TFT_BLACK:cBg);
    tft.setTextColor(cSub,slot==0?TFT_BLACK:cBg);tft.setTextSize(1);tft.setCursor(x+20,y+23);tft.print("nenhum");
  }
  tft.drawRect(x-1,y-1,74,56,tft.color565(70,70,120));
}
static void telaBotaoMini(int x,int y,int w,int h,const char* t,bool ativo){
  uint16_t bg=ativo?tft.color565(10,50,20):tft.color565(16,16,46), br=ativo?cGreen:tft.color565(55,55,120), fg=ativo?cGreen:TFT_WHITE;
  tft.fillRoundRect(x,y,w,h,5,bg);tft.drawRoundRect(x,y,w,h,5,br);
  tft.setTextColor(fg,bg);tft.setTextSize(1);tft.setCursor(x+(w-tft.textWidth(t))/2,y+(h-8)/2);tft.print(t);
}
static void telaDrawRow(int slot){
  int y0=TELA_ROW0_Y+slot*TELA_ROW_H;
  tft.fillRect(0,y0-2,320,TELA_ROW_H-4,cBg);
  telaThumb(slot,8,y0+4);
  tft.setTextSize(1);tft.setTextColor(cLogo,cBg);tft.setCursor(TELA_BAR_X,y0+4);tft.print(slot==0?"Tela de bloqueio":"Tela principal");
  char v[20]; snprintf(v,20,"Escurecer %u%%",(unsigned)wall[slot].escuro);
  tft.setTextColor(cClk,cBg);tft.setCursor(314-tft.textWidth(v),y0+4);tft.print(v);
  tft.fillRoundRect(TELA_BAR_X,y0+18,TELA_BAR_W,16,4,tft.color565(20,20,44));
  int fw=(int)wall[slot].escuro*TELA_BAR_W/90; if(fw>0) tft.fillRoundRect(TELA_BAR_X,y0+18,fw<8?8:fw,16,4,tft.color565(60,90,200));
  tft.drawRoundRect(TELA_BAR_X,y0+18,TELA_BAR_W,16,4,tft.color565(70,70,140));
  tft.fillRoundRect(264,y0+18,24,16,4,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setCursor(273,y0+22);tft.print("-");
  tft.fillRoundRect(290,y0+18,24,16,4,cKey);tft.setCursor(299,y0+22);tft.print("+");
  telaBotaoMini(TELA_BAR_X,y0+40,70,20,"Padrao",wall[slot].modo==1);
  telaBotaoMini(TELA_BAR_X+74,y0+40,70,20,"Nenhum",wall[slot].modo==0);
  telaBotaoMini(TELA_BAR_X+148,y0+40,76,20,"Arquivos",wall[slot].modo==2);
}
// Fase 103 -- Tela: Brilho · Reduzir brilho apos · Apagar apos · Manter ligada + subtela "Papel de parede"
#define TELA_Y_BRILHO 22
#define TELA_Y_DIM    56
#define TELA_Y_OFF    90
#define TELA_Y_RELOGIO 124
#define TELA_Y_SEMPRE 158
#define TELA_Y_PAPEL  192
static void telaStep(int y,const char* rot,const char* val,bool on){
  uint16_t cb=tft.color565(16,16,46);
  tft.fillRect(0,y-2,320,34,cBg);
  uiCard(6,y,308,30);
  tft.setTextSize(1);tft.setTextColor(on?TFT_WHITE:cSub,cb);tft.setCursor(14,y+11);tft.print(rot);
  tft.fillRoundRect(186,y+3,36,24,6,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setTextSize(2);tft.setCursor(198,y+8);tft.print("-");
  tft.fillRoundRect(272,y+3,36,24,6,cKey);tft.setCursor(284,y+8);tft.print("+");
  tft.setTextSize(1);tft.setTextColor(on?cClk:cSub,cb);tft.setCursor(247-tft.textWidth(val)/2,y+11);tft.print(val);
}
static void telaDrawBrilho(){ char v[8]; snprintf(v,8,"%u%%",(unsigned)brilhoPct); telaStep(TELA_Y_BRILHO,"Brilho da tela",v,true); }
static void telaDrawDim(){ char v[10]; if(dimSegundos==0) snprintf(v,10,"nunca"); else snprintf(v,10,"%us",(unsigned)dimSegundos); telaStep(TELA_Y_DIM,"Reduzir brilho apos",v,!telaSempreLigada); }
static void telaDrawOff(){ char v[10]; snprintf(v,10,"%lus",screenOffTimeoutSec); telaStep(TELA_Y_OFF,"Apagar a tela apos",v,!telaSempreLigada); }
static void telaDrawSempre(){
  uint16_t cb=tft.color565(16,16,46); int y=TELA_Y_SEMPRE;
  tft.fillRect(0,y-2,320,34,cBg);
  uiCard(6,y,308,30);
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(14,y+11);tft.print("Manter tela ligada");
  int x0=262,y0=y+7; bool on=telaSempreLigada;
  tft.fillRoundRect(x0,y0,44,16,8,on?tft.color565(10,90,40):tft.color565(50,50,70));
  tft.fillCircle(on?x0+36:x0+8,y0+8,6,on?cGreen:tft.color565(150,150,170));
}
static void telaDrawRelogio(){
  uint16_t cb=tft.color565(16,16,46); int y=TELA_Y_RELOGIO;
  tft.fillRect(0,y-2,320,34,cBg);
  uiCard(6,y,308,30);
  tft.setTextSize(1);tft.setTextColor(!telaSempreLigada?TFT_WHITE:cSub,cb);tft.setCursor(14,y+11);tft.print("Relogio sempre ligado (repouso)");
  int x0=262,y0=y+7; bool on=relogioSempre;
  tft.fillRoundRect(x0,y0,44,16,8,on?tft.color565(10,90,40):tft.color565(50,50,70));
  tft.fillCircle(on?x0+36:x0+8,y0+8,6,on?cGreen:tft.color565(150,150,170));
}
static void telaDrawPapel(){
  uint16_t cb=tft.color565(16,16,46); int y=TELA_Y_PAPEL;
  tft.fillRect(0,y-2,320,34,cBg);
  uiCard(6,y,308,30);
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(14,y+11);tft.print("Papel de parede (bloqueio e principal)");
  tft.setTextSize(2);tft.setTextColor(cClk,cb);tft.setCursor(292,y+8);tft.print(">");tft.setTextSize(1);
}
void drawDisplayScreen(){
  tft.fillScreen(cBg);
  if(telaSub){ uiTopo("PAPEL DE PAREDE"); telaDrawRow(0); telaDrawRow(1); return; }
  uiTopo("TELA");
  telaDrawBrilho(); telaDrawDim(); telaDrawOff(); telaDrawRelogio(); telaDrawSempre(); telaDrawPapel();
}
void handleDisplayTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ if(telaSub){ telaSub=0; drawDisplayScreen(); } else { appState=S_SETTINGS; drawSettingsPanel(); } return; }
  if(telaSub){
    for(int slot=0;slot<2;slot++){
      int y0=TELA_ROW0_Y+slot*TELA_ROW_H;
      if(ty>=y0+14&&ty<=y0+36){
        if(tx>=TELA_BAR_X&&tx<TELA_BAR_X+TELA_BAR_W){ int p=((tx-TELA_BAR_X)*90)/TELA_BAR_W; wallSetEscuro(slot,(p+2)/5*5); telaDrawRow(slot); }
        else if(tx>=264&&tx<288){ wallSetEscuro(slot,(int)wall[slot].escuro-5); telaDrawRow(slot); }
        else if(tx>=290&&tx<316){ wallSetEscuro(slot,(int)wall[slot].escuro+5); telaDrawRow(slot); }
        return;
      }
      if(ty>=y0+38&&ty<=y0+62){
        if(tx>=TELA_BAR_X&&tx<TELA_BAR_X+70){ wallSetModo(slot,1); telaDrawRow(slot); }
        else if(tx>=TELA_BAR_X+74&&tx<TELA_BAR_X+144){ wallSetModo(slot,0); telaDrawRow(slot); }
        else if(tx>=TELA_BAR_X+148&&tx<TELA_BAR_X+224){ abrirArquivos(); }
        return;
      }
    }
    return;
  }
  int dir=(tx>=180&&tx<228)?-1:(tx>=266&&tx<314)?+1:0;
  if(ty>=TELA_Y_BRILHO&&ty<TELA_Y_BRILHO+32){
    if(dir){ int b=(int)brilhoPct+dir*10; if(b<10) b=10; if(b>100) b=100; brilhoPct=(uint8_t)b; saveDisplayCfg(); blAplica(); telaDrawBrilho(); }
    return;
  }
  if(ty>=TELA_Y_DIM&&ty<TELA_Y_DIM+32){
    if(dir && !telaSempreLigada){
      int d=(int)dimSegundos+dir*10; int mx=(int)screenOffTimeoutSec-10; if(d>mx) d=mx; if(d<0) d=0;
      dimSegundos=(uint16_t)d; saveDisplayCfg(); telaDrawDim();
    }
    return;
  }
  if(ty>=TELA_Y_OFF&&ty<TELA_Y_OFF+32){
    if(dir && !telaSempreLigada){
      long n=(long)screenOffTimeoutSec+dir*15; if(n<30) n=30; if(n>600) n=600; screenOffTimeoutSec=(unsigned long)n;
      if(dimSegundos>0 && (long)dimSegundos>n-10) dimSegundos=(uint16_t)(n-10);
      saveDisplayCfg(); telaDrawOff(); telaDrawDim();
    }
    return;
  }
  if(ty>=TELA_Y_RELOGIO&&ty<TELA_Y_RELOGIO+32){ relogioSempre=!relogioSempre; saveDisplayCfg(); telaDrawRelogio(); return; }   // Fase 104
  if(ty>=TELA_Y_SEMPRE&&ty<TELA_Y_SEMPRE+32){ telaSempreLigada=!telaSempreLigada; saveDisplayCfg(); telaDrawDim(); telaDrawOff(); telaDrawRelogio(); telaDrawSempre(); return; }
  if(ty>=TELA_Y_PAPEL&&ty<TELA_Y_PAPEL+32){ telaSub=1; drawDisplayScreen(); return; }
}

void enterScreensaver(){
  if(appState==S_SCREENSAVER) return;
  stateBeforeSaver=appState;
  appState=S_SCREENSAVER;
  blEstagio=relogioSempre?1:2; blAplica();   // Fase 103/104 -- screensaver = tela apagada (backlight 0 %) ou, com 'relogio sempre ligado', relogio com brilho reduzido; o toque acorda
  if(wallGarantir(0)) tft.pushImage(0,0,320,240,wall[0].disp); else tft.fillScreen(TFT_BLACK);
  saverLastClockMs=0; saverUltimoMinuto=-1;   // forca desenho imediato no proximo tick do loop()
}

void exitScreensaver(){
  blEstagio=0; blAplica();   // Fase 101b/103 -- brilho de volta antes de desenhar (inclusive para a tela de PIN)
  bool emRecuperacao=pinEmRecuperacao(stateBeforeSaver);   // Fase 53: ja provou identidade (codigo) -- nao pede o PIN de novo
  if(pinAtivo && stateBeforeSaver!=S_LOCK && !emRecuperacao){ entrarBloqueio(stateBeforeSaver); return; }   // Fase 46: PIN antes de voltar
  appState=stateBeforeSaver;
  redrawCurrentScreen();
}

// Chamado a cada volta do loop() enquanto appState==S_SCREENSAVER.
// Fase 40 -- so a hora (HH:MM, sem segundos) sobre o wallpaper: fonte 8 do
// TFT_eSPI (75px, numerica, bem mais nitida que a GLCD ampliada), com uma
// sombra suave pra destacar do fundo. Redesenha so quando o MINUTO muda
// (checagem 1x/s), restaurando o wallpaper embaixo -- nunca fillScreen.
void screensaverTick(){
  unsigned long now=millis();
  if(!rtcOK || now-saverLastClockMs<=1000) return;
  saverLastClockMs=now;
  DateTime n=rtc.now();
  if(n.minute()==saverUltimoMinuto) return;
  saverUltimoMinuto=n.minute();

  char cs[6]; snprintf(cs,6,"%02d:%02d",n.hour(),n.minute());
  wallRestore(10,78,300,84);
  tft.setTextFont(8); tft.setTextSize(1); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(tft.color565(4,4,12));                       // sombra (cor unica = transparente)
  tft.drawString(cs,162,122);
  tft.setTextColor(tft.color565(225,228,245));
  tft.drawString(cs,160,120);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(1);
}

// ═══════════════════════════════════════════════════════════════
// GERENCIADOR DE ARQUIVOS DO CARTAO SD (Fase 41)
//  Firmware: listar/entrar em pastas, Renomear, Mover, Deletar, Nova pasta e
//  "Definir como wallpaper" (JPEG do SD vira o fundo do screensaver).
//  PWA: ve o indice e pede o conteudo de UM arquivo por vez, via Firestore
//  (arquivos/indice e arquivos/conteudo) -- ver publicarIndiceSD() /
//  processarArquivoReq() no fim deste bloco.
// ═══════════════════════════════════════════════════════════════
#define ARQ_MAX        64
#define ARQ_VIS        7
#define ARQ_ROW_Y0     26
#define ARQ_ROW_H      30
#define ARQ_NOME_MAX   44     // inclui o \0 -- nomes maiores sao ignorados na listagem
#define ARQ_ENVIO_MAX  150000UL   // maior arquivo que o PWA pode pedir (Firestore: doc <= 1MB, base64 +33%)

struct ArqEnt{ char nome[ARQ_NOME_MAX]; bool dir; uint32_t tam; };
ArqEnt arqLista[ARQ_MAX];
int  arqN=0, arqScroll=0;
char arqDir[100]="/";
char arqSelNome[ARQ_NOME_MAX]=""; bool arqSelDir=false; uint32_t arqSelTam=0;
bool arqModoMover=false;
char arqMoverNome[ARQ_NOME_MAX]=""; char arqMoverDirOrigem[100]="/"; bool arqMoverEhDir=false;
bool arqInputRenomear=true;      // true = renomear, false = nova pasta
char arqNomeBuf[ARQ_NOME_MAX]=""; int arqNomeLen=0;

bool wallDefinirDoSD(const char* caminho,int slot);   // Fase 45: 0=tela de bloqueio/screensaver, 1=tela principal
uint16_t* wallDispPtr(int slot);
void drawArqLista(); void drawArqAcao(); void drawArqNome(); void drawArqConfirmaDel();

static void arqCaminho(char* out,size_t n,const char* dir,const char* nome){
  if(strcmp(dir,"/")==0) snprintf(out,n,"/%s",nome); else snprintf(out,n,"%s/%s",dir,nome);
}
static void arqSobe(){
  char* p=strrchr(arqDir,'/');
  if(p==arqDir) arqDir[1]=0; else if(p) *p=0;
}
static void arqFmtTam(char* out,size_t n,uint32_t t){
  if(t<1024) snprintf(out,n,"%uB",(unsigned)t);
  else if(t<1024*1024) snprintf(out,n,"%.1fK",t/1024.0f);
  else snprintf(out,n,"%.1fM",t/1048576.0f);
}
static bool arqEhJpeg(const char* nome){
  const char* p=strrchr(nome,'.'); if(!p) return false;
  return strcasecmp(p,".jpg")==0 || strcasecmp(p,".jpeg")==0;
}

void arqCarregar(){
  arqN=0;
  if(!sdOK) return;
  File d=SD.open(arqDir);
  if(!d || !d.isDirectory()){ if(d) d.close(); strcpy(arqDir,"/"); d=SD.open("/"); if(!d) return; }
  if(strcmp(arqDir,"/")!=0){ strcpy(arqLista[0].nome,".."); arqLista[0].dir=true; arqLista[0].tam=0; arqN=1; }
  int primeiro=arqN;
  File f;
  while(arqN<ARQ_MAX && (f=d.openNextFile())){
    String n=f.name(); int i=n.lastIndexOf('/'); if(i>=0) n=n.substring(i+1);
    bool ok = n.length()>0 && n.length()<ARQ_NOME_MAX && n[0]!='.' && n!="System Volume Information";
    if(ok){ strlcpy(arqLista[arqN].nome,n.c_str(),ARQ_NOME_MAX); arqLista[arqN].dir=f.isDirectory(); arqLista[arqN].tam=f.isDirectory()?0:(uint32_t)f.size(); arqN++; }
    f.close();
  }
  d.close();
  // pastas primeiro, depois ordem alfabetica (insercao -- no maximo 64 itens)
  for(int i=primeiro+1;i<arqN;i++){
    ArqEnt t=arqLista[i]; int j=i-1;
    while(j>=primeiro && ((arqLista[j].dir<t.dir) || (arqLista[j].dir==t.dir && strcasecmp(arqLista[j].nome,t.nome)>0))){ arqLista[j+1]=arqLista[j]; j--; }
    arqLista[j+1]=t;
  }
}

void arqToast(const char* msg,bool ok){
  uint16_t bg=ok?tft.color565(10,45,10):tft.color565(60,14,14), fg=ok?cGreen:tft.color565(255,120,120);
  tft.fillRoundRect(16,96,288,44,8,bg);tft.drawRoundRect(16,96,288,44,8,fg);
  tft.setTextColor(fg,bg);tft.setTextSize(1);
  int w=tft.textWidth(msg); tft.setCursor(160-w/2,114);tft.print(msg);
  delay(1300);
}

void abrirArquivos(){
  arqModoMover=false; arqScroll=0; strcpy(arqDir,"/");
  arqCarregar(); appState=S_ARQ; drawArqLista();
}

void drawArqLista(){
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  tft.fillScreen(cBg);
  tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char pb[48];
  if(strlen(arqDir)>24) snprintf(pb,48,"...%s",arqDir+strlen(arqDir)-21); else strlcpy(pb,arqDir,48);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print(arqModoMover?"Mover p/ ":"SD ");tft.print(pb);
  if(arqModoMover){
    tft.fillRoundRect(236,2,80,20,4,tft.color565(10,60,10));tft.drawRoundRect(236,2,80,20,4,cGreen);
    tft.setTextColor(cGreen,tft.color565(10,60,10));tft.setCursor(246,8);tft.print("Mover aqui");
  }else{
    tft.fillRoundRect(284,2,32,20,4,tft.color565(30,30,70));
    tft.drawFastHLine(292,12,16,cLogo);tft.drawFastVLine(300,6,13,cLogo);   // "+" nova pasta
  }

  if(!sdOK){
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(80,110);tft.print("Cartao SD indisponivel");
    return;
  }
  if(arqN==0){ tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(118,110);tft.print("Pasta vazia"); }

  for(int i=0;i<ARQ_VIS;i++){
    int idx=arqScroll+i; if(idx>=arqN) break;
    const ArqEnt& e=arqLista[idx];
    int y=ARQ_ROW_Y0+i*ARQ_ROW_H;
    bool apagado = arqModoMover && !e.dir;
    tft.fillRoundRect(4,y,278,ARQ_ROW_H-2,5,rowBg);tft.drawRoundRect(4,y,278,ARQ_ROW_H-2,5,rowBrd);
    // icone: pasta (amarela) ou arquivo (branco)
    if(e.dir){ tft.fillRect(10,y+8,8,3,cLogo);tft.fillRoundRect(10,y+10,18,12,2,cLogo); }
    else     { uint16_t c=apagado?tft.color565(70,70,70):tft.color565(210,210,225); tft.drawRect(13,y+6,12,16,c);tft.drawFastHLine(15,y+11,8,c);tft.drawFastHLine(15,y+15,8,c); }
    char nm[28]; strlcpy(nm,e.nome,28);
    tft.setTextColor(apagado?tft.color565(80,80,80):TFT_WHITE,rowBg);tft.setTextSize(1);tft.setCursor(34,y+10);tft.print(nm);
    if(!e.dir){ char sz[12]; arqFmtTam(sz,12,e.tam); tft.setTextColor(cSub,rowBg);tft.setCursor(246-tft.textWidth(sz),y+10);tft.print(sz); }
    if(!arqModoMover && strcmp(e.nome,"..")!=0){   // "..." = acoes do item
      for(int k=0;k<3;k++) tft.fillCircle(256+k*8,y+14,2,cLogo);
    }
  }
  bool podeCima=arqScroll>0, podeBaixo=arqScroll<arqN-ARQ_VIS;
  uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.drawRoundRect(288,ARQ_ROW_Y0,30,100,5,rowBrd);
  tft.fillTriangle(303,ARQ_ROW_Y0+34,292,ARQ_ROW_Y0+66,314,ARQ_ROW_Y0+66,podeCima?on:off);
  tft.drawRoundRect(288,ARQ_ROW_Y0+108,30,100,5,rowBrd);
  tft.fillTriangle(303,ARQ_ROW_Y0+108+66,292,ARQ_ROW_Y0+108+34,314,ARQ_ROW_Y0+108+34,podeBaixo?on:off);
}

// ── Menu de acoes de um item ──
static void arqBotao(int x,int y,int w,int h,const char* txt,uint16_t bg,uint16_t fg,uint16_t brd){
  tft.fillRoundRect(x,y,w,h,8,bg);tft.drawRoundRect(x,y,w,h,8,brd);
  tft.setTextColor(fg,bg);tft.setTextSize(2);tft.setCursor(x+(w-tft.textWidth(txt))/2,y+(h-16)/2);tft.print(txt);
}
// Fase 44 -- "Abrir": pasta entra; .txt/.log/... abre no leitor; .jpg abre a imagem.
static bool arqEhTexto(const char* nome){
  const char* p=strrchr(nome,'.'); if(!p) return false;
  static const char* ext[]={".txt",".log",".old",".cfg",".csv",".json",".md",".ini"};
  for(auto e:ext) if(!strcasecmp(p,e)) return true;
  return false;
}
static bool arqAbrivel(const char* nome,bool dir){ return dir||arqEhTexto(nome)||arqEhJpeg(nome); }

void drawArqAcao(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char nm[36]; strlcpy(nm,arqSelNome,36);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print(nm);
  char info[40]; if(arqSelDir) strcpy(info,"Pasta"); else { char sz[12]; arqFmtTam(sz,12,arqSelTam); snprintf(info,40,"Arquivo - %s",sz); }
  tft.setTextColor(cSub,cBg);tft.setCursor(10,34);tft.print(info);
  uint16_t n=tft.color565(16,16,46), nb=tft.color565(55,55,120), off=tft.color565(14,14,26), offb=tft.color565(36,36,60), offf=tft.color565(70,70,70);
  bool abre=arqAbrivel(arqSelNome,arqSelDir);
  if(abre) arqBotao(12,50,140,44,"Abrir",tft.color565(10,50,20),cGreen,tft.color565(30,130,50));
  else     arqBotao(12,50,140,44,"Abrir",off,offf,offb);
  arqBotao(168,50,140,44,"Renomear",n,TFT_WHITE,nb);
  arqBotao(12,102,140,44,"Mover",n,TFT_WHITE,nb);
  arqBotao(168,102,140,44,"Deletar",tft.color565(55,12,12),TFT_WHITE,tft.color565(140,40,40));
  bool jpg=!arqSelDir&&arqEhJpeg(arqSelNome);
  if(jpg) arqBotao(12,154,296,44,"Definir como wallpaper",tft.color565(10,45,60),tft.color565(120,210,255),tft.color565(40,120,160));
  else    arqBotao(12,154,296,44,"Definir como wallpaper",off,offf,offb);
  tft.setTextColor(tft.color565(90,90,120),cBg);tft.setTextSize(1);tft.setCursor(10,208);
  tft.print(jpg?"Escolha depois: tela de bloqueio ou tela principal.":"Wallpaper so para arquivos .jpg/.jpeg (baseline).");
  tft.setCursor(10,222);
  tft.print(abre?(arqSelDir?"Abrir: entra na pasta.":"Abrir: le o arquivo aqui na tela."):"Abrir: so pastas, .jpg e arquivos de texto.");
}

// Escolha do destino do wallpaper (Fase 45)
void drawArqWall(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print("Wallpaper: usar em qual tela?");
  char nm[40]; strlcpy(nm,arqSelNome,40);
  tft.setTextColor(cSub,cBg);tft.setCursor(10,34);tft.print(nm);
  arqBotao(20,56,280,52,"Tela de bloqueio",tft.color565(16,16,46),TFT_WHITE,tft.color565(55,55,120));
  arqBotao(20,120,280,52,"Tela principal",tft.color565(16,16,46),TFT_WHITE,tft.color565(55,55,120));
  tft.setTextColor(tft.color565(90,90,120),cBg);tft.setCursor(10,190);
  tft.print("O escurecimento se ajusta em Config. > Tela.");
}
void drawArqConfirmaDel(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(10,8);tft.print("Deletar");
  char nm[40]; strlcpy(nm,arqSelNome,40);
  tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(1);tft.setCursor(20,70);tft.printf("Deletar %s \"%s\"?",arqSelDir?"a pasta":"o arquivo",nm);
  tft.setTextColor(cSub,cBg);tft.setCursor(20,90);tft.print(arqSelDir?"So e possivel se a pasta estiver vazia.":"Esta acao nao pode ser desfeita.");
  arqBotao(20,140,130,50,"Cancelar",tft.color565(16,16,46),TFT_WHITE,tft.color565(55,55,120));
  arqBotao(170,140,130,50,"Deletar",tft.color565(55,12,12),TFT_WHITE,tft.color565(140,40,40));
}

// ── Entrada de nome (renomear / nova pasta) -- teclado virtual generico ──
void drawArqNomeField(){
  tft.fillRect(5,40,310,24,tft.color565(5,5,18));
  tft.drawRect(5,40,310,24,tft.color565(75,75,170));
  tft.setTextColor(TFT_WHITE,tft.color565(5,5,18));tft.setTextSize(1);
  tft.setCursor(9,48);tft.print(arqNomeBuf);
  tft.drawFastVLine(9+arqNomeLen*6,42,18,TFT_WHITE);
}
void drawArqNome(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(90,6);tft.print(arqInputRenomear?"Renomear":"Nova pasta");
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(5,32);tft.print(arqInputRenomear?"Novo nome:":"Nome da pasta:");
  drawArqNomeField();
  drawKeyboard();
}
static void arqNomeAdd(char c){ if(arqNomeLen<ARQ_NOME_MAX-1){ arqNomeBuf[arqNomeLen++]=c; arqNomeBuf[arqNomeLen]=0; } drawArqNomeField(); }
static void arqNomeDel(){ if(arqNomeLen>0) arqNomeBuf[--arqNomeLen]=0; drawArqNomeField(); }

static bool arqNomeValido(const char* n){
  if(!n[0]) return false;
  for(const char* p=n;*p;p++) if(strchr("/\\:*?\"<>|",*p)) return false;
  return n[0]!='.';
}

// ── Operacoes ──
void arqRenomearOuCriar(){
  if(!arqNomeValido(arqNomeBuf)){ arqToast("Nome invalido (nao use / \\ : * ? \" < > |)",false); return; }
  char novo[160]; arqCaminho(novo,sizeof(novo),arqDir,arqNomeBuf);
  if(SD.exists(novo)){ arqToast("Ja existe um item com esse nome",false); return; }
  bool ok;
  if(arqInputRenomear){ char velho[160]; arqCaminho(velho,sizeof(velho),arqDir,arqSelNome); ok=SD.rename(velho,novo); }
  else ok=SD.mkdir(novo);
  if(ok){ arqIndiceSujo=true; arqCarregar(); appState=S_ARQ; drawArqLista(); arqToast(arqInputRenomear?"Renomeado":"Pasta criada",true); drawArqLista(); }
  else arqToast("Falhou (SD ocupado ou nome nao aceito)",false);
}
void arqDeletar(){
  char c[160]; arqCaminho(c,sizeof(c),arqDir,arqSelNome);
  bool ok = arqSelDir ? SD.rmdir(c) : SD.remove(c);
  if(ok){ arqIndiceSujo=true; arqCarregar(); if(arqScroll>0&&arqScroll>=arqN) arqScroll=0; appState=S_ARQ; drawArqLista(); arqToast("Deletado",true); drawArqLista(); }
  else { arqToast(arqSelDir?"Pasta nao vazia":"Falha ao deletar",false); appState=S_ARQ; drawArqLista(); }
}
void arqMoverAqui(){
  char origem[160],destino[160]; arqCaminho(origem,sizeof(origem),arqMoverDirOrigem,arqMoverNome); arqCaminho(destino,sizeof(destino),arqDir,arqMoverNome);
  if(strcmp(arqMoverDirOrigem,arqDir)==0){ arqToast("O item ja esta nesta pasta",false); return; }
  if(arqMoverEhDir){ size_t l=strlen(origem); if(strncmp(destino,origem,l)==0 && (destino[l]=='/'||destino[l]==0)){ arqToast("Destino invalido (dentro da propria pasta)",false); return; } }
  if(SD.exists(destino)){ arqToast("Ja existe um item com esse nome aqui",false); return; }
  if(SD.rename(origem,destino)){ arqModoMover=false; arqIndiceSujo=true; arqCarregar(); arqScroll=0; drawArqLista(); arqToast("Movido",true); drawArqLista(); }
  else arqToast("Falha ao mover",false);
}
void arqDefinirWallpaper(int slot){
  char c[160]; arqCaminho(c,sizeof(c),arqDir,arqSelNome);
  tft.fillRoundRect(16,96,288,44,8,tft.color565(20,20,60));tft.setTextColor(TFT_WHITE,tft.color565(20,20,60));tft.setTextSize(1);tft.setCursor(96,114);tft.print("Carregando imagem...");
  if(wallDefinirDoSD(c,slot)){
    uint16_t* d=wallDispPtr(slot); if(d){ tft.pushImage(0,0,320,240,d); delay(1800); }   // previa do resultado
    appState=S_ARQ; drawArqLista(); arqToast("Wallpaper definido",true); drawArqLista();
  }else{
    appState=S_ARQ; drawArqLista(); arqToast("Falhou: precisa ser JPEG baseline",false); drawArqLista();
  }
}

// ── Leitor de texto (Fase 44) -- abre .txt/.log/... do SD e relatorios de ciclo ──
// Arquivos grandes: le so os ultimos TXT_MAX_BYTES (o mais recente de um log fica no fim).
#define TXT_MAX_BYTES  12000
#define TXT_COLS       45
#define TXT_LINHAS     21
#define TXT_MAX_LINHAS 800
char*    txtBuf=nullptr;
uint16_t txtIni[TXT_MAX_LINHAS]; uint8_t txtTam[TXT_MAX_LINHAS];
int      txtNL=0, txtPag=0;
bool     txtTrunc=false;
char     txtTitulo[36]="";
AppState txtVoltarPara=S_ARQ;

static int txtTotalPag(){ int t=(txtNL+TXT_LINHAS-1)/TXT_LINHAS; return t<1?1:t; }
static bool txtAbrir(const char* caminho,const char* titulo,bool irParaFim){
  if(!sdOK) return false;
  File f=SD.open(caminho,FILE_READ);
  if(!f||f.isDirectory()){ if(f) f.close(); return false; }
  size_t total=f.size(); size_t ler=total, off=0; txtTrunc=false;
  if(total>TXT_MAX_BYTES){ off=total-TXT_MAX_BYTES; ler=TXT_MAX_BYTES; txtTrunc=true; f.seek(off); }
  if(!txtBuf) txtBuf=(char*)ps_malloc(TXT_MAX_BYTES+2);
  if(!txtBuf){ f.close(); return false; }
  int n=(int)f.read((uint8_t*)txtBuf,ler); f.close(); if(n<0) n=0;
  int w=0; for(int i=0;i<n;i++){ char c=txtBuf[i]; if(c=='\r') continue; if(c!='\n'&&(c<32||c>126)) c='?'; txtBuf[w++]=c; }
  txtBuf[w]=0; n=w;
  int ini=0;
  if(txtTrunc){ while(ini<n&&txtBuf[ini]!='\n') ini++; if(ini<n) ini++; }   // descarta a 1a linha (cortada no meio)
  txtNL=0;
  int i=ini;
  while(i<n && txtNL<TXT_MAX_LINHAS){
    int ls=i, col=0;
    while(i<n && txtBuf[i]!='\n' && col<TXT_COLS){ i++; col++; }
    txtIni[txtNL]=(uint16_t)ls; txtTam[txtNL]=(uint8_t)(i-ls); txtNL++;
    if(i<n && txtBuf[i]=='\n') i++;
  }
  strlcpy(txtTitulo,titulo,sizeof(txtTitulo));
  txtPag=irParaFim?txtTotalPag()-1:0;
  return true;
}
void drawTxtView(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,22,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(6,3);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,7);tft.print(txtTitulo);
  char pg[16]; snprintf(pg,16,"%d/%d",txtPag+1,txtTotalPag());
  tft.setTextColor(cClk,cCard);tft.setCursor(316-tft.textWidth(pg),7);tft.print(pg);
  tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(1);
  for(int i=0;i<TXT_LINHAS;i++){
    int idx=txtPag*TXT_LINHAS+i; if(idx>=txtNL) break;
    char ln[TXT_COLS+2]; int l=txtTam[idx]; if(l>TXT_COLS) l=TXT_COLS;
    memcpy(ln,txtBuf+txtIni[idx],l); ln[l]=0;
    tft.setCursor(4,25+i*10);tft.print(ln);
  }
  if(txtNL==0){ tft.setTextColor(cSub,cBg);tft.setCursor(110,110);tft.print("Arquivo vazio"); }
  if(txtTrunc&&txtPag==0){ tft.setTextColor(tft.color565(230,180,60),cBg);tft.setCursor(4,232);tft.print("Mostrando so o FINAL do arquivo"); }
  uint16_t on=cLogo, off=tft.color565(60,55,25), brd=tft.color565(46,46,90);
  bool podeCima=txtPag>0, podeBaixo=txtPag<txtTotalPag()-1;
  tft.drawRoundRect(288,26,30,100,5,brd);
  tft.fillTriangle(303,26+34,292,26+66,314,26+66,podeCima?on:off);
  tft.drawRoundRect(288,134,30,100,5,brd);
  tft.fillTriangle(303,134+66,292,134+34,314,134+34,podeBaixo?on:off);
}
void handleTxtViewTap(int tx,int ty){
  if(ty<22&&tx<60){ appState=txtVoltarPara; redrawCurrentScreen(); return; }
  if(tx>=286){
    if(ty<130){ if(txtPag>0){ txtPag--; drawTxtView(); } }
    else if(txtPag<txtTotalPag()-1){ txtPag++; drawTxtView(); }
  }
}
void abrirTextoDoSD(const char* caminho,const char* titulo,AppState voltar,bool fim){
  if(txtAbrir(caminho,titulo,fim)){ txtVoltarPara=voltar; appState=S_TXTVIEW; drawTxtView(); }
  else arqToast("Nao foi possivel abrir o arquivo",false);
}

// ── Visualizador de imagem (Fase 44) -- JPEG baseline do SD, reduzido pra caber em 320x240 ──
char imgCaminho[160]=""; char imgTitulo[36]="";
AppState imgVoltarPara=S_ARQ;
bool imgDesenhar(){
  if(!sdOK) return false;
  File f=SD.open(imgCaminho,FILE_READ);
  if(!f||f.isDirectory()){ if(f) f.close(); return false; }
  size_t n=f.size();
  if(n==0||n>800000){ f.close(); return false; }
  uint8_t* buf=(uint8_t*)ps_malloc(n);
  if(!buf){ f.close(); return false; }
  size_t got=f.read(buf,n); f.close();
  uint16_t w=0,h=0;
  if(got!=n||TJpgDec.getJpgSize(&w,&h,buf,n)!=0||w==0||h==0){ free(buf); return false; }
  int sc=1; while((w/sc>320||h/sc>240)&&sc<8) sc*=2;
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(sc); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(faseJpgOut);
  int r=TJpgDec.drawJpg((320-(int)(w/sc))/2,(240-(int)(h/sc))/2,buf,n);
  TJpgDec.setJpgScale(1);
  free(buf);
  return r==0;
}
void drawImgView(){
  if(!imgDesenhar()){
    tft.fillScreen(cBg);tft.setTextColor(cRed,cBg);tft.setTextSize(1);
    tft.setCursor(40,104);tft.print("Nao foi possivel exibir esta imagem.");
    tft.setTextColor(cSub,cBg);tft.setCursor(40,120);tft.print("Precisa ser JPEG baseline (nao progressivo).");
  }
  tft.fillRect(0,228,320,12,tft.color565(8,8,20));
  tft.setTextColor(tft.color565(200,200,215),tft.color565(8,8,20));tft.setTextSize(1);tft.setCursor(4,231);tft.print(imgTitulo);
  tft.setCursor(226,231);tft.print("toque p/ voltar");
}
void handleImgViewTap(int,int){ appState=imgVoltarPara; redrawCurrentScreen(); }
void abrirImagemDoSD(const char* caminho,const char* titulo,AppState voltar){
  strlcpy(imgCaminho,caminho,sizeof(imgCaminho)); strlcpy(imgTitulo,titulo,sizeof(imgTitulo)); imgVoltarPara=voltar;
  appState=S_IMGVIEW; drawImgView();
}
// Abre o item selecionado no gerenciador: pasta entra, texto/imagem abre no leitor.
static void arqAbrirSelecionado(){
  if(arqSelDir){ char c[100]; arqCaminho(c,sizeof(c),arqDir,arqSelNome); strlcpy(arqDir,c,sizeof(arqDir)); arqScroll=0; appState=S_ARQ; arqCarregar(); drawArqLista(); return; }
  char c[160]; arqCaminho(c,sizeof(c),arqDir,arqSelNome);
  if(arqEhTexto(arqSelNome)) abrirTextoDoSD(c,arqSelNome,S_ARQ,strstr(arqSelNome,".log")||strstr(arqSelNome,".old"));
  else if(arqEhJpeg(arqSelNome)) abrirImagemDoSD(c,arqSelNome,S_ARQ);
}

// ── Toques ──
static void arqSelecionar(const ArqEnt& e){
  strlcpy(arqSelNome,e.nome,ARQ_NOME_MAX); arqSelDir=e.dir; arqSelTam=e.tam;
  appState=S_ARQ_ACAO; drawArqAcao();
}
void handleArqListaTap(int tx,int ty){
  if(ty<24){
    if(tx<40){
      if(arqModoMover && strcmp(arqDir,"/")==0){ arqModoMover=false; arqCarregar(); drawArqLista(); }
      else if(strcmp(arqDir,"/")!=0){ arqSobe(); arqScroll=0; arqCarregar(); drawArqLista(); }
      else { appState=S_SETTINGS; drawSettingsPanel(); }
    }
    else if(arqModoMover && tx>=236) arqMoverAqui();
    else if(!arqModoMover && tx>=284 && sdOK){ arqInputRenomear=false; arqNomeBuf[0]=0; arqNomeLen=0; kbMode=0; appState=S_ARQ_NOME; drawArqNome(); }
    return;
  }
  if(tx>=286){
    int maxS=arqN-ARQ_VIS; if(maxS<0) maxS=0;
    if(ty<ARQ_ROW_Y0+104){ if(arqScroll>0){ arqScroll--; drawArqLista(); } }
    else if(arqScroll<maxS){ arqScroll++; drawArqLista(); }
    return;
  }
  int row=(ty-ARQ_ROW_Y0)/ARQ_ROW_H, idx=arqScroll+row;
  if(row<0||row>=ARQ_VIS||idx>=arqN) return;
  const ArqEnt e=arqLista[idx];
  bool ehVolta=strcmp(e.nome,"..")==0;
  if(ehVolta){ arqSobe(); arqScroll=0; arqCarregar(); drawArqLista(); return; }
  bool dots=!arqModoMover && tx>=244;
  if(dots){ arqSelecionar(e); return; }
  if(!e.dir && !arqModoMover){   // Fase 44: tocar no arquivo ABRE (texto/imagem); os demais vao pro menu de acoes
    if(arqAbrivel(e.nome,false)){ strlcpy(arqSelNome,e.nome,ARQ_NOME_MAX); arqSelDir=false; arqSelTam=e.tam; arqAbrirSelecionado(); }
    else arqSelecionar(e);
    return;
  }
  if(e.dir){ char c[100]; arqCaminho(c,sizeof(c),arqDir,e.nome); strlcpy(arqDir,c,sizeof(arqDir)); arqScroll=0; arqCarregar(); drawArqLista(); }
}
void handleArqAcaoTap(int tx,int ty){
  if(ty<24&&tx<40){ appState=S_ARQ; arqCarregar(); drawArqLista(); return; }
  if(ty>=50&&ty<=94){
    if(tx>=12&&tx<=152){ if(arqAbrivel(arqSelNome,arqSelDir)) arqAbrirSelecionado(); }
    else if(tx>=168&&tx<=308){ arqInputRenomear=true; strlcpy(arqNomeBuf,arqSelNome,ARQ_NOME_MAX); arqNomeLen=strlen(arqNomeBuf); kbMode=0; appState=S_ARQ_NOME; drawArqNome(); }
  }else if(ty>=102&&ty<=146){
    if(tx>=12&&tx<=152){ strlcpy(arqMoverNome,arqSelNome,ARQ_NOME_MAX); strlcpy(arqMoverDirOrigem,arqDir,sizeof(arqMoverDirOrigem)); arqMoverEhDir=arqSelDir; arqModoMover=true; arqScroll=0; appState=S_ARQ; arqCarregar(); drawArqLista(); }
    else if(tx>=168&&tx<=308){ appState=S_ARQ_DEL; drawArqConfirmaDel(); }
  }else if(ty>=154&&ty<=198){
    if(tx>=12&&tx<=308 && !arqSelDir && arqEhJpeg(arqSelNome)){ appState=S_ARQ_WALL; drawArqWall(); }
  }
}
void handleArqWallTap(int tx,int ty){
  if(ty<24&&tx<40){ appState=S_ARQ_ACAO; drawArqAcao(); return; }
  if(tx>=20&&tx<=300){
    if(ty>=56&&ty<=108) arqDefinirWallpaper(0);        // bloqueio (slot 0)
    else if(ty>=120&&ty<=172) arqDefinirWallpaper(1);  // principal (slot 1)
  }
}
void handleArqDelTap(int tx,int ty){
  if(ty>=140&&ty<=190){
    if(tx>=20&&tx<=150){ appState=S_ARQ_ACAO; drawArqAcao(); }
    else if(tx>=170&&tx<=300) arqDeletar();
  }
}
// Espelha handleKbTap()/handleLogsSearchKbTap() (mesmo grid); duplicado de proposito,
// como o da busca de logs, pra nao arriscar regressao no teclado do WiFi.
void handleArqNomeKbTap(int tx,int ty){
  if(ty<28&&tx<62){ appState=arqInputRenomear?S_ARQ_ACAO:S_ARQ; if(arqInputRenomear) drawArqAcao(); else drawArqLista(); return; }
  const char(*KB)[11]=(kbMode==0?KBL:(kbMode==1?KBU:KBN));
  if(ty>=KR1Y&&ty<KR1Y+KH){int ki=(tx-5)/31;if(ki>=0&&ki<10) arqNomeAdd(KB[0][ki]);}
  else if(ty>=KR2Y&&ty<KR2Y+KH){int ki=(tx-20)/31;if(ki>=0&&ki<9) arqNomeAdd(KB[1][ki]);}
  else if(ty>=KR3Y&&ty<KR3Y+KH){if(tx<50){kbMode=(kbMode==0)?1:(kbMode==1)?0:2;drawKeyboard();return;}else if(tx>=262) arqNomeDel();else{int ki=(tx-50)/30;if(ki>=0&&ki<7) arqNomeAdd(KB[2][ki]);}}
  else if(ty>=KR4Y){
    if(tx<63){kbMode=(kbMode==2)?0:2;drawKeyboard();return;}
    else if(tx>=257){ arqRenomearOuCriar(); return; }
    else arqNomeAdd(' ');
  }
}

// ═══ Sincronizacao com o PWA (via Firestore, sempre sob demanda) ═══
// arquivos/indice   : lista {p:caminho,s:tamanho} + usado/total (republicado no boot,
//                     apos mudancas locais e quando o PWA pede "*").
// arquivos/conteudo : o arquivo pedido em base64 (<= ARQ_ENVIO_MAX; logs grandes vao pelo FIM/tail).
static void arqScanRec(const char* dir,int nivel,String& out,int& cnt){
  if(nivel>3 || cnt>=80) return;
  File d=SD.open(dir); if(!d||!d.isDirectory()){ if(d) d.close(); return; }
  File f;
  while(cnt<80 && (f=d.openNextFile())){
    String n=f.name(); int i=n.lastIndexOf('/'); if(i>=0) n=n.substring(i+1);
    bool ok = n.length()>0 && n[0]!='.' && n!="System Volume Information" && n.indexOf('"')<0 && n.indexOf('\\')<0;
    if(ok){
      char c[160]; arqCaminho(c,sizeof(c),dir,n.c_str());
      if(f.isDirectory()){ f.close(); arqScanRec(c,nivel+1,out,cnt); }
      else{ if(cnt>0) out+=","; out+="{\"p\":\""; out+=c; out+="\",\"s\":"; out+=String((unsigned long)f.size()); out+="}"; cnt++; f.close(); }
    }else f.close();
  }
  d.close();
}
void fbArquivoCallback(AsyncResult &aResult){
  if(aResult.isError()){
    Serial.printf("[Firebase Arquivos ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
    if(aResult.uid()=="fbArqIdxTask"){ arqIndiceSujo=true; arqIndiceProximoMs=millis()+30000UL; }   // Fase 44: falhou -> tenta de novo em 30s (antes ficava sem lista pra sempre)
  }
}
// Fase 44: devolve false quando NAO deu pra publicar agora (sem hora valida/rede) -- o
// chamador mantem o indice "sujo" e tenta de novo (antes o aviso era descartado).
bool publicarIndiceSD(){
  if(!sdOK || !fbStarted || !fbApp.ready()) return false;
  time_t now=time(nullptr); if(now<(time_t)EPOCH_2024_01_01) return false;
  struct tm* utc=gmtime(&now); char ts[25]; strftime(ts,sizeof(ts),"%Y-%m-%dT%H:%M:%SZ",utc);
  String json="["; int cnt=0; arqScanRec("/",0,json,cnt); json+="]";
  Document<Values::Value> doc("json",Values::Value(Values::StringValue(jsonEsc(json))));
  doc.add("usadoMB",Values::Value(Values::DoubleValue(number_t(SD.usedBytes()/1048576.0,1))));
  doc.add("totalMB",Values::Value(Values::DoubleValue(number_t(SD.totalBytes()/1048576.0,1))));
  doc.add("atualizadoEm",Values::Value(Values::TimestampValue(String(ts))));
  PatchDocumentOptions opt(DocumentMask("json,usadoMB,totalMB,atualizadoEm"),DocumentMask(),Precondition());
  String path=String(FIRESTORE_DOCUMENT_PATH)+"/arquivos/indice";
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),path,opt,doc,fbArquivoCallback,"fbArqIdxTask");
  Serial.printf("[Arquivos] Indice publicado (%d arquivos)\n",cnt);
  return true;
}

static const char* arqMime(const char* nome){
  const char* p=strrchr(nome,'.'); if(!p) return "application/octet-stream";
  if(!strcasecmp(p,".jpg")||!strcasecmp(p,".jpeg")) return "image/jpeg";
  if(!strcasecmp(p,".png")) return "image/png";
  if(!strcasecmp(p,".gif")) return "image/gif";
  if(!strcasecmp(p,".bmp")) return "image/bmp";
  if(!strcasecmp(p,".txt")||!strcasecmp(p,".log")||!strcasecmp(p,".old")||!strcasecmp(p,".cfg")||!strcasecmp(p,".csv")) return "text/plain";
  return "application/octet-stream";
}
static void arqEnviarConteudo(const String& reqId,const String& path,const char* mime,const String& b64,uint32_t tam,bool truncado,const char* erro){
  Document<Values::Value> doc("reqId",Values::Value(Values::StringValue(reqId)));
  doc.add("path",Values::Value(Values::StringValue(path)));
  doc.add("mime",Values::Value(Values::StringValue(String(mime))));
  doc.add("tam",Values::Value(Values::IntegerValue((int)tam)));
  doc.add("truncado",Values::Value(Values::BooleanValue(truncado)));
  doc.add("erro",Values::Value(Values::StringValue(String(erro))));
  doc.add("b64",Values::Value(Values::StringValue(b64)));
  PatchDocumentOptions opt(DocumentMask("reqId,path,mime,tam,truncado,erro,b64"),DocumentMask(),Precondition());
  String docPath=String(FIRESTORE_DOCUMENT_PATH)+"/arquivos/conteudo";
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),docPath,opt,doc,fbArquivoCallback,"fbArqContTask");
}
void processarArquivoReq(const String& req){
  int bar=req.indexOf('|'); if(bar<=0) return;
  String reqId=req.substring(0,bar), path=req.substring(bar+1);
  if(path=="*"){ if(!publicarIndiceSD()) arqIndiceSujo=true; return; }
  Serial.printf("[Arquivos] Pedido %s: %s\n",reqId.c_str(),path.c_str());
  if(!sdOK||path.length()==0||path[0]!='/'||path.indexOf("..")>=0){ arqEnviarConteudo(reqId,path,"text/plain","",0,false,"Caminho invalido ou SD indisponivel"); return; }
  File f=SD.open(path.c_str(),FILE_READ);
  if(!f||f.isDirectory()){ if(f) f.close(); arqEnviarConteudo(reqId,path,"text/plain","",0,false,"Arquivo nao encontrado"); return; }
  size_t total=f.size(); const char* mime=arqMime(path.c_str());
  bool texto=strncmp(mime,"text/",5)==0;
  size_t ler=total; bool trunc=false;
  if(total>ARQ_ENVIO_MAX){
    if(!texto){ f.close(); arqEnviarConteudo(reqId,path,mime,"",(uint32_t)total,false,"Arquivo maior que 150 KB -- copie pelo leitor de cartao"); return; }
    ler=ARQ_ENVIO_MAX; trunc=true; f.seek(total-ler);   // log grande: envia so o FIM
  }
  uint8_t* raw=(uint8_t*)ps_malloc(ler+1); size_t b64max=((ler+2)/3)*4+4; uint8_t* enc=(uint8_t*)ps_malloc(b64max);
  if(!raw||!enc){ if(raw) free(raw); if(enc) free(enc); f.close(); arqEnviarConteudo(reqId,path,mime,"",0,false,"Sem memoria no dispositivo"); return; }
  size_t got=f.read(raw,ler); f.close();
  size_t olen=0; mbedtls_base64_encode(enc,b64max,&olen,raw,got); enc[olen]=0;
  String b64((const char*)enc);
  free(raw); free(enc);
  arqEnviarConteudo(reqId,path,mime,b64,(uint32_t)got,trunc,"");
  Serial.printf("[Arquivos] Enviado %u bytes (%u base64)\n",(unsigned)got,(unsigned)olen);
}
// Chamado de loop() a cada 3s -- fora de qualquer callback async.
void arqServico(){
  if(!sdOK || !fbStarted || !fbApp.ready()) return;
  static bool indiceBoot=false;
  if(!indiceBoot && millis()>20000){ indiceBoot=true; arqIndiceSujo=true; }
  if(arqReqPend.length()){ String r=arqReqPend; arqReqPend=""; processarArquivoReq(r); return; }
  if(arqIndiceSujo && millis()>=arqIndiceProximoMs){ arqIndiceSujo=false; if(!publicarIndiceSD()) arqIndiceSujo=true; }
}

// ═══════════════════════════════════════════════════════════════
// SEGURANCA / BLOQUEIO DE TELA POR PIN (Fase 46)
//  * "Nenhum" (padrao) ou PIN de 4 digitos. O PIN guarda so um HASH (SHA-256 com sal do
//    dispositivo) em /seg.cfg (LittleFS) -- nunca o PIN em claro.
//  * So da pra ATIVAR o PIN depois de vincular um e-mail de recuperacao: o vinculo e
//    feito no PWA (Config. > Seguranca) com o e-mail da conta logada, e chega aqui pelo
//    campo emailRecuperacao do documento (mesmo poll dos atuadores -- zero leitura extra).
//  * Recuperacao ("Esqueci o PIN"): o aparelho mostra um codigo de 6 digitos; no PWA,
//    logado com o e-mail vinculado, o usuario digita o codigo -> o PWA grava pinRecCodigo
//    -> o aparelho confere e libera a definicao de um PIN novo. (Nao ha servidor de e-mail:
//    a "prova" do e-mail e o login da conta no PWA + a presenca fisica diante do aparelho.)
//  * Bloqueia no boot e ao voltar do screensaver. Reles/automacao seguem rodando.
// ═══════════════════════════════════════════════════════════════
bool pinAtivo=false;
char pinHash[65]="", emailRec[64]="";
bool segStatusSujo=true;                       // publicar "pinAtivo" no Firestore (boot e a cada mudanca)
enum PinModo{ PIN_SET1, PIN_SET2, PIN_DESATIVAR, PIN_ALTERAR, PIN_DESBLOQUEAR, PIN_RECUP };
PinModo pinModo=PIN_DESBLOQUEAR;
char pinBuf[5]="", pinNovo[5]=""; int pinLen=0;
AppState pinDestino=S_MAIN;
bool pinRecuperando=false;                     // true = veio da recuperacao (ao salvar o PIN novo, desbloqueia)
int pinErros=0; unsigned long pinBloqAteMs=0;
char pinMsg[40]="";
char recCodigo[8]=""; unsigned long recExpiraMs=0; bool recAtiva=false; volatile bool recConfirmado=false;
#define SEG_CFG_PATH "/seg.cfg"
#define PIN_MAX_ERROS 5
#define PIN_BLOQ_MS   30000UL
#define REC_VALIDADE_MS (10UL*60UL*1000UL)

static void pinHashDe(const char* pin,char* out65){
  char in[80]; snprintf(in,sizeof(in),"chocaiot|%s|%s",pin,getDeviceId());
  unsigned char h[32]; mbedtls_sha256((const unsigned char*)in,strlen(in),h,0);
  for(int i=0;i<32;i++) sprintf(out65+i*2,"%02x",h[i]);
}
void segCfgSalvar(){
  File c=LittleFS.open(SEG_CFG_PATH,"w"); if(!c) return;
  c.println(pinAtivo?"1":"0"); c.println(pinHash); c.println(emailRec); c.close();
}
void segCfgCarregar(){
  File c=LittleFS.open(SEG_CFG_PATH,"r"); if(!c) return;
  String a=c.readStringUntil('\n'); a.trim(); String h=c.readStringUntil('\n'); h.trim(); String e=c.readStringUntil('\n'); e.trim(); c.close();
  strlcpy(pinHash,h.c_str(),sizeof(pinHash)); strlcpy(emailRec,e.c_str(),sizeof(emailRec));
  pinAtivo=(a=="1" && strlen(pinHash)==64);
}
static void emailMascarado(char* out,size_t n){
  if(!emailRec[0]){ strlcpy(out,"nao vinculado",n); return; }
  const char* at=strchr(emailRec,'@'); if(!at){ strlcpy(out,emailRec,n); return; }
  int loc=at-emailRec, mostra=loc<2?loc:2;
  snprintf(out,n,"%.*s***%s",mostra,emailRec,at);
}
// Chamado do poll dos atuadores (callback): so atualiza dados; nada de desenho aqui.
unsigned long lastLocalEmailMs=0;   // Fase 53 -- edicao local do e-mail (relogio logico vs poll)
void segEmailAtualiza(const String* em){
  if(lastLocalEmailMs && lastLocalEmailMs>=fbPollIssuedAtMs) return;   // poll antigo: nao desfaz a edicao feita aqui
  if(em){ if(strcmp(emailRec,em->c_str())!=0){ strlcpy(emailRec,em->c_str(),sizeof(emailRec)); segCfgSalvar(); } }
  else if(!pinAtivo && emailRec[0]){ emailRec[0]=0; segCfgSalvar(); }   // desvinculado no PWA (so vale sem PIN ativo)
}
bool pinEmRecuperacao(AppState st){ return st==S_PIN && (pinRecuperando||pinModo==PIN_RECUP); }
void recVerificar(const String& cod){
  bool ok=recAtiva && millis()<recExpiraMs && cod==String(recCodigo);
  if(recAtiva) Serial.printf("[PIN] codigo recebido do app: %s -> %s\n",cod.c_str(),ok?"ACEITO":"nao confere/expirou");
  if(ok) recConfirmado=true;
}
void pushSegStatus(){
  if(!fbStarted||!fbApp.ready()) return;
  Document<Values::Value> doc("pinAtivo",Values::Value(Values::BooleanValue(pinAtivo)));
  PatchDocumentOptions opt(DocumentMask("pinAtivo"),DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbPatchCallback,"fbSegTask");
  segStatusSujo=false;
}

// ── Teclado numerico (usado pelo bloqueio e pelas telas de PIN) ──
static const char KPC[12]={'1','2','3','4','5','6','7','8','9','X','0','B'};   // X = esquerda (Esqueci/Cancelar), B = apagar
static void kpDesenha(const char* rotuloEsq){
  uint16_t bg=tft.color565(14,14,38), br=tft.color565(70,70,140);
  for(int i=0;i<12;i++){
    int col=i%3,row=i/3, x=8+col*104, y=88+row*38;
    tft.fillRoundRect(x,y,96,34,7,bg);tft.drawRoundRect(x,y,96,34,7,br);
    char c=KPC[i];
    if(c=='X'){ tft.setTextSize(1);tft.setTextColor(cLogo,bg);tft.setCursor(x+(96-tft.textWidth(rotuloEsq))/2,y+13);tft.print(rotuloEsq); }
    else if(c=='B'){ tft.setTextSize(1);tft.setTextColor(TFT_WHITE,bg);tft.setCursor(x+37,y+13);tft.print("Apagar"); }
    else{ char s[2]={c,0}; tft.setTextSize(2);tft.setTextColor(TFT_WHITE,bg);tft.setCursor(x+42,y+9);tft.print(s); }
  }
}
static void kpPontos(){
  if(appState==S_LOCK) wallSlotRestore(0,90,54,140,26,TFT_BLACK); else tft.fillRect(90,54,140,26,cBg);
  for(int i=0;i<4;i++){
    int cx=112+i*32;
    if(i<pinLen) tft.fillCircle(cx,66,8,cLogo); else tft.drawCircle(cx,66,8,tft.color565(200,200,215));
  }
}
static int kpTecla(int tx,int ty){
  if(ty<88||ty>=88+3*38+34) return -1;
  int row=(ty-88)/38, col=(tx-8)/104; if(tx<8||col<0||col>2||row<0||row>3) return -1;
  int x=8+col*104; if(tx>x+96) return -1;
  if(ty-88-row*38>34) return -1;
  return row*3+col;
}
static void pinLimpa(){ pinLen=0; pinBuf[0]=0; }
static void pinDesbloqueia(){
  pinErros=0; pinMsg[0]=0; pinLimpa();
  appState=pinDestino; lastActivityMs=millis(); redrawCurrentScreen();
}
static void pinToast(const char* m,bool ok){ arqToast(m,ok); }

// ── Tela de bloqueio ──
void drawLock(){
  wallSlotRestore(0,0,0,320,240,TFT_BLACK);
  if(rtcOK){ DateTime n=rtc.now(); char cs[6]; snprintf(cs,6,"%02d:%02d",n.hour(),n.minute());
    tft.setTextSize(4);tft.setTextColor(tft.color565(4,4,12));tft.setCursor(102,7);tft.print(cs);
    tft.setTextColor(tft.color565(225,228,245));tft.setCursor(100,5);tft.print(cs); }
  tft.setTextSize(1);
  unsigned long agora=millis();
  if(agora<pinBloqAteMs){ char b[36]; snprintf(b,36,"Aguarde %lus",(pinBloqAteMs-agora)/1000+1); tft.setTextColor(tft.color565(255,120,120));tft.setCursor(160-tft.textWidth(b)/2,42);tft.print(b); }
  else if(pinMsg[0]){ tft.setTextColor(tft.color565(255,120,120));tft.setCursor(160-tft.textWidth(pinMsg)/2,42);tft.print(pinMsg); }
  else { const char* m="Digite o PIN"; tft.setTextColor(tft.color565(200,200,215));tft.setCursor(160-tft.textWidth(m)/2,42);tft.print(m); }
  kpPontos();
  kpDesenha("Esqueci o PIN");
}
void entrarBloqueio(AppState destino){
  pinDestino=destino; pinModo=PIN_DESBLOQUEAR; pinLimpa(); pinMsg[0]=0;
  appState=S_LOCK; drawLock();
}

// ── Telas de definir/verificar PIN e de recuperacao ──
static const char* pinTitulo(){
  switch(pinModo){
    case PIN_SET1: return "Novo PIN";
    case PIN_SET2: return "Confirme o novo PIN";
    case PIN_DESATIVAR: return "PIN atual (desativar)";
    case PIN_ALTERAR: return "PIN atual";
    default: return "PIN";
  }
}
void drawPin(){
  tft.fillScreen(cBg);
  if(pinModo==PIN_RECUP){
    tft.fillRect(0,0,320,24,cCard);tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(10,8);tft.print("Recuperar PIN");
    char em[70]; emailMascarado(em,sizeof(em));
    tft.setTextColor(cSub,cBg);tft.setCursor(10,34);tft.print("E-mail vinculado:");
    tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(10,46);tft.print(em);
    tft.setTextColor(cSub,cBg);tft.setCursor(10,66);tft.print("Codigo de confirmacao:");
    tft.setTextColor(cLogo,cBg);tft.setTextSize(4);tft.setCursor(70,82);tft.print(recCodigo);
    tft.setTextSize(1);tft.setTextColor(cClk,cBg);
    tft.setCursor(10,126);tft.print("No app (conta com esse e-mail):");
    tft.setCursor(10,138);tft.print("Config. > Seguranca > Recuperar PIN");
    tft.setCursor(10,150);tft.print("e digite o codigo acima.");
    tft.setTextColor(cSub,cBg);tft.setCursor(10,172);tft.print("Aguardando confirmacao... (vale 10 min)");
    arqBotao(90,196,140,34,"Cancelar",tft.color565(16,16,46),TFT_WHITE,tft.color565(55,55,120));
    return;
  }
  tft.setTextColor(cLogo,cBg);tft.setTextSize(2);
  const char* t=pinTitulo(); tft.setCursor(160-tft.textWidth(t)/2,14);tft.print(t);
  tft.setTextSize(1);
  if(pinMsg[0]){ tft.setTextColor(tft.color565(255,120,120),cBg);tft.setCursor(160-tft.textWidth(pinMsg)/2,42);tft.print(pinMsg); }
  kpPontos();
  kpDesenha("Cancelar");
}
static void pinFalhou(){
  pinErros++; pinLimpa();
  if(pinErros>=PIN_MAX_ERROS){ pinBloqAteMs=millis()+PIN_BLOQ_MS; pinErros=0; strlcpy(pinMsg,"Muitas tentativas",sizeof(pinMsg)); }
  else strlcpy(pinMsg,"PIN incorreto",sizeof(pinMsg));
}
static void pinConfirmar(){
  char h[65]; pinHashDe(pinBuf,h);
  switch(pinModo){
    case PIN_DESBLOQUEAR:
      if(strcmp(h,pinHash)==0){ pinDesbloqueia(); return; }
      pinFalhou(); drawLock(); return;
    case PIN_ALTERAR:
      if(strcmp(h,pinHash)==0){ pinModo=PIN_SET1; pinLimpa(); pinMsg[0]=0; } else pinFalhou();
      drawPin(); return;
    case PIN_DESATIVAR:
      if(strcmp(h,pinHash)==0){
        pinAtivo=false; pinHash[0]=0; segCfgSalvar(); segStatusSujo=true; pinLimpa(); pinMsg[0]=0;
        logEvento("Seguranca","Bloqueio por PIN desativado",true);
        appState=S_SEG; drawSeg(); pinToast("PIN desativado",true); drawSeg();
      }else{ pinFalhou(); drawPin(); }
      return;
    case PIN_SET1:
      strlcpy(pinNovo,pinBuf,sizeof(pinNovo)); pinModo=PIN_SET2; pinLimpa(); pinMsg[0]=0; drawPin(); return;
    case PIN_SET2:
      if(strcmp(pinBuf,pinNovo)==0){
        pinHashDe(pinNovo,pinHash); pinAtivo=true; segCfgSalvar(); segStatusSujo=true;
        memset(pinNovo,0,sizeof(pinNovo)); pinLimpa(); pinMsg[0]=0;
        logEvento("Seguranca",pinRecuperando?"PIN redefinido (recuperacao)":"Bloqueio por PIN ativado",true);
        if(pinRecuperando){ pinRecuperando=false; recAtiva=false; pinDesbloqueia(); }
        else{ appState=S_SEG; drawSeg(); pinToast("PIN ativado",true); drawSeg(); }
      }else{ memset(pinNovo,0,sizeof(pinNovo)); pinModo=PIN_SET1; pinLimpa(); strlcpy(pinMsg,"Os PINs nao conferem",sizeof(pinMsg)); drawPin(); }
      return;
    default: return;
  }
}
static void pinDigito(char c){
  if(pinLen>=4) return;
  pinBuf[pinLen++]=c; pinBuf[pinLen]=0; pinMsg[0]=0;
  kpPontos();
  if(pinLen==4){ delay(120); pinConfirmar(); }
}
static void pinApaga(){ if(pinLen>0){ pinBuf[--pinLen]=0; kpPontos(); } }
static void recIniciar(){
  if(!emailRec[0]){ strlcpy(pinMsg,"Sem e-mail vinculado",sizeof(pinMsg)); drawLock(); return; }
  if(!wifiOK||!fbStarted){ strlcpy(pinMsg,"Sem internet p/ recuperar",sizeof(pinMsg)); drawLock(); return; }
  snprintf(recCodigo,sizeof(recCodigo),"%06lu",(unsigned long)(esp_random()%1000000UL));
  recExpiraMs=millis()+REC_VALIDADE_MS; recAtiva=true; recConfirmado=false;
  logEvento("Seguranca","Recuperacao de PIN solicitada na tela de bloqueio",true);
  pinModo=PIN_RECUP; appState=S_PIN; drawPin();
}
void handleLockTap(int tx,int ty){
  if(millis()<pinBloqAteMs){ drawLock(); return; }
  int k=kpTecla(tx,ty); if(k<0) return;
  char c=KPC[k];
  if(c=='X') recIniciar();
  else if(c=='B') pinApaga();
  else pinDigito(c);
}
void handlePinTap(int tx,int ty){
  if(pinModo==PIN_RECUP){
    if(ty>=196&&ty<=230&&tx>=90&&tx<=230){ recAtiva=false; pinModo=PIN_DESBLOQUEAR; appState=S_LOCK; drawLock(); }
    return;
  }
  int k=kpTecla(tx,ty); if(k<0) return;
  char c=KPC[k];
  if(c=='X'){   // Cancelar
    pinLimpa(); pinMsg[0]=0; memset(pinNovo,0,sizeof(pinNovo));
    if(pinRecuperando){ pinRecuperando=false; recAtiva=false; pinModo=PIN_DESBLOQUEAR; appState=S_LOCK; drawLock(); }
    else{ appState=S_SEG; drawSeg(); }
  }
  else if(c=='B') pinApaga();
  else pinDigito(c);
}

// ── Tela Seguranca (Configuracoes > Seguranca) ──
// E-mail de recuperacao digitado no proprio aparelho (Fase 53): usa o teclado de texto generico (abrirKbTxt).
// Sobe pra nuvem em cmd.emailRec (mesmo campo que o PWA usa). Vazio = desvinculado (so sem PIN ativo).
static char emailEditBuf[48]="";
static void emailPersistir(){
  segCfgSalvar(); lastLocalEmailMs=millis();
  if(!fbStarted||!fbApp.ready()) return;
  Document<Values::Value> doc("cmd",Values::Value(Values::MapValue("emailRec",Values::StringValue(jsonEsc(String(emailRec))))));
  PatchDocumentOptions opt(DocumentMask("cmd.emailRec"),DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbPatchCallback,"fbEmailTask");
}
static bool emailValido(const char* e){
  const char* at=strchr(e,'@'); if(!at||at==e) return false;
  if(strchr(at+1,'@')) return false;
  const char* dot=strrchr(at+1,'.'); if(!dot||dot==at+1||!dot[1]) return false;
  for(const char* p=e;*p;p++) if(*p==' '||*p=='"'||*p=='\\'||*p=='|'||*p==',') return false;
  return true;
}
static void emailOk(){
  for(char* p=emailEditBuf;*p;p++) *p=tolower((unsigned char)*p);
  if(!emailEditBuf[0] && !pinAtivo){ emailRec[0]=0; emailPersistir(); appState=S_SEG; drawSeg(); pinToast("E-mail removido",true); drawSeg(); return; }
  if(!emailValido(emailEditBuf)){ pinToast("E-mail invalido",false); drawKbTxtScreen(); return; }
  strlcpy(emailRec,emailEditBuf,sizeof(emailRec)); emailPersistir();
  logEvento("Seguranca","E-mail de recuperacao vinculado no aparelho",true);
  appState=S_SEG; drawSeg(); pinToast("E-mail vinculado",true); drawSeg();
}
static void emailVoltar(){ appState=S_SEG; drawSeg(); }
static void emailAbrirEdicao(){
  strlcpy(emailEditBuf,emailRec,sizeof(emailEditBuf));
  abrirKbTxt("E-mail de recuperacao","Use o e-mail da sua conta do app:",0,emailEditBuf,46,emailOk,emailVoltar);
}
void drawSeg(){
  tft.fillScreen(cBg); uiTopo("SEGURANCA");
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(10,22);tft.print("Bloqueio de tela");
  uint16_t n=tft.color565(16,16,46), nb=tft.color565(55,55,120);
  arqBotao(10,32,140,34,"Nenhum",pinAtivo?n:tft.color565(10,50,20),pinAtivo?TFT_WHITE:cGreen,pinAtivo?nb:cGreen);
  arqBotao(170,32,140,34,"PIN",pinAtivo?tft.color565(10,50,20):n,pinAtivo?cGreen:TFT_WHITE,pinAtivo?cGreen:nb);
  if(pinAtivo) arqBotao(10,72,300,28,"Alterar PIN",n,TFT_WHITE,nb);
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(10,106);tft.print("E-mail de recuperacao");
  char em[70]; emailMascarado(em,sizeof(em));
  if(emailRec[0]) arqBotao(10,116,300,30,em,tft.color565(10,40,20),cGreen,cGreen);
  else            arqBotao(10,116,300,30,"Vincular e-mail",tft.color565(40,30,10),tft.color565(255,190,90),tft.color565(140,100,30));
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(10,152);tft.print("Nome da chocadeira (aparece no app)");
  arqBotao(10,162,300,30,chocNome[0]?chocNome:"Definir nome",tft.color565(16,16,46),cClk,tft.color565(55,55,120));
  tft.setTextSize(1);   // arqBotao deixa a fonte em tamanho 2
  tft.setTextColor(tft.color565(110,110,150),cBg);
  tft.setCursor(10,200);tft.print("Sem e-mail vinculado nao se ativa o PIN.");
  tft.setCursor(10,212);tft.print("Esqueceu? Na tela de bloqueio toque em \"Esqueci o");
  tft.setCursor(10,224);tft.print("PIN\" e digite o codigo no app.");
}
void handleSegTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_SETTINGS; drawSettingsPanel(); return; }
  if(ty>=32&&ty<=66){
    if(tx<=150 && pinAtivo){ pinModo=PIN_DESATIVAR; pinLimpa(); pinMsg[0]=0; appState=S_PIN; drawPin(); }
    else if(tx>=170 && !pinAtivo){
      if(!emailRec[0]){ pinToast("Vincule um e-mail primeiro",false); drawSeg(); return; }
      pinRecuperando=false; pinModo=PIN_SET1; pinLimpa(); pinMsg[0]=0; appState=S_PIN; drawPin();
    }
  }else if(pinAtivo && ty>=72&&ty<=100){ pinModo=PIN_ALTERAR; pinLimpa(); pinMsg[0]=0; appState=S_PIN; drawPin(); }
  else if(ty>=116&&ty<=146){
    if(pinAtivo){ pinToast("Desative o PIN para trocar o e-mail",false); drawSeg(); return; }
    emailAbrirEdicao();
  }
  else if(ty>=162&&ty<=192){ nomeAbrirEdicao(); }
}

// Chamado de loop() a cada 3s (fora de callbacks).
void segServico(){
  if(segStatusSujo && fbStarted && fbApp.ready() && millis()>15000) pushSegStatus();
}
// Rapido, todo loop(): recuperacao de PIN em andamento e contagem do bloqueio temporario.
void segTick(){
  static unsigned long ult=0;
  if(pinOffPend){ pinOffPend=false;
    if(pinAtivo){ pinAtivo=false; pinHash[0]=0; segCfgSalvar(); segStatusSujo=true; logEvento("Seguranca","Bloqueio por PIN desativado pelo app",true);
      if(appState==S_LOCK||appState==S_PIN){ recAtiva=false; pinLimpa(); pinMsg[0]=0; appState=(appState==S_LOCK)?pinDestino:S_SEG; redrawCurrentScreen(); }
      else if(appState==S_SEG) drawSeg(); } }
  // Fase 53 (correcao): a confirmacao do codigo vale em QUALQUER estado. Antes so era tratada com a tela de recuperacao
  // aberta -- se o screensaver entrasse enquanto o usuario digitava o codigo no app, o aparelho nunca desbloqueava.
  if(recAtiva && recConfirmado){
    recConfirmado=false; recAtiva=false; pinRecuperando=true; pinModo=PIN_SET1; pinLimpa();
    strlcpy(pinMsg,"Codigo aceito: defina o novo PIN",sizeof(pinMsg));
    lastActivityMs=millis(); appState=S_PIN; drawPin();
    Serial.println("[PIN] codigo aceito -> tela de novo PIN");
    return;
  }
  if(recAtiva && millis()>recExpiraMs){
    recAtiva=false;
    if(appState==S_PIN && pinModo==PIN_RECUP){ pinModo=PIN_DESBLOQUEAR; strlcpy(pinMsg,"Codigo expirou",sizeof(pinMsg)); appState=S_LOCK; drawLock(); return; }
  }
  if(appState==S_LOCK && pinBloqAteMs && millis()>=pinBloqAteMs && millis()-ult>500){ pinBloqAteMs=0; pinMsg[0]=0; drawLock(); }
  ult=millis();
}

// ═══════════════════════════════════════════════════════════════
// NOTIFICACOES PUSH (Fase 49) -- o proprio aparelho envia (FCM v1, mesma conta de
// servico do Firestore): sem Cloud Functions, sem custo, sem leitura periodica.
//  * Cada PWA que ativa as notificacoes grava seu token em pushTokens (array) do documento
//    da chocadeira. So quando ha algo pra avisar o aparelho LE esse campo (1 leitura) e
//    dispara uma mensagem "data" por token (max 5); o service worker do PWA mostra.
//  * Eventos: temperatura/umidade saiu (ou voltou) da faixa, ultimo dia do ciclo, teste.
//  * Anti-spam: alertas de faixa no maximo 1 a cada 15 min por tipo.
//  * Configuracao no aparelho: Config. > Alertas (persistida em /alert.cfg).
// ═══════════════════════════════════════════════════════════════
bool alertaFaixa=true, alertaFimCiclo=true;
#define ALERT_CFG_PATH "/alert.cfg"
#define PUSH_FILA 4
#define PUSH_COOLDOWN_MS (15UL*60UL*1000UL)
struct PushItem{ char titulo[48]; char corpo[110]; uint8_t tag; };
PushItem pushFila[PUSH_FILA]; int pushFilaN=0;
bool pushAlertaAtivo[2]={false,false};
unsigned long pushUlt[2]={0,0};     // ultimo envio de alerta de faixa: 0 temp, 1 umid
enum { PS_IDLE, PS_ESPERA, PS_ENVIA };
int pushEstado=PS_IDLE; unsigned long pushT0=0; String pushTokensRaw="";
String pushTesteUltimo=""; bool pushTesteInit=false, pushTestePend=false;

void alertCfgSalvar(){ File c=LittleFS.open(ALERT_CFG_PATH,"w"); if(!c) return; c.printf("%d\n%d\n",alertaFaixa?1:0,alertaFimCiclo?1:0); c.close(); }
void alertCfgCarregar(){
  File c=LittleFS.open(ALERT_CFG_PATH,"r"); if(!c) return;
  String a=c.readStringUntil('\n'); String b=c.readStringUntil('\n'); c.close();
  a.trim(); b.trim(); if(a.length()) alertaFaixa=(a=="1"); if(b.length()) alertaFimCiclo=(b=="1");
}
// tipo: 0 temp fora, 1 umid fora, 2 fim de ciclo, 3 teste, 4 temp normalizada, 5 umid normalizada, 6 queda de energia, 7 internet caiu (avisos POS-FATO)
void pushEnfileirar(int tipo,const char* titulo,const char* corpo){
  if((tipo==0||tipo==1||tipo==4||tipo==5) && !alertaFaixa) return;
  if(tipo==6||tipo==7){ for(int i=0;i<pushFilaN;i++) if(pushFila[i].tag==tipo) return; }   // sem duplicar
  if(tipo==4||tipo==5){ int k=tipo-4; if(!pushAlertaAtivo[k]) return; pushAlertaAtivo[k]=false; }   // so avisa "normalizou" se o alerta chegou a ser notificado
  if(tipo==2 && !alertaFimCiclo) return;
  if(tipo==0||tipo==1){
    if(pushUlt[tipo] && millis()-pushUlt[tipo]<PUSH_COOLDOWN_MS){ Serial.println("[Push] alerta suprimido (cooldown de 15 min)."); return; }
    pushUlt[tipo]=millis(); pushAlertaAtivo[tipo]=true;
  }
  if(pushFilaN>=PUSH_FILA){ for(int i=1;i<PUSH_FILA;i++) pushFila[i-1]=pushFila[i]; pushFilaN=PUSH_FILA-1; }   // fila cheia: descarta a mais antiga
  PushItem& it=pushFila[pushFilaN++];
  strlcpy(it.titulo,titulo,sizeof(it.titulo)); strlcpy(it.corpo,corpo,sizeof(it.corpo)); it.tag=(uint8_t)tipo;
  Serial.printf("[Push] enfileirado: %s\n",titulo);
}
void pushTesteRecebido(const String& id,bool presente){
  if(!pushTesteInit){ pushTesteInit=true; if(presente) pushTesteUltimo=id; return; }   // 1a leitura pos-boot so memoriza (campo ausente = "")
  if(presente && id!=pushTesteUltimo){ pushTesteUltimo=id; pushTestePend=true; }
}
void fbPushTokensCallback(AsyncResult &aResult){
  if(aResult.isError()){ Serial.printf("[Push] erro ao ler tokens: %s\n",aResult.error().message().c_str()); pushEstado=PS_IDLE; pushFilaN=0; return; }
  if(!aResult.isResult()||!aResult.available()) return;
  String json=aResult.c_str(); pushTokensRaw="";
  int k=json.indexOf("\"pushTokens\"");
  if(k>=0){
    int fim=json.indexOf(']',k); if(fim<0) fim=json.length();
    int p=k, cnt=0;
    while(cnt<5){
      int sv=json.indexOf("\"stringValue\"",p); if(sv<0||sv>fim) break;
      int colon=json.indexOf(':',sv); int q1=json.indexOf('"',colon+1); int q2=json.indexOf('"',q1+1);
      if(colon<0||q1<0||q2<0) break;
      if(cnt) pushTokensRaw+=','; pushTokensRaw+=json.substring(q1+1,q2); cnt++; p=q2+1;
    }
  }
  pushEstado=PS_ENVIA;
}
void fbPushSendCallback(AsyncResult &aResult){
  if(aResult.isError()) Serial.printf("[Push] falha no envio: %s (code %d)\n",aResult.error().message().c_str(),aResult.error().code());
  else if(aResult.isResult()) Serial.println("[Push] mensagem aceita pelo FCM.");
}
// Bit de preferencia (por dispositivo, escolhido no PWA) de cada tipo de evento. Teste (3) sempre chega.
static int pushBitDoTag(int tag){
  switch(tag){ case 0: case 1: case 4: case 5: return 4; case 2: return 8; case 6: return 1; case 7: return 2; case 8: case 9: return 16; case 16: return 4; default: return 15; }
}
static void pushEnviarUm(const String& token,const PushItem& it){
  Messages::Message msg;
  msg.token(token);
  object_t data,o1,o2,o3; JsonWriter w;
  w.create(o1,"title",string_t(jsonEsc(String(it.titulo))));
  w.create(o2,"body",string_t(jsonEsc(String(it.corpo))));
  char tg[12]; snprintf(tg,sizeof(tg),"choca-%u",(unsigned)it.tag);
  w.create(o3,"tag",string_t(tg));
  w.join(data,3,o1,o2,o3);
  msg.data(data);
  Messages::WebpushConfig wp; object_t hd,h1,h2;
  w.create(h1,"Urgency",string_t("high")); w.create(h2,"TTL",string_t("3600"));
  w.join(hd,2,h1,h2); wp.headers(hd); msg.webpush(wp);
  fbMsg.send(fbClient,Messages::Parent(FIREBASE_PROJECT_ID),msg,fbPushSendCallback,"fbPushTask");
}
// Chamado de loop() a cada 3s (fora de callbacks).
bool pushEnergiaPend=false;   // Fase 53 -- setado no boot se o reset foi por energia
void pushServico(){
  if(pushEnergiaPend && fbStarted && fbApp.ready() && time(nullptr)>=(time_t)EPOCH_2024_01_01){
    pushEnergiaPend=false;
    // Fase 75 -- anti-spam: reinicios repetidos por energia (rede eletrica/EMI do rele) so geram 1 push a cada 30 min
    { time_t ag=time(nullptr); long ult=0;
      File f=LittleFS.open("/queda.cfg","r"); if(f){ ult=f.readStringUntil('\n').toInt(); f.close(); }
      if(ult>0 && ag-ult<1800 && ag>=ult){ logEvento("Energia","Reinicio por energia repetido (instavel) -- push suprimido",true); }
      else{
        File w=LittleFS.open("/queda.cfg","w"); if(w){ w.printf("%ld\n",(long)ag); w.close(); }
        pushEnfileirar(6,"ChocaIoT: queda de energia","O aparelho reiniciou por falta de energia (ou foi religado).");
      }
    }
    if(false) pushEnfileirar(6,"ChocaIoT: queda de energia","O aparelho reiniciou por falta de energia (ou foi religado).");
  }
  if(convitePend[0]){   // Fase 88: convidado aceitou o convite -> push para o dono + log
    // cmd.convite = "<ms>|<email>|<tipo>": tipo "aceito" (padrao), "novodisp" (aparelho novo acessou) ou "negado" (>= 3 acoes bloqueadas) -- Fase 90
    char em[64]="alguem"; char tipo[12]="aceito"; { char* a=strchr(convitePend,'|'); if(a){ char* b=strchr(a+1,'|'); if(b){ size_t n=b-(a+1); if(n>0 && n<sizeof(em)){ memcpy(em,a+1,n); em[n]=0; } strlcpy(tipo,b+1,sizeof(tipo)); } } }
    char corpo[120];
    if(!strcmp(tipo,"novodisp")){ snprintf(corpo,sizeof(corpo),"%s entrou de um aparelho novo. Confira em Configuracoes > Acessos e privacidade.",em); logEvento("Seguranca",corpo,true); pushEnfileirar(3,"ChocaIoT: novo aparelho acessou",corpo); }
    else if(!strcmp(tipo,"negado")){ snprintf(corpo,sizeof(corpo),"%s tentou varias acoes sem permissao (bloqueadas).",em); logEvento("Seguranca",corpo,true); pushEnfileirar(3,"ChocaIoT: tentativas bloqueadas",corpo); }
    else { snprintf(corpo,sizeof(corpo),"%s aceitou o convite e agora acompanha a chocadeira.",em); logEvento("Compartilhamento",corpo,true); pushEnfileirar(3,"ChocaIoT: convite aceito",corpo); }
    convitePend[0]=0;
  }
  if(pushTestePend){ pushTestePend=false; pushEnfileirar(3,"ChocaIoT: teste de notificacao","Notificacoes funcionando neste aparelho."); }
  if(!fbStarted||!fbApp.ready()||!wifiOK){ if(pushEstado==PS_ESPERA) pushEstado=PS_IDLE; return; }
  if(pushEstado==PS_IDLE && pushFilaN>0){
    pushEstado=PS_ESPERA; pushT0=millis();
    GetDocumentOptions options(DocumentMask("pushTokens"));
    fbDocs.get(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,options,fbPushTokensCallback,"fbPushTokTask");
  }else if(pushEstado==PS_ESPERA && millis()-pushT0>20000){ pushEstado=PS_IDLE; }   // sem resposta: tenta de novo na proxima
  else if(pushEstado==PS_ENVIA){
    if(pushTokensRaw.length()==0) Serial.println("[Push] nenhum aparelho com notificacoes ativas -- descartado.");
    else{
      int p=0;
      while(p<(int)pushTokensRaw.length()){
        int v=pushTokensRaw.indexOf(',',p); String tk=(v<0)?pushTokensRaw.substring(p):pushTokensRaw.substring(p,v); p=(v<0)?pushTokensRaw.length():v+1;
        int flags=15; int bar=tk.indexOf('|');          // "token|flags": bit1 queda de energia, bit2 internet, bit4 faixa T/U, bit8 fim de ciclo
        if(bar>=0){ flags=tk.substring(bar+1).toInt(); tk=tk.substring(0,bar); }
        if(tk.length()<20) continue;
        for(int i=0;i<pushFilaN;i++) if(pushBitDoTag(pushFila[i].tag)&flags) pushEnviarUm(tk,pushFila[i]);
      }
    }
    pushFilaN=0; pushEstado=PS_IDLE;
  }
}

// ── Tela Alertas (Config. > Alertas) ──
void drawAlertasScreen(){
  tft.fillScreen(cBg); uiTopo("ALERTAS");
  uint16_t cb=tft.color565(16,16,46);
  uiCard(6,22,308,50);
  auto faixaRow=[&](int y,const char* lb,float mn,float mx,const char* un){
    tft.setTextSize(1);tft.setTextColor(cSub,cb);tft.setCursor(16,y);tft.print(lb);
    char b[24]; snprintf(b,24,"%.1f a %.1f%s",mn,mx,un);
    tft.setTextColor(TFT_WHITE,cb);tft.setCursor(300-tft.textWidth(b),y);tft.print(b);
  };
  faixaRow(32,"Faixa ideal - Temperatura",TEMP_IDEAL_MIN,TEMP_IDEAL_MAX,"C");
  faixaRow(52,"Faixa ideal - Umidade",UMID_IDEAL_MIN,UMID_IDEAL_MAX,"%");
  uiCard(6,78,308,36); tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,92);tft.print("Notificar ao sair da faixa");
  drawToggle(240,82,alertaFaixa,cGreen);
  uiCard(6,118,308,36); tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,132);tft.print("Notificar ultimo dia do ciclo");
  drawToggle(240,122,alertaFimCiclo,cGreen);
  uiCard(6,158,308,34); tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(16,171);tft.print("Notificacao de teste");
  tft.fillRoundRect(232,164,72,22,6,tft.color565(24,40,70)); tft.drawRoundRect(232,164,72,22,6,tft.color565(55,55,120));
  tft.setTextColor(cClk,tft.color565(24,40,70));tft.setCursor(232+(72-tft.textWidth("Testar"))/2,171);tft.print("Testar");
  tft.setTextColor(tft.color565(110,110,150),cBg);tft.setTextSize(1);
  tft.setCursor(10,200);tft.print("Chegam nos celulares que ativaram as notificacoes");
  tft.setCursor(10,212);tft.print("(app: Config. > Notificacoes).");
  tft.setCursor(10,224);tft.print("Faixas: tipo do ciclo (Config. > Ovos).");
}
void handleAlertasTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_SETTINGS; drawSettingsPanel(); return; }
  if(ty>=78&&ty<=114&&tx>=200){ alertaFaixa=!alertaFaixa; alertCfgSalvar(); drawAlertasScreen(); return; }
  if(ty>=118&&ty<=154&&tx>=200){ alertaFimCiclo=!alertaFimCiclo; alertCfgSalvar(); drawAlertasScreen(); return; }
  if(ty>=160&&ty<=190&&tx>=226&&tx<=310){
    if(!wifiOK||!fbStarted){ arqToast("Sem internet",false); drawAlertasScreen(); return; }
    pushFilaN=0; pushEstado=PS_IDLE;   // teste sempre imediato
    pushEnfileirar(3,"ChocaIoT: teste de notificacao","Notificacoes funcionando neste aparelho.");
    arqToast("Teste enviado (aguarde alguns segundos)",true); drawAlertasScreen();
  }
}

// ═══════════════════════════════════════════════════════════════
// CAMERA (Fase 50) -- integracao com o modulo ESP32-S3-CAM (projeto chocaiot-camera).
// Arquitetura pensada na COTA do Firestore: a camera NAO fala com a nuvem. Ela roda um
// servidor HTTP na rede local (mDNS "chocaiot-cam.local": GET /status e GET /foto) e ESTE
// aparelho faz a ponte:
//   * uma task separada (nao trava a tela) faz ping /status a cada 20s e baixa /foto sob pedido;
//   * pedido vem do PWA (campo cameraReq -- ja vai no poll dos atuadores, custo zero de
//     leitura) ou do botao "Tirar foto" da tela Camera;
//   * a foto (JPEG VGA ~40-80 KB) sobe em base64 pra chocadeiras/{id}/camera/ultima; o PWA mostra.
//   * transicao online/offline -> campo cameraOnline (escrita so quando muda).
// ═══════════════════════════════════════════════════════════════
#define CAM_HOST        "chocaiot-cam"
#define CAM_FOTO_MAX    180000UL
volatile bool camPedidoFoto=false;
volatile int  camFotoEstado=0;          // 0 livre, 1 baixando, 2 pronta, 3 erro
uint8_t*      camFotoBuf=nullptr; volatile size_t camFotoLen=0;
char          camFotoReq[24]="", camErro[48]="";
bool          camOnlinePublicado=false, camOnlinePubValido=false;
unsigned long camUltimoPedidoMs=0;
TaskHandle_t  camTask=nullptr;
String camIpAtual="";   // Fase 66 -- IP da camera na rede local, publicado pro PWA falar direto com ela (stream, porta 81)

#define CAM_AUTO_CFG_PATH "/camauto.cfg"
void camAutoCfgSalvar(){ File c=LittleFS.open(CAM_AUTO_CFG_PATH,"w"); if(!c) return; c.println(camAutoAtivo?"1":"0"); c.close(); }
void camAutoCfgCarregar(){ File c=LittleFS.open(CAM_AUTO_CFG_PATH,"r"); if(!c) return; String a=c.readStringUntil(10); c.close(); a.trim(); if(a.length()) camAutoAtivo=(a=="1"); }
unsigned long camAutoIntervaloMs(){   // 0 = nao captura sozinho (sem ciclo, so por pedido manual/PWA)
  // Fase 66 -- "1 foto a cada 5 min durante o ciclo" (pedido explicito do usuario; substitui a escala por
  // fase da Fase 65 -- 30/10/5 min conforme incubacao/pre-eclosao/eclosao -- por um intervalo fixo).
  if(!camAutoAtivo || faseCicloAtual()==FASE_SEM_CICLO) return 0;
  return 5UL*60UL*1000UL;
}
static void camSalvarFoto(const char* pasta);   // definida mais abaixo (secao "pastas do SD")
#define CAM_VIS_W 80
#define CAM_VIS_H 60
uint8_t camVisAtual[CAM_VIS_W*CAM_VIS_H];
uint8_t camVisAnterior[CAM_VIS_W*CAM_VIS_H];
bool camVisTemAnterior=false;
#define CAM_VIS_LIMIAR_BAIXO 1.5f     // abaixo disso: quadro praticamente igual ao anterior
#define CAM_VIS_LIMIAR_PICO  28.0f    // mudanca grande entre 2 quadros consecutivos
#define CAM_VIS_SEM_MOV_ALERTA_MS (6UL*3600UL*1000UL)   // 6h sem nenhum movimento detectado (so avisa na janela de eclosao)
// Callback do TJpg_Decoder (Fase 38/45 ja usam a mesma lib) -- em vez de desenhar na tela, so acumula luma
// numa grade pequena (80x60, escala 8 do JPEG VGA 640x480 original). Nao aloca nada, roda em RAM estatica.
static bool camVisJpgCb(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bitmap){
  for(int j=0;j<h;j++) for(int i=0;i<w;i++){
    int px=x+i, py=y+j; if(px<0||py<0||px>=CAM_VIS_W||py>=CAM_VIS_H) continue;
    uint16_t v=bitmap[j*w+i];
    uint8_t r=(v>>11)&31, g=(v>>5)&63, b=v&31;
    camVisAtual[py*CAM_VIS_W+px]=(uint8_t)((r*8*30+g*4*59+b*8*11)/100);   // aprox. luminancia (Rec.601)
  }
  return true;
}
// Analisa o quadro atual (camFotoBuf/camFotoLen) contra o anterior: movimento (diff medio) e brilho medio.
// So gera ALERTA (nunca muda um atuador) e so dentro da janela de eclosao, onde falta de movimento e um
// sinal relevante -- fora dela (ovo ainda em incubacao) e normal nao haver quase nenhum movimento.
static void camVisAnalisar(){
  TJpgDec.setJpgScale(8); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(camVisJpgCb);
  memset(camVisAtual,0,sizeof(camVisAtual));
  if(TJpgDec.drawJpg(0,0,camFotoBuf,camFotoLen)!=0){ Serial.println("[Visao] falha ao decodificar o JPEG (quadro ignorado)"); return; }
  uint32_t soma=0; for(int i=0;i<CAM_VIS_W*CAM_VIS_H;i++) soma+=camVisAtual[i];
  camVisBrilho = soma/(float)(CAM_VIS_W*CAM_VIS_H);
  if(camVisTemAnterior){
    uint32_t diff=0; for(int i=0;i<CAM_VIS_W*CAM_VIS_H;i++) diff+=abs((int)camVisAtual[i]-(int)camVisAnterior[i]);
    camVisUltimoScore = diff/(float)(CAM_VIS_W*CAM_VIS_H);
  }else camVisUltimoScore=-1;   // 1o quadro: nada pra comparar ainda
  memcpy(camVisAnterior,camVisAtual,sizeof(camVisAtual)); camVisTemAnterior=true;
  if(camVisUltimoScore<0){ Serial.printf("[Visao] brilho=%.1f movimento=(1o quadro)\n",camVisBrilho); return; }
  Serial.printf("[Visao] brilho=%.1f movimento=%.1f\n",camVisBrilho,camVisUltimoScore);

  unsigned long agora=millis(); bool baixo=(camVisUltimoScore<CAM_VIS_LIMIAR_BAIXO);
  if(!baixo) camVisSemMovimentoDesdeMs=0; else if(!camVisSemMovimentoDesdeMs) camVisSemMovimentoDesdeMs=agora;
  FaseCiclo f=faseCicloAtual();
  if(f==FASE_ECLOSAO){
    if(camVisSemMovimentoDesdeMs && agora-camVisSemMovimentoDesdeMs>CAM_VIS_SEM_MOV_ALERTA_MS){
      static unsigned long ultAviso=0;
      if(!ultAviso || agora-ultAviso>PUSH_COOLDOWN_MS){ ultAviso=agora;
        unsigned long h=(agora-camVisSemMovimentoDesdeMs)/3600000UL;
        char m[96]; snprintf(m,sizeof(m),"Nenhum movimento detectado nas fotos ha %luh (estimativa por imagem, nao confirmado)",h);
        logEvento("Visao",m,true); pushEnfileirar(8,"ChocaIoT: sem movimento detectado",m);
      }
    }
    if(camVisUltimoScore>CAM_VIS_LIMIAR_PICO){
      static unsigned long ultPico=0;
      if(!ultPico || agora-ultPico>PUSH_COOLDOWN_MS){ ultPico=agora;
        logEvento("Visao","Variacao grande entre fotos (estimativa de possivel eclosao/quebra) - confira a imagem",true);
        pushEnfileirar(9,"ChocaIoT: possivel eclosao detectada","Variacao grande entre fotos perto do dia de eclosao. Confira a ultima imagem no app (estimativa, nao confirmado).");
        camSalvarFoto("/cam/alertas");   // Fase 66 -- guarda o quadro que disparou o aviso, pra conferir depois
      }
    }
  }
}
// Fase 66 -- pastas dedicadas no SD do CONTROLADOR (o PWA ja navega o SD via o indexador de Arquivos,
// Fase 41 -- e so navegar ate /cam/... por la; sem componente novo no PWA): /cam/timelapse (capturas
// automaticas), /cam/manual (toque em "Tirar foto", aparelho ou app), /cam/alertas (quadro que disparou
// um aviso de visao, ver camVisAnalisar). Substitui a pasta unica "/timelapse" da Fase 65.
static void camSalvarFoto(const char* pasta){
  if(!sdOK||!camFotoBuf||!camFotoLen) return;
  if(!SD.exists("/cam")) SD.mkdir("/cam");
  if(!SD.exists(pasta)) SD.mkdir(pasta);
  if(SD.totalBytes()>0 && (SD.usedBytes()*100ULL)/SD.totalBytes()>90ULL){   // SD quase cheio: apaga o arquivo mais antigo da pasta antes de gravar
    File dir=SD.open(pasta); String maisAntigo; if(dir){ File f=dir.openNextFile(); if(f){ maisAntigo=String(pasta)+"/"+f.name(); } dir.close(); }
    if(maisAntigo.length()) SD.remove(maisAntigo);
  }
  char nome[56]; time_t now=time(nullptr);
  if(now>=(time_t)EPOCH_2024_01_01){ time_t l=now+tzOffsetSeg(); struct tm t; gmtime_r(&l,&t); snprintf(nome,sizeof(nome),"%s/%04d%02d%02d_%02d%02d%02d.jpg",pasta,t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec); }
  else snprintf(nome,sizeof(nome),"%s/foto_%lu.jpg",pasta,millis());
  File f=SD.open(nome,FILE_WRITE); if(f){ f.write(camFotoBuf,camFotoLen); f.close(); arqIndiceSujo=true; }
}

static void camTaskFn(void*){
  bool mdnsOk=false; unsigned long ultPing=0;
  for(;;){
    vTaskDelay(pdMS_TO_TICKS(250));
    if(WiFi.status()!=WL_CONNECTED){ camOnlineLan=false; continue; }
    if(!mdnsOk){ mdnsOk=MDNS.begin("chocaiot-ctrl"); if(!mdnsOk){ vTaskDelay(pdMS_TO_TICKS(5000)); continue; } }
    bool pedido=camPedidoFoto;
    if(!pedido && millis()-ultPing<20000UL) continue;
    ultPing=millis();
    IPAddress ip=MDNS.queryHost(CAM_HOST,1500);
    if((uint32_t)ip==0){ camOnlineLan=false; if(pedido){ strlcpy(camErro,"Camera nao encontrada na rede",sizeof(camErro)); camFotoEstado=3; camPedidoFoto=false; } continue; }
    camIpAtual=ip.toString();   // Fase 66
    HTTPClient http; http.setConnectTimeout(1500);
    if(!pedido){
      http.setTimeout(1500);
      bool ok=http.begin(ip.toString(),80,"/status") && http.GET()==200;
      http.end(); camOnlineLan=ok; continue;
    }
    // baixa a foto (a camera descarta 2 quadros de aquecimento: pode levar alguns segundos)
    camFotoEstado=1; http.setTimeout(20000);
    bool ok=false; size_t total=0;
    if(http.begin(ip.toString(),80,"/foto")){
      int code=http.GET();
      if(code==200){
        int len=http.getSize();
        if(!camFotoBuf) camFotoBuf=(uint8_t*)ps_malloc(CAM_FOTO_MAX);
        if(camFotoBuf && (len<0 || (size_t)len<=CAM_FOTO_MAX)){
          WiFiClient* st=http.getStreamPtr(); unsigned long t0=millis();
          while(http.connected() && (len<0||(int)total<len) && total<CAM_FOTO_MAX && millis()-t0<20000UL){
            size_t av=st->available();
            if(av){ size_t n=st->readBytes(camFotoBuf+total,min(av,(size_t)(CAM_FOTO_MAX-total))); total+=n; }
            else vTaskDelay(pdMS_TO_TICKS(5));
          }
          ok=(total>1000 && camFotoBuf[0]==0xFF && camFotoBuf[1]==0xD8);
          if(!ok) strlcpy(camErro,"Foto recebida incompleta",sizeof(camErro));
        }else strlcpy(camErro,"Foto grande demais ou sem memoria",sizeof(camErro));
      }else{ snprintf(camErro,sizeof(camErro),"Camera respondeu %d",code); }
    }else strlcpy(camErro,"Falha ao falar com a camera",sizeof(camErro));
    http.end();
    camOnlineLan=true;
    if(ok){ camFotoLen=total; camFotoEstado=2; } else camFotoEstado=3;
    camPedidoFoto=false;
  }
}
void cameraIniciarTask(){ if(!camTask) xTaskCreate(camTaskFn,"camLan",10240,nullptr,1,&camTask); }

void fbCamCallback(AsyncResult &aResult){
  if(aResult.isError()) Serial.printf("[Firebase Camera ERRO] %s, code: %d\n",aResult.error().message().c_str(),aResult.error().code());
}
static void cameraEnviarResultado(const char* reqId,bool ok){
  time_t now=time(nullptr); struct tm* u=gmtime(&now); char ts[25]; strftime(ts,sizeof(ts),"%Y-%m-%dT%H:%M:%SZ",u);
  String b64;
  if(ok){
    size_t b64max=((camFotoLen+2)/3)*4+4; uint8_t* enc=(uint8_t*)ps_malloc(b64max);
    if(!enc){ ok=false; strlcpy(camErro,"Sem memoria no aparelho",sizeof(camErro)); }
    else{ size_t olen=0; mbedtls_base64_encode(enc,b64max,&olen,camFotoBuf,(size_t)camFotoLen); enc[olen]=0; b64=String((const char*)enc); free(enc); }
  }
  Document<Values::Value> doc("reqId",Values::Value(Values::StringValue(String(reqId))));
  doc.add("mime",Values::Value(Values::StringValue(String("image/jpeg"))));
  doc.add("tam",Values::Value(Values::IntegerValue((int)(ok?camFotoLen:0))));
  doc.add("tirouEm",Values::Value(Values::TimestampValue(String(ts))));
  doc.add("erro",Values::Value(Values::StringValue(String(ok?"":camErro))));
  doc.add("b64",Values::Value(Values::StringValue(b64)));
  // Fase 65 -- visao (so estimativa/alerta, ver comentario acima de camVisAnalisar): -1 = sem dado ainda
  doc.add("camMov",Values::Value(Values::DoubleValue(number_t(ok?camVisUltimoScore:-1.0f,1))));
  doc.add("camBrilho",Values::Value(Values::DoubleValue(number_t(ok?camVisBrilho:-1.0f,1))));
  doc.add("camFase",Values::Value(Values::StringValue(String(faseCicloNome(faseCicloAtual())))));
  doc.add("camSemMovH",Values::Value(Values::IntegerValue((int)(camVisSemMovimentoDesdeMs?(millis()-camVisSemMovimentoDesdeMs)/3600000UL:0))));
  PatchDocumentOptions opt(DocumentMask("reqId,mime,tam,tirouEm,erro,b64,camMov,camBrilho,camFase,camSemMovH"),DocumentMask(),Precondition());
  String path=String(FIRESTORE_DOCUMENT_PATH)+"/camera/ultima";
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),path,opt,doc,fbCamCallback,"fbCamTask");
  Serial.printf("[Camera] resultado enviado (%s, %u bytes)\n",ok?"foto":"erro",(unsigned)(ok?camFotoLen:0));
}
String camIpPublicado="";
static void cameraPublicarOnline(){
  if(!fbStarted||!fbApp.ready()) return;
  bool on=camOnlineLan;
  bool ipMudou=(camIpAtual!=camIpPublicado);
  if(camOnlinePubValido && on==camOnlinePublicado && !ipMudou) return;
  Document<Values::Value> doc("cameraOnline",Values::Value(Values::BooleanValue(on)));
  doc.add("cameraIP",Values::Value(Values::StringValue(camIpAtual)));   // Fase 66 -- PWA monta http://<ip>:81/stream sozinho, sem passar pela nuvem
  PatchDocumentOptions opt(DocumentMask("cameraOnline,cameraIP"),DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbPatchCallback,"fbCamOnTask");
  camOnlinePublicado=on; camOnlinePubValido=true; camIpPublicado=camIpAtual;
}
// Fase 65 -- publica o estado do toggle "Captura automatica" (campo top-level, o PWA le no mesmo onSnapshot que ja usa
// pra atuadores/ciclo -- zero leitura nova) sempre que muda, local ou vindo da nuvem.
bool camAutoPublicado=false, camAutoPubValido=false;
static void camAutoPublicarSeMudou(){
  if(!fbStarted||!fbApp.ready()) return;
  if(camAutoPubValido && camAutoAtivo==camAutoPublicado) return;
  Document<Values::Value> doc("camAutoAtivo",Values::Value(Values::BooleanValue(camAutoAtivo)));
  PatchDocumentOptions opt(DocumentMask("camAutoAtivo"),DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbPatchCallback,"fbCamAutoTask");
  camAutoPublicado=camAutoAtivo; camAutoPubValido=true;
}
// Chamado de loop() a cada 3s (fora de callbacks). Pedido da nuvem: camReqPend ("id"); local: prefixo "fw"; automatico: prefixo "auto".
unsigned long camAutoUltimaMs=0;
void cameraServico(){
  cameraIniciarTask();
  if(!fbStarted||!fbApp.ready()) return;
  cameraPublicarOnline();
  camAutoPublicarSeMudou();
  // Fase 65 -- captura periodica (timelapse), so quando ninguem mais pediu uma foto agora
  unsigned long intervalo=camAutoIntervaloMs();
  if(intervalo>0 && camOnlineLan && camReqPend.length()==0 && camFotoEstado==0 && !camPedidoFoto && millis()-camAutoUltimaMs>=intervalo){
    camAutoUltimaMs=millis();
    char id[16]; snprintf(id,sizeof(id),"auto%lx",(unsigned long)millis());
    camReqPend=String(id);
  }
  if(camReqPend.length() && camFotoEstado==0 && !camPedidoFoto){
    String id=camReqPend; camReqPend="";
    strlcpy(camFotoReq,id.c_str(),sizeof(camFotoReq));
    if(!camOnlineLan){ strlcpy(camErro,"Camera nao conectada",sizeof(camErro)); if(!String(camFotoReq).startsWith("auto")) cameraEnviarResultado(camFotoReq,false); }
    else{ camUltimoPedidoMs=millis(); camPedidoFoto=true; }
  }
  if(camFotoEstado==2){
    String origem=camFotoReq;
    if(origem.startsWith("live")){
      camLiveTemQuadro=true;   // Fase 66 -- so decodifica em tela cheia; nao gasta cota nem SD com quadros do "ao vivo"
    }else{
      camVisAnalisar();       // so alertas/log (ver comentario); nunca muda um atuador
      camSalvarFoto(origem.startsWith("auto")?"/cam/timelapse":"/cam/manual");
      cameraEnviarResultado(camFotoReq,true);
    }
    camFotoEstado=0;
  }else if(camFotoEstado==3){ if(!String(camFotoReq).startsWith("live")) cameraEnviarResultado(camFotoReq,false); camFotoEstado=0; }
}
void camLiveTick(){   // Fase 66 -- so roda com a tela Camera aberta e o toggle "Ao vivo" ligado
  if(appState!=S_CAMERA || !camLiveAtivo) return;
  static unsigned long ultPedido=0;
  if(camLiveTemQuadro){
    camLiveTemQuadro=false;
    TJpgDec.setJpgScale(2); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(faseJpgOut);   // VGA (640x480) / 2 = 320x240: cabe exato na tela
    TJpgDec.drawJpg(0,0,camFotoBuf,camFotoLen);
    TJpgDec.setJpgScale(1);
    uiTopo("CAMERA");   // a imagem cobre a tela inteira -- redesenha o cabecalho e o rodape por cima
    drawCameraLiveOverlayRodape();
    tft.fillCircle(300,28,4,cameraOnline()?cGreen:cRed);
  }
  if(millis()-ultPedido<5000UL) return;   // pede um quadro novo a cada 5s (pedido: 2-10s) -- nao trava o resto do aparelho
  if(!cameraOnline()) return;
  if(camReqPend.length()==0 && camFotoEstado==0 && !camPedidoFoto){
    ultPedido=millis();
    char id[16]; snprintf(id,sizeof(id),"live%lx",(unsigned long)millis());
    camReqPend=String(id);
  }
}
void cameraPedirFoto(){
  if(!cameraOnline()){ arqToast("Camera nao conectada",false); drawCameraScreen(); return; }
  if(!fbStarted||!fbApp.ready()){ arqToast("Sem conexao com a nuvem",false); drawCameraScreen(); return; }
  if(camFotoEstado!=0||camPedidoFoto||millis()-camUltimoPedidoMs<6000){ arqToast("Aguarde a foto anterior",false); drawCameraScreen(); return; }
  char id[16]; snprintf(id,sizeof(id),"fw%lx",(unsigned long)millis());
  camReqPend=String(id);
  logEvento("Camera","Foto solicitada pelo aparelho",false);
  arqToast("Foto pedida - veja no app",true); drawCameraScreen();
}
void handleCameraTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ camLiveAtivo=false; appState=S_MAIN; drawMainScreen(); return; }   // sair da tela desliga o "ao vivo" (defesa extra contra superaquecimento)
  if(!camLiveAtivo && ty>=102&&ty<=138&&tx>=200){ camAutoAtivo=!camAutoAtivo; lastLocalCamAutoMs=millis(); camAutoCfgSalvar(); drawCameraScreen(); return; }
  if(ty>=CAM_LIVE_ROW_Y&&ty<=CAM_LIVE_ROW_Y+36){
    if(tx>=4&&tx<=154){ camLiveAtivo=!camLiveAtivo; camLiveTemQuadro=false; drawCameraScreen(); return; }
    if(tx>=160&&tx<=316) cameraPedirFoto();
    return;
  }
}

// ═══════════════════════════════════════════════════════════════
// ENVIO DE MIDIA DO PWA PARA O APARELHO (Fase 51) -- a imagem escolhida no PWA (convertida pra JPEG
// baseline <= ~80 KB no proprio navegador) cai em /download/ no cartao SD.
//   PWA  -> chocadeiras/{id}/uploads/atual = {reqId, nome, mime, b64}  e  cmd.upReq = reqId
//   FW   -> ve cmd.upReq no poll (custo 0), le uploads/atual (1 leitura), decodifica, grava /download/<nome>,
//           responde em uploads/atual.status ("ok"/"erro"), limpa b64 e republica o indice do SD.
// Nada de trabalho pesado dentro de callback: o callback so copia o texto; loop() decodifica e grava.
// ═══════════════════════════════════════════════════════════════
#define DOWNLOAD_DIR   "/download"
#define UP_MAX_B64     200000UL
String upB64="", upNome="", upReqId="";
volatile int upEstado=0;          // 0 livre, 1 lendo do Firestore, 2 recebido (decodificar), 3 erro na leitura
unsigned long upT0=0;
char upErro[48]="";

void fbUploadGetCallback(AsyncResult &aResult){
  if(aResult.isError()){ Serial.printf("[Upload] erro ao ler: %s\n",aResult.error().message().c_str()); strlcpy(upErro,"Falha ao ler o envio",sizeof(upErro)); upEstado=3; return; }
  if(!aResult.isResult()||!aResult.available()) return;
  String j=aResult.c_str();
  String rid; extrairCampoString(j,"reqId",rid);
  if(rid!=upReqId){ strlcpy(upErro,"Envio nao encontrado (id diferente)",sizeof(upErro)); upEstado=3; return; }
  extrairCampoString(j,"nome",upNome);
  int k=j.indexOf("\"b64\"");
  if(k<0){ strlcpy(upErro,"Envio sem conteudo",sizeof(upErro)); upEstado=3; return; }
  int sv=j.indexOf("\"stringValue\"",k), colon=(sv<0)?-1:j.indexOf(':',sv);
  int q1=(colon<0)?-1:j.indexOf('"',colon+1), q2=(q1<0)?-1:j.indexOf('"',q1+1);
  if(q2<0||(unsigned long)(q2-q1)>UP_MAX_B64){ strlcpy(upErro,"Arquivo grande demais",sizeof(upErro)); upEstado=3; return; }
  upB64=j.substring(q1+1,q2);
  upEstado=2;
}
void fbUploadCallback(AsyncResult &aResult){
  if(aResult.isError()) Serial.printf("[Upload] erro ao responder: %s\n",aResult.error().message().c_str());
}
static void uploadResponder(bool ok,const char* msg){
  Document<Values::Value> doc("status",Values::Value(Values::StringValue(String(ok?"ok":"erro"))));
  doc.add("msg",Values::Value(Values::StringValue(jsonEsc(String(msg)))));
  doc.add("b64",Values::Value(Values::StringValue(String(""))));   // libera o documento (era ~100 KB)
  PatchDocumentOptions opt(DocumentMask("status,msg,b64"),DocumentMask(),Precondition());
  String path=String(FIRESTORE_DOCUMENT_PATH)+"/uploads/atual";
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),path,opt,doc,fbUploadCallback,"fbUpRespTask");
}
static void uploadSalvar(){
  bool ok=false; char msg[64]="";
  size_t maxb=upB64.length()/4*3+8;
  uint8_t* raw=(uint8_t*)ps_malloc(maxb);
  if(!sdOK) strlcpy(msg,"Cartao SD indisponivel",sizeof(msg));
  else if(!raw) strlcpy(msg,"Sem memoria no aparelho",sizeof(msg));
  else{
    size_t olen=0;
    int r=mbedtls_base64_decode(raw,maxb,&olen,(const unsigned char*)upB64.c_str(),upB64.length());
    if(r!=0||olen<200) strlcpy(msg,"Conteudo invalido",sizeof(msg));
    else if(!(raw[0]==0xFF&&raw[1]==0xD8)) strlcpy(msg,"So JPEG e aceito",sizeof(msg));
    else{
      if(!SD.exists(DOWNLOAD_DIR)) SD.mkdir(DOWNLOAD_DIR);
      char base[40]; size_t k=0;                      // nome seguro: so [A-Za-z0-9._-], termina em .jpg
      for(size_t i=0;i<upNome.length()&&k<32;i++){ char c=upNome[i]; base[k++]=(isalnum((unsigned char)c)||c=='-'||c=='_')?c:'_'; }
      if(k==0){ strcpy(base,"imagem"); k=6; } base[k]=0;
      char p[80]; snprintf(p,sizeof(p),"%s/%s.jpg",DOWNLOAD_DIR,base);
      for(int n=2;SD.exists(p)&&n<100;n++) snprintf(p,sizeof(p),"%s/%s_%d.jpg",DOWNLOAD_DIR,base,n);
      File f=SD.open(p,FILE_WRITE);
      if(f){ size_t w=f.write(raw,olen); f.close();
        if(w==olen){ ok=true; strlcpy(msg,p,sizeof(msg)); } else strlcpy(msg,"Falha ao gravar no cartao",sizeof(msg)); }
      else strlcpy(msg,"Falha ao criar o arquivo",sizeof(msg));
    }
  }
  if(raw) free(raw);
  upB64=""; upNome="";
  Serial.printf("[Upload] %s: %s\n",ok?"salvo":"ERRO",msg);
  char lg[80]; snprintf(lg,sizeof(lg),"Midia recebida do app: %s",msg); logEvento("Arquivos",lg,false);
  if(ok) arqIndiceSujo=true;
  uploadResponder(ok,msg);
}
// Chamado de loop() a cada 3s (fora de callbacks).
void uploadServico(){
  if(!fbStarted||!fbApp.ready()) return;
  if(upEstado==0 && upReqPend.length()){
    upReqId=upReqPend; upReqPend=""; upEstado=1; upT0=millis(); upErro[0]=0;
    GetDocumentOptions options(DocumentMask("reqId,nome,b64"));
    fbDocs.get(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),String(FIRESTORE_DOCUMENT_PATH)+"/uploads/atual",options,fbUploadGetCallback,"fbUpGetTask");
    Serial.printf("[Upload] pedido %s -- lendo do Firestore\n",upReqId.c_str());
  }else if(upEstado==2){ uploadSalvar(); upEstado=0; }
  else if(upEstado==3){ uploadResponder(false,upErro); upEstado=0; }
  else if(upEstado==1 && millis()-upT0>40000UL){ uploadResponder(false,"Tempo esgotado lendo o envio"); upEstado=0; }
}

// ── Comandos de depuracao pela serial (nao afetam o uso normal) ──
//   p = "print screen" (le o TFT pixel a pixel e despeja em hex)   s = entra no screensaver   x = sai do screensaver
static void serialShot(){
  static uint16_t row[320]; static char lin[320*4+2];
  Serial.println("SHOT_BEGIN 320 240");
  for(int y=0;y<240;y++){
    tft.readRect(0,y,320,1,row);
    for(int x=0;x<320;x++) sprintf(lin+x*4,"%04x",row[x]);
    lin[1280]='\n'; Serial.write((const uint8_t*)lin,1281);
  }
  Serial.println("SHOT_END");
}
void serialDebug(){
  while(Serial.available()){
    char c=(char)Serial.read();
    if(c=='p') serialShot();
    else if(c=='h') histForcar();
    else if(c=='c'||c=='g'||c=='e'||c=='C'||c=='l'||c=='m'||c=='n'||c=='N'||c=='k'||c=='i'||c=='a'||c=='d'||c=='w'||c=='z'||c=='Q') debugTela(c);   // c = tela do ciclo, g = seguranca, e = editor de tipo (pag. ovoscopia)
    else if(c>='0'&&c<='9'){   // telas do padrao novo (Fase 64): 1-3 memoria, 4 sistema, 5 logs, 6 alertas, 7 seguranca, 8 data/hora, 9 atuadores, 0 audio
      if(c>='1'&&c<='3'){ memPage=c-'1'; appState=S_MEMORIA; drawMemoriaScreen(); drawMemCurrentPageContent(); }
      else if(c=='4'){ appState=S_SYS_INFO; drawSysInfo(); }
      else if(c=='5'){ abrirTelaLogs(); }
      else if(c=='6'){ appState=S_ALERTAS; drawAlertasScreen(); }
      else if(c=='7'){ appState=S_SEG; drawSeg(); }
      else if(c=='8'){ appState=S_DATETIME; loadEditFromRtc(); drawDateTimeScreen(); }
      else if(c=='9'){ appState=S_ATUADORES; atPageOff=0; drawAtuadoresScreen(); }
      else { appState=S_AUDIO; drawAudioScreen(); }
    }
    else if(c=='v'){   // TESTE (sem camera real): roda o pipeline de visao/timelapse com o wallpaper embutido como "foto" -- so valida decode/diff/SD, nao a deteccao real
      if(!camFotoBuf) camFotoBuf=(uint8_t*)ps_malloc(CAM_FOTO_MAX);
      if(camFotoBuf && WALLPAPER_JPG_LEN<=CAM_FOTO_MAX){
        memcpy(camFotoBuf,WALLPAPER_JPG,WALLPAPER_JPG_LEN); camFotoLen=WALLPAPER_JPG_LEN;
        camVisAnalisar(); camSalvarFoto("/cam/timelapse");
        Serial.printf("[TESTE] visao: brilho=%.1f mov=%.1f fase=%s semMovH=%lu sdOK=%d\n",camVisBrilho,camVisUltimoScore,faseCicloNome(faseCicloAtual()),camVisSemMovimentoDesdeMs?(millis()-camVisSemMovimentoDesdeMs)/3600000UL:0,sdOK);
      }else Serial.println("[TESTE] sem memoria para o buffer de teste");
    }
    else if(c=='D'){   // TESTE: roda so a ANIMACAO de desligar (sem dormir de verdade -- pra tirar print)
      tft.fillScreen(cBg);
      efeitoAnelDesligar(cRed);
      tft.setTextColor(cLogo,cBg);tft.setTextSize(2);
      { const char* m="Desligando..."; tft.setCursor(160-tft.textWidth(m)/2,130); tft.print(m); }
      tft.setTextColor(cSub,cBg);tft.setTextSize(1);
      { const char* m2="Pressione BOOT ou RST na placa para ligar"; tft.setCursor(160-tft.textWidth(m2)/2,152); tft.print(m2); }
      delay(500);
      efeitoColapsoCRT(cRed);
      delay(600);
      appState=S_MAIN; drawMainScreen();
      Serial.println("[TESTE] animacao de desligar concluida (sem dormir)");
    }
    else if(c=='R'){   // TESTE: roda so a ANIMACAO de reiniciar (sem reiniciar de verdade)
      tft.fillScreen(cBg);
      efeitoSpinnerReiniciar(cLogo);
      tft.setTextColor(cLogo,cBg);tft.setTextSize(2);
      { const char* m="Reiniciando..."; tft.setCursor(160-tft.textWidth(m)/2,140); tft.print(m); }
      delay(600);
      appState=S_MAIN; drawMainScreen();
      Serial.println("[TESTE] animacao de reiniciar concluida (sem reiniciar)");
    }
    else if(c=='V'){   // TESTE: forca o modo "Ao vivo" com o wallpaper como quadro de mentira (valida decode em tela cheia)
      if(!camFotoBuf) camFotoBuf=(uint8_t*)ps_malloc(CAM_FOTO_MAX);
      if(camFotoBuf && WALLPAPER_JPG_LEN<=CAM_FOTO_MAX){
        memcpy(camFotoBuf,WALLPAPER_JPG,WALLPAPER_JPG_LEN); camFotoLen=WALLPAPER_JPG_LEN;
        appState=S_CAMERA; camLiveAtivo=true; camLiveTemQuadro=true; drawCameraScreen(); camLiveTick();
        Serial.println("[TESTE] modo ao vivo forcado com wallpaper");
      }
    }
    else if(c=='q'||c=='Q'){ detailShowPass=(c=='q'); appState=S_WIFI_DETAIL; drawWifiDetail(); }
    else if(c=='y'){ connectWifi(savedSSID,savedPass); Serial.println("[TESTE] connectWifi terminou"); }
    else if(c=='o'){ debugWifiOff=!debugWifiOff; if(debugWifiOff) WiFi.disconnect(); Serial.printf("[TESTE] WiFi %s\n",debugWifiOff?"DESLIGADO (sem reconectar)":"liberado (reconecta)"); }
    else if(c=='j'||c=='J'||c=='u') debugHardware(c=='j'?0:c=='J'?1:2);   // Fase 86: abre a tela Hardware (j = pagina 1, J = pagina 2)
    else if(c=='W') aguaSimular();
    else if(c=='A') adxlImprime();      // Fase 91 -- ADXL345: imprime x/y/z e o desvio
    else if(c=='K') adxlCalibrar(false);     // calibra a posicao atual como "fechada"
    else if(c=='I') adxlInit();
    else if(c=='P') pwmAlternar();      // Fase 99 -- liga/desliga o PWM preparado (GPIO4/5/6/47)
    else if(c=='L'){ adxlLive=!adxlLive; Serial.printf("[ADXL345] visualizacao ao vivo %s (tecla L alterna)\n",adxlLive?"LIGADA":"desligada"); }         // varre o I2C e inicia o ADXL de novo
    else if(c=='B') aguaAlternaBomba();
    else if(c=='r'){ if(!emailRec[0]) strlcpy(emailRec,"teste@teste.com",sizeof(emailRec)); recIniciar(); Serial.printf("[PIN] recuperacao iniciada, codigo %s\n",recCodigo); }
    else if(c=='s'){ if(appState!=S_SCREENSAVER) enterScreensaver(); }
    else if(c=='x'){ lastActivityMs=millis(); if(appState==S_SCREENSAVER) exitScreensaver(); }
  }
}

// ═══════════════════════════════════════════════════════════════
// AUTOMACAO (Fase 52) -- regras por rele usando os sensores (SHT31) e o relogio.
// Uma regra por canal (8): modo + parametros. Modos:
//   0 Manual (sem automacao)        1 Aquecer (termostato: liga abaixo do alvo)   2 Resfriar (liga acima do alvo)
//   3 Umidificar (liga abaixo)      4 Desumidificar/ventilar (liga acima)         5 Periodico (liga N s a cada M min)
//   6 Horario (liga entre hh:mm e hh:mm)      + "desliga sozinho apos N min" em qualquer modo (0 = nunca).
// Alvo = "do tipo do ciclo" (temp ideal / umidade ideal, com umidade de ECLOSAO nos 3 ultimos dias) ou valor personalizado.
// Seguranca (sempre que a automacao esta ativa): sensor sem leitura ha >2 min desliga saidas de termostato/umidostato;
// temperatura >= maxima+0,5C desliga o aquecedor; um toque manual/PWA num rele pausa a automacao dele por 5 min.
// O CH7 (Cooler do sistema) continua no controle automatico proprio (temperatura do chip) -- nao entra aqui.
// Configuracao: /auto.cfg (LittleFS) e campo cmd.auto do doc (string compacta, editada no PWA ou aqui).
// ═══════════════════════════════════════════════════════════════
#define AUTO_N 8
enum { AM_OFF, AM_AQUECER, AM_RESFRIAR, AM_UMIDIFICAR, AM_DESUMIDIFICAR, AM_PERIODICO, AM_HORARIO, AM_VIRAR, AM_CIRCULAR, AM_N };
static const char* AUTO_MODO_NOME[AM_N]={"Manual","Aquecer","Resfriar","Umidificar","Desumidif.","Periodico","Horario","Viragem","Circular"};
struct RegraAuto{ uint8_t modo, alvoCustom; int16_t alvo10, hist10; uint16_t onSeg, cadaMin, hIni, hFim, autoOffMin; };
RegraAuto regras[AUTO_N];
bool canalTermostato(int ch){ return ch>=0 && ch<AUTO_N && (regras[ch].modo==AM_AQUECER||regras[ch].modo==AM_UMIDIFICAR); }
uint16_t autoRetomaMin=10;   // Fase 111 -- com um ciclo ativo/pausado, a chave da automacao volta sozinha apos N min desligada (0 = nunca)
bool fansCh8PorZero=false;   // Fase 111 -- CH8 foi desligado porque os 3 fans estao em 0 %
bool autoAtivo=false, autoSoCiclo=true;
unsigned long lastLocalAutoMs=0;
// ═══════════════════════════════════════════════════════════════
// Fase 78 -- HARDWARE: o que o usuario realmente instalou (caixa, aquecedor, rele, umidificador, fans). O controle PI usa
// esses dados para escolher o periodo de chaveamento, os ganhos e o duty inicial (antes: chute pelo volume). Sincroniza com
// o PWA dentro da propria string cmd.auto (token "H,..." logo apos as 8 regras) -- sem campo novo no Firestore.
// ═══════════════════════════════════════════════════════════════
struct HwCfg{ uint16_t volL, aqW, umMlH, bombaMlS10; uint8_t iso, aqTipo, aqQtd, rele, umTipo, umQtd, fanQtd, bomba, bombaPrimeS10; };   // bombaPrimeS10 = tempo (decimos de s) que a bomba leva ate a agua SAIR da mangueira vazia (Fase 88; sem valvula a mangueira esvazia)
//   // bomba: 0 nao instalada · 1 bomba de agua (CH3) instalada -- EM DESENVOLVIMENTO (Fase 83) · bombaMlS10 = vazao MEDIDA da bomba em decimos de ml/s (0 = nao calibrada = bomba DESATIVADA, Fase 85)
static HwCfg hw={3,100,300,0,0,0,1,0,0,1,1,0,0};   // padrao = a caixa de teste: isopor ~3 L, 1 resistencia de 100 W, 1 fan
static bool hwSujo=false; static unsigned long hwUltMs=0;
static const char* HW_ISO_NOME[5]={"Isopor","Isolada","Madeira","Plastico","Metal"};
static const float HW_ISO_K[5]={1.0f,0.5f,2.0f,3.0f,5.0f};          // perda de calor relativa ao isopor
static const char* HW_AQ_NOME[4]={"Resist.","Lampada","PTC","Manta"};
static const float HW_AQ_LAG[4]={1.0f,0.8f,1.3f,2.0f};              // atraso relativo ate o calor chegar ao sensor
static const char* HW_REL_NOME[2]={"Mecanic","SSR"};
static const char* HW_UM_NOME[3]={"Ultrass","Vapor","Evapor"};
static String hwSerializar(){
  char b[112]; snprintf(b,sizeof(b),"H,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",(unsigned)hw.volL,(unsigned)hw.iso,(unsigned)hw.aqTipo,(unsigned)hw.aqW,(unsigned)hw.aqQtd,
                       (unsigned)hw.rele,(unsigned)hw.umTipo,(unsigned)hw.umMlH,(unsigned)hw.umQtd,(unsigned)hw.fanQtd,(unsigned)hw.bomba,(unsigned)hw.bombaMlS10,(unsigned)hw.bombaPrimeS10);
  return String(b);
}
static void hwParse(const String& tk){
  long v[14]={0,3,0,0,100,1,0,0,300,1,1,0,0,0}; int k=0,p=0;
  while(k<14 && p<=(int)tk.length()){ int c=tk.indexOf(',',p); if(c<0) c=tk.length(); v[k++]=tk.substring(p,c).toInt(); p=c+1; }
  HwCfg n;
  n.volL=(uint16_t)(v[1]<1?1:v[1]>500?500:v[1]); n.iso=(uint8_t)(v[2]<0||v[2]>4?0:v[2]); n.aqTipo=(uint8_t)(v[3]<0||v[3]>3?0:v[3]);
  n.aqW=(uint16_t)(v[4]<5?5:v[4]>2000?2000:v[4]); n.aqQtd=(uint8_t)(v[5]<1?1:v[5]>8?8:v[5]); n.rele=(uint8_t)(v[6]?1:0);
  n.umTipo=(uint8_t)(v[7]<0||v[7]>2?0:v[7]); n.umMlH=(uint16_t)(v[8]<10?10:v[8]>3000?3000:v[8]); n.umQtd=(uint8_t)(v[9]<0?0:v[9]>4?4:v[9]); n.fanQtd=(uint8_t)(v[10]<0?0:v[10]>8?8:v[10]); n.bomba=(uint8_t)(v[11]?1:0); n.bombaMlS10=(uint16_t)(v[12]<0?0:v[12]>500?500:v[12]); n.bombaPrimeS10=(uint8_t)(v[13]<0?0:v[13]>100?100:v[13]);
  hw=n;
}
static float hwPtot(){ return (float)hw.aqW*hw.aqQtd; }
static float hwCap(){ return 3500.0f*powf(hw.volL/3.0f,0.8f); }                         // capacidade termica efetiva (J/K); 3500 = medido na caixa de teste de 3 L
static float hwRonPrior(){ return 0.5f*hwPtot()/hwCap(); }                              // subida liquida (C/s) com o aquecedor 100% ligado
static float hwUA(){ return 4.4f*powf(hw.volL/3.0f,0.667f)*HW_ISO_K[hw.iso]; }         // perda (W/C); 4,4 = caixa de teste (isopor com frestas)
static float hwIssPrior(float alvo){ float d=hwUA()*(alvo-25.0f)/hwPtot(); if(d<0.05f) d=0.05f; if(d>0.9f) d=0.9f; return d; }   // duty de regime estimado (ambiente 25 C)
static float hwKumPrior(){ float m=(float)hw.umMlH*hw.umQtd; if(m<=0) return 1.0f; float k=2.7f*(m/300.0f)*(3.0f/hw.volL); if(k<0.05f) k=0.05f; if(k>15.0f) k=15.0f; return k; }   // %UR por segundo ligado (2,7 = medido a 300 ml/h em 3 L)
#define AUTO_CFG_PATH "/auto.cfg"
// ── Regras avancadas (Fase 57): SE <condicao> ENTAO <acoes> SENAO <acoes> ──
// Qualquer sensor (temperatura, umidade, temperatura do chip) pode comandar QUALQUER combinacao de reles.
// Condicao de ligar: c1 [E|OU c2]. Desliga (histerese): "inverso" ou uma condicao propria (ex.: liga em T>=37.5 e so
// desliga quando T<=37.3). ENTAO = acoes quando ATIVA; SENAO = acoes quando INATIVA. Cada rele: 0 nada, 1 liga, 2 desliga
// (2 bits por rele: acaoA/acaoI, 16 bits). Janela horaria opcional (hIni==hFim = sempre).
#define ADV_MAX 8
struct RegraAv{
  uint8_t ativa, s1, op1, comb, s2, op2, temOff, sOff, opOff;   // s*: 0 temp, 1 umid, 2 temp do chip · op*: 0 >=, 1 <=, 2 >, 3 <
  int16_t v1, v2, vOff;                                         // valores em decimos (37.5C = 375, 55% = 550)
  uint16_t acaoA, acaoI, hIni, hFim;
};
RegraAv adv[ADV_MAX]; int advN=0;
// CO2 (MH-Z19B, ainda nao instalado): quando o sensor existir, basta atualizar co2Atual/lastCo2OkMs. Sem leitura valida a regra que usa CO2 fica inibida.
float co2Atual=NAN; unsigned long lastCo2OkMs=0;

// ═══════════════════════════════════════════════════════════════
// Fase 99 -- MH-Z19B (CO2) por UART1 (9600 8N1). Ligacao: sensor TX -> GPIO16 (RX do ESP) · sensor RX -> GPIO15 (TX do ESP) · Vin 5 V · GND.
// Sem sensor conectado nada acontece (so uma linha no Serial). Aquecimento de 3 min ignorado; ABC (auto-calibracao de 400 ppm) e DESLIGADO
// uma vez, porque numa caixa fechada com ovos o CO2 quase nunca volta a 400 ppm e o ABC "puxaria" a leitura para baixo.
// Desligar o driver (liberar GPIO15/16 para o tacometro dos fans): gravar "0" em /co2.cfg.
// ═══════════════════════════════════════════════════════════════
#define CO2_RX_PIN 16
#define CO2_TX_PIN 15
static bool co2Ativo=true, co2Aguardando=false, co2AbcDesligado=false, co2Visto=false; static unsigned long co2UltPedidoMs=0, co2BootMs=0; static uint8_t co2Falhas=0;
static void co2Init(){
  File f=LittleFS.open("/co2.cfg","r");
  if(f){ String l=f.readStringUntil('\n'); f.close(); l.trim(); if(l=="0") co2Ativo=false; }
  if(!co2Ativo){ Serial.println("[CO2] driver desativado (/co2.cfg = 0)"); return; }
  Serial1.begin(9600,SERIAL_8N1,CO2_RX_PIN,CO2_TX_PIN); co2BootMs=millis();
  Serial.printf("[CO2] UART1 pronta (RX=GPIO%d <- TX do sensor, TX=GPIO%d -> RX do sensor). Sem sensor conectado: sem leitura.\n",CO2_RX_PIN,CO2_TX_PIN);
}
static uint32_t co2BytesRx=0;   // Fase 114 -- bytes recebidos do sensor desde o boot (0 = o TX do sensor nao chega ao GPIO16)
static int co2Rd(){ co2BytesRx++; return Serial1.read(); }
// Fase 115 -- estatisticas do dia + amostra do grafico + alerta (CO2 alto por >= 10 min; normalizou por >= 5 min).
static bool co2Alertando=false;
static void co2AlertaTick(float v){
  static unsigned long altoDesde=0, okDesde=0, ultAviso=0;
  bool ctx=fbCicloAtivo && !fbCicloPreparo;   // sem ovos dentro nao ha alerta
  float okMax,alto; co2Limites(okMax,alto);
  if(tampaEst!=0){ altoDesde=0; return; }      // Fase 116: tampa aberta/recuperando: a leitura nao representa a caixa
  if(!ctx){ altoDesde=0; okDesde=0; co2Alertando=false; return; }
  unsigned long ag=millis();
  if(v>alto){
    okDesde=0; if(!altoDesde) altoDesde=ag;
    if(ag-altoDesde>=600000UL && (!co2Alertando || ag-ultAviso>=1800000UL)){
      co2Alertando=true; ultAviso=ag;
      char m[96]; snprintf(m,sizeof(m),"CO2 alto: %d ppm (limite %d). Ventile a caixa.",(int)v,(int)alto);
      logEvento("Alerta",m,true); pushEnfileirar(16,"ChocaIoT: CO2 alto",m);
    }
  }else if(v<=okMax){
    altoDesde=0;
    if(co2Alertando){
      if(!okDesde) okDesde=ag;
      else if(ag-okDesde>=300000UL){
        co2Alertando=false; okDesde=0;
        char m[72]; snprintf(m,sizeof(m),"CO2 normalizado: %d ppm.",(int)v);
        logEvento("Alerta",m,true); pushEnfileirar(16,"ChocaIoT: CO2 normalizado",m);
      }
    }else okDesde=0;
  }else{ altoDesde=0; okDesde=0; }   // zona de atencao: mantem o estado
}
static void co2Amostra(float v){
  resetarEstatisticasSensorSeVirouODia();
  if(isnan(co2Min)||v<co2Min) co2Min=v;
  if(isnan(co2Max)||v>co2Max) co2Max=v;
  co2Soma+=v; co2N++;
  static unsigned long ultMs=0; unsigned long iv=(co2HistCount<10)?60000UL:SENSOR_HIST_INTERVAL_MS;
  if(co2HistCount==0 || millis()-ultMs>iv){ ultMs=millis(); if(co2HistCount<SENSOR_HIST_MAX) co2Historico[co2HistCount++]=v; }
  co2AlertaTick(v);
}
static void co2Tick(){
  if(!co2Ativo) return;
  unsigned long agora=millis();
  if(!co2Aguardando){
    if(agora-co2UltPedidoMs<3000UL) return;   // Fase 114e: 5 s -> 3 s
    while(Serial1.available()) co2Rd();
    static const uint8_t CMD_LER[9]={0xFF,0x01,0x86,0,0,0,0,0,0x79};
    Serial1.write(CMD_LER,9); co2UltPedidoMs=agora; co2Aguardando=true; return;
  }
  if(Serial1.available()>=9){
    while(Serial1.available() && Serial1.peek()!=0xFF) co2Rd();
    if(Serial1.available()<9) return;   // espera completar o quadro
    uint8_t b[9]; for(int i=0;i<9;i++) b[i]=co2Rd();
    co2Aguardando=false;
    uint8_t soma=0; for(int i=1;i<8;i++) soma+=b[i]; soma=(uint8_t)(0xFF-soma+1);
    if(b[1]==0x86 && soma==b[8]){
      int ppm=b[2]*256+b[3];
      co2Falhas=0;
      if(!co2Visto){
        co2Visto=true; Serial.printf("[CO2] MH-Z19B respondeu: %d ppm (aquecendo: leituras so valem apos 3 min)\n",ppm);
        char m[80]; snprintf(m,sizeof(m),"Sensor respondeu: %d ppm (valores valem apos 1 min ligado)",ppm); logEvento("CO2",m,true);   // Fase 114
      }
      if(agora-co2BootMs>=60000UL){   // Fase 114e: 3 min -> 1 min (pre-aquecimento do manual: 1 min)
        if(ppm>=300 && ppm<=10000){ co2Atual=(float)ppm; lastCo2OkMs=agora; co2Amostra((float)ppm); }
        if(!co2AbcDesligado){
          static const uint8_t CMD_ABC_OFF[9]={0xFF,0x01,0x79,0,0,0,0,0,0x86};
          Serial1.write(CMD_ABC_OFF,9); co2AbcDesligado=true; Serial.println("[CO2] ABC (auto-calibracao 400 ppm) desligado.");
        }
      }
    }else{
      if(co2Falhas<250) co2Falhas++;
      if(co2Falhas==3 && !co2Visto){   // Fase 114: chegou algo, mas quadro invalido (ruido / velocidade / pino trocado)
        char m[96]; snprintf(m,sizeof(m),"Quadro invalido do sensor (%u bytes recebidos): %02X %02X %02X %02X",(unsigned)co2BytesRx,b[0],b[1],b[2],b[3]); logEvento("CO2",m,true);
      }
    }
  }else if(agora-co2UltPedidoMs>1000UL){   // sem resposta
    while(Serial1.available()) co2Rd();    // descarta (e conta) o que chegou incompleto
    co2Aguardando=false; if(co2Falhas<250) co2Falhas++;
    if(!co2Visto && (co2Falhas==12 || co2Falhas==60)){   // ~1 min e ~6 min sem resposta
      Serial.println("[CO2] sem resposta do MH-Z19B (confira 5 V, GND e TX/RX cruzados em GPIO15/16).");
      char m[112]; snprintf(m,sizeof(m),"Sem resposta (%u bytes recebidos no GPIO16): confira 5 V no sensor, GND e fios nos pinos 15/16",(unsigned)co2BytesRx);
      logEvento("CO2",m,true);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// Fase 99 -- PWM PREPARADO para os modulos IRF520 (ainda nao instalados): fans em GPIO4/5/6 e backlight em GPIO47. DESLIGADO por padrao
// (/pwm.cfg ausente ou "0"): enquanto desligado NENHUM pino e tocado. Liga com a tecla serial 'P' (grava "1|100|100|100|100" = ativo|fan1|fan2|fan3|backlight %).
// Atencao: o IRF520 NAO e logic-level; em 3,3 V muitos modulos so conduzem bem com ~5 V no gate: teste a tensao na carga a 100 %.
// ═══════════════════════════════════════════════════════════════
static const uint8_t PWM_PINS[4]={4,5,6,47};   // fan1, fan2, fan3, backlight
static bool pwmAtivo=false; static uint8_t pwmDuty[4]={100,100,100,100};
static uint8_t pwmDutyEf[3]={100,100,100};   // Fase 112: duty EFETIVO aplicado (o manual, ou o calculado pela funcao do fan)
static uint8_t fanModo[3]={0,0,0};            // Fase 112: 0 Manual · 1 Circulacao · 2 Resfriamento · 3 Renovacao
#define PWM_FREQ_HZ 25000   // Fase 104 -- 25 kHz: padrao Intel dos fans de 4 fios (PWM no fio 4; 12 V fixo)
static void pwmAplica(){ if(!pwmAtivo) return; for(int i=0;i<3;i++) ledcWrite(i,(uint32_t)pwmDutyEf[i]*255U/100U); }   // Fase 101/112: so os 3 fans, no duty efetivo
static void pwmSalvar(){ File f=LittleFS.open("/pwm.cfg","w"); if(!f) return; f.printf("%d|%u|%u|%u|%u\n",pwmAtivo?1:0,pwmDuty[0],pwmDuty[1],pwmDuty[2],pwmDuty[3]); f.close(); }
static void pwmInit(){
  File f=LittleFS.open("/pwm.cfg","r");
  if(f){ String l=f.readStringUntil('\n'); f.close(); int a=0,d0=100,d1=100,d2=100,d3=100; if(sscanf(l.c_str(),"%d|%d|%d|%d|%d",&a,&d0,&d1,&d2,&d3)>=1){
      pwmAtivo=(a==1); int* v[4]={&d0,&d1,&d2,&d3}; for(int i=0;i<4;i++){ int x=*v[i]; if(x<0) x=0; if(x>100) x=100; pwmDuty[i]=(uint8_t)x; } } }
  for(int i=0;i<3;i++) pwmDutyEf[i]=pwmDuty[i];   // Fase 112
  if(!pwmAtivo){ Serial.println("[PWM] desligado (modulos IRF520 nao instalados): GPIO4/5/6/47 intocados. Tecla P liga."); return; }
  for(int i=0;i<3;i++){ ledcSetup(i,PWM_FREQ_HZ,8); ledcAttachPin(PWM_PINS[i],i); }
  pwmAplica();
  Serial.printf("[PWM] ligado: fans %u/%u/%u %%, backlight %u %%\n",pwmDuty[0],pwmDuty[1],pwmDuty[2],pwmDuty[3]);
}
// API para a futura tela/automacao: i = 0..2 fans, 3 = backlight; pct 0..100
void pwmSet(int i,int pct){ if(i<0||i>3) return; if(pct<0) pct=0; if(pct>100) pct=100;
  if(i==3){ brilhoPct=(uint8_t)(pct<10?10:pct); saveDisplayCfg(); blAplica(); return; }   // Fase 101: backlight (minimo 10 %: nunca apagar a tela por engano)
  pwmDuty[i]=(uint8_t)pct; if(fanModo[i]==0) pwmDutyEf[i]=(uint8_t)pct; pwmSalvar(); pwmAplica(); }
void pwmAlternar(){
  pwmAtivo=!pwmAtivo; pwmSalvar();
  if(pwmAtivo){ for(int i=0;i<3;i++){ ledcSetup(i,PWM_FREQ_HZ,8); ledcAttachPin(PWM_PINS[i],i); } pwmAplica(); Serial.println("[PWM] LIGADO (100 %). Reinicie para o estado ficar so em /pwm.cfg."); }
  else { for(int i=0;i<3;i++){ ledcDetachPin(PWM_PINS[i]); pinMode(PWM_PINS[i],INPUT); } Serial.println("[PWM] DESLIGADO: pinos liberados."); }
}

// ═══════════════════════════════════════════════════════════════
// Fase 102 -- tela Configuracoes > Fans: liga/desliga o PWM e ajusta cada fan (0..100 %, passo 10). Antes so existia a tecla serial 'P'.
// Fan 1/2/3 = GPIO4/5/6 -> modulos IRF520 (o backlight tem controle proprio em Tela).
// ═══════════════════════════════════════════════════════════════
// ── Fase 105 -- tacometro do fan 1 (fio 3 -> GPIO17, coletor aberto, 2 pulsos por volta). Contagem por interrupcao, janela de 2 s (1 pulso = 15 rpm).
#define TACH_PIN 17
#define TACH_PPR 2            // pulsos por volta (padrao Intel dos fans de 4 fios)
#define TACH_LOW_MIN_US 800U  // Fase 109: so conta se o nivel BAIXO durar >= 0,8 ms (pico estreito = ruido de comutacao do motor/PWM)
#define TACH_DEAD_US 4000U    // intervalo minimo entre pulsos aceitos (ate 7500 rpm a 2 ppr)
static volatile uint32_t tachPulsos=0, tachRej=0, tachUltUs=0, tachFallUs=0, tachMinUs=0xFFFFFFFFUL, tachMaxUs=0, tachWsum=0;
static void IRAM_ATTR tachIsr(){   // interrupcao nas DUAS bordas: mede a largura do nivel baixo (fan real: ms; ruido: us)
  uint32_t t=micros();
  if(gpio_get_level((gpio_num_t)TACH_PIN)==0){ tachFallUs=t; return; }
  uint32_t w=t-tachFallUs;
  if(w<TACH_LOW_MIN_US || w>200000U){ tachRej++; return; }
  uint32_t d=tachFallUs-tachUltUs;
  if(d<TACH_DEAD_US){ tachRej++; return; }
  tachUltUs=tachFallUs; tachPulsos++; tachWsum+=w;
  if(d<1000000U){ if(d<tachMinUs) tachMinUs=d; if(d>tachMaxUs) tachMaxUs=d; }
}
static int fanRpm=-1; static bool tachVisto=false; static unsigned long tachAmostraMs=0; static uint8_t tachRuim=0;   // -1 = sem sinal
int fanRpmAtual(){ return fanRpm; }
static uint32_t tachDiagN=0, tachDiagRej=0, tachDiagW=0, tachDiagMin=0, tachDiagMax=0; static int tachDiagNivel=-1;   // ultima janela (tela Fans mostra quando sem sinal)
void tachInit(){ pinMode(TACH_PIN,INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(TACH_PIN),tachIsr,CHANGE); tachAmostraMs=millis(); }
void tachTick(){
  unsigned long agora=millis(), dt=agora-tachAmostraMs; if(dt<2000UL) return;
  noInterrupts(); uint32_t n=tachPulsos, rj=tachRej, ws=tachWsum, mn=tachMinUs, mx=tachMaxUs; tachPulsos=0; tachRej=0; tachWsum=0; tachMinUs=0xFFFFFFFFUL; tachMaxUs=0; interrupts(); tachAmostraMs=agora;
  int rpm=(int)((uint64_t)n*60000ULL/((uint64_t)TACH_PPR*dt));
  tachDiagN=n; tachDiagRej=rj; tachDiagW=n?ws/n:0; tachDiagMin=(mn==0xFFFFFFFFUL)?0:mn; tachDiagMax=mx; tachDiagNivel=digitalRead(TACH_PIN);
  // So aceita sinal REGULAR e plausivel: >= 4 pulsos, intervalos dentro de 4x, 60..12000 rpm.
  // Fase 110: a contagem por janela bate com o rpm fisico (fotos: 90 pulsos/2 s a 50 % = ~1350 rpm vs 1300 esperado); a checagem de regularidade
  // reprovava janelas boas porque um ou outro espinho largo cai no meio. Agora: >= 4 pulsos, 60..12000 rpm e MEDIANA das ultimas 3 janelas.
  bool valido=(n>=4 && rpm>=60 && rpm<=12000);
  { static unsigned long ultDiag=0; if(!valido && agora-ultDiag>=10000UL){ ultDiag=agora; Serial.printf("[TACH] GPIO%d aceitos=%u rejeitados=%u largura=%lu us intervalo=%lu..%lu us nivel=%d rpm=%d\n",TACH_PIN,(unsigned)n,(unsigned)rj,(unsigned long)tachDiagW,(unsigned long)tachDiagMin,(unsigned long)mx,tachDiagNivel,rpm); } }
  if(valido){ tachVisto=true; tachRuim=0;
    static int h[3]={0,0,0}; static uint8_t cnt=0, pos=0; h[pos]=rpm; pos=(uint8_t)((pos+1)%3); if(cnt<3) cnt++;
    if(cnt==1) fanRpm=rpm; else if(cnt==2) fanRpm=(h[0]+h[1])/2;
    else { int a=h[0],b=h[1],c=h[2]; int mn2=a<b?a:b, mx2=a>b?a:b; fanRpm=mx2<c?mx2:(mn2>c?mn2:c); }   // mediana de 3
  }
  else if(n==0){ if(tachVisto) fanRpm=0; tachRuim=0; }   // sem pulsos validos: parado (se ja houve sinal) ou sem fio
  else if(++tachRuim>=3){ fanRpm=-1; tachVisto=false; }   // so declara "sem sinal" apos 3 janelas ruins seguidas (6 s)
}
// ── Fase 112 -- FUNCAO de cada fan e regras das funcoes ───────────────────────────────────────────────────────────────
// Cada fan e Manual (usa o % da tela Fans) ou recebe uma funcao; as regras (min/max/limiares) sao compartilhadas por funcao e editadas na tela
// "Regras" (aparelho) / card "Regras dos fans" (PWA). Funcoes so valem com a chave da automacao ligada e ciclo ativo (ou manter ambiente);
// fora disso o fan volta ao % manual. Tampa aberta/recuperando ou sensor sem leitura = minimo da funcao.
//   Circulacao   : min..max conforme o tempo que o aquecedor fica ligado (media movel ~50 s)
//   Resfriamento : min..max conforme a temperatura acima do ponto medio da faixa do tipo (comeca em +X C, maximo em +Y C)
//   Renovacao    : min..max conforme a umidade acima do maximo da faixa (+X pts .. +Y pts) e, quando ativo, o CO2 (INATIVO ate haver sensor)
struct FanRegras { uint8_t cMin,cMax, rMin,rMax, rIni10,rFull10, vMin,vMax, vIni,vFull, co2On; uint16_t co2Ini,co2Full; };
static FanRegras fr={40,100, 0,100,0,10, 20,100,0,5, 1,1500,3000};   // Fase 116: CO2 ATIVO (comeca em 1500 ppm, maximo em 3000 ppm)
static const char* FAN_FN_NOME[4]={"Manual","Circulacao","Resfriamento","Renovacao"};
static float heaterEma=0;
static String fanRegrasSerialize(){
  char b[140];
  snprintf(b,sizeof(b),"F1|%u,%u,%u|%u,%u|%u,%u,%u,%u|%u,%u,%u,%u|%u,%u,%u",(unsigned)fanModo[0],(unsigned)fanModo[1],(unsigned)fanModo[2],
           (unsigned)fr.cMin,(unsigned)fr.cMax,(unsigned)fr.rMin,(unsigned)fr.rMax,(unsigned)fr.rIni10,(unsigned)fr.rFull10,
           (unsigned)fr.vMin,(unsigned)fr.vMax,(unsigned)fr.vIni,(unsigned)fr.vFull,(unsigned)fr.co2On,(unsigned)fr.co2Ini,(unsigned)fr.co2Full);
  return String(b);
}
static int fcl(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static bool fanRegrasParse(const String& str){
  int m0,m1,m2,cMin,cMax,rMin,rMax,rIni,rFull,vMin,vMax,vIni,vFull,co2On,co2Ini,co2Full;
  if(sscanf(str.c_str(),"F1|%d,%d,%d|%d,%d|%d,%d,%d,%d|%d,%d,%d,%d|%d,%d,%d",&m0,&m1,&m2,&cMin,&cMax,&rMin,&rMax,&rIni,&rFull,&vMin,&vMax,&vIni,&vFull,&co2On,&co2Ini,&co2Full)!=16) return false;
  fanModo[0]=(uint8_t)fcl(m0,0,3); fanModo[1]=(uint8_t)fcl(m1,0,3); fanModo[2]=(uint8_t)fcl(m2,0,3);
  fr.cMin=(uint8_t)fcl(cMin,0,100); fr.cMax=(uint8_t)fcl(cMax,fr.cMin,100);
  fr.rMin=(uint8_t)fcl(rMin,0,100); fr.rMax=(uint8_t)fcl(rMax,fr.rMin,100);
  fr.rIni10=(uint8_t)fcl(rIni,0,99); fr.rFull10=(uint8_t)fcl(rFull,fr.rIni10+1,100);
  fr.vMin=(uint8_t)fcl(vMin,0,100); fr.vMax=(uint8_t)fcl(vMax,fr.vMin,100);
  fr.vIni=(uint8_t)fcl(vIni,0,29); fr.vFull=(uint8_t)fcl(vFull,fr.vIni+1,30);
  fr.co2On=(uint8_t)(co2On?1:0);   // Fase 116: CO2 liga/desliga pela tela Regras ou pelo PWA
  fr.co2Ini=(uint16_t)fcl(co2Ini,500,4900); fr.co2Full=(uint16_t)fcl(co2Full,fr.co2Ini+100,5000);
  return true;
}
static void fanRegrasSalvar(){ File f=LittleFS.open("/fanrules.cfg","w"); if(!f) return; f.println(fanRegrasSerialize()); f.close(); }
void fanRegrasCarregar(){
  File f=LittleFS.open("/fanrules.cfg","r");
  if(f){ String l=f.readStringUntil('\n'); f.close(); l.trim(); if(l.length()) fanRegrasParse(l); }
  if(!LittleFS.exists("/mig116.flag")){   // Fase 116 -- migracao UNICA: a Renovacao passa a usar o CO2 (1500 -> 3000 ppm)
    fr.co2On=1; fr.co2Ini=1500; fr.co2Full=3000; fanRegrasSalvar();
    File g=LittleFS.open("/mig116.flag","w"); if(g){ g.print("1"); g.close(); }
  }
}
static void fansEfManual(){ for(int i=0;i<3;i++) if(fanModo[i]==0) pwmDutyEf[i]=pwmDuty[i]; pwmAplica(); }
static int fanAlvoFuncao(int m){
  unsigned long agora=millis();
  bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (agora-lastSht31OkMs)<120000UL;
  int minV,maxV;
  if(m==1){ minV=fr.cMin; maxV=fr.cMax; } else if(m==2){ minV=fr.rMin; maxV=fr.rMax; } else { minV=fr.vMin; maxV=fr.vMax; }
  if(tampaEst!=0 || !tOk) return minV;   // tampa aberta/recuperando ou sensor sem leitura: minimo da funcao
  float f=0;
  if(m==1) f=heaterEma;
  else if(m==2){ float alvoT=(TEMP_IDEAL_MIN+TEMP_IDEAL_MAX)/2.0f, ex=temperaturaAtual-alvoT, ini=fr.rIni10/10.0f, full=fr.rFull10/10.0f; if(full<=ini) full=ini+0.1f; f=(ex-ini)/(full-ini); }
  else{
    float ex=umidadeAtual-UMID_IDEAL_MAX, ini=fr.vIni, full=fr.vFull; if(full<=ini) full=ini+1; f=(ex-ini)/(full-ini);
    if(fr.co2On && !isnan(co2Atual) && lastCo2OkMs && agora-lastCo2OkMs<180000UL){
      float i2=fr.co2Ini, f2=fr.co2Full;
      { float ok,al; co2Limites(ok,al); i2+=ok-2000.0f; f2+=al-3000.0f; }   // Fase 116: na pre-eclosao/eclosao os limites sobem junto com as faixas de alerta (+1000 / +1500 ppm)
      if(f2<=i2) f2=i2+100; float g=(co2Atual-i2)/(f2-i2); if(g>f) f=g;
    }
  }
  if(f<0) f=0; if(f>1) f=1;
  return (int)(minV+(maxV-minV)*f+0.5f);
}
void fansRegrasTick(){
  unsigned long agora=millis();
  static unsigned long uS=0, uT=0;
  if(agora-uS>=1000UL){ uS=agora; heaterEma+=((relayState[0]?1.0f:0.0f)-heaterEma)*0.02f; }
  if(agora-uT<5000UL) return; uT=agora;
  bool funcoes=autoAtivo && (fbCicloAtivo || ambManterOn());
  bool mudou=false;
  for(int i=0;i<3;i++){
    int ef=pwmDutyEf[i], alvo=(fanModo[i]==0 || !funcoes)?pwmDuty[i]:fanAlvoFuncao(fanModo[i]);
    if(fanModo[i]!=0 && funcoes){ if(alvo>ef+10) alvo=ef+10; else if(alvo<ef-10) alvo=ef-10; }   // rampa: no maximo 10 pontos por 5 s
    if(alvo!=ef){ pwmDutyEf[i]=(uint8_t)alvo; mudou=true; }
  }
  if(mudou){ pwmAplica(); if(appState==S_FANS) fansDraw(); }
}
bool fansAtivo(){ return pwmAtivo; }
bool fansTodosZero(){ return pwmAtivo && pwmDutyEf[0]==0 && pwmDutyEf[1]==0 && pwmDutyEf[2]==0; }   // Fase 111/112 (duty efetivo)
#define FANS_ROW0 60
#define FANS_ROWH 52
static void fansRow(int i){
  uint16_t cb=tft.color565(16,16,46); int y=FANS_ROW0+i*FANS_ROWH; bool man=(fanModo[i]==0);
  tft.fillRect(0,y-2,320,FANS_ROWH-2,cBg);
  uiCard(6,y,308,FANS_ROWH-6);
  char t[36]; snprintf(t,sizeof(t),"Fan %d  %s",i+1,FAN_FN_NOME[fanModo[i]]);   // Fase 112: toque no nome muda a funcao
  tft.setTextSize(1);tft.setTextColor(man?TFT_WHITE:cLogo,cb);tft.setCursor(14,y+7);tft.print(t);
  if(i==0){ char r[16]; if(fanRpm<0) snprintf(r,16,"sem sinal"); else snprintf(r,16,"%d rpm",fanRpm);   // Fase 105
    tft.setTextColor(fanRpm>0?cGreen:cSub,cb); tft.setCursor(140,y+7); tft.print(r); }
  uint8_t d=pwmDutyEf[i];
  char v[10]; snprintf(v,10,"%u%%",(unsigned)d);
  uint16_t cv=pwmAtivo?cClk:tft.color565(90,90,110);
  tft.setTextColor(cv,cb);tft.setCursor(206-tft.textWidth(v),y+7);tft.print(v);
  int bx=14,bw=186,by=y+22,bh=14;
  tft.fillRoundRect(bx,by,bw,bh,4,tft.color565(20,20,44));
  int fw=(int)d*bw/100; if(fw>0) tft.fillRoundRect(bx,by,fw<8?8:fw,bh,4,pwmAtivo?(man?tft.color565(60,170,110):tft.color565(70,140,230)):tft.color565(70,70,90));
  tft.drawRoundRect(bx,by,bw,bh,4,tft.color565(70,70,140));
  uint16_t kb=man?cKey:tft.color565(24,24,56);   // -/+ so no modo Manual
  tft.fillRoundRect(216,y+14,40,24,5,kb);tft.setTextColor(man?TFT_WHITE:tft.color565(80,80,110),kb);tft.setTextSize(2);tft.setCursor(230,y+19);tft.print("-");
  tft.fillRoundRect(264,y+14,40,24,5,kb);tft.setCursor(278,y+19);tft.print("+");
  tft.setTextSize(1);
}
void fansDraw(){
  tft.fillScreen(cBg); uiTopo("FANS","Regras >");
  uint16_t cb=tft.color565(16,16,46);
  uiCard(6,22,308,32);
  tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cb);tft.setCursor(14,34);tft.print(pwmAtivo?"Fans por PWM: LIGADO":"Fans por PWM: DESLIGADO");
  int x0=262,y0=30; bool on=pwmAtivo;
  tft.fillRoundRect(x0,y0,44,16,8,on?tft.color565(10,90,40):tft.color565(50,50,70));
  tft.fillCircle(on?x0+36:x0+8,y0+8,6,on?cGreen:tft.color565(150,150,170));
  for(int i=0;i<3;i++) fansRow(i);
  tft.setTextColor(cSub,cBg);tft.setCursor(8,FANS_ROW0+3*FANS_ROWH+2);
  tft.print("Toque no nome do fan: muda a funcao.");
  tft.setCursor(8,FANS_ROW0+3*FANS_ROWH+14);
  if(fanRpm>0) tft.print("Os 3 em 0%: o CH8 desliga.");
  else { char d[64]; snprintf(d,sizeof(d),"Tach: %up %ur w%lu i%lu-%lu n%d",(unsigned)tachDiagN,(unsigned)tachDiagRej,(unsigned long)tachDiagW,(unsigned long)tachDiagMin,(unsigned long)tachDiagMax,tachDiagNivel); tft.print(d); }
}
extern int fanRegPagVar;
void fansTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_SETTINGS; drawSettingsPanel(); return; }
  if(ty<20 && tx>=230){ appState=S_FANREGRAS; fanRegPagVar=0; fanRegrasDraw(); return; }   // Fase 112: "Regras >"
  if(ty>=22&&ty<56){ pwmAlternar(); fansDraw(); return; }
  for(int i=0;i<3;i++){
    int y=FANS_ROW0+i*FANS_ROWH;
    if(ty>=y && ty<y+22 && tx<200){   // Fase 112: ciclar a funcao do fan (Manual > Circulacao > Resfriamento > Renovacao)
      fanModo[i]=(uint8_t)((fanModo[i]+1)%4); fanRegrasSalvar(); fansEfManual(); fansRow(i); return;
    }
    if(ty>=y+10&&ty<=y+42){
      if(fanModo[i]!=0) return;   // fan com funcao: o % vem das regras
      int d=(tx>=210&&tx<260)?-10:(tx>=260&&tx<312)?+10:0; if(!d) return;
      int n=(int)pwmDuty[i]+d; if(n<0) n=0; if(n>100) n=100;
      pwmSet(i,n); fansRow(i); return;
    }
  }
}

// ── Fase 112 -- tela "Regras" das funcoes (3 paginas: Circulacao · Resfriamento · Renovacao), reaproveita os cartoes -/+ da tela Tela ──
int fanRegPagVar=0;
static void fanRegLinha(int k,const char* rot,const char* val,bool on=true){ telaStep(22+k*34,rot,val,on); }
void fanRegrasDraw(){
  tft.fillScreen(cBg);
  static const char* T[4]={"REGRAS: CIRCULACAO","REGRAS: RESFRIAMENTO","REGRAS: RENOVACAO","REGRAS: CO2"};
  char pg[8]; snprintf(pg,sizeof(pg),"%d/4 >",fanRegPagVar+1); uiTopo(T[fanRegPagVar],pg);
  char v[20]; tft.setTextSize(1); tft.setTextColor(cSub,cBg);
  if(fanRegPagVar==0){
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.cMin); fanRegLinha(0,"Minimo",v);
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.cMax); fanRegLinha(1,"Maximo (aquecedor sempre ligado)",v);
    tft.setCursor(8,100); tft.print("Misturar o ar: sobe com o tempo em que o aquecedor");
    tft.setCursor(8,112); tft.print("fica ligado (mais calor = mais circulacao).");
    tft.setCursor(8,128); tft.print("Tampa aberta ou sensor sem leitura: minimo.");
  }else if(fanRegPagVar==1){
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.rMin); fanRegLinha(0,"Minimo",v);
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.rMax); fanRegLinha(1,"Maximo",v);
    snprintf(v,sizeof(v),"+%.1fC",fr.rIni10/10.0f); fanRegLinha(2,"Comeca acima do ideal",v);
    snprintf(v,sizeof(v),"+%.1fC",fr.rFull10/10.0f); fanRegLinha(3,"Maximo acima do ideal",v);
    tft.setCursor(8,164); tft.print("Ideal = ponto medio da faixa do tipo de ovo.");
    tft.setCursor(8,176); tft.print("Esfria quando a temperatura passa do ideal.");
  }else if(fanRegPagVar==3){   // Fase 116: renovacao pelo CO2
    fanRegLinha(0,"Usar o CO2 na renovacao",fr.co2On?"ligado":"desligado");
    snprintf(v,sizeof(v),"%uppm",(unsigned)fr.co2Ini); fanRegLinha(1,"Comeca a renovar em",v,fr.co2On);
    snprintf(v,sizeof(v),"%uppm",(unsigned)fr.co2Full); fanRegLinha(2,"Renovacao maxima em",v,fr.co2On);
    float ok,al; co2Limites(ok,al);
    tft.setCursor(8,128); tft.printf("Hoje a faixa sobe +%.0f / +%.0f ppm (fase do ciclo).",ok-2000.0f,al-3000.0f);
    tft.setCursor(8,144); tft.print("Vale para o fan com a funcao Renovacao. Sem leitura");
    tft.setCursor(8,156); tft.print("do sensor ou com a tampa aberta: so a umidade.");
    if(!isnan(co2Atual)){ tft.setCursor(8,176); tft.printf("CO2 agora: %.0f ppm",co2Atual); }
  }else{
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.vMin); fanRegLinha(0,"Minimo (renovacao basica)",v);
    snprintf(v,sizeof(v),"%u%%",(unsigned)fr.vMax); fanRegLinha(1,"Maximo",v);
    snprintf(v,sizeof(v),"+%upt",(unsigned)fr.vIni); fanRegLinha(2,"Umidade: comeca acima do max.",v);
    snprintf(v,sizeof(v),"+%upt",(unsigned)fr.vFull); fanRegLinha(3,"Umidade: maximo acima do max.",v);
    tft.setCursor(8,164); tft.print("Renova o ar: sobe com a umidade acima do maximo.");
    tft.setCursor(8,176); tft.print("O CO2 tem a propria pagina (4/4).");
  }
}
void fanRegrasTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ appState=S_FANS; fansDraw(); return; }
  if(ty<20 && tx>=230){ fanRegPagVar=(fanRegPagVar+1)%4; fanRegrasDraw(); return; }
  int k=(ty-22)/34; int dir=(tx>=180&&tx<228)?-1:(tx>=266&&tx<314)?+1:0;
  if(ty<22 || k<0 || k>4 || !dir) return;
  auto u8=[&](uint8_t& x,int step,int lo,int hi){ x=(uint8_t)fcl((int)x+dir*step,lo,hi); };
  if(fanRegPagVar==0){
    if(k==0) u8(fr.cMin,5,0,fr.cMax); else if(k==1) u8(fr.cMax,5,fr.cMin,100); else return;
  }else if(fanRegPagVar==1){
    if(k==0) u8(fr.rMin,5,0,fr.rMax); else if(k==1) u8(fr.rMax,5,fr.rMin,100);
    else if(k==2) u8(fr.rIni10,5,0,fr.rFull10-1); else if(k==3) u8(fr.rFull10,5,fr.rIni10+1,100); else return;
  }else if(fanRegPagVar==3){   // Fase 116: CO2
    if(k==0){ fr.co2On=fr.co2On?0:1; }
    else if(k==1){ int v=(int)fr.co2Ini+dir*100; fr.co2Ini=(uint16_t)fcl(v,500,(int)fr.co2Full-100); }
    else if(k==2){ int v=(int)fr.co2Full+dir*100; fr.co2Full=(uint16_t)fcl(v,(int)fr.co2Ini+100,5000); }
    else return;
  }else{
    if(k==0) u8(fr.vMin,5,0,fr.vMax); else if(k==1) u8(fr.vMax,5,fr.vMin,100);
    else if(k==2) u8(fr.vIni,1,0,fr.vFull-1); else if(k==3) u8(fr.vFull,1,fr.vIni+1,30); else return;
  }
  fanRegrasSalvar(); fanRegrasDraw();
}

// Fase 105 -- publicacao ao vivo do painel (rpm) so enquanto o cartao Fans do PWA esta visivel (cmd.fansVivo renovado a cada ~40 s): 1 escrita a cada 5 s
static bool fansPubOcupado=false; static unsigned long fansPubMs=0;
static void fbFansCallback(AsyncResult &r){ fansPubOcupado=false; if(r.isError()) Serial.printf("[FANS] publicacao ao vivo ERRO: %s\n",r.error().message().c_str()); }
void fansPublicaTick(){
  unsigned long agora=millis();
  if(!fansVivoMs || agora-fansVivoMs>90000UL) return;
  if(!fbStarted || !fbApp.ready()) return;
  if(fansPubOcupado && agora-fansPubMs<8000UL) return;
  if(agora-fansPubMs<5000UL) return;
  fansPubMs=agora; fansPubOcupado=true;
  String pn=painelCfgStr();
  Document<Values::Value> doc("painel", Values::Value(Values::StringValue(pn)));
  DocumentMask dm("painel");
  PatchDocumentOptions po(dm, DocumentMask(), Precondition());
  fbDocs.patch(fbClient, Firestore::Parent(FIREBASE_PROJECT_ID), FIRESTORE_DOCUMENT_PATH, po, doc, fbFansCallback, "fbFansTask");
}
void fansTelaTick(){   // tela Fans do aparelho: atualiza so a linha do rpm quando o valor muda
  if(appState!=S_FANS) return;
  static int ult=-2; static unsigned long u=0;
  if(millis()-u<1000UL) return; u=millis();
  static uint32_t ultN=0xFFFFFFFFUL;
  if(fanRpm!=ult){ ult=fanRpm; fansRow(0); }
  if((tachDiagN+tachDiagRej)!=ultN && fanRpm<=0){   // diagnostico (pulsos brutos) so enquanto nao ha rpm valido: redesenha so a linha do rodape
    ultN=tachDiagN+tachDiagRej; int yy=FANS_ROW0+3*FANS_ROWH+12; tft.fillRect(0,yy,320,10,cBg); tft.setTextSize(1); tft.setTextColor(cSub,cBg); tft.setCursor(8,yy+2);
    char d[64]; snprintf(d,sizeof(d),"Tach: %up %ur w%lu i%lu-%lu n%d",(unsigned)tachDiagN,(unsigned)tachDiagRej,(unsigned long)tachDiagW,(unsigned long)tachDiagMin,(unsigned long)tachDiagMax,tachDiagNivel); tft.print(d);
  }
}

// ═══════════════════════════════════════════════════════════════
// Fase 104 -- ajustes de Tela e Fans vindos do PWA (cmd.telaSet / cmd.fansSet) e estado publicado no heartbeat (campo "painel")
// painel = "T|brilho|reduzirSeg|apagarSeg|manter|relogio#F|ativo|f1|f2|f3#R|rpmFan1" (Fase 105: #R)
// cmd.telaSet = "<ms>|brilho|reduzirSeg|apagarSeg|manter|relogio" · cmd.fansSet = "<ms>|ativo|f1|f2|f3"
// ═══════════════════════════════════════════════════════════════
String painelCfgStr(){
  char b[112];
  snprintf(b,sizeof(b),"T|%u|%u|%lu|%d|%d#F|%d|%u|%u|%u#R|%d",(unsigned)brilhoPct,(unsigned)dimSegundos,screenOffTimeoutSec,
           telaSempreLigada?1:0,relogioSempre?1:0,pwmAtivo?1:0,(unsigned)pwmDuty[0],(unsigned)pwmDuty[1],(unsigned)pwmDuty[2],fanRpm);   // Fase 105: #R|rpm do fan 1 (-1 = sem sinal)
  char g[130];   // Fase 112: #G|funcoes|duty efetivo|circulacao|resfriamento|renovacao|co2
  snprintf(g,sizeof(g),"#G|%u,%u,%u|%u,%u,%u|%u,%u|%u,%u,%u,%u|%u,%u,%u,%u|%u,%u,%u",(unsigned)fanModo[0],(unsigned)fanModo[1],(unsigned)fanModo[2],
           (unsigned)pwmDutyEf[0],(unsigned)pwmDutyEf[1],(unsigned)pwmDutyEf[2],(unsigned)fr.cMin,(unsigned)fr.cMax,(unsigned)fr.rMin,(unsigned)fr.rMax,(unsigned)fr.rIni10,(unsigned)fr.rFull10,
           (unsigned)fr.vMin,(unsigned)fr.vMax,(unsigned)fr.vIni,(unsigned)fr.vFull,(unsigned)fr.co2On,(unsigned)fr.co2Ini,(unsigned)fr.co2Full);
  return String(b)+String(g);
}
void telaComandosTick(){   // no loop(), fora de qualquer callback
  if(telaSetPend[0]){
    char buf[48]; strlcpy(buf,telaSetPend,sizeof(buf)); telaSetPend[0]=0;
    int br=100,dm=30,of=120,sp=0,rl=0;
    if(sscanf(buf,"%*[^|]|%d|%d|%d|%d|%d",&br,&dm,&of,&sp,&rl)==5){
      if(br<10) br=10; if(br>100) br=100;
      if(of<30) of=30; if(of>600) of=600;
      if(dm<0) dm=0; if(dm>0 && dm>of-10) dm=of-10;
      brilhoPct=(uint8_t)br; dimSegundos=(uint16_t)dm; screenOffTimeoutSec=(unsigned long)of; telaSempreLigada=(sp==1); relogioSempre=(rl==1);
      saveDisplayCfg(); blAplica();
      char lg[110]; snprintf(lg,sizeof(lg),"Tela ajustada pelo app: brilho %d%%, reduzir %ds, apagar %ds, manter %s, relogio %s",br,dm,of,sp?"sim":"nao",rl?"sim":"nao");
      logEvento("Automacao",lg,true);
      if(appState==S_DISPLAY && !telaSub) drawDisplayScreen();
    }
  }
  if(fansRegPend[0]){   // Fase 112: funcoes e regras vindas do PWA ("<ms>|m0,m1,m2|cMin,cMax|...")
    char buf[150]; strlcpy(buf,fansRegPend,sizeof(buf)); fansRegPend[0]=0;
    const char* q=strchr(buf,'|');
    if(q && fanRegrasParse(String("F1|")+String(q+1))){
      fanRegrasSalvar(); fansEfManual();
      char lg[110]; snprintf(lg,sizeof(lg),"Funcoes dos fans ajustadas pelo app: %s / %s / %s",FAN_FN_NOME[fanModo[0]],FAN_FN_NOME[fanModo[1]],FAN_FN_NOME[fanModo[2]]);
      logEvento("Automacao",lg,true);
      if(appState==S_FANS) fansDraw(); else if(appState==S_FANREGRAS) fanRegrasDraw();
    }
  }
  if(fansSetPend[0]){
    char buf[40]; strlcpy(buf,fansSetPend,sizeof(buf)); fansSetPend[0]=0;
    int at=0,f1=100,f2=100,f3=100;
    if(sscanf(buf,"%*[^|]|%d|%d|%d|%d",&at,&f1,&f2,&f3)==4){
      int v[3]={f1,f2,f3};
      for(int i=0;i<3;i++){ if(v[i]<0) v[i]=0; if(v[i]>100) v[i]=100; pwmSet(i,v[i]); }
      if((at==1)!=pwmAtivo) pwmAlternar();
      char lg[100]; snprintf(lg,sizeof(lg),"Fans ajustados pelo app: PWM %s, fan1 %d%%, fan2 %d%%, fan3 %d%%",pwmAtivo?"ligado":"desligado",v[0],v[1],v[2]);
      logEvento("Automacao",lg,true);
      if(appState==S_FANS) fansDraw();
    }
  }
}
static inline float advRef(int s,int v){ return s==3?(float)v:v/10.0f; }   // CO2 guarda ppm direto; os demais, decimos
static bool advLatch[ADV_MAX]={false}, advInit[ADV_MAX]={false};
static unsigned long advUltMs[ADV_MAX]={0};
static String advSerializar(){
  String s;
  if(advN==0) return s;
  s="|X";
  for(int i=0;i<advN;i++){
    const RegraAv& r=adv[i]; char b[120];
    snprintf(b,sizeof(b),"|%u,%u,%u,%d,%u,%u,%u,%d,%u,%u,%u,%d,%u,%u,%u,%u",(unsigned)r.ativa,(unsigned)r.s1,(unsigned)r.op1,(int)r.v1,(unsigned)r.comb,(unsigned)r.s2,(unsigned)r.op2,(int)r.v2,
             (unsigned)r.temOff,(unsigned)r.sOff,(unsigned)r.opOff,(int)r.vOff,(unsigned)r.acaoA,(unsigned)r.acaoI,(unsigned)r.hIni,(unsigned)r.hFim);
    s+=b;
  }
  return s;
}
static void advDefaultRegra(RegraAv& r){
  memset(&r,0,sizeof(r)); r.ativa=1; r.s1=0; r.op1=0; r.v1=375; r.comb=0; r.s2=1; r.op2=3; r.v2=500; r.temOff=0; r.sOff=0; r.opOff=1; r.vOff=373;
  r.acaoA=0x0004;   // ENTAO: CH... (por padrao liga o CH2; o usuario escolhe)
  r.acaoI=0; r.hIni=0; r.hFim=0;
}
// resto = "X|reg|reg..." (vazio = sem regras avancadas)
static void advParse(const String& resto){
  advN=0; memset(advLatch,0,sizeof(advLatch)); memset(advInit,0,sizeof(advInit));
  if(!resto.startsWith("X")) return;
  int pos=resto.indexOf('|'); if(pos<0) return; pos++;
  while(pos<=(int)resto.length() && advN<ADV_MAX){
    int e=resto.indexOf('|',pos); if(e<0) e=resto.length();
    String tk=resto.substring(pos,e); pos=e+1;
    long v[16]={1,0,0,375,0,1,3,500,0,0,1,373,0,0,0,0}; int k=0,p=0;
    while(k<16 && p<=(int)tk.length()){ int c=tk.indexOf(',',p); if(c<0) c=tk.length(); v[k++]=tk.substring(p,c).toInt(); p=c+1; }
    RegraAv& r=adv[advN++];
    r.ativa=v[0]?1:0; r.s1=(uint8_t)(v[1]%4); r.op1=(uint8_t)(v[2]&3); r.v1=(int16_t)v[3]; r.comb=(uint8_t)(v[4]%3); r.s2=(uint8_t)(v[5]%4); r.op2=(uint8_t)(v[6]&3); r.v2=(int16_t)v[7];
    r.temOff=v[8]?1:0; r.sOff=(uint8_t)(v[9]%4); r.opOff=(uint8_t)(v[10]&3); r.vOff=(int16_t)v[11]; r.acaoA=(uint16_t)v[12]; r.acaoI=(uint16_t)v[13]; r.hIni=(uint16_t)(v[14]%1440); r.hFim=(uint16_t)(v[15]%1440);
    if(e>=(int)resto.length()) break;
  }
}

static void autoDefaults(){
  autoAtivo=false; autoSoCiclo=true; advN=0; manterAtivo=false; autoRetomaMin=10;
  for(int i=0;i<AUTO_N;i++){
    RegraAuto& r=regras[i]; r.modo=AM_OFF; r.alvoCustom=0; r.alvo10=375; r.hist10=2; r.onSeg=60; r.cadaMin=60; r.hIni=480; r.hFim=1080; r.autoOffMin=0;
  }
  regras[0].modo=AM_AQUECER;                                            // Resistencia
  regras[1].modo=AM_UMIDIFICAR; regras[1].alvo10=550; regras[1].hist10=10;   // Umidificador (Fase 70: histerese 1%, antes 3% -- ficava parado a ate 3 pontos do ideal)
  regras[2].autoOffMin=5;                                               // Bomba de agua: limite de seguranca
  regras[3].modo=AM_PERIODICO; regras[3].onSeg=120; regras[3].cadaMin=60;    // Bomba de ar: ventila 2 min por hora
  regras[4].autoOffMin=10;                                              // LED ovoscopio: apaga sozinho
  regras[7].modo=AM_CIRCULAR;                                           // Fans (CH8): Fase 70 -- circulam o ar enquanto a automacao esta ativa
}
String autoSerialize(){
  String s="A1|"; s+=autoAtivo?"1":"0"; s+="|"; s+=autoSoCiclo?"1":"0";
  for(int i=0;i<AUTO_N;i++){
    const RegraAuto& r=regras[i]; char b[64];
    snprintf(b,sizeof(b),"|%u,%u,%d,%d,%u,%u,%u,%u,%u",(unsigned)r.modo,(unsigned)r.alvoCustom,(int)r.alvo10,(int)r.hist10,(unsigned)r.onSeg,(unsigned)r.cadaMin,(unsigned)r.hIni,(unsigned)r.hFim,(unsigned)r.autoOffMin);
    s+=b;
  }
  s+="|"; s+=hwSerializar();   // Fase 78: hardware ("|H,...")
  s+=manterAtivo?"|M,1":"|M,0";   // Fase 79: manter o ambiente sem ciclo
  s+="|R,"; s+=String((unsigned)autoRetomaMin);   // Fase 111: religar a automacao sozinha apos N min (0 = nunca)
  s+="|L,0";                      // Fase 82: comando de tampa vindo do app ("L,1" = vou abrir · "L,2" = fechei); o aparelho zera de volta
  s+=advSerializar();   // Fase 57: regras avancadas ("|X|...")
  return s;
}
bool autoParse(const String& s){
  if(!s.startsWith("A1|")) return false;
  RegraAuto nv[AUTO_N]; bool a=false, sc=true; int idx=0, pos=3;
  while(pos<=(int)s.length() && idx<AUTO_N+2){
    int e=s.indexOf('|',pos); if(e<0) e=s.length();
    String tk=s.substring(pos,e);
    if(idx==0) a=(tk=="1");
    else if(idx==1) sc=(tk=="1");
    else{
      long v[9]={0,0,375,2,60,60,480,1080,0}; int k=0, p=0;
      while(k<9 && p<=(int)tk.length()){ int c=tk.indexOf(',',p); if(c<0) c=tk.length(); v[k++]=tk.substring(p,c).toInt(); p=c+1; }
      RegraAuto& r=nv[idx-2];
      r.modo=(uint8_t)(v[0]>=0&&v[0]<AM_N?v[0]:0); r.alvoCustom=v[1]?1:0; r.alvo10=(int16_t)v[2]; r.hist10=(int16_t)(v[3]<1?1:v[3]); r.onSeg=(uint16_t)v[4]; r.cadaMin=(uint16_t)(v[5]<1?1:v[5]);
      r.hIni=(uint16_t)(v[6]%1440); r.hFim=(uint16_t)(v[7]%1440); r.autoOffMin=(uint16_t)v[8];
    }
    idx++; pos=e+1;
  }
  if(idx<AUTO_N+2) return false;
  autoAtivo=a; autoSoCiclo=sc; memcpy(regras,nv,sizeof(nv));
  String resto=pos<(int)s.length()?s.substring(pos):String();
  if(resto.startsWith("H,")){   // Fase 78: hardware
    int e=resto.indexOf('|'); hwParse(e<0?resto:resto.substring(0,e)); resto=(e<0)?String():resto.substring(e+1);
  }
  if(resto.startsWith("M,")){   // Fase 79
    int e=resto.indexOf('|'); String m=(e<0)?resto:resto.substring(0,e); bool nv=(m.length()>2 && m[2]=='1');
    if(nv!=manterAtivo){ manterAtivo=nv; aplicarLimitesDoTipo(); }
    resto=(e<0)?String():resto.substring(e+1);
  }
  if(resto.startsWith("R,")){   // Fase 111
    int e=resto.indexOf('|'); String m=(e<0)?resto:resto.substring(0,e); long v=m.substring(2).toInt(); if(v<0) v=0; if(v>240) v=240; autoRetomaMin=(uint16_t)v;
    resto=(e<0)?String():resto.substring(e+1);
  }
  if(resto.startsWith("L,")){   // Fase 82
    int e=resto.indexOf('|'); String m=(e<0)?resto:resto.substring(0,e);
    if(m.length()>2 && (m[2]=='1'||m[2]=='2')){ tampaCmd=(uint8_t)(m[2]-'0'); tampaSyncPend=1; }
    resto=(e<0)?String():resto.substring(e+1);
  }
  advParse(resto);   // Fase 57
  return true;
}
void autoSalvarCfg(){ File c=LittleFS.open(AUTO_CFG_PATH,"w"); if(!c) return; c.println(autoSerialize()); c.close(); }
void autoCarregarCfg(){
  autoDefaults(); ultCarregar();
  File c=LittleFS.open(AUTO_CFG_PATH,"r"); if(!c) return;
  String s=c.readStringUntil('\n'); c.close(); s.trim(); if(s.length()) autoParse(s);
}
void fbAutoCallback(AsyncResult &aResult){
  if(aResult.isError()) Serial.printf("[Firebase Automacao ERRO] %s, code: %d\n",aResult.error().message().c_str(),aResult.error().code());
}
// Edicao local: grava, avisa o PWA (cmd.auto) e marca o relogio logico (evita o poll antigo desfazer a edicao)
void autoPersistir(){
  autoSalvarCfg(); lastLocalAutoMs=millis();
  if(!fbStarted||!fbApp.ready()) return;
  Document<Values::Value> doc("cmd",Values::Value(Values::MapValue("auto",Values::StringValue(autoSerialize()))));
  PatchDocumentOptions opt(DocumentMask("cmd.auto"),DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbAutoCallback,"fbAutoTask");
}
// Chamado pelo poll (callback): so guarda; aplicar/gravar e barato e nao mexe em rede.
static void autoTiraTok(String& t,const char* pf){ int a=t.indexOf(pf); if(a>=0){ int b=t.indexOf('|',a+1); t.remove(a,(b<0?t.length():b)-a); } }
void autoDoPoll(const String& au,unsigned long pollIssuedAtMs){
  if(lastLocalAutoMs>=pollIssuedAtMs) return;          // edicao local mais nova que esta leitura
  if(hwSujo || millis()-lastLocalAutoMs<10000UL) return;   // Fase 87: edicao na tela Hardware ainda nao salva/enviada: o poll antigo desfazia a opcao (ex.: bomba de agua)
  bool temCmd=(au.indexOf("|L,1")>=0 || au.indexOf("|L,2")>=0);   // Fase 82
  if(!temCmd){
    String cur=autoSerialize(), a2=au; autoTiraTok(cur,"|L,"); autoTiraTok(a2,"|L,");
    if(a2==cur) return;
    if(a2.indexOf("|H,")<0){ autoTiraTok(cur,"|H,"); autoTiraTok(cur,"|M,"); if(a2==cur) return; }   // PWA antigo (sem hardware/manter)
    if(a2.indexOf("|R,")<0){ autoTiraTok(cur,"|R,"); if(a2==cur) return; }   // Fase 111: PWA sem o token de religar
  }
  if(autoParse(au)){ autoSalvarCfg(); Serial.println("[Automacao] configuracao atualizada pelo app."); }
}

// ── Alvos vindos do tipo do ciclo ──
static const TipoOvo* autoTipoAtual(){
  if(!fbCicloAtivo && ambManterOn()) return manterTipoPtr();   // Fase 79
  if(!fbCicloAtivo||!fbCicloTipo[0]) return nullptr;
  for(int i=0;i<tiposN;i++) if(strcasecmp(tipos[i].nome,fbCicloTipo)==0) return &tipos[i];
  return nullptr;
}
// Fase 78 -- alvo personalizado FORA da faixa do ciclo (ex.: aquecedor em 37,5 C com tipo 40-41 C): o ambiente nunca ficaria "pronto".
// Enquanto ha um ciclo com tipo valido, so vale o personalizado que cai dentro da faixa do tipo (+-0,3); senao usa o do tipo e avisa 1x por ciclo.
static void autoAvisaDiverge(bool temp,float v){
  static long ep[2]={0,0}; int i=temp?0:1;
  if(ep[i]==(long)fbCicloIniciadoEm) return; ep[i]=(long)fbCicloIniciadoEm;
  char m[120];
  if(temp) snprintf(m,sizeof(m),"Alvo do aquecedor %.1fC fora da faixa do ciclo (%.1f-%.1fC): usando o alvo do tipo",v,TEMP_IDEAL_MIN,TEMP_IDEAL_MAX);
  else     snprintf(m,sizeof(m),"Alvo de umidade %.0f%% fora da faixa do ciclo (%.0f-%.0f%%): usando o alvo do tipo",v,UMID_IDEAL_MIN,UMID_IDEAL_MAX);
  logEvento("Automacao",m,true);
}
static float autoAlvoTemp(const RegraAuto& r){
  const TipoOvo* t=autoTipoAtual();
  if(r.alvoCustom){
    float v=r.alvo10/10.0f;
    if(!t || (v>=TEMP_IDEAL_MIN-0.3f && v<=TEMP_IDEAL_MAX+0.3f)) return v;
    autoAvisaDiverge(true,v);
  }
  if(t&&t->eclDia>0&&t->tempEcl10>0&&calcularDiaCicloAtual()>=t->eclDia) return t->tempEcl10/10.0f;   // Fase 69: temperatura de eclosao
  if(t&&t->temp10>0) return t->temp10/10.0f;
  return (TEMP_IDEAL_MIN+TEMP_IDEAL_MAX)/2.0f;
}
static float autoAlvoUmid(const RegraAuto& r){
  const TipoOvo* t=autoTipoAtual();
  if(r.alvoCustom){
    float v=r.alvo10/10.0f;
    if(!t || (v>=UMID_IDEAL_MIN-3.0f && v<=UMID_IDEAL_MAX+3.0f)) return v;
    autoAvisaDiverge(false,v);
  }
  if(t){
    int dia=calcularDiaCicloAtual(), total=fbCicloDuracaoDias>0?fbCicloDuracaoDias:21;
    bool ecl=(t->eclDia>0)?(dia>=t->eclDia):(dia>total-3);   // Fase 69: a partir do dia de eclosao do TIPO (antes: ultimos 3 dias fixos)
    if(dia>0 && ecl && t->umidEcl>0) return (float)t->umidEcl;
    if(t->umid>0) return (float)t->umid;
    return (t->umidMin+t->umidMax)/2.0f;
  }
  return (UMID_IDEAL_MIN+UMID_IDEAL_MAX)/2.0f;
}

// ── Motor ──
static unsigned long autoPausaAte[AUTO_N]={0}, autoUltToggleMs[AUTO_N]={0}, autoOnDesde[AUTO_N]={0};
static bool autoUltimo[AUTO_N]={false}, autoTinhaUltimo[AUTO_N]={false}, autoPrevOn[AUTO_N]={false};
static bool autoEstavaAtivo=false, autoSensorAvisado=false, autoSobretempAvisada=false;

// Fase 70/72 -- inclinacao da temperatura (C/min) numa janela de ~30 s (amostra a cada 2 s, 16 amostras; precisa de >=10 s).
// A decisao de DESLIGAR/NAO LIGAR usa a temperatura PREVISTA = atual + inclinacao x "lag" (tempo que o aquecedor leva pra
// aparecer no sensor, APRENDIDO a cada pulso -- ~20 s numa caixinha de isopor, minutos numa estufa grande).
static float tempSlopeMin=0;
static float ctlT=NAN, ctlU=NAN;   // leitura "rapida" do SHT31 (1 Hz) so para o controle; o resto do app segue com a de 5 s
static void tempSlopeAtualizar(){
  static unsigned long tms[16]; static float tv[16]; static int n=0; static unsigned long ult=0;
  unsigned long agora=millis();
  if(isnan(ctlT)){ n=0; tempSlopeMin=0; return; }
  if(n==0 || agora-ult>=2000UL){
    ult=agora;
    if(n==16){ for(int i=1;i<16;i++){ tms[i-1]=tms[i]; tv[i-1]=tv[i]; } n=15; }
    tms[n]=agora; tv[n]=ctlT; n++;
  }
  unsigned long span=agora-tms[0];
  tempSlopeMin=(n>=2 && span>=10000UL)?(ctlT-tv[0])/(span/60000.0f):0;
}

static float humSlopeMin=0;   // Fase 78: inclinacao da umidade (%UR/min), mesma janela de ~30 s
static void humSlopeAtualizar(){
  static unsigned long tms[16]; static float tv[16]; static int n=0; static unsigned long ult=0;
  unsigned long agora=millis();
  if(isnan(ctlU)){ n=0; humSlopeMin=0; return; }
  if(n==0 || agora-ult>=2000UL){
    ult=agora;
    if(n==16){ for(int i=1;i<16;i++){ tms[i-1]=tms[i]; tv[i-1]=tv[i]; } n=15; }
    tms[n]=agora; tv[n]=ctlU; n++;
  }
  unsigned long span=agora-tms[0];
  humSlopeMin=(n>=2 && span>=10000UL)?(ctlU-tv[0])/(span/60000.0f):0;
}
static void autoAplica(int ch,bool on,const char* motivo){
  lastLocalTouchMs[ch]=millis();
  setRelay(ch,on); if(ch!=2) pushActuatorToFirestore(ch,on);   // Fase 87: CH3 nunca na nuvem
  autoUltimo[ch]=on; autoTinhaUltimo[ch]=true; autoUltToggleMs[ch]=millis(); autoPrevOn[ch]=on; if(on) autoOnDesde[ch]=millis();
  char msg[72]; snprintf(msg,sizeof(msg),"%s -> %s (%s)",RELAY_NAMES[ch],on?"ON":"OFF",motivo);
  logEvento("Automacao",msg,true);
}
// Fase 111 -- os 3 fans em 0 % (PWM ligado) desligam o CH8 (+12 V dos fans); qualquer fan acima de 0 % religa (se foi esta regra que desligou).
// Age so na TRANSICAO: um toque manual no CH8 depois nao e desfeito. Sem fans o aquecedor tambem para (intertravamento do modo Circular).
void fansCh8Tick(){
  static bool zeroAnt=false; bool z=fansTodosZero();
  if(z && !zeroAnt){ if(relayState[7]){ autoAplica(7,false,"todos os fans em 0%"); } fansCh8PorZero=true; }
  else if(!z && zeroAnt){ if(fansCh8PorZero){ fansCh8PorZero=false; if(!relayState[7]) autoAplica(7,true,"fan acima de 0%"); } }
  zeroAnt=z;
}
// ═══════════════════════════════════════════════════════════════
// Fase 71/72 -- controle por PULSOS ADAPTATIVO (aquecedor e umidificador).
// Quando falta pouco para o alvo, o rele da um PULSO e ESPERA o efeito aparecer no sensor. Aprende, a cada pulso:
//  * o GANHO k (unidade subida por segundo ligado) -> tamanho do proximo pulso = metade do que falta / k;
//  * o LAG (tempo entre desligar e o sensor atingir o pico) -> quanto esperar e quanto prever a frente.
// Escala automatica: pulsos de 0,5 s (100 W numa caixinha de isopor) ate 60 s (estufa grande). Se o pulso nao produz
// subida mensuravel, o proximo dobra (busca geometrica); a leitura do SHT31 para o controle e de 1 Hz.
// Longe do alvo (>3 C / >12 %) fica ligado direto e passa a pulsar ao chegar perto. Aquecedor so com as Fans ligadas.
// Nuvem: pulsos NAO vao para o log nem para o estado do rele (seriam milhares de escritas/dia); a nuvem so ve a "sessao"
// (ligado enquanto houver pulsos nos ultimos 60 s). Logs de pulso: so SD; resumo de 15 min na nuvem.
// ═══════════════════════════════════════════════════════════════
struct PulsoCtl{ uint8_t fase, n, fails; uint16_t cont; bool sess, cortado, sujo, serie; unsigned long ini, fimMs, durMs, tPico, ultResumo, ultFim; float x0, pico, k, lagS, agr, alvoP, x0Base, onAcum; };
static PulsoCtl pulso[AUTO_N];
static void autoAplicaPulso(int ch,bool on,const char* motivo){
  setRelay(ch,on);
  autoUltimo[ch]=on; autoTinhaUltimo[ch]=true; autoUltToggleMs[ch]=millis(); autoPrevOn[ch]=on; if(on) autoOnDesde[ch]=millis();
  char msg[72]; snprintf(msg,sizeof(msg),"%s -> %s (%s)",RELAY_NAMES[ch],on?"ON":"OFF",motivo);
  logEvento("Automacao",msg,false);   // so SD
}
#define PULSO_CFG_PATH "/pulso2.cfg"   // Fase 74: v2 (ignora valores aprendidos com o bug do ganho colapsado)
static void pulsoSalvar(){
  File f=LittleFS.open(PULSO_CFG_PATH,"w"); if(!f) return;
  for(int c=0;c<AUTO_N;c++) if(pulso[c].k>0) f.printf("%d,%.5f,%.1f,%.3f\n",c,pulso[c].k,pulso[c].lagS,pulso[c].agr);
  f.close();
}
static void pulsoCarregar(){
  static bool feito=false; if(feito) return; feito=true;
  File f=LittleFS.open(PULSO_CFG_PATH,"r"); if(!f) return;
  while(f.available()){
    String l=f.readStringUntil('\n'); int c; float k,lag,agr;
    if(sscanf(l.c_str(),"%d,%f,%f,%f",&c,&k,&lag,&agr)==4 && c>=0 && c<AUTO_N && k>0 && lag>0 && agr>0){ pulso[c].k=k; pulso[c].lagS=lag; pulso[c].agr=agr; pulso[c].n=1; }
  }
  f.close();
}
static inline float pulsoX(int ch){ return regras[ch].modo==AM_AQUECER?ctlT:ctlU; }
static void pulsoEncerrar(int ch,const char* motivo){
  PulsoCtl& p=pulso[ch]; unsigned long agora=millis();
  p.cortado=(motivo[0]=='a');   // "alvo previsto": cortado antes do planejado
  autoAplicaPulso(ch,false,motivo);
  p.durMs=agora-p.ini; p.fimMs=agora; p.ultFim=agora; p.fase=2; p.tPico=agora;
  float x=pulsoX(ch); if(!isnan(x) && x>p.pico) p.pico=x;
}
// Chamado a cada volta do loop(): encerra o pulso na hora certa (precisao de ms, o tick da automacao e de 1 s)
void pulsoFimRapido(){
  for(int ch=0;ch<AUTO_N;ch++){
    PulsoCtl& p=pulso[ch];
    if(p.fase==1 && relayState[ch] && millis()-p.ini>=p.durMs) pulsoEncerrar(ch,"fim do pulso");
  }
}
__attribute__((unused)) static void autoPulsoTick(int ch,bool temp,float x,float alvo,float hist){
  PulsoCtl& p=pulso[ch]; unsigned long agora=millis();
  pulsoCarregar();
  if(p.k<=0){ p.k=temp?0.30f:0.05f; p.lagS=temp?20.0f:45.0f; p.n=0; }
  if(p.agr<=0) p.agr=0.5f;
  if(!p.ultResumo) p.ultResumo=agora;
  const float kMin=temp?0.0005f:0.005f, kMax=temp?5.0f:5.0f, ruido=temp?0.04f:0.4f;
  float prev=x+(temp?(tempSlopeMin>0?tempSlopeMin/60.0f*p.lagS:0):0);   // previsao: onde o valor vai parar
  float err=alvo-x, grande=temp?3.0f:12.0f;
  if(agora-p.ultResumo>=900000UL){
    if(p.cont){ char m[120]; snprintf(m,sizeof(m),"%s: %u pulsos em 15 min (ganho %.3f %s/s, lag %.0fs, agress. %.2f)",RELAY_NAMES[ch],(unsigned)p.cont,p.k,temp?"C":"%",p.lagS,p.agr); logEvento("Automacao",m,true); }
    p.cont=0; p.ultResumo=agora;
  }
  { static unsigned long ultSalvo=0;   // grava o aprendizado (no maximo a cada 5 min) se mudou
    if(p.sujo && p.fase==0 && agora-ultSalvo>=300000UL){ ultSalvo=agora; for(int c=0;c<AUTO_N;c++) pulso[c].sujo=false; pulsoSalvar(); } }
  if(p.fase==1){   // pulsando (o fim por tempo e feito em pulsoFimRapido)
    if(!relayState[ch]){ p.fase=0; return; }   // desligado por fora (seguranca/manual)
    if(x>p.pico) p.pico=x;
    if(x>=alvo || prev>=alvo) pulsoEncerrar(ch,"alvo previsto");
    return;
  }
  if(p.fase==2){   // esperando o efeito aparecer no sensor
    // Fase 74b -- "SERIE": enquanto o calor de pulsos anteriores ainda nao chegou ao sensor (tempo morto grande: sensor longe da
    // resistencia), os pulsos seguintes NAO podem ser medidos isoladamente (a subida seria atribuida ao pulso errado, superestimando
    // ou subestimando o ganho). A serie guarda o valor inicial (x0Base) e o total de segundos ligados (onAcum); o ganho so e
    // calculado quando a resposta aparece, como (subida total)/(segundos ligados totais), e so depois do PICO.
    float lim=temp?ruido:ruido*1.5f;
    bool subiu=(p.pico-p.x0Base)>lim;
    if(x>p.pico){ p.pico=x; p.tPico=agora; }
    float soakMax=p.lagS*2.0f; if(soakMax<(temp?30.0f:45.0f)) soakMax=temp?30.0f:45.0f; if(soakMax>300.0f) soakMax=300.0f;
    unsigned long dt=agora-p.fimMs;
    bool pico=(subiu && x<=p.pico-ruido && dt>=5000UL && dt>=(unsigned long)((temp?10.0f:25.0f)*1000.0f));   // ja passou do pico (e esperou o minimo)
    bool tempoAcabou=subiu ? (dt>=(unsigned long)(soakMax*2.5f*1000.0f)) : (dt>=(unsigned long)(soakMax*1000.0f));   // subindo: espera ate o pico
    if(pico || tempoAcabou){
      if(p.durMs>0 && p.onAcum>=0.4f){
        float rise=p.pico-p.x0Base;
        if(!subiu){   // nenhuma resposta ainda (serie continua aberta): calor demora -> espera mais e aumenta o pulso devagar
          p.lagS*=1.5f; if(p.lagS>300.0f) p.lagS=300.0f;
          p.k*=0.7f; if(p.k<kMin) p.k=kMin;
          if(p.n>1) p.n=1; if(p.fails<8) p.fails++;
        }else{
          float g=rise/p.onAcum; if(g<kMin) g=kMin; if(g>kMax) g=kMax;
          p.k=(!p.n || g>2.5f*p.k || g<0.4f*p.k)?g:(0.6f*p.k+0.4f*g);   // modelo muito errado: reaprende de uma vez
          if(p.n<250) p.n++;
          p.fails=0; p.serie=false;   // resposta medida: fecha a serie
          float lag=(p.tPico-p.fimMs)/1000.0f; if(lag<3.0f) lag=3.0f; if(lag>300.0f) lag=300.0f; p.lagS=0.6f*p.lagS+0.4f*lag;
          // AUTO-AJUSTE do metodo: quao agressivo cada pulso e (fracao do que falta que ele tenta cobrir)
          float acima=p.pico-p.alvoP; float oa=p.agr;
          if(acima>(temp?0.15f:2.0f)){ p.agr*=0.75f; if(p.agr<0.12f) p.agr=0.12f; }                 // passou do alvo: mais cauteloso
          else if(!p.cortado && x<p.alvoP-hist && p.pico<p.alvoP-hist){ p.agr*=1.2f; if(p.agr>0.9f) p.agr=0.9f; }   // nao chegou nem na banda: mais firme
          if(p.agr>oa*1.01f || p.agr<oa*0.99f){ char m[96]; snprintf(m,sizeof(m),"%s: ajuste automatico da agressividade %.2f -> %.2f (%s)",RELAY_NAMES[ch],oa,p.agr,p.agr<oa?"passou do alvo":"ficou aquem"); logEvento("Automacao",m,false); }
        }
        p.sujo=true;
      }
      p.fase=0;
    }
    return;
  }
  if(relayState[ch]){   // ligado em modo continuo (aquecimento inicial): ao chegar perto do alvo passa a pulsar
    bool demais=(agora-autoOnDesde[ch])>480000UL;   // trava de seguranca: no maximo 8 min direto
    if(x>=alvo || prev>=alvo || err<=grande || demais){
      autoAplicaPulso(ch,false,demais?"limite de 8 min direto":"perto do alvo: entrando em pulsos");
      p.fase=2; p.durMs=0; p.serie=false; p.x0=x; p.x0Base=x; p.pico=x; p.fimMs=agora; p.tPico=agora; p.ultFim=agora;
    }
    return;
  }
  if(err<=hist || prev>=alvo) return;   // dentro da banda, ou a previsao ja passa do alvo
  if(temp){ int fanCh=-1; for(int k=0;k<AUTO_N;k++) if(regras[k].modo==AM_CIRCULAR){ fanCh=k; break; } if(fanCh>=0 && !relayState[fanCh]) return; }   // nao aquece sem circular o ar
  if(err>grande){ autoAplicaPulso(ch,true,temp?"aquecimento inicial":"umidificacao inicial"); p.sess=true; return; }
  float teto=p.lagS*1.5f; if(teto<60.0f) teto=60.0f; if(teto>600.0f) teto=600.0f;   // sistema lento -> pulsos mais longos permitidos
  float naoVal=(temp?3.0f:15.0f)*powf(1.6f,(float)p.fails); float capNaoVal=(p.fails<6)?(temp?12.0f:30.0f):60.0f; if(naoVal>capNaoVal) naoVal=capNaoVal;   // sem ganho validado: pulsos pequenos, crescendo devagar
  float minOn=temp?0.5f:2.0f, maxOn=(p.n>=2)?teto:naoVal;
  float sg=p.agr*err/p.k; if(sg<minOn) sg=minOn; if(sg>maxOn) sg=maxOn;
  if(!p.serie){ p.serie=true; p.x0Base=x; p.pico=x; p.onAcum=0; }   // abre uma serie (ver fase 2)
  p.onAcum+=sg;
  p.durMs=(unsigned long)(sg*1000.0f); p.ini=agora; p.x0=x; p.fase=1; p.cont++; p.sess=true; p.alvoP=alvo; p.cortado=false;
  char m[40]; snprintf(m,sizeof(m),"pulso de %.1fs",sg);
  autoAplicaPulso(ch,true,m);
}
// ═══════════════════════════════════════════════════════════════
// Fase 76/78 -- controle PI de ciclo fixo (time-proportional) para o AQUECEDOR e o UMIDIFICADOR (substitui o "pulso + janela de espera").
// Motivo (logs reais, 2026-10-04): numa caixa que PERDE calor o tempo todo, o aprendizado por resposta ao pulso derrubava o ganho e a
// temperatura caia sem parar. O PI nao precisa identificar o sistema: o termo integral aprende sozinho o duty que compensa a perda.
// Fase 78 -- ajuste fino (simulado em planta calibrada com os logs; 100 W / 3 L: +-0,1 C, antes +-0,5 C):
//   * o corte antecipado do pulso por "temperatura prevista" foi REMOVIDO (distorcia a saida do PI e criava erro permanente abaixo do alvo);
//     so corta por seguranca se passar do alvo + 0,3 C (umidade: + 3 %);
//   * limite do termo integral e do duty subiu de 0,5/0,8 para 1,0 (o duty de regime de 55-70 % nao cabia -> a temperatura ficava abaixo);
//   * histerese entre "aquecimento direto" (liga se faltar > 1,2 C) e PI (assume quando faltar <= 0,5 C): antes chaveava o rele a cada 1-3 s no limiar;
//   * periodo do ciclo e ganho Kp saem do HARDWARE (potencia / volume): ondulacao ~ potencia x periodo / capacidade termica;
//     a taxa de subida real e MEDIDA nos aquecimentos diretos e substitui a estimativa;
//   * duty de regime inicial estimado pela perda da caixa (material, volume, potencia) e reescalado quando o alvo muda;
//   * umidade: mesmo PI (ganho ~ 1/(%UR por segundo ligado), medido igual), periodo 30 s;
//   * auto-ajuste dos ganhos a cada 10 min (oscilou -> mais manso; lento e longe do limite -> mais firme) + aviso quando o aquecedor/umidificador
//     fica no limite por 10 min (potencia insuficiente).
//   * memoria em /pid2.cfg: I, escala, volume, potencia, alvo e taxa medida (sobrevive a reboot; descartados se o hardware mudar > 20 %).
// ═══════════════════════════════════════════════════════════════
#define PID_CFG_PATH "/pid3.cfg"   // Fase 82: descarta o aprendizado das fases 78-81 (ganhos antigos / escala 0,5)
struct PidCtl{ bool init, sujo, satAv; float I, esc, acc, somaAbs, rMeas, rPico, alvoRef, volCfg, ptotCfg, perAt; unsigned long perIni, janIni, onMs, resumoIni, dirIni; uint16_t satTot, satOn; int8_t sinalAnt; uint8_t osc; uint16_t nAmostras; };
static PidCtl pid[AUTO_N];
struct PidPar{ float per, leadS, leadMax, kp, ti, dirOn, dirOff, corte, minOn, tolOsc, tolMed; bool fans; unsigned long dirMaxMs; };
static float pidPotencia(bool temp){ return temp?hwPtot():(float)hw.umMlH*hw.umQtd; }
static PidPar pidParametros(bool temp,const PidCtl& c){
  PidPar q;
  if(temp){
    float ron=hwRonPrior(); if(c.rMeas>ron) ron=c.rMeas; if(ron<0.001f) ron=0.001f;   // Fase 81: a taxa medida (liquida, em temperatura alta) so encurta o periodo; antes alongava para 28 s e a ondulacao crescia
    float pm=hw.rele?3.0f:15.0f;   // rele mecanico: nao chaveia mais rapido que 15 s (vida util); SSR aguenta
    q.per=0.15f/ron; if(q.per<pm) q.per=pm; if(q.per>30.0f) q.per=30.0f;
    q.kp=0.0014f/ron; if(q.kp<0.03f) q.kp=0.03f; if(q.kp>0.6f) q.kp=0.6f;   // Fase 81b: planta identificada nos logs reais = 77 s de atraso morto + ~100 s de inercia do sensor (+-0,03 C de erro no ajuste) -> ganho brando
    q.ti=900.0f;   // Fase 82f (era 1500: lento demais para achar o duty de regime) | Fase 82e (log de 12:03-12:24: rajada de ~65 s de aquecedor -> +1,3 C por 4 min -> queda por 7 min; planta real K~32 C/duty, perda tau~1700 s, atraso ~1,5-2 min) | Fase 81b (era 450, 200 antes): era 200 s -> o integral oscilava (I de 0,0 a 0,46) numa caixa onde o calor leva ~90 s para chegar ao sensor
    q.leadS=45.0f*powf(hw.volL/3.0f,0.25f)*HW_AQ_LAG[hw.aqTipo]; if(q.leadS<20.0f) q.leadS=20.0f; if(q.leadS>90.0f) q.leadS=90.0f;   // Fase 82e: antecipacao curta (150 s fazia o termo de inclinacao mandar mais que o erro)   // antecipacao ~ atraso total do sensor
    q.leadMax=1.5f; q.dirOn=1.2f; q.dirOff=0.5f; q.corte=0.8f;   /* Fase 82f: corte duro de 0,3 C virava um termostato (liga/desliga com atraso -> ciclo limite de ~1 C); o PI modula ate +0,8 C */ q.minOn=0.5f; q.tolOsc=0.25f; q.tolMed=0.4f; q.fans=true; q.dirMaxMs=480000UL;
  }else{
    float k=c.rMeas>0?c.rMeas:hwKumPrior();
    q.per=30.0f; q.kp=1.0f/(k*90.0f); if(q.kp<0.004f) q.kp=0.004f; if(q.kp>0.5f) q.kp=0.5f;
    q.ti=300.0f; q.leadS=30.0f; q.leadMax=5.0f; q.dirOn=8.0f; q.dirOff=3.0f; q.corte=2.0f; /* Fase 88: era 3,0 -- a leitura passando de alvo+2 ja interrompe os pulsos (maximas de 55-57 % em 08/10) */ q.minOn=0.5f; q.tolOsc=2.0f; q.tolMed=4.0f; q.fans=false; q.dirMaxMs=600000UL;
  }
  q.kp*=c.esc;
  return q;
}
static void pidSalvar(){
  File f=LittleFS.open(PID_CFG_PATH,"w"); if(!f) return;
  for(int c=0;c<AUTO_N;c++) if(pid[c].init) f.printf("%d,%.4f,%.3f,%.0f,%.0f,%.1f,%.5f\n",c,pid[c].I,pid[c].esc,pid[c].volCfg,pid[c].ptotCfg,pid[c].alvoRef,pid[c].rMeas);
  f.close();
}
static void pidCarregar(){
  static bool feito=false; if(feito) return; feito=true;
  File f=LittleFS.open(PID_CFG_PATH,"r"); if(!f) return;
  bool mig83=!LittleFS.exists("/mig83.flag");   // Fase 84: descarta UMA vez o aprendizado do umidificador (ganho/integral corrompidos pela hora sem agua)
  while(f.available()){
    String l=f.readStringUntil('\n'); int c; float I,esc,vol,pt,al,rm=0;
    if(sscanf(l.c_str(),"%d,%f,%f,%f,%f,%f,%f",&c,&I,&esc,&vol,&pt,&al,&rm)>=6 && c>=0 && c<AUTO_N && I>=0 && I<=1.0f && esc>=0.3f && esc<=3.0f){
      if(c==1 && mig83) continue;
      pid[c].I=I; pid[c].esc=(esc>1.3f?1.3f:esc); pid[c].volCfg=vol; pid[c].ptotCfg=pt; pid[c].alvoRef=al; pid[c].rMeas=(rm>0)?rm:0; pid[c].init=true;
    }
  }
  f.close();
  if(mig83){ File g=LittleFS.open("/mig83.flag","w"); if(g){ g.print("1"); g.close(); } }
}
static void pidFimDireto(PidCtl& c,bool temp,unsigned long agora){
  unsigned long dur=agora-c.dirIni; c.onMs+=dur;
  // Fase 84: umidade so aprende com resposta plausivel (>= 25 % da estimativa do Hardware) e com agua no reservatorio. Em 07/10 uma hora SEM AGUA gravou 0,05 %/s (o piso)
  // em vez de ~2,7 -> Kp 25x maior, o integral encheu (I 0,15 -> 0,66) e a umidade passou a oscilar 44 <-> 54 mesmo depois de encher o reservatorio.
  bool plausivel=temp || (aguaEst==0 && c.rPico>=0.25f*hwKumPrior());
  if(dur>=(temp?60000UL:30000UL) && c.rPico>0 && plausivel){   // taxa de subida medida com 100% ligado
    float lo=temp?0.002f:0.05f, hi=temp?0.2f:15.0f, m=c.rPico; if(m<lo) m=lo; if(m>hi) m=hi;
    c.rMeas=(c.rMeas>0)?(0.6f*c.rMeas+0.4f*m):m; c.sujo=true;
  }
  c.rPico=0; c.dirIni=0;
}
static void autoPidTick(int ch,bool temp,float x,float alvo,float hist){
  (void)hist;
  PulsoCtl& p=pulso[ch]; PidCtl& c=pid[ch]; unsigned long agora=millis();
  pidCarregar();
  float pot=pidPotencia(temp);
  bool hwMudou=c.init && ((c.volCfg>0 && fabsf(hw.volL-c.volCfg)/c.volCfg>0.2f) || (c.ptotCfg>0 && fabsf(pot-c.ptotCfg)/c.ptotCfg>0.2f));
  if(!c.init || hwMudou){   // primeira vez, ou outro hardware: semente pela fisica da caixa
    c.init=true; c.I=temp?hwIssPrior(alvo):0.03f; c.esc=1.0f; c.rMeas=0; c.sujo=true; c.volCfg=hw.volL; c.ptotCfg=pot; c.alvoRef=alvo; c.acc=0; c.perIni=0;
  }else if(temp && c.alvoRef>0 && fabsf(alvo-c.alvoRef)>0.5f){   // alvo novo (ex.: outro tipo de ovo): o duty de regime escala com (alvo - ambiente)
    float f=(alvo-25.0f)/((c.alvoRef-25.0f)>1.0f?(c.alvoRef-25.0f):1.0f); if(f<0.5f) f=0.5f; if(f>2.0f) f=2.0f;
    c.I*=f; if(c.I>1.0f) c.I=1.0f; c.alvoRef=alvo; c.sujo=true;
  }else if(!temp && c.alvoRef!=alvo) c.alvoRef=alvo;
  if(c.esc<=0) c.esc=1.0f;
  PidPar q=pidParametros(temp,c); c.perAt=q.per;
  bool tampaA=(tampaEst==1), tampaR=(tampaEst==2);   // Fase 82
  if(!temp && tampaR){ q.dirOn=3.0f; q.dirOff=1.0f; }   // recuperando a umidade: entra direto bem antes
  if(!temp && tampaA){ if(relayState[ch]){ autoAplicaPulso(ch,false,"tampa aberta: umidificador em pausa"); pidFimDireto(c,temp,agora); } p.fase=0; c.acc=0; return; }
  float sl=temp?tempSlopeMin:humSlopeMin;
  if(!c.resumoIni){ c.resumoIni=agora; c.janIni=agora; }
  if(p.fase==2) p.fase=0;   // (pulsoEncerrar marca fase 2; no PI nao ha janela de espera)
  float lead=sl/60.0f*q.leadS; if(lead>q.leadMax) lead=q.leadMax; if(lead<-q.leadMax) lead=-q.leadMax;
  float e=alvo-(x+lead), eReal=alvo-x;
  // Fase 82c: previsao COMPLETA (sem o limite de +-1,5 C do `lead`): com ~77 s de atraso morto + ~100 s de inercia do sensor o calor em transito
  // pode somar varios graus. Em 05/10 o aquecimento direto saiu a 35,5 C (previsao limitada) e a caixa foi a 41,7 C.
  float tprev=x+sl/60.0f*q.leadS;
  // Fase 82d -- GOVERNADOR DE ENERGIA (so aquecedor): o calor aplicado nos ultimos ~260 s ainda nao chegou ao sensor (atraso morto ~77 s + inercia ~100 s).
  // gpend = (segundos ligado na janela - duty de regime x janela) x (potencia/capacidade) = subida que ainda vai acontecer. Se ela ja cobre 70 % do que falta
  // (ou passa de 0,15 C com a temperatura no alvo), NAO aquece. Em 05/10 o aquecedor ligado 82 s a 2 C do alvo levou a caixa a 39,5 C (e a 41,7 C na partida a frio).
  // Simulado com planta +-30 % de potencia/capacidade e atrasos de 60-130 s: pico <= +0,6 C e oscilacao ~0,15 C.
  bool govBloqueia=false;
  if(temp){
    static uint8_t onHist[260]; static int onIdx=0; static int onSoma=0;
    onSoma-=onHist[onIdx]; onHist[onIdx]=relayState[ch]?1:0; onSoma+=onHist[onIdx]; onIdx=(onIdx+1)%260;
    float g=hwPtot()/hwCap(), gpend=g*((float)onSoma-c.I*260.0f);
    float falta=alvo-x; if(falta<0) falta=0;
    govBloqueia=(gpend>0.15f && gpend>=0.7f*falta);
  }
  // ---- resumo de 15 min na nuvem ----
  if(agora-c.resumoIni>=900000UL){
    char m[130]; snprintf(m,sizeof(m),"%s (PI): duty medio %.1f%% em 15 min, I=%.2f, ganhos x%.2f, ciclo %.0fs",RELAY_NAMES[ch],c.onMs/9000.0f,c.I,c.esc,q.per);
    logEvento("Automacao",m,true); c.onMs=0; c.resumoIni=agora;
  }
  { static unsigned long ultSalvo=0;
    if(c.sujo && agora-ultSalvo>=300000UL){ ultSalvo=agora; for(int k=0;k<AUTO_N;k++) pid[k].sujo=false; pidSalvar(); } }
  // ---- auto-ajuste dos ganhos (janela de 10 min) ----
  { int8_t sg=(x>alvo+q.tolOsc)?1:(x<alvo-q.tolOsc)?-1:0;
    if(sg!=0){ if(c.sinalAnt!=0 && sg!=c.sinalAnt) c.osc++; c.sinalAnt=sg; }
    c.somaAbs+=fabsf(eReal); c.nAmostras++;
    if(agora-c.janIni>=600000UL){
      float media=c.nAmostras?c.somaAbs/c.nAmostras:0; float oe=c.esc;
      if(c.osc>=2){ float piso=temp?0.5f:0.35f; c.esc*=0.8f; if(c.esc<piso) c.esc=piso; }   // Fase 88: umidade pode ficar mais mansa (piso 0,35; o umidificador ficou travado em x0,50 oscilando 47<->55)
                                                  // oscilando em volta do alvo: mais manso
      else if(c.osc==0 && media>q.tolMed && eReal>0 && c.I<0.85f){ c.esc*=1.15f; if(c.esc>1.3f) c.esc=1.3f; }  // abaixo do alvo, sem cruzar e sem estar no limite: mais firme
      if(c.esc!=oe){ char m[100]; snprintf(m,sizeof(m),"%s (PI): ganhos x%.2f -> x%.2f (%s)",RELAY_NAMES[ch],oe,c.esc,c.esc<oe?"oscilando":"lento"); logEvento("Automacao",m,false); c.sujo=true; }
      c.osc=0; c.somaAbs=0; c.nAmostras=0; c.janIni=agora;
    } }
  // ---- no limite? (1 Hz): ligado >= 90% de 10 min com a leitura ainda longe do alvo = potencia insuficiente ----
  { float thr=temp?0.5f:5.0f;
    if(eReal>thr && !tampaA && !tampaR){
      c.satTot++; if(relayState[ch]) c.satOn++;
      if(c.satTot>=600){
        if(c.satOn>=540 && !c.satAv){
          c.satAv=true; char m[130];
          if(temp) snprintf(m,sizeof(m),"Aquecedor no limite: nao passa de %.1fC (alvo %.1fC) com %.0f W. Aumente a potencia (Config > Hardware) ou isole melhor a caixa",x,alvo,hwPtot());
          else     snprintf(m,sizeof(m),"Umidificador no limite: nao passa de %.0f%% (alvo %.0f%%) com %u ml/h. Aumente a vazao (Config > Hardware) ou vede a caixa",x,alvo,(unsigned)(hw.umMlH*hw.umQtd));
          logEvento("Automacao",m,true); pushEnfileirar(0,"ChocaIoT: potencia insuficiente",m);
        }
        c.satTot=0; c.satOn=0;
      }
    }else{ c.satTot=0; c.satOn=0; c.satAv=false; }
  }
  // ---- pulso em andamento: so corta cedo por seguranca (passou do alvo) ----
  if(p.fase==1){
    if(!relayState[ch]){ p.fase=0; return; }
    if(x>=alvo+q.corte || (temp && sl>=0.3f && tprev>=alvo+0.5f) || govBloqueia) pulsoEncerrar(ch,"acima do alvo");   // Fase 82c: tambem corta pela previsao completa
    return;
  }
  // ---- aquecimento / umidificacao direta longe do alvo ----
  bool semFans=false;
  if(q.fans){ int fanCh=-1; for(int k=0;k<AUTO_N;k++) if(regras[k].modo==AM_CIRCULAR){ fanCh=k; break; } semFans=(fanCh>=0 && !relayState[fanCh]); }   // nao aquece sem circular o ar
  if(relayState[ch]){   // ligado direto (nao e pulso)
    if(!c.dirIni) c.dirIni=agora;
    bool demais=(agora-autoOnDesde[ch])>q.dirMaxMs;
    if(e<=q.dirOff || x>=alvo || demais || semFans || govBloqueia || (temp && (tprev>=alvo-0.3f || tampaA))){   // Fase 82c: previsao completa; tampa aberta nao usa aquecimento direto
      autoAplicaPulso(ch,false,demais?"limite de tempo direto":semFans?"Fans desligadas":"perto do alvo: controle PI");
      pidFimDireto(c,temp,agora); p.ultFim=agora; c.perIni=0; c.acc=0;
    }else{
      if(agora-c.dirIni>(temp?40000UL:15000UL) && !tampaA && !tampaR){ float r=sl/60.0f; if(r>c.rPico) c.rPico=r; }   // taxa de subida (depois do atraso inicial)
    }
    return;
  }
  if(semFans) return;
  bool partidaIncerta=(temp && agora<300000UL && (alvo-x)<3.0f);   // Fase 92: nos 5 min apos ligar nao se sabe quanto calor ainda esta em transito (resistencia quente de antes do boot/gravacao): so liga direto se estiver bem frio
  if(e>q.dirOn && !govBloqueia && !partidaIncerta && !(temp && (tprev>=alvo-0.3f || tampaA))){ autoAplicaPulso(ch,true,temp?"aquecimento inicial":"umidificacao inicial"); p.sess=true; c.dirIni=agora; c.rPico=0; return; }
  // ---- ciclo PI ----
  if(c.perIni && agora-c.perIni<(unsigned long)(q.per*1000.0f)) return;
  c.perIni=agora;
  float kp=q.kp, ki=kp/q.ti, per=q.per;
  float D=kp*e+c.I; if(D<0) D=0; if(D>1.0f) D=1.0f;
  if(temp && tampaA && D>0.6f) D=0.6f;   // Fase 82c: com a tampa aberta o aquecedor nao passa de 60% (fechar a tampa depois de aquecer a 100% gerava overshoot)
  if(temp && tprev>=alvo+0.2f && sl>=0.3f){ D=0;   // so em subida rapida (>= 0,3 C/min): em regime a trava nao atua e nao cria oscilacao (simulado)
    c.acc=0; c.I-=ki*(tprev-alvo)*per; if(c.I<0) c.I=0; c.sujo=true; return; }   // Fase 82c: indo para cima do alvo: nao aquece (o integral nao pode manter o duty de regime)
  if(fabsf(e)<(temp?2.0f:8.0f) && !(D>=1.0f && e>0) && !tampaA){ float kiMul=(temp && fabsf(eReal)>0.4f && fabsf(sl)<0.12f)?3.0f:1.0f; c.I+=ki*e*per*kiMul; if(c.I<0) c.I=0;   // Fase 82g: com a temperatura parada fora do alvo (sem inclinacao) o integral corrige 3x mais rapido (simulado: x5 ja piora)
    if(c.I>1.0f) c.I=1.0f; c.sujo=true; }   // anti-windup
  if(x>=alvo+q.corte || govBloqueia){ c.acc=0; return; }   // Fase 82d: calor em transito ja cobre o que falta
  c.acc+=D*per;
  if(c.acc>=q.minOn){
    float ton=c.acc; if(ton>per) ton=per;
    if(D>=0.97f) ton=per*3.0f;   // saturado: pulso longo, sem a pausa de ~1 s a cada ciclo
    c.acc-=(ton>c.acc?c.acc:ton);
    p.durMs=(unsigned long)(ton*1000.0f); p.ini=agora; p.fase=1; p.cont++; p.sess=true; p.alvoP=alvo; p.cortado=false;
    c.onMs+=p.durMs;
    char m[48]; snprintf(m,sizeof(m),"PI: %.1fs (D=%.0f%% I=%.2f)",ton,D*100.0f,c.I);
    autoAplicaPulso(ch,true,m);
  }
}
static bool advUsaRele(int ch);
// Fase 106 -- o usuario (toque ou PWA) mexeu num canal. DESLIGAR = a automacao daquele canal fica em pausa por 2 h (manutencao) e qualquer pulso em andamento
// e abandonado; LIGAR = pausa de 5 min (como antes) e o canal passa a contar como sessao ligada. O rele ja foi comandado por quem chamou.
#define MANUAL_OFF_HOLD_MS 7200000UL
void manualHold(int ch,bool on){
  if(ch<0 || ch>=AUTO_N) return;
  unsigned long agora=millis();
  // Fase 109: o canal passa a ser do USUARIO: a automacao nao o considera "seu" (autoTinhaUltimo=false). Antes ela marcava true e, com a automacao
  // DESLIGADA, o bloco "automacao desativada" desligava o canal e republicava OFF na nuvem (CH8 manual so ficava ligado com a chave da automacao ligada).
  autoUltimo[ch]=on; autoTinhaUltimo[ch]=false; autoPrevOn[ch]=on;
  pulso[ch].fase=0; pulso[ch].serie=false; pulso[ch].sess=pulsoGerencia[ch]?on:false; pulso[ch].ultFim=agora;
  pid[ch].acc=0; pid[ch].perIni=0; if(pulsoGerencia[ch]) pulsoNuvemVal[ch]=on;
  autoPausaAte[ch]= on ? agora+300000UL : agora+MANUAL_OFF_HOLD_MS;
  // Fase 110: mexeu num canal que a AUTOMACAO controla (regra por rele diferente de Manual, ou citado por regra avancada) -> a chave da automacao desliga
  // (visivel na tela Atuadores e no PWA); religar a chave devolve o controle. Canal em Manual, bomba de agua e cooler nao mexem na chave.
  if(autoAtivo && ch!=2 && ch!=COOLER_CH && (regras[ch].modo!=AM_OFF || advUsaRele(ch))){ autoAtivo=false; autoManualPend=ch; }
  if(!on){ char m[88]; snprintf(m,sizeof(m),"%s desligado manualmente: automacao do canal em pausa por 2 h (ligue de novo para retomar antes)",RELAY_NAMES[ch]); logEvento("Automacao",m,true); }
}
// A nuvem so ve a SESSAO de pulsos (ligado enquanto houve pulso nos ultimos 60 s): poucas escritas, indicador do PWA coerente.
static void pulsoSyncNuvem(int ch){
  PulsoCtl& p=pulso[ch]; unsigned long agora=millis();
  if(p.sess && p.fase==0 && !relayState[ch] && agora-p.ultFim>=60000UL) p.sess=false;
  if(p.sess!=pulsoNuvemVal[ch]){ pulsoNuvemVal[ch]=p.sess; lastLocalTouchMs[ch]=agora; pushActuatorToFirestore(ch,p.sess); }
}

static void advTick(bool tOk,bool sobreTemp,int minDia);
// Relé citado (ENTAO/SENAO) por uma regra avancada ATIVA: a regra por rele dele fica de lado (senao o termostato padrao do CH1 ligava o aquecedor abaixo do alvo, contrariando a regra avancada)
static bool advUsaRele(int ch){
  for(int i=0;i<advN;i++) if(adv[i].ativa && (((adv[i].acaoA|adv[i].acaoI)>>(2*ch))&3)) return true;
  return false;
}
// ═══════════════════════════════════════════════════════════════
// Fase 83 -- BOMBA DE AR (CH4, 3,7 V) e AGUA DO UMIDIFICADOR.
// BOMBA DE AR: (1) o Cooler Sistema liga junto (a bomba esquenta e fica perto do ESP32) -- ver coolerPorBomba(); (2) no maximo 20 min seguidos
// ligada (esquenta); (3) se ligada >= 4 min com a umidade ACIMA do maximo e AINDA SUBINDO (ar de fora mais umido, ex.: chuva), desliga e nao
// deixa o modo "Desumidificar/ventilar" religar por 30 min -- ventilar so piora, o aparelho avisa em vez de insistir.
// AGUA DO UMIDIFICADOR (reservatorio interno ~150 ml, externo ~5 L; nao ha sensor de nivel): se o umidificador esta trabalhando (ligado >= 60 % dos
// ultimos 5 min), a umidade esta >= 3 pontos abaixo do alvo e NAO sobe (variacao compensada pela temperatura <= +0,6 por 3 avaliacoes seguidas ~ 6,5 min),
// conclui falta de agua. Sem a bomba de agua (padrao): ALERTA na hora (Status, log, push), desliga o umidificador (nao deixa seco) e faz uma SONDA de
// 150 s a cada 10 min: assim que a umidade voltar a responder (o usuario encheu o reservatorio), o alerta encerra sozinho. Com a bomba de agua
// (CH3, EM DESENVOLVIMENTO, desligada por padrao em Config > Hardware): ate 5 pulsos de 4 s, cada um seguido de 150 s de observacao com o umidificador
// ligado; se a umidade responder, "reservatorio reabastecido"; se nao, alerta de reservatorio externo/bomba/mangueira. Nada disto roda com a tampa aberta.
// ═══════════════════════════════════════════════════════════════
static void ambToast(const char* m);
static unsigned long ventInefAte=0;
static void bombaArTick(){
  static bool ant=false; static unsigned long onDesde=0; static float u0=NAN;
  unsigned long agora=millis(); bool on=relayState[3];
  if(on && !ant){ onDesde=agora; u0=ctlU; }
  if(!on && ant) bombaArOffMs=agora;
  ant=on;
  if(coolerPorBomba() && !relayState[COOLER_CH]) controlarCoolerPorHisterese();   // liga o cooler na hora (sem esperar o ciclo de 5 s)
  if(!on) return;
  unsigned long dur=agora-onDesde;
  if(dur>20UL*60000UL){
    autoAplica(3,false,"limite de 20 min seguidos: a bomba esquenta"); autoPausaAte[3]=agora+600000UL;
    logEvento("Alerta","Bomba de ar desligada: 20 min seguidos ligada (a bomba esquenta). Deixe esfriar.",true);
    pushEnfileirar(14,"ChocaIoT: bomba de ar desligada","20 min seguidos ligada: a bomba esquenta. Deixe esfriar.");
    return;
  }
  if(dur>=240000UL && !isnan(ctlU) && !isnan(u0) && tampaEst==0 && ctlU>UMID_IDEAL_MAX && (ctlU-u0)>=1.0f && humSlopeMin>0.2f){
    char m[130]; snprintf(m,sizeof(m),"Bomba de ar desligada: ligada ha %lu min e a umidade subiu de %.0f%% para %.0f%%. O ar de fora deve estar mais umido (chuva): ventilar nao ajuda agora.",dur/60000UL,u0,ctlU);
    autoAplica(3,false,"ventilar nao ajuda: umidade continua subindo"); autoPausaAte[3]=agora+600000UL; ventInefAte=agora+30UL*60000UL;
    logEvento("Alerta",m,true); pushEnfileirar(14,"ChocaIoT: ventilacao nao ajuda",m);
  }
}
struct AguaCtl{ uint8_t fase, sub, suspeitas, pulsos, nBuf; bool resetPend, teste; unsigned long ult10, ultAv, faseIni, proxSonda, ultPush, tBomba, ultSeq, okDesde, bombaFimMs; float u0, t0, frac, ganho; float uBuf[30], tBuf[30]; uint8_t onBuf[30]; };
static AguaCtl ag;   // fase: 0 vigia · 1 pulsos da bomba · 2 ALERTA (umidificador desligado, sonda a cada 10 min) · 3 sonda
#define AGUA_OBS_MS     150000UL
// Fase 85 -- BOMBA DE AGUA (CH3 = VARIALL VA-BA01, AC 110-220 V, 3 W, 180 L/h maximo, Hmax 0,35 m, regulagem no minimo). Forte demais para um reservatorio de 150 ml:
// camadas contra transbordar -- (1) DESATIVADA ate o usuario informar a vazao MEDIDA (Hardware > Vazao da bomba, ml/s); (2) pulsos de ~15 ml (tempo = 15 ml / vazao, 0,3-8 s);
// (3) no maximo 4 pulsos por sequencia (60 ml), cada um seguido de 150 s de observacao (para ao primeiro sinal de resposta); (4) 1 sequencia por hora e 120 ml por dia
// (persistidos em /bomba.cfg, sobrevivem a reboot); (5) so apos ~6,5 min de umidificador ligado SEM resposta e com a tampa fechada; (6) vigia de 8 s no rele CH3 por QUALQUER origem
// (aguaBombaFimRapido, precisao de ms no loop()). O fim do pulso tambem e feito la (nunca depende do tick de 1 s).
#define AGUA_ML_PULSO         15
#define AGUA_PULSOS_MAX        4
#define AGUA_ML_DIA          120
#define AGUA_SEQ_MIN_S      3600UL
#define BOMBA_MAX_ON_MS     8000UL
static unsigned long bUltSeqEpoch=0, bDia=0; static unsigned bMlHoje=0;
static void bombaCfgCarregar(){
  static bool f=false; if(f) return; f=true;
  File fl=LittleFS.open("/bomba.cfg","r"); if(!fl) return;
  String l=fl.readStringUntil('\n'); fl.close();
  unsigned long a=0,c=0; unsigned b=0; if(sscanf(l.c_str(),"%lu,%u,%lu",&a,&b,&c)==3){ bUltSeqEpoch=a; bMlHoje=b; bDia=c; }
}
static void bombaCfgSalvar(){ File fl=LittleFS.open("/bomba.cfg","w"); if(!fl) return; fl.printf("%lu,%u,%lu\n",bUltSeqEpoch,bMlHoje,bDia); fl.close(); }
static unsigned long bombaPulsoMs(){ return hw.bombaMlS10?(unsigned long)hw.bombaPrimeS10*100UL+(unsigned long)AGUA_ML_PULSO*10000UL/hw.bombaMlS10:0UL; }   // ms para ~15 ml = tempo de ENCHER a mangueira (sem valvula ela esvazia entre pulsos) + 15 ml / vazao (Fase 88)
void aguaBombaFimRapido(){   // chamado a cada volta do loop(): fim do pulso com precisao de ms + vigia de 8 s no CH3 (qualquer origem: automatico, PWA ou toque)
  static bool ant=false; static unsigned long onMs=0; unsigned long agora=millis(); bool o=relayState[2];
  if(o && !ant) onMs=agora;
  ant=o;
  if(bombaNuvemLimpar && fbStarted && fbApp.ready()){ static unsigned long ultL=0; if(agora-ultL>30000UL){ ultL=agora; bombaNuvemLimpar=false; pushActuatorToFirestore(2,false); Serial.println("[Bomba] CH3 estava ligado na nuvem -- reescrito como desligado (a nuvem nunca liga a bomba)."); } }   // Fase 87
  { static uint8_t snap=0; uint8_t m=0; for(int i=0;i<8;i++) if(relayState[i]) m|=(1<<i); if(m!=snap){ snap=m; if(appState==S_ATUADORES) drawAtuadoresScreen(); } }   // Fase 87: a tela Atuadores acompanha o rele (a chave da bomba voltava a "off" so apos novo toque)
  if(ag.bombaFimMs && (long)(agora-ag.bombaFimMs)>=0){ ag.bombaFimMs=0; if(relayState[2]) autoAplica(2,false,"fim do pulso"); return; }
  if(o && agora-onMs>BOMBA_MAX_ON_MS){
    ag.bombaFimMs=0; autoAplica(2,false,"limite de 8 s seguidos: bomba de agua"); autoPausaAte[2]=agora+300000UL;
    logEvento("Alerta","Bomba de agua desligada: 8 s seguidos ligada (limite de seguranca contra transbordar).",true);
    pushEnfileirar(14,"ChocaIoT: bomba de agua desligada","8 s seguidos ligada: limite de seguranca contra transbordar.");
  }
}
static inline float aguaGanho(float u0,float t0){ return (ctlU-u0)+3.0f*(ctlT-t0); }   // subida de umidade compensada pela temperatura (UR cai ~3 pontos a cada +1 C)
static void aguaForcaUm(bool on,const char* m){
  if(on){ pulso[1].sess=true; if(!relayState[1]) autoAplicaPulso(1,true,m); }
  else{ pulso[1].sess=true; pulso[1].ultFim=millis(); if(relayState[1]) autoAplicaPulso(1,false,m); }
}
static void aguaAlerta(uint8_t modo){   // 0 sem bomba · 1 apos os pulsos · 2 bomba nao calibrada · 3 limite de tempo/volume da bomba · 4 vazao fora da faixa dosavel
  unsigned long agora=millis();
  ag.fase=2; ag.sub=0; aguaEst=2; aguaPausa=true; ag.proxSonda=agora+(ag.teste?20000UL:300000UL); ag.ultPush=agora; ag.okDesde=0;
  if(modo==1) snprintf(aguaTxt,sizeof(aguaTxt),"Sem agua mesmo apos %u pulsos da bomba. Encha o galao externo (5 L) e confira bomba, mangueira e umidificador.",(unsigned)ag.pulsos);
  else if(modo==2) snprintf(aguaTxt,sizeof(aguaTxt),"Sem agua no umidificador. A bomba esta SEM CALIBRAR (Hardware > Vazao da bomba): encha o reservatorio interno (150 ml) a mao.");
  else if(modo==3) snprintf(aguaTxt,sizeof(aguaTxt),"Sem agua no umidificador e a bomba atingiu o limite (1 sequencia/h, 120 ml/dia ou sem hora). Encha o reservatorio interno (150 ml).");
  else if(modo==4) snprintf(aguaTxt,sizeof(aguaTxt),"Sem agua. A vazao informada da bomba nao permite pulsos de ~15 ml (0,3 a 8 s): confira a medida. Encha o reservatorio (150 ml) a mao.");
  else snprintf(aguaTxt,sizeof(aguaTxt),"Sem agua no umidificador? Ligado %.0f%% do tempo e a umidade nao sobe (%+.1f pts). Encha o reservatorio interno (150 ml).",ag.frac*100.0f,ag.ganho);
  aguaForcaUm(false,"sem agua: umidificador desligado");
  logEvento("Alerta",aguaTxt,true);
  pushEnfileirar(15,"ChocaIoT: sem agua no umidificador",aguaTxt);
  ambToast("Sem agua no umidificador");
}
static void aguaSucesso(const char* como){
  char m[120]; snprintf(m,sizeof(m),"Agua detectada: %s",como); logEvento("Automacao",m,true);
  if(ag.fase==2||ag.fase==3) pushEnfileirar(15,"ChocaIoT: agua OK","O umidificador voltou a responder.");
  ag.fase=0; ag.sub=0; ag.nBuf=0; ag.suspeitas=0; ag.okDesde=0; ag.teste=false; aguaEst=0; aguaPausa=false;   // o PI assume de novo (o umidificador segue como esta ate ele decidir)
  pid[1].acc=0; pid[1].perIni=0;
}
static void aguaAborta(bool limpaRele){
  ag.bombaFimMs=0;
  if(limpaRele){ if(ag.fase==1||ag.fase==3) aguaForcaUm(false,"teste de agua encerrado"); if(relayState[2] && ag.fase==1) autoAplica(2,false,"teste de agua encerrado"); }
  ag.fase=0; ag.sub=0; ag.suspeitas=0; ag.nBuf=0; ag.pulsos=0; ag.okDesde=0; aguaEst=0; aguaPausa=false; ag.resetPend=false; pid[1].acc=0; pid[1].perIni=0;
}
void aguaSimular(){   // depuracao (serial W): finge que a deteccao confirmou; testa alerta -> sonda -> sucesso (o umidificador tem agua: a sonda deve "achar" agua)
  ag.teste=true; ag.ultSeq=0; ag.frac=1.0f; ag.ganho=0.0f; Serial.printf("[AGUA] simulando suspeita de falta de agua (bomba %s)\n",hw.bomba?"instalada":"nao instalada");
  if(aguaEst==0 && ag.fase==0){ ag.fase=0; ag.suspeitas=3; ag.nBuf=30; ag.ultAv=0; }
}
void aguaAlternaBomba(){ hw.bomba=hw.bomba?0:1; hwSujo=true; hwUltMs=millis(); Serial.printf("[TESTE] bomba de agua (CH3) %s\n",hw.bomba?"INSTALADA":"nao instalada"); }
static void aguaTick(){
  unsigned long agora=millis();
  bool termoAtivo=fbCicloAtivo || ambManterOn();
  bool tOk=sht31OK && !isnan(ctlT) && !isnan(ctlU) && lastSht31OkMs && (agora-lastSht31OkMs)<120000UL;
  bool apto=autoAtivo && termoAtivo && regras[1].modo==AM_UMIDIFICAR && !advUsaRele(1) && hw.umQtd>0 && tOk && agora>240000UL && agora>=autoPausaAte[1];
  if(ag.resetPend || !apto){ if(ag.fase!=0 || aguaEst!=0 || aguaPausa || ag.nBuf) aguaAborta(true); ag.resetPend=false; return; }
  float alvo=autoAlvoUmid(regras[1]);
  if(tampaEst!=0){   // tampa aberta/recuperando: nada de teste; o alerta (se houver) fica, mas a sonda so volta depois
    if(ag.fase==1||ag.fase==3){ aguaAborta(true); }
    ag.nBuf=0; ag.suspeitas=0;
    if(ag.fase==2){ ag.proxSonda=agora+60000UL; aguaForcaUm(false,"tampa aberta"); }
    return;
  }
  if(agora-ag.ult10>=10000UL){   // amostra a cada 10 s: janela de 5 min
    ag.ult10=agora;
    if(ag.nBuf==30){ for(int i=1;i<30;i++){ ag.uBuf[i-1]=ag.uBuf[i]; ag.tBuf[i-1]=ag.tBuf[i]; ag.onBuf[i-1]=ag.onBuf[i]; } ag.nBuf=29; }
    ag.uBuf[ag.nBuf]=ctlU; ag.tBuf[ag.nBuf]=ctlT; ag.onBuf[ag.nBuf]=relayState[1]?1:0; ag.nBuf++;
  }
  if(ag.fase==0){
    aguaEst=0; aguaPausa=false;
    if(ag.nBuf<30) return;
    if(agora-ag.ultAv<30000UL) return; ag.ultAv=agora;
    int on=0; for(int i=0;i<30;i++) on+=ag.onBuf[i];
    if(!ag.teste){ ag.frac=on/30.0f; ag.ganho=(ag.uBuf[29]-ag.uBuf[0])+3.0f*(ag.tBuf[29]-ag.tBuf[0]); }
    bool pede=(alvo-ctlU>=3.0f) && ag.frac>=0.6f;
    if(ag.teste) pede=true;
    if(pede && ag.ganho<=0.6f) ag.suspeitas++; else if(!ag.teste) ag.suspeitas=0;
    if(!ag.teste) Serial.printf("[AGUA] ligado %.0f%% ganho %+.2f falta %.1f suspeitas %u\n",ag.frac*100.0f,ag.ganho,alvo-ctlU,(unsigned)ag.suspeitas);
    if(ag.suspeitas>=3){
      ag.suspeitas=0;
      if(!hw.bomba) aguaAlerta(0);
      else{   // bomba de agua (EM DESENVOLVIMENTO) -- todas as travas da Fase 85 antes do primeiro pulso
        bombaCfgCarregar();
        time_t tn=time(nullptr); bool horaOk2=tn>=(time_t)EPOCH_2024_01_01; unsigned long pms=bombaPulsoMs();
        if(horaOk2 && bDia!=(unsigned long)(tn/86400)){ bDia=(unsigned long)(tn/86400); bMlHoje=0; bombaCfgSalvar(); }
        if(hw.bombaMlS10==0) aguaAlerta(2);
        else if(pms<300UL || pms>BOMBA_MAX_ON_MS) aguaAlerta(4);
        else if(!horaOk2 || (!ag.teste && (unsigned long)tn-bUltSeqEpoch<AGUA_SEQ_MIN_S) || bMlHoje+AGUA_ML_PULSO>AGUA_ML_DIA) aguaAlerta(3);
        else{
          ag.fase=1; ag.sub=0; ag.pulsos=0; ag.faseIni=agora; ag.ultSeq=agora; aguaEst=1; aguaPausa=true; bUltSeqEpoch=(unsigned long)tn; bombaCfgSalvar();
          char m[120]; snprintf(m,sizeof(m),"Umidade nao sobe (ligado %.0f%%, %+.1f pts): testando a bomba de agua (pulsos de ~%d ml)",ag.frac*100.0f,ag.ganho,(int)AGUA_ML_PULSO); logEvento("Automacao",m,true);
        }
      }
    }
    return;
  }
  aguaPausa=true;
  if(ag.fase==1){   // pulsos da bomba de agua (CH3)
    aguaForcaUm(true,"teste de agua");
    if(ag.sub==0){
      unsigned long pms=bombaPulsoMs();
      if(pms<300UL || pms>BOMBA_MAX_ON_MS){ aguaAlerta(4); return; }
      if(bMlHoje+AGUA_ML_PULSO>AGUA_ML_DIA){ aguaAlerta(3); return; }
      ag.pulsos++; bMlHoje+=AGUA_ML_PULSO; bombaCfgSalvar();   // contabiliza e grava ANTES de ligar (um travamento no meio nao zera o limite)
      char m[72]; snprintf(m,sizeof(m),"bomba de agua: pulso %u de %u (~%d ml, %lu ms)",(unsigned)ag.pulsos,(unsigned)AGUA_PULSOS_MAX,(int)AGUA_ML_PULSO,pms);
      ag.bombaFimMs=agora+pms; autoAplica(2,true,m); ag.tBomba=agora; ag.sub=1;   // o desligamento e feito por aguaBombaFimRapido() (ms)
    }
    else if(ag.sub==1){ if(!relayState[2]){ ag.bombaFimMs=0; ag.u0=ctlU; ag.t0=ctlT; ag.faseIni=agora; ag.sub=2; } }
    else{
      float g=aguaGanho(ag.u0,ag.t0);
      if(g>=0.8f){ char m[100]; snprintf(m,sizeof(m),"reservatorio reabastecido (%u pulso(s), ~%d ml, umidade %+.1f pts)",(unsigned)ag.pulsos,(int)(ag.pulsos*AGUA_ML_PULSO),g); aguaSucesso(m); }
      else if(agora-ag.faseIni>=AGUA_OBS_MS){ if(ag.pulsos<AGUA_PULSOS_MAX) ag.sub=0; else aguaAlerta(1); }
    }
    return;
  }
  if(ag.fase==3){   // sonda: umidificador ligado direto por 150 s para ver se a umidade responde
    aguaForcaUm(true,"sonda de agua");
    float g=aguaGanho(ag.u0,ag.t0);
    if(g>=0.8f){ char m[80]; snprintf(m,sizeof(m),"o umidificador voltou a responder (%+.1f pts)",g); aguaSucesso(m); }
    else if(agora-ag.faseIni>=AGUA_OBS_MS){ aguaForcaUm(false,"sonda sem resposta"); ag.fase=2; ag.proxSonda=agora+(ag.teste?30000UL:600000UL); Serial.println("[AGUA] sonda sem resposta: continua sem agua"); }
    return;
  }
  // fase 2: alerta -- umidificador desligado; sonda a cada 10 min; re-avisa a cada 60 min
  aguaEst=2;
  if(relayState[1]) aguaForcaUm(false,"sem agua: umidificador desligado");
  if(ctlU>=alvo-1.0f){   // a umidade chegou ao alvo por outro motivo: encerra (sem confirmar a agua)
    if(!ag.okDesde) ag.okDesde=agora;
    else if(agora-ag.okDesde>=120000UL){ logEvento("Automacao","Umidade voltou ao alvo: alerta de agua encerrado (agua nao confirmada)",true); aguaAborta(false); return; }
  }else ag.okDesde=0;
  if(agora-ag.ultPush>=3600000UL){ ag.ultPush=agora; pushEnfileirar(15,"ChocaIoT: ainda sem agua no umidificador",aguaTxt); }
  if(agora>=ag.proxSonda){ ag.fase=3; ag.u0=ctlU; ag.t0=ctlT; ag.faseIni=agora; aguaForcaUm(true,"sonda de agua (150 s)"); Serial.println("[AGUA] sonda: umidificador ligado por 150 s"); }
}
void automacaoTick(){
  { static bool primeiro=true; if(primeiro && millis()>4000UL){ primeiro=false; aplicarLimitesDoTipo(); } }   // Fase 79: faixa do tipo ja no boot (modo manter)
  if(tampaSyncPend){ tampaSyncPend=0; autoPersistir(); }   // Fase 82: devolve "L,0" a nuvem (comando consumido)
  if(manterSyncPend){ manterSyncPend=false; aplicarLimitesDoTipo(); autoPersistir(); }   // Fase 79 (fora de callback)
  if(hwSujo && millis()-hwUltMs>3000UL){ hwSujo=false; autoPersistir(); char hm[110]; snprintf(hm,sizeof(hm),"Hardware: caixa %u L, aquecedor %ux%uW, umidificador %ux%uml/h, bomba de agua %s",(unsigned)hw.volL,(unsigned)hw.aqQtd,(unsigned)hw.aqW,(unsigned)hw.umQtd,(unsigned)hw.umMlH,hw.bomba?"sim (dev)":"nao"); logEvento("Automacao",hm,true); }   // Fase 78
  if(autoManualPend>=0){   // Fase 110: o usuario assumiu um canal automatizado -> chave da automacao desligada (persiste/loga aqui, fora de callback)
    int c=autoManualPend; autoManualPend=-1; autoPersistir();
    char m[120]; snprintf(m,sizeof(m),"Automacao DESLIGADA: %s alterado manualmente (ligue a chave em Atuadores para retomar)",RELAY_NAMES[c]); logEvento("Automacao",m,true);
    if(appState==S_ATUADORES) drawAtuadoresScreen();
  }
  { static unsigned long offDesde=0;   // Fase 111: rede de seguranca -- automacao desligada com um ciclo ativo/pausado volta sozinha apos N min
    if(!autoAtivo && fbCicloAtivo && autoRetomaMin>0){
      unsigned long t=millis(); if(!offDesde) offDesde=t;
      else if(t-offDesde>=(unsigned long)autoRetomaMin*60000UL){
        offDesde=0; autoAtivo=true; autoPersistir();
        char m[100]; snprintf(m,sizeof(m),"Automacao religada sozinha apos %u min desligada (ciclo em andamento)",(unsigned)autoRetomaMin); logEvento("Automacao",m,true);
        if(appState==S_ATUADORES) drawAtuadoresScreen(); else if(appState==S_AUTO) drawAuto();
      }
    } else offDesde=0; }
  { static bool atvAnt=false; if(autoAtivo && !atvAnt){ for(int c=0;c<AUTO_N;c++) autoPausaAte[c]=0; } atvAnt=autoAtivo; }   // religou a chave: pausas manuais caem, a automacao reassume
  bombaArTick();   // Fase 83: bomba de ar (cooler junto, limite de tempo, ventilacao inutil) -- vale mesmo com a automacao desligada
  bool ativo=autoAtivo && (!autoSoCiclo || fbCicloAtivo || ambManterOn());   // Fase 82: PAUSAR o ciclo so congela a contagem de dias -- os ovos continuam la dentro, o ambiente segue controlado
  unsigned long agora=millis();
  if(!ativo){
    ag.resetPend=true; aguaEst=0; aguaPausa=false;   // Fase 83
    for(int c=0;c<AUTO_N;c++){ pulso[c].fase=0; pulso[c].sess=false; pulso[c].serie=false; pulsoGerencia[c]=false; pid[c].acc=0; pid[c].perIni=0; }   // abandona pulsos em andamento
    if(autoEstavaAtivo){
      for(int ch=0;ch<AUTO_N;ch++) if(ch!=COOLER_CH && autoTinhaUltimo[ch] && relayState[ch] && true) autoAplica(ch,false,"automacao desativada");
      memset(autoTinhaUltimo,0,sizeof(autoTinhaUltimo)); memset(advLatch,0,sizeof(advLatch)); memset(advInit,0,sizeof(advInit));
      autoEstavaAtivo=false;
    }
    for(int c=0;c<AUTO_N;c++) if(pulsoNuvemVal[c]){ pulsoNuvemVal[c]=false; lastLocalTouchMs[c]=agora; pushActuatorToFirestore(c,false); }   // Fase 80
    return;
  }
  autoEstavaAtivo=true;
  bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (agora-lastSht31OkMs)<120000UL;
  bool sobreTemp=tOk && temperaturaAtual>=TEMP_IDEAL_MAX+0.5f;
  if(!tOk && !autoSensorAvisado){ autoSensorAvisado=true; logEvento("Automacao","Sensor SHT31 sem leitura: saidas de termostato/umidostato desligadas",true); pushEnfileirar(0,"ChocaIoT: sensor sem leitura","Automacao desligou aquecedor/umidificador por seguranca."); }
  if(tOk) autoSensorAvisado=false;
  if(sobreTemp && !autoSobretempAvisada && (fbCicloAtivo||ambManterOn())){ autoSobretempAvisada=true; char m[64]; snprintf(m,sizeof(m),"Sobretemperatura %.1fC: aquecedor desligado",temperaturaAtual); logEvento("Automacao",m,true); pushEnfileirar(0,"ChocaIoT: SOBRETEMPERATURA",m); }
  if(!sobreTemp && tOk && temperaturaAtual<TEMP_IDEAL_MAX+0.2f) autoSobretempAvisada=false;

  {   // Fase 72: leitura rapida (1 Hz) do SHT31 so para o controle por pulsos
    float ft=sht31.readTemperature(), fh=sht31.readHumidity();
    if(!isnan(ft)&&!isnan(fh)&&ft>-20&&ft<80&&fh>=0&&fh<=100){ ctlT=ft; ctlU=fh; }
    else if(!isnan(temperaturaAtual)&&lastSht31OkMs&&(agora-lastSht31OkMs)<10000UL){ ctlT=temperaturaAtual; ctlU=umidadeAtual; }
  }
  tempSlopeAtualizar(); humSlopeAtualizar();
  aguaTick();   // Fase 83
  { static unsigned long ultCtl=0;   // Fase 75 -- diagnostico do controle a cada 30 s (so serial)
    if(agora-ultCtl>=30000UL){ ultCtl=agora;
      Serial.printf("[CTL] T=%.2f U=%.1f incl=%.2fC/min | CH1 f%u k=%.3f lag=%.0fs agr=%.2f fails=%u serie=%d on=%.1fs rel=%d | CH2 f%u k=%.3f agr=%.2f rel=%d | Fans=%d\n",
        ctlT,ctlU,tempSlopeMin,pulso[0].fase,pulso[0].k,pulso[0].lagS,pulso[0].agr,pulso[0].fails,pulso[0].serie,pulso[0].onAcum,relayState[0],
        pulso[1].fase,pulso[1].k,pulso[1].agr,relayState[1],relayState[7]);
      Serial.printf("[PI] T: I=%.3f esc=%.2f ciclo=%.0fs rMeas=%.4f osc=%u sat=%u/%u | U: I=%.3f esc=%.2f rMeas=%.3f alvoT=%.1f alvoU=%.0f\n",pid[0].I,pid[0].esc,pid[0].perAt,pid[0].rMeas,(unsigned)pid[0].osc,(unsigned)pid[0].satOn,(unsigned)pid[0].satTot,pid[1].I,pid[1].esc,pid[1].rMeas,pid[0].alvoRef,pid[1].alvoRef); } }
  bool termoAtivo=fbCicloAtivo || ambManterOn();   // Fase 80/82 (ciclo pausado continua controlando o ambiente)
  time_t now=time(nullptr); bool horaOk=now>=(time_t)1704067200UL;
  int minDia=rtcOK?(rtc.now().hour()*60+rtc.now().minute()):-1;
  for(int ch=0;ch<AUTO_N;ch++){
    if(ch==COOLER_CH) continue;
    RegraAuto& r=regras[ch];
    { bool termo=(r.modo==AM_AQUECER||r.modo==AM_RESFRIAR||r.modo==AM_UMIDIFICAR||r.modo==AM_DESUMIDIFICAR);
      if(termo && !termoAtivo){   // Fase 80: Status em OFF = termostatos desligados, mesmo com "so com ciclo" desmarcado
        if(relayState[ch] && autoUltimo[ch]) autoAplica(ch,false,"sem ciclo e sem manutencao do ambiente");
        pulso[ch].fase=0; pulso[ch].sess=false; pulsoGerencia[ch]=false; pid[ch].acc=0; pid[ch].perIni=0; autoPrevOn[ch]=relayState[ch];
        if(pulsoNuvemVal[ch]){ pulsoNuvemVal[ch]=false; lastLocalTouchMs[ch]=agora; pushActuatorToFirestore(ch,false); }
        continue;
      } }
    pulsoGerencia[ch]=(r.modo==AM_AQUECER||r.modo==AM_UMIDIFICAR) && !advUsaRele(ch);
    if(pulsoGerencia[ch]) pulsoSyncNuvem(ch); else if(pulso[ch].sess){ pulso[ch].sess=false; pulso[ch].fase=0; }
    if(r.modo==AM_OFF && r.autoOffMin==0) { autoPrevOn[ch]=relayState[ch]; continue; }
    if(relayState[ch] && !autoPrevOn[ch]) autoOnDesde[ch]=agora;   // ligou (por qualquer origem)
    autoPrevOn[ch]=relayState[ch];
    if(autoTinhaUltimo[ch] && relayState[ch]!=autoUltimo[ch]){ autoPausaAte[ch]=agora+300000UL; autoUltimo[ch]=relayState[ch]; }   // alguem mexeu: pausa 5 min

    bool sensorT=(r.modo==AM_AQUECER||r.modo==AM_RESFRIAR), sensorH=(r.modo==AM_UMIDIFICAR||r.modo==AM_DESUMIDIFICAR);
    // seguranca (vale mesmo pausado)
    if(r.modo==AM_AQUECER && sobreTemp && relayState[ch]){ autoAplica(ch,false,"sobretemperatura"); continue; }
    if((sensorT||sensorH) && !tOk){ if(relayState[ch]) autoAplica(ch,false,"sensor sem leitura"); continue; }
    // desliga sozinho
    if(r.autoOffMin>0 && relayState[ch] && agora-autoOnDesde[ch]>(unsigned long)r.autoOffMin*60000UL){
      char m[40]; snprintf(m,sizeof(m),"limite de %u min",(unsigned)r.autoOffMin); autoAplica(ch,false,m); autoPausaAte[ch]=agora+60000UL; continue;
    }
    if(agora<autoPausaAte[ch] || r.modo==AM_OFF || advUsaRele(ch)) continue;

    if(r.modo==AM_AQUECER||r.modo==AM_UMIDIFICAR){   // Fase 71: controle por pulsos adaptativo
      bool tp=(r.modo==AM_AQUECER);
      if(!tp && aguaPausa) continue;   // Fase 83: teste/alerta de agua comanda o umidificador
      if(isnan(ctlT)||isnan(ctlU)) continue;
      autoPidTick(ch,tp,tp?ctlT:ctlU,tp?autoAlvoTemp(r):autoAlvoUmid(r),r.hist10/10.0f);   // Fase 76/78: aquecedor e umidificador = PI de ciclo fixo
      continue;
    }
    int want=-1; const char* motivo="";
    float hist=r.hist10/10.0f;
    if(r.modo==AM_AQUECER){
      float a=autoAlvoTemp(r);
      float prev=temperaturaAtual;   // (ramo antigo, nao usado: AM_AQUECER agora vai por autoPulsoTick)
      int fanCh=-1; for(int k=0;k<AUTO_N;k++) if(regras[k].modo==AM_CIRCULAR){ fanCh=k; break; }
      if(temperaturaAtual>=a){ want=0; motivo="alvo atingido"; }
      else if(prev>=a){ want=0; motivo="subindo rapido: desliga antes do alvo"; }
      else if(temperaturaAtual<a-hist){
        if(fanCh>=0 && !relayState[fanCh]){ want=0; motivo="aguardando as Fans (circulacao do ar)"; }   // nao aquece sem circular o ar
        else { want=1; motivo="abaixo do alvo"; }
      }
    }
    else if(r.modo==AM_CIRCULAR){ want=fansTodosZero()?0:1; motivo=want?"circulacao do ar":"todos os fans em 0%"; }   // Fase 111
    else if(r.modo==AM_RESFRIAR){ float a=autoAlvoTemp(r); if(temperaturaAtual>a+hist) want=1; else if(temperaturaAtual<=a) want=0; motivo=want?"acima do alvo":"alvo atingido"; }
    else if(r.modo==AM_UMIDIFICAR){ float a=autoAlvoUmid(r); if(umidadeAtual<a-hist) want=1; else if(umidadeAtual>=a) want=0; motivo=want?"umidade baixa":"umidade ok"; }
    else if(r.modo==AM_DESUMIDIFICAR){ float a=autoAlvoUmid(r); if(agora<ventInefAte){ want=0; motivo="ar de fora mais umido"; } else { if(umidadeAtual>a+hist) want=1; else if(umidadeAtual<=a) want=0; motivo=want?"umidade alta":"umidade ok"; } }   // Fase 83
    else if(r.modo==AM_PERIODICO){ if(!horaOk) continue; unsigned long cada=(unsigned long)(r.cadaMin<1?1:r.cadaMin)*60UL; want=((unsigned long)now%cada)<r.onSeg?1:0; motivo="ciclo periodico"; }
    else if(r.modo==AM_VIRAR){   // motor de viragem: N pulsos por dia (do tipo do ciclo), a partir da hora da 1a viragem, ate o dia "virar ate"
      const TipoOvo* t=cicloTipoAtualPtr(); if(!t||!rtcOK||!horaOk){ want=0; motivo="sem ciclo"; }
      else{
        DateTime dt=rtc.now(); long sd=dt.hour()*3600L+dt.minute()*60L+dt.second();
        int n=t->viraPorDia<1?1:t->viraPorDia; long inter=86400L/n; bool pulso=false;
        if(calcularDiaCicloAtual()<=t->viraAte) for(int k=0;k<n;k++){ long ini=(t->viraHH*3600L+t->viraMM*60L+k*inter)%86400L; long d=(sd-ini+86400L)%86400L; if(d<(long)r.onSeg){ pulso=true; break; } }
        want=pulso?1:0; motivo="viragem";
      }
    }
    else if(r.modo==AM_HORARIO){
      if(minDia<0) continue;
      bool dentro=(r.hIni<=r.hFim)?(minDia>=r.hIni&&minDia<r.hFim):(minDia>=r.hIni||minDia<r.hFim);
      want=dentro?1:0; motivo="janela de horario";
    }
    if(want<0 || want==(relayState[ch]?1:0)) continue;
    unsigned long dwell=(r.modo==AM_PERIODICO||r.modo==AM_HORARIO||r.modo==AM_VIRAR)?3000UL:30000UL;   // termostato: nao chaveia mais que 1x/30s (protege rele/carga)
    if(agora-autoUltToggleMs[ch]<dwell) continue;
    autoAplica(ch,want==1,motivo);
  }
  advTick(tOk,sobreTemp,minDia);   // Fase 57: regras avancadas (depois das por rele: se conflitarem, as avancadas valem)
}

// ═══════════════════════════════════════════════════════════════
// Fase 69 -- "ambiente pronto" antes de colocar os ovos + automacao ligada junto com o ciclo.
// Ciclo NOVO (inicio nos ultimos 12 h, nunca visto por este aparelho): (1) se a automacao estiver desligada,
// liga junto (so com ciclo em andamento) e coloca aquecedor/umidificador em modo automatico se estavam em
// Manual; (2) avisa "NAO coloque os ovos ainda"; (3) quando temperatura E umidade ficam dentro da faixa do tipo
// por 10 min seguidos, avisa "ambiente pronto". Se em 15 min/1 h nao estabilizar, avisa o provavel motivo.
// Lote antigo/retroativo (ovos ja dentro) nao passa por isso. Estado em /amb.cfg ("epochInicio|pronto").
// ═══════════════════════════════════════════════════════════════
#define AMB_CFG_PATH "/amb.cfg"
#define AMB_RECENTE_S  (12L*3600L)
static long ambEpoch=0; static bool ambPronto=false, ambCarregado=false, ambOkAtivo=false;
static unsigned long ambOkDesde=0, ambForaDesde=0; static uint8_t ambAvisos=0;
static void ambSalvar(){ File f=LittleFS.open(AMB_CFG_PATH,"w"); if(!f) return; f.printf("%ld|%d\n",ambEpoch,ambPronto?1:0); f.close(); }
static void ambCarregar(){
  File f=LittleFS.open(AMB_CFG_PATH,"r"); if(!f) return;
  String l=f.readStringUntil('\n'); f.close(); int p=l.indexOf('|');
  if(p>0){ ambEpoch=l.substring(0,p).toInt(); ambPronto=(l.substring(p+1).toInt()==1); }
}
// Fase 70 -- migracao UNICA da configuracao ja salva (os novos padroes so valem em instalacao nova): Fans (CH8)
// Manual -> Circular e umidificador com a histerese antiga de 3% -> 1%. Roda quando ha nuvem pra sincronizar (senao o
// poll devolveria a config antiga). Marcador em /mig70.flag.
static void migracao70(){
  if(!fbStarted || !fbApp.ready() || !cicloPollRecebido) return;
  static bool feito=false; if(feito) return; feito=true;
  if(LittleFS.exists("/mig70.flag")) return;
  bool mudou=false;
  if(regras[7].modo==AM_OFF){ regras[7].modo=AM_CIRCULAR; mudou=true; }
  if(regras[1].modo==AM_UMIDIFICAR && !regras[1].alvoCustom && regras[1].hist10==30){ regras[1].hist10=10; mudou=true; }
  if(mudou){ autoPersistir(); logEvento("Automacao","Config atualizada: Fans circulam, umidificador com histerese de 1%",true); }
  File f=LittleFS.open("/mig70.flag","w"); if(f){ f.print("1"); f.close(); }
}
// ═══════════════════════════════════════════════════════════════
// Fase 80 -- DIAGNOSTICO do ambiente. Se em 10 min (desde o inicio do preparo/manutencao, ou desde a ultima vez na faixa) a temperatura/umidade
// nao estao na faixa, o card Status mostra a causa MAIS PROVAVEL (etiqueta curta) e a janela do Status / PWA / log / push o texto completo.
// Ordem: sensor -> automacao -> regra do rele em Manual -> Fans -> queda brusca (tampa aberta) -> temperatura (alta · hardware informado errado ·
// aquecedor no limite · caindo com o aquecedor ligado) -> umidade (sem umidificador · no limite · sem agua · alta) -> ainda ajustando.
// ═══════════════════════════════════════════════════════════════
static void ambDiagSet(const char* curto,const char* fmt,...){
  strlcpy(ambDiagCurto,curto,sizeof(ambDiagCurto));
  va_list ap; va_start(ap,fmt); vsnprintf(ambDiagTxt,sizeof(ambDiagTxt),fmt,ap); va_end(ap);
}
static void ambDiagCalcular(bool tOk,float lidMin,float onT,float onU){
  float T=temperaturaAtual, U=umidadeAtual;
  bool tBaixa=tOk && T<TEMP_IDEAL_MIN, tAlta=tOk && T>TEMP_IDEAL_MAX, uBaixa=tOk && U<UMID_IDEAL_MIN, uAlta=tOk && U>UMID_IDEAL_MAX;
  int fanCh=-1; for(int k=0;k<AUTO_N;k++) if(regras[k].modo==AM_CIRCULAR){ fanCh=k; break; }
  bool tipoAlto=(TEMP_IDEAL_MIN>39.0f);
  char extra[56]=""; if(tipoAlto) snprintf(extra,sizeof(extra)," O tipo pede %.1fC: confira se escolheu o tipo certo.",(TEMP_IDEAL_MIN+TEMP_IDEAL_MAX)/2);
  if(!tOk){ ambDiagSet("sensor","Sensor de temperatura/umidade (SHT31) sem leitura. Verifique o cabo do sensor."); return; }
  if(!autoAtivo){ ambDiagSet("auto off","A automacao esta desligada, entao nada corrige o ambiente. Ligue em Configuracoes > Automacao."); return; }
  if(tBaixa && regras[0].modo!=AM_AQUECER){ ambDiagSet("CH1 manual","O CH1 (aquecedor) nao esta em modo Aquecer. Ajuste em Configuracoes > Automacao."); return; }
  if(uBaixa && regras[1].modo!=AM_UMIDIFICAR){ ambDiagSet("CH2 manual","O CH2 (umidificador) nao esta em modo Umidificar. Ajuste em Configuracoes > Automacao."); return; }
  if(tBaixa && (fanCh<0 || !relayState[fanCh])){ ambDiagSet("fans off","As Fans (CH8) estao desligadas e o aquecedor so liga com o ar circulando. Coloque o CH8 em Circular."); return; }
  if(lidMin>=0 && (tBaixa||uBaixa)){ ambDiagSet("tampa?","Queda brusca de %s ha %.0f min: a tampa da caixa pode estar aberta. Feche e aguarde alguns minutos.",tempSlopeMin<=-0.9f?"temperatura":"umidade",lidMin); return; }
  if(tAlta){ ambDiagSet("temp alta","Temperatura alta (%.1fC, maximo %.1fC): o aquecedor ja esta desligado. Aguarde esfriar; se persistir, ventile ou confira a potencia em Hardware.",T,TEMP_IDEAL_MAX); return; }
  if(tBaixa){
    float prior=hwRonPrior(), rm=pid[0].rMeas;
    if(rm>0 && prior>0){
      float r=rm/prior;
      if(r>1.8f){ ambDiagSet("volume?","Aquece %.1fx mais rapido que o previsto para %u L e %.0f W: a caixa deve ser MENOR (ou o aquecedor mais forte) do que informado. Confira Configuracoes > Hardware.",r,(unsigned)hw.volL,hwPtot()); return; }
      if(r<0.45f){ ambDiagSet("vol/tampa?","Aquece %.1fx mais devagar que o previsto para %u L e %.0f W: volume ou potencia informados altos demais, tampa aberta ou frestas grandes. Confira Hardware.",1.0f/(r>0.01f?r:0.01f),(unsigned)hw.volL,hwPtot()); return; }
    }
    if(pid[0].satAv){ ambDiagSet("aquec. max","Aquecedor no limite (%.0f W): nao passa de %.1fC (minimo %.1fC). A caixa perde calor demais (tampa aberta, frestas) ou a potencia e baixa para %u L.%s",hwPtot(),T,TEMP_IDEAL_MIN,(unsigned)hw.volL,extra); return; }
    if(onT>0.7f && tempSlopeMin<-0.1f){ ambDiagSet("sem aquecer","Temperatura caindo mesmo com o aquecedor ligado: confira a resistencia, a fonte e o rele CH1, ou se a tampa esta aberta."); return; }
    ambDiagSet("aquecendo","Temperatura %.1fC, abaixo da faixa (%.1f-%.1f). O aquecedor esta subindo; aguarde mais um pouco.%s",T,TEMP_IDEAL_MIN,TEMP_IDEAL_MAX,extra); return;
  }
  if(uBaixa){
    if(hw.umQtd==0){ ambDiagSet("sem umidif","Nenhum umidificador configurado em Configuracoes > Hardware, entao a umidade nao sobe."); return; }
    if(pid[1].satAv){ ambDiagSet("umid. max","Umidificador no limite (%u ml/h): umidade nao passa de %.0f%% (minimo %.0f%%). Falta agua no reservatorio? Tampa aberta/frestas, ou a vazao real e menor que a informada.",(unsigned)(hw.umMlH*hw.umQtd),U,UMID_IDEAL_MIN); return; }
    if(onU>0.7f && humSlopeMin<=0.0f){ ambDiagSet("sem agua?","Umidificador ligado e a umidade nao sobe: o reservatorio pode estar sem agua."); return; }
    ambDiagSet("umidific.","Umidade %.0f%%, abaixo da faixa (%.0f-%.0f%%). O umidificador esta atuando; aguarde.",U,UMID_IDEAL_MIN,UMID_IDEAL_MAX); return;
  }
  if(uAlta){ ambDiagSet("umid. alta","Umidade alta (%.0f%%, maximo %.0f%%): ventile a caixa.",U,UMID_IDEAL_MAX); return; }
  ambDiagSet("ajustando","Ainda ajustando o ambiente. Temperatura %.1fC, umidade %.0f%%.",T,U);
}
static void ambDiagTick(){   // 1 Hz, chamado de ambienteTick
  static unsigned long ultDentro=0, lidMs=0, ultAviso=0; static float onT=0, onU=0; static long ctx=-1; static char ultCurto[18]="";
  if(tampaEst!=0){ ultDentro=millis(); ambDiagTxt[0]=0; ambDiagCurto[0]=0; ultCurto[0]=0; return; }   // Fase 82: com a tampa aberta nao ha diagnostico
  if(aguaEst==2 && (fbCicloAtivo || ambManterOn())){ ambDiagSet("sem agua","%s",aguaTxt); return; }   // Fase 83: alerta de agua (o aviso/push e do aguaTick)
  bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (millis()-lastSht31OkMs)<120000UL;
  bool dentro=tOk && temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX && umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
  long cx=(long)fbCicloIniciadoEm ^ (ambManterOn()?0x55555:0) ^ (fbCicloAtivo?1:0);
  if(cx!=ctx){ ctx=cx; ultDentro=millis(); ambDiagTxt[0]=0; ambDiagCurto[0]=0; ultCurto[0]=0; }
  if(dentro) ultDentro=millis();
  onT=onT*0.99f+(relayState[0]?0.01f:0.0f); onU=onU*0.99f+(relayState[1]?0.01f:0.0f);
  if(tempSlopeMin<=-0.9f || humSlopeMin<=-6.0f) lidMs=millis();
  bool contexto=fbCicloAtivo || ambManterOn();
  bool problema=contexto && !dentro && (millis()-ultDentro>=600000UL);
  if(!problema){ ambDiagTxt[0]=0; ambDiagCurto[0]=0; ultCurto[0]=0; return; }
  float lidMin=(lidMs && millis()-lidMs<480000UL)?(millis()-lidMs)/60000.0f:-1.0f;
  ambDiagCalcular(tOk,lidMin,onT,onU);
  if(strcmp(ultCurto,ambDiagCurto)!=0 && millis()-ultAviso>=300000UL){   // log + push quando muda a causa (no maximo a cada 5 min)
    strlcpy(ultCurto,ambDiagCurto,sizeof(ultCurto)); ultAviso=millis();
    logEvento("Ciclo",ambDiagTxt,true);
    pushEnfileirar(13,"ChocaIoT: ambiente nao estabilizou",ambDiagTxt);
  }
}
// ═══════════════════════════════════════════════════════════════
// Fase 82 -- TAMPA ABERTA (ovoscopia, agua do umidificador, virar ovos): o usuario NAO precisa pausar o ciclo.
// Pausar so congela a contagem de dias; o ambiente segue controlado (os ovos estao la dentro). Abrir a caixa e tratado sozinho:
//  * detecta a abertura por queda brusca (temperatura <= -0,9 C/min ou umidade <= -6 %/min) ou pelo aviso "Vou abrir a caixa";
//  * ABERTA: sem alertas/diagnostico, integral do PI congelado (nao acumula erro que nao e do controle), umidificador em pausa
//    (nao umidifica o ar da sala), aquecedor compensa normalmente; estatisticas do relatorio nao contam esse tempo;
//  * fechada (temperatura voltou a subir, aviso "Fechei a caixa" ou 15 min): RECUPERANDO ate 1 min na faixa -- umidificador
//    entra direto bem antes (3 % abaixo) para recuperar a umidade rapido; ao voltar, log com o tempo de recuperacao.
// Nao vale no preparo (sem ovos).
// ═══════════════════════════════════════════════════════════════
static void tampaTick(){
  bool contexto=(fbCicloAtivo && !fbCicloPreparo) || ambManterOn();
  unsigned long agora=millis();
  static unsigned long subindoDesde=0, dentroDesde=0, abertaSegTotal=0;
  static bool quedaVista=false, origAdxl=false;   // Fase 95: abertura pelo sensor sem NENHUMA queda de T/U = alarme falso
  if(!contexto){ tampaEst=0; tampaCmd=0; return; }
  bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (agora-lastSht31OkMs)<120000UL;
  bool dentro=tOk && temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX && umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
  float sT=tempSlopeMin, sH=humSlopeMin;
  if(tampaCmd==1){ tampaCmd=0; if(tampaEst==0){ tampaEst=1; tampaIniMs=agora; subindoDesde=0; origAdxl=tampaOrigemAdxl; quedaVista=false; logEvento("Ciclo",tampaOrigemAdxl?"Tampa aberta (sensor ADXL: modulo retirado ou movido): alertas e umidificador em pausa, aquecedor compensando":"Tampa aberta (aviso do usuario): alertas e umidificador em pausa, aquecedor compensando",true); } tampaOrigemAdxl=false; }
  else if(tampaCmd==2){ tampaCmd=0; if(tampaEst==1 && tampaOrigemAdxl && origAdxl && !quedaVista){   // Fase 96: o modulo voltou e a caixa nunca esfriou: nao houve abertura, nada a recuperar
      tampaEst=0; origAdxl=false; logEvento("Ciclo","Alarme falso do sensor ADXL: o modulo voltou ao lugar e a caixa nao chegou a esfriar -- sem recuperacao",true);
    } else if(tampaEst==1){ abertaSegTotal=(agora-tampaIniMs)/1000UL; tampaEst=2; tampaIniMs=agora; dentroDesde=0; logEvento("Ciclo",tampaOrigemAdxl?"Tampa fechada (sensor ADXL: modulo de volta na posicao): recuperando o ambiente":"Tampa fechada (aviso do usuario): recuperando o ambiente",true); } tampaOrigemAdxl=false; }
  if(tampaEst==0){
    static unsigned long quedaDesde=0;   // queda precisa durar >= 8 s e so vale depois de 3 min ligado (a janela de inclinacao ainda esta se formando no boot)
    if(tOk && agora>180000UL && (sT<=-0.9f || sH<=-8.0f)){ if(!quedaDesde) quedaDesde=agora; } else quedaDesde=0;
    if(quedaDesde && agora-quedaDesde>=8000UL){ quedaDesde=0; tampaEst=1; tampaIniMs=agora; subindoDesde=0; origAdxl=false; quedaVista=true; logEvento("Ciclo","Tampa aberta detectada (queda brusca de temperatura/umidade): alertas e umidificador em pausa, aquecedor compensando",true); }
  }else if(tampaEst==1){
    unsigned long aberta=agora-tampaIniMs;
    if(sT<=-0.3f || sH<=-2.0f) quedaVista=true;
    if(origAdxl && !quedaVista && aberta>30000UL && adxlAssentado()){   // Fase 97: o modulo ja esta parado no lugar e a caixa nunca esfriou -> nao abriu
      tampaEst=0; origAdxl=false; subindoDesde=0; adxlTampaCancelada();
      logEvento("Ciclo","Alarme falso do sensor ADXL: o modulo voltou ao lugar e a caixa nao esfriou (cancelado em 30 s)",true);
    }
    if(origAdxl && !quedaVista && aberta>240000UL){   // Fase 95: o modulo foi mexido mas a caixa nao esfriou nem perdeu umidade em 4 min -> nao abriu
      tampaEst=0; origAdxl=false; subindoDesde=0;
      logEvento("Ciclo","Alarme falso do sensor ADXL: o modulo foi movido mas a caixa nao abriu (sem queda de temperatura/umidade em 4 min)",true);
    }
    if(sT>=0.12f && sH>=-1.0f){ if(!subindoDesde) subindoDesde=agora; } else subindoDesde=0;
    // Fase 111: se o sensor ADXL (calibrado, vivo) diz que o modulo AINDA esta fora do lugar (ABERTA), a tampa esta aberta de verdade: o aquecedor
    // compensando faz a temperatura subir mesmo aberta e o limite de 15 min nao pode devolver o Status para OK.
    bool adxlAberta=(adxlAutoAtivo() && adxlEst==4);
    bool fechou=(tampaEst==1 && aberta>45000UL && subindoDesde && agora-subindoDesde>=20000UL && !adxlAberta);
    if(tampaEst==1 && (fechou || (aberta>900000UL && !adxlAberta))){ abertaSegTotal=aberta/1000UL; tampaEst=2; tampaIniMs=agora; dentroDesde=0; logEvento("Ciclo",fechou?"Tampa fechada (a temperatura voltou a subir): recuperando o ambiente":"Tampa aberta ha 15 min: voltando ao controle normal",true); }
  }else{
    if(dentro){ if(!dentroDesde) dentroDesde=agora; } else dentroDesde=0;
    unsigned long rec=agora-tampaIniMs;
    if((dentroDesde && agora-dentroDesde>=60000UL) || rec>900000UL){
      char m[100]; snprintf(m,sizeof(m),"Ambiente recuperado: tampa aberta %lus, recuperacao %lum%02lus",abertaSegTotal,rec/60000UL,(rec/1000UL)%60UL);
      tampaEst=0; logEvento("Ciclo",m,true);
    }
  }
  tampaSeg=(unsigned)((agora-tampaIniMs)/1000UL);
}
// Fase 79 -- o aviso (arqToast) desenha por cima da tela e nao apaga: ficava a barra verde pela metade entre os cards.
static void ambToast(const char* m){
  if(appState!=S_MAIN && appState!=S_STATUS && appState!=S_CICLO && appState!=S_SETTINGS) return;
  arqToast(m,true); redrawCurrentScreen();
}
void ambienteTick(){
  migracao70();
  tampaTick();   // Fase 82
  if(!ambCarregado){ ambCarregado=true; ambCarregar(); }
  time_t now=time(nullptr);
  int novo=0;
  if(cicloPollRecebido && !fbCicloAtivo && ambEpoch!=0){ ambEpoch=0; ambPronto=false; ambOkAtivo=false; ambSalvar(); }   // ciclo encerrado: o proximo e "novo"
  if(fbCicloAtivo && !fbCicloPausado && now>=(time_t)EPOCH_2024_01_01 && cicloTipoAtualPtr()){
    if((long)fbCicloIniciadoEm!=ambEpoch && ambEpoch!=0){   // mesmo lote: "Retomar" depois de uma pausa desloca o inicio -- nao e ciclo novo
      ambEpoch=(long)fbCicloIniciadoEm; ambSalvar();
    }
    if((long)fbCicloIniciadoEm!=ambEpoch){   // ciclo que este aparelho ainda nao tinha visto (ambEpoch==0: o anterior terminou)
      long idade=(long)(now-fbCicloIniciadoEm);
      ambEpoch=(long)fbCicloIniciadoEm; ambOkAtivo=false; ambAvisos=0;
      bool recente=fbCicloPreparo;   // Fase 77: so o ciclo em PREPARO (sem ovos) espera; ciclo ja com ovos nao passa por isso
      ambPronto=!recente;
      ambSalvar();
      if(recente){
        char extra[60]="";
        if(!autoAtivo){
          autoAtivo=true; autoSoCiclo=true;
          if(regras[0].modo==AM_OFF){ regras[0].modo=AM_AQUECER; regras[0].alvoCustom=0; regras[0].hist10=2; strlcat(extra," Aquecedor->auto.",sizeof(extra)); }
          if(regras[1].modo==AM_OFF){ regras[1].modo=AM_UMIDIFICAR; regras[1].alvoCustom=0; regras[1].hist10=10; strlcat(extra," Umidif.->auto.",sizeof(extra)); }
          if(regras[7].modo==AM_OFF){ regras[7].modo=AM_CIRCULAR; strlcat(extra," Fans->circular.",sizeof(extra)); }
          autoPersistir();
          char m[100]; snprintf(m,sizeof(m),"Automacao ligada junto com o ciclo.%s",extra); logEvento("Automacao",m,true);
        }
        logEvento("Ciclo","Aguardando o ambiente estabilizar: nao coloque os ovos ainda",true);
        pushEnfileirar(11,"ChocaIoT: NAO coloque os ovos ainda","Ciclo iniciado. A chocadeira esta ajustando temperatura e umidade. Avisaremos quando estiver pronta.");
        ambToast("Aguarde: ajustando ambiente");
      }
    }
    if(!fbCicloPreparo && !ambPronto){ ambPronto=true; ambSalvar(); }
    novo=ambPronto?2:1;
    if(novo==1){
      bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (millis()-lastSht31OkMs)<120000UL;
      bool dentro=tOk && temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX && umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
      if(dentro) ambForaDesde=0;
      else if(ambOkAtivo){   // saiu da faixa: so zera o relogio se ficar fora por mais de 1 min (oscilacao normal do controle)
        if(!ambForaDesde) ambForaDesde=millis();
        else if(millis()-ambForaDesde>=AMB_FORA_TOL_MS){ ambOkAtivo=false; ambForaDesde=0; }
      }
      ambEstavelSeg=ambOkAtivo?(unsigned)((millis()-ambOkDesde)/1000UL):0;
      if(dentro){
        if(!ambOkAtivo){ ambOkAtivo=true; ambOkDesde=millis(); }
        else if(millis()-ambOkDesde>=AMB_ESTAVEL_MS){
          ambPronto=true; ambSalvar(); novo=2;
          char m[110]; snprintf(m,sizeof(m),"Ambiente estavel (%.1fC / %.0f%%): pode colocar os ovos",temperaturaAtual,umidadeAtual);
          logEvento("Ciclo",m,true);
          pushEnfileirar(12,"ChocaIoT: ambiente pronto","Temperatura e umidade estaveis ha 5 min. Pode colocar os ovos e confirmar no aparelho ou no app.");
          ambToast("Ambiente pronto! Pode colocar os ovos");
        }
      }
      long dec=(long)(now-fbCicloIniciadoEm);
      if(false && novo==1 && !dentro && ((dec>=900 && !(ambAvisos&1)) || (dec>=3600 && !(ambAvisos&2)))){   // Fase 80: substituido por ambDiagTick (10 min)
        ambAvisos|=(dec>=3600)?3:1;
        char c[110];
        if(!tOk) snprintf(c,sizeof(c),"Sensor de temperatura/umidade sem leitura. Verifique o SHT31.");
        else if(!autoAtivo) snprintf(c,sizeof(c),"A automacao esta desligada. Ligue em Configuracoes > Automacao.");
        else if(temperaturaAtual<TEMP_IDEAL_MIN){
          if(pid[0].satAv) snprintf(c,sizeof(c),"Temperatura baixa (%.1fC, minimo %.1fC): aquecedor no limite (%.0f W). Veja Configuracoes > Hardware.",temperaturaAtual,TEMP_IDEAL_MIN,hwPtot());
          else snprintf(c,sizeof(c),"Temperatura baixa (%.1fC, minimo %.1fC). Verifique a resistencia e o rele CH1.",temperaturaAtual,TEMP_IDEAL_MIN);
        }
        else if(temperaturaAtual>TEMP_IDEAL_MAX) snprintf(c,sizeof(c),"Temperatura alta (%.1fC, maximo %.1fC). Verifique ventilacao.",temperaturaAtual,TEMP_IDEAL_MAX);
        else if(umidadeAtual<UMID_IDEAL_MIN){
          if(pid[1].satAv) snprintf(c,sizeof(c),"Umidade baixa (%.0f%%, minimo %.0f%%): umidificador no limite. Veja Configuracoes > Hardware.",umidadeAtual,UMID_IDEAL_MIN);
          else snprintf(c,sizeof(c),"Umidade baixa (%.0f%%, minimo %.0f%%). Verifique agua e o umidificador (CH2).",umidadeAtual,UMID_IDEAL_MIN);
        }
        else snprintf(c,sizeof(c),"Umidade alta (%.0f%%, maximo %.0f%%). Ventile a chocadeira.",umidadeAtual,UMID_IDEAL_MAX);
        logEvento("Ciclo",c,true);
        pushEnfileirar(13,"ChocaIoT: ambiente ainda nao estabilizou",c);
      }
    }
  }
  if(!fbCicloAtivo && ambManterOn()){   // Fase 79: 3 = mantendo (na faixa ha >= 1 min) · 4 = ajustando
    static unsigned long okDesde=0, foraDesde=0;
    bool tOk=sht31OK && !isnan(temperaturaAtual) && !isnan(umidadeAtual) && lastSht31OkMs && (millis()-lastSht31OkMs)<120000UL;
    bool dentro=tOk && temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX && umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
    if(dentro){ foraDesde=0; if(!okDesde) okDesde=millis(); }
    else if(okDesde){ if(!foraDesde) foraDesde=millis(); else if(millis()-foraDesde>=AMB_FORA_TOL_MS){ okDesde=0; foraDesde=0; } }
    novo=(okDesde && millis()-okDesde>=60000UL)?3:4;
  }
  if(tampaEst==1) novo=5; else if(tampaEst==2) novo=6;   // Fase 82: tampa aberta / recuperando
  if(novo!=1) ambEstavelSeg=0;
  ambEstado=novo;
  ambDiagTick();   // Fase 80
  { static int ultSig=-1;   // Fase 77: card Status (borda de progresso) e card Ciclo so redesenham quando algo muda
    int sig=(fbCicloAtivo?1:0)|(fbCicloPreparo?2:0)|(ambEstado<<2)|((int)ambEstavelSeg<<5);
    for(const char* q=ambDiagCurto;*q;q++) sig=sig*31+*q;   // Fase 80: etiqueta do diagnostico mudou -> redesenha
    { int r=fanRpmAtual(); sig=sig*31+(r>0?r/50+1:0); }   // Fase 110: rpm mudou (faixas de 50)
    sig=sig*31+(fbCicloAtivo&&!fbCicloPreparo?statusMotivoCiclo():0);   // Fase 112: motivo do status mudou
    { StItem t2[4]; float pg2; bool pp2; int nn=statusItens(t2,&pg2,&pp2); sig=sig*31+nn+(nn>1?(int)((millis()/5000UL)%nn)*7:0); }   // Fase 115: item rotativo / CO2 alto
    if(sig!=ultSig){ ultSig=sig; if(appState==S_MAIN){ drawStatusCard(); drawCicloCard(); } } }
}

// ── Resumo de uma regra (lista) ──
static void autoResumo(int ch,char* out,size_t n){
  if(ch==COOLER_CH){ strlcpy(out,"Automatico pela temp. do chip",n); return; }
  const RegraAuto& r=regras[ch]; char extra[24]="";
  if(r.autoOffMin) snprintf(extra,sizeof(extra)," | max %umin",(unsigned)r.autoOffMin);
  switch(r.modo){
    case AM_AQUECER:       snprintf(out,n,"Aquecer %s%.1fC +-%.1f%s",r.alvoCustom?"":"tipo ",autoAlvoTemp(r),r.hist10/10.0f,extra); break;
    case AM_RESFRIAR:      snprintf(out,n,"Resfriar %s%.1fC +-%.1f%s",r.alvoCustom?"":"tipo ",autoAlvoTemp(r),r.hist10/10.0f,extra); break;
    case AM_UMIDIFICAR:    snprintf(out,n,"Umidificar %s%.0f%%%s",r.alvoCustom?"":"tipo ",autoAlvoUmid(r),extra); break;
    case AM_DESUMIDIFICAR: snprintf(out,n,"Ventilar acima de %s%.0f%%%s",r.alvoCustom?"":"tipo ",autoAlvoUmid(r),extra); break;
    case AM_PERIODICO:     snprintf(out,n,"Liga %us a cada %umin%s",(unsigned)r.onSeg,(unsigned)r.cadaMin,extra); break;
    case AM_HORARIO:       snprintf(out,n,"%02u:%02u ate %02u:%02u%s",r.hIni/60,r.hIni%60,r.hFim/60,r.hFim%60,extra); break;
    case AM_VIRAR:         snprintf(out,n,"Viragem: pulso de %us (tipo)%s",(unsigned)r.onSeg,extra); break;
    case AM_CIRCULAR:      snprintf(out,n,"Circular: ligado com a automacao%s",extra); break;
    default: if(r.autoOffMin) snprintf(out,n,"Manual | desliga em %umin",(unsigned)r.autoOffMin); else strlcpy(out,"Manual",n);
  }
}

// ── Telas ──
void drawAutoEdit();
int autoPag=0, autoCh=0, autoEdPag=0;
#define AUTO_VIS 4
#define AUTO_ROW_Y0 96
#define AUTO_ROW_H  36
static void drawAutoAvancadasPagina();
void drawAuto(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(2);tft.setCursor(96,6);tft.print("Automacao");
  char pg[8]; snprintf(pg,sizeof(pg),"%d/3 >",autoPag+1);
  tft.setTextColor(cClk,cCard);tft.setTextSize(1);tft.setCursor(316-tft.textWidth(pg),10);tft.print(pg);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(10,42);tft.print("Automacao ativa"); drawToggle(246,32,autoAtivo,cGreen);
  tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(10,72);tft.print("So com ciclo em andamento"); drawToggle(246,62,autoSoCiclo,cGreen);
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  if(autoPag==2){ drawAutoAvancadasPagina(); return; }
  for(int i=0;i<AUTO_VIS;i++){
    int ch=autoPag*AUTO_VIS+i; if(ch>=AUTO_N) break;
    int y=AUTO_ROW_Y0+i*AUTO_ROW_H;
    tft.fillRoundRect(4,y,312,AUTO_ROW_H-3,5,rowBg);tft.drawRoundRect(4,y,312,AUTO_ROW_H-3,5,rowBrd);
    char nm[30]; snprintf(nm,sizeof(nm),"CH%d %s",ch+1,RELAY_NAMES[ch]);
    tft.setTextColor(TFT_WHITE,rowBg);tft.setCursor(10,y+5);tft.print(nm);
    char rs[48]; autoResumo(ch,rs,sizeof(rs));
    tft.setTextColor(cSub,rowBg);tft.setCursor(10,y+19);tft.print(rs);
    bool on=relayState[ch];
    tft.fillRoundRect(280,y+6,30,18,4,on?tft.color565(10,60,10):tft.color565(40,40,50));
    tft.setTextColor(on?cGreen:cSub,on?tft.color565(10,60,10):tft.color565(40,40,50));tft.setCursor(on?287:284,y+11);tft.print(on?"ON":"OFF");
  }
}
// pagina 3/3: atalho pras regras avancadas
static void drawAutoAvancadasPagina(){
  tft.fillRoundRect(4,AUTO_ROW_Y0,312,70,8,tft.color565(16,16,46));tft.drawRoundRect(4,AUTO_ROW_Y0,312,70,8,tft.color565(55,55,120));
  tft.setTextColor(TFT_WHITE,tft.color565(16,16,46));tft.setTextSize(2);tft.setCursor(16,AUTO_ROW_Y0+10);tft.print("Regras avancadas");
  char n[32]; snprintf(n,sizeof(n),"%d regra(s) criada(s)",advN);
  tft.setTextColor(cClk,tft.color565(16,16,46));tft.setTextSize(1);tft.setCursor(16,AUTO_ROW_Y0+34);tft.print(n);
  tft.setTextColor(cSub,tft.color565(16,16,46));tft.setCursor(16,AUTO_ROW_Y0+48);tft.print("Qualquer sensor comanda qualquer combinacao de reles.");
  tft.setTextColor(tft.color565(110,110,150),cBg);tft.setCursor(8,AUTO_ROW_Y0+82);tft.print("Ex.: SE T>=37.5 ENTAO CH1 liga ATE T<=37.3,");tft.setCursor(8,AUTO_ROW_Y0+94);tft.print("SENAO CH4 liga. Combine sensores com E / OU.");
  // Fase 111: religar a automacao sozinha (rede de seguranca com ciclo ativo/pausado)
  { uint16_t cb=tft.color565(16,16,46); tft.fillRoundRect(4,200,312,36,8,cb); tft.drawRoundRect(4,200,312,36,8,tft.color565(55,55,120));
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE,cb); tft.setCursor(12,206); tft.print("Religar a automacao sozinha apos");
    tft.setTextColor(cSub,cb); tft.setCursor(12,220); tft.print("(so com ciclo ativo/pausado)");
    tft.fillRoundRect(190,206,34,24,5,cKey); tft.setTextColor(TFT_WHITE,cKey); tft.setTextSize(2); tft.setCursor(201,211); tft.print("-");
    tft.fillRoundRect(278,206,34,24,5,cKey); tft.setCursor(289,211); tft.print("+");
    char v[12]; if(autoRetomaMin==0) snprintf(v,sizeof(v),"nunca"); else snprintf(v,sizeof(v),"%u min",(unsigned)autoRetomaMin);
    tft.setTextSize(1); tft.setTextColor(cClk,cb); tft.setCursor(251-tft.textWidth(v)/2,214); tft.print(v); }
}
void abrirAuto(){ autoPag=0; appState=S_AUTO; drawAuto(); }
void handleAutoTap(int tx,int ty){
  if(ty<28){
    if(tx<80){ appState=S_SETTINGS; drawSettingsPanel(); }
    else if(tx>=250){ autoPag=(autoPag+1)%3; drawAuto(); }
    return;
  }
  if(tx>=200&&ty>=30&&ty<=60){ autoAtivo=!autoAtivo; autoPersistir(); drawAuto(); return; }
  if(tx>=200&&ty>=61&&ty<=92){ autoSoCiclo=!autoSoCiclo; autoPersistir(); drawAuto(); return; }
  if(autoPag==2 && ty>=200){   // Fase 111: seletor de religar sozinha
    static const uint16_t op[]={0,5,10,15,20,30,45,60,90,120}; const int n=sizeof(op)/sizeof(op[0]);
    int k=0; for(int i=0;i<n;i++) if(op[i]<=autoRetomaMin) k=i;
    if(tx>=180&&tx<236){ if(k>0) k--; } else if(tx>=270){ if(k<n-1) k++; } else return;
    autoRetomaMin=op[k]; autoPersistir(); drawAuto(); return;
  }
  if(ty>=AUTO_ROW_Y0){
    if(autoPag==2){ abrirAdv(); return; }   // pagina 3: regras avancadas
    int row=(ty-AUTO_ROW_Y0)/AUTO_ROW_H, ch=autoPag*AUTO_VIS+row;
    if(row<0||row>=AUTO_VIS||ch>=AUTO_N) return;
    if(ch==COOLER_CH){ arqToast("Controlado pela temp. do chip",false); drawAuto(); return; }
    autoCh=ch; autoEdPag=0; appState=S_AUTO_EDIT; drawAutoEdit();
  }
}

// Edicao de uma regra: pagina 0 = modo; pagina 1 = parametros do modo
struct AutoLinha{ const char* lb; char val[18]; int id; };   // id: 1 alvo tipo/custom, 2 alvo, 3 histerese, 4 onSeg, 5 cadaMin, 6 hIni, 7 hFim, 8 autoOff
static int autoLinhas(AutoLinha* L){
  const RegraAuto& r=regras[autoCh]; int n=0;
  bool tmp=(r.modo==AM_AQUECER||r.modo==AM_RESFRIAR), um=(r.modo==AM_UMIDIFICAR||r.modo==AM_DESUMIDIFICAR);
  if(tmp||um){
    L[n].lb="Alvo"; strlcpy(L[n].val,r.alvoCustom?"Personalizado":"Do tipo do ciclo",18); L[n].id=1; n++;
    if(r.alvoCustom){ L[n].lb=tmp?"Valor (C)":"Valor (%)"; snprintf(L[n].val,18,"%.1f",r.alvo10/10.0f); L[n].id=2; n++; }
    L[n].lb=tmp?"Histerese (C)":"Histerese (%)"; snprintf(L[n].val,18,"%.1f",r.hist10/10.0f); L[n].id=3; n++;
  }else if(r.modo==AM_PERIODICO||r.modo==AM_VIRAR){
    L[n].lb=(r.modo==AM_VIRAR)?"Pulso (s)":"Liga por (s)"; snprintf(L[n].val,18,"%u",(unsigned)r.onSeg); L[n].id=4; n++;
    if(r.modo==AM_PERIODICO){ L[n].lb="A cada (min)"; snprintf(L[n].val,18,"%u",(unsigned)r.cadaMin); L[n].id=5; n++; }
  }else if(r.modo==AM_HORARIO){
    L[n].lb="Liga as"; snprintf(L[n].val,18,"%02u:%02u",r.hIni/60,r.hIni%60); L[n].id=6; n++;
    L[n].lb="Desliga as"; snprintf(L[n].val,18,"%02u:%02u",r.hFim/60,r.hFim%60); L[n].id=7; n++;
  }
  L[n].lb="Desliga apos (min)"; if(r.autoOffMin) snprintf(L[n].val,18,"%u",(unsigned)r.autoOffMin); else strlcpy(L[n].val,"nunca",18); L[n].id=8; n++;
  return n;
}
void drawAutoEdit(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Salvar");
  char t[30]; snprintf(t,sizeof(t),"CH%d %s",autoCh+1,RELAY_NAMES[autoCh]);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(80,10);tft.print(t);
  tft.setTextColor(cClk,cCard);tft.setCursor(316-tft.textWidth(autoEdPag?"< Modo":"Params >"),10);tft.print(autoEdPag?"< Modo":"Params >");
  const RegraAuto& r=regras[autoCh];
  if(autoEdPag==0){
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(10,36);tft.print("Escolha o que este rele faz:");
    for(int m=0;m<AM_N;m++){
      int col=m%2,row=m/2, x=10+col*152, y=46+row*36;
      bool sel=(r.modo==m);
      arqBotao(x,y,146,32,AUTO_MODO_NOME[m],sel?tft.color565(10,50,20):tft.color565(16,16,46),sel?cGreen:TFT_WHITE,sel?cGreen:tft.color565(55,55,120));
    }
    tft.setTextColor(tft.color565(110,110,150),cBg);tft.setTextSize(1);
    // Fase 57: o 8o quadrado da grade (dir. da ultima linha) esta livre -- a leitura dos sensores mora ali, sem sobrepor os botoes
    tft.setCursor(10,230);tft.printf("T %s  U %s  Rele: %s  | Params > ajusta",isnan(temperaturaAtual)?"--":String(temperaturaAtual,1).c_str(),isnan(umidadeAtual)?"--":String(umidadeAtual,0).c_str(),relayState[autoCh]?"ON":"OFF");
  }else{
    AutoLinha L[6]; int n=autoLinhas(L);
    for(int i=0;i<n;i++){
      int y=38+i*38;
      tft.setTextSize(1);tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(8,y+12);tft.print(L[i].lb);
      if(L[i].id==1){ arqBotao(130,y,180,32,L[i].val,tft.color565(16,16,46),cClk,tft.color565(55,55,120)); continue; }
      tft.fillRoundRect(128,y,36,32,6,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setTextSize(2);tft.setCursor(140,y+9);tft.print("-");
      tft.fillRoundRect(274,y,36,32,6,cKey);tft.setCursor(286,y+9);tft.print("+");
      tft.setTextColor(cClk,cBg);tft.setTextSize(L[i].val[0]&&strlen(L[i].val)>5?1:2);
      tft.setCursor(170+(100-tft.textWidth(L[i].val))/2,y+(strlen(L[i].val)>5?12:9));tft.print(L[i].val);
    }
    tft.setTextSize(1);tft.setTextColor(tft.color565(110,110,150),cBg);
    tft.setCursor(8,232-10);
    switch(r.modo){
      case AM_OFF: tft.print("Sem automacao: so o limite de tempo ligado."); break;
      case AM_PERIODICO: tft.print("Liga no inicio de cada intervalo, sincronizado com o relogio."); break;
      case AM_HORARIO: tft.print("Liga dentro da janela (vale virar a meia-noite)."); break;
      case AM_VIRAR: tft.print("Vira N x/dia na hora e ate o dia definidos no Tipo de ovo."); break;
      case AM_CIRCULAR: tft.print("Fica ligado sempre que a automacao esta ativa (circula o ar)."); break;
      default: tft.print("Liga abaixo do alvo-histerese, desliga ao atingir o alvo."); break;
    }
  }
}
static void autoAjusta(int id,int dir){
  RegraAuto& r=regras[autoCh];
  bool tmp=(r.modo==AM_AQUECER||r.modo==AM_RESFRIAR);
  switch(id){
    case 1: r.alvoCustom=r.alvoCustom?0:1; break;
    case 2: r.alvo10+=dir*(tmp?1:10); if(tmp){ if(r.alvo10<300) r.alvo10=300; if(r.alvo10>420) r.alvo10=420; } else { if(r.alvo10<200) r.alvo10=200; if(r.alvo10>950) r.alvo10=950; } break;
    case 3: r.hist10+=dir*(tmp?1:10); if(r.hist10<1) r.hist10=1; if(r.hist10>(tmp?50:200)) r.hist10=(tmp?50:200); break;
    case 4: { int v=(int)r.onSeg+dir*10; if(v<5) v=5; if(v>3600) v=3600; r.onSeg=v; } break;
    case 5: { int v=(int)r.cadaMin+dir*5; if(v<1) v=1; if(v>1440) v=1440; r.cadaMin=v; } break;
    case 6: r.hIni=(uint16_t)((r.hIni+1440+dir*15)%1440); break;
    case 7: r.hFim=(uint16_t)((r.hFim+1440+dir*15)%1440); break;
    case 8: { int v=(int)r.autoOffMin+dir*5; if(v<0) v=0; if(v>720) v=720; r.autoOffMin=v; } break;
  }
}
void handleAutoEditTap(int tx,int ty){
  if(ty<28){
    if(tx<70){ autoPersistir(); appState=S_AUTO; drawAuto(); }
    else if(tx>=240){ autoEdPag=1-autoEdPag; drawAutoEdit(); }
    return;
  }
  RegraAuto& r=regras[autoCh];
  if(autoEdPag==0){
    for(int m=0;m<AM_N;m++){
      int col=m%2,row=m/2, x=10+col*152, y=46+row*36;
      if(tx>=x&&tx<=x+146&&ty>=y&&ty<=y+32){
        if(r.modo!=m){
          r.modo=m;
          if(m==AM_AQUECER||m==AM_RESFRIAR){ r.alvo10=375; r.hist10=2; r.alvoCustom=0; }
          else if(m==AM_UMIDIFICAR||m==AM_DESUMIDIFICAR){ r.alvo10=550; r.hist10=10; r.alvoCustom=0; }
          else if(m==AM_VIRAR){ r.onSeg=30; }
        }
        drawAutoEdit(); return;
      }
    }
    return;
  }
  AutoLinha L[6]; int n=autoLinhas(L);
  for(int i=0;i<n;i++){
    int y=38+i*38; if(ty<y||ty>y+32) continue;
    if(L[i].id==1){ if(tx>=130) { autoAjusta(1,0); drawAutoEdit(); } return; }
    if(tx>=120&&tx<=170){ autoAjusta(L[i].id,-1); drawAutoEdit(); }
    else if(tx>=268){ autoAjusta(L[i].id,+1); drawAutoEdit(); }
    return;
  }
}

// ── Nome da chocadeira (Fase 55): Config. > Nome. Sincroniza com o PWA (campo "nome" + cmd.nomeChoc) ──
char chocNome[25]="Chocadeira"; unsigned long lastLocalNomeMs=0;
static char nomeEditBuf[25]="";
#define NOME_CFG_PATH "/nome.cfg"
void nomeCfgSalvar(){ File c=LittleFS.open(NOME_CFG_PATH,"w"); if(!c) return; c.println(chocNome); c.close(); }
void nomeCfgCarregar(){
  File c=LittleFS.open(NOME_CFG_PATH,"r"); if(!c) return;
  String n=c.readStringUntil('\n'); c.close(); n.trim(); if(n.length()) strlcpy(chocNome,n.c_str(),sizeof(chocNome));
}
void nomeDoPoll(const String& n){   // callback do poll: so guarda (relogio logico contra o poll antigo)
  if(lastLocalNomeMs && lastLocalNomeMs>=fbPollIssuedAtMs) return;
  String t=asciiFold(n); t.trim(); if(t.length()>24) t=t.substring(0,24);
  if(t.length() && t!=String(chocNome)){ strlcpy(chocNome,t.c_str(),sizeof(chocNome)); nomeCfgSalvar(); }
}
static void nomePersistir(){
  nomeCfgSalvar(); lastLocalNomeMs=millis();
  if(!fbStarted||!fbApp.ready()) return;
  Document<Values::Value> doc("nome",Values::Value(Values::StringValue(jsonEsc(String(chocNome)))));
  doc.add("cmd",Values::Value(Values::MapValue("nomeChoc",Values::StringValue(jsonEsc(String(chocNome))))));
  DocumentMask dm("nome,cmd.nomeChoc");
  PatchDocumentOptions opt(dm,DocumentMask(),Precondition());
  fbDocs.patch(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),FIRESTORE_DOCUMENT_PATH,opt,doc,fbPatchCallback,"fbNomeTask");
}
static void nomeOk(){
  sanitizarNome(nomeEditBuf);
  if(!nomeEditBuf[0]){ arqToast("Digite um nome",false); drawKbTxtScreen(); return; }
  strlcpy(chocNome,nomeEditBuf,sizeof(chocNome)); nomePersistir();
  appState=S_SEG; drawSeg(); arqToast("Nome salvo",true); drawSeg();
}
static void nomeVoltar(){ appState=S_SEG; drawSeg(); }
void nomeAbrirEdicao(){
  strlcpy(nomeEditBuf,chocNome,sizeof(nomeEditBuf));
  abrirKbTxt("Nome da chocadeira","Ate 24 letras (aparece no app):",0,nomeEditBuf,24,nomeOk,nomeVoltar);
}

// ── Motor das regras avancadas ──
static bool advLeitura(int s,float& v){
  if(s==0){ if(!sht31OK||isnan(temperaturaAtual)||!lastSht31OkMs||millis()-lastSht31OkMs>120000UL) return false; v=temperaturaAtual; return true; }
  if(s==1){ if(!sht31OK||isnan(umidadeAtual)||!lastSht31OkMs||millis()-lastSht31OkMs>120000UL) return false; v=umidadeAtual; return true; }
  if(s==3){ if(isnan(co2Atual)||!lastCo2OkMs||millis()-lastCo2OkMs>120000UL) return false; v=co2Atual; return true; }
  if(isnan(espTempAtual)) return false; v=espTempAtual; return true;
}
static bool advCmp(int op,float x,float ref){ switch(op){ case 0: return x>=ref-0.0001f; case 1: return x<=ref+0.0001f; case 2: return x>ref; default: return x<ref; } }
static void advAplicaAcoes(int idx,uint16_t mask,bool sobreTemp){
  unsigned long agora=millis();
  for(int ch=0;ch<AUTO_N;ch++){
    if(ch==COOLER_CH) continue;
    int code=(mask>>(2*ch))&3; if(!code) continue;
    if(autoTinhaUltimo[ch] && relayState[ch]!=autoUltimo[ch]){ autoPausaAte[ch]=agora+300000UL; autoUltimo[ch]=relayState[ch]; }   // mexeram a mao: pausa 5 min
    if(agora<autoPausaAte[ch]) continue;
    bool quer=(code==1);
    if(quer && ch==0 && sobreTemp) continue;                                   // nunca liga o aquecedor em sobretemperatura
    if(quer==relayState[ch]) continue;
    if(agora-autoUltToggleMs[ch]<30000UL) continue;                            // chaveamento minimo de 30 s
    char m[24]; snprintf(m,sizeof(m),"regra %d",idx+1);
    autoAplica(ch,quer,m);
  }
}
static void advTick(bool tOk,bool sobreTemp,int minDia){
  unsigned long agora=millis();
  for(int i=0;i<advN;i++){
    RegraAv& r=adv[i]; if(!r.ativa) continue;
    float x1=0,x2=0; bool ok1=advLeitura(r.s1,x1), ok2=r.comb?advLeitura(r.s2,x2):true;
    if(!ok1||!ok2){ if(!tOk && i>=0){ /* sensor perdido: so o aquecedor/umidificador que a automacao ligou sao desligados */
        for(int ch=0;ch<2;ch++) if(autoTinhaUltimo[ch]&&relayState[ch]) autoAplica(ch,false,"sensor sem leitura"); }
      continue; }
    bool c=advCmp(r.op1,x1,advRef(r.s1,r.v1));
    if(r.comb){ bool c2=advCmp(r.op2,x2,advRef(r.s2,r.v2)); c=(r.comb==1)?(c&&c2):(c||c2); }
    bool janela=true;
    if(r.hIni!=r.hFim){ if(minDia<0) janela=true; else janela=(r.hIni<r.hFim)?(minDia>=r.hIni&&minDia<r.hFim):(minDia>=r.hIni||minDia<r.hFim); }
    bool on=janela&&c, novo=advLatch[i];
    if(!advLatch[i]){ if(on) novo=true; }
    else{
      bool off;
      if(r.temOff){ float xo=0; bool okO=advLeitura(r.sOff,xo); off=okO&&advCmp(r.opOff,xo,advRef(r.sOff,r.vOff)); if(!janela) off=true; }
      else off=!on;
      if(off) novo=false;
    }
    bool mudou=(!advInit[i])||(novo!=advLatch[i]);
    advLatch[i]=novo; advInit[i]=true;
    if(mudou||agora-advUltMs[i]>30000UL){
      advUltMs[i]=agora;
      uint16_t m=novo?r.acaoA:r.acaoI;
      if(!novo){   // "liga enquanto a regra esta ativa": rele do ENTAO sem acao no SENAO desliga ao inativar (a menos que outra regra ativa o queira ligado)
        for(int ch=0;ch<AUTO_N;ch++){
          if(((r.acaoA>>(2*ch))&3)!=1 || ((m>>(2*ch))&3)!=0) continue;
          bool outra=false;
          for(int k=0;k<advN;k++) if(k!=i && adv[k].ativa && advLatch[k] && ((adv[k].acaoA>>(2*ch))&3)==1) outra=true;
          if(!outra) m|=(uint16_t)(2u<<(2*ch));
        }
      }
      advAplicaAcoes(i,m,sobreTemp);
    }
  }
}

// ── Telas: lista de regras avancadas e editor ──
static const char* ADV_SENSOR[4]={"Temperatura","Umidade","Temp. chip","CO2"};
static const char* ADV_OP[4]={">=","<=",">","<"};
static const char* ADV_SENSOR_C[4]={"T","U","Chip","CO2"};
int advSel=-1, advPag=0, advScroll=0;
static void advValTxt(char* out,size_t n,int s,int v){ if(s==3) snprintf(out,n,"%dppm",v); else if(s==1) snprintf(out,n,"%d%%",(int)lroundf(v/10.0f)); else snprintf(out,n,"%.1fC",v/10.0f); }
static void advAcoesTxt(char* out,size_t n,uint16_t mask){
  out[0]=0; size_t l=0;
  for(int ch=0;ch<AUTO_N;ch++){ int code=(mask>>(2*ch))&3; if(!code) continue; l+=snprintf(out+l,n-l,"%sCH%d:%s",l?" ":"",ch+1,code==1?"ON":"OFF"); if(l>=n-8) break; }
  if(!out[0]) strlcpy(out,"nada",n);
}
static void advResumo(int i,char* l1,size_t n1,char* l2,size_t n2){
  const RegraAv& r=adv[i]; char a[12],b[12],c[12],ac[40],ai[40];
  advValTxt(a,sizeof(a),r.s1,r.v1);
  int n=snprintf(l1,n1,"SE %s%s%s",ADV_SENSOR_C[r.s1],ADV_OP[r.op1],a);
  if(r.comb){ advValTxt(b,sizeof(b),r.s2,r.v2); n+=snprintf(l1+n,n1-n," %s %s%s%s",r.comb==1?"E":"OU",ADV_SENSOR_C[r.s2],ADV_OP[r.op2],b); }
  if(r.temOff){ advValTxt(c,sizeof(c),r.sOff,r.vOff); snprintf(l1+n,n1-n," ATE %s%s%s",ADV_SENSOR_C[r.sOff],ADV_OP[r.opOff],c); }
  advAcoesTxt(ac,sizeof(ac),r.acaoA); advAcoesTxt(ai,sizeof(ai),r.acaoI);
  if(r.acaoI) snprintf(l2,n2,"ENTAO %s | SENAO %s",ac,ai); else snprintf(l2,n2,"ENTAO %s",ac);
}
#define ADV_ROW_Y0 28
#define ADV_ROW_H  36
#define ADV_VIS    5
void drawAdv(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print("Regras avancadas (sensor -> reles)");
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  for(int i=0;i<ADV_VIS;i++){
    int k=advScroll+i; if(k>=advN) break;
    int y=ADV_ROW_Y0+i*ADV_ROW_H; char l1[64],l2[64]; advResumo(k,l1,sizeof(l1),l2,sizeof(l2));
    tft.fillRoundRect(4,y,282,ADV_ROW_H-3,5,rowBg);tft.drawRoundRect(4,y,282,ADV_ROW_H-3,5,adv[k].ativa?rowBrd:tft.color565(30,30,40));
    tft.setTextSize(1);tft.setTextColor(adv[k].ativa?TFT_WHITE:cSub,rowBg);tft.setCursor(9,y+6);tft.printf("%.46s",l1);
    tft.setTextColor(cSub,rowBg);tft.setCursor(9,y+19);tft.printf("%.46s",l2);
  }
  if(advN==0){ tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(30,90);tft.print("Nenhuma regra. Toque em + para criar.");
    tft.setCursor(14,108);tft.print("Ex.: SE T>=37.5 ENTAO CH1 liga ATE T<=37.3,"); tft.setCursor(14,120);tft.print("SENAO CH4 liga."); }
  if(advN<ADV_MAX){ tft.fillRoundRect(4,ADV_ROW_Y0+ADV_VIS*ADV_ROW_H+2,282,22,5,tft.color565(10,50,20));tft.drawRoundRect(4,ADV_ROW_Y0+ADV_VIS*ADV_ROW_H+2,282,22,5,cGreen);
    tft.setTextColor(cGreen,tft.color565(10,50,20));tft.setCursor(96,ADV_ROW_Y0+ADV_VIS*ADV_ROW_H+10);tft.print("+ Nova regra"); }
  bool cima=advScroll>0, baixo=advScroll<advN-ADV_VIS; uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.drawRoundRect(290,ADV_ROW_Y0,26,84,5,rowBrd);   tft.fillTriangle(303,ADV_ROW_Y0+22,294,ADV_ROW_Y0+56,312,ADV_ROW_Y0+56,cima?on:off);
  tft.drawRoundRect(290,ADV_ROW_Y0+90,26,84,5,rowBrd); tft.fillTriangle(303,ADV_ROW_Y0+90+60,294,ADV_ROW_Y0+90+26,312,ADV_ROW_Y0+90+26,baixo?on:off);
}
void abrirAdv(){ advScroll=0; appState=S_ADV; drawAdv(); }
void drawAdvEdit();
void handleAdvTap(int tx,int ty){
  if(ty<24){ if(tx<40){ appState=S_AUTO; autoPag=0; drawAuto(); } return; }
  if(tx>=288){ int maxS=advN-ADV_VIS; if(maxS<0) maxS=0; if(ty<ADV_ROW_Y0+88){ if(advScroll>0){ advScroll--; drawAdv(); } } else if(advScroll<maxS){ advScroll++; drawAdv(); } return; }
  int nb=ADV_ROW_Y0+ADV_VIS*ADV_ROW_H+2;
  if(advN<ADV_MAX && ty>=nb && ty<nb+24){ advDefaultRegra(adv[advN]); advSel=advN; advN++; advPag=0; appState=S_ADV_EDIT; drawAdvEdit(); return; }
  int row=(ty-ADV_ROW_Y0)/ADV_ROW_H, k=advScroll+row;
  if(ty>=ADV_ROW_Y0&&row<ADV_VIS&&k<advN){ advSel=k; advPag=0; appState=S_ADV_EDIT; drawAdvEdit(); }
}

// Editor: 6 paginas -- 0 condicao 1 · 1 condicao 2 · 2 "desligar quando" · 3 ENTAO · 4 SENAO · 5 janela/ativa
#define ADV_PAGS 6
static void advRow(int y,const char* lb,const char* val,int tipo){   // tipo 0 botao (toque cicla) · 1 stepper
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(8,y+12);tft.print(lb);
  if(tipo==0){ arqBotao(130,y,180,32,val,tft.color565(16,16,46),cClk,tft.color565(55,55,120)); return; }
  tft.fillRoundRect(128,y,36,32,6,cKey);tft.setTextColor(TFT_WHITE,cKey);tft.setTextSize(2);tft.setCursor(140,y+9);tft.print("-");
  tft.fillRoundRect(274,y,36,32,6,cKey);tft.setCursor(286,y+9);tft.print("+");
  tft.setTextColor(cClk,cBg);tft.setTextSize(2);tft.setCursor(170+(100-tft.textWidth(val))/2,y+9);tft.print(val);
}
static void advChips(uint16_t mask){
  static const char* nm[8]={"Resist.","Umidif.","B.agua","B.ar","LED","Audio","Cooler","Fans"};
  for(int i=0;i<8;i++){
    int col=i%4,row=i/4, x=6+col*78, y=54+row*62; int code=(mask>>(2*i))&3; bool trav=(i==COOLER_CH);
    uint16_t bg=trav?tft.color565(20,20,28):code==1?tft.color565(10,60,20):code==2?tft.color565(60,14,14):tft.color565(16,16,46);
    uint16_t br=trav?tft.color565(40,40,55):code==1?cGreen:code==2?tft.color565(220,80,80):tft.color565(55,55,120);
    tft.fillRoundRect(x,y,74,56,7,bg);tft.drawRoundRect(x,y,74,56,7,br);
    tft.setTextSize(1);tft.setTextColor(cSub,bg);tft.setCursor(x+6,y+6);tft.printf("CH%d",i+1);
    tft.setTextColor(TFT_WHITE,bg);tft.setCursor(x+6,y+18);tft.print(nm[i]);
    tft.setTextSize(2);tft.setTextColor(trav?tft.color565(70,70,85):code==1?cGreen:code==2?tft.color565(255,110,110):cSub,bg);
    const char* t=trav?"auto":code==1?"ON":code==2?"OFF":"--"; tft.setCursor(x+(74-tft.textWidth(t))/2,y+34);tft.print(t);
  }
}
void drawAdvEdit(){
  RegraAv& r=adv[advSel];
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Salvar");
  static const char* tit[ADV_PAGS]={"SE (condicao 1)","E/OU (condicao 2)","DESLIGAR quando","ENTAO (ativa)","SENAO (inativa)","Horario e estado"};
  char h[40]; snprintf(h,sizeof(h),"Regra %d - %s",advSel+1,tit[advPag]);
  tft.setTextColor(cLogo,cCard);tft.setCursor(70,10);tft.print(h);
  char v[20];
  if(advPag==0){
    advRow(36,"Sensor",ADV_SENSOR[r.s1],0); advRow(74,"Operador",ADV_OP[r.op1],0);
    advValTxt(v,sizeof(v),r.s1,r.v1); advRow(112,"Valor",v,1);
    static const char* cb[3]={"Nenhuma","E (as duas)","OU (uma)"}; advRow(150,"Combinar com 2a",cb[r.comb],0);
  }else if(advPag==1){
    if(!r.comb){ tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(10,70);tft.print("Sem 2a condicao. Na pagina anterior, em"); tft.setCursor(10,84);tft.print("\"Combinar com 2a\", escolha E ou OU."); }
    else{ advRow(36,"Sensor 2",ADV_SENSOR[r.s2],0); advRow(74,"Operador",ADV_OP[r.op2],0); advValTxt(v,sizeof(v),r.s2,r.v2); advRow(112,"Valor",v,1); }
  }else if(advPag==2){
    advRow(36,"Desligar",r.temOff?"Condicao propria":"Inverso da condicao",0);
    if(r.temOff){ advRow(74,"Sensor",ADV_SENSOR[r.sOff],0); advRow(112,"Operador",ADV_OP[r.opOff],0); advValTxt(v,sizeof(v),r.sOff,r.vOff); advRow(150,"Valor",v,1); }
    else{ tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(10,84);tft.print("Volta ao estado INATIVO assim que a condicao");tft.setCursor(10,96);tft.print("de ligar deixa de valer. Use \"Condicao propria\"");tft.setCursor(10,108);tft.print("para uma histerese (ex.: liga em T>=37.5 e");tft.setCursor(10,120);tft.print("so desliga em T<=37.3)."); }
  }else if(advPag==3||advPag==4){
    tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(8,36);tft.print(advPag==3?"Quando a regra ATIVA (toque cicla: -- / ON / OFF):":"Quando a regra fica INATIVA (senao):");
    advChips(advPag==3?r.acaoA:r.acaoI);
    tft.setTextSize(1);tft.setTextColor(tft.color565(110,110,150),cBg);tft.setCursor(8,182);tft.print("-- = nao mexe no rele. Um toque manual num rele");tft.setCursor(8,192);tft.print("pausa a automacao dele por 5 min.");
  }else{
    char a[10],b[10]; snprintf(a,sizeof(a),"%02u:%02u",r.hIni/60,r.hIni%60); snprintf(b,sizeof(b),"%02u:%02u",r.hFim/60,r.hFim%60);
    advRow(36,"Regra",r.ativa?"Ativa":"Desativada",0);
    advRow(74,"Janela das",a,1); advRow(112,"ate",b,1);
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(8,150);tft.print(r.hIni==r.hFim?"Janela igual = a regra vale o dia todo.":"A regra so liga dentro da janela.");
  }
  uint16_t on=cLogo, off=tft.color565(60,55,25), nb=tft.color565(30,30,70);
  tft.fillRoundRect(4,200,46,34,8,nb); tft.fillTriangle(16,217,32,207,32,227,advPag>0?on:off);
  arqBotao(54,200,104,34,"Salvar",tft.color565(10,70,30),cGreen,cGreen);
  arqBotao(162,200,104,34,"Apagar",tft.color565(70,14,14),TFT_WHITE,tft.color565(140,40,40));
  tft.fillRoundRect(270,200,46,34,8,nb); tft.fillTriangle(304,217,288,207,288,227,advPag<ADV_PAGS-1?on:off);
}
static void advSalvar(){ autoPersistir(); appState=S_ADV; drawAdv(); }
static void advNormVal(uint8_t s,int16_t& v){ v=(s==3)?1000:(s==1?550:375); }   // valor padrao do novo sensor (unidades diferentes)
static void advValAjusta(int16_t& v,int s,int dir){ int passo=(s==1)?10:(s==3?50:1); int n=v+dir*passo; int mn=(s==3)?300:(s==1?0:200), mx=(s==3)?5000:(s==1?1000:(s==2?1000:500)); if(n<mn) n=mn; if(n>mx) n=mx; v=(int16_t)n; }
void handleAdvEditTap(int tx,int ty){
  RegraAv& r=adv[advSel];
  if(ty<28){ if(tx<70) advSalvar(); return; }
  if(ty>=200&&ty<=234){
    if(tx<50){ if(advPag>0){ advPag--; drawAdvEdit(); } }
    else if(tx>=266){ if(advPag<ADV_PAGS-1){ advPag++; drawAdvEdit(); } }
    else if(tx>=54&&tx<=158) advSalvar();
    else if(tx>=162&&tx<=266){   // apagar
      for(int i=advSel;i<advN-1;i++) adv[i]=adv[i+1];
      advN--; memset(advLatch,0,sizeof(advLatch)); memset(advInit,0,sizeof(advInit));
      autoPersistir(); appState=S_ADV; drawAdv(); arqToast("Regra apagada",true); drawAdv();
    }
    return;
  }
  auto ciclo=[&](uint8_t& x,int n){ x=(uint8_t)((x+1)%n); };
  auto linha=[&](int y0)->bool{ return ty>=y0&&ty<=y0+32; };
  int dir=(tx>=120&&tx<=170)?-1:(tx>=268)?+1:0;
  if(advPag==0){
    if(linha(36)&&tx>=130){ ciclo(r.s1,4); advNormVal(r.s1,r.v1); }
    else if(linha(74)&&tx>=130) ciclo(r.op1,4);
    else if(linha(112)&&dir) advValAjusta(r.v1,r.s1,dir);
    else if(linha(150)&&tx>=130) ciclo(r.comb,3);
    else return;
  }else if(advPag==1){
    if(!r.comb) return;
    if(linha(36)&&tx>=130) ciclo(r.s2,4), advNormVal(r.s2,r.v2); else if(linha(74)&&tx>=130) ciclo(r.op2,4); else if(linha(112)&&dir) advValAjusta(r.v2,r.s2,dir); else return;
  }else if(advPag==2){
    if(linha(36)&&tx>=130) r.temOff=r.temOff?0:1;
    else if(r.temOff){ if(linha(74)&&tx>=130) ciclo(r.sOff,4), advNormVal(r.sOff,r.vOff); else if(linha(112)&&tx>=130) ciclo(r.opOff,4); else if(linha(150)&&dir) advValAjusta(r.vOff,r.sOff,dir); else return; }
    else return;
  }else if(advPag==3||advPag==4){
    if(ty<54||ty>=54+2*62) return;
    int col=(tx-6)/78, row=(ty-54)/62, i=row*4+col; if(col<0||col>3||i>=8||i==COOLER_CH) return;
    uint16_t& m=(advPag==3)?r.acaoA:r.acaoI; int code=(m>>(2*i))&3; code=(code+1)%3; m=(uint16_t)((m&~(3<<(2*i)))|(code<<(2*i)));
  }else{
    if(linha(36)&&tx>=130) r.ativa=r.ativa?0:1;
    else if(linha(74)&&dir) r.hIni=(uint16_t)((r.hIni+1440+dir*15)%1440);
    else if(linha(112)&&dir) r.hFim=(uint16_t)((r.hFim+1440+dir*15)%1440);
    else return;
  }
  drawAdvEdit();
}

// ═══════════════════════════════════════════════════════════════
// CICLO v2 (Fase 42) — wizard de inicio em 4 etapas, tela de ciclo ativo em
// tela cheia (Pausar / Parar / Lixeira) e CRUD de tipos de ovos.
//  * Parar so habilita a partir de "Ultimo dia comeca as" (inicio + (dias-1) dias).
//  * A duracao NAO e mais editavel na tela do ciclo: vem do tipo escolhido.
//  * Toda escrita usa o mesmo "logical clock" (lastLocalCicloActionMs) da Fase 12.
// ═══════════════════════════════════════════════════════════════
int cicloConfirm=0;   // 0 nada, 1 parar, 2 lixeira

// ── Hora local (o RTC guarda hora local; time() e UTC) ──
long tzOffsetSeg(){
  if(!rtcOK) return 0;
  time_t u=time(nullptr); if(u<(time_t)EPOCH_2024_01_01) return 0;
  long off=(long)rtc.now().unixtime()-(long)u;
  return lroundf(off/900.0f)*900L;   // fusos sao multiplos de 15min; absorve o drift do RTC
}
void fmtLocal(time_t utc,char* out,size_t n){
  time_t l=utc+tzOffsetSeg(); struct tm t; gmtime_r(&l,&t);
  snprintf(out,n,"%02d/%02d %02d:%02d",t.tm_mday,t.tm_mon+1,t.tm_hour,t.tm_min);
}
int cicloTotalDias(){ return fbCicloDuracaoDias>0?fbCicloDuracaoDias:21; }
time_t cicloUltimoDiaUtc(){
  time_t base=fbCicloIniciadoEm;
  if(fbCicloPausado) base+=time(nullptr)-fbCicloPausadoEm;   // pausa empurra o fim (Retomar desloca o inicio)
  return base+(time_t)(cicloTotalDias()-1)*86400;
}
bool cicloPodeParar(){ return fbCicloAtivo && time(nullptr)>=cicloUltimoDiaUtc(); }

// ═══════════════════════════════════════════════════════════════
// RELATORIO DO CICLO ENCERRADO (Fase 48)
// Enquanto ha ciclo ativo (e nao pausado) o firmware acumula estatisticas (min/max/media
// de temperatura e umidade, tempo fora da faixa, alertas, acionamentos do cooler,
// reinicios) e as guarda em /cicloStats.bin (LittleFS, a cada 5 min e nos eventos).
// Quando o ciclo termina -- Parar/Lixeira no aparelho OU no PWA (o poll do ciclo
// percebe) -- gera UM relatorio: arquivo /relatorios/AAAAMMDD_HHMM_lote.txt no SD e
// documento em chocadeiras/{id}/ciclos (o PWA lista e compartilha). Se um lote novo
// comeca sem o anterior ter sido encerrado, o anterior tambem e fechado.
// ═══════════════════════════════════════════════════════════════
struct CicloStats{
  uint32_t id; uint32_t inicio; uint32_t ultimoDia; uint8_t dias; uint8_t gerado; uint8_t avisoFim; uint8_t pausado; uint8_t excluido; uint8_t pad2[3];
  char nome[24]; char tipo[20];
  float tMin,tMax; double tSoma; uint32_t tN;
  float hMin,hMax; double hSoma; uint32_t hN;
  uint32_t segForaT,segForaH; uint16_t alertasT,alertasH,coolerOn,boots;
};
CicloStats cs;                 // .id==0 = sem ciclo registrado
bool csBootContado=false;
#define CS_PATH "/cicloStats.bin"
#define REL_DIR "/relatorios"

static void csZerar(uint32_t id){
  memset(&cs,0,sizeof(cs)); cs.id=id; cs.inicio=id;
  cs.tMin=cs.tMax=cs.hMin=cs.hMax=NAN;
}
void cicloStatsSalvar(){
  File f=LittleFS.open(CS_PATH,"w"); if(!f) return;
  f.write((const uint8_t*)&cs,sizeof(cs)); f.close();
}
void cicloStatsCarregar(){
  csZerar(0);
  File f=LittleFS.open(CS_PATH,"r"); if(!f) return;
  if(f.size()==sizeof(cs)){ CicloStats t; if(f.read((uint8_t*)&t,sizeof(t))==sizeof(t)) cs=t; }
  f.close();
}
static bool csAtivo(){ return fbCicloAtivo && !fbCicloPausado && cs.id!=0 && !cs.gerado; }
void cicloContaAlerta(bool temp){ if(csAtivo()){ if(temp) cs.alertasT++; else cs.alertasH++; } }
void cicloContaCooler(){ if(csAtivo()) cs.coolerOn++; }

static void fmtLocalCompleto(time_t utc,char* out,size_t n){
  time_t l=utc+tzOffsetSeg(); struct tm t; gmtime_r(&l,&t);
  snprintf(out,n,"%02d/%02d/%04d %02d:%02d",t.tm_mday,t.tm_mon+1,t.tm_year+1900,t.tm_hour,t.tm_min);
}
void cicloRelatorioGerar();
// Chamado a cada leitura valida do SHT31 (5s).
void cicloStatsAmostra(float t,float h){
  if(!fbCicloAtivo||fbCicloPausado||fbCicloPreparo||fbCicloIniciadoEm<=0) return;   // Fase 77: preparo (sem ovos) nao entra no relatorio
  if(cs.id!=(uint32_t)fbCicloIniciadoEm){
    // "Retomar" DESLOCA o inicio do ciclo (pausa empurra o fim) -- mesmo lote, nao e ciclo novo
    bool retomada = cs.id!=0 && !cs.gerado && cs.pausado && strcmp(cs.nome,fbCicloNome[0]?fbCicloNome:"Lote")==0 && strcmp(cs.tipo,fbCicloTipo)==0;
    if(retomada){ cs.id=(uint32_t)fbCicloIniciadoEm; cs.pausado=0; }
    else{
      if(cs.id!=0&&!cs.gerado) cicloRelatorioGerar();   // lote anterior ficou sem relatorio
      csZerar((uint32_t)fbCicloIniciadoEm); csBootContado=true; cicloStatsSalvar();
    }
  }else if(!csBootContado){ csBootContado=true; cs.boots++; }   // reiniciou durante o ciclo
  cs.ultimoDia=(uint32_t)cicloUltimoDiaUtc();
  cs.dias=(uint8_t)cicloTotalDias();
  strlcpy(cs.nome,fbCicloNome[0]?fbCicloNome:"Lote",sizeof(cs.nome)); strlcpy(cs.tipo,fbCicloTipo,sizeof(cs.tipo));
  if(isnan(cs.tMin)||t<cs.tMin) cs.tMin=t; if(isnan(cs.tMax)||t>cs.tMax) cs.tMax=t; cs.tSoma+=t; cs.tN++;
  if(isnan(cs.hMin)||h<cs.hMin) cs.hMin=h; if(isnan(cs.hMax)||h>cs.hMax) cs.hMax=h; cs.hSoma+=h; cs.hN++;
  if(tampaEst==0){   // Fase 82: o tempo com a tampa aberta nao conta como "fora da faixa" no relatorio
    if(t<TEMP_IDEAL_MIN||t>TEMP_IDEAL_MAX) cs.segForaT+=SHT31_READ_INTERVAL_MS/1000;
    if(h<UMID_IDEAL_MIN||h>UMID_IDEAL_MAX) cs.segForaH+=SHT31_READ_INTERVAL_MS/1000;
  }
}
static void relHM(char* out,size_t n,uint32_t seg){ snprintf(out,n,"%luh %02lum",(unsigned long)(seg/3600),(unsigned long)((seg%3600)/60)); }
static void relSanitiza(const char* in,char* out,size_t n){
  size_t k=0; for(const char* p=in;*p&&k<n-1;p++){ char c=*p; out[k++]=(isalnum((unsigned char)c))?c:'_'; } out[k]=0;
}
void fbRelCallback(AsyncResult &aResult){
  if(aResult.isError()) Serial.printf("[Firebase Relatorio ERRO] %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
}
static bool fimTipoLixeira(){   // o PWA escreve "lixeira|<ms>" em cmd.fimTipo ao excluir o lote
  if(!fimTipoRecebido.startsWith("lixeira|")) return false;
  unsigned long long ms=strtoull(fimTipoRecebido.c_str()+8,nullptr,10); time_t t=(time_t)(ms/1000ULL), agora=time(nullptr);
  return t>=(time_t)cs.inicio && agora>=t && (agora-t)<6*3600;
}
void cicloRelatorioGerar(){
  if(cs.id==0||cs.gerado||cs.tN==0){ if(cs.id!=0&&cs.tN==0){ cs.gerado=1; cicloStatsSalvar(); } return; }   // nada medido -> sem relatorio
  time_t fim=time(nullptr); bool horaOk=fim>=(time_t)EPOCH_2024_01_01; if(!horaOk) fim=(time_t)cs.inicio;
  time_t ultimo=cs.ultimoDia?(time_t)cs.ultimoDia:(time_t)cs.inicio+(time_t)(cs.dias>0?cs.dias-1:20)*86400;
  bool excluido=(cs.excluido||fimTipoLixeira());
  bool concluido=!excluido && horaOk && fim>=ultimo;
  int diaFim=(int)((fim-(time_t)cs.inicio)/86400)+1; if(diaFim<1) diaFim=1; if(diaFim>cs.dias) diaFim=cs.dias;
  char ini[20],fimS[20]; fmtLocalCompleto((time_t)cs.inicio,ini,sizeof(ini)); fmtLocalCompleto(fim,fimS,sizeof(fimS));
  float tMed=cs.tN?(float)(cs.tSoma/cs.tN):NAN, hMed=cs.hN?(float)(cs.hSoma/cs.hN):NAN;
  char fT[16],fH[16]; relHM(fT,sizeof(fT),cs.segForaT); relHM(fH,sizeof(fH),cs.segForaH);
  String txt; txt.reserve(700); char ln[80];
  txt+="RELATORIO DO CICLO\n";
  snprintf(ln,sizeof(ln),"Lote: %s\n",cs.nome); txt+=ln;
  snprintf(ln,sizeof(ln),"Tipo: %s (%u dias)\n",cs.tipo[0]?cs.tipo:"-",(unsigned)cs.dias); txt+=ln;
  snprintf(ln,sizeof(ln),"Inicio: %s\n",ini); txt+=ln;
  snprintf(ln,sizeof(ln),"Fim:    %s\n",fimS); txt+=ln;
  if(excluido) snprintf(ln,sizeof(ln),"Situacao: Excluido (lixeira, dia %d de %u)\n",diaFim,(unsigned)cs.dias);
  else if(concluido) snprintf(ln,sizeof(ln),"Situacao: Concluido (%u dias)\n",(unsigned)cs.dias);
  else snprintf(ln,sizeof(ln),"Situacao: Interrompido (dia %d de %u)\n",diaFim,(unsigned)cs.dias);
  txt+=ln; txt+="\n";
  snprintf(ln,sizeof(ln),"Temperatura (C)  min %.1f  max %.1f  med %.1f\n",cs.tMin,cs.tMax,tMed); txt+=ln;
  snprintf(ln,sizeof(ln),"Umidade (%%)      min %.0f  max %.0f  med %.0f\n",cs.hMin,cs.hMax,hMed); txt+=ln;
  snprintf(ln,sizeof(ln),"Fora da faixa: temp %s, umid %s\n",fT,fH); txt+=ln;
  snprintf(ln,sizeof(ln),"Alertas: temp %u, umid %u\n",(unsigned)cs.alertasT,(unsigned)cs.alertasH); txt+=ln;
  snprintf(ln,sizeof(ln),"Cooler do sistema ligou %u vez(es)\n",(unsigned)cs.coolerOn); txt+=ln;
  snprintf(ln,sizeof(ln),"Reinicios do aparelho: %u\n",(unsigned)cs.boots); txt+=ln;
  snprintf(ln,sizeof(ln),"Amostras: %lu (a cada %us)\n",(unsigned long)cs.tN,(unsigned)(SHT31_READ_INTERVAL_MS/1000)); txt+=ln;

  if(sdOK){
    if(!SD.exists(REL_DIR)) SD.mkdir(REL_DIR);
    time_t l=fim+tzOffsetSeg(); struct tm t; gmtime_r(&l,&t);
    char nm[24]; relSanitiza(cs.nome,nm,sizeof(nm));
    char p[80]; snprintf(p,sizeof(p),"%s/%04d%02d%02d_%02d%02d_%s.txt",REL_DIR,t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,nm);
    if(SD.exists(p)) snprintf(p,sizeof(p),"%s/%04d%02d%02d_%02d%02d%02d_%s.txt",REL_DIR,t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec,nm);
    File f=SD.open(p,FILE_WRITE); if(f){ f.print(txt); f.close(); Serial.printf("[Relatorio] gravado em %s\n",p); arqIndiceSujo=true; }
  }
  if(fbStarted&&fbApp.ready()&&horaOk){
    struct tm* u; char tsI[25],tsF[25]; time_t ii=(time_t)cs.inicio; u=gmtime(&ii); strftime(tsI,sizeof(tsI),"%Y-%m-%dT%H:%M:%SZ",u); u=gmtime(&fim); strftime(tsF,sizeof(tsF),"%Y-%m-%dT%H:%M:%SZ",u);
    Document<Values::Value> doc("nome",Values::Value(Values::StringValue(jsonEsc(String(cs.nome)))));
    doc.add("tipo",Values::Value(Values::StringValue(jsonEsc(String(cs.tipo)))));
    doc.add("inicio",Values::Value(Values::TimestampValue(String(tsI))));
    doc.add("fim",Values::Value(Values::TimestampValue(String(tsF))));
    doc.add("dias",Values::Value(Values::IntegerValue((int)cs.dias)));
    doc.add("situacao",Values::Value(Values::StringValue(String(excluido?"excluido":(concluido?"concluido":"interrompido")))));
    doc.add("tMin",Values::Value(Values::DoubleValue(number_t(cs.tMin,1))));
    doc.add("tMax",Values::Value(Values::DoubleValue(number_t(cs.tMax,1))));
    doc.add("tMed",Values::Value(Values::DoubleValue(number_t(tMed,1))));
    doc.add("hMin",Values::Value(Values::DoubleValue(number_t(cs.hMin,1))));
    doc.add("hMax",Values::Value(Values::DoubleValue(number_t(cs.hMax,1))));
    doc.add("hMed",Values::Value(Values::DoubleValue(number_t(hMed,1))));
    doc.add("foraTempMin",Values::Value(Values::IntegerValue((int)(cs.segForaT/60))));
    doc.add("foraUmidMin",Values::Value(Values::IntegerValue((int)(cs.segForaH/60))));
    doc.add("alertasTemp",Values::Value(Values::IntegerValue((int)cs.alertasT)));
    doc.add("alertasUmid",Values::Value(Values::IntegerValue((int)cs.alertasH)));
    doc.add("coolerOn",Values::Value(Values::IntegerValue((int)cs.coolerOn)));
    doc.add("reinicios",Values::Value(Values::IntegerValue((int)cs.boots)));
    doc.add("amostras",Values::Value(Values::IntegerValue((int)cs.tN)));
    doc.add("texto",Values::Value(Values::StringValue(jsonEsc(txt))));
    String cpath=String(FIRESTORE_DOCUMENT_PATH)+"/ciclos";
    fbDocs.createDocument(fbClient,Firestore::Parent(FIREBASE_PROJECT_ID),cpath,DocumentMask(),doc,fbRelCallback,"fbRelTask");
  }else Serial.println("[Relatorio] sem nuvem agora -- relatorio so no SD.");
  cicloIdxGravar((time_t)cs.inicio,fim,excluido?'x':(concluido?'c':'i'),cs.nome);   // Fase 55: indice do historico por ciclo
  char msg[64]; snprintf(msg,sizeof(msg),"Relatorio do lote '%s' gerado (%s)",cs.nome,excluido?"excluido":(concluido?"concluido":"interrompido"));
  logEvento("Ciclo",msg,true);
  cs.gerado=1; cicloStatsSalvar();
}
// Chamado de loop() a cada 5s.
void cicloRelatorioServico(){
  static unsigned long ultSalva=0;
  if(cicloPollRecebido && !fbCicloAtivo && cs.id!=0 && !cs.gerado) cicloRelatorioGerar();   // ciclo terminou (aqui ou no PWA)
  if(fbCicloAtivo && cs.id!=0 && !cs.gerado){
    if(fbCicloPausado) cs.pausado=1;
    else cs.ultimoDia=(uint32_t)cicloUltimoDiaUtc();
    if(millis()-ultSalva>5UL*60UL*1000UL){ ultSalva=millis(); cicloStatsSalvar(); }
    if(!cs.avisoFim && cicloPodeParar()){   // 1o instante do "ultimo dia": avisa (push)
      cs.avisoFim=1; cicloStatsSalvar();
      char m[64]; snprintf(m,sizeof(m),"Lote %s: ja pode encerrar o ciclo.",fbCicloNome[0]?fbCicloNome:"");
      pushEnfileirar(2,"ChocaIoT: ultimo dia do ciclo",m);
    }
  }
}

// ── Lista de relatorios (Config. > Relatorios) ──
#define REL_MAX 30
char relNome[REL_MAX][44]; int relN=0, relScroll=0;
static void relCarregar(){
  relN=0; if(!sdOK) return;
  File d=SD.open(REL_DIR); if(!d||!d.isDirectory()){ if(d) d.close(); return; }
  File f;
  while(relN<REL_MAX && (f=d.openNextFile())){
    String n=f.name(); int i=n.lastIndexOf('/'); if(i>=0) n=n.substring(i+1);
    if(!f.isDirectory() && n.endsWith(".txt") && n.length()<44){ strlcpy(relNome[relN],n.c_str(),44); relN++; }
    f.close();
  }
  d.close();
  for(int i=1;i<relN;i++){ char t[44]; strcpy(t,relNome[i]); int j=i-1; while(j>=0&&strcmp(relNome[j],t)<0){ strcpy(relNome[j+1],relNome[j]); j--; } strcpy(relNome[j+1],t); }   // mais recente primeiro
}
void abrirRelatorios(){ relScroll=0; relCarregar(); appState=S_RELS; drawRels(); }
void drawRels(){
  uint16_t rowBg=tft.color565(16,16,46), rowBrd=tft.color565(46,46,90);
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(30,8);tft.print("Relatorios de ciclos encerrados");
  if(!sdOK){ tft.setTextColor(cSub,cBg);tft.setCursor(80,110);tft.print("Cartao SD indisponivel"); return; }
  if(relN==0){ tft.setTextColor(cSub,cBg);tft.setCursor(40,104);tft.print("Nenhum relatorio ainda.");tft.setCursor(40,120);tft.print("Sao gerados ao encerrar um ciclo."); }
  for(int i=0;i<ARQ_VIS;i++){
    int idx=relScroll+i; if(idx>=relN) break;
    int y=ARQ_ROW_Y0+i*ARQ_ROW_H;
    tft.fillRoundRect(4,y,278,ARQ_ROW_H-2,5,rowBg);tft.drawRoundRect(4,y,278,ARQ_ROW_H-2,5,rowBrd);
    const char* n=relNome[idx]; char dh[24]="";   // AAAAMMDD_HHMM_lote.txt -> dd/mm/aaaa hh:mm  lote
    if(strlen(n)>13 && n[8]=='_' && n[13]=='_') snprintf(dh,sizeof(dh),"%.2s/%.2s/%.4s %.2s:%.2s",n+6,n+4,n,n+9,n+11);
    char lote[26]; const char* lp=(strlen(n)>14)?n+14:n; strlcpy(lote,lp,sizeof(lote)); char* pt=strrchr(lote,'.'); if(pt) *pt=0;
    tft.setTextColor(cClk,rowBg);tft.setTextSize(1);tft.setCursor(10,y+10);tft.print(dh);
    tft.setTextColor(TFT_WHITE,rowBg);tft.setCursor(104,y+10);tft.print(lote);
  }
  bool podeCima=relScroll>0, podeBaixo=relScroll<relN-ARQ_VIS;
  uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.drawRoundRect(288,ARQ_ROW_Y0,30,100,5,rowBrd);
  tft.fillTriangle(303,ARQ_ROW_Y0+34,292,ARQ_ROW_Y0+66,314,ARQ_ROW_Y0+66,podeCima?on:off);
  tft.drawRoundRect(288,ARQ_ROW_Y0+108,30,100,5,rowBrd);
  tft.fillTriangle(303,ARQ_ROW_Y0+108+66,292,ARQ_ROW_Y0+108+34,314,ARQ_ROW_Y0+108+34,podeBaixo?on:off);
}
void handleRelsTap(int tx,int ty){
  if(ty<24&&tx<40){ appState=S_SETTINGS; drawSettingsPanel(); return; }
  if(tx>=286){
    int maxS=relN-ARQ_VIS; if(maxS<0) maxS=0;
    if(ty<ARQ_ROW_Y0+104){ if(relScroll>0){ relScroll--; drawRels(); } }
    else if(relScroll<maxS){ relScroll++; drawRels(); }
    return;
  }
  int row=(ty-ARQ_ROW_Y0)/ARQ_ROW_H, idx=relScroll+row;
  if(row<0||row>=ARQ_VIS||idx>=relN) return;
  char p[80]; snprintf(p,sizeof(p),"%s/%s",REL_DIR,relNome[idx]);
  abrirTextoDoSD(p,"Relatorio do ciclo",S_RELS,false);
}


// ── Teclado de texto generico (wizard: nome do lote; tipos: nome do tipo) ──
char* kbTxtBuf=nullptr; int kbTxtMax=0, kbTxtLen=0, kbTxtEtapa=0;
const char* kbTxtTitulo=""; const char* kbTxtDica="";
void (*kbTxtOk)()=nullptr; void (*kbTxtVoltar)()=nullptr;
void drawKbTxtField(){
  tft.fillRect(5,40,310,24,tft.color565(5,5,18));
  tft.drawRect(5,40,310,24,tft.color565(75,75,170));
  tft.setTextColor(TFT_WHITE,tft.color565(5,5,18));tft.setTextSize(1);
  tft.setCursor(9,48);tft.print(kbTxtBuf);
  tft.drawFastVLine(9+kbTxtLen*6,42,18,TFT_WHITE);
}
void drawKbTxtScreen(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,28,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(1);tft.setCursor(5,10);tft.print("< Voltar");
  tft.setTextColor(cLogo,cCard);tft.setCursor(70,10);tft.print(kbTxtTitulo);
  if(kbTxtEtapa>0){ for(int i=0;i<4;i++) tft.fillRect(8+i*77,28,73,3,i<kbTxtEtapa?cLogo:tft.color565(36,36,78)); }
  tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(5,32);tft.print(kbTxtDica);
  drawKbTxtField();
  drawKeyboard();
}
void abrirKbTxt(const char* titulo,const char* dica,int etapa,char* buf,int max,void(*ok)(),void(*voltar)()){
  kbTxtTitulo=titulo; kbTxtDica=dica; kbTxtEtapa=etapa; kbTxtBuf=buf; kbTxtMax=max; kbTxtLen=strlen(buf); kbTxtOk=ok; kbTxtVoltar=voltar;
  kbMode=0; appState=S_KBTXT; drawKbTxtScreen();
}
static void kbTxtAdd(char c){ if(kbTxtLen<kbTxtMax){ kbTxtBuf[kbTxtLen++]=c; kbTxtBuf[kbTxtLen]=0; } drawKbTxtField(); }
static void kbTxtDel(){ if(kbTxtLen>0) kbTxtBuf[--kbTxtLen]=0; drawKbTxtField(); }
static void sanitizarNome(char* s){   // ; | " \ quebrariam o formato compacto / o parse do REST
  for(char* p=s;*p;p++) if(strchr(";|\"\\",*p)) *p='-';
  int n=strlen(s); while(n>0&&s[n-1]==' ') s[--n]=0;
  int i=0; while(s[i]==' ') i++; if(i) memmove(s,s+i,strlen(s+i)+1);
}
// Espelha handleKbTap() (mesmo grid); duplicado de proposito (ver handleLogsSearchKbTap).
void handleKbTxtTap(int tx,int ty){
  if(ty<28&&tx<62){ if(kbTxtVoltar) kbTxtVoltar(); return; }
  const char(*KB)[11]=(kbMode==0?KBL:(kbMode==1?KBU:KBN));
  if(ty>=KR1Y&&ty<KR1Y+KH){int ki=(tx-5)/31;if(ki>=0&&ki<10) kbTxtAdd(KB[0][ki]);}
  else if(ty>=KR2Y&&ty<KR2Y+KH){int ki=(tx-20)/31;if(ki>=0&&ki<9) kbTxtAdd(KB[1][ki]);}
  else if(ty>=KR3Y&&ty<KR3Y+KH){if(tx<50){kbMode=(kbMode==0)?1:(kbMode==1)?0:2;drawKeyboard();return;}else if(tx>=262) kbTxtDel();else{int ki=(tx-50)/30;if(ki>=0&&ki<7) kbTxtAdd(KB[2][ki]);}}
  else if(ty>=KR4Y){
    if(tx<63){kbMode=(kbMode==2)?0:2;drawKeyboard();return;}
    else if(tx>=257){ if(kbTxtOk) kbTxtOk(); return; }
    else kbTxtAdd(' ');
  }
}

// ── Botoes com icone (Pausar/Retomar, Parar, Lixeira) ──
static void cicloIcone(int tipo,int cx,int cy,uint16_t c){
  if(tipo==0){ tft.fillRect(cx-12,cy-11,8,22,c); tft.fillRect(cx+4,cy-11,8,22,c); }                                   // pausa
  else if(tipo==3){ tft.fillTriangle(cx-8,cy-12,cx-8,cy+12,cx+12,cy,c); }                                             // retomar
  else if(tipo==1){ tft.fillRect(cx-11,cy-11,22,22,c); }                                                              // parar
  else { tft.fillRect(cx-12,cy-12,24,3,c); tft.fillRect(cx-5,cy-15,10,3,c); tft.fillRect(cx-9,cy-8,18,20,c);          // lixeira
         for(int k=-1;k<=1;k++) tft.drawFastVLine(cx+k*5,cy-5,14,tft.color565(20,20,30)); }
}
// tipo: 0 pausar/retomar, 1 parar, 2 lixeira. press=true desenha o "efeito" (clarao) do toque.
static void cicloBotao(int x,int tipo,bool press){
  int y=168,w=96,h=64;
  bool retomar=(tipo==0&&fbCicloPausado);
  uint16_t bg,fg; const char* lb;
  if(tipo==0){ bg=tft.color565(58,40,6); fg=tft.color565(255,190,60); lb=retomar?"Retomar":"Pausar"; }
  else if(tipo==1){
    if(cicloPodeParar()){ bg=tft.color565(60,14,14); fg=tft.color565(255,95,95); }
    else { bg=tft.color565(20,20,28); fg=tft.color565(70,70,85); }
    lb="Parar";
  } else { bg=tft.color565(36,36,70); fg=tft.color565(175,175,235); lb="Lixeira"; }
  if(press){ bg=tft.color565(min(255,((bg>>11)&31)*8+90),min(255,((bg>>5)&63)*4+90),min(255,(bg&31)*8+90)); }
  tft.fillRoundRect(x,y,w,h,10,bg);
  cicloIcone(tipo==0?(retomar?3:0):tipo,x+w/2,y+26,press?TFT_WHITE:fg);
  tft.setTextColor(press?TFT_WHITE:fg,bg);tft.setTextSize(1);
  tft.setCursor(x+(w-tft.textWidth(lb))/2,y+50);tft.print(lb);
  if(tipo==1&&!cicloPodeParar()){ tft.drawLine(x+w-22,y+8,x+w-10,y+8,fg); tft.drawRect(x+w-20,y+11,8,6,fg); }   // cadeadinho discreto
}

// ── Marcos do ciclo (Fase 54): ovoscopias e inicio da eclosao, desenhados na barra de progresso ──
struct CicloMarco{ int dia; uint8_t tipo; };   // tipo: 0 ovoscopia, 1 eclosao
static int cicloMarcos(CicloMarco* m){
  const TipoOvo* t=cicloTipoAtualPtr(); int n=0; if(!t) return 0;
  int total=cicloTotalDias();
  if(t->ovo1>0 && t->ovo1<=total) m[n++]={t->ovo1,0};
  if(t->ovo2>0 && t->ovo2<=total && t->ovo2!=t->ovo1) m[n++]={t->ovo2,0};
  if(t->eclDia>0 && t->eclDia<=total) m[n++]={t->eclDia,1};
  return n;
}
// estagio pela cor: 0 antes da 1a ovoscopia · 1 apos 1a · 2 apos 2a · 3 eclosao
static int cicloEstagio(int dia){
  const TipoOvo* t=cicloTipoAtualPtr(); if(!t) return 0;
  if(t->eclDia && dia>=t->eclDia) return 3;
  if(t->ovo2 && dia>=t->ovo2) return 2;
  if(t->ovo1 && dia>=t->ovo1) return 1;
  return 0;
}
static uint16_t cicloCorEstagio(int e){
  switch(e){ case 1: return tft.color565(70,210,255); case 2: return tft.color565(255,165,40); case 3: return tft.color565(255,95,150); default: return cGreen; }
}
static bool cicloPulsoOn=false;
static float cicloFrac(){
  time_t ref=fbCicloPausado?fbCicloPausadoEm:time(nullptr);
  float frac=(float)(ref-fbCicloIniciadoEm)/(float)((long)cicloTotalDias()*86400L); if(frac<0)frac=0; if(frac>1)frac=1;
  return frac;
}
#define CICLO_BAR_Y 92
// Barra + marcos + rotulos (regiao y 84..130). Marco ja passado = losango cheio com halo; o PROXIMO pulsa.
static void cicloDesenhaBarra(bool pulso){
  int total=cicloTotalDias(), dia=calcularDiaCicloAtual(); float frac=cicloFrac();
  uint16_t cor=fbCicloPausado?tft.color565(150,150,160):cicloCorEstagio(cicloEstagio(dia));
  tft.fillRect(0,82,320,50,cBg);
  tft.fillRoundRect(8,CICLO_BAR_Y,304,14,7,tft.color565(30,30,60));
  int fw=(int)(304*frac); if(fw>=8) tft.fillRoundRect(8,CICLO_BAR_Y,fw,14,7,cor); else if(fw>0) tft.fillRect(8,CICLO_BAR_Y,fw,14,cor);
  CicloMarco mk[3]; int n=cicloMarcos(mk); int proximo=-1;
  for(int i=0;i<n;i++) if(dia<mk[i].dia){ proximo=i; break; }
  int fimUltimoRotulo[2]={0,0};
  for(int i=0;i<n;i++){
    int x=8+(int)(304.0f*(mk[i].dia-1)/total); int y=CICLO_BAR_Y+7;
    bool passou=(dia>=mk[i].dia);
    uint16_t c=cicloCorEstagio(mk[i].tipo==1?3:(i==0?1:2));
    if(passou){
      tft.drawCircle(x,y,10,tft.color565(((c>>11)&31)*3,((c>>5)&63)*2,(c&31)*3));   // halo suave
      tft.fillTriangle(x,y-7,x+7,y,x,y+7,c); tft.fillTriangle(x,y-7,x-7,y,x,y+7,c);
      tft.drawFastVLine(x,y-3,7,TFT_WHITE);
    }else{
      uint16_t anel=(i==proximo&&pulso)?c:tft.color565(((c>>11)&31)*4,((c>>5)&63)*3,(c&31)*4);
      if(i==proximo){ tft.drawCircle(x,y,pulso?11:9,anel); tft.drawCircle(x,y,pulso?12:10,anel); }   // pulsa
      tft.fillTriangle(x,y-7,x+7,y,x,y+7,cBg); tft.fillTriangle(x,y-7,x-7,y,x,y+7,cBg);
      tft.drawLine(x,y-7,x+7,y,c); tft.drawLine(x+7,y,x,y+7,c); tft.drawLine(x,y+7,x-7,y,c); tft.drawLine(x-7,y,x,y-7,c);
    }
    char lb[16]; if(mk[i].tipo==1) snprintf(lb,sizeof(lb),"Eclos. d%d",mk[i].dia); else snprintf(lb,sizeof(lb),"Ovosc. d%d",mk[i].dia);
    tft.setTextSize(1); int w=tft.textWidth(lb); int lx=x-w/2;   // tamanho ANTES de medir (senao a largura sai da fonte anterior) if(lx<4) lx=4; if(lx+w>316) lx=316-w;
    int linha=(lx<fimUltimoRotulo[0]+4)?1:0; fimUltimoRotulo[linha]=lx+w;
    tft.setTextSize(1);tft.setTextColor(passou?c:tft.color565(150,150,170),cBg);tft.setCursor(lx,CICLO_BAR_Y+20+linha*11);tft.print(lb);
  }
}
// ── Tela do ciclo (Fase 54: sem "<" e sem textos repetidos; toque acima dos botoes volta ao inicio) ──
void drawCicloScreen(){
  tft.fillScreen(cBg);
  if(!fbCicloAtivo){
    tft.setTextColor(cSub,cBg);tft.setTextSize(2);tft.setCursor(50,72);tft.print("Nenhum ciclo ativo");
    tft.fillRoundRect(40,116,240,60,12,tft.color565(10,70,30));
    tft.setTextColor(cGreen,tft.color565(10,70,30));tft.setTextSize(2);tft.setCursor(64,138);tft.print("Iniciar novo ciclo");
    tft.setTextColor(tft.color565(90,90,120),cBg);tft.setTextSize(1);tft.setCursor(70,214);tft.print("Toque fora do botao para voltar");
    return;
  }
  if(cicloConfirm){
    bool lix=(cicloConfirm==2);
    tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(2);tft.setCursor(8,40);tft.print(lix?"Excluir o lote?":"Encerrar o ciclo?");
    char nm[26]; strlcpy(nm,fbCicloNome[0]?fbCicloNome:"Lote",26);
    tft.setTextColor(cLogo,cBg);tft.setTextSize(1);tft.setCursor(8,72);tft.print(nm);
    tft.setTextColor(cSub,cBg);tft.setCursor(8,90);
    tft.print(lix?"Todo o progresso deste lote sera perdido.":"O lote sera finalizado e o ciclo removido do painel.");
    tft.fillRoundRect(16,150,136,64,10,tft.color565(20,20,46));
    tft.setTextColor(TFT_WHITE,tft.color565(20,20,46));tft.setTextSize(2);tft.setCursor(16+(136-tft.textWidth("Cancelar"))/2,174);tft.print("Cancelar");
    tft.fillRoundRect(168,150,136,64,10,tft.color565(70,14,14));
    const char* t2=lix?"Excluir":"Parar";
    tft.setTextColor(TFT_WHITE,tft.color565(70,14,14));tft.setCursor(168+(136-tft.textWidth(t2))/2,174);tft.print(t2);
    return;
  }
  if(fbCicloPreparo){ drawStatusDetalhe(true); return; }   // Fase 77: preparo = a mesma janela do card Status (com "Ovos colocados")
  int dia=calcularDiaCicloAtual(), total=cicloTotalDias();
  int pct=(int)(cicloFrac()*100.0f+0.5f);
  uint16_t cor=fbCicloPausado?tft.color565(150,150,160):cicloCorEstagio(cicloEstagio(dia));

  // linha unica de identificacao (nome do lote + tipo) -- sem repetir dias/porcentagem
  char nm[40]; snprintf(nm,sizeof(nm),"%s  -  %s",fbCicloNome[0]?fbCicloNome:"Ciclo",fbCicloTipo[0]?fbCicloTipo:"tipo nao informado");
  tft.setTextColor(cLogo,cBg);tft.setTextSize(1);tft.setCursor(8,6);tft.print(nm);
  if(fbCicloPausado){ tft.setTextColor(tft.color565(230,180,60),cBg);tft.setCursor(316-tft.textWidth("PAUSADO"),6);tft.print("PAUSADO"); }

  // numero do dia (grande, muda de cor a cada marco) e porcentagem
  char dn[6]; snprintf(dn,sizeof(dn),"%d",dia);
  tft.setTextColor(cor,cBg);tft.setTextSize(6);tft.setCursor(8,24);tft.print(dn);
  int xd=8+tft.textWidth(dn);
  char dt[8]; snprintf(dt,sizeof(dt),"/%d",total);
  tft.setTextColor(tft.color565(110,115,140),cBg);tft.setTextSize(3);tft.setCursor(xd+4,50);tft.print(dt);
  char pp[8]; snprintf(pp,sizeof(pp),"%d%%",pct);
  tft.setTextColor(cor,cBg);tft.setTextSize(4);tft.setCursor(312-tft.textWidth(pp),40);tft.print(pp);
  cicloDesenhaBarra(cicloPulsoOn);

  // proximo marco / datas
  char a[20]; fmtLocal(fbCicloIniciadoEm,a,20);
  tft.setTextSize(1);
  tft.setTextColor(cSub,cBg);tft.setCursor(8,136);tft.print("Inicio");
  tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(58,136);tft.print(a);
  CicloMarco mk[3]; int n=cicloMarcos(mk); int prox=-1;
  for(int i=0;i<n;i++) if(dia<mk[i].dia){ prox=i; break; }
  if(prox>=0){
    time_t quando=fbCicloIniciadoEm+(time_t)(mk[prox].dia-1)*86400+(fbCicloPausado?time(nullptr)-fbCicloPausadoEm:0);
    char q[20]; fmtLocal(quando,q,20); long dif=(long)(quando-time(nullptr)); if(dif<0) dif=0;
    char r[64]; snprintf(r,sizeof(r),"%s dia %d: %s (em %ldd %ldh)",mk[prox].tipo==1?"Eclosao":"Ovoscopia",mk[prox].dia,q,dif/86400,(dif%86400)/3600);
    tft.setTextColor(cSub,cBg);tft.setCursor(8,148);tft.print("Proximo");
    tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(58,148);tft.print(r);
  }
  if(fbCicloPausado){ tft.setTextColor(tft.color565(230,180,60),cBg);tft.setCursor(8,158);tft.print("Ciclo pausado - a contagem esta parada"); }
  else if(cicloPodeParar()){ tft.setTextColor(cGreen,cBg);tft.setCursor(8,158);tft.print("Ultimo dia: Parar liberado"); }
  else{
    long dif=(long)(cicloUltimoDiaUtc()-time(nullptr)); if(dif<0) dif=0;
    char r[40]; snprintf(r,40,"Parar libera em %ldd %ldh %ldmin",dif/86400,(dif%86400)/3600,(dif%3600)/60);
    tft.setTextColor(cSub,cBg);tft.setCursor(8,158);tft.print(r);
  }
  cicloBotao(8,0,false); cicloBotao(112,1,false); cicloBotao(216,2,false);
}
// Pulso do proximo marco (chamado do loop a cada ~600 ms so na tela do ciclo)
void cicloPulso(){
  if(appState!=S_CICLO||!fbCicloAtivo||cicloConfirm||fbCicloPreparo) return;
  cicloPulsoOn=!cicloPulsoOn; cicloDesenhaBarra(cicloPulsoOn);
}
void cicloAcaoEncerrar(bool lixeira){
  if(lixeira){ cs.excluido=1; cicloStatsSalvar(); }   // Fase 55: relatorio/historico marcam "excluido"
  char msg[64]; snprintf(msg,64,"Lote '%s' %s (dia %d/%d)",fbCicloNome[0]?fbCicloNome:"-",lixeira?"excluido":"encerrado",calcularDiaCicloAtual(),cicloTotalDias());
  logEvento("Ciclo",msg,true);
  lastLocalCicloActionMs=millis();
  cicloSyncPend=(WiFi.status()!=WL_CONNECTED||!fbStarted||!fbApp.ready());   // Fase 79: Firebase nao pronto = "Parar" perdido; reenvia quando voltar
  cicloParouMs=millis()|1UL;
  pushCicloParar();
  fbCicloAtivo=false; fbCicloPausado=false; fbCicloPreparo=false; fbCicloVolL=0; fbCicloNome[0]=0; fbCicloTipo[0]=0; cicloConfirm=0;
  if(ultTipoNome[0]){ manterAtivo=true; manterSyncPend=true; }   // Fase 79: continua mantendo o ambiente do ultimo tipo
  aplicarLimitesDoTipo();
  appState=S_MAIN; drawMainScreen();
}
void handleCicloTap(int tx,int ty);
static void wizIniciar();
void handleCicloTap(int tx,int ty){
  if(!fbCicloAtivo){
    if(ty>=116&&ty<=176&&tx>=40&&tx<=280) wizIniciar();
    else { appState=S_MAIN; drawMainScreen(); }   // Fase 54: toque fora do botao volta ao inicio
    return;
  }
  if(cicloConfirm){
    if(ty>=150&&ty<=214){
      if(tx>=16&&tx<=152){ cicloConfirm=0; drawCicloScreen(); }
      else if(tx>=168&&tx<=304) cicloAcaoEncerrar(cicloConfirm==2);
    }
    return;
  }
  if(fbCicloPreparo){ handleStatusTap(tx,ty); return; }   // Fase 77
  if(ty<168){ appState=S_MAIN; drawMainScreen(); return; }   // Fase 54: sem botao "<" -- tocar acima dos botoes volta ao inicio
  int idx=(tx<108)?0:(tx<212)?1:2;
  int bx=(idx==0)?8:(idx==1)?112:216;
  if(idx==1&&!cicloPodeParar()){   // desabilitado: so avisa
    cicloBotao(bx,1,true); delay(90); cicloBotao(bx,1,false);
    arqToast("Parar libera no ultimo dia do ciclo",false); drawCicloScreen(); return;
  }
  cicloBotao(bx,idx,true); delay(110);   // efeito de toque
  if(idx==0){
    lastLocalCicloActionMs=millis(); cicloSyncPend=(WiFi.status()!=WL_CONNECTED);
    if(fbCicloPausado){
      time_t novoInicio=fbCicloIniciadoEm+(time(nullptr)-fbCicloPausadoEm);
      pushCicloRetomar(novoInicio); fbCicloIniciadoEm=novoInicio; fbCicloPausado=false;
    }else{
      time_t agora=time(nullptr); pushCicloTimestampField("cicloPausadoEm",agora); fbCicloPausado=true; fbCicloPausadoEm=agora;
    }
    drawCicloScreen();
  }else{ cicloConfirm=(idx==1)?1:2; drawCicloScreen(); }
}

// ═══════════════════════════════════════════════════════════════
// Fase 77 -- janela do STATUS (tela cheia) + confirmacao "ovos colocados".
// Mesma informacao do PWA: temperatura/umidade/automacao, tempo estavel de 5 min; em PREPARO tem o botao que INICIA o ciclo de verdade
// (data/hora = momento da confirmacao). Confirmar antes do "Pronto" pede um 2o toque (6 s).
// ═══════════════════════════════════════════════════════════════
static bool statusArmado=false; static unsigned long statusArmadoMs=0;
void drawStatusDetalhe(bool full){
  if(statusArmado && millis()-statusArmadoMs>6000UL) statusArmado=false;
  bool prep=fbCicloAtivo&&fbCicloPreparo, pronto=(prep&&ambEstado==2);
  bool tOk=sht31OK && !isnan(temperaturaAtual) && temperaturaAtual>=TEMP_IDEAL_MIN && temperaturaAtual<=TEMP_IDEAL_MAX;
  bool uOk=sht31OK && !isnan(umidadeAtual) && umidadeAtual>=UMID_IDEAL_MIN && umidadeAtual<=UMID_IDEAL_MAX;
  bool aOk=autoAtivo; int segs=(int)ambEstavelSeg;
  bool man=(!fbCicloAtivo&&(ambEstado==3||ambEstado==4)), podeManter=(!fbCicloAtivo&&!man&&ultTipoNome[0]&&manterTipoPtr()!=nullptr);   // Fase 79
  uint16_t ambar=tft.color565(230,180,60);
  uint32_t sig=(tOk?1:0)|(uOk?2:0)|(aOk?4:0)|(prep?8:0)|(pronto?16:0)|(fbCicloAtivo?32:0)|(statusArmado?64:0)|((uint32_t)(isnan(temperaturaAtual)?0:(int)(temperaturaAtual*10))<<8)|((uint32_t)(isnan(umidadeAtual)?0:(int)umidadeAtual)<<20);
  sig|=((uint32_t)(man?1:0)<<27)|((uint32_t)(podeManter?1:0)<<28)|((uint32_t)ambEstado<<29);
  { uint32_t dh=0; for(const char* q=ambDiagTxt;*q;q++) dh=dh*31+(uint8_t)*q; sig^=dh*40503u; }   // Fase 80
  sig^=(uint32_t)segs*2654435761u;
  sig^=((uint32_t)adxlEst*977u+(adxlAutoAtivo()?13u:0u)+(tampaEst==1?7u:0u))*2246822519u;   // Fase 95
  if(tampaEst!=0) sig^=(uint32_t)tampaSeg*0x9E3779B9u;   // Fase 96: o cronometro (aberta/recuperando) so atualizava quando outra coisa mudava
  static uint32_t sigAnt=0; static bool sigValido=false;
  if(!full && sigValido && sig==sigAnt) return;
  sigAnt=sig; sigValido=true;
  if(full){ tft.fillScreen(cBg); uiTopo("STATUS DO AMBIENTE"); } else tft.fillRect(0,20,320,220,cBg);
  const char* pal="OFF"; uint16_t cor=cGreen;
  if(fbCicloAtivo){ if(prep){ if(pronto) pal="Pronto"; else { pal="Aguarde"; cor=ambar; } } else { int mo=statusMotivoCiclo(); pal=STATUS_MOTIVO[mo]; if(mo) cor=ambar; } }   // Fase 112
  else if(man){ if(ambEstado==3) pal="Mantem"; else { pal="Ajuste"; cor=ambar; } }
  if(ambEstado==5){ pal="Tampa aberta"; cor=ambar; } else if(ambEstado==6){ pal="Recuperando"; cor=ambar; }   // Fase 82
  tft.setTextColor(cor,cBg); tft.setTextSize(3); tft.setCursor(10,26); tft.print(pal);
  if(fbCicloAtivo && fbCicloVolL>0){ char v[16]; snprintf(v,16,"Caixa %d L",fbCicloVolL); tft.setTextSize(1); tft.setTextColor(cSub,cBg); tft.setCursor(314-tft.textWidth(v),32); tft.print(v); }
  if(prep){
    float p=pronto?1.0f:segs/(AMB_ESTAVEL_MS/1000.0f); if(p>1) p=1;
    tft.fillRoundRect(10,58,300,10,5,tft.color565(30,30,60));
    int fw=(int)(300*p); if(fw>0) tft.fillRoundRect(10,58,fw<10?10:fw,10,5,pronto?cGreen:ambar);
    char s[44]; if(pronto) snprintf(s,44,"Ambiente estavel (%d min seguidos)",(int)(AMB_ESTAVEL_MS/60000UL)); else snprintf(s,44,"Estavel ha %d:%02d de %d:00 min",segs/60,segs%60,(int)(AMB_ESTAVEL_MS/60000UL));
    tft.setTextSize(1); tft.setTextColor(cSub,cBg); tft.setCursor(10,72); tft.print(s);
  }
  int y=88;
  auto linha=[&](bool ok,const char* a,const char* b){
    tft.fillCircle(16,y+4,4,ok?cGreen:tft.color565(230,80,80));
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE,cBg); tft.setCursor(28,y); tft.print(a);
    tft.setTextColor(cSub,cBg); tft.setCursor(314-tft.textWidth(b),y); tft.print(b); y+=16; };
  if(fbCicloAtivo||man){
    char a1[32],b1[32],a2[32],b2[32];
    if(isnan(temperaturaAtual)) snprintf(a1,32,"Temperatura  --"); else snprintf(a1,32,"Temperatura  %.1f C",temperaturaAtual);
    snprintf(b1,32,"faixa %.1f - %.1f",TEMP_IDEAL_MIN,TEMP_IDEAL_MAX);
    if(isnan(umidadeAtual)) snprintf(a2,32,"Umidade  --"); else snprintf(a2,32,"Umidade  %.0f %%",umidadeAtual);
    snprintf(b2,32,"faixa %.0f - %.0f",UMID_IDEAL_MIN,UMID_IDEAL_MAX);
    linha(tOk,a1,b1); linha(uOk,a2,b2); linha(aOk,"Automacao",aOk?"ligada":"desligada");
  }
  y+=4; tft.setTextSize(1);
  const char* m1=""; const char* m2=""; uint16_t cm=cSub;
  char mm1[48], mm2[56];
  if(ambEstado==5){ snprintf(mm1,sizeof(mm1),"Tampa aberta ha %u:%02u. Sem alertas.",tampaSeg/60,tampaSeg%60); m1=mm1; m2="Umidificador em pausa; aquecedor compensando."; cm=ambar; }
  else if(ambEstado==6){ snprintf(mm1,sizeof(mm1),"Recuperando o ambiente (%u:%02u).",tampaSeg/60,tampaSeg%60); m1=mm1; m2="Volta ao normal apos 1 min na faixa."; cm=ambar; }
  else if(man){ snprintf(mm1,sizeof(mm1),"Mantendo o ambiente: %s",ultTipoNome); m1=mm1; m2=(tOk&&uOk)?"Tudo na faixa. Para parar, toque no botao.":"Ajustando temperatura e umidade..."; cm=(tOk&&uOk)?cGreen:ambar; }
  else if(!fbCicloAtivo){ m1="Nenhum ciclo em andamento."; if(podeManter) m2="Toque abaixo para manter o ambiente do ultimo ciclo."; }
  else if(prep && !pronto){ m1="NAO coloque os ovos ainda."; m2="Aguarde: temperatura e umidade na faixa por 5 min."; cm=ambar; }
  else if(pronto){ m1="Pode colocar os ovos."; m2="Depois toque em \"Ovos colocados\": o ciclo comeca ali."; cm=cGreen; }
  else if(!autoAtivo){   // Fase 112: automacao desligada = nada controla o ambiente (e o texto "tudo normal" nao procede)
    m1="Automacao DESLIGADA: nada controla o ambiente.";
    if(autoRetomaMin>0){ snprintf(mm2,sizeof(mm2),"Volta sozinha em ate %u min. Ou ligue em Atuadores.",(unsigned)autoRetomaMin); m2=mm2; } else m2="Ligue a chave em Atuadores ou em Automacao.";
    cm=ambar;
  }
  else{ m1=(tOk&&uOk)?"Ciclo em andamento - tudo normal.":"Atencao: fora da faixa ideal."; cm=(tOk&&uOk)?cSub:ambar; }
  if(ambDiagTxt[0]){   // Fase 80: causa provavel (ate 3 linhas de ~50 caracteres)
    tft.setTextColor(tft.color565(255,150,70),cBg);
    const char* p=ambDiagTxt; int ln=0;
    while(*p && ln<3){
      int n=0, ult=-1; while(p[n] && n<50){ if(p[n]==' ') ult=n; n++; }
      int corte=(p[n]&&ult>0)?ult:n;
      char tmp[56]; if(corte>54) corte=54; memcpy(tmp,p,corte); tmp[corte]=0;
      tft.setCursor(10,y+ln*12); tft.print(tmp);
      p+=corte; while(*p==' ') p++; ln++;
    }
  }else{ tft.setTextColor(cm,cBg); tft.setCursor(10,y); tft.print(m1); if(m2[0]){ tft.setCursor(10,y+12); tft.print(m2); } }
  if(prep){
    uint16_t bg=pronto?tft.color565(10,70,30):tft.color565(58,40,6), fg=pronto?cGreen:ambar;
    if(statusArmado){ bg=tft.color565(120,50,10); fg=TFT_WHITE; }
    tft.fillRoundRect(8,176,200,56,10,bg);
    const char* l1=statusArmado?"Confirmar?":"Ovos colocados";
    tft.setTextColor(fg,bg); tft.setTextSize(2); tft.setCursor(8+(200-tft.textWidth(l1))/2,184); tft.print(l1);
    const char* l2=statusArmado?"ainda estabilizando - toque de novo":(pronto?"inicia o ciclo agora":"ainda estabilizando");
    tft.setTextSize(1); tft.setCursor(8+(200-tft.textWidth(l2))/2,212); tft.print(l2);
    tft.fillRoundRect(216,176,96,56,10,tft.color565(60,14,14));
    tft.setTextColor(tft.color565(255,95,95),tft.color565(60,14,14)); tft.setTextSize(1);
    tft.setCursor(216+(96-tft.textWidth("Cancelar"))/2,196); tft.print("Cancelar");
    tft.setCursor(216+(96-tft.textWidth("ciclo"))/2,208); tft.print("ciclo");
  }
  if(fbCicloAtivo && !fbCicloPreparo && (tampaEst==0||tampaEst==1)){   // Fase 82: avisar a tampa (opcional: a abertura tambem e detectada sozinha)
    bool ab=(tampaEst==1);
    if(adxlAutoAtivo()){   // Fase 95: tampa AUTOMATICA pelo sensor -- cartao fino com o estado + botao manual pequeno (sem o botao enorme)
      uint16_t bg=tft.color565(16,16,46), bd=ab?ambar:tft.color565(46,100,60);
      tft.fillRoundRect(8,192,206,40,8,bg); tft.drawRoundRect(8,192,206,40,8,bd);
      tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(16,198); tft.print("Tampa - automatica (sensor ADXL)");
      { const char* st=ab?"ABERTA":((adxlConfiavel&&adxlDentro)?"FECHADA":(adxlEst==4?"MODULO FORA":"SEM ABERTURA")); bool fora=(!ab && adxlEst==4);   // Fase 112: so afirma FECHADA com assinatura confiavel
        tft.setTextSize(2); tft.setTextColor((ab||fora)?ambar:cGreen,bg); tft.setCursor(16,212); tft.print(st); }
      uint16_t b2=ab?tft.color565(10,70,30):tft.color565(58,40,6), f2=ab?cGreen:ambar;
      tft.fillRoundRect(220,192,92,40,8,b2);
      tft.setTextSize(1); tft.setTextColor(f2,b2);
      const char* a1=ab?"Fechei":"Vou abrir"; tft.setCursor(220+(92-tft.textWidth(a1))/2,200); tft.print(a1);
      const char* a2="(manual)"; tft.setCursor(220+(92-tft.textWidth(a2))/2,216); tft.print(a2);
    }else{
      uint16_t bg=ab?tft.color565(10,70,30):tft.color565(58,40,6), fg=ab?cGreen:ambar;
      tft.fillRoundRect(8,176,304,56,10,bg);
      const char* l1=ab?"Fechei a caixa":"Vou abrir a caixa";
      tft.setTextColor(fg,bg); tft.setTextSize(2); tft.setCursor(8+(304-tft.textWidth(l1))/2,184); tft.print(l1);
      const char* l2=ab?"volta ao controle normal":"ovoscopia, agua, virar: sem pausar o ciclo";
      tft.setTextSize(1); tft.setCursor(8+(304-tft.textWidth(l2))/2,212); tft.print(l2);
    }
  }
  if(man||podeManter){   // Fase 79: parar / iniciar a manutencao do ambiente
    uint16_t bg=man?(statusArmado?tft.color565(120,50,10):tft.color565(60,14,14)):tft.color565(10,70,30);
    uint16_t fg=man?(statusArmado?TFT_WHITE:tft.color565(255,95,95)):cGreen;
    tft.fillRoundRect(8,176,304,56,10,bg);
    const char* l1=man?(statusArmado?"Confirmar?":"Parar manutencao"):"Manter ambiente";
    tft.setTextColor(fg,bg); tft.setTextSize(2); tft.setCursor(8+(304-tft.textWidth(l1))/2,184); tft.print(l1);
    const char* l2=man?(statusArmado?"toque de novo: desliga aquecedor e umidificador":"desliga o controle de temperatura e umidade"):"usa a temperatura e umidade do ultimo tipo";
    tft.setTextSize(1); tft.setCursor(8+(304-tft.textWidth(l2))/2,212); tft.print(l2);
  }
}
void handleStatusTap(int tx,int ty){
  if(uiVoltar(tx,ty)){ statusArmado=false; appState=S_MAIN; drawMainScreen(); return; }
  if(fbCicloAtivo && !fbCicloPreparo && ty>=176 && ty<=232){   // Fase 82: tampa
    if(adxlAutoAtivo() && !(ty>=192 && tx>=216)) return;   // Fase 95: automatica -- so o botao manual pequeno vale
    tampaOrigemAdxl=false; if(tampaEst==0) tampaCmd=1; else if(tampaEst==1) tampaCmd=2;
    return;
  }
  if(!fbCicloAtivo && ty>=176 && ty<=232){   // Fase 79: manutencao do ambiente sem ciclo
    if(ambManterOn()){
      if(statusArmado && millis()-statusArmadoMs<6000UL){
        statusArmado=false; manterAtivo=false; manterSyncPend=true; ambEstado=0; aplicarLimitesDoTipo();
        logEvento("Ciclo","Manutencao do ambiente parada pelo usuario",true); drawStatusCard(); drawStatusDetalhe(true);
      }else{ statusArmado=true; statusArmadoMs=millis(); drawStatusDetalhe(true); }
    }else if(ultTipoNome[0] && manterTipoPtr()){
      statusArmado=false; manterAtivo=true; manterSyncPend=true; ambEstado=4; aplicarLimitesDoTipo();
      char m[60]; snprintf(m,sizeof(m),"Mantendo o ambiente do tipo %s",ultTipoNome); logEvento("Ciclo",m,true); drawStatusCard(); drawStatusDetalhe(true);
    }
    return;
  }
  if(!(fbCicloAtivo&&fbCicloPreparo)) return;
  if(ty>=176 && ty<=232){
    if(tx<212){
      if(ambEstado==2 || statusArmado){ statusArmado=false; cicloConfirmarOvos(); }
      else{ statusArmado=true; statusArmadoMs=millis(); drawStatusDetalhe(true); }
    }else if(tx>=216){ statusArmado=false; cicloConfirm=2; appState=S_CICLO; drawCicloScreen(); }   // cancelar o preparo (mesma confirmacao da lixeira)
  }
}
// O USUARIO CONFIRMOU que colocou os ovos: o ciclo comeca AGORA (data/hora = este instante) e o dia 1 passa a contar.
void cicloConfirmarOvos(){
  if(!fbCicloAtivo || !fbCicloPreparo) return;
  time_t agora=time(nullptr);
  if(agora<(time_t)EPOCH_2024_01_01 && rtcOK) agora=(time_t)rtc.now().unixtime()-tzOffsetSeg();
  lastLocalCicloActionMs=millis(); cicloParouMs=0; cicloSyncPend=(WiFi.status()!=WL_CONNECTED||!fbStarted||!fbApp.ready());
  fbCicloIniciadoEm=agora; fbCicloPreparo=false; fbCicloPausado=false; fbCicloPausadoEm=0;
  ambPronto=true; ambEpoch=(long)agora; ambSalvar();
  char ex[16]; snprintf(ex,sizeof(ex),"0;%d",fbCicloVolL);
  pushCicloIniciar(fbCicloNome,fbCicloTipo,fbCicloDuracaoDias,agora,ex);
  char msg[72]; snprintf(msg,sizeof(msg),"Ovos colocados -- lote '%s' iniciado agora",fbCicloNome[0]?fbCicloNome:"-"); logEvento("Ciclo",msg,true);
  aplicarLimitesDoTipo();
  statusArmado=false; cicloConfirm=0; appState=S_CICLO;
  arqToast("Ciclo iniciado!",true);
  drawCicloScreen();
}

// ═══ Wizard de inicio (4 etapas) ═══
int wizStep=0, wizTipoIdx=0, wizDias=21, wizScrollT=0;
bool wizPersEscolhendo=false;
bool wizVoltaDoTipo=false;   // Fase 54: o editor de tipo foi aberto pelo "Personalizado" do wizard
static void wizCriarTipoPers();
char wizNome[24]="";
int wizA=2026,wizM=1,wizD=1,wizH=0,wizMi=0;   // inicio (hora local)

static int wizDiasMes(int m,int a){ static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31}; return (m==2&&((a%4==0&&a%100!=0)||a%400==0))?29:d[m-1]; }
static long wizLocalAgora(){ return rtcOK?(long)rtc.now().unixtime():(long)time(nullptr); }
static void wizAgora(){
  if(rtcOK){ DateTime n=rtc.now(); wizA=n.year();wizM=n.month();wizD=n.day();wizH=n.hour();wizMi=n.minute(); }
  else { time_t t=time(nullptr); struct tm x; gmtime_r(&t,&x); wizA=x.tm_year+1900;wizM=x.tm_mon+1;wizD=x.tm_mday;wizH=x.tm_hour;wizMi=x.tm_min; }
}
static long wizLocalEscolhido(){ return (long)DateTime(wizA,wizM,wizD,wizH,wizMi,0).unixtime(); }
static void wizClamp(){   // sem inicio no futuro; dia dentro do mes
  if(wizD>wizDiasMes(wizM,wizA)) wizD=wizDiasMes(wizM,wizA);
  if(wizLocalEscolhido()>wizLocalAgora()) wizAgora();
}
static time_t wizInicioUtc(){ return (time_t)(wizLocalEscolhido()-tzOffsetSeg()); }

static void wizCabecalho(int etapa,const char* titulo){
  tft.fillScreen(cBg);
  tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char e[20]; snprintf(e,20,"Etapa %d de 4",etapa);
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(36,10);tft.print(e);
  for(int i=0;i<4;i++) tft.fillRect(8+i*77,26,73,4,i<etapa?cLogo:tft.color565(36,36,78));
  tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(2);tft.setCursor(8,38);tft.print(titulo);
}
void drawWiz();
static void wizBotao(int x,int y,int w,int h,const char* t,uint16_t bg,uint16_t fg){
  tft.fillRoundRect(x,y,w,h,8,bg);tft.setTextColor(fg,bg);tft.setTextSize(2);tft.setCursor(x+(w-tft.textWidth(t))/2,y+(h-16)/2);tft.print(t);
}
void drawWizTipo(){
  wizCabecalho(2,"Tipo de ovos");
  if(wizPersEscolhendo){
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(8,66);tft.print("Personalizado: dias de incubacao");
    wizBotao(30,96,80,70,"-",tft.color565(30,30,70),TFT_WHITE);
    wizBotao(210,96,80,70,"+",tft.color565(30,30,70),TFT_WHITE);
    char d[8]; snprintf(d,8,"%d",wizDias);
    tft.setTextColor(cLogo,cBg);tft.setTextSize(5);tft.setCursor(160-tft.textWidth(d)/2,110);tft.print(d);
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(136,150);tft.print("dias");
    wizBotao(80,184,160,46,"Proximo >",tft.color565(10,70,30),cGreen);
    return;
  }
  int itens=tiposN+1;
  for(int i=0;i<5;i++){
    int idx=wizScrollT+i; if(idx>=itens) break;
    int y=62+i*34; bool sel=(idx==wizTipoIdx);
    uint16_t bg=sel?tft.color565(10,50,24):tft.color565(16,16,46);
    tft.fillRoundRect(4,y,278,32,6,bg);
    tft.setTextColor(TFT_WHITE,bg);tft.setTextSize(2);tft.setCursor(12,y+9);tft.print(idx<tiposN?tipos[idx].nome:"Personalizado");
    char r[16]; if(idx<tiposN) snprintf(r,16,"%d dias",tipos[idx].dias); else strcpy(r,"criar tipo");
    tft.setTextColor(cSub,bg);tft.setTextSize(1);tft.setCursor(276-tft.textWidth(r),y+12);tft.print(r);
  }
  bool cima=wizScrollT>0, baixo=wizScrollT+5<itens; uint16_t on=cLogo, off=tft.color565(60,55,25);
  tft.fillTriangle(303,70,292,100,314,100,cima?on:off);
  tft.fillTriangle(303,226,292,196,314,196,baixo?on:off);
}
void drawWizData(){
  wizCabecalho(3,"Inicio da incubacao");
  const char* lb[5]={"Dia","Mes","Ano","Hora","Min"};
  char v[5][6]; snprintf(v[0],6,"%02d",wizD);snprintf(v[1],6,"%02d",wizM);snprintf(v[2],6,"%d",wizA);snprintf(v[3],6,"%02d",wizH);snprintf(v[4],6,"%02d",wizMi);
  for(int i=0;i<5;i++){
    int x=6+i*63, w=58;
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(x+(w-tft.textWidth(lb[i]))/2,66);tft.print(lb[i]);
    tft.fillRoundRect(x,78,w,30,6,tft.color565(30,30,70)); tft.fillTriangle(x+w/2,86,x+w/2-9,100,x+w/2+9,100,cLogo);
    tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(2);tft.setCursor(x+(w-tft.textWidth(v[i]))/2,118);tft.print(v[i]);
    tft.fillRoundRect(x,142,w,30,6,tft.color565(30,30,70)); tft.fillTriangle(x+w/2,164,x+w/2-9,150,x+w/2+9,150,cLogo);
  }
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(8,180);tft.print("Nao pode ser no futuro. \"Agora\" usa a hora atual.");
  wizBotao(8,192,120,40,"Agora",tft.color565(30,30,70),TFT_WHITE);
  wizBotao(168,192,144,40,"Proximo >",tft.color565(10,70,30),cGreen);
}
int wizVol=10;   // Fase 77 -- volume da caixa (litros); lembra o ultimo usado
static void wizVolCarregar(){ wizVol=hw.volL; }   // Fase 78: o volume da caixa mora em Configuracoes > Hardware
static void wizVolSalvar(){ if(hw.volL!=wizVol){ hw.volL=wizVol; hwSujo=true; hwUltMs=millis(); } }
void drawWizVolume(){
  wizCabecalho(3,"Volume da caixa");
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(8,60);tft.print("Litros da camara. Ajuda a estabilizar mais rapido.");
  static const int pre[5]={3,10,30,60,100};
  for(int i=0;i<5;i++){ char t[8]; snprintf(t,8,"%d L",pre[i]); telaBotaoMini(6+i*62,74,58,28,t,wizVol==pre[i]); }
  wizBotao(30,112,80,64,"-",tft.color565(30,30,70),TFT_WHITE);
  wizBotao(210,112,80,64,"+",tft.color565(30,30,70),TFT_WHITE);
  char d[8]; snprintf(d,8,"%d",wizVol);
  tft.setTextColor(cLogo,cBg);tft.setTextSize(5);tft.setCursor(160-tft.textWidth(d)/2,122);tft.print(d);
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(160-tft.textWidth("litros")/2,160);tft.print("litros");
  wizBotao(80,190,160,42,"Proximo >",tft.color565(10,70,30),cGreen);
}
void drawWizResumo(){
  wizCabecalho(4,"Resumo");
  bool pers=(wizTipoIdx>=tiposN);
  int y=64; auto lin=[&](const char* l,const char* v){ tft.setTextSize(1);tft.setTextColor(cSub,cBg);tft.setCursor(8,y);tft.print(l);tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(110,y);tft.print(v);y+=14; };
  lin("Lote",wizNome);
  lin("Tipo",pers?"Personalizado":tipos[wizTipoIdx].nome);
  char d[24]; snprintf(d,24,"%d dias  -  caixa %d L",wizDias,wizVol); lin("Duracao",d);
  lin("Inicio","quando voce confirmar os ovos");
  if(!pers){ char p[44]; snprintf(p,44,"%.1f C / %d%%",tipos[wizTipoIdx].temp10/10.0f,tipos[wizTipoIdx].umid); lin("Ideal",p);
    const TipoOvo& tw=tipos[wizTipoIdx]; snprintf(p,44,"%.1f C / %d%% (dia %d)",(tw.tempEcl10>0?tw.tempEcl10:tw.temp10)/10.0f,tw.umidEcl,tw.eclDia); lin("Eclosao",p); }
  y+=4; tft.setTextSize(1);tft.setTextColor(tft.color565(230,180,60),cBg);
  tft.setCursor(8,y);tft.print("1) A chocadeira vai estabilizar SEM ovos (~5 min).");
  tft.setCursor(8,y+12);tft.print("2) Aviso \"Pronto\": coloque os ovos e confirme");
  tft.setCursor(8,y+24);tft.print("   no card Status ou no card Ciclo.");
  tft.setCursor(8,y+36);tft.print("3) O ciclo (dia 1) comeca na confirmacao.");
  wizBotao(8,192,120,40,"< Voltar",tft.color565(30,30,70),TFT_WHITE);
  wizBotao(140,192,172,40,"Iniciar preparo",tft.color565(10,70,30),cGreen);
}
void drawWiz(){ if(wizStep==2) drawWizTipo(); else if(wizStep==3) drawWizVolume(); else drawWizResumo(); }

static void wizNomeOk(){
  sanitizarNome(wizNome);
  if(!wizNome[0]) snprintf(wizNome,sizeof(wizNome),"Lote %02d/%02d",wizD,wizM);
  wizStep=2; wizPersEscolhendo=false; appState=S_CICLO_WIZ; drawWiz();
}
static void wizNomeVoltar(){ appState=S_CICLO; drawCicloScreen(); }
static void wizIrNome(){ wizStep=1; abrirKbTxt("Etapa 1 de 4 - Nome do lote","Ex.: Lote Galinhas 01",1,wizNome,23,wizNomeOk,wizNomeVoltar); }
static void wizIniciar(){
  wizNome[0]=0; wizTipoIdx=0; wizScrollT=0; wizPersEscolhendo=false; wizDias=tipos[0].dias; wizAgora(); wizVolCarregar();
  wizIrNome();
}
void wizConfirmar(){
  bool pers=(wizTipoIdx>=tiposN);
  const char* tn=pers?"Personalizado":tipos[wizTipoIdx].nome;
  wizAgora(); time_t ini=wizInicioUtc();   // marca o inicio do PREPARO; o inicio real (dia 1) sera a confirmacao dos ovos
  wizVolSalvar();
  lastLocalCicloActionMs=millis(); cicloParouMs=0; cicloSyncPend=(WiFi.status()!=WL_CONNECTED||!fbStarted||!fbApp.ready());   // sem internet: vale local e sobe quando voltar
  char ex[16]; snprintf(ex,sizeof(ex),"1;%d",wizVol);
  pushCicloIniciar(wizNome,tn,wizDias,ini,ex);
  fbCicloAtivo=true; fbCicloPausado=false; fbCicloPreparo=true; fbCicloVolL=wizVol; fbCicloIniciadoEm=ini; fbCicloPausadoEm=0; fbCicloDuracaoDias=wizDias;
  strlcpy(fbCicloNome,wizNome,sizeof(fbCicloNome)); strlcpy(fbCicloTipo,tn,sizeof(fbCicloTipo));
  char msg[80]; snprintf(msg,sizeof(msg),"Lote '%s' em preparo (%s, %d dias, caixa %d L)",wizNome,tn,wizDias,wizVol); logEvento("Ciclo",msg,true);
  aplicarLimitesDoTipo();
  cicloConfirm=0; statusArmado=false; appState=S_STATUS; drawStatusDetalhe(true);
}
void handleWizTap(int tx,int ty){
  if(ty<26&&tx<40){   // voltar
    if(wizStep==2&&wizPersEscolhendo){ wizPersEscolhendo=false; drawWiz(); }
    else if(wizStep==2) wizIrNome();
    else { wizStep--; drawWiz(); }
    return;
  }
  if(wizStep==2){
    if(wizPersEscolhendo){
      if(ty>=96&&ty<=166){ if(tx>=30&&tx<=110&&wizDias>1) wizDias--; else if(tx>=210&&tx<=290&&wizDias<60) wizDias++; drawWizTipo(); }
      else if(ty>=184&&ty<=230&&tx>=80&&tx<=240){ wizStep=3; drawWiz(); }
      return;
    }
    int itens=tiposN+1;
    if(tx>=286){ if(ty<148){ if(wizScrollT>0){ wizScrollT--; drawWizTipo(); } } else if(wizScrollT+5<itens){ wizScrollT++; drawWizTipo(); } return; }
    int row=(ty-62)/34, idx=wizScrollT+row;
    if(ty>=62&&row<5&&idx<itens){
      wizTipoIdx=idx;
      if(idx==tiposN){ wizCriarTipoPers(); }
      else { wizDias=tipos[idx].dias; wizStep=3; drawWiz(); }
    }
  }else if(wizStep==3){   // Fase 77: volume da caixa
    if(ty>=74&&ty<=102){ static const int pre[5]={3,10,30,60,100}; int i=(tx-6)/62; if(i>=0&&i<5){ wizVol=pre[i]; drawWizVolume(); } return; }
    if(ty>=112&&ty<=176){
      int passo=(wizVol<20)?1:(wizVol<100)?5:10;
      if(tx>=30&&tx<=110){ wizVol-=passo; if(wizVol<1) wizVol=1; drawWizVolume(); }
      else if(tx>=210&&tx<=290){ wizVol+=passo; if(wizVol>500) wizVol=500; drawWizVolume(); }
      return;
    }
    if(ty>=190&&ty<=232&&tx>=80&&tx<=240){ wizStep=4; drawWiz(); }
  }else if(wizStep==4){
    if(ty>=192&&ty<=232){ if(tx>=8&&tx<=128){ wizStep=3; drawWiz(); } else if(tx>=140) wizConfirmar(); }
  }
}

// ═══ Tipos de ovos (Configuracoes > Ovos) — Fase 43: edicao em 3 paginas ═══
#define TIPOS_ROW_Y0 26
#define TIPOS_ROW_H  26
#define TIPO_PAGS    4
TipoOvo tipoEd, tipoOrig; int tipoSel=-1, tipoPag=0; bool tipoNovo=false, tipoDelArmado=false; char tipoNomeBuf[20]="";
void drawTiposLista(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(36,8);tft.print("Ovos - tipos e parametros");
  if(tiposN<TIPOS_MAX){ tft.fillRoundRect(284,2,32,20,4,tft.color565(30,30,70)); tft.drawFastHLine(292,12,16,cLogo); tft.drawFastVLine(300,6,13,cLogo); }
  for(int i=0;i<tiposN;i++){
    int y=TIPOS_ROW_Y0+i*TIPOS_ROW_H; uint16_t bg=tft.color565(16,16,46);
    tft.fillRoundRect(4,y,312,TIPOS_ROW_H-2,5,bg);
    tft.setTextColor(TFT_WHITE,bg);tft.setTextSize(1);tft.setCursor(12,y+9);tft.print(tipos[i].nome);
    char r[40]; snprintf(r,40,"%dd  %.1fC  %d%%  %dx/dia",tipos[i].dias,tipos[i].temp10/10.0f,tipos[i].umid,tipos[i].viraPorDia);
    tft.setTextColor(cSub,bg);tft.setCursor(308-tft.textWidth(r),y+9);tft.print(r);
  }
  if(tiposN<TIPOS_MAX){ tft.setTextColor(tft.color565(90,90,120),cBg);tft.setTextSize(1);tft.setCursor(8,TIPOS_ROW_Y0+tiposN*TIPOS_ROW_H+4);tft.print("Toque num tipo para editar. \"+\" cria um novo."); }
}
void abrirTipos(){ appState=S_TIPOS; drawTiposLista(); }
static void tipoRow(int y,const char* lb,const char* val,bool stepper,int passo=1){
  tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(10,y+10);tft.print(lb);
  tft.setTextColor(TFT_WHITE,cBg);tft.setTextSize(2);tft.setCursor(stepper?150:110,y+6);tft.print(val);
  if(stepper){
    tft.fillRoundRect(214,y+1,40,24,5,tft.color565(30,30,70)); tft.fillRect(228,y+12,12,2,TFT_WHITE);
    tft.fillRoundRect(262,y+1,40,24,5,tft.color565(30,30,70)); tft.fillRect(276,y+12,12,2,TFT_WHITE); tft.fillRect(281,y+7,2,12,TFT_WHITE);
  }
}
static void desenhaRestaurar(){   // seta circular (restaurar parametros)
  tft.fillRoundRect(284,2,32,20,4,tft.color565(30,30,70));
  tft.drawArc(300,12,7,5,40,340,cLogo,tft.color565(30,30,70),false);
  tft.fillTriangle(302,2,302,10,309,6,cLogo);
}
void drawTipoEdit(){
  tft.fillScreen(cBg);tft.fillRect(0,0,320,24,cCard);
  tft.setTextColor(TFT_WHITE,cCard);tft.setTextSize(2);tft.setCursor(8,4);tft.print("<");
  char h[32]; snprintf(h,32,"%s  (pag %d/%d)",tipoNovo?"Novo tipo":"Editar tipo",tipoPag+1,TIPO_PAGS);
  tft.setTextColor(cLogo,cCard);tft.setTextSize(1);tft.setCursor(36,8);tft.print(h);
  desenhaRestaurar();
  char v[16];
  if(tipoPag==0){
    tipoRow(30,"Nome",tipoEd.nome,false);
    snprintf(v,16,"%d",tipoEd.dias); tipoRow(58,"Dias de incubacao",v,true);
    snprintf(v,16,"%.1f",tipoEd.temp10/10.0f); tipoRow(86,"Temp. ideal (C)",v,true);
    snprintf(v,16,"%.1f",tipoEd.tempMin10/10.0f); tipoRow(114,"Temp. minima (C)",v,true);
    snprintf(v,16,"%.1f",tipoEd.tempMax10/10.0f); tipoRow(142,"Temp. maxima (C)",v,true);
  }else if(tipoPag==1){
    snprintf(v,16,"%d",tipoEd.umid); tipoRow(30,"Umidade ideal (%)",v,true);
    snprintf(v,16,"%d",tipoEd.umidMin); tipoRow(58,"Umidade minima (%)",v,true);
    snprintf(v,16,"%d",tipoEd.umidMax); tipoRow(86,"Umidade maxima (%)",v,true);
    tft.setTextColor(tft.color565(90,90,120),cBg);tft.setTextSize(1);tft.setCursor(10,118);tft.print("Temperatura e umidade da ECLOSAO ficam na");tft.setCursor(10,130);tft.print("ultima pagina (junto das ovoscopias).");
  }else if(tipoPag==3){
    snprintf(v,16,"%d",tipoEd.ovo1); tipoRow(30,"1a ovoscopia (dia)",v,true);
    if(tipoEd.ovo2) snprintf(v,16,"%d",tipoEd.ovo2); else strcpy(v,"--"); tipoRow(58,"2a ovoscopia (dia)",v,true);
    snprintf(v,16,"%d",tipoEd.eclDia); tipoRow(86,"Inicio eclosao (dia)",v,true);
    snprintf(v,16,"%.1f",(tipoEd.tempEcl10>0?tipoEd.tempEcl10:tipoEd.temp10)/10.0f); tipoRow(114,"Temp. eclosao (C)",v,true);
    snprintf(v,16,"%d",tipoEd.umidEcl); tipoRow(142,"Umid. eclosao (%)",v,true);
    tft.setTextColor(tft.color565(90,90,120),cBg);tft.setTextSize(1);
    tft.setCursor(10,174);tft.print("Valem a partir do dia de eclosao (alertas e");
    tft.setCursor(10,186);tft.print("automacao). \"--\" na 2a ovoscopia = sem 2a.");
  }else{
    snprintf(v,16,"%d",tipoEd.viraPorDia); tipoRow(30,"Viragens por dia",v,true);
    snprintf(v,16,"%d",tipoEd.viraAte); tipoRow(58,"Virar ate o dia",v,true);
    snprintf(v,16,"%02d",tipoEd.viraHH); tipoRow(86,"1a viragem - hora",v,true);
    snprintf(v,16,"%02d",tipoEd.viraMM); tipoRow(114,"1a viragem - minuto",v,true);
    tft.setTextColor(cSub,cBg);tft.setTextSize(1);tft.setCursor(10,146);tft.print("Horarios (a cada intervalo igual):");
    int inter=1440/tipoEd.viraPorDia, ini=tipoEd.viraHH*60+tipoEd.viraMM; char t[8]; int x=10,y=160;
    for(int i=0;i<tipoEd.viraPorDia&&i<24;i++){
      int m=(ini+i*inter)%1440; snprintf(t,8,"%02d:%02d",m/60,m%60);
      if(x>270){ x=10; y+=12; }
      tft.setTextColor(TFT_WHITE,cBg);tft.setCursor(x,y);tft.print(t); x+=42;
    }
  }
  // barra inferior: < | Salvar | Deletar/Cancelar | >
  uint16_t on=cLogo, off=tft.color565(60,55,25), nb=tft.color565(30,30,70);
  tft.fillRoundRect(4,200,46,34,8,nb); tft.fillTriangle(16,217,32,207,32,227,tipoPag>0?on:off);
  wizBotao(54,200,104,34,"Salvar",tft.color565(10,70,30),cGreen);
  if(tipoNovo) wizBotao(162,200,104,34,"Cancelar",nb,TFT_WHITE);
  else wizBotao(162,200,104,34,tipoDelArmado?"Certeza?":"Deletar",tipoDelArmado?tft.color565(140,20,20):tft.color565(70,14,14),TFT_WHITE);
  tft.fillRoundRect(270,200,46,34,8,nb); tft.fillTriangle(304,217,288,207,288,227,tipoPag<TIPO_PAGS-1?on:off);
}
static void tipoNomeOk(){ sanitizarNome(tipoNomeBuf); if(tipoNomeBuf[0]) strlcpy(tipoEd.nome,tipoNomeBuf,sizeof(tipoEd.nome)); appState=S_TIPO_EDIT; drawTipoEdit(); }
static void tipoNomeVoltar(){ appState=S_TIPO_EDIT; drawTipoEdit(); }
static void tiposPersistir(){ tiposSalvarCfg(); lastLocalTiposMs=millis(); pushTiposToFirestore(); aplicarLimitesDoTipo(); }
static void tipoNovoPadrao(){ tipoEd=tipoFabrica(0); strcpy(tipoEd.nome,"Novo tipo"); }
static void wizCriarTipoPers(){   // Fase 54: "Personalizado" abre o editor completo (nome, temperaturas, umidades, viragem, ovoscopia, eclosao)
  if(tiposN>=TIPOS_MAX){ arqToast("Limite de tipos atingido",false); drawWizTipo(); return; }
  tipoNovo=true; tipoSel=-1; tipoDelArmado=false; tipoPag=0; tipoNovoPadrao(); tipoOrig=tipoEd; wizVoltaDoTipo=true;
  appState=S_TIPO_EDIT; drawTipoEdit();
}
static void wizVoltarDoTipo(bool salvou){   // volta ao passo 2 do wizard (selecionando o tipo novo, se salvou)
  wizVoltaDoTipo=false; appState=S_CICLO_WIZ;
  if(salvou){ wizTipoIdx=tiposN-1; wizDias=tipos[tiposN-1].dias; wizStep=3; } else wizStep=2;
  drawWiz();
}
void handleTiposListaTap(int tx,int ty){
  if(ty<24){
    if(tx<40){ appState=S_SETTINGS; drawSettingsPanel(); }
    else if(tx>=284&&tiposN<TIPOS_MAX){ tipoNovo=true; tipoSel=-1; tipoDelArmado=false; tipoPag=0; tipoNovoPadrao(); tipoOrig=tipoEd; appState=S_TIPO_EDIT; drawTipoEdit(); }
    return;
  }
  int i=(ty-TIPOS_ROW_Y0)/TIPOS_ROW_H;
  if(ty>=TIPOS_ROW_Y0&&i>=0&&i<tiposN){ tipoNovo=false; tipoSel=i; tipoDelArmado=false; tipoPag=0; tipoEd=tipos[i]; tipoOrig=tipoEd; appState=S_TIPO_EDIT; drawTipoEdit(); }
}
// Ajusta um campo (pagina/linha) com clamps que mantem minima <= ideal <= maxima.
static void tipoAjusta(int pag,int linha,int dir){
  TipoOvo& t=tipoEd;
  if(pag==0){
    if(linha==1){ int d=t.dias+dir; if(d>=1&&d<=60){ t.dias=d; if(t.viraAte>d) t.viraAte=d; if(t.ovo1>=d) t.ovo1=(d>1)?d-1:1; if(t.ovo2>=d) t.ovo2=0; if(t.eclDia>d) t.eclDia=d; if(t.eclDia<=t.ovo1) t.eclDia=(t.ovo1+1<=d)?t.ovo1+1:d; } }
    else if(linha==2){ int v=t.temp10+dir; if(v>=300&&v<=420){ t.temp10=v; if(t.tempMin10>v) t.tempMin10=v; if(t.tempMax10<v) t.tempMax10=v; } }
    else if(linha==3){ int v=t.tempMin10+dir; if(v>=300&&v<=t.temp10) t.tempMin10=v; }
    else if(linha==4){ int v=t.tempMax10+dir; if(v>=t.temp10&&v<=420) t.tempMax10=v; }
  }else if(pag==1){
    if(linha==0){ int v=t.umid+dir; if(v>=20&&v<=90){ t.umid=v; if(t.umidMin>v) t.umidMin=v; if(t.umidMax<v) t.umidMax=v; } }
    else if(linha==1){ int v=t.umidMin+dir; if(v>=10&&v<=t.umid) t.umidMin=v; }
    else if(linha==2){ int v=t.umidMax+dir; if(v>=t.umid&&v<=100) t.umidMax=v; }
  }else if(pag==3){
    if(linha==0){ int v=t.ovo1+dir; if(v>=1&&v<t.dias){ t.ovo1=v; if(t.ovo2&&t.ovo2<=v) t.ovo2=(v+1<t.dias)?v+1:0; if(t.eclDia<=v) t.eclDia=(v+1<=t.dias)?v+1:t.dias; } }
    else if(linha==1){ int v=t.ovo2+dir; if(v<=t.ovo1) v=(dir>0)?t.ovo1+1:0; if(v==0||(v>t.ovo1&&v<t.dias)){ t.ovo2=v; if(v&&t.eclDia<=v) t.eclDia=(v+1<=t.dias)?v+1:t.dias; } }
    else if(linha==2){ int piso=(t.ovo2?t.ovo2:t.ovo1)+1; int v=t.eclDia+dir; if(v>=piso&&v<=t.dias) t.eclDia=v; }
    else if(linha==3){ int base=t.tempEcl10>0?t.tempEcl10:t.temp10; int v=base+dir; if(v>=300&&v<=420) t.tempEcl10=v; }   // Fase 69
    else if(linha==4){ int v=t.umidEcl+dir; if(v>=30&&v<=95) t.umidEcl=v; }
  }else{
    if(linha==0){ int v=t.viraPorDia+dir; if(v>=1&&v<=24) t.viraPorDia=v; }
    else if(linha==1){ int v=t.viraAte+dir; if(v>=0&&v<=t.dias) t.viraAte=v; }
    else if(linha==2){ int v=t.viraHH+dir; if(v<0) v=23; else if(v>23) v=0; t.viraHH=v; }
    else if(linha==3){ int v=t.viraMM+dir*5; if(v<0) v=55; else if(v>59) v=0; t.viraMM=v; }
  }
}
void handleTipoEditTap(int tx,int ty){
  if(ty<24){
    if(tx<40){ if(wizVoltaDoTipo) wizVoltarDoTipo(false); else { appState=S_TIPOS; drawTiposLista(); } }
    else if(tx>=280){   // restaurar: padrao de fabrica (Galinha/Codorna) ou, em tipos proprios, os valores de quando a tela abriu
      char nome[20]; strlcpy(nome,tipoEd.nome,sizeof(nome)); TipoOvo f;
      if(!tipoNovo && tipoFabricaPorNome(tipoOrig.nome,&f)) tipoEd=f; else tipoEd=tipoOrig;
      strlcpy(tipoEd.nome,tipoOrig.nome,sizeof(tipoEd.nome));
      tipoDelArmado=false; drawTipoEdit(); arqToast("Parametros restaurados",true); drawTipoEdit();
    }
    return;
  }
  if(ty>=200&&ty<=234){
    if(tx<50){ if(tipoPag>0){ tipoPag--; tipoDelArmado=false; drawTipoEdit(); } }
    else if(tx>=266){ if(tipoPag<TIPO_PAGS-1){ tipoPag++; tipoDelArmado=false; drawTipoEdit(); } }
    else if(tx>=54&&tx<=158){   // salvar
      sanitizarNome(tipoEd.nome);
      for(int i=0;i<tiposN;i++) if(i!=tipoSel && strcasecmp(tipos[i].nome,tipoEd.nome)==0){ arqToast("Ja existe um tipo com esse nome",false); drawTipoEdit(); return; }
      if(tipoNovo){ if(tiposN>=TIPOS_MAX) return; tipos[tiposN++]=tipoEd; } else tipos[tipoSel]=tipoEd;
      tiposPersistir();
      if(wizVoltaDoTipo){ wizVoltarDoTipo(true); arqToast("Tipo salvo",true); drawWiz(); return; }
      appState=S_TIPOS; drawTiposLista(); arqToast("Tipo salvo",true); drawTiposLista();
    }else if(tx>=162&&tx<=266){
      if(tipoNovo){ if(wizVoltaDoTipo) wizVoltarDoTipo(false); else { appState=S_TIPOS; drawTiposLista(); } return; }
      if(!tipoDelArmado){ tipoDelArmado=true; drawTipoEdit(); return; }
      if(tiposN<=1){ tipoDelArmado=false; arqToast("Mantenha pelo menos um tipo",false); drawTipoEdit(); return; }
      for(int i=tipoSel;i<tiposN-1;i++) tipos[i]=tipos[i+1];
      tiposN--; tiposPersistir(); appState=S_TIPOS; drawTiposLista(); arqToast("Tipo deletado",true); drawTiposLista();
    }
    return;
  }
  int linha=(ty-30)/28; if(ty<30||linha<0||linha>4) return;
  if(tipoPag==0&&linha==0){ strlcpy(tipoNomeBuf,tipoEd.nome,sizeof(tipoNomeBuf)); abrirKbTxt("Nome do tipo","Nome (max. 19 letras):",0,tipoNomeBuf,19,tipoNomeOk,tipoNomeVoltar); return; }
  int dir=(tx>=214&&tx<254)?-1:(tx>=262&&tx<302)?+1:0; if(!dir) return;
  tipoDelArmado=false;
  tipoAjusta(tipoPag,linha,dir);
  drawTipoEdit();
}

void debugTela(char c){   // depuracao pela serial: abre telas pra conferir o layout com o print da serial ('p')
  lastActivityMs=millis();
  if(c=='c'){ cicloConfirm=0; appState=S_CICLO; drawCicloScreen(); }
  else if(c=='C'){ debugCicloFalso=true; fbCicloAtivo=true; fbCicloPausado=false; fbCicloIniciadoEm=time(nullptr)-10L*86400L-3600L; fbCicloDuracaoDias=17; strcpy(fbCicloNome,"Teste codorna"); strcpy(fbCicloTipo,"Codorna"); aplicarLimitesDoTipo(); cicloConfirm=0; appState=S_CICLO; drawCicloScreen(); }
  else if(c=='g'){ appState=S_SEG; drawSeg(); }
  else if(c=='l'){ abrirHistLista(true); }
  else if(c=='k'){ if(cidxVisN>0){ histSel=cidxVis[0]; abrirHistCiclo(); } }
  else if(c=='m'){ appState=S_MAIN; drawMainScreen(); }
  else if(c=='z'){ appState=S_CAMERA; drawCameraScreen(); }
  else if(c=='a'){ abrirAdv(); }
  else if(c=='d'){ autoCh=0; autoEdPag=0; appState=S_AUTO_EDIT; drawAutoEdit(); }
  else if(c=='w'){ advSel=0; advPag=(advPag+1)%ADV_PAGS; if(advN==0){ advN=1; advDefaultRegra(adv[0]); } appState=S_ADV_EDIT; drawAdvEdit(); }
  else if(c=='i'){ File f=SD.open("/ciclos.idx",FILE_READ); if(!f){ Serial.println("[idx] sem arquivo"); } else { while(f.available()) Serial.write(f.read()); f.close(); Serial.println("[idx] fim"); } }
  else if(c=='n'||c=='N'){ settingsPage=(c=='n')?0:1; appState=S_SETTINGS; drawSettingsPanel(); }
  else if(c=='Q'){ Serial.println("[DBG] encerrando o ciclo (Q)"); if(fbCicloAtivo) cicloAcaoEncerrar(false); }
  else if(c=='e'){ tipoNovo=false; tipoSel=0; tipoEd=tipos[0]; tipoOrig=tipoEd; tipoPag=3; appState=S_TIPO_EDIT; drawTipoEdit(); }
}

// ═══════════════════════════════════════════════════════════════
// TRATAMENTO DE TOQUE
// ═══════════════════════════════════════════════════════════════
void appendKbChar(char c){if(typingSSID){if(manualSSIDLen<32){manualSSID[manualSSIDLen++]=c;manualSSID[manualSSIDLen]=0;}drawSSIDField();}else{if(passLen<64){passBuf[passLen++]=c;passBuf[passLen]=0;}drawPassField();tft.setTextSize(1);tft.setTextColor(tft.color565(58,58,98),cBg);tft.fillRect(225,32,80,10,cBg);tft.setCursor(225,32);tft.printf("%d/64",passLen);}}
void deleteKbChar(){if(typingSSID){if(manualSSIDLen>0){manualSSID[--manualSSIDLen]=0;}drawSSIDField();}else{if(passLen>0){passBuf[--passLen]=0;}drawPassField();tft.setTextSize(1);tft.setTextColor(tft.color565(58,58,98),cBg);tft.fillRect(225,32,80,10,cBg);tft.setCursor(225,32);tft.printf("%d/64",passLen);}}
void handleOK(){if(typingSSID){if(manualSSIDLen>0){if(manualIsOpen){strlcpy(selSSID,manualSSID,33);connectWifi(selSSID,"");}else{strlcpy(selSSID,manualSSID,33);typingSSID=false;memset(passBuf,0,65);passLen=0;kbMode=0;showPass=false;appState=S_WIFI_PASS;drawPassScreen();}}}else{if(passLen>=8) connectWifi(selSSID,passBuf);else{tft.setTextSize(1);tft.setTextColor(cRed,cBg);tft.fillRect(5,78,250,10,cBg);tft.setCursor(5,78);tft.print("Minimo 8 caracteres");}}}
void handleKbTap(int tx,int ty){
  if(!typingSSID&&ty>=40&&ty<64&&tx>=285){showPass=!showPass;drawPassField();return;}
  if(ty<28&&tx<62){if(appState==S_WIFI_PASS||appState==S_WIFI_ADD){appState=S_WIFI_LIST;drawWifiList();}return;}
  if(appState==S_WIFI_ADD&&ty>=68&&ty<88){if(tx<98){manualIsOpen=true;drawAddNetScreen();drawKeyboard();}else if(tx>=104&&tx<196){manualIsOpen=false;drawAddNetScreen();drawKeyboard();}return;}
  const char(*KB)[11]=(kbMode==0?KBL:(kbMode==1?KBU:KBN));
  if(ty>=KR1Y&&ty<KR1Y+KH){int ki=(tx-5)/31;if(ki>=0&&ki<10) appendKbChar(KB[0][ki]);}
  else if(ty>=KR2Y&&ty<KR2Y+KH){int ki=(tx-20)/31;if(ki>=0&&ki<9) appendKbChar(KB[1][ki]);}
  else if(ty>=KR3Y&&ty<KR3Y+KH){if(tx<50){kbMode=(kbMode==0)?1:(kbMode==1)?0:2;drawKeyboard();return;}else if(tx>=262) deleteKbChar();else{int ki=(tx-50)/30;if(ki>=0&&ki<7) appendKbChar(KB[2][ki]);}}
  else if(ty>=KR4Y){if(tx<63){kbMode=(kbMode==2)?0:2;drawKeyboard();return;}else if(tx>=257){handleOK();return;}else appendKbChar(' ');}
}

// Fase 29 — espelha handleKbTap() acima (mesmo grid de teclas/coordenadas),
// mas escreve em logsSearchBuf e o "OK" aplica o filtro nos logs em vez de
// conectar no WiFi. Duplicado de propósito (não generalizado dentro de
// handleKbTap) pra não arriscar nenhuma regressão no fluxo de WiFi já
// testado e em produção -- ver comentário em cima das novas globais de
// busca dos logs.
void handleLogsSearchKbTap(int tx,int ty){
  if(ty<28&&tx<62){ logsBuscando=false; drawLogsScreen(); return; }
  const char(*KB)[11]=(kbMode==0?KBL:(kbMode==1?KBU:KBN));
  if(ty>=KR1Y&&ty<KR1Y+KH){int ki=(tx-5)/31;if(ki>=0&&ki<10) appendLogsSearchChar(KB[0][ki]);}
  else if(ty>=KR2Y&&ty<KR2Y+KH){int ki=(tx-20)/31;if(ki>=0&&ki<9) appendLogsSearchChar(KB[1][ki]);}
  else if(ty>=KR3Y&&ty<KR3Y+KH){if(tx<50){kbMode=(kbMode==0)?1:(kbMode==1)?0:2;drawKeyboard();return;}else if(tx>=262) deleteLogsSearchChar();else{int ki=(tx-50)/30;if(ki>=0&&ki<7) appendLogsSearchChar(KB[2][ki]);}}
  else if(ty>=KR4Y){
    if(tx<63){kbMode=(kbMode==2)?0:2;drawKeyboard();return;}
    else if(tx>=257){
      strlcpy(logsSearchAtivo, logsSearchBuf, sizeof(logsSearchAtivo));
      logsSearchAtivoLen = logsSearchLen;
      logsBuscando=false;
      aplicarFiltroLogs();
      drawLogsScreen();
      return;
    }
    else appendLogsSearchChar(' ');
  }
}
// ═══════════════════════════════════════════════════════════════
// Fase 78 -- Configuracoes > Hardware (2 paginas de 5 linhas, - / +). Salva sozinho 3 s depois da ultima mudanca (automacaoTick).
// ═══════════════════════════════════════════════════════════════
static int hwPag=0;
// Fase 86 -- layout denso: 6 cartoes por pagina (36 px cada), nome pequeno em cima, valor grande embaixo, botoes - / + grandes; sem textos de explicacao
#define HW_Y0 21
#define HW_PITCH 36
#define HW_N 6
static void hwRow(int i,const char* lb,const char* val,uint16_t cor=TFT_WHITE,const char* extra=nullptr,bool botoes=true){
  int y=HW_Y0+i*HW_PITCH; uint16_t bg=tft.color565(16,16,46);
  uiCard(4,y,312,HW_PITCH-3);
  tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(12,y+4); tft.print(lb);
  tft.setTextSize(2); tft.setTextColor(cor,bg); tft.setCursor(12,y+15); tft.print(val);
  if(extra){ int w=tft.textWidth(val); tft.setTextSize(1); tft.setTextColor(cSub,bg); tft.setCursor(20+w,y+19); tft.print(extra); }
  if(!botoes) return;
  uint16_t nb=tft.color565(40,40,110), bd=tft.color565(90,90,180);
  tft.fillRoundRect(228,y+4,38,26,6,nb); tft.drawRoundRect(228,y+4,38,26,6,bd); tft.fillRect(238,y+16,18,3,TFT_WHITE);
  tft.fillRoundRect(272,y+4,38,26,6,nb); tft.drawRoundRect(272,y+4,38,26,6,bd); tft.fillRect(282,y+16,18,3,TFT_WHITE); tft.fillRect(289,y+9,3,13,TFT_WHITE);
}
void drawHardwareScreen(){
  tft.fillScreen(cBg);
  char pg[10]; snprintf(pg,sizeof(pg),"%d/3 >",hwPag+1);
  uiTopo("HARDWARE",pg);
  char v[16];
  if(hwPag==0){
    snprintf(v,16,"%u L",(unsigned)hw.volL);  hwRow(0,"Volume da caixa",v);
    hwRow(1,"Material da caixa",HW_ISO_NOME[hw.iso]);
    hwRow(2,"Tipo de aquecedor",HW_AQ_NOME[hw.aqTipo]);
    snprintf(v,16,"%u W",(unsigned)hw.aqW);   hwRow(3,"Potencia de cada aquecedor",v);
    snprintf(v,16,"%u",(unsigned)hw.aqQtd);   hwRow(4,"Quantidade de aquecedores",v);
    hwRow(5,"Rele do aquecedor",HW_REL_NOME[hw.rele]);
  }else if(hwPag==1){
    hwRow(0,"Tipo de umidificador",HW_UM_NOME[hw.umTipo]);
    snprintf(v,16,"%u",(unsigned)hw.umMlH);   hwRow(1,"Vazao de cada umidificador (ml/h)",v);
    snprintf(v,16,"%u",(unsigned)hw.umQtd);   hwRow(2,"Quantidade de umidificadores",v);
    snprintf(v,16,"%u",(unsigned)hw.fanQtd);  hwRow(3,"Quantidade de fans (CH8)",v);
  }else{   // pagina 3 -- bomba de agua (CH3), EM DESENVOLVIMENTO (Fases 83/85/88)
    hwRow(0,"Bomba de agua CH3 (EM DESENV.)",hw.bomba?"Instalada":"Nao",hw.bomba?cGreen:cSub);
    if(hw.bombaMlS10){ snprintf(v,16,"%.1f",hw.bombaMlS10/10.0f); hwRow(1,"Vazao com a mangueira cheia (ml/s)",v,TFT_WHITE,"ml/s"); }
    else hwRow(1,"Vazao com a mangueira cheia (ml/s)","SEM MEDIDA",hw.bomba?tft.color565(230,180,60):cSub);
    snprintf(v,16,"%.1f s",hw.bombaPrimeS10/10.0f); hwRow(2,"Tempo ate a agua sair (mangueira vazia)",v);
    if(hw.bombaMlS10){ char ex[24]; snprintf(ex,sizeof(ex),"=15 ml (maximo %lu s)",(unsigned long)(BOMBA_MAX_ON_MS/1000UL)); snprintf(v,16,"%.1f s",bombaPulsoMs()/1000.0f); unsigned long pm=bombaPulsoMs(); hwRow(3,"Duracao de cada pulso",v,(pm<300UL||pm>BOMBA_MAX_ON_MS)?cRed:cGreen,ex,false); }
    else hwRow(3,"Duracao de cada pulso","--",cSub,nullptr,false);
  }
}
void debugHardware(int pag){ lastActivityMs=millis(); hwPag=pag; appState=S_HARDWARE; drawHardwareScreen(); }
static void hwAjusta(int pag,int linha,int dir){
  hwSujo=true; hwUltMs=millis();
  if(pag==0){
    if(linha==0){ int p=(hw.volL<20)?1:(hw.volL<100)?5:10; int v=(int)hw.volL+dir*p; if(v>=1&&v<=500) hw.volL=v; }
    else if(linha==1) hw.iso=(hw.iso+5+dir)%5;
    else if(linha==2) hw.aqTipo=(hw.aqTipo+4+dir)%4;
    else if(linha==3){ int p=(hw.aqW<100)?5:(hw.aqW<300)?10:50; int v=(int)hw.aqW+dir*p; if(v>=5&&v<=2000) hw.aqW=v; }
    else if(linha==4){ int v=(int)hw.aqQtd+dir; if(v>=1&&v<=8) hw.aqQtd=v; }
    else if(linha==5) hw.rele=hw.rele?0:1;
  }else if(pag==1){
    if(linha==0) hw.umTipo=(hw.umTipo+3+dir)%3;
    else if(linha==1){ int p=(hw.umMlH<500)?50:100; int v=(int)hw.umMlH+dir*p; if(v>=10&&v<=3000) hw.umMlH=v; else if(v<10 && hw.umMlH>10) hw.umMlH=10; }
    else if(linha==2){ int v=(int)hw.umQtd+dir; if(v>=0&&v<=4) hw.umQtd=v; }
    else if(linha==3){ int v=(int)hw.fanQtd+dir; if(v>=0&&v<=8) hw.fanQtd=v; }
  }else{   // pagina 3: bomba de agua
    if(linha==0) hw.bomba=hw.bomba?0:1;   // Fase 83
    else if(linha==1){ int v=(int)hw.bombaMlS10+dir*5; if(v<0) v=0; if(v>500) v=500; hw.bombaMlS10=v; }   // Fase 85: passo de 0,5 ml/s
    else if(linha==2){ int v=(int)hw.bombaPrimeS10+dir*5; if(v<0) v=0; if(v>100) v=100; hw.bombaPrimeS10=v; }   // Fase 88: passo de 0,5 s
  }
}
void handleHardwareTap(int tx,int ty){
  if(ty<20){
    if(tx>=240){ hwPag=(hwPag+1)%3; drawHardwareScreen(); }
    else if(tx<110){ appState=S_SETTINGS; drawSettingsPanel(); }
    return;
  }
  int linha=(ty-HW_Y0)/HW_PITCH; if(ty<HW_Y0||linha<0||linha>=HW_N||(hwPag==1&&linha>3)||(hwPag==2&&linha>2)) return;
  int dir=(tx>=220&&tx<268)?-1:(tx>=268&&tx<316)?+1:0; Serial.printf("[HW] toque x=%d y=%d pag=%d linha=%d dir=%d\n",tx,ty,hwPag,linha,dir); if(!dir) return;
  hwAjusta(hwPag,linha,dir); drawHardwareScreen();
}

static void settingsAbre(int id){
  switch(id){
    case 0: wifiFromHome=false; appState=S_WIFI_LIST; startWifiScan(); break;
    case 1: appState=S_DATETIME; loadEditFromRtc(); drawDateTimeScreen(); break;
    case 2: appState=S_ATUADORES; atPageOff=0; drawAtuadoresScreen(); break;
    case 3: telaSub=0; appState=S_DISPLAY; drawDisplayScreen(); break;
    case 4: appState=S_AUDIO; drawAudioScreen(); break;
    case 5: appState=S_MEMORIA; memPage=0; drawMemoriaScreen(); drawMemCurrentPageContent(); break;
    case 6: appState=S_SYS_INFO; drawSysInfo(); break;
    case 7: abrirTelaLogs(); break;
    case 8: appState=S_ALERTAS; drawAlertasScreen(); break;
    case 9: abrirAuto(); break;
    case 10: abrirTipos(); break;
    case 11: abrirArquivos(); break;
    case 12: appState=S_SEG; drawSeg(); break;
    case 13: abrirRelatorios(); break;
    case 14: nomeAbrirEdicao(); break;
    case 15: abrirHistLista(true); break;
    case 16: hwPag=0; appState=S_HARDWARE; drawHardwareScreen(); break;   // Fase 78
    case 17: appState=S_ADXL; drawAdxlScreen(); break;                      // Fase 93
    case 18: appState=S_FANS; fansDraw(); break;                            // Fase 102
  }
}
void handleSettingsTap(int tx,int ty){
  if(ty<20){
    if(tx>=240){ settingsPage=(settingsPage+1)%SETTINGS_TOTAL_PAGES; drawSettingsPanel(); }
    else { appState=S_MAIN; drawMainScreen(); }
    return;
  }
  int c=(tx-SET_CX0)/(SET_CW+SET_GX), r=(ty-SET_CY0)/(SET_CH+SET_GY);
  if(tx<SET_CX0||c<0||c>2||r<0||r>3) return;
  const uint8_t* ids; int n=settingsItensPagina(&ids); int idx=r*3+c; if(idx>=n) return;
  settingsDesenhaCelula(idx,ids[idx],true); delay(90);   // efeito de toque
  settingsAbre(ids[idx]);
}
void openSettings(){appState=S_SETTINGS;drawSettingsPanel();}

void handleTap(int tx,int ty){
  switch(appState){
    case S_MAIN:
      if(ty<28&&tx>=MENU_BTN_X){ openSettings(); break; }
      if(ty<30&&tx<130){ appState=S_SYS_INFO; drawSysInfo(); break; }                       // Fase 65: atalho pelo nome "ChocaIoT"
      if(ty<30&&tx>=170&&tx<MENU_BTN_X-2){ appState=S_DATETIME; loadEditFromRtc(); drawDateTimeScreen(); break; }   // atalho pela hora
      if(ty<30&&tx>=WIFI_ICX-14&&tx<=WIFI_ICX+20){ wifiFromHome=true; appState=S_WIFI_LIST; startWifiScan(); break; }   // Fase 61: atalho pelo icone de conexao
      // Fase 21 — cards Temperatura/Umidade abrem a tela de detalhe
      // (estatisticas desde o boot + faixa ideal).
      if(ty>=CARD_Y0&&ty<=CARD_Y0+CARD_H&&tx>=4&&tx<=104){ sensorDetalheEhTemp=true; sensorDetalheCo2=false; appState=S_SENSOR_DETAIL; drawSensorDetailScreen(); break; }
      if(ty>=CARD_Y0&&ty<=CARD_Y0+CARD_H&&tx>=112&&tx<=212){ sensorDetalheEhTemp=false; sensorDetalheCo2=false; appState=S_SENSOR_DETAIL; drawSensorDetailScreen(); break; }
      if(ty>=CARD_Y0&&ty<=CARD_Y0+CARD_H&&tx>=216&&tx<=316){ sensorDetalheCo2=true; sensorChart7d=false; appState=S_SENSOR_DETAIL; drawSensorDetailScreen(); break; }   // Fase 115: card CO2
      if(ty>=CARD_Y1&&ty<=CARD_Y1+CARD_H&&tx>=4&&tx<=104){ appState=S_STATUS; drawStatusDetalhe(true); break; }   // Fase 77: janela completa do status
      // Card "Ciclo" (Fase 11) — gatilho do controle local, mesma posição
      // do antigo card "Timer" estático.
      if(ty>=CARD_Y1&&ty<=CARD_Y1+CARD_H&&tx>=216&&tx<=316){ cicloConfirmandoParar=false; appState=S_CICLO; drawCicloScreen(); break; }
      // Atuadores saiu da home (Fase 10 — foi pra Configurações); a zona
      // de toque de baixo-esquerda agora não faz mais nada.
      if(ty>=CARD_Y2&&ty<=CARD_Y2+CARD_H&&tx>=216&&tx<=316){ appState=S_CAMERA; drawCameraScreen(); break; }
      if(ty>=CARD_Y2&&ty<=CARD_Y2+CARD_H&&tx>=4&&tx<=104){ appState=S_GUIA; drawGuiaLista(); break; }   // Fase 37
      break;

    case S_SENSOR_DETAIL:   // Fase 21 — toque volta pra home; o botao do rodape alterna Hoje/7 dias (Fase 47)
      if(ty>=222&&tx>=262){
        if(sensorDetalheCo2){ sensorChart7d=!sensorChart7d; if(sensorChart7d) histCarregar(7,0); drawSensorDetailScreen(); break; }   // Fase 115: Hoje <-> 7 dias
        abrirHistLista(sensorDetalheEhTemp); break; }   // Fase 55: historico por ciclo
      sensorChart7d=false; sensorDetalheCo2=false; appState=S_MAIN; drawMainScreen();
      break;

    case S_CAMERA:
      handleCameraTap(tx,ty);   // Fase 50
      break;

    case S_SETTINGS: handleSettingsTap(tx,ty); break;   // Fase 62

    case S_GUIA:       handleGuiaListaTap(tx,ty); break;        // Fase 37
    case S_GUIA_TEXTO: handleGuiaTextoTap(tx,ty); break;        // Fase 37
    case S_FASES:      handleFaseTap(tx,ty); break;             // Fase 38
    case S_ARQ:        handleArqListaTap(tx,ty); break;         // Fase 41
    case S_ARQ_ACAO:   handleArqAcaoTap(tx,ty); break;
    case S_ARQ_NOME:   handleArqNomeKbTap(tx,ty); break;
    case S_ARQ_DEL:    handleArqDelTap(tx,ty); break;
    case S_ARQ_WALL:   handleArqWallTap(tx,ty); break;
    case S_SEG:        handleSegTap(tx,ty); break;              // Fase 46
    case S_PIN:        handlePinTap(tx,ty); break;
    case S_LOCK:       handleLockTap(tx,ty); break;
    case S_TXTVIEW:    handleTxtViewTap(tx,ty); break;
    case S_IMGVIEW:    handleImgViewTap(tx,ty); break;

    case S_DATETIME: handleDateTimeTap(tx,ty); break;
    case S_DISPLAY: handleDisplayTap(tx,ty); break;
    case S_HARDWARE: handleHardwareTap(tx,ty); break;
    case S_ADXL:     handleAdxlTap(tx,ty); break;   // Fase 93
    case S_FANS:     fansTap(tx,ty); break;         // Fase 102
    case S_FANREGRAS: fanRegrasTap(tx,ty); break;   // Fase 112

    case S_MEMORIA:   // Fase 64 -- 3 paginas (ao vivo | arquivos do sistema | arquivos do usuario)
      if(uiVoltar(tx,ty)){appState=S_SETTINGS;drawSettingsPanel();break;}
      if(ty<20&&tx>=200){ memPage=(memPage+1)%MEM_TOTAL_PAGES; drawMemoriaScreen(); drawMemCurrentPageContent(); break; }
      if(memPage==2 && ty>=192&&ty<=226&&tx>=20&&tx<=300){ abrirArquivos(); break; }
      break;

    case S_WIFI_LIST:{
      if(ty<28){if(tx<62){if(wifiFromHome){wifiFromHome=false;appState=S_MAIN;drawMainScreen();}else{appState=S_SETTINGS;drawSettingsPanel();}break;}if(tx>=220){memset(manualSSID,0,33);manualSSIDLen=0;manualIsOpen=false;typingSSID=true;kbMode=0;appState=S_WIFI_ADD;drawAddNetScreen();drawKeyboard();break;}}
      int vis=4,rh=44,sy=38;if(listOff>0&&ty>=28&&ty<38){listOff--;drawWifiList();break;}int by2=sy+vis*rh;if(listOff+vis<numNets&&ty>=by2&&ty<by2+12){listOff++;drawWifiList();break;}
      if(numNets>0){for(int i=0;i<min(vis,numNets-listOff);i++){if(ty>=sy+i*rh&&ty<sy+(i+1)*rh){int ni=i+listOff;if(wifiOK&&WiFi.SSID(ni)==String(savedSSID)){detailShowPass=false;appState=S_WIFI_DETAIL;drawWifiDetail();break;}if(!wifiOK&&savedSSID[0]&&WiFi.SSID(ni)==String(savedSSID)){connectWifi(savedSSID,savedPass);break;}WiFi.SSID(ni).toCharArray(selSSID,33);if(WiFi.encryptionType(ni)==WIFI_AUTH_OPEN){connectWifi(selSSID,"");}else{memset(passBuf,0,65);passLen=0;kbMode=0;showPass=false;typingSSID=false;appState=S_WIFI_PASS;drawPassScreen();}break;}}}
      break;}
    case S_WIFI_PASS:case S_WIFI_ADD: handleKbTap(tx,ty); break;
    case S_WIFI_DETAIL:{
      if(ty<28&&tx<62){appState=S_WIFI_LIST;drawWifiList();break;}
      if(ty>=WD_BY&&ty<=WD_BY+54){
        int idx=(tx<108)?0:(tx<212)?1:2;
        wifiBotoesDetalhe(idx); delay(120);   // efeito de toque
        if(idx==0){ detailShowPass=!detailShowPass; drawWifiDetail(); }
        else if(idx==1) disconnectWifi();
        else forgetWifi();
      }
      break;}

    case S_ATUADORES:{
      // Agora vive dentro de Configuracoes (nao mais um card na home) —
      // volta pra la, nao pra tela principal.
      if(uiVoltar(tx,ty)){appState=S_SETTINGS;drawSettingsPanel();break;}
      if(ty<20 && tx>=200){ nextAtuadoresPage(); drawAtuadoresScreen(); break; }
      if(ty>=22 && ty<54){   // Fase 110: chave da automacao
        autoAtivo=!autoAtivo; autoPersistir();
        logEvento("Automacao",autoAtivo?"Automacao ATIVADA (tela Atuadores)":"Automacao DESATIVADA (tela Atuadores)",true);
        drawAtuadoresScreen(); break;
      }
      int rowIdx = (ty - AT_ROW_Y0) / AT_ROW_H;
      if(ty>=AT_ROW_Y0 && rowIdx>=0 && rowIdx<AT_VIS){
        int ch = atPageOff+rowIdx;
        if(ch==COOLER_CH){
          // Fase 36 — bloqueado de propósito: esse canal é controlado por
          // histerese (Fase 35), um toque manual aqui seria sobrescrito
          // no próximo ciclo de 5s de qualquer forma -- em vez de deixar
          // isso confuso, o toque não faz nada, só avisa o motivo.
          int y = AT_ROW_Y0 + rowIdx*AT_ROW_H;
          tft.fillRect(0,y,320,AT_ROW_H-2,tft.color565(40,30,10));
          tft.setTextColor(tft.color565(255,190,90),tft.color565(40,30,10));tft.setTextSize(1);
          tft.setCursor(8,y+14);tft.print("Controlado automaticamente (histerese de temperatura)");
          delay(900);
          drawAtuadoresScreen();
        }else if(ch<8){
          setRelayAndSync(ch, !relayState[ch]); drawAtuadoresScreen();
        }
      }
      break;}

    // Controle Local do Ciclo (Fase 11) — espelha o modal "Configurações
    // do Ciclo" do PWA: iniciar, pausar/retomar, parar (com confirmação),
    // ajustar duração. Ver pushCiclo*() (secao FIREBASE) pelas escritas.
    case S_CICLO:      handleCicloTap(tx,ty); break;            // Fase 42 (wizard + tela cheia)
    case S_STATUS:     handleStatusTap(tx,ty); break;           // Fase 77
    case S_CICLO_WIZ:  handleWizTap(tx,ty); break;
    case S_KBTXT:      handleKbTxtTap(tx,ty); break;
    case S_TIPOS:      handleTiposListaTap(tx,ty); break;
    case S_TIPO_EDIT:  handleTipoEditTap(tx,ty); break;

    case S_SYS_INFO:   // Fase 34 -- acoes viraram um tile na propria grade (era icone no header)
      if(uiVoltar(tx,ty)){appState=S_SETTINGS;drawSettingsPanel();break;}
      if(ty>=133&&ty<=163&&tx>=166&&tx<=234){
        // Desliga tudo antes de reiniciar, por segurança
        for(int i=0;i<8;i++) setRelay(i,false);
        tft.fillScreen(cBg);
        efeitoSpinnerReiniciar(cLogo);
        tft.setTextColor(cLogo,cBg);tft.setTextSize(2);
        { const char* m="Reiniciando..."; tft.setCursor(160-tft.textWidth(m)/2,140); tft.print(m); }
        delay(300);
        ESP.restart();
      }else if(ty>=133&&ty<=163&&tx>=244&&tx<=312){
        desligarLocal();  // nunca retorna (deep sleep)
      }
      break;

    case S_LOGS:   // Fase 21/29; Fase 64: filtros Data/Tipo, sem rodape (paginas: topo "< n/N >" ou tocar nos lados da lista)
      if(logsBuscando){ handleLogsSearchKbTap(tx,ty); break; }
      if(logsConfirmandoLimpar){
        if(ty>=120&&ty<=160&&tx>=30&&tx<=140){
          SD.remove(SD_LOG_PATH); SD.remove("/chocaiot.old");
          logsConfirmandoLimpar=false;
          logEvento("Logs","Log local limpo pelo usuario (touchscreen)",false);
          carregarLogsBuffer();  // recarrega (vazio + a linha "Log limpo" que acabou de ser gravada)
          drawLogsScreen();
        }else if(ty>=120&&ty<=160&&tx>=180&&tx<=290){
          logsConfirmandoLimpar=false; drawLogsScreen();
        }
        break;
      }
      if(ty<20){
        int totalPag=logsTotalPaginas();
        if(tx<80){ appState=S_SETTINGS; drawSettingsPanel(); break; }
        if(tx>=96&&tx<150){ if(logsPage>0){ logsPage--; drawLogsScreen(); } break; }
        if(tx>=150&&tx<206){ if(logsPage<totalPag-1){ logsPage++; drawLogsScreen(); } break; }
        if(tx>=240&&tx<=278){
          if(logsSearchAtivoLen>0){ logsSearchAtivo[0]=0; logsSearchAtivoLen=0; aplicarFiltroLogs(); drawLogsScreen(); }   // com filtro de texto ativo, a lupa limpa
          else { logsSearchBuf[0]=0; logsSearchLen=0; logsBuscando=true; kbMode=0; drawLogsScreen(); }
          break;
        }
        if(tx>=282&&tx<=310&&sdOK){ logsConfirmandoLimpar=true; drawLogsScreen(); break; }
        break;
      }
      if(ty<38){   // chips: Data | Tipo (cada toque avanca; depois do ultimo volta a "todos")
        if(tx<158){ logsFiltroDia=(logsFiltroDia+1>=logsNDatas)?-1:logsFiltroDia+1; }
        else      { logsFiltroTipo=(logsFiltroTipo+1>=logsNTipos)?-1:logsFiltroTipo+1; }
        aplicarFiltroLogs(); drawLogsScreen(); break;
      }
      { int totalPag=logsTotalPaginas();   // lados da lista trocam de pagina
        if(tx<70 && logsPage>0){ logsPage--; drawLogsScreen(); }
        else if(tx>=250 && logsPage<totalPag-1){ logsPage++; drawLogsScreen(); } }
      break;


    case S_AUDIO:    // Fase 22 -- so o voltar (faixa do topo) funciona, controles sao decorativos de proposito
      if(uiVoltar(tx,ty)){appState=S_SETTINGS;drawSettingsPanel();}
      break;
    case S_ALERTAS:  handleAlertasTap(tx,ty); break;   // Fase 49
    case S_RELS:     handleRelsTap(tx,ty); break;      // Fase 48
    case S_AUTO:     handleAutoTap(tx,ty); break;      // Fase 52
    case S_AUTO_EDIT: handleAutoEditTap(tx,ty); break;
    case S_HIST:     handleHistTap(tx,ty); break;      // Fase 53
    case S_HISTL:    handleHistListaTap(tx,ty); break; // Fase 55
    case S_ADV:      handleAdvTap(tx,ty); break;       // Fase 57
    case S_ADV_EDIT: handleAdvEditTap(tx,ty); break;

    default:break;
  }
}

void handleRelease(int sx,int sy,int ex,int ey){
  int dx=ex-sx,dy=ey-sy;unsigned long dur=millis()-tDown;
  if(abs(dx)<35&&abs(dy)<35&&dur<500&&millis()-lastTap>DEBOUNCE_MS){lastTap=millis();handleTap(sx,sy);}
}

// ═══════════════════════════════════════════════════════════════
// DIAGNÓSTICO DE REBOOT — motivo do último reset (brownout, watchdog,
// painc, etc). Existia como pendência (CLAUDE.md, secao 8: "resets
// aleatórios em investigação") — nunca tinha sido checado no firmware.
// Mostrado no Serial no boot E na tela Sistema (drawSysInfo), pra dar pra
// ver sem precisar de laptop/monitor serial depois de um reset sozinho.
// ═══════════════════════════════════════════════════════════════
void logResetReason(){
  esp_reset_reason_t r = esp_reset_reason();
  const char* longo;
  switch(r){
    case ESP_RST_POWERON:   longo="Ligou normalmente";        strlcpy(lastResetReasonShort,"Normal",16);      resetFoiAnormal=false; break;
    case ESP_RST_EXT:       longo="Reset externo (pino RST)"; strlcpy(lastResetReasonShort,"Externo",16);     resetFoiAnormal=false; break;
    case ESP_RST_SW:        longo="Reset por software";       strlcpy(lastResetReasonShort,"Software",16);    resetFoiAnormal=false; break;
    case ESP_RST_DEEPSLEEP: longo="Saiu de deep sleep";       strlcpy(lastResetReasonShort,"Deep sleep",16);  resetFoiAnormal=false; break;
    case ESP_RST_PANIC:     longo="PANIC (crash/excecao)";    strlcpy(lastResetReasonShort,"Panic!",16);      resetFoiAnormal=true;  break;
    case ESP_RST_INT_WDT:   longo="Watchdog de interrupcao";  strlcpy(lastResetReasonShort,"Watchdog",16);    resetFoiAnormal=true;  break;
    case ESP_RST_TASK_WDT:  longo="Watchdog de tarefa (loop travado)"; strlcpy(lastResetReasonShort,"Watchdog",16); resetFoiAnormal=true; break;
    case ESP_RST_WDT:       longo="Outro watchdog";           strlcpy(lastResetReasonShort,"Watchdog",16);    resetFoiAnormal=true;  break;
    case ESP_RST_BROWNOUT:  longo="BROWNOUT - queda de tensao"; strlcpy(lastResetReasonShort,"Brownout!",16); resetFoiAnormal=true;  break;
    default:                longo="Desconhecido";             strlcpy(lastResetReasonShort,"Desconhecido",16);resetFoiAnormal=true;  break;
  }
  Serial.print("Motivo do ultimo reset: ");
  Serial.println(longo);
  if(r==ESP_RST_BROWNOUT){
    Serial.println("!! Queda de tensao no boot/durante uso -- suspeito nº1 pra tela branca/corrompida "
                    "e resets aleatorios. Verificar capacitor de desacoplamento perto do VCC do "
                    "display e se o inrush dos reles (mesmo trilho de 5V) esta afetando a tensao.");
  }

  // Fase 19 — contador de loop (ver comentario do RTC_DATA_ATTR acima).
  if(resetFoiAnormal){
    bootLoopCounter++;
    Serial.printf("[Boot] Reset anormal (%s) -- tentativa consecutiva #%u nesta sessao de energia.\n",
                   lastResetReasonShort, (unsigned)bootLoopCounter);
  }else{
    if(bootLoopCounter>0){
      Serial.printf("[Boot] Boot normal (%s) apos %u reset(s) anormal(is) seguido(s) -- contador zerado.\n",
                     lastResetReasonShort, (unsigned)bootLoopCounter);
    }
    bootLoopCounter=0;
  }
}

// Fase 113 -- ONDE o ultimo Panic/Watchdog aconteceu. O ESP32 ja grava o core dump na particao "coredump" ao quebrar;
// aqui o resumo (tarefa, PC, causa, endereco, backtrace) vai pro log (SD + nuvem) e a particao e apagada, pra nao repetir.
// Decodificar: xtensa-esp32s3-elf-addr2line -pfiaC -e <firmware.elf do MESMO build> <pc> <bt...>
void coredumpRelatar(){
  if(esp_core_dump_image_check()!=ESP_OK) return;
  esp_core_dump_summary_t* s=(esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
  if(!s) return;
  if(esp_core_dump_get_summary(s)==ESP_OK){
    char bt[120]; bt[0]=0; size_t n=0;
    uint32_t d=s->exc_bt_info.depth; if(d>8) d=8;
    for(uint32_t i=0;i<d && n<sizeof(bt)-12;i++) n+=snprintf(bt+n,sizeof(bt)-n,"%s%08x",i?",":"",(unsigned)s->exc_bt_info.bt[i]);
    char msg[240];
    snprintf(msg,sizeof(msg),"Crash task=%s pc=%08x causa=%u vaddr=%08x bt=%s%s",s->exc_task,(unsigned)s->exc_pc,(unsigned)s->ex_info.exc_cause,(unsigned)s->ex_info.exc_vaddr,bt,s->exc_bt_info.corrupted?" (corrompido)":"");
    Serial.printf("[CRASH] %s\n",msg);
    logEvento("Panic",msg,true);
  }else{
    Serial.println("[CRASH] core dump presente, mas o resumo nao pode ser lido");
    logEvento("Panic","core dump presente (resumo ilegivel)",true);
  }
  free(s);
  esp_core_dump_image_erase();
}

// Fase 18 — chamada BEM cedo no setup(), antes de qualquer coisa depender
// de rede/Firebase (100% local, sempre funciona). Se este boot foi causado
// pelo botão físico (acordando de um deep sleep), ativa a janela de graça:
// nenhum comandoDesligar vindo do Firestore é processado por
// DESLIGAR_SUPRESSAO_MS, não importa se a limpeza do campo no Firestore
// (best-effort, tentada mais tarde já com rede/Firebase prontos) funcionar
// ou não. É essa flag local que garante de vez que não existe loop.
void ativarSupressaoDesligarSeAcordouPorBotao(){
  esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();
  if(causa != ESP_SLEEP_WAKEUP_EXT0 && causa != ESP_SLEEP_WAKEUP_EXT1) return;
  desligarSuprimidoNesteBoot = true;
  desligarSupressaoAteMs = millis() + DESLIGAR_SUPRESSAO_MS;
  Serial.println("[DeepSleep] Acordou via botao fisico -- comandoDesligar sera ignorado por 30s (janela de graca, garante que nao reentra em sleep mesmo se a limpeza no Firestore falhar).");
}

// Fase 28c — mesmo raciocinio do ativarSupressaoDesligarSeAcordouPorBotao,
// mas pro reinicio remoto, usando o marcador em LittleFS em vez de
// RTC_DATA_ATTR (ver comentario grande la em cima -- RTC_DATA_ATTR
// provou NAO sobreviver ao tipo de reset que ESP.restart() gera nesta
// placa, testado em hardware real). Chamada logo apos LittleFS.begin()
// no setup() (precisa do filesystem montado), ainda bem cedo, 100% local
// (nao depende de WiFi/Firebase). A supressao NAO expira por tempo --
// fica ativa ate limparComandoReiniciarSincrono() confirmar de verdade a
// limpeza no Firestore (retry periodico em loop()); reiniciarSupressaoAteMs
// abaixo e' so' a valvula de seguranca de 3min.
void ativarSupressaoReiniciarSeNecessario(){
  if(!LittleFS.exists(REINICIANDO_FLAG_PATH)) return;
  LittleFS.remove(REINICIANDO_FLAG_PATH);  // consumido -- so' precisa valer por 1 boot
  reiniciarSuprimidoNesteBoot = true;
  reiniciarSupressaoAteMs = millis() + REINICIAR_SUPRESSAO_VALVULA_MS;
  Serial.println("[Reiniciar] Boot seguinte a um reinicio remoto (marcador LittleFS encontrado) -- comandoReiniciar sera ignorado ate a limpeza no Firestore ser confirmada (evita loop de reboot).");
}

// REMOVIDO (2026-09-20): a tentativa de self-check via Display ID
// (comando 0x04) sempre "falhava" nesta placa/módulo específico -- mesmo
// em boots claramente normais, com o display funcionando perfeitamente
// depois. Esse ILI9341 (breakout barato, 4 fios) provavelmente não
// devolve leitura válida nesse comando (MISO pode nem estar realmente
// utilizável pra leitura, mesmo estando fiado) -- então o check não
// distinguia sucesso de falha, só adicionava ~300ms de delay em todo
// boot e ruído no log. Ver "Lições aprendidas" no CLAUDE.md.

// ═══════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═══════════════════════════════════════════════════════════════
void setup(){
  // SEGURANÇA PRIMEIRO: reles em OFF antes de qualquer outra coisa
  initRelays();
  blInit();   // Fase 101 -- backlight em 100 % ja no inicio (GPIO47 -> pino LED do display)

  // Fase 21 — CS do SD em HIGH (desselecionado) o quanto antes, ANTES do
  // tft.init(). Mesma cautela do exemplo oficial da TFT_eSPI
  // (ESP32_SDcard_jpeg.ino): varios dispositivos vao compartilhar o
  // mesmo barramento SPI (display + SD), e um CS flutuando durante a
  // inicialização de qualquer um deles pode causar contenção no
  // barramento. Sem custo nenhum mesmo sem o SD conectado ainda.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  Serial.begin(115200);
  logResetReason();
  { esp_reset_reason_t rr=esp_reset_reason(); if(rr==ESP_RST_POWERON||rr==ESP_RST_BROWNOUT) pushEnergiaPend=true; }   // Fase 53
  // Bem cedo, 100% local (sem WiFi/Firebase ainda) -- garante a supressao
  // do loop de deep sleep independente de rede, ver Fase 18.
  ativarSupressaoDesligarSeAcordouPorBotao();
  // 300ms tambem funciona como uma folga pro trilho de 5V (display + bobinas
  // dos reles) estabilizar antes do display ser inicializado. Fase 19: se
  // os ultimos boots vieram de reset anormal (Brownout/Watchdog/Panic) em
  // sequencia -- ver bootLoopCounter -- essa folga cresce (+300ms por
  // tentativa consecutiva, ate +1500ms), dando mais tempo pro trilho de
  // energia estabilizar antes de ligar TFT/WiFi (os maiores consumidores
  // de corrente do boot). Nao resolve uma fonte/capacitor fraco de fato,
  // mas reduz a chance da MESMA sessao de energia entrar num loop repetido.
  unsigned long estabilizacaoExtraMs = min(bootLoopCounter, (uint32_t)5) * 300UL;
  if(estabilizacaoExtraMs>0){
    Serial.printf("[Boot] Aplicando %lums extras de estabilizacao antes de TFT/WiFi (backoff de reset anormal).\n", estabilizacaoExtraMs);
  }
  delay(300+estabilizacaoExtraMs);uptimeStart=millis();
  Wire.begin(8,9);tft.init();tft.setRotation(1);tft.setTouch(calData);
  initColors();LittleFS.begin(false,"/littlefs",10,"littlefs");
  // Fase 28c -- precisa do LittleFS ja montado (le/apaga um arquivo
  // marcador). Ainda bem cedo, 100% local (sem WiFi/Firebase).
  ativarSupressaoReiniciarSeNecessario();
  loadDisplayCfg();
  blAplica();   // Fase 101 -- brilho salvo
  wallCfgCarregar();   // Fase 45 -- wallpapers (bloqueio/principal) + escurecimento
  segCfgCarregar();    // Fase 46 -- PIN (hash) + e-mail de recuperacao
  alertCfgCarregar();  // Fase 49 -- preferencias de notificacao
  camAutoCfgCarregar(); // Fase 65 -- captura automatica da camera
  autoCarregarCfg();   // Fase 52 -- regras de automacao
  nomeCfgCarregar();   // Fase 55 -- nome da chocadeira
  cicloStatsCarregar(); // Fase 48 -- estatisticas do ciclo em andamento
  tiposCarregarCfg();   // Fase 42 -- tipos de ovos (padroes + /tipos.cfg)
  aplicarLimitesDoTipo();   // Fase 43 -- limites vem do tipo do ciclo (a tela "Ajustes Finos" foi removida)
  if(rtc.begin()){
    rtcOK=true;
    rtcHoraConfiavel=!rtc.lostPower();   // Fase 92
    if(rtc.lostPower()){
      // Sinal de integridade da bateria: DS3231 so reporta lostPower()
      // quando ficou sem alimentacao nenhuma (nem VCC nem bateria) desde
      // o ultimo ajuste -- ou seja, a bateria de backup (LIR2032) nao
      // segurou o relogio. Se isso aparecer em TODO boot, a bateria
      // provavelmente esta fraca/morta e vale trocar. Ajusta com a hora
      // de compilacao so como "melhor palpite" imediato -- syncRtcFromNtp()
      // corrige pra hora real assim que o WiFi conectar.
      Serial.println("[RTC] lostPower() = true -- bateria de backup pode estar fraca/morta (ou RTC ficou sem nenhuma alimentacao). Usando hora de compilacao ate o NTP corrigir.");
      rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
    }
    DateTime n=rtc.now();snprintf(clockStr,9,"%02d:%02d:%02d",n.hour(),n.minute(),n.second());
  }
  relogioDoRtcAplicar();   // Fase 92 -- sem internet o relogio do sistema nao fica em 1970: usa o RTC (o NTP corrige depois)
  cicloRestaurar();        // Fase 92 -- ciclo em andamento volta do /ciclo.cfg (sem internet a nuvem nao responde)

  // Fase 20 — SHT31 no mesmo barramento I2C da RTC (ja iniciado acima via
  // Wire.begin(8,9)). Sem sht31.begin(TwoWire*) explicito porque a lib
  // usa Wire por padrao, que ja esta correto aqui.
  sht31OK = sht31.begin(0x44);
  Serial.println(sht31OK
    ? "[SHT31] Sensor de temperatura/umidade OK (I2C 0x44)."
    : "[SHT31] NAO encontrado no barramento I2C (addr 0x44) -- verifique fiacao "
      "(vermelho=VCC 3.3V, preto=GND, tente amarelo/verde trocados entre SCL/SDA) "
      "ou se o sensor ainda nao foi conectado nesta placa.");
  adxlInit();   // Fase 91 -- varre o I2C e procura o ADXL345
  co2Init();    // Fase 99 -- MH-Z19B (UART1; sem sensor = silencioso)
  pwmInit();    // Fase 99 -- PWM dos IRF520 (desligado por padrao)
  tachInit();   // Fase 105 -- tacometro do fan 1 (GPIO17)
  fanRegrasCarregar(); fansEfManual();   // Fase 112 -- funcoes dos fans

  // Fase 21 (3a tentativa) — SD reaproveitando o MESMO periferico SPI do
  // TFT_eSPI, via tft.getSPIinstance() (metodo oficial da propria lib,
  // usado no exemplo ESP32_SDcard_jpeg.ino dela). Ver comentario extenso
  // perto de "SD — LOGS" (acima no arquivo) pra entender por que as duas
  // tentativas anteriores nao davam certo. CS proprio (SD_CS_PIN), resto
  // do barramento (MOSI/MISO/SCK) compartilhado com o display de verdade
  // -- sem inicializar nenhum periferico novo, entao sem risco de repetir
  // a tela branca.
  sdOK = SD.begin(SD_CS_PIN, tft.getSPIinstance());
  Serial.println(sdOK
    ? "[SD] Cartao OK, logs serao gravados."
    : "[SD] NAO encontrado/montado -- logs vao so pro Serial (e eventos "
      "importantes pro Firestore, se a rede estiver ok). Verifique se o "
      "modulo esta conectado e se SD_CS_PIN (GPIO18) esta correto pra "
      "sua fiacao, e se MOSI/MISO/SCK do leitor SD estao emendados nos "
      "mesmos pontos do conector do display (SDI/SDO/SCK).");
  // Fase 30 -- importante=true sempre (era so' resetFoiAnormal): "Reinícios
  // do sistema" foi pedido explicitamente no historico do PWA, nao so os
  // anormais. Boots sao raros o bastante (nao e' um evento de alta
  // frequencia) pra nao pesar em nada no Firestore.
  logEvento("Boot", lastResetReasonShort, true);
  coredumpRelatar();   // Fase 113 -- se o ultimo reset foi um crash, registra onde (pc + backtrace)

  bootAnimation();
  loadWifiCreds();

  // Bug de UI corrigido (Fase 16): a tela preta "Conectando: SSID" sumiu —
  // a home já é desenhada aqui, antes de saber se o WiFi vai conectar, e o
  // ícone de WiFi no topo anima (barras acendendo em sequência) enquanto
  // WiFi.begin() ainda não resolveu, em vez de travar numa tela vazia.
  appState=S_MAIN;drawMainScreen();

  if(strlen(savedSSID)>0){
    WiFi.mode(WIFI_STA);WiFi.begin(savedSSID,savedPass);
    unsigned long t0=millis();int frame=0;unsigned long lastFrameMs=0,lastClockMsBoot=0;
    while(WiFi.status()!=WL_CONNECTED&&millis()-t0<10000){
      if(millis()-lastFrameMs>150){lastFrameMs=millis();drawWifiConnectingIcon(WIFI_ICX,WIFI_ICY,frame++);}
      if(rtcOK&&millis()-lastClockMsBoot>1000){lastClockMsBoot=millis();updateClockHeader();}  // relogio nao trava enquanto conecta
      delay(20);
    }
    // WiFi.setSleep(false): dispositivo é alimentado pela rede elétrica
    // (sem bateria pra economizar), então o modem sleep so traz risco
    // sem beneficio real aqui -- e e um suspeito bem documentado em
    // travamentos de heap/buffer de RX no ESP32 sob polling HTTPS
    // frequente (bate com o crash confirmado em 2026-09-20, dentro do
    // proprio driver de WiFi ao reciclar um buffer de recepcao).
    wifiOK=(WiFi.status()==WL_CONNECTED);if(wifiOK) WiFi.setSleep(false);
    drawWifiIcon(WIFI_ICX,WIFI_ICY,wifiOK,true);  // estado final (verde ou vermelho com X)
  }
  if(wifiOK) syncTimeAndStartFirebase();

  limparComandoDesligarSeAcordouPorBotao();  // limpeza best-effort no Firestore (garantia de verdade e a flag local, ja ativada acima)
  // Fase 28b: a limpeza de comandoReiniciar NAO roda mais aqui (bloqueando
  // o setup() por ate ~26s, o que sozinho ja quase estourava a antiga
  // janela de supressao de 20s) -- agora roda em retry periodico dentro
  // de loop(), fora do caminho critico do boot. Ver reiniciarClearRetry.

  prevWifiState=wifiOK;
  lastActivityMs=millis();
  if(pinAtivo) entrarBloqueio(S_MAIN);   // Fase 46 -- bloqueia no boot (depois da conexao WiFi, pra nao desenhar por cima da tela de PIN)
  Serial.println("ChocaIoT Fase 8b (Nuvem -> Atuadores) — pronto!");
  Serial.println("Reles: todos OFF confirmado.");
}

void loop(){
  { static unsigned long ultLoopMs=0; unsigned long n=millis(); if(ultLoopMs && n-ultLoopMs>300) Serial.printf("[LOOP] travado %lu ms (touch sem resposta nesse tempo)\n",n-ultLoopMs); ultLoopMs=n; }
  serialDebug();   // Fase 51 -- comandos de depuracao pela serial (p/s/x)
  // sem WiFi o cliente async tenta conectar de forma bloqueante (segundos) e a tela para de responder: so roda com rede
  if(WiFi.status()==WL_CONNECTED) fbApp.loop();  // mantém autenticação do Firebase rodando

  cicloSyncTick();
  static unsigned long lastWifiCheckMs=0;
  if(millis()-lastWifiCheckMs>10000){ lastWifiCheckMs=millis(); wifiWatchdog(); }

  static unsigned long lastNtpCheckMs=0;
  if(millis()-lastNtpCheckMs>30000){ lastNtpCheckMs=millis(); ntpWatchdog(); }
  cicloPersistTick();   // Fase 92

  static unsigned long lastDiagMs=0;
  if(millis()-lastDiagMs>10000){ lastDiagMs=millis(); diagnosticoStatusPeriodico(); }

  // SHT31 — leitura local (I2C, sem rede), independente de WiFi/Firebase.
  static unsigned long lastSht31Ms=0;
  if(millis()-lastSht31Ms>SHT31_READ_INTERVAL_MS){ lastSht31Ms=millis(); lerSht31(); }
  adxlTick();   // Fase 91/93 -- ADXL345 a 10 Hz: estado da tampa
  adxlTelaTick();   // Fase 93 -- tela Configuracoes > Tampa ao vivo
  adxlComandosTick();   // Fase 98 -- ajustes/calibracao vindos da tela Tampa do PWA
  telaComandosTick();   // Fase 104 -- ajustes de Tela/Fans vindos do PWA
  tachTick(); fansPublicaTick(); fansTelaTick();   // Fase 105 -- rpm do fan 1
  { static unsigned long ultF=0; if(millis()-ultF>=1000UL){ ultF=millis(); fansCh8Tick(); } }   // Fase 111
  fansRegrasTick();   // Fase 112
  adxlPublicaTick();    // Fase 98 -- telemetria ao vivo para o PWA (so com a tela aberta)
  co2Tick();            // Fase 99 -- leitura do MH-Z19B a cada 5 s

  // Fase 30 — registro periodico da leitura no historico de Logs (5min).
  static unsigned long lastSensorLogMs=0;
  if(millis()-lastSensorLogMs>SENSOR_LOG_INTERVAL_MS){ lastSensorLogMs=millis(); logLeituraSensorPeriodica(); }

  // Fase 32 — temperatura interna do ESP32 (Min/Med/Max na tela Sistema).
  // Roda sempre em background, nao so com a tela aberta, pra Min/Max
  // refletirem a sessao inteira.
  static unsigned long lastEspTempMs=0;
  if(millis()-lastEspTempMs>5000){ lastEspTempMs=millis(); atualizarEspTempStats(); controlarCoolerPorHisterese(); }

  // Push da leitura pro Firestore -- 60s (bem mais espaçado que o
  // heartbeat/poll: temperatura/umidade de uma incubadora nao muda rapido,
  // e cada leitura vira um documento novo na subcolecao "leituras" --
  // nao ha motivo pra gastar quota de escrita/subir custo mais que isso).
  static unsigned long fbLastLeituraMs=0;
  if(fbStarted && fbApp.ready() && millis()-fbLastLeituraMs>FB_LEITURA_PUSH_INTERVAL_MS){
    fbLastLeituraMs=millis();
    pushLeituraToFirestore();
  }

  // Heartbeat primeiro (prioridade): fbClient é um único cliente async
  // compartilhado por todas as chamadas Firestore -- se várias tarefas
  // vencerem no mesmo tick, quem for enfileirada primeiro sai primeiro. O
  // heartbeat é o que o PWA usa pra decidir "online"/"offline", então não
  // pode ficar sistematicamente atrás do poll de atuadores/ciclo.
  if(fbStarted && fbApp.ready() && millis()-fbLastHeartbeatMs>(pwaAtivo()?20000UL:(unsigned long)FB_HEARTBEAT_INTERVAL_MS)){   // Fase 114e: com o PWA aberto o heartbeat (temp/umid/CO2) sai a cada 20 s em vez de 60 s
    fbLastHeartbeatMs=millis();
    pushHeartbeatToFirestore();
  }

  if(fbStarted && fbApp.ready() && millis()-fbLastPollMs>fbPollIntervaloMs()){
    fbLastPollMs=millis();
    pollActuatorsFromFirestore();
  }

  // Fase 28b — retry periodico da limpeza de comandoReiniciar, chamado
  // AQUI (top-level de loop(), fora de qualquer callback async) porque a
  // tentativa original (dentro do proprio callback do poll) nao era
  // confiavel. So roda enquanto reiniciarSuprimidoNesteBoot estiver ativo
  // (ou seja, so no boot seguinte a um reinicio remoto); assim que
  // confirmar a limpeza, desativa a si mesmo.
  // Fase 75 -- limpeza do comandoDesligar antigo por retry no top-level do loop() (fora de callback async, como o Reiniciar)
  static unsigned long lastDesligarClearRetryMs=0;
  if(desligarLimpezaPend && fbStarted && fbApp.ready() && millis()-lastDesligarClearRetryMs>5000){
    lastDesligarClearRetryMs=millis();
    if(limparComandoDesligarSincrono()){
      desligarLimpoEmMs=millis(); desligarLimpezaPend=false; desligarSuprimidoNesteBoot=false;
      Serial.println("[Desligar] comandoDesligar antigo confirmado limpo no Firestore.");
      logEvento("Desligar","Comando antigo de desligar limpo na nuvem",true);
    }else Serial.println("[Desligar] ainda nao confirmou a limpeza de comandoDesligar -- tentando de novo em 5s.");
  }
  static unsigned long lastReiniciarClearRetryMs=0;
  if(reiniciarSuprimidoNesteBoot && fbStarted && fbApp.ready() && millis()-lastReiniciarClearRetryMs>5000){
    lastReiniciarClearRetryMs=millis();
    if(limparComandoReiniciarSincrono()){
      reiniciarSuprimidoNesteBoot=false;
      Serial.println("[Reiniciar] comandoReiniciar confirmado limpo no Firestore -- supressao desativada.");
    }else{
      Serial.println("[Reiniciar] Ainda nao confirmou a limpeza de comandoReiniciar -- tentando de novo em 5s.");
    }
  }

  // Fase 42 -- tela do ciclo: refresca a cada 30s (contagem "Parar libera em") ou quando o poll mudou algo
  static unsigned long lastCicloTelaMs=0;
  if(appState==S_CICLO && !cicloConfirm && fbCicloAtivo && (cicloTelaSuja || millis()-lastCicloTelaMs>30000)){
    cicloTelaSuja=false; lastCicloTelaMs=millis(); drawCicloScreen();
  }

  static unsigned long lastStDraw=0;   // Fase 77: janela de status (e ciclo em preparo) atualiza a cada 1 s so se algo mudou
  if((appState==S_STATUS || (appState==S_CICLO&&fbCicloPreparo&&!cicloConfirm)) && millis()-lastStDraw>1000){ lastStDraw=millis(); drawStatusDetalhe(false); }
  static unsigned long lastPulsoMs=0;
  if(appState==S_CICLO && millis()-lastPulsoMs>650){ lastPulsoMs=millis(); cicloPulso(); }   // Fase 54: pulso do proximo marco
  // Fase 41 -- indice do SD + pedidos de arquivo do PWA (fora de qualquer callback)
  static unsigned long lastArqMs=0;
  if(millis()-lastArqMs>3000){ lastArqMs=millis(); arqServico(); segServico(); histServico(); pushServico(); cameraServico(); uploadServico(); sdManter(); }
  static unsigned long lastRelMs=0;
  if(millis()-lastRelMs>5000){ lastRelMs=millis(); cicloRelatorioServico(); }   // Fase 48
  static unsigned long lastAutoMs=0;
  if(millis()-lastAutoMs>1000){ lastAutoMs=millis(); automacaoTick(); ambienteTick(); }   // Fase 52 / 69 / 72 (1 s: controle por pulsos)
  pulsoFimRapido();   // Fase 72 -- fim de pulso com precisao de ms
  aguaBombaFimRapido();   // Fase 85 -- fim do pulso da bomba de agua (ms) + vigia de 8 s no CH3
  segTick();   // Fase 46 -- recuperacao de PIN / bloqueio temporario

  if(fbStarted && fbApp.ready() && millis()-fbLastCicloPollMs>fbCicloIntervaloMs()){
    fbLastCicloPollMs=millis();
    pollCicloFromFirestore();
  }

  // Energia da tela — qualquer toque em QUALQUER tela conta como atividade
  // e volta da screensaver na hora; a checagem de inatividade vale pra todo
  // o sistema (nao so tela principal). Camera em tela cheia fica sempre a
  // 100%, nunca aciona o screensaver sozinha (ponto 1 do pedido).
  if(appState==S_CAMERA) lastActivityMs=millis();
  if(appState==S_PIN && (pinModo==PIN_RECUP||pinRecuperando)) lastActivityMs=millis();   // Fase 53: recuperando o PIN nao deixa o screensaver entrar

  uint16_t tx,ty;bool t=tft.getTouch(&tx,&ty);
  if(t){
    lastActivityMs=millis();
    if(appState==S_SCREENSAVER){ exitScreensaver(); }   // primeiro toque so acorda, nao aciona o que tiver embaixo do dedo
    else{ txL=tx;tyL=ty;if(!touching){touching=true;txS=tx;tyS=ty;tDown=millis();} }
  }else if(touching){touching=false;handleRelease(txS,tyS,txL,tyL);}

  if(appState!=S_SCREENSAVER && !telaSempreLigada && millis()-lastActivityMs>screenOffTimeoutSec*1000UL){
    enterScreensaver();
  }
  if(appState==S_SCREENSAVER){
    screensaverTick();
  }else{   // Fase 103 -- passo 1: sem toque por dimSegundos -> brilho reduzido (a tela continua a mesma); qualquer atividade restaura
    uint8_t alvo=(!telaSempreLigada && dimSegundos>0 && millis()-lastActivityMs>(unsigned long)dimSegundos*1000UL)?1:0;
    if(blEstagio!=alvo){ blEstagio=alvo; blAplica(); }
  }

  if(appState==S_MAIN){
    if(millis()-lastClkMs>1000){lastClkMs=millis();updateClockHeader();}
    bool wc=(wifiOK!=prevWifiState);if(wc){prevWifiState=wifiOK;wifiIconPhase=true;drawWifiIcon(WIFI_ICX,WIFI_ICY,wifiOK,true);}
    if(!wifiOK&&millis()-lastWifiBlink>600){lastWifiBlink=millis();wifiIconPhase=!wifiIconPhase;drawWifiIcon(WIFI_ICX,WIFI_ICY,false,wifiIconPhase);}

    // Card "Ciclo" avança sozinho (dia muda com o tempo, mesmo sem novo
    // dado do Firestore) -- mesma ideia do setInterval(60s) no app.js.
    static unsigned long lastCicloCardMs=0;
    if(millis()-lastCicloCardMs>=5000){ lastCicloCardMs=millis(); drawCicloCard(); }   // Fase 116: a cada 5 s (alterna com a data)

    // Cards Temperatura/Umidade -- mesma cadencia da leitura do SHT31
    // (sem sentido redesenhar mais rapido que o valor muda).
    static unsigned long lastDataCardMs=0;
    if(millis()-lastDataCardMs>60000){ lastDataCardMs=millis(); drawDataCard(); }

    static unsigned long lastTempCardMs=0;
    if(millis()-lastTempCardMs>SHT31_READ_INTERVAL_MS){ lastTempCardMs=millis(); drawTempUmidCards(); }
  }else if(appState==S_SENSOR_DETAIL){
    refreshSensorDetailLive();  // Fase 24 — dot piscando + grafico ao vivo
  }else if(appState==S_CAMERA){
    camLiveTick();  // Fase 66 -- "ao vivo" local (so roda com a tela aberta + toggle ligado)
  }else if(appState==S_SETTINGS && SETTINGS_TOTAL_PAGES>1){
    // Fase 25 — pulso das setinhas de paginação (só a área do rodapé,
    // nunca a tela inteira).
    static unsigned long lastArrowMs=0;
    if(millis()-lastArrowMs>500){ lastArrowMs=millis(); settingsArrowOn=!settingsArrowOn; drawSettingsPageIndicator(); }
  }else if(appState==S_LOGS && !logsConfirmandoLimpar && !logsBuscando && logsTotalPaginas()>1){
    // Fase 29 — mesmo pulso de setinhas da Fase 25, reaproveitado aqui.
    static unsigned long lastLogsArrowMs=0;
    if(millis()-lastLogsArrowMs>500){ lastLogsArrowMs=millis(); logsArrowOn=!logsArrowOn; drawLogsPageFooter(); }
  }else if(appState==S_MEMORIA){
    // Fase 34 — mesmo pulso de setinhas, pra paginação nova de Memória.
    static unsigned long lastMemArrowMs=0;
    if(millis()-lastMemArrowMs>500){ lastMemArrowMs=millis(); memArrowOn=!memArrowOn; drawMemPageIndicator(); }
  }

  if(appState==S_MEMORIA && millis()-lastMemRefreshMs>MEM_REFRESH_MS){
    lastMemRefreshMs=millis();
    updateMemoriaValues();
  }

  // Diagnóstico do heap corrompido confirmado em 2026-09-20 (crash dentro
  // do driver de WiFi, reciclando um buffer de recepção -- a corrupção em
  // si aconteceu ANTES, em outro lugar; o free() só a descobriu por
  // acidente). Checar a integridade do heap com frequência encurta a
  // janela entre "quando corrompeu" e "quando percebemos" -- da próxima
  // vez, o que apareceu no Serial nos ~10s antes deste aviso é a pista de
  // qual rotina causou.
  static unsigned long lastHeapCheckMs=0;
  if(millis()-lastHeapCheckMs>10000){
    lastHeapCheckMs=millis();
    if(!heap_caps_check_integrity_all(true)){
      Serial.println("!! HEAP CORROMPIDO detectado por heap_caps_check_integrity_all() -- ver detalhes do bloco acima.");
    }
  }
}
