// camera_pins.h — ChocaIoT-Camera
// Placa: ESP32-S3-WROOM-1 N16R8 CAM + OV5640
//
// ⚠️ STATUS: pinout de alta confiança, mas NÃO confirmado fisicamente
// nesta placa específica.
//
// Fonte: dois registros independentes (issue do repositório oficial
// arduino-esp32 da Espressif + thread do fórum oficial Arduino), ambos
// tratando explicitamente de placas "ESP32-S3-N16R8 CAM", com os
// MESMOS 15 valores de pino. Não é coincidência — esse conjunto de
// valores não teria motivo pra bater em duas fontes diferentes por
// acaso, então isso deve ser o design de referência que os fabricantes
// desses clones replicam.
//
// Reforço de confiança: nenhum destes pinos colide com os GPIOs
// restritos do ESP32-S3 (35-37 = PSRAM Octal, 19-20 = USB nativo) —
// se fosse um pinout inventado/errado, essa coincidência favorável
// seria improvável.
//
// Ainda assim: se o primeiro teste de captura falhar, o próximo passo
// é confirmar esses pinos com multímetro (continuidade) traçando o
// FPC da câmera até os pads de solda correspondentes na placa.

#pragma once

#define PWDN_GPIO_NUM   -1   // não usado nesta placa
#define RESET_GPIO_NUM  -1   // não usado nesta placa

#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM    4   // I2C SDA (configuração do sensor OV5640)
#define SIOC_GPIO_NUM    5   // I2C SCL (configuração do sensor OV5640)

#define Y9_GPIO_NUM     16   // D7
#define Y8_GPIO_NUM     17   // D6
#define Y7_GPIO_NUM     18   // D5
#define Y6_GPIO_NUM     12   // D4
#define Y5_GPIO_NUM     10   // D3
#define Y4_GPIO_NUM      8   // D2
#define Y3_GPIO_NUM      9   // D1
#define Y2_GPIO_NUM     11   // D0

#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM   13
