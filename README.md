# ChocaIoT — chocadeira inteligente com ESP32-S3, Firebase e PWA

Controlador completo para uma **chocadeira de ovos** (galinha, codorna e outros): mede temperatura, umidade e CO₂,
controla aquecimento, umidificação, ventilação e fans por **malha fechada (PI)**, guia o ciclo de incubação dia a dia
e é operado tanto pela **tela touch do aparelho** quanto por um **app web (PWA)** instalável no celular.

> ⚠️ **Segurança elétrica.** Este projeto comanda cargas de **220 V** (resistência, bomba, lâmpada). Monte e teste com
> a chocadeira **desligada da tomada**, use fusível/DR e leia [`docs/05-seguranca.md`](docs/05-seguranca.md) antes de
> energizar. Você é responsável pela sua montagem.

---

## O que ele faz

| Área | Recursos |
|---|---|
| **Ambiente** | Temperatura/umidade (SHT31) e CO₂ (MH-Z19C) com gráficos do dia, histórico de 7 dias e alertas |
| **Controle** | Aquecedor e umidificador por **PI de ciclo fixo** que aprende a caixa; bomba de ar; cooler do sistema; fans de PC com PWM de 25 kHz |
| **Ciclo** | Tipos de ovo configuráveis (temperatura/umidade/viragem/ovoscopia/eclosão), **preparo** sem ovos até estabilizar, contagem de dias, relatório do lote |
| **Segurança** | Sobretemperatura, sensor sem leitura, água do umidificador, tampa aberta (acelerômetro), PIN do aparelho, bloqueio biométrico do app |
| **Remoto** | PWA com login Google, compartilhamento com permissões por pessoa, notificações push, auditoria de acessos |
| **Aparelho** | Tela touch 3,2", brilho/escurecimento, gerenciador de arquivos do SD, logs, relatórios, deep sleep |
| **Câmera** | Módulo ESP32-S3-CAM separado (foto sob demanda, timelapse, vídeo ao vivo na rede local) |

## Visão geral da arquitetura

```
                 ┌────────────────────────────────────────────┐
  sensores ────▶ │  ESP32-S3 (controlador)   firmware/        │ ──▶ relés (8 canais) ──▶ cargas
 SHT31 · ADXL345 │  PI de temperatura/umidade · ciclo · telas │ ──▶ PWM fans · backlight
 MH-Z19C · DS3231│  logs/histórico no SD · deep sleep         │
                 └───────────────┬────────────────────────────┘
                                 │ HTTPS (polling + escrita)        mDNS/HTTP (rede local)
                                 ▼                                       │
                        ┌─────────────────┐                      ┌───────▼────────┐
                        │ Firebase        │                      │ ESP32-S3-CAM   │
                        │ Auth · Firestore│                      │ camera/        │
                        │ Hosting · FCM   │                      └────────────────┘
                        └────────┬────────┘
                                 │ onSnapshot (tempo real)
                                 ▼
                        ┌─────────────────┐
                        │ PWA   pwa/      │  celular / computador
                        └─────────────────┘
```

O aparelho **funciona sozinho** (sem internet): a nuvem só espelha estado, recebe comandos e entrega notificações.

## Estrutura do repositório

```
firmware/    Controlador (PlatformIO / Arduino-ESP32): src/main.cpp, lib/, tools/
camera/      Módulo de câmera (ESP32-S3-CAM): servidor HTTP/MJPEG
pwa/         App web (HTML/JS puro): index.html, app.js, extras.js, painel.js, service-worker.js, firestore.rules
docs/        Documentação, esquemas elétricos (PNG/SVG + scripts que os geram) e histórico de desenvolvimento
```

## Início rápido (visão de 10 minutos)

1. **Clone** e instale o [PlatformIO](https://platformio.org/) (extensão do VS Code ou CLI).
2. **Crie o projeto Firebase** (Auth Google + Firestore + Hosting) — passo a passo em
   [`docs/04-firebase-e-pwa.md`](docs/04-firebase-e-pwa.md).
3. **Credenciais do firmware:** copie `firmware/src/firebase_credentials.example.h` para
   `firmware/src/firebase_credentials.h` e preencha (esse arquivo **nunca** é versionado).
4. **ID da chocadeira:** crie o documento `chocadeiras/<id>` no Firestore e coloque o `<id>` em
   `FIRESTORE_DOCUMENT_PATH` (`firmware/src/main.cpp`).
5. **Compile e grave** (com as cargas de 220 V **fora da tomada**):
   ```bash
   cd firmware
   pio run -e esp32-s3-controller -t upload
   ```
6. **PWA:** preencha `firebaseConfig` em `pwa/app.js` e `pwa/service-worker.js` e publique:
   ```bash
   cd pwa
   firebase deploy --only hosting,firestore:rules --project SEU-PROJETO
   ```
7. Abra o app, entre com sua conta Google e **vincule** a chocadeira pelo ID mostrado em *Configurações > Sistema* do aparelho.

## Hardware (lista de materiais resumida)

| Item | Função |
|---|---|
| ESP32-S3 N16R8 (YD-ESP32-S3 44P) | Controlador |
| Display TFT ILI9341 3,2" SPI + touch XPT2046 + microSD | Interface local, logs |
| SHT31 (sonda IP68) | Temperatura e umidade |
| MH-Z19C | CO₂ (UART, 5 V) |
| ADXL345 | Detecção de tampa aberta |
| DS3231 | Relógio de tempo real |
| Módulo de relés 8 canais 5 V | Cargas (resistência, umidificador, bombas, lâmpada, cooler, fans) |
| Conversor DC-DC 12/24 V → 5 V (3 A) | Alimentação |
| Fans de PC 4 fios | Circulação / resfriamento / renovação |
| (opcional) MCP23017, MAX98357A | Expansão de I/O, áudio |

Ligações, pinagem completa e esquemas: [`docs/02-hardware.md`](docs/02-hardware.md) e [`docs/esquemas/`](docs/esquemas/).

## Documentação

| Documento | Conteúdo |
|---|---|
| [01 — Arquitetura](docs/01-arquitetura.md) | Componentes, fluxo de dados, modelo do Firestore, cota |
| [02 — Hardware](docs/02-hardware.md) | Pinagem, ligações por subsistema, alimentação, armadilhas |
| [03 — Firmware](docs/03-firmware.md) | Compilação, telas, controle PI, ciclo, CO₂, fans, diagnóstico |
| [04 — Firebase e PWA](docs/04-firebase-e-pwa.md) | Criar o projeto, regras, hospedagem, notificações |
| [05 — Segurança](docs/05-seguranca.md) | Segredos, regras, 220 V, o que nunca publicar |
| [06 — Exemplos](docs/06-exemplos.md) | Receitas prontas: regras, comandos, histórico, novos sensores |
| [07 — Solução de problemas](docs/07-solucao-de-problemas.md) | Sintoma → causa → correção |
| [Histórico](docs/historico-de-desenvolvimento.md) | Diário técnico fase a fase (decisões, bugs e medições) |

## Estado do projeto (honesto)

- **Validado em hardware real:** leitura de sensores, controle PI de temperatura/umidade, ciclo/preparo, PWA com
  compartilhamento e push, fans com PWM e tacômetro, ADXL345, CO₂ MH-Z19C (comunicação, gráficos, alertas), SD/logs.
- **Compilado mas pouco testado em campo:** alertas de CO₂ por vários minutos, regra de renovação por CO₂,
  relatório de ciclo completo, bomba de água dosada.
- **Pendente / planejado:** câmera integrada (hardware em troca), áudio (MAX98357A), expansor MCP23017,
  proteção por *pull-up* nos relés (recomendada — ver `docs/02-hardware.md`).

## Como este projeto foi feito

Projeto de um autor individual, validado em bancada com hardware real. Parte do código e da documentação foi
produzida com apoio de IA (Claude Code) e revisada/testada pelo autor; o diário técnico completo está em
[`docs/historico-de-desenvolvimento.md`](docs/historico-de-desenvolvimento.md).

## Licença e créditos

Código e documentação sob a **licença MIT** (arquivo [`LICENSE`](LICENSE)). Créditos das bibliotecas em `firmware/platformio.ini`
(TFT_eSPI, FirebaseClient, RTClib, Adafruit SHT31/ADXL345, TJpg_Decoder, QRCode).
As imagens da galeria "Fases do pintinho" **não** acompanham o repositório (veja
[`docs/galeria-e-wallpaper.md`](docs/galeria-e-wallpaper.md)).
