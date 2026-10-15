# 01 — Arquitetura

## Componentes

| Componente | Pasta | Papel |
|---|---|---|
| **Controlador** | `firmware/` | Lê sensores, controla relés/PWM, mostra a tela local, guarda logs/histórico no SD e sincroniza com o Firebase |
| **Câmera** | `camera/` | Placa ESP32-S3-CAM independente; serve foto e MJPEG por HTTP **na rede local** |
| **PWA** | `pwa/` | App web (sem framework): painéis, configuração, compartilhamento, histórico |
| **Firebase** | — | Auth (login Google), Firestore (estado/comandos/histórico), Hosting (PWA), FCM (push) |

Princípio central: **o aparelho é autônomo**. Sem internet ele continua controlando temperatura, umidade e o ciclo
(o estado do ciclo é gravado em `/ciclo.cfg` e restaurado no boot; o relógio vem do RTC DS3231).

## Fluxo de dados

```
PWA ──escreve──▶ chocadeiras/{id}  (campos atuadores.chN, cmd.*, ciclo*)
                      │
                      ▼  poll HTTPS (3 s com app aberto · 15 s sem app)
                 ESP32 aplica ─▶ relés/PWM ──▶ publica estado de volta
                      │
                      └─ heartbeat (60 s; 20 s com app aberto): temp/umid/CO₂, ambiente, painel...
PWA ◀──onSnapshot──── (atualiza cards em tempo real)
```

Não existe *listener* em tempo real do Firestore para microcontroladores: o firmware faz **polling** com
`DocumentMask` mínimo. O PWA grava `cmd.vivo` (um texto que muda) para o aparelho saber que há alguém olhando e
acelerar o polling.

## Modelo do Firestore (resumo)

```
chocadeiras/{id}                         ← documento principal
  donoId, nome, firmwareLastSeen, tempAtual, umidAtual, co2Atual
  atuadores: { ch0..ch7: bool }          ← espelho dos 8 relés
  cicloIniciadoEm, cicloPausadoEm, cicloDuracaoDias, cicloNome, cicloTipoNome, cicloExtra
  tiposOvos: "nome;dias;temp;umid;...|..."   ← string compacta (parse barato no ESP32)
  cmd: { auto, vivo, telaSet, fansSet, fansRegras, adxlCfg, adxlCal, ... }   ← comandos app → aparelho
  painel: "T|...#F|...#R|...#G|..."      ← telemetria compacta de tela/fans/rpm
  ambiente: "modo;seg;total;diag;A"      ← estado do ambiente (status do card)
  ├─ leituras/{auto}      temperatura, umidade, co2, timestamp       (a cada 5 min)
  ├─ historico/AAAA-MM-DD mapa m: { s0..s143: "t10,h10[,co2]" }      (1 ponto / 10 min)
  ├─ logs/{auto}          categoria, mensagem, ator, timestamp
  ├─ ciclos/{auto}        relatórios dos lotes encerrados
  ├─ arquivos/{indice|conteudo}  gerenciador de arquivos do SD (sob demanda)
  ├─ shared_with/{email}  convites e permissões
  ├─ acessos/{auto}       auditoria de acessos (aparelho, bloqueios)
  └─ camera/ultima        última foto (base64)
usuarios/{uid}            acessos[] (chocadeiras compartilhadas)
```

Por que strings compactas (`painel`, `tiposOvos`, `cmd.auto`)? A API REST do Firestore devolve JSON "verboso";
cada campo adicional na máscara do `GET` custa memória e tempo no ESP32 — e, na biblioteca usada, máscaras com
muitos campos chegaram a retornar `HTTP 400`. Ver [`docs/03-firmware.md`](03-firmware.md).

## Cota gratuita do Firestore (50 mil leituras e 20 mil escritas por dia)

| Fonte | Custo aproximado/dia |
|---|---|
| Poll do aparelho, app fechado (15 s) | ~5,8 mil leituras |
| Poll do aparelho, app aberto (3 s) | até ~28 mil leituras |
| Heartbeat (60 s) | 1,4 mil escritas |
| Leituras de T/U/CO₂ (5 min) | 288 escritas |
| Histórico (10 min) | 144 escritas |

Regras práticas que o projeto adotou depois de estourar a cota duas vezes:

1. **Nunca** re-assinar um `onSnapshot` por temporizador (relê o lote inteiro).
2. Preferir `onSnapshot` a polling no PWA.
3. Calcular o impacto de qualquer leitura periódica nova **antes** de adicioná-la.
4. `HTTP 429` no serial = cota estourada, não bug do comando.

## Decisões de projeto que valem conhecer

| Decisão | Motivo |
|---|---|
| Polling + strings compactas | Sem listener no ESP32; economia de RAM/cota |
| PI de ciclo fixo (não "liga/desliga") | Caixa pequena perde calor o tempo todo; o integral aprende o *duty* de regime |
| Preparo antes do ciclo | Estabilizar a caixa **sem ovos**; o dia 1 só começa quando o usuário confirma |
| Tampa aberta tratada como estado | Evita alarmes e integral descontrolado na ovoscopia |
| Hardware informado pelo usuário (volume, potência) | Ajusta ganhos iniciais do PI e permite diagnóstico ("volume errado?") |
| Segredos fora do repositório | Ver [`05-seguranca.md`](05-seguranca.md) |
