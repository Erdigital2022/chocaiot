# Integração da câmera (Fase 50) — como as peças se falam

```
  PWA (celular)            Firestore                Controlador (ESP32-S3 + tela)         ESP32-S3-CAM
  ─────────────            ─────────                ─────────────────────────────         ────────────
  "Tirar foto" ──cameraReq─►  doc da chocadeira ──poll 2,5 s (já existente, custo 0)──►  cameraServico()
                                                                                              │  HTTP local
                                                    task "camLan": mDNS chocaiot-cam.local ───► GET /status (a cada 20 s)
                                                                                          ───► GET /foto   (sob pedido)
  mostra a foto ◄──────── camera/ultima ◄── base64 (VGA, ~40-80 KB) ◄── controlador ◄── JPEG
  botão habilitado ◄──── cameraOnline (bool, só escreve quando muda)
```

**Por que assim?** A cota gratuita do Firestore (50 mil leituras/dia) já está ocupada pelo poll do controlador
(~35 mil/dia). Se a câmera também consultasse a nuvem, estouraria. Com o controlador fazendo a ponte, a câmera
não gasta nenhuma leitura e o pedido do PWA chega junto do poll que já existe.

## Contrato

| Onde | Campo / rota | Quem escreve | Formato |
|---|---|---|---|
| `chocadeiras/{id}` | `cmd.camReq` | PWA (ou controlador) | string com um id do pedido |
| `chocadeiras/{id}` | `cameraOnline` | controlador | bool — só grava quando o módulo aparece/some |
| `chocadeiras/{id}/camera/ultima` | `reqId, mime, tam, tirouEm, erro, b64` | controlador | `erro` vazio = sucesso; `b64` = JPEG em base64 |
| Câmera `GET /status` | — | câmera | `{"ok":true,"temp":41.2,"sd":true}` |
| Câmera `GET /foto` | — | câmera | `image/jpeg` (VGA) · 503 chip quente · 500 falha |

Regras do Firestore (`firestore.rules`): `camera/*` só leitura para o dono; escrita só pelo controlador (Admin).

## Para colocar em uso quando a placa nova chegar

1. **Sem o cabo da câmera**, grave o teste de curto (`src/diag_curto_cam7.cpp.bk`, copie para `src/main.cpp` temporariamente)
   e confira "nenhum par ligado". A placa V1695 anterior tinha GPIO 9–13 em curto (ver `CLAUDE.md` do controlador, §2.2).
2. Rode o teste de hardware (`pio run -e esp32-s3-cam -t upload`): SD + fotos VGA/XGA/UXGA + guarda térmica.
3. Edite `src/servidor/wifi_config.h` com o **mesmo WiFi** do controlador e grave
   `pio run -e esp32-s3-cam-servidor -t upload`.
4. No monitor serial deve aparecer `WiFi OK`, `mDNS: chocaiot-cam.local`. Teste no navegador do PC (mesma rede):
   `http://chocaiot-cam.local/status` e `http://chocaiot-cam.local/foto`.
5. No controlador, o card "Camera" da home passa a "Online" (até ~20 s) e a tela Camera habilita "Tirar foto".
   No PWA, o painel da câmera habilita o botão "Tirar foto" e mostra a imagem.

## Limitações conhecidas
- Foto VGA (640×480): cabe no documento de 1 MB do Firestore e sobe em poucos segundos. Resoluções maiores exigiriam
  Firebase Storage (plano Blaze) — decisão futura.
- mDNS depende do roteador/rede permitir multicast (redes de convidados às vezes bloqueiam). Se `chocaiot-cam.local`
  não resolver, dá para trocar por IP fixo no controlador (`CAM_HOST` em `main.cpp`).
- O caminho câmera→controlador é HTTP simples, sem autenticação — só na rede local.

## Fase 66 — vídeo ao vivo (streaming MJPEG, porta 81)

```
  PWA (celular, MESMA rede Wi-Fi)                  Firestore              Controlador               ESP32-S3-CAM
  ────────────────────────────────                 ─────────              ───────────               ────────────
  <img src="http://<cameraIP>:81/stream">  ◄─────── vídeo direto, NUNCA passa pela nuvem ─────────────────┘
  cmd.camStreamPing (avisinho, 1 escrita) ────────►  doc ──poll (custo 0)──► loga "Stream acessado"
  cameraOnline / cameraIP  ◄──────────────────────── doc ◄── controlador publica (so quando mudam) ◄── /status (mDNS)
```

- **O vídeo nunca passa pela nuvem** — o app só recebe o IP da câmera (campo `cameraIP`, publicado pelo controlador
  quando o mDNS resolve) e monta `http://<cameraIP>:81/stream` sozinho. Isso só funciona com o celular na
  **mesma rede Wi-Fi/local** da câmera — fora de casa o vídeo fica inacessível (limitação física da arquitetura,
  não um bug); o botão "Tirar foto" (via Firestore/controlador) continua funcionando de qualquer lugar.
- **Porta 81 = `WebServer streamServer` separado** de `server` (porta 80, `/status`/`/foto`) — o handler de
  `/stream` roda **dentro de si mesmo** um laço que envia quadro após quadro (multipart `x-mixed-replace`) até o
  cliente desconectar, o chip esquentar (`temperatureRead()`, mesmo limite do `/foto`) ou passar
  `STREAM_MAX_MS` (5 min, teto duro — depois disso o app precisa reabrir). A porta 80 continua respondendo
  normalmente (é outra instância de `WebServer`).
- `cameraEmUso` (flag global) impede `/foto` e `/stream` de disputar o mesmo sensor ao mesmo tempo — o que
  chegar primeiro vence, o outro recebe HTTP 503.
- `Access-Control-Allow-Origin: *` no `/stream`: permite o PWA ler os quadros num `<canvas>` (detecção de
  movimento local) sem "manchar" o canvas — sem isso, `crossorigin="anonymous"` no `<img>` faria o navegador
  bloquear a leitura de pixels mesmo com o vídeo aparecendo normalmente.
- **Sem hardware físico ainda** (ver `CLAUDE.md` do controlador, §2.2) — este servidor de stream foi escrito
  seguindo o mesmo padrão consagrado dos exemplos oficiais do `esp32-camera` (handler bloqueante com
  `WebServer::sendContent()`), mas **nada aqui foi testado numa câmera real**.
- **Mixed content**: o PWA é servido via HTTPS (`SEU-PROJETO.web.app`); o stream é HTTP puro (a câmera não
  tem certificado). Nem todo navegador deixa uma página HTTPS **embutir** um recurso HTTP (mesmo "passivo"). Por
  isso o app sempre mostra um link "Abrir numa aba nova" ao lado do vídeo — abrir o stream como página própria
  (navegação de topo) não sofre essa restrição e é o caminho garantido se o `<img>` embutido não carregar.
