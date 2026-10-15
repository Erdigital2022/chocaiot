# 07 — Solução de problemas

| Sintoma | Causa mais provável | O que fazer |
|---|---|---|
| Botões do app "não fazem nada" e o serial mostra `HTTP 429` | **Cota diária de leituras** do Firestore estourada | Esperar o reset (meia-noite, horário do Pacífico) e rever qualquer leitura periódica nova |
| PWA mostra "chocadeira offline" mas o aparelho funciona | Listener do navegador parado / aba em segundo plano | Reabrir a aba; o app reconfere ao voltar ao foco |
| Gravação: `Failed to connect… no sync reply: serial TX path seems to be down` | Fios nos bornes **RX/TX** (UART0), cabo ruim, ou porta ocupada | Tirar os fios de RX/TX, fechar monitores seriais, trocar cabo; ou BOOT+RST manual |
| Porta COM some durante a gravação | USB instável / EMI da carga ligada | Religar o cabo; gravar com cargas desligadas |
| **Bomba ou resistência liga sozinha ao gravar/reiniciar** | Pino de relé flutuando no bootloader | *Pull-up* de 10 kΩ ao 3V3 nas entradas IN; até lá, retirar da tomada |
| Reinício sem aviso ("Panic!" no Sistema) | Exceção de software | Ver o log `Panic … pc=… bt=…` e decodificar com `addr2line` |
| "Brownout!" | Alimentação insuficiente | Alimentar pelo conversor 5 V, não só USB; capacitor de bulk |
| CO₂: "Sem resposta (0 bytes recebidos no GPIO16)" | Fio TX/RX trocado/solto, 5 V ausente, ou fios nos bornes errados | Conferir GPIO15/16 (não RX/TX), trocar TX↔RX, medir 5,0 V no sensor |
| CO₂ com leituras absurdas | 5 V fora de 5,0 ±0,1 V | Regulador dedicado de 5 V |
| Tela branca depois de mexer no SD | `SPI.begin()` extra disputando o barramento | Usar `SD.begin(CS, tft.getSPIinstance())` |
| SD "removido" a todo instante | Mau contato nos 4 fios compartilhados com o display | Prender/soldar os contatos |
| Temperatura passa do alvo e oscila | Caixa pequena com aquecedor forte | Informar **potência e volume** em *Hardware*; verificar o diagnóstico "volume?" |
| Umidade cai e o umidificador "trabalha" | Reservatório sem água | O firmware detecta e avisa ("sem água"); encher e aguardar |
| Status "TAMPA" sem ter aberto | Módulo ADXL se mexendo/vibração | Calibrar *fechada* e *fora* na tela Tampa; ver `ign N` |
| Card Status em "AUTO OFF" | Automação desligada (tocar num atuador a desliga) | Religar a chave em *Atuadores*; volta sozinha em 10 min com ciclo |
| Gráfico "7 dias" vazio | Sem histórico gravado ainda (1 ponto/10 min) | Aguardar; CO₂ só aparece depois do firmware com sensor |
| `pio run` falha por falta de `firebase_credentials.h` | Arquivo de credenciais ausente | Copiar o `.example.h` e preencher |
| App abre em branco depois de um deploy | Cache do service worker antigo | "Forçar atualização" no menu ou aumentar `CACHE_VERSION` |

## Coleta de diagnóstico para abrir uma *issue*

1. Versão/fase do firmware (tela *Sistema*) e o **motivo do último reset**.
2. Linhas relevantes do Log (Config. → Logs) — **sem** e-mails/IDs.
3. Fotos da fiação (sem expor a senha do WiFi nem o ID da chocadeira).
4. O que mudou antes do problema começar.
