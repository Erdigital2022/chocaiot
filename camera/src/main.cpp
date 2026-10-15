// main.cpp — ChocaIoT-Camera — Cam-2: teste do modulo DCX-OV5640-100MM + SD da placa
// Placa: ESP32-S3-WROOM-1 N16R8 CAM (slot microSD na propria placa)
//
// O que este teste faz (tudo com GUARDA TERMICA -- a 1a unidade aqueceu forte):
//   1. Mostra PSRAM e temperatura interna do chip (idle, antes de ligar a camera)
//   2. Monta o SD (SD_MMC, modo 1-bit: CLK=39, CMD=38, D0=40 -- pinos "~ SD Card" da serigrafia)
//   3. Inicia a OV5640 (JPEG) e descarta 2 quadros de aquecimento do AGC/AEC
//   4. Tira fotos em VGA, XGA e UXGA, salva em /cam/*.jpg e imprime tamanho + temperatura
//   5. Se a temperatura do chip passar de TEMP_LIMITE_C, desliga a camera e PARA (nao insiste)
//
// Serial: 115200 na porta USB-UART (CH343). Digite 'f' para repetir o teste, 's' so p/ status.
// NAO usar -DARDUINO_USB_CDC_ON_BOOT=1 (desvia o Serial pra porta OTG).

#include <Arduino.h>
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "camera_pins.h"

// Pinos do SD desta placa (confirmados pela serigrafia "~ : SD Card" nos GPIO 38/39/40)
#define SD_CLK_PIN 39
#define SD_CMD_PIN 38
#define SD_D0_PIN  40

#define TEMP_LIMITE_C 75.0f     // temperatura interna do chip: acima disso o teste e abortado
bool camOK = false, sdOK = false, abortado = false;
int contadorFoto = 0;

float tempChip() { return temperatureRead(); }

void relatorioStatus(const char* momento) {
  Serial.printf("[%s] temp chip=%.1f C | heap=%u KB | psram livre=%u KB\n",
                momento, tempChip(), ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
}

bool guardaTermica() {
  if (tempChip() > TEMP_LIMITE_C) {
    Serial.printf("!!! TEMPERATURA %.1f C > %.0f C -- DESLIGANDO A CAMERA E ABORTANDO O TESTE !!!\n", tempChip(), TEMP_LIMITE_C);
    Serial.println("Desconecte o USB e deixe esfriar. Verifique o cabo FPC (orientacao/trava) antes de tentar de novo.");
    if (camOK) { esp_camera_deinit(); camOK = false; }
    abortado = true;
    return true;
  }
  return false;
}

bool initSD() {
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  if (!SD_MMC.begin("/sdcard", true /* modo 1-bit */)) {
    Serial.println("SD: FALHA ao montar. Cartao em FAT32? Inserido ate o clique? (o ESP32 nao le exFAT)");
    return false;
  }
  uint8_t tipo = SD_MMC.cardType();
  if (tipo == CARD_NONE) { Serial.println("SD: nenhum cartao detectado."); return false; }
  Serial.printf("SD OK: tipo=%s, %llu MB total, %llu MB usados\n",
                tipo == CARD_MMC ? "MMC" : tipo == CARD_SD ? "SDSC" : tipo == CARD_SDHC ? "SDHC" : "?",
                SD_MMC.cardSize() / (1024ULL * 1024ULL), SD_MMC.usedBytes() / (1024ULL * 1024ULL));
  if (!SD_MMC.exists("/cam")) SD_MMC.mkdir("/cam");
  return true;
}

bool initCamera(framesize_t fs, int xclkMHz, int fbCount, camera_grab_mode_t grab) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = xclkMHz * 1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = fs;               // Cam-2b: o tamanho e definido NA INICIALIZACAO (como no Cam-1, que funcionava)
  config.jpeg_quality = 12;
  config.fb_count     = fbCount;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = grab;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("CAMERA: FALHA ao iniciar (0x%x). 0x105=sensor nao respondeu (SCCB): cabo FPC/trava/orientacao.\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("CAMERA OK: sensor PID=0x%04x (OV5640 = 0x5640), xclk=%d MHz\n", s->id.PID, xclkMHz);
    s->set_vflip(s, 1);   // muitos clones montam invertido -- confira a 1a foto
  }
  camOK = true;
  return true;
}
void desligaCamera() { if (camOK) { esp_camera_deinit(); camOK = false; } }

// Tenta capturar (ate 3 quadros; os 2 primeiros aquecem AEC/AGC). true = salvou/mediu.
bool capturaEsalva(const char* nome) {
  camera_fb_t* fb = nullptr;
  for (int i = 0; i < 4; i++) {
    unsigned long t0 = millis();
    fb = esp_camera_fb_get();
    if (fb) { if (i < 2) { esp_camera_fb_return(fb); fb = nullptr; delay(100); continue; } break; }
    Serial.printf("    quadro %d: fb_get falhou apos %lu ms\n", i, millis() - t0);
  }
  if (!fb) return false;
  Serial.printf("  %s: %dx%d, %u bytes JPEG\n", nome, fb->width, fb->height, (unsigned)fb->len);
  if (sdOK) {
    char path[40]; snprintf(path, sizeof(path), "/cam/foto_%03d_%s.jpg", ++contadorFoto, nome);
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.write(fb->buf, fb->len); f.close(); Serial.printf("  salvo em %s\n", path); }
    else Serial.printf("  ERRO ao gravar %s\n", path);
  }
  esp_camera_fb_return(fb);
  return true;
}

// Para cada tamanho: reinicia a camera nesse tamanho; se nao capturar com XCLK de 20 MHz,
// tenta com 10 MHz (falha so no clock alto = sinal/cabo/alimentacao no limite).
void rodarTeste() {
  if (abortado) { Serial.println("Teste abortado por temperatura -- reinicie a placa depois de esfriar."); return; }
  Serial.println("\n=== TESTE DE CAMERA ===");
  relatorioStatus("inicio");
  const struct { framesize_t t; const char* n; } passos[] = { {FRAMESIZE_VGA,"vga"}, {FRAMESIZE_XGA,"xga"}, {FRAMESIZE_UXGA,"uxga"} };
  for (auto& p : passos) {
    bool ok = false;
    for (int xclk : {20, 10}) {
      if (guardaTermica()) return;
      desligaCamera();
      Serial.printf("-- %s @ XCLK %d MHz --\n", p.n, xclk);
      if (!initCamera(p.t, xclk, 2, CAMERA_GRAB_LATEST)) continue;
      delay(500);
      if (capturaEsalva(p.n)) { ok = true; break; }
      Serial.printf("  %s @ %d MHz: SEM QUADRO\n", p.n, xclk);
    }
    relatorioStatus(p.n);
    if (!ok) Serial.printf("  >>> %s falhou nos dois clocks\n", p.n);
    delay(1000);
  }
  desligaCamera();
  Serial.println("=== FIM. Digite 'f' para repetir. Toque no chip e no modulo da camera: qual esta quente? ===");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  esp_log_level_set("*", ESP_LOG_INFO);          // mostra os erros internos da camera (cam_hal: FB-OVF, timeout etc.)
  esp_log_level_set("cam_hal", ESP_LOG_VERBOSE);
  Serial.println("\n=========================================");
  Serial.println("  ChocaIoT-Camera - Cam-2: OV5640 + SD   ");
  Serial.println("=========================================");
  Serial.printf("PSRAM: %s (%u KB livres)\n", psramFound() ? "OK" : "NAO ENCONTRADA", ESP.getFreePsram() / 1024);
  relatorioStatus("boot (camera desligada)");
  sdOK = initSD();
  Serial.println("Teste inicia em 3 s...");
  delay(3000);
  rodarTeste();
}

void loop() {
  static unsigned long ultimoStatus = 0;
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'f') rodarTeste();
    else if (c == 's') relatorioStatus("status");
  }
  if (millis() - ultimoStatus > 10000) {   // monitora o calor mesmo parado, com a camera ligada
    ultimoStatus = millis();
    relatorioStatus("idle");
    guardaTermica();
  }
  delay(20);
}
