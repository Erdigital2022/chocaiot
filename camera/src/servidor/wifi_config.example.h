// wifi_config.example.h — MODELO da camera (ChocaIoT-Camera).
// Copie para  camera/src/servidor/wifi_config.h  (ja esta no .gitignore) e preencha.
// A camera e o controlador precisam estar na MESMA rede local: o controlador acha a camera por mDNS.
#pragma once
#define CAM_WIFI_SSID "SEU_WIFI"
#define CAM_WIFI_PASS "SUA_SENHA_WIFI"
#define CAM_MDNS_NOME "chocacam"   // nao mude sem mudar CAM_HOST no controlador (firmware/src/main.cpp)
