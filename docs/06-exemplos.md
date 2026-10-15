# 06 — Exemplos práticos

Receitas curtas para o que mais se faz com o projeto. Todos os exemplos usam a UI; onde existe um campo de
Firestore equivalente ele é mostrado para você entender o formato.

## 1. Criar um tipo de ovo (ex.: pata)

**Pelo app:** Configurações → *Tipos de ovos* → **+ Novo**.
**Pelo aparelho:** Configurações (pág. 2) → *Ovos* → **+**.

| Campo | Exemplo (pata) | Efeito |
|---|---|---|
| Dias | 28 | duração do ciclo |
| Temperatura ideal / mín / máx | 37,5 / 37,2 / 37,8 °C | faixa dos alertas e alvo do PI |
| Umidade ideal / mín / máx | 60 / 55 / 70 % | idem |
| Umidade na eclosão | 75 % | faixa muda a partir do dia da eclosão |
| Viragem | 4×/dia, 1ª às 08:00, até o dia 25 | parâmetros da viragem |
| Ovoscopia | dias 7 e 14 | marcos na barra do ciclo |
| Eclosão | início no dia 25 | troca de faixas e de limites de CO₂ |

No Firestore o tipo é uma **string compacta** (campo `tiposOvos`, tipos separados por `|`):

```
nome;dias;temp;umid;umidEcl;viraAte;tempMin;tempMax;umidMin;umidMax;viraPorDia;viraHH;viraMM;ovo1;ovo2;eclDia;tempEcl
Pata;28;375;60;75;25;372;378;55;70;4;8;0;7;14;25;375
```

(temperaturas em décimos de °C: `375` = 37,5 °C).

## 2. Regra por relé: ventilar quando a umidade passa de 54 %

Automação → canal **CH4** → modo *Desumidificar/ventilar* → alvo personalizado 52 %, histerese 2 pts.
Liga acima de 54 %, desliga em ≤ 52 %. Janela opcional 08:00–18:00. No `cmd.auto` cada canal é:

```
modo , alvoCustom , alvo×10 , hist×10 , onSeg , cadaMin , hIni , hFim , autoOffMin
  4  ,     1      ,   520   ,   20    ,  120  ,   60    ,  480 , 1080 ,    0
```

Modos: `0` manual · `1` aquecer · `2` resfriar · `3` umidificar · `4` desumidificar/ventilar · `5` periódico ·
`6` horário · `7` viragem · `8` circular. `hIni/hFim` em minutos desde 00:00 (480 = 08:00).

## 3. Regra avançada: CO₂ alto liga a bomba de ar

Automação → *Regras avançadas* → **+ Nova regra**:

```
SE   CO₂ ≥ 2800 ppm
ENTÃO liga CH4 (bomba de ar)
DESLIGAR QUANDO CO₂ ≤ 2200 ppm       ← histerese própria (evita liga/desliga rápido)
```

Sensores disponíveis nas condições: temperatura, umidade, temperatura do chip e **CO₂**. Se o sensor estiver sem
leitura a regra é **inibida** (não age). A bomba de ar tem limite de 20 min contínuos e liga o cooler junto.

## 4. Fans por função

Tela *Fans* → toque no nome de cada fan para escolher a função:

| Fan | Posição física | Função | Comportamento |
|---|---|---|---|
| 1 | piso, empurrando o calor para as paredes | **Circulação** | 40–100 % conforme o aquecedor trabalha |
| 2 | 5 cm do piso, empurrando para cima | **Resfriamento** | sobe quando a temperatura passa do ponto médio |
| 3 | parte alta, soprando para a parede com furos | **Renovação** | umidade alta e CO₂ entre 1500 e 3000 ppm |

Regras (mín./máx./limiares) ficam em *Fans → Regras* (aparelho, 4 páginas) ou no card "Regras dos fans" (app).

## 5. Ajustar a tela por comando (ex.: para um script)

```js
// no console do navegador, com o app aberto e logado
db.collection("chocadeiras").doc("SEU_ID").update({
  // <ms>|brilho|reduzirSeg|apagarSeg|manter|relogioSempre
  "cmd.telaSet": `${Date.now()}|60|30|120|0|1`,
});
```

O valor começa com um *timestamp*: o aparelho só aplica quando o texto **muda** (e ignora o que já estava lá no boot).

## 6. Interpretar uma linha do histórico (SD ou Firestore)

```
72,375,552,820      → slot 72 (12:00) · 37,5 °C · 55,2 % · 820 ppm
```

`slot × 10 min` = hora do dia. A 4ª coluna (CO₂) é opcional; arquivos antigos têm 3 colunas.
No Firestore: `historico/2026-10-15` → `m.s72 = "375,552,820"`.

## 7. Adicionar um sensor novo (padrão usado pelo CO₂)

1. **Leitura:** crie `static void meuSensorTick()` e chame no `loop()`; guarde `float meuValor` e `lastMeuSensorOkMs`.
2. **Mostrar na tela:** desenhe em `drawTempUmidCards()` (home) e, se quiser gráfico, reaproveite
   `drawSensorHistChart()` (unidade `"ppm"` mostra sem casas decimais).
3. **Nuvem:** em `pushHeartbeatToFirestore()` acrescente o campo e o nome em `hbMask`:
   ```cpp
   if(!isnan(meuValor)) { doc.add("meuValor", Values::Value(Values::DoubleValue(number_t(meuValor,1)))); hbMask+=",meuValor"; }
   ```
4. **PWA:** `app.js` → `atualizarCardsComLeitura()` e `FAIXAS` (cores); o card em `index.html`.
5. **Regras avançadas:** inclua o sensor em `advLeitura()` e na lista `ADV_SENSOR`.
6. **Cota:** a escrita já viaja no heartbeat (sem custo extra). Não crie novo `onSnapshot` periódico.

## 8. Trocar um pino

Pinos de relé: `PIN_RELE_CH1..CH8` no topo do `main.cpp`. Antes de escolher, consulte a tabela de pinos em
[`02-hardware.md`](02-hardware.md): **evite** 0, 3, 45, 46 (strapping), 19/20 (USB), 35–37 (PSRAM) e 43/44 (UART0).

## 9. Ler o log sem serial

Config. → *Logs*: filtre por **Tipo** (Boot, Alerta, CO2, Panic, Automação…) ou por data; o mesmo conteúdo vai
para `chocadeiras/<id>/logs` e aparece no app.

## 10. Decodificar um crash

```text
Log:  Panic  Crash task=loopTask pc=420a1b2c causa=28 vaddr=00000000 bt=420a1b2c,4208ffee,...
```
```bash
xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf 0x420a1b2c 0x4208ffee
# → funcao() at firmware/src/main.cpp:1234
```
`causa=28/29` = acesso a ponteiro nulo (LoadProhibited/StoreProhibited). Use o `.elf` do **mesmo** build gravado.
