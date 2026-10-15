// main_servidor.cpp — ChocaIoT-Camera — FIRMWARE DE PRODUÇÃO (Fase 50, streaming na Fase 66)
// Servidor HTTP na rede local. NÃO fala com a nuvem: o controlador (firmware) a encontra
// por mDNS ("chocaiot-cam.local"), consulta /status a cada 20 s e busca /foto quando o PWA (ou o
// próprio aparelho) pede uma foto — assim a câmera não gasta nenhuma leitura da cota do Firestore.
//
//   GET /status -> 200 {"ok":true,"temp":41.2,"sd":true,"streaming":false}  (rápido, não liga a câmera)
//   GET /foto   -> 200 image/jpeg (VGA)  | 503 se o chip estiver quente/streaming | 500 se falhar
//   Porta 81:
//   GET /stream -> 200 multipart/x-mixed-replace (MJPEG ao vivo) — ver "Fase 66" abaixo
//
// A câmera só é LIGADA durante a foto OU o stream (init -> ... -> deinit): parada, o sensor fica sem
// clock e o módulo praticamente não esquenta (o 1º módulo testado aqueceu forte — ver CLAUDE.md §2.2).
// Cada foto avulsa também é gravada em /cam/foto_NNN.jpg no cartão SD da própria placa (se houver) —
// isso é só um backup local; o PWA navega pelo cartão SD do CONTROLADOR (firmware), não este.
//
// ⚠️ ANTES de gravar este firmware numa placa nova: rode o teste de curto (src/diag_curto_cam7.cpp.bk)
// sem o cabo da câmera — a placa V1695 usada nos primeiros testes tem GPIO 9–13 em curto entre si.
//
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Fase 66 — streaming MJPEG ao vivo (pedido do usuário; câmera física ainda não confirmada, ver
// CLAUDE.md do controlador §2.2 — este arquivo inteiro segue sem validação em hardware real).
//
// Arquitetura: o PWA consome http://<ip-da-camera>:81/stream DIRETO do navegador (o controlador só
// informa esse IP pro app via Firestore, Fase 66 no main.cpp do controlador — o vídeo em si nunca
// passa pela nuvem). Isso só funciona com o celular na MESMA rede local da câmera; fora de casa o
// stream fica inacessível (limitação física, não um bug) — o botão "Tirar foto" (via controlador)
// continua funcionando de qualquer lugar, como já era.
//
// Reaproveitando a lib Arduino `WebServer` (em vez de esp_http_server): `server.send(200, mime, "")`
// com `setContentLength(CONTENT_LENGTH_UNKNOWN)` deixa a resposta "aberta"; o handler então escreve
// quadro após quadro com `sendContent()` até o cliente desconectar. Padrão bem conhecido em projetos
// ESP32-CAM com essa mesma lib.
//
// Segurança térmica (a câmera já superaqueceu uma vez neste projeto, ver CLAUDE.md): a cada quadro
// checa `temperatureRead()` contra o mesmo limite do /foto, e o stream tem um teto DURO de duração
// (`STREAM_MAX_MS`) — passado esse tempo a conexão fecha sozinha mesmo sem erro, o app precisa
// reabrir. Isso é intencional (defesa extra, não só a "boa vontade" do cliente fechar a aba).
// ═══════════════════════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "../camera_pins.h"
#include "wifi_config.h"

#define SD_CLK_PIN 39
#define SD_CMD_PIN 38
#define SD_D0_PIN  40
#define TEMP_LIMITE_C 75.0f

#define STREAM_MAX_MS      (5UL*60UL*1000UL)   // teto duro de 5 min por conexão — reabrir depois (defesa contra superaquecimento)
#define STREAM_FRAME_MS_MIN 100UL              // ~10 fps no máximo (pedido: 5-10 fps)
#define STREAM_JPEG_QUALITY 15                 // um pouco mais leve que o /foto avulso (12): menos CPU/RAM por quadro

WebServer server(80);
WebServer streamServer(81);
bool sdOK=false;
int contadorFoto=0;
unsigned long ultimaFotoMs=0;
volatile bool cameraEmUso=false;   // true durante /foto OU /stream — evita os dois brigarem pelo mesmo sensor

static bool iniciarCamera(int qualidade){
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;      // ~40-80 KB: cabe no documento do Firestore (base64) com folga
  config.jpeg_quality = qualidade;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  if(esp_camera_init(&config)!=ESP_OK) return false;
  sensor_t* s=esp_camera_sensor_get();
  if(s) s->set_vflip(s,1);                 // conferir na 1a foto: muitos clones montam invertido
  return true;
}

void handleStatus(){
  char b[110]; snprintf(b,sizeof(b),"{\"ok\":true,\"temp\":%.1f,\"sd\":%s,\"streaming\":%s}",temperatureRead(),sdOK?"true":"false",cameraEmUso?"true":"false");
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",b);
}

void handleFoto(){
  if(cameraEmUso){ server.send(503,"text/plain","camera em uso (stream ao vivo aberto)"); return; }
  if(temperatureRead()>TEMP_LIMITE_C){ server.send(503,"text/plain","chip quente demais"); return; }
  if(millis()-ultimaFotoMs<1500){ server.send(429,"text/plain","aguarde"); return; }
  cameraEmUso=true;
  if(!iniciarCamera(12)){ cameraEmUso=false; server.send(500,"text/plain","falha ao iniciar a camera"); return; }
  delay(400);                              // deixa o sensor estabilizar (AEC/AGC)
  camera_fb_t* fb=nullptr;
  for(int i=0;i<4;i++){
    fb=esp_camera_fb_get();
    if(fb && i<2){ esp_camera_fb_return(fb); fb=nullptr; delay(120); continue; }   // 2 quadros de aquecimento
    if(fb) break;
  }
  if(!fb){ esp_camera_deinit(); cameraEmUso=false; server.send(500,"text/plain","sem quadro (ver cabo/placa)"); return; }
  if(sdOK){
    char p[40]; snprintf(p,sizeof(p),"/cam/foto_%03d.jpg",++contadorFoto);
    File f=SD_MMC.open(p,FILE_WRITE); if(f){ f.write(fb->buf,fb->len); f.close(); }
  }
  server.sendHeader("Cache-Control","no-store");
  server.send_P(200,"image/jpeg",(const char*)fb->buf,fb->len);
  Serial.printf("[Foto] %u bytes enviados\n",(unsigned)fb->len);
  esp_camera_fb_return(fb);
  esp_camera_deinit();                     // desliga o clock do sensor: nao esquenta parado
  ultimaFotoMs=millis();
  cameraEmUso=false;
}

// Fase 66 — /stream (porta 81): multipart MJPEG. Roda inteiro dentro deste handler (bloqueia esta
// WebServer até o cliente desconectar, o tempo esgotar ou o chip esquentar demais) — a outra porta
// (80, /status e /foto) continua respondendo normalmente porque é OUTRA instância de WebServer.
static const char* STREAM_BOUNDARY = "\r\n--frame\r\n";
void handleStream(){
  if(cameraEmUso){ streamServer.send(503,"text/plain","camera em uso"); return; }
  if(temperatureRead()>TEMP_LIMITE_C){ streamServer.send(503,"text/plain","chip quente demais"); return; }
  cameraEmUso=true;
  if(!iniciarCamera(STREAM_JPEG_QUALITY)){ cameraEmUso=false; streamServer.send(500,"text/plain","falha ao iniciar a camera"); return; }
  delay(300);
  WiFiClient client = streamServer.client();
  streamServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  streamServer.send(200,"multipart/x-mixed-replace; boundary=frame","");
  streamServer.sendContent("Access-Control-Allow-Origin: *\r\n");   // permite o PWA (outra origem) usar a imagem num <canvas> (deteccao de movimento)
  Serial.println("[Stream] iniciado");
  unsigned long t0=millis(), ultQuadro=0;
  int quadros=0;
  while(client.connected() && millis()-t0<STREAM_MAX_MS){
    if(temperatureRead()>TEMP_LIMITE_C){ Serial.println("[Stream] abortado -- chip quente demais"); break; }
    if(millis()-ultQuadro<STREAM_FRAME_MS_MIN){ delay(5); continue; }
    camera_fb_t* fb=esp_camera_fb_get();
    if(!fb){ delay(50); continue; }
    ultQuadro=millis();
    char cab[64]; snprintf(cab,sizeof(cab),"Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",(unsigned)fb->len);
    streamServer.sendContent(STREAM_BOUNDARY);
    streamServer.sendContent(cab);
    client.write(fb->buf,fb->len);
    esp_camera_fb_return(fb);
    quadros++;
    yield();   // cede a vez ao WiFi/RTOS entre quadros
  }
  Serial.printf("[Stream] encerrado (%d quadros, %lus)\n",quadros,(millis()-t0)/1000UL);
  esp_camera_deinit();
  cameraEmUso=false;
}

void iniciarWifi(){
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(CAM_WIFI_SSID,CAM_WIFI_PASS);
  unsigned long t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(200); }
  if(WiFi.status()==WL_CONNECTED){
    Serial.printf("WiFi OK: %s\n",WiFi.localIP().toString().c_str());
    if(MDNS.begin(CAM_MDNS_NOME)) { MDNS.addService("http","tcp",80); Serial.printf("mDNS: %s.local\n",CAM_MDNS_NOME); }
  }else Serial.println("WiFi: sem conexao (confira wifi_config.h) -- tentando de novo em segundo plano.");
}

void setup(){
  Serial.begin(115200); delay(1200);
  Serial.println("\n=== ChocaIoT-Camera: servidor HTTP (Fase 50/66) ===");
  Serial.printf("PSRAM: %s | temp chip %.1f C\n",psramFound()?"OK":"NAO ENCONTRADA",temperatureRead());
  SD_MMC.setPins(SD_CLK_PIN,SD_CMD_PIN,SD_D0_PIN);
  sdOK=SD_MMC.begin("/sdcard",true) && SD_MMC.cardType()!=CARD_NONE;
  if(sdOK && !SD_MMC.exists("/cam")) SD_MMC.mkdir("/cam");
  Serial.printf("SD: %s\n",sdOK?"OK":"indisponivel (fotos so sao enviadas, nao gravadas)");
  iniciarWifi();
  server.on("/status",HTTP_GET,handleStatus);
  server.on("/foto",HTTP_GET,handleFoto);
  server.begin();
  streamServer.on("/stream",HTTP_GET,handleStream);
  streamServer.begin();
  Serial.println("Servidor pronto: porta 80 (/status, /foto) e porta 81 (/stream)");
}

void loop(){
  server.handleClient();
  streamServer.handleClient();   // so retorna quando o handler termina (stream inteiro roda dentro dele) -- ver comentario em handleStream
  static unsigned long ultWifi=0;
  if(WiFi.status()!=WL_CONNECTED && millis()-ultWifi>15000){ ultWifi=millis(); WiFi.reconnect(); }
  static bool mdnsIniciado=true; static bool eraOk=true;
  bool ok=(WiFi.status()==WL_CONNECTED);
  if(ok && !eraOk){ MDNS.end(); if(MDNS.begin(CAM_MDNS_NOME)) MDNS.addService("http","tcp",80); }   // voltou do zero: refaz o mDNS
  eraOk=ok; (void)mdnsIniciado;
  delay(2);
}
