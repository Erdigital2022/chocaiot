// User_Setup.h — ChocaIoT
// Configurado para: YD-ESP32-S3 44P + Display ILI9341 3.2" SPI + Touch XPT2046
// Última atualização: 19/07/2026

#define USER_SETUP_INFO "ChocaIoT_ESP32S3_ILI9341"

// ── Driver ────────────────────────────────────────────────────
#define ILI9341_DRIVER

// ── IMPORTANTE PARA ESP32-S3 ──────────────────────────────────
// No ESP32-S3: VSPI=0 (barramento do flash, já ocupado)
//              HSPI=1 (SPI2, barramento de uso geral — o correto)
// Sem esta linha, o TFT_eSPI tenta usar o barramento do flash e
// trava com StoreProhibited/Guru Meditation.
#define USE_HSPI_PORT

// ── Pinos SPI — ESP32-S3 ──────────────────────────────────────
#define TFT_MISO  13
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC    14
#define TFT_RST   21

// ── Touch XPT2046 (mesmo barramento SPI) ──────────────────────
#define TOUCH_CS  48

// ── Fontes ────────────────────────────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ── Velocidade SPI ────────────────────────────────────────────
// Reduzido de 40MHz -> 27MHz: 40MHz é agressivo pra fiação em jumper/
// dupont (sem blindagem) e principalmente pra proximidade com o relé de
// 8 canais chaveando cargas de 220V AC no mesmo gabinete -- EMI do
// chaveamento pode corromper bytes no SPI nessa frequência, e o sintoma
// bate exatamente com o reportado (flicker + artefatos "garbled",
// pioides/aleatórios, plausivelmente mais quando os relés de carga alta
// estão comutando). 27MHz é o valor mais citado como seguro pra ILI9341
// em protótipo com fios soltos, ainda ~4x mais rápido que o necessário
// pra um display 240x320.
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY  2500000