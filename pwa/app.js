// app.js — ChocaIoT PWA
// Pendência #7: login Google real + criação do documento de usuário.
// NOVO: controle de atuadores (espelha os 8 canais do relé do firmware).

// --- Config do projeto (igual ao firmware/checkpoint) ---
const firebaseConfig = {
  apiKey: "SUA_API_KEY_WEB_DO_FIREBASE",
  authDomain: "SEU-PROJETO.firebaseapp.com",
  projectId: "SEU-PROJETO",
  storageBucket: "SEU-PROJETO.firebasestorage.app",
  messagingSenderId: "SEU_MESSAGING_SENDER_ID",
  appId: "SEU_APP_ID",
  measurementId: "G-XXXXXXXXXX",
};

firebase.initializeApp(firebaseConfig);
const auth = firebase.auth();
const db = firebase.firestore();

// --- Canais do relé — MESMA ordem/nomes do firmware (RELAY_NAMES) ---
// Mantenha isso sincronizado manualmente com pins_s3.h / main.cpp
// sempre que a fiação dos canais mudar.
const RELAY_NAMES = [
  "Resistência", "Umidificador", "Bomba Água", "Bomba Ar",
  "LED Ovoscópio", "Áudio", "Cooler Sistema", "Fans",
];
// Fase 36 — Cooler Sistema (CH7) virou controle automático por histerese
// de temperatura no firmware (Fase 35). Índice aqui precisa bater com
// COOLER_CH no main.cpp (0-based, canal 7 = índice 6) -- toque manual
// nesse canal é bloqueado dos dois lados (ver montarGridAtuadores/
// atualizarControlesDisponibilidade abaixo), porque seria sobrescrito
// pelo firmware no próximo ciclo de 5s de qualquer forma.
const COOLER_CH_INDEX = 6;
const BOMBA_CH_INDEX = 2;   // Fase 87 — bomba de água (220 V) só liga pelo toque no aparelho / lógica da água; o app não liga (a nuvem religava a bomba após reiniciar)

const RELAY_VOLTAGE = [
  "220V AC", "5V DC", "220V AC", "3.7V DC",
  "220V AC", "5V DC", "12V DC", "12V DC",
];

function classeTensao(v) {
  if (v.includes("220")) return "v220";
  if (v.includes("3.7")) return "v5"; // Fase 83: bomba de ar 3,7 V entra na família de baixa tensão
  if (v.includes("12")) return "v12";
  if (v.includes("5")) return "v5";
  return "vnone";
}

// --- Thresholds (Fase 21) — MESMOS limites do firmware (ver TEMP_IDEAL_MIN
// etc em main.cpp). Padrão geral de incubação de galinha; ainda não existe
// "Perfil de Incubação" configurável, então é uma faixa única fixa por ora.
const FAIXAS = {
  temperatura: { min: 37.2, max: 38.9, margem: 1.0, unidade: "°C", dec: 1, folgaMin: 0.2 },
  umidade: { min: 45, max: 65, margem: 8, unidade: "%", dec: 1, folgaMin: 0.2 },
  // Fase 115 — CO₂ (MH-Z19C). Mesmas faixas do firmware (co2Limites): incubação ≤ 2000 ok / > 3000 alto;
  // pré-eclosão e eclosão ≤ 3000 ok / > 4500 alto (o sensor satura em 5000 ppm).
  co2: { min: 0, max: 2000, margem: 1000, unidade: " ppm", dec: 0, folgaMin: 30 },
};

// Retorna 'ideal' | 'alerta' | 'critico' | null (sem dado ainda).
function corPorFaixa(valor, faixa) {
  if (typeof valor !== "number" || Number.isNaN(valor)) return null;
  if (valor >= faixa.min && valor <= faixa.max) return "ideal";
  const fora = valor < faixa.min ? faixa.min - valor : valor - faixa.max;
  return fora <= faixa.margem ? "alerta" : "critico";
}

// Aplica a classe value-ideal/alerta/critico num elemento (troca a classe
// anterior, preserva as outras classes que o elemento já tinha).
function aplicarCorFaixa(elementId, valor, faixaKey) {
  const el = document.getElementById(elementId);
  if (!el) return;
  el.classList.remove("value-ideal", "value-alerta", "value-critico");
  const estado = corPorFaixa(valor, FAIXAS[faixaKey]);
  if (estado) el.classList.add(`value-${estado}`);
}

// --- Controle de telas ---
const screens = {
  loading: document.getElementById("screen-loading"),
  login: document.getElementById("screen-login"),
  vincular: document.getElementById("screen-vincular"),
  dashboard: document.getElementById("screen-dashboard"),
  configuracoes: document.getElementById("screen-configuracoes"),
  guia: document.getElementById("screen-guia"), // Fase 37
  ciclo: document.getElementById("screen-ciclo"), // Fase 42
  cicloWizard: document.getElementById("screen-ciclo-wizard"), // Fase 42
  historico: document.getElementById("screen-historico"), // Fase 53
  convite: document.getElementById("screen-convite"), // Fase 88 — aceitar convite de compartilhamento
  tampa: document.getElementById("screen-tampa"), // Fase 98 — tela "Tampa" (sensor ADXL345) ao vivo
};

// Fase 55 — no celular a tela do ciclo é um cartão flutuante sobre o painel (fundo desfocado): o dashboard continua "ativo" por baixo.
const cicloFlutuante = () => window.matchMedia("(max-width: 640px)").matches;
function showScreen(name) {
  Object.entries(screens).forEach(([k, el]) => {
    if ((name === "ciclo" || name === "guia") && k === "dashboard" && cicloFlutuante()) return; // cartões flutuantes (Fase 55/56)
    el.classList.remove("active");
  });
  screens[name].classList.add("active");
}

// Estado simples em memória — guarda a chocadeira ativa do usuário logado
const state = {
  chocadeiraId: null,
  unsubscribeLeituras: null,
  unsubscribeAtuadores: null,
  unsubscribeArquivos: null, // Fase 41 — chocadeiras/{id}/arquivos/indice
  unsubscribeLogs: null, // Fase 21 — chocadeiras/{id}/logs (eventos sincronizados do SD do firmware)
  aplicandoSnapshotAtuadores: false, // evita loop: snapshot -> render -> "change" -> write
  bootRevealed: false, // true depois da 1ª troca de tela pós-boot (ver irParaTela)
  firmwareLastSeen: null, // Date do ultimo heartbeat do ESP32 (campo "firmwareLastSeen")
  estadoConexao: "app-offline", // "online" | "device-offline" | "app-offline" — ver atualizarStatusOnline()
  cicloIniciadoEm: null, // Date do inicio do ciclo atual (campo "cicloIniciadoEm"), null = nenhum ciclo ativo
  cicloPausadoEm: null, // Date de quando o ciclo foi pausado, null = rodando (ou sem ciclo)
  cicloNome: "", // Fase 42 — nome do lote
  cicloTipoNome: "", // Fase 42 — tipo de ovo do lote
  tipos: [], // Fase 42 — tipos de ovos (ver parseTiposOvos)
  cicloDuracaoDias: 21, // duração alvo do ciclo atual (campo "cicloDuracaoDias", ajustável no modal)
  feedbackTimer: null, // intervalo da digitação caractere-a-caractere em andamento (#action-feedback)
  feedbackHideTimer: null, // timeout que tira o cursor piscando depois do texto completo
  headerFeedbackTimer: null, // idem, mas pro aviso no topo do cabeçalho (#header-feedback)
  headerFeedbackHideTimer: null,
  pendente220v: null, // índice do canal 220V aguardando confirmação no modal, null = nenhum
  papel: "dono", // Fase 88 — "dono" | "convidado" (compartilhamento; ver share.js)
  perm: null, // Fase 88 — permissões do convidado (array: ver, ch0..ch7, ciclo, total); null = dono (tudo)
  conv: null, // Fase 88 — dados do convite aceito (dono, nome da chocadeira…)
};

// Heartbeat: o firmware grava "firmwareLastSeen" no documento da chocadeira a
// cada ~30s (ver FB_HEARTBEAT_INTERVAL_MS no main.cpp). Se esse campo estiver
// mais velho que isso, a chocadeira esta desligada/sem rede — mesmo que o
// navegador continue "online" (navigator.onLine so sabe sobre a REDE do PWA,
// nao sobre a placa).
// Subido de 90s -> 150s -> 200s -> 360s (2026-09-20/21). Log real de
// `pio device monitor` (2026-09-20) DESCARTOU jitter do firmware como causa:
// heartbeat confirmado a cada 30s sem falha nenhuma por 4+ minutos seguidos,
// WiFi/Firebase sempre "ok". A causa real é o listener onSnapshot do PWA
// ficando parado/atrasado (aba em segundo plano, notebook suspenso, etc.),
// não o firmware atrasando. reconferirStatusDoServidor() (mais abaixo) já
// ataca a causa raiz de verdade; este limiar é só a margem de segurança
// final antes de mostrar "offline" — 6min (12x o intervalo de heartbeat)
// dá folga generosa sem deixar de detectar uma queda genuína.
const HEARTBEAT_STALE_MS = 360000;

// Duração da animação de abertura (ovo/pintinho) em index.html + style.css.
// Garante que ela sempre termine de tocar, mesmo se o Firebase Auth resolver
// mais rápido que isso — sem isso a tela trocaria e cortaria a animação no meio.
const BOOT_ANIM_MS = 2300;
const bootStartTs = performance.now();

function irParaTela(nome) {
  if (state.bootRevealed) {
    showScreen(nome);
    return;
  }
  state.bootRevealed = true;
  const espera = Math.max(0, BOOT_ANIM_MS - (performance.now() - bootStartTs));
  setTimeout(() => showScreen(nome), espera);
}

// --- Estado de autenticação ---
// Decide qual tela mostrar: login → (sem chocadeira vinculada) vincular → dashboard
auth.onAuthStateChanged(async (user) => {
  if (!user) {
    state.chocadeiraId = null;
    state.papel = "dono";
    state.perm = null;
    state.conv = null;
    if (window.shareAoSair) window.shareAoSair();
    if (window.acAoSair) window.acAoSair();
    if (state.unsubscribeLeituras) {
      state.unsubscribeLeituras();
      state.unsubscribeLeituras = null;
    }
    if (state.unsubscribeAtuadores) {
      state.unsubscribeAtuadores();
      state.unsubscribeAtuadores = null;
    }
    if (state.unsubscribeLogs) {
      state.unsubscribeLogs();
      state.unsubscribeLogs = null;
    }
    if (state.unsubscribeArquivos) { // Fase 41
      state.unsubscribeArquivos();
      state.unsubscribeArquivos = null;
    }
    irParaTela("login");
    return;
  }

  document.getElementById("user-email").textContent = user.email ?? "";
  showScreen("loading");

  try {
    // Fase 88 — abriu um link de convite neste aparelho? mostra a tela "Aceitar convite" (ou o motivo de não poder)
    if (!window.shareProcessarConvitePendente && document.readyState !== "complete") await new Promise((r) => window.addEventListener("load", r, { once: true })); // share.js é o último script
    if (window.shareProcessarConvitePendente && (await window.shareProcessarConvitePendente(user))) return;
    const chocadeira = await buscarChocadeiraDoUsuario(user.uid);
    if (chocadeira) {
      entrarNaChocadeira(chocadeira, "dono", null);
    } else {
      // Fase 88 — sem chocadeira própria: procura um acesso compartilhado já aceito
      const g = window.shareBuscarAcessoConvidado ? await window.shareBuscarAcessoConvidado(user) : null;
      if (g) entrarNaChocadeira(g.chocadeira, "convidado", g.conv);
      else irParaTela("vincular");
    }
  } catch (err) {
    console.error("Erro ao buscar chocadeira do usuario:", err);
    // Se a consulta falhar (ex.: regra do Firestore), manda pra tela de
    // vínculo em vez de travar a tela de loading pra sempre.
    irParaTela("vincular");
  }
});

// Fase 88 — entrada única na chocadeira (dono ou convidado)
function entrarNaChocadeira(chocadeira, papel, conv) {
  state.chocadeiraId = chocadeira.id;
  state.papel = papel || "dono";
  state.conv = conv || null;
  state.perm = state.papel === "convidado" && conv ? (conv.permissoes || ["ver"]) : null;
  if (window.shareAoEntrar) window.shareAoEntrar(state.papel, state.conv, chocadeira);
  if (window.acAoEntrar) window.acAoEntrar(); // Fase 90 — registro de acesso / aviso de privacidade
  atualizarBannerChocadeira(chocadeira);
  escutarAtuadores(chocadeira.id);
  irParaTela("dashboard");
}

// Busca a primeira chocadeira cujo donoId é o uid do usuário logado
async function buscarChocadeiraDoUsuario(uid) {
  const snap = await db
    .collection("chocadeiras")
    .where("donoId", "==", uid)
    .limit(1)
    .get();

  if (snap.empty) return null;
  const doc = snap.docs[0];
  return { id: doc.id, ...doc.data() };
}

function atualizarBannerChocadeira(chocadeira) {
  const banner = document.getElementById("cycle-banner");
  if (banner) {
    banner.textContent = `${chocadeira.nome} — dados aparecerão aqui quando o firmware estiver conectado.`;
    banner.dataset.nomeChocadeira = chocadeira.nome;
  }
}

// pwa10 — escuta em tempo real a leitura mais recente da chocadeira ativa.
function escutarUltimaLeitura(chocadeiraId) {
  if (state.unsubscribeLeituras) {
    state.unsubscribeLeituras();
  }

  state.unsubscribeLeituras = db
    .collection("chocadeiras")
    .doc(chocadeiraId)
    .collection("leituras")
    .orderBy("timestamp", "desc")
    .limit(1)
    .onSnapshot(
      (snap) => {
        if (snap.empty) return;
        atualizarCardsComLeitura(snap.docs[0].data());
      },
      (err) => {
        console.error("Erro ao escutar leituras:", err);
      }
    );
}

function atualizarCardsComLeitura(leitura) {
  setValorCard("card-temp-value", leitura.temperatura, 1);
  setValorCard("card-umid-value", leitura.umidade, 0);
  setValorCard("card-co2-value", leitura.co2, 0);
  // Fase 21 — cor por faixa (verde/amarelo/vermelho) nos cards.
  aplicarCorFaixa("card-temp", leitura.temperatura, "temperatura");
  aplicarCorFaixa("card-umid", leitura.umidade, "umidade");
  aplicarCorFaixa("card-co2", leitura.co2, "co2");   // Fase 115
  if (typeof leitura.co2 === "number") { state.co2Atual = leitura.co2; if (typeof atualizarCardStatus === "function") atualizarCardStatus(); }   // Fase 115c: "CO₂ ALTO" entra na rotação do card Status

  const banner = document.getElementById("cycle-banner");
  if (banner && leitura.timestamp) {
    const hora = leitura.timestamp.toDate().toLocaleTimeString("pt-BR", {
      hour: "2-digit",
      minute: "2-digit",
    });
    const nome = banner.dataset.nomeChocadeira ?? "Chocadeira";
    banner.textContent = `${nome} — última leitura às ${hora}`;
  }
}

function setValorCard(elementId, valor, decimais) {
  const el = document.getElementById(elementId);
  if (!el) return;
  el.textContent = typeof valor === "number" ? valor.toFixed(decimais) : "--";
}

// ═══════════════════════════════════════════════════════════════
// ATUADORES — controle remoto dos 8 canais do relé
// ═══════════════════════════════════════════════════════════════

// Monta os 8 controles uma única vez (o estado deles é atualizado
// depois via escutarAtuadores, não recriando o DOM a cada leitura)
function montarGridAtuadores() {
  const grid = document.getElementById("atuadores-grid");
  if (!grid || grid.dataset.montado) return; // evita duplicar se chamado 2x
  grid.dataset.montado = "1";

  RELAY_NAMES.forEach((nome, i) => {
    const item = document.createElement("div");
    item.className = "atuador-item";

    const info = document.createElement("div");
    info.className = "atuador-info";

    const nomeEl = document.createElement("div");
    nomeEl.className = "atuador-nome";
    nomeEl.textContent = nome;

    const tensaoEl = document.createElement("div");
    tensaoEl.className = `atuador-tensao ${classeTensao(RELAY_VOLTAGE[i])}`;
    tensaoEl.textContent = RELAY_VOLTAGE[i];

    info.appendChild(nomeEl);
    info.appendChild(tensaoEl);

    // Fase 36 — Cooler Sistema é automático (histerese de temperatura,
    // firmware). Selo "AUTO" deixa claro à primeira vista que aquele
    // toggle não é pra mexer, sem precisar tocar pra descobrir.
    if (i === COOLER_CH_INDEX) {
      const autoEl = document.createElement("div");
      autoEl.className = "atuador-auto-badge";
      autoEl.textContent = "AUTO";
      info.appendChild(autoEl);
    }
    if (i === BOMBA_CH_INDEX) {
      const bEl = document.createElement("div");
      bEl.className = "atuador-auto-badge";
      bEl.textContent = "SÓ NO APARELHO";
      info.appendChild(bEl);
    }

    const label = document.createElement("label");
    label.className = "toggle";

    const input = document.createElement("input");
    input.type = "checkbox";
    input.id = `atuador-ch${i}`;
    if (i === COOLER_CH_INDEX) {
      // Bloqueado de propósito -- toggle manual seria sobrescrito pelo
      // firmware no próximo ciclo de histerese (5s) de qualquer forma.
      input.disabled = true;
      label.title = "Controlado automaticamente por histerese de temperatura (Cooler Sistema)";
    }
    if (i === BOMBA_CH_INDEX) {
      input.disabled = true;
      label.title = "Bomba de água (220 V): só liga pelo toque no aparelho (limite de 8 s) ou pela lógica de água do firmware";
    }
    input.addEventListener("change", () => {
      if (state.aplicandoSnapshotAtuadores) return; // veio do Firestore, não do usuário

      // Ligar (não desligar — isso é sempre seguro) um canal de 220V AC
      // remotamente merece confirmação: quem está mexendo pode estar em
      // qualquer lugar do mundo, sem ninguém fisicamente presente pra
      // notar se algo der errado.
      if (input.checked && RELAY_VOLTAGE[i].includes("220")) {
        input.checked = false; // reverte visualmente até confirmar
        abrirModalConfirmacao220v(i);
        return;
      }

      alternarAtuador(i, input.checked);
    });

    const slider = document.createElement("span");
    slider.className = "slider";

    label.appendChild(input);
    label.appendChild(slider);

    item.appendChild(info);
    item.appendChild(label);
    grid.appendChild(item);
  });
}

// Escuta o documento da chocadeira (não a subcoleção de leituras) para
// refletir o campo "atuadores" — atualizado pelo firmware, por este
// mesmo navegador, ou por outra aba/dispositivo do dono.
function escutarAtuadores(chocadeiraId) {
  montarGridAtuadores();
  vivoPing();

  if (state.unsubscribeAtuadores) {
    state.unsubscribeAtuadores();
  }

  state.unsubscribeAtuadores = db
    .collection("chocadeiras")
    .doc(chocadeiraId)
    .onSnapshot(
      (snap) => {
        const dados = snap.data();
        const atuadores = (dados && dados.atuadores) || {};

        state.aplicandoSnapshotAtuadores = true;
        RELAY_NAMES.forEach((_, i) => {
          const ligado = !!atuadores[`ch${i}`];
          const input = document.getElementById(`atuador-ch${i}`);
          if (input) input.checked = ligado;
          atualizarBordaAtuador(i, ligado);
        });
        state.aplicandoSnapshotAtuadores = false;

        state.firmwareLastSeen = dados && dados.firmwareLastSeen ? dados.firmwareLastSeen.toDate() : null;
        atualizarStatusOnline();
        // Fase 63: leitura ao vivo vem no próprio documento (o firmware a grava junto do heartbeat) — sem listener extra em leituras/
        if (dados && typeof dados.tempAtual === "number") atualizarCardsComLeitura({ temperatura: dados.tempAtual, umidade: dados.umidAtual, co2: typeof dados.co2Atual === "number" ? dados.co2Atual : undefined, timestamp: dados.firmwareLastSeen }); // Fase 99: co2Atual (MH-Z19B) quando o sensor existir

        { const ambNovo = (dados && dados.ambiente) || "";
          if (ambNovo !== state.ambiente || !state.ambRecebidoEm) state.ambRecebidoEm = Date.now();   // Fase 96: a base da interpolação só muda quando o aparelho publica um valor novo (outras escritas no documento não a reiniciam)
          state.ambiente = ambNovo; }
        state.tempAtual = dados && typeof dados.tempAtual === "number" ? dados.tempAtual : null;
        state.umidAtual = dados && typeof dados.umidAtual === "number" ? dados.umidAtual : null;
        state.cicloIniciadoEm = dados && dados.cicloIniciadoEm ? dados.cicloIniciadoEm.toDate() : null;
        state.cicloPausadoEm = dados && dados.cicloPausadoEm ? dados.cicloPausadoEm.toDate() : null;
        state.cicloDuracaoDias = (dados && dados.cicloDuracaoDias) || INCUBACAO_DIAS_PADRAO;
        state.cicloNome = (dados && dados.cicloNome) || "";
        state.cicloTipoNome = (dados && dados.cicloTipoNome) || "";
        { const mEx = /^(\d+);(\d+)/.exec((dados && dados.cicloExtra) || ""); // Fase 77: "preparo;litros"
          state.cicloPreparo = !!(state.cicloIniciadoEm && mEx && mEx[1] === "1");
          state.cicloVolL = mEx ? parseInt(mEx[2], 10) || 0 : 0; }
        state.tipos = parseTiposOvos(dados && dados.tiposOvos);
        renderTipos();
        aplicarFaixasDoTipo();
        atualizarDiaCiclo();
        atualizarCardStatus();
        if (typeof onChocadeiraDoc === "function") onChocadeiraDoc(dados); // extras.js (Fases 44-50)
        if (typeof window.tampaDoDoc === "function") window.tampaDoDoc(dados); // tampa.js (Fase 98): telemetria ao vivo do sensor
        if (typeof window.painelDoDoc === "function") window.painelDoDoc(dados); // painel.js (Fase 104): ajustes de Tela e Fans
      },
      (err) => {
        console.error("Erro ao escutar atuadores:", err);
      }
    );
}

// Acende/apaga a borda neon do card do canal, refletindo o estado real vindo
// do Firestore (local ou remoto — este é o único ponto que atualiza a borda).
// A direção (ligando/desligando) decide a duração da transição em CSS.
function atualizarBordaAtuador(indice, ligado) {
  const item = document.getElementById(`atuador-ch${indice}`)?.closest(".atuador-item");
  if (!item) return;
  item.classList.toggle("atuador-desligando", !ligado);
  item.style.setProperty("--border-fill", ligado ? 100 : 0);
}

// ── Confirmação obrigatória pra LIGAR um canal de 220V AC remotamente ──
// (ver comentário no listener de "change" em montarGridAtuadores). Só
// intercepta o "ligar"; desligar nunca precisa de confirmação.
function abrirModalConfirmacao220v(indice) {
  state.pendente220v = indice;
  document.getElementById("confirmar-220v-nome").textContent = RELAY_NAMES[indice];
  document.getElementById("modal-220v-backdrop").hidden = false;
}

function fecharModalConfirmacao220v() {
  document.getElementById("modal-220v-backdrop").hidden = true;
  state.pendente220v = null;
}

document.getElementById("modal-220v-close").addEventListener("click", fecharModalConfirmacao220v);
document.getElementById("btn-cancelar-220v").addEventListener("click", fecharModalConfirmacao220v);
document.getElementById("modal-220v-backdrop").addEventListener("click", (e) => {
  if (e.target.id === "modal-220v-backdrop") fecharModalConfirmacao220v();
});

document.getElementById("btn-confirmar-220v").addEventListener("click", () => {
  const indice = state.pendente220v;
  fecharModalConfirmacao220v();
  if (indice === null) return;
  const input = document.getElementById(`atuador-ch${indice}`);
  if (input) input.checked = true;
  alternarAtuador(indice, true);
});

// ── Desligar (deep sleep remoto, Fase 15) ── Ação mais séria que existe:
// deixa o dispositivo totalmente inacessível até alguém apertar um botão
// físico na placa — por isso confirmação obrigatória, igual ao 220V.
function fecharModalDesligar() {
  document.getElementById("modal-desligar-backdrop").hidden = true;
}

document.getElementById("btn-desligar").addEventListener("click", () => {
  // Consulta direta (não usa as constantes btnMenu/menuDropdown — só são
  // declaradas mais abaixo no arquivo) pra fechar o dropdown antes do modal.
  document.getElementById("menu-dropdown").classList.remove("open");
  document.getElementById("btn-menu").setAttribute("aria-expanded", "false");
  document.getElementById("modal-desligar-backdrop").hidden = false;
});
document.getElementById("modal-desligar-close").addEventListener("click", fecharModalDesligar);
document.getElementById("btn-cancelar-desligar").addEventListener("click", fecharModalDesligar);
document.getElementById("modal-desligar-backdrop").addEventListener("click", (e) => {
  if (e.target.id === "modal-desligar-backdrop") fecharModalDesligar();
});

document.getElementById("btn-confirmar-desligar").addEventListener("click", async () => {
  fecharModalDesligar();
  if (!state.chocadeiraId) return;
  try {
    await db.collection("chocadeiras").doc(state.chocadeiraId).update({ comandoDesligar: true });
    registrarAcao("Comando", "Enviou o comando Desligar (deep sleep)");
    mostrarFeedbackHeader("Comando de desligar enviado — aguardando o dispositivo aplicar", "warn");
  } catch (err) {
    console.error("Erro ao enviar comando de desligar:", err);
    mostrarFeedbackHeader("Falha ao enviar comando de desligar", "error");
  }
});

// ── Reiniciar (software reset remoto, Fase 28) ── Mesmo modal do Desligar
// acima, mas escreve comandoReiniciar em vez de comandoDesligar — o
// firmware aplica ESP.restart() e volta sozinho em poucos segundos, sem
// precisar de nenhum botão físico (ao contrário do Desligar).
document.getElementById("btn-confirmar-reiniciar").addEventListener("click", async () => {
  fecharModalDesligar();
  if (!state.chocadeiraId) return;
  try {
    await db.collection("chocadeiras").doc(state.chocadeiraId).update({ comandoReiniciar: true });
    registrarAcao("Comando", "Enviou o comando Reiniciar");
    mostrarFeedbackHeader("Comando de reiniciar enviado — aguardando o dispositivo aplicar", "warn");
  } catch (err) {
    console.error("Erro ao enviar comando de reiniciar:", err);
    mostrarFeedbackHeader("Falha ao enviar comando de reiniciar", "error");
  }
});

// ── Forçar atualização (Fase 15) ── Escape hatch manual pro cache do
// service worker — hard refresh pelo atalho de teclado é trabalhoso no
// mobile. Desde a Fase 12 o SW já é network-first, então isso raramente
// deveria ser necessário, mas continua valendo como saída direta.
document.getElementById("btn-forcar-atualizacao").addEventListener("click", async () => {
  const btn = document.getElementById("btn-forcar-atualizacao");
  btn.disabled = true;
  btn.classList.add("girando"); btn.setAttribute("aria-label", "Atualizando...");
  try {
    if ("caches" in window) {
      const chaves = await caches.keys();
      await Promise.all(chaves.map((k) => caches.delete(k)));
    }
    if ("serviceWorker" in navigator) {
      const registros = await navigator.serviceWorker.getRegistrations();
      await Promise.all(registros.map((r) => r.unregister()));
    }
  } catch (err) {
    console.error("Erro ao limpar cache/service worker:", err);
  } finally {
    location.reload();
  }
});

// Fase 89 — auditoria: toda ação feita pelo APP (dono ou convidado) vira uma linha em logs/ com o e-mail de quem fez
// (as regras do Firestore garantem que o "ator" é o e-mail verificado da própria conta e que o convidado só registra o que pode fazer).
function registrarAcao(categoria, mensagem) {
  const u = auth.currentUser;
  if (!u || !state.chocadeiraId) return;
  db.collection("chocadeiras").doc(state.chocadeiraId).collection("logs").add({
    categoria, mensagem: String(mensagem).slice(0, 240), ator: u.email || "", papel: state.papel === "convidado" ? "convidado" : "dono",
    origem: "app", timestamp: firebase.firestore.FieldValue.serverTimestamp(),
  }).catch((e) => console.warn("registrarAcao:", e && e.code));
}
window.registrarAcao = registrarAcao;

// Escreve o novo estado de UM canal via update com dot-notation —
// não sobrescreve os outros 7 canais nem qualquer outro campo do doc.
// Feedback: pending assim que o toque acontece, success/error quando o
// Firestore confirma (ou falha) a escrita — .update() só resolve depois
// do servidor confirmar, então o "pending" é real, não decorativo.
function alternarAtuador(indice, novoEstado) {
  if (!state.chocadeiraId) return;
  const nome = RELAY_NAMES[indice];
  const acao = novoEstado ? "Ligando" : "Desligando";
  // Bug corrigido: a escrita no Firestore tem sucesso mesmo com a chocadeira
  // offline (o Firestore não sabe/não liga se o ESP32 está ouvindo — só o
  // ESP32 não vai VER o comando até reconectar). Antes disso, o "sucesso" da
  // escrita fazia o feedback mentir "ligado/desligado" como se tivesse
  // acontecido de verdade. Guarda o estado no momento do clique pra decidir
  // a mensagem certa quando a escrita terminar.
  const dispositivoOffline = state.estadoConexao !== "online";
  mostrarFeedbackAcao(`${acao} ${nome}...`, "pending");

  // Aviso de contexto no topo do cabeçalho — explica o "por quê" (some sozinho).
  if (state.estadoConexao === "device-offline") {
    mostrarFeedbackHeader(`Chocadeira offline — comando será aplicado ao reconectar`, "warn");
  } else if (state.estadoConexao === "app-offline") {
    mostrarFeedbackHeader(`Sem conexão — a ação será enviada quando a internet voltar`, "warn");
  }

  db.collection("chocadeiras")
    .doc(state.chocadeiraId)
    .update({ [`atuadores.ch${indice}`]: novoEstado, "cmd.vivo": String(Date.now()) })
    .then(() => {
      registrarAcao("Atuador", `${novoEstado ? "Ligou" : "Desligou"} ${nome}${dispositivoOffline ? " (aparelho offline: aplica ao reconectar)" : ""}`);
      if (dispositivoOffline) {
        // A escrita no Firestore foi bem-sucedida (o comando fica salvo pra
        // quando a chocadeira reconectar), mas NÃO afirma "ligado/desligado"
        // — isso seria mentira, o relé físico não mudou de estado agora.
        mostrarFeedbackAcao(`${nome}: não foi possível se conectar`, "error");
      } else {
        mostrarFeedbackAcao(`${nome} ${novoEstado ? "ligado" : "desligado"}`, "success");
      }
    })
    .catch((err) => {
      console.error("Erro ao atualizar atuador:", err);
      mostrarFeedbackAcao(`${nome}: não foi possível se conectar`, "error");
      // Reverte visualmente só quando a escrita em si falhou (aí sim o
      // estado desejado não foi salvo em lugar nenhum, local ou nuvem)
      const input = document.getElementById(`atuador-ch${indice}`);
      if (input) input.checked = !novoEstado;
    });
}

// Digita o texto caractere-a-caractere no span de feedback (ao lado do
// "última leitura..."). Cada chamada cancela a digitação/timers anteriores,
// então uma ação nova sempre substitui a anterior, mesmo no meio do efeito.
function mostrarFeedbackAcao(texto, tipo) {
  const el = document.getElementById("action-feedback");
  if (!el) return;

  if (state.feedbackTimer) clearInterval(state.feedbackTimer);
  if (state.feedbackHideTimer) clearTimeout(state.feedbackHideTimer);

  el.className = `action-feedback typing ${tipo}`;
  el.textContent = "";

  let i = 0;
  state.feedbackTimer = setInterval(() => {
    i++;
    el.textContent = texto.slice(0, i);
    if (i >= texto.length) {
      clearInterval(state.feedbackTimer);
      state.feedbackTimer = null;
      state.feedbackHideTimer = setTimeout(() => el.classList.remove("typing"), 1500);
    }
  }, 28);
}

// Mesma digitação caractere-a-caractere, mas no aviso do topo do cabeçalho
// (#header-feedback) — usado só pro aviso "chocadeira offline"/"sem conexão"
// ao tentar uma ação (ver alternarAtuador). Some sozinho depois de um tempo,
// diferente do #action-feedback (que fica com o texto final até a próxima ação).
function mostrarFeedbackHeader(texto, tipo) {
  const el = document.getElementById("header-feedback");
  if (!el) return;

  if (state.headerFeedbackTimer) clearInterval(state.headerFeedbackTimer);
  if (state.headerFeedbackHideTimer) clearTimeout(state.headerFeedbackHideTimer);

  el.className = `action-feedback typing ${tipo}`;
  el.textContent = "";

  let i = 0;
  state.headerFeedbackTimer = setInterval(() => {
    i++;
    el.textContent = texto.slice(0, i);
    if (i >= texto.length) {
      clearInterval(state.headerFeedbackTimer);
      state.headerFeedbackTimer = null;
      state.headerFeedbackHideTimer = setTimeout(() => {
        el.classList.remove("typing");
        el.textContent = "";
      }, 4000);
    }
  }, 28);
}

// ═══════════════════════════════════════════════════════════════
// CICLO DE INCUBAÇÃO (Fase 42) — wizard de início (4 etapas), tela cheia do
// ciclo ativo (Pausar / Parar / Lixeira) e tipos de ovos (CRUD).
// Campos do doc da chocadeira: cicloIniciadoEm (início escolhido), cicloPausadoEm,
// cicloDuracaoDias (vem do tipo), cicloNome, cicloTipoNome, tiposOvos.
// "Parar" só habilita em "Último dia começa às" = início + (dias-1) dias.
// A duração não é mais editável no ciclo (será substituída no futuro).
// ═══════════════════════════════════════════════════════════════

const INCUBACAO_DIAS_PADRAO = 21; // padrão pra ovo de galinha

// Tipos de ovos — mesmo formato compacto do firmware:
// "nome;dias;temp;umid;umidEclosao;viragemAte|nome;..." (string em "tiposOvos").
const TIPOS_PADRAO = [
  { nome: "Galinha", dias: 21, temp: 37.5, tempMin: 37.2, tempMax: 37.8, umid: 50, umidMin: 45, umidMax: 55, umidEcl: 70, viraAte: 17, viraPorDia: 4, viraHH: 8, viraMM: 0, ovo1: 7, ovo2: 14, eclDia: 18, tempEcl: 37.5 },
  { nome: "Codorna", dias: 17, temp: 37.7, tempMin: 37.5, tempMax: 38.0, umid: 50, umidMin: 45, umidMax: 55, umidEcl: 70, viraAte: 14, viraPorDia: 4, viraHH: 8, viraMM: 0, ovo1: 5, ovo2: 10, eclDia: 14, tempEcl: 37.7 },
];
// Valores originais (restaurar): só existem pros tipos de fábrica.
function tipoDeFabrica(nome) {
  const t = TIPOS_PADRAO.find((x) => x.nome.toLowerCase() === String(nome).toLowerCase());
  return t ? { ...t } : null;
}
const TIPOS_MAX = 8;

function limparNomePeloFormato(s) {
  return String(s || "").replace(/[;|"\\]/g, "-").trim();
}
// Formato: nome;dias;temp;umid;umidEcl;viraAte;tempMin;tempMax;umidMin;umidMax;viraPorDia;viraHH;viraMM;ovo1;ovo2;eclDia;tempEcl
// (registros antigos de 6 campos continuam válidos: o resto é derivado)
function parseTiposOvos(str) {
  if (!str) return TIPOS_PADRAO.map((t) => ({ ...t }));
  const lista = [];
  String(str).split("|").forEach((reg) => {
    const p = reg.split(";");
    if (p.length < 6) return;
    const n = (i, f = parseInt) => f(p[i], 10);
    const t = { nome: p[0].trim(), dias: n(1), temp: parseFloat(p[2]), umid: n(3), umidEcl: n(4), viraAte: n(5) };
    if (p.length >= 13) {
      Object.assign(t, { tempMin: parseFloat(p[6]), tempMax: parseFloat(p[7]), umidMin: n(8), umidMax: n(9), viraPorDia: n(10), viraHH: n(11), viraMM: n(12) });
    } else {
      Object.assign(t, { tempMin: +(t.temp - 0.3).toFixed(1), tempMax: +(t.temp + 0.3).toFixed(1), umidMin: Math.max(0, t.umid - 5), umidMax: Math.min(100, t.umid + 5), viraPorDia: 4, viraHH: 8, viraMM: 0 });
    }
    if (p.length >= 16 && Number.isFinite(n(13)) && Number.isFinite(n(14)) && Number.isFinite(n(15))) Object.assign(t, { ovo1: n(13), ovo2: n(14), eclDia: n(15) });
    else Object.assign(t, { ovo1: Math.round(t.dias / 3), ovo2: Math.round((t.dias * 2) / 3), eclDia: t.dias > 3 ? t.dias - 3 : t.dias }); // registro antigo: marcos derivados
    t.tempEcl = p.length >= 17 && Number.isFinite(parseFloat(p[16])) && parseFloat(p[16]) > 0 ? parseFloat(p[16]) : t.temp; // Fase 69: temperatura ideal na eclosão
    if (t.nome && t.dias >= 1 && t.dias <= 60 && !Number.isNaN(t.temp)) lista.push(t);
  });
  return lista.length ? lista.slice(0, TIPOS_MAX) : TIPOS_PADRAO.map((t) => ({ ...t }));
}
function serializarTiposOvos(lista) {
  return lista.map((t) => [
    limparNomePeloFormato(t.nome), t.dias, Number(t.temp).toFixed(1), t.umid, t.umidEcl, t.viraAte,
    Number(t.tempMin).toFixed(1), Number(t.tempMax).toFixed(1), t.umidMin, t.umidMax, t.viraPorDia, t.viraHH, t.viraMM, t.ovo1 || 0, t.ovo2 || 0, t.eclDia || 0,
    Number(t.tempEcl > 0 ? t.tempEcl : t.temp).toFixed(1),
  ].join(";")).join("|");
}
// Horários das viragens: N por dia, em intervalos iguais a partir da 1ª (HH:MM).
function horariosViragem(t) {
  const inter = 1440 / t.viraPorDia, ini = t.viraHH * 60 + t.viraMM, p = (n) => String(n).padStart(2, "0");
  return Array.from({ length: t.viraPorDia }, (_, i) => { const m = Math.round(ini + i * inter) % 1440; return `${p(Math.floor(m / 60))}:${p(m % 60)}`; });
}
// A faixa ideal de temperatura/umidade (cores dos cards, gráfico, alertas) vem do TIPO do ciclo ativo.
function aplicarFaixasDoTipo() {
  // Fase 79: sem ciclo mas "mantendo o ambiente", a faixa é a do ÚLTIMO tipo (o aparelho informa o nome no campo ambiente)
  const amb = String(state.ambiente || "").split(";");
  const nomeT = state.cicloIniciadoEm ? state.cicloTipoNome : (amb[0] === "manter" ? amb[2] : "");
  const t = nomeT ? state.tipos.find((x) => x.nome.toLowerCase() === String(nomeT).toLowerCase()) : null;
  FAIXAS.temperatura.min = t ? t.tempMin : 37.2;
  FAIXAS.temperatura.max = t ? t.tempMax : 38.9;
  FAIXAS.umidade.min = t ? t.umidMin : 45;
  FAIXAS.umidade.max = t ? t.umidMax : 65;
  const f = (n) => String(n).replace(".", ",");
  // Fase 115: faixa de CO₂ por fase (igual ao firmware: a partir de eclDia−2 a faixa sobe)
  { const preEcl = !!(t && t.eclDia > 0 && (calcularDiaAtual() || 0) >= t.eclDia - 2);
    FAIXAS.co2.max = preEcl ? 3000 : 2000; FAIXAS.co2.margem = preEcl ? 1500 : 1000; }
  // Fase 54: na eclosão (a partir do dia eclDia) a faixa de umidade gira em torno da umidade de eclosão — igual ao firmware
  if (t && t.eclDia > 0 && t.umidEcl > 0 && (calcularDiaAtual() || 0) >= t.eclDia) {
    FAIXAS.umidade.min = t.umidEcl - 5;
    FAIXAS.umidade.max = Math.min(100, t.umidEcl + 10);
  }
  // Fase 69: na eclosão a faixa de temperatura se desloca junto com a temperatura de eclosão (igual ao firmware)
  if (t && t.eclDia > 0 && t.tempEcl > 0 && (calcularDiaAtual() || 0) >= t.eclDia) {
    const d = t.tempEcl - t.temp;
    FAIXAS.temperatura.min = +(FAIXAS.temperatura.min + d).toFixed(1);
    FAIXAS.temperatura.max = +(FAIXAS.temperatura.max + d).toFixed(1);
  }
  const et = document.getElementById("alerta-temp-faixa"), eu = document.getElementById("alerta-umid-faixa");
  if (et) et.textContent = t ? `${f(FAIXAS.temperatura.min)}–${f(FAIXAS.temperatura.max)}°C` : "sem ciclo ativo";
  if (eu) eu.textContent = t ? `${FAIXAS.umidade.min}–${FAIXAS.umidade.max}%` : "sem ciclo ativo";
}
async function salvarTiposOvos(lista) {
  if (!state.chocadeiraId) return;
  await db.collection("chocadeiras").doc(state.chocadeiraId).update({ tiposOvos: serializarTiposOvos(lista) });
  registrarAcao("Comando", "Alterou os tipos de ovos");
}

// ─── Cálculos do ciclo ───
function cicloTotalDias() { return state.cicloDuracaoDias || INCUBACAO_DIAS_PADRAO; }

// Dia atual (1..duração), congelado na hora da pausa se estiver pausado. null = sem ciclo.
function calcularDiaAtual() {
  if (!state.cicloIniciadoEm) return null;
  if (state.cicloPreparo) return 1; // Fase 77: ainda sem ovos — a contagem começa quando o usuário confirma
  const referencia = state.cicloPausadoEm ? state.cicloPausadoEm.getTime() : Date.now();
  const dias = Math.floor((referencia - state.cicloIniciadoEm.getTime()) / 86400000) + 1;
  return Math.min(Math.max(dias, 1), cicloTotalDias());
}
// Quando começa o ÚLTIMO dia. Pausado: o fim anda junto com a pausa (Retomar desloca o início).
function cicloUltimoDiaMs() {
  if (!state.cicloIniciadoEm) return null;
  const extra = state.cicloPausadoEm ? Date.now() - state.cicloPausadoEm.getTime() : 0;
  return state.cicloIniciadoEm.getTime() + extra + (cicloTotalDias() - 1) * 86400000;
}
function cicloPodeParar() {
  const fim = cicloUltimoDiaMs();
  return fim !== null && Date.now() >= fim;
}
function cicloProgressoPct() {
  if (!state.cicloIniciadoEm) return 0;
  const ref = state.cicloPausadoEm ? state.cicloPausadoEm.getTime() : Date.now();
  const frac = (ref - state.cicloIniciadoEm.getTime()) / (cicloTotalDias() * 86400000);
  return Math.min(100, Math.max(0, Math.round(frac * 100)));
}
function fmtDataHora(ms) {
  return new Date(ms).toLocaleString("pt-BR", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" });
}
function fmtFalta(ms) {
  const min = Math.max(0, Math.floor(ms / 60000));
  const d = Math.floor(min / 1440), h = Math.floor((min % 1440) / 60), m = min % 60;
  return `${d}d ${h}h ${m}min`;
}

function atualizarDiaCiclo() {
  aplicarFaixasDoTipo(); // Fase 54: a fase (eclosão) muda com o dia
  const dia = calcularDiaAtual();
  const el = document.getElementById("card-dia-value");
  if (el) el.textContent = dia === null ? "--" : String(dia);

  const cicloEl = document.getElementById("card-ciclo-value");
  if (cicloEl) {
    if (dia === null) {
      cicloEl.textContent = "--";
      cicloEl.classList.remove("value-ok", "value-warn");
    } else if (state.cicloPreparo) {
      cicloEl.textContent = "Preparo";
      cicloEl.classList.add("value-warn"); cicloEl.classList.remove("value-ok");
    } else {
      cicloEl.textContent = `${dia}/${cicloTotalDias()}`;
      cicloEl.classList.toggle("value-warn", !!state.cicloPausadoEm);
      cicloEl.classList.toggle("value-ok", !state.cicloPausadoEm);
    }
  }
  atualizarBotaoCiclo();
  if (screens.ciclo.classList.contains("active")) renderTelaCiclo();
}
setInterval(atualizarDiaCiclo, 60000); // mantém o dia e a contagem do "Parar" certos sem recarregar

function atualizarBotaoCiclo() {
  const btn = document.getElementById("btn-iniciar-ciclo");
  if (!btn) return;
  if (!state.cicloIniciadoEm) {
    btn.classList.remove("ciclo-ativo", "ciclo-pausado");
    btn.innerHTML = `<span class="btn-iniciar-icon">🥚</span> Iniciar Chocadeira`;
    return;
  }
  const dia = calcularDiaAtual();
  const pausado = !!state.cicloPausadoEm;
  if (state.cicloPreparo) { btn.classList.remove("ciclo-ativo"); btn.classList.add("ciclo-pausado"); btn.innerHTML = `<span class="btn-iniciar-icon">🌡️</span> Preparando…`; return; }
  btn.classList.toggle("ciclo-ativo", !pausado);
  btn.classList.toggle("ciclo-pausado", pausado);
  btn.innerHTML = pausado
    ? `<span class="btn-iniciar-icon">⏸</span> Pausado — Dia ${dia}`
    : `<span class="btn-iniciar-icon">🐣</span> Dia ${dia}/${cicloTotalDias()}`;
}

// Sem ciclo: abre o wizard. Com ciclo (rodando ou pausado): abre a tela cheia do ciclo.
// Gatilhos: botão do cabeçalho e card "Ciclo" (mesma ideia do card "Ciclo" do firmware).
function manipularCiclo() {
  if (!state.chocadeiraId) return;
  if (!state.cicloIniciadoEm) abrirWizardCiclo();
  else { renderTelaCiclo(); showScreen("ciclo"); }
}
document.getElementById("btn-iniciar-ciclo").addEventListener("click", manipularCiclo);
const cardCiclo = document.getElementById("card-ciclo");
cardCiclo.addEventListener("click", manipularCiclo);
cardCiclo.addEventListener("keydown", (e) => {
  if (e.key === "Enter" || e.key === " ") { e.preventDefault(); manipularCiclo(); }
});

// ─── Tela cheia do ciclo ativo ───
const cicloEls = {
  nome: document.getElementById("ciclo-nome"),
  tipo: document.getElementById("ciclo-tipo-linha"),
  estado: document.getElementById("ciclo-estado"),
  diaNum: document.getElementById("ciclo-dia-num"),
  diaTot: document.getElementById("ciclo-dia-tot"),
  pct: document.getElementById("ciclo-pct"),
  barra: document.getElementById("ciclo-barra-fill"),
  inicio: document.getElementById("ciclo-info-inicio"),
  fim: document.getElementById("ciclo-info-fim"),
  dica: document.getElementById("ciclo-parar-dica"),
  btnPausar: document.getElementById("btn-ciclo-pausar"),
  btnParar: document.getElementById("btn-ciclo-parar"),
};
// Fase 54 — marcos (ovoscopias e eclosão) na barra de progresso; a cor do número/percentual/barra muda ao passar de cada marco.
function renderMarcosCiclo(pausado) {
  const tela = document.getElementById("screen-ciclo"), cont = document.getElementById("ciclo-marcos"), leg = document.getElementById("ciclo-marcos-leg");
  const t = state.tipos.find((x) => x.nome.toLowerCase() === String(state.cicloTipoNome).toLowerCase());
  const total = cicloTotalDias(), dia = calcularDiaAtual() || 1;
  const marcos = [];
  if (t) {
    if (t.ovo1 > 0 && t.ovo1 <= total) marcos.push({ dia: t.ovo1, tipo: "ovo", rot: t.ovo2 ? "1ª ovoscopia" : "Ovoscopia" });
    if (t.ovo2 > 0 && t.ovo2 <= total && t.ovo2 !== t.ovo1) marcos.push({ dia: t.ovo2, tipo: "ovo2", rot: "2ª ovoscopia" });
    if (t.eclDia > 0 && t.eclDia <= total) marcos.push({ dia: t.eclDia, tipo: "ecl", rot: "Eclosão" });
  }
  const estagio = !t ? 0 : t.eclDia && dia >= t.eclDia ? 3 : t.ovo2 && dia >= t.ovo2 ? 2 : t.ovo1 && dia >= t.ovo1 ? 1 : 0;
  tela.dataset.estagio = pausado ? "pausado" : String(estagio);
  cont.textContent = ""; leg.textContent = "";
  const proximo = marcos.findIndex((m) => dia < m.dia);
  marcos.forEach((m, i) => {
    const data = new Date(state.cicloIniciadoEm.getTime() + (m.dia - 1) * 86400000 + (state.cicloPausadoEm ? Date.now() - state.cicloPausadoEm.getTime() : 0));
    const dd = data.toLocaleDateString("pt-BR", { day: "2-digit", month: "2-digit" });
    const passou = dia >= m.dia;
    const el = document.createElement("span");
    el.className = "ciclo-marco" + (passou ? " passou" : "") + (i === proximo ? " proximo" : "");
    el.dataset.tipo = m.tipo;
    el.style.left = `${((m.dia - 1) / total) * 100}%`;
    el.title = `${m.rot} — dia ${m.dia} (${dd})`;
    el.appendChild(document.createElement("i"));
    cont.appendChild(el);
    const chip = document.createElement("span");
    chip.className = "ciclo-leg-item" + (passou ? " passou" : "") + (i === proximo ? " proximo" : "");
    chip.dataset.tipo = m.tipo;
    chip.textContent = `${passou ? "✓ " : ""}${m.rot} · dia ${m.dia} · ${dd}`;
    leg.appendChild(chip);
  });
}
function renderTelaCiclo() {
  if (!state.cicloIniciadoEm) { showScreen("dashboard"); return; }
  const telaEl = document.getElementById("screen-ciclo");
  telaEl.classList.toggle("em-preparo", !!state.cicloPreparo);
  const prepEl = document.getElementById("ciclo-preparo");
  prepEl.hidden = !state.cicloPreparo;
  document.querySelector("#btn-ciclo-lixeira .ciclo-acao-rotulo").textContent = state.cicloPreparo ? "Cancelar" : "Lixeira";
  if (state.cicloPreparo) {
    cicloEls.nome.textContent = state.cicloNome || "Ciclo de incubação";
    cicloEls.tipo.textContent = `${state.cicloTipoNome || "Tipo não informado"} · ${cicloTotalDias()} dias${state.cicloVolL ? ` · caixa ${state.cicloVolL} L` : ""}`;
    cicloEls.estado.textContent = "Preparo"; cicloEls.estado.classList.add("pausado");
    renderPreparoBloco(prepEl);
    return;
  }
  const pausado = !!state.cicloPausadoEm;
  const total = cicloTotalDias();
  cicloEls.nome.textContent = state.cicloNome || "Ciclo de incubação";
  cicloEls.tipo.textContent = `${state.cicloTipoNome || "Tipo não informado"} · ${total} dias`;
  cicloEls.estado.textContent = pausado ? "Pausado" : "Em andamento";
  cicloEls.estado.classList.toggle("pausado", pausado);
  cicloEls.diaNum.textContent = calcularDiaAtual();
  cicloEls.diaTot.textContent = total;
  const pct = cicloProgressoPct();
  cicloEls.pct.textContent = `${pct}%`;
  cicloEls.barra.style.width = `${pct}%`;
  cicloEls.barra.classList.toggle("pausado", pausado);
  renderMarcosCiclo(pausado);
  cicloEls.inicio.textContent = fmtDataHora(state.cicloIniciadoEm.getTime());
  cicloEls.fim.textContent = fmtDataHora(cicloUltimoDiaMs());
  const pode = cicloPodeParar();
  cicloEls.btnParar.disabled = !pode;
  cicloEls.btnParar.setAttribute("aria-disabled", String(!pode));
  cicloEls.dica.textContent = pausado
    ? "Ciclo pausado — a contagem está parada."
    : pode ? "Último dia iniciado: Parar liberado." : `Parar libera em ${fmtFalta(cicloUltimoDiaMs() - Date.now())}.`;
  cicloEls.btnPausar.classList.toggle("retomar", pausado);
  cicloEls.btnPausar.querySelector(".ciclo-acao-rotulo").textContent = pausado ? "Retomar" : "Pausar";
}
document.getElementById("btn-ciclo-voltar").addEventListener("click", () => showScreen("dashboard"));
// Fase 55: toque no fundo desfocado fecha o cartão flutuante
screens.ciclo.addEventListener("click", (e) => { if (e.target === screens.ciclo && cicloFlutuante()) showScreen("dashboard"); });

// Pausar congela a contagem; Retomar desloca cicloIniciadoEm pelo tempo pausado (o dia não pula).
cicloEls.btnPausar.addEventListener("click", async () => {
  if (!state.chocadeiraId || !state.cicloIniciadoEm) return;
  cicloEls.btnPausar.disabled = true;
  try {
    const ref = db.collection("chocadeiras").doc(state.chocadeiraId);
    if (state.cicloPausadoEm) {
      const novoInicio = new Date(state.cicloIniciadoEm.getTime() + (Date.now() - state.cicloPausadoEm.getTime()));
      await ref.update({ cicloIniciadoEm: firebase.firestore.Timestamp.fromDate(novoInicio), cicloPausadoEm: firebase.firestore.FieldValue.delete() });
      registrarAcao("Ciclo", `Retomou o ciclo "${state.cicloNome || "atual"}"`);
    } else {
      await ref.update({ cicloPausadoEm: firebase.firestore.FieldValue.serverTimestamp() });
      registrarAcao("Ciclo", `Pausou o ciclo "${state.cicloNome || "atual"}"`);
    }
  } catch (err) {
    console.error("Erro ao pausar/retomar ciclo:", err);
    mostrarFeedbackHeader("Falha ao pausar/retomar — tente de novo", "error");
  } finally {
    cicloEls.btnPausar.disabled = false;
  }
});

// Confirmação genérica (Parar, Lixeira, deletar tipo)
const confEls = {
  modal: document.getElementById("modal-confirma-backdrop"),
  titulo: document.getElementById("confirma-titulo"),
  texto: document.getElementById("confirma-texto"),
  ok: document.getElementById("confirma-ok"),
};
let confAcao = null;
function confirmarAcao(titulo, texto, rotuloOk, acao) {
  confEls.titulo.textContent = titulo;
  confEls.texto.textContent = texto;
  confEls.ok.textContent = rotuloOk;
  confAcao = acao;
  confEls.modal.hidden = false;
}
function fecharConfirma() { confEls.modal.hidden = true; confAcao = null; }
document.getElementById("confirma-cancelar").addEventListener("click", fecharConfirma);
document.getElementById("confirma-close").addEventListener("click", fecharConfirma);
confEls.modal.addEventListener("click", (e) => { if (e.target === confEls.modal) fecharConfirma(); });
confEls.ok.addEventListener("click", async () => {
  const fn = confAcao;
  fecharConfirma();
  if (fn) await fn();
});

async function encerrarCiclo(motivo) {
  if (!state.chocadeiraId) return;
  try {
    const D = firebase.firestore.FieldValue.delete();
    await db.collection("chocadeiras").doc(state.chocadeiraId).update({
      cicloIniciadoEm: D, cicloPausadoEm: D, cicloDuracaoDias: D, cicloNome: D, cicloTipoNome: D, cicloExtra: D,
      "cmd.fimTipo": `${motivo || "parar"}|${Date.now()}`, // Fase 55: o aparelho distingue "parar" de "lixeira" no relatório
    });
    registrarAcao("Ciclo", `${motivo === "lixeira" ? "Excluiu o lote" : "Encerrou o ciclo"} "${state.cicloNome || "atual"}"`);
    showScreen("dashboard");
  } catch (err) {
    console.error("Erro ao encerrar ciclo:", err);
    mostrarFeedbackHeader("Falha ao encerrar o ciclo — tente de novo", "error");
  }
}
cicloEls.btnParar.addEventListener("click", () => {
  if (!cicloPodeParar()) return;
  confirmarAcao("Encerrar o ciclo?", `O lote "${state.cicloNome || "atual"}" será finalizado (dia ${calcularDiaAtual()}/${cicloTotalDias()}) e o ciclo removido do painel.`, "Parar ciclo", () => encerrarCiclo("parar"));
});
document.getElementById("btn-ciclo-lixeira").addEventListener("click", () => {
  confirmarAcao(state.cicloPreparo ? "Cancelar o preparo?" : "Excluir o lote?", state.cicloPreparo ? `O preparo do lote "${state.cicloNome || "atual"}" será cancelado.` : `Todo o progresso do lote "${state.cicloNome || "atual"}" será perdido. Esta ação não pode ser desfeita.`, state.cicloPreparo ? "Cancelar preparo" : "Excluir", () => encerrarCiclo("lixeira"));
});

// ─── Fase 77: status do ambiente (OFF · AGUARDE · PRONTO · OK) e confirmação "ovos colocados" ───
function statusAmbInfo() {
  const ativo = !!state.cicloIniciadoEm, prep = ativo && !!state.cicloPreparo;
  const ambPartes = String(state.ambiente || "").split(";");
  const [modo, segS, totS, diagS] = ambPartes; // Fase 80: 4º campo = diagnóstico (causa provável) quando o ambiente não estabiliza em 10 min
  const tampaAuto = ambPartes.length >= 5 && ambPartes[ambPartes.length - 1] === "A"; // Fase 95: último campo "A" = tampa automática (sensor ADXL345 ligado e calibrado no aparelho)
  const total = Number(totS) || 300;
  let seg = Number(segS) || 0;
  if (modo === "aguardando" && state.ambRecebidoEm) seg = Math.min(total, seg + (Date.now() - state.ambRecebidoEm) / 1000); // o aparelho informa a cada ~1 min; interpola entre os avisos
  const pronto = prep && modo === "pronto";
  // Fase 79: sem ciclo o aparelho MANTÉM o ambiente do último tipo ("manter;1|0;<tipo>") até o usuário parar; "parado;<tipo>" = pode voltar a manter
  const man = !ativo && modo === "manter", manOk = man && segS === "1", parado = !ativo && modo === "parado";
  const tampa = modo === "tampa" ? Number(segS) || 1 : 0;
  let tampaSeg = tampa ? Number(totS) || 0 : 0; // Fase 82: 1 = aberta · 2 = recuperando (segundos desde o início do estado)
  if (tampa && state.ambRecebidoEm) tampaSeg += Math.max(0, (Date.now() - state.ambRecebidoEm) / 1000); // Fase 96: o cronômetro anda entre os avisos do aparelho (~1 min)
  const tipoUlt = man ? (totS || "") : parado ? (segS || "") : "";
  const tOk = state.tempAtual != null && state.tempAtual >= FAIXAS.temperatura.min && state.tempAtual <= FAIXAS.temperatura.max;
  const uOk = state.umidAtual != null && state.umidAtual >= FAIXAS.umidade.min && state.umidAtual <= FAIXAS.umidade.max;
  let aOk = false; try { aOk = !!autoCfg.ativo; } catch (e) { /* extras.js ainda não carregou */ }
  let txt = "OFF", cls = "value-ok", pct = 0;
  if (ativo) {
    if (prep) { if (pronto) { txt = "PRONTO"; pct = 100; } else { txt = "AGUARDE"; cls = "value-warn"; pct = Math.min(100, (seg / total) * 100); } }
    else {   // Fase 112: o motivo REAL em vez de "OK" sempre (automação desligada, sensor, fora da faixa)
      const semLeitura = state.tempAtual == null || state.umidAtual == null;
      if (!aOk) { txt = "AUTO OFF"; cls = "value-warn"; }
      else if (semLeitura) { txt = "SENSOR"; cls = "value-warn"; }
      else if (!tOk || !uOk) { txt = !tOk && !uOk ? "FORA T/U" : !tOk ? "FORA T" : "FORA U"; cls = "value-warn"; }
      else txt = "OK";
    }
  } else if (man) { txt = manOk ? "MANTÉM" : "AJUSTE"; cls = manOk ? "value-ok" : "value-warn"; }
  if (tampa) { txt = tampa === 1 ? "TAMPA" : "RECUP."; cls = "value-warn"; }
  return { ativo, prep, pronto, seg, total, tOk, uOk, aOk, txt, cls, pct, man, manOk, parado, tipoUlt, diag: diagS || "", tampa, tampaSeg, tampaAuto };
}
// Fase 115c — o card mostra UMA informação por vez e troca a cada 5 s (igual ao aparelho): estado principal, causa provável
// (diagnóstico curto do aparelho, se não repetir o estado), rpm do fan 1 e "CO₂ ALTO". Com 1 item só, fica fixo.
let statusItensN = 1;
function statusItensPwa(s) {
  const itens = [{ txt: s.txt, cls: s.cls }];
  const d = String(s.diag || "").trim();
  if (d && d.toUpperCase() !== s.txt.toUpperCase() && !(/^AUTO/i.test(s.txt) && /^auto/i.test(d))) itens.push({ txt: d.toUpperCase(), cls: "value-warn" });
  const co2Alto = s.ativo && !s.prep && typeof state.co2Atual === "number" && state.co2Atual > FAIXAS.co2.max + FAIXAS.co2.margem;
  if (co2Alto) itens.push({ txt: "CO₂ ALTO", cls: "value-critico" });
  if (typeof state.rpm === "number" && state.rpm > 0) itens.push({ txt: String(state.rpm), unit: "rpm", cls: "value-ok" });
  return itens;
}
function desenharItemStatus(s) {
  const v = document.getElementById("card-status-value");
  if (!v) return;
  const itens = statusItensPwa(s);
  statusItensN = itens.length;
  const k = itens.length > 1 ? Math.floor(Date.now() / 5000) % itens.length : 0;
  const it = itens[k];
  v.textContent = it.txt;
  if (it.unit) { const u = document.createElement("span"); u.className = "unit"; u.textContent = it.unit; v.appendChild(u); }
  v.classList.toggle("value-warn", it.cls === "value-warn");
  v.classList.toggle("value-ok", it.cls === "value-ok");
  v.classList.toggle("value-critico", it.cls === "value-critico");
  v.classList.toggle("status-longo", it.txt.length > 8);
  const dots = document.getElementById("card-status-dots");
  if (dots) {
    dots.textContent = "";
    if (itens.length > 1) itens.forEach((_, i) => { const e = document.createElement(i === k ? "b" : "span"); e.textContent = i === k ? "●" : "○"; dots.appendChild(e); });
  }
}
function atualizarCardStatus() {
  const v = document.getElementById("card-status-value"), card = document.getElementById("card-status");
  if (!v || !card) return;
  const s = statusAmbInfo();
  desenharItemStatus(s);
  card.style.setProperty("--sp", String(s.prep ? s.pct : 0)); // borda neon enchendo = tempo estável / 5 min
  card.dataset.sp = s.prep ? (s.pronto ? "ok" : "aguarde") : "";
  if (typeof statusJanelaAtualizar === "function") statusJanelaAtualizar();
}
setInterval(() => { if (statusItensN > 1) desenharItemStatus(statusAmbInfo()); }, 1000);   // Fase 115c: rotação de 5 s (só quando há mais de 1 aviso)
setInterval(() => { if (state.cicloPreparo || String(state.ambiente || "").startsWith("tampa;")) atualizarCardStatus(); }, 1000);   // Fase 96: também com a tampa aberta/recuperando (cronômetro)

async function confirmarOvosColocados() {
  if (!state.chocadeiraId || !state.cicloPreparo) return;
  const executar = async () => {
    try {
      await db.collection("chocadeiras").doc(state.chocadeiraId).update({
        cicloIniciadoEm: firebase.firestore.Timestamp.now(), // o ciclo (dia 1) começa AGORA
        cicloPausadoEm: firebase.firestore.FieldValue.delete(),
        cicloExtra: `0;${state.cicloVolL || 0}`,
      });
      registrarAcao("Ciclo", `Confirmou que os ovos foram colocados (lote "${state.cicloNome || "atual"}")`);
      if (typeof statusJanelaFechar === "function") statusJanelaFechar();
      mostrarFeedbackHeader("Ciclo iniciado! A contagem começou agora.", "ok");
    } catch (err) {
      console.error("Erro ao confirmar ovos:", err);
      mostrarFeedbackHeader("Falha ao iniciar o ciclo — tente de novo", "error");
    }
  };
  if (statusAmbInfo().pronto) await executar();
  else confirmarAcao("Ainda estabilizando", "A chocadeira ainda não ficou 5 min estável. Confirmar mesmo assim que os ovos já foram colocados?", "Sim, colocados", executar);
}
// bloco do preparo (tela do ciclo e janela do Status): barra de estabilidade, checklist e botão de confirmação
function renderPreparoBloco(el) {
  const s = statusAmbInfo(), f = (n, d = 1) => (n == null ? "--" : Number(n).toFixed(d).replace(".", ","));
  el.textContent = "";
  const titulo = document.createElement("div"); titulo.className = "prep-titulo " + (s.pronto ? "ok" : "aguarde");
  titulo.textContent = s.pronto ? "Pronto — pode colocar os ovos" : "Aguarde — NÃO coloque os ovos ainda";
  const barra = document.createElement("div"); barra.className = "prep-barra";
  const fill = document.createElement("i"); fill.style.width = `${s.pct}%`; fill.className = s.pronto ? "ok" : ""; barra.appendChild(fill);
  const leg = document.createElement("div"); leg.className = "prep-leg";
  leg.textContent = s.pronto ? "Ambiente estável por 5 min seguidos." : `Estável há ${Math.floor(s.seg / 60)}:${String(Math.floor(s.seg % 60)).padStart(2, "0")} de ${Math.round(s.total / 60)}:00 min (saídas de até 1 min não zeram a contagem).`;
  const diagEl = document.createElement("p"); diagEl.className = "prep-diag"; diagEl.hidden = !s.diag; diagEl.textContent = s.diag ? `⚠ ${s.diag}` : "";
  const ul = document.createElement("ul"); ul.className = "prep-lista";
  [[s.tOk, `Temperatura: ${f(state.tempAtual)} °C (faixa ${f(FAIXAS.temperatura.min)}–${f(FAIXAS.temperatura.max)})`],
   [s.uOk, `Umidade: ${f(state.umidAtual, 0)}% (faixa ${FAIXAS.umidade.min}–${FAIXAS.umidade.max})`],
   [s.aOk, `Automação: ${s.aOk ? "ligada" : "desligada"}`]].forEach(([ok, t]) => {
    const li = document.createElement("li"); li.className = ok ? "ok" : "nok"; li.textContent = `${ok ? "✓" : "✗"} ${t}`; ul.appendChild(li);
  });
  const nota = document.createElement("p"); nota.className = "prep-nota";
  nota.textContent = `${state.cicloVolL ? `Caixa de ${state.cicloVolL} L. ` : ""}Quando colocar os ovos, confirme abaixo: o ciclo (dia 1) começa nesse instante.`;
  const btn = document.createElement("button"); btn.type = "button"; btn.className = "prep-confirmar" + (s.pronto ? " pronto" : "");
  btn.textContent = "Ovos colocados"; btn.addEventListener("click", confirmarOvosColocados);
  el.append(titulo, barra, leg, diagEl, ul, nota, btn);
}
setInterval(() => { if (state.cicloPreparo && screens.ciclo.classList.contains("active")) renderPreparoBloco(document.getElementById("ciclo-preparo")); }, 5000);

// ─── Wizard de início (4 etapas) ───
const wizEls = {
  ind: document.getElementById("wiz-indicador"),
  segs: document.querySelectorAll("#wiz-progresso i"),
  etapas: document.querySelectorAll(".wiz-etapa"),
  nome: document.getElementById("wiz-nome"),
  tipos: document.getElementById("wiz-tipos"),
  pers: document.getElementById("wiz-pers"),
  persDias: document.getElementById("wiz-pers-dias"),
  inicio: document.getElementById("wiz-inicio"),
  inicioAviso: document.getElementById("wiz-inicio-aviso"),
  resumo: document.getElementById("wiz-resumo"),
  prev: document.getElementById("wiz-prev"),
  next: document.getElementById("wiz-next"),
};
const wiz = { etapa: 1, tipo: 0, dias: 21, vol: 3, modo: "preparo" }; // Fase 77: volume da caixa + modo de início // tipo: índice em state.tipos, ou -1 = personalizado
function inputDataHoraLocal(d) {
  const p = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
}
function abrirWizardCiclo() {
  wiz.etapa = 1; wiz.tipo = 0; wiz.dias = (state.tipos[0] && state.tipos[0].dias) || 21;
  wizEls.nome.value = "";
  const agora = new Date();
  wizEls.inicio.value = inputDataHoraLocal(agora);
  wizEls.inicio.max = inputDataHoraLocal(agora);
  wizEls.pers.hidden = true;
  wiz.modo = "preparo";
  wiz.vol = (typeof hwEd !== "undefined" && hwEd && hwEd.volL) ? hwEd.volL : 3; // Fase 80: o volume vem de Configurações > Hardware
  renderWizard();
  showScreen("cicloWizard");
  setTimeout(() => wizEls.nome.focus(), 50);
}
function wizInicioDate() { const d = new Date(wizEls.inicio.value); return Number.isNaN(d.getTime()) ? null : d; }
function wizTipoNome() { return wiz.tipo >= 0 && state.tipos[wiz.tipo] ? state.tipos[wiz.tipo].nome : "Personalizado"; }
function wizValido() {
  if (wiz.etapa === 1) return limparNomePeloFormato(wizEls.nome.value).length > 0;
  if (wiz.etapa === 2) return wiz.tipo >= 0 && wiz.dias >= 1 && wiz.dias <= 60;
  if (wiz.etapa === 3) {
    if (!(wiz.vol >= 1 && wiz.vol <= 500)) return false;
    if (wiz.modo === "preparo") return true;
    const d = wizInicioDate(); return !!d && d.getTime() <= Date.now() + 60000;
  }
  return true;
}
function renderWizTipos() {
  wizEls.tipos.textContent = "";
  const itens = state.tipos.map((t, i) => ({ i, nome: t.nome, sub: `${t.dias} dias` }));
  itens.push({ i: -1, nome: "Personalizado", sub: "criar um tipo completo" });
  itens.forEach((it) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "wiz-tipo" + (wiz.tipo === it.i ? " sel" : "");
    b.setAttribute("aria-pressed", String(wiz.tipo === it.i));
    const n = document.createElement("span"); n.className = "wiz-tipo-nome"; n.textContent = it.nome;
    const s = document.createElement("span"); s.className = "wiz-tipo-sub"; s.textContent = it.sub;
    b.append(n, s);
    b.addEventListener("click", () => {
      if (it.i === -1) { // Fase 54: "Personalizado" abre o editor COMPLETO de tipo; o tipo novo fica salvo junto com os outros
        if (state.tipos.length >= TIPOS_MAX) { mostrarFeedbackHeader("Limite de tipos atingido — apague um em Configurações", "warn"); return; }
        abrirModalTipo(-1, (idx) => { wiz.tipo = idx; wiz.dias = state.tipos[idx].dias; renderWizTipos(); renderWizard(); });
        return;
      }
      wiz.tipo = it.i;
      wiz.dias = state.tipos[it.i].dias;
      wizEls.pers.hidden = true;
      renderWizTipos();
      renderWizard();
    });
    wizEls.tipos.appendChild(b);
  });
}
function renderWizResumo() {
  const prep = wiz.modo === "preparo";
  const ini = prep ? null : wizInicioDate();
  const fim = ini ? ini.getTime() + (wiz.dias - 1) * 86400000 : null;
  const t = wiz.tipo >= 0 ? state.tipos[wiz.tipo] : null;
  const linhas = [
    ["Lote", limparNomePeloFormato(wizEls.nome.value)],
    ["Tipo de ovos", wizTipoNome()],
    ["Duração", `${wiz.dias} dias`],
    ["Caixa", `${wiz.vol} L`],
    ["Início da incubação", prep ? "Quando você confirmar que colocou os ovos" : (ini ? fmtDataHora(ini.getTime()) : "--")],
  ];
  if (!prep) linhas.push(["Último dia começa às", fim ? fmtDataHora(fim) : "--"]);
  if (t) {
    linhas.push(["Ideal", `${t.temp.toFixed(1)} °C · ${t.umid}%`]);
    linhas.push(["Na eclosão", `${(t.tempEcl > 0 ? t.tempEcl : t.temp).toFixed(1)} °C · ${t.umidEcl}%`]);
  }
  if (t && (ini || prep)) {
    const dt = (d) => (ini ? ` (${new Date(ini.getTime() + (d - 1) * 86400000).toLocaleDateString("pt-BR", { day: "2-digit", month: "2-digit" })})` : "");
    linhas.push(["Ovoscopia", `dia ${t.ovo1}${dt(t.ovo1)}${t.ovo2 ? ` e dia ${t.ovo2}${dt(t.ovo2)}` : ""}`]);
    linhas.push(["Início da eclosão", `dia ${t.eclDia}${dt(t.eclDia)}`]);
  }
  wizEls.resumo.textContent = "";
  linhas.forEach(([k, v]) => {
    const dt = document.createElement("dt"); dt.textContent = k;
    const dd = document.createElement("dd"); dd.textContent = v;
    wizEls.resumo.append(dt, dd);
  });
  // Fase 69: aviso de pré-aquecimento (o aparelho avisa quando o ambiente estabilizar)
  const dtA = document.createElement("dt"); dtA.textContent = "Antes de colocar os ovos";
  const ddA = document.createElement("dd"); ddA.className = "wiz-aviso-ovos";
  ddA.textContent = prep ? "Ao iniciar, a chocadeira ajusta temperatura e umidade sem ovos (e liga a automação, se estiver desligada). Quando o status mostrar PRONTO, coloque os ovos e confirme: o ciclo começa nesse instante." : "O ciclo começa na data informada, com os ovos já dentro.";
  wizEls.resumo.append(dtA, ddA);
}
function renderWizard() {
  wizEls.ind.textContent = `Etapa ${wiz.etapa} de 4`;
  wizEls.segs.forEach((s, i) => s.classList.toggle("on", i < wiz.etapa));
  wizEls.etapas.forEach((e) => { e.hidden = Number(e.dataset.etapa) !== wiz.etapa; });
  if (wiz.etapa === 2) renderWizTipos();
  if (wiz.etapa === 4) renderWizResumo();
  if (wiz.etapa === 3) renderWizVolume();
  wizEls.next.textContent = wiz.etapa === 4 ? (wiz.modo === "preparo" ? "Iniciar preparo" : "Iniciar ciclo") : "Próximo ›";
  wizEls.next.classList.toggle("final", wiz.etapa === 4);
  wizEls.next.disabled = !wizValido();
  wizEls.inicioAviso.hidden = wiz.etapa !== 3 || wiz.modo === "preparo" || wizValido();
}
wizEls.nome.addEventListener("input", renderWizard);
// Fase 54: Enter fecha o teclado e vai para a próxima etapa (nome do lote / data de início)
[wizEls.nome, wizEls.inicio].forEach((el) => el.addEventListener("keydown", (e) => {
  if (e.key !== "Enter") return;
  e.preventDefault();
  el.blur();
  if (wizValido()) wizEls.next.click();
}));
wizEls.inicio.addEventListener("input", renderWizard);
// Fase 77: etapa "Caixa e início"
function renderWizVolume() {
  const cont = document.getElementById("wiz-vol-chips"), inp = document.getElementById("wiz-vol");
  if (!cont.children.length) {
    [3, 10, 30, 60, 100].forEach((v) => {
      const b = document.createElement("button"); b.type = "button"; b.className = "wiz-vol-chip"; b.textContent = `${v} L`; b.dataset.v = String(v);
      b.addEventListener("click", () => { wiz.vol = v; renderWizard(); });
      cont.appendChild(b);
    });
  }
  [...cont.children].forEach((b) => b.classList.toggle("sel", Number(b.dataset.v) === wiz.vol));
  if (document.activeElement !== inp) inp.value = wiz.vol;
  document.querySelectorAll('input[name="wiz-modo"]').forEach((r) => { r.checked = r.value === wiz.modo; });
  wizEls.inicio.hidden = wiz.modo === "preparo";
}
document.getElementById("wiz-vol").addEventListener("input", (e) => { wiz.vol = parseInt(e.target.value, 10) || 0; renderWizard(); });
document.querySelectorAll('input[name="wiz-modo"]').forEach((r) => r.addEventListener("change", () => { if (r.checked) { wiz.modo = r.value; renderWizard(); } }));
wizEls.persDias.addEventListener("input", () => {
  wiz.dias = parseInt(wizEls.persDias.value, 10) || 0;
  renderWizard();
});
// Fase 80: teclado no desktop — Enter/→ avançam, ←/Esc voltam, ↑/↓ escolhem na lista (tipo de ovos / volume)
document.addEventListener("keydown", (e) => {
  if (!screens.cicloWizard.classList.contains("active") || e.ctrlKey || e.metaKey || e.altKey) return;
  if (document.getElementById("modal-tipo-backdrop") && !document.getElementById("modal-tipo-backdrop").hidden) return; // editor de tipo aberto
  const alvo = e.target, tag = alvo && alvo.tagName;
  const campo = tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA";
  const btnProprio = tag === "BUTTON" && (alvo.id === "wiz-prev" || alvo.id === "wiz-next" || alvo.id === "btn-wiz-voltar");
  const avancar = () => { if (wizValido()) wizEls.next.click(); };
  if (e.key === "Enter") {
    if (btnProprio || alvo === wizEls.nome || alvo === wizEls.inicio) return; // o próprio botão / o campo de texto já tratam o Enter
    if (tag === "TEXTAREA") return;
    e.preventDefault();
    if (alvo && alvo.blur && campo) alvo.blur();
    if (tag === "BUTTON" && alvo.click) { alvo.click(); if (wiz.etapa === 2 && wiz.tipo < 0) return; } // seleciona o tipo/volume focado
    avancar();
  } else if (e.key === "ArrowRight" && !campo) { e.preventDefault(); avancar(); }
  else if ((e.key === "ArrowLeft" && !campo) || e.key === "Escape") { e.preventDefault(); wizEls.prev.click(); }
  else if ((e.key === "ArrowDown" || e.key === "ArrowUp") && !campo) {
    const d = e.key === "ArrowDown" ? 1 : -1;
    if (wiz.etapa === 2 && state.tipos.length) {
      e.preventDefault();
      const n = state.tipos.length, i = wiz.tipo < 0 ? 0 : wiz.tipo;
      wiz.tipo = (i + d + n) % n; wiz.dias = state.tipos[wiz.tipo].dias; wizEls.pers.hidden = true;
      renderWizTipos(); renderWizard();
    } else if (wiz.etapa === 3) {
      e.preventDefault();
      const pre = [3, 10, 30, 60, 100]; let i = pre.indexOf(wiz.vol);
      i = i < 0 ? (d > 0 ? 0 : pre.length - 1) : (i + d + pre.length) % pre.length;
      wiz.vol = pre[i]; renderWizard();
    }
  }
});
wizEls.prev.addEventListener("click", () => {
  if (wiz.etapa === 1) { showScreen("dashboard"); return; }
  wiz.etapa--; renderWizard();
});
document.getElementById("btn-wiz-voltar").addEventListener("click", () => wizEls.prev.click());
wizEls.next.addEventListener("click", async () => {
  if (!wizValido()) return;
  if (wiz.etapa < 4) { wiz.etapa++; renderWizard(); return; }
  if (!state.chocadeiraId) return;
  wizEls.next.disabled = true;
  try {
    const prep = wiz.modo === "preparo";
    if (typeof hwAtualizarVolume === "function") hwAtualizarVolume(wiz.vol); // mantém o Hardware em dia com a caixa informada (não bloqueia o início do ciclo)
    await db.collection("chocadeiras").doc(state.chocadeiraId).update({
      // preparo: guarda "agora" só como marca do preparo; o início real (dia 1) é gravado quando o usuário confirma os ovos
      cicloIniciadoEm: firebase.firestore.Timestamp.fromDate(prep ? new Date() : wizInicioDate()),
      cicloPausadoEm: firebase.firestore.FieldValue.delete(),
      cicloDuracaoDias: wiz.dias,
      cicloNome: limparNomePeloFormato(wizEls.nome.value),
      cicloTipoNome: wizTipoNome(),
      cicloExtra: `${prep ? 1 : 0};${wiz.vol}`,
    });
    registrarAcao("Ciclo", `${prep ? "Iniciou o preparo do" : "Iniciou o"} lote "${limparNomePeloFormato(wizEls.nome.value)}" (${wizTipoNome()}, ${wiz.dias} dias, caixa ${wiz.vol} L)`);
    showScreen("dashboard");
  } catch (err) {
    console.error("Erro ao iniciar ciclo:", err);
    mostrarFeedbackHeader("Falha ao iniciar o ciclo — tente de novo", "error");
    wizEls.next.disabled = false;
  }
});

// ─── Tipos de ovos (Configurações) ───
const tiposEls = {
  lista: document.getElementById("tipos-lista"),
  modal: document.getElementById("modal-tipo-backdrop"),
  titulo: document.getElementById("tipo-modal-titulo"),
  nome: document.getElementById("tipo-f-nome"),
  dias: document.getElementById("tipo-f-dias"),
  temp: document.getElementById("tipo-f-temp"),
  umid: document.getElementById("tipo-f-umid"),
  umidEcl: document.getElementById("tipo-f-umidecl"), tempEcl: document.getElementById("tipo-f-tempecl"),
  vira: document.getElementById("tipo-f-vira"),
  tmin: document.getElementById("tipo-f-tmin"), tmax: document.getElementById("tipo-f-tmax"),
  umin: document.getElementById("tipo-f-umin"), umax: document.getElementById("tipo-f-umax"),
  vpd: document.getElementById("tipo-f-vpd"), vhora: document.getElementById("tipo-f-vhora"),
  ovo1: document.getElementById("tipo-f-ovo1"), ovo2: document.getElementById("tipo-f-ovo2"), ecl: document.getElementById("tipo-f-ecl"),
  horarios: document.getElementById("tipo-f-horarios"),
  erro: document.getElementById("tipo-f-erro"),
};
let tipoEditando = -1; // -1 = novo
function renderTipos() {
  tiposEls.lista.textContent = "";
  state.tipos.forEach((t, i) => {
    const row = document.createElement("div");
    row.className = "tipo-item";
    const tx = document.createElement("div");
    tx.className = "tipo-item-texto";
    const n = document.createElement("div"); n.className = "tipo-item-nome"; n.textContent = t.nome;
    const s = document.createElement("div"); s.className = "tipo-item-sub";
    s.textContent = `${t.dias} dias · ${t.temp.toFixed(1)} °C (${t.tempMin.toFixed(1)}–${t.tempMax.toFixed(1)}) · umid. ${t.umid}% (${t.umidMin}–${t.umidMax}%) · eclosão ${(t.tempEcl > 0 ? t.tempEcl : t.temp).toFixed(1)} °C / ${t.umidEcl}% · ${t.viraPorDia} viragens/dia a partir de ${String(t.viraHH).padStart(2, "0")}:${String(t.viraMM).padStart(2, "0")}, até o dia ${t.viraAte} · ovoscopia dia ${t.ovo1}${t.ovo2 ? ` e ${t.ovo2}` : ""} · eclosão a partir do dia ${t.eclDia}`;
    tx.append(n, s);
    const ed = document.createElement("button"); ed.type = "button"; ed.className = "icon-btn"; ed.title = "Editar"; ed.setAttribute("aria-label", `Editar ${t.nome}`); ed.textContent = "✎";
    ed.addEventListener("click", () => abrirModalTipo(i));
    const del = document.createElement("button"); del.type = "button"; del.className = "icon-btn icon-btn-danger"; del.title = "Deletar"; del.setAttribute("aria-label", `Deletar ${t.nome}`); del.textContent = "🗑";
    del.addEventListener("click", () => {
      if (state.tipos.length <= 1) { mostrarFeedbackHeader("Mantenha pelo menos um tipo de ovo", "warn"); return; }
      confirmarAcao("Deletar tipo?", `O tipo "${t.nome}" será removido da lista. Ciclos em andamento não são afetados.`, "Deletar", async () => {
        try { await salvarTiposOvos(state.tipos.filter((_, k) => k !== i)); }
        catch (e) { console.error("Erro ao deletar tipo:", e); mostrarFeedbackHeader("Falha ao deletar o tipo", "error"); }
      });
    });
    row.append(tx, ed, del);
    tiposEls.lista.appendChild(row);
  });
  document.getElementById("btn-tipo-novo").disabled = state.tipos.length >= TIPOS_MAX;
}
let tipoOriginalAoAbrir = null; // valores de quando o modal abriu (restaurar em tipos próprios)
function preencherModalTipo(t) {
  const p = (n) => String(n).padStart(2, "0");
  tiposEls.nome.value = t.nome; tiposEls.dias.value = t.dias; tiposEls.temp.value = t.temp;
  tiposEls.tmin.value = t.tempMin; tiposEls.tmax.value = t.tempMax;
  tiposEls.umid.value = t.umid; tiposEls.umin.value = t.umidMin; tiposEls.umax.value = t.umidMax;
  tiposEls.umidEcl.value = t.umidEcl; tiposEls.tempEcl.value = t.tempEcl > 0 ? t.tempEcl : t.temp; tiposEls.vira.value = t.viraAte;
  tiposEls.vpd.value = t.viraPorDia; tiposEls.vhora.value = `${p(t.viraHH)}:${p(t.viraMM)}`;
  tiposEls.ovo1.value = t.ovo1; tiposEls.ovo2.value = t.ovo2 || 0; tiposEls.ecl.value = t.eclDia;
  atualizarHorariosTipo();
}
function lerModalTipo() {
  const [hh, mm] = String(tiposEls.vhora.value || "").split(":").map((x) => parseInt(x, 10));
  return {
    nome: limparNomePeloFormato(tiposEls.nome.value).slice(0, 19),
    dias: parseInt(tiposEls.dias.value, 10), temp: parseFloat(tiposEls.temp.value),
    tempMin: parseFloat(tiposEls.tmin.value), tempMax: parseFloat(tiposEls.tmax.value),
    umid: parseInt(tiposEls.umid.value, 10), umidMin: parseInt(tiposEls.umin.value, 10), umidMax: parseInt(tiposEls.umax.value, 10),
    umidEcl: parseInt(tiposEls.umidEcl.value, 10), tempEcl: parseFloat(tiposEls.tempEcl.value), viraAte: parseInt(tiposEls.vira.value, 10),
    viraPorDia: parseInt(tiposEls.vpd.value, 10), viraHH: hh, viraMM: mm,
    ovo1: parseInt(tiposEls.ovo1.value, 10), ovo2: parseInt(tiposEls.ovo2.value, 10) || 0, eclDia: parseInt(tiposEls.ecl.value, 10),
  };
}
function atualizarHorariosTipo() {
  const t = lerModalTipo();
  tiposEls.horarios.textContent = t.viraPorDia >= 1 && t.viraPorDia <= 24 && !Number.isNaN(t.viraHH) && !Number.isNaN(t.viraMM)
    ? `Horários: ${horariosViragem(t).join(" · ")}` : "";
}
let tipoAoSalvar = null; // callback opcional (wizard "Personalizado"): recebe o índice do tipo salvo
function abrirModalTipo(i, aoSalvar) {
  tipoAoSalvar = aoSalvar || null;
  tipoEditando = i;
  const t = i >= 0 ? state.tipos[i] : { ...TIPOS_PADRAO[0], nome: "" };
  tipoOriginalAoAbrir = { ...t };
  tiposEls.titulo.textContent = i >= 0 ? "Editar tipo de ovo" : "Novo tipo de ovo";
  preencherModalTipo(t);
  tiposEls.erro.textContent = "";
  tiposEls.modal.hidden = false;
  tiposEls.nome.focus();
}
function fecharModalTipo() { tiposEls.modal.hidden = true; tipoAoSalvar = null; }
document.getElementById("btn-tipo-novo").addEventListener("click", () => abrirModalTipo(-1));
document.getElementById("tipo-modal-close").addEventListener("click", fecharModalTipo);
document.getElementById("tipo-cancelar").addEventListener("click", fecharModalTipo);
// Restaurar: padrão de fábrica (Galinha/Codorna); tipos próprios voltam aos valores de quando o modal abriu.
document.getElementById("tipo-restaurar").addEventListener("click", () => {
  const base = tipoOriginalAoAbrir || TIPOS_PADRAO[0];
  const fab = tipoEditando >= 0 ? tipoDeFabrica(base.nome) : null;
  preencherModalTipo({ ...(fab || base), nome: base.nome });
  tiposEls.erro.textContent = "Parâmetros restaurados (toque em Salvar para confirmar).";
});
["tipo-f-vpd", "tipo-f-vhora"].forEach((id) => document.getElementById(id).addEventListener("input", atualizarHorariosTipo));
tiposEls.modal.addEventListener("click", (e) => { if (e.target === tiposEls.modal) fecharModalTipo(); });
document.getElementById("tipo-salvar").addEventListener("click", async () => {
  const t = lerModalTipo();
  const erro = !t.nome ? "Informe o nome."
    : !(t.dias >= 1 && t.dias <= 60) ? "Dias de incubação: 1 a 60."
    : !(t.temp >= 30 && t.temp <= 42) ? "Temperatura ideal: 30 a 42 °C."
    : !(t.tempMin >= 30 && t.tempMin <= t.temp) ? "Temperatura mínima: entre 30 °C e a ideal."
    : !(t.tempMax >= t.temp && t.tempMax <= 42) ? "Temperatura máxima: entre a ideal e 42 °C."
    : !(t.umid >= 20 && t.umid <= 90) ? "Umidade ideal: 20 a 90%."
    : !(t.umidMin >= 10 && t.umidMin <= t.umid) ? "Umidade mínima: entre 10% e a ideal."
    : !(t.umidMax >= t.umid && t.umidMax <= 100) ? "Umidade máxima: entre a ideal e 100%."
    : !(t.umidEcl >= 30 && t.umidEcl <= 95) ? "Umidade na eclosão: 30 a 95%."
    : !(t.tempEcl >= 30 && t.tempEcl <= 42) ? "Temperatura na eclosão: 30 a 42 °C."
    : !(t.viraPorDia >= 1 && t.viraPorDia <= 24) ? "Viragens por dia: 1 a 24."
    : !(t.viraAte >= 0 && t.viraAte <= t.dias) ? "Virar até o dia: entre 0 e os dias de incubação."
    : (Number.isNaN(t.viraHH) || Number.isNaN(t.viraMM)) ? "Informe o horário da primeira viragem."
    : !(t.ovo1 >= 1 && t.ovo1 < t.dias) ? "1ª ovoscopia: entre o dia 1 e o penúltimo dia."
    : !(t.ovo2 === 0 || (t.ovo2 > t.ovo1 && t.ovo2 < t.dias)) ? "2ª ovoscopia: 0 (nenhuma) ou depois da 1ª e antes do último dia."
    : !(t.eclDia > Math.max(t.ovo1, t.ovo2) && t.eclDia <= t.dias) ? "Início da eclosão: depois das ovoscopias e até o último dia."
    : state.tipos.some((o, k) => k !== tipoEditando && o.nome.toLowerCase() === t.nome.toLowerCase()) ? "Já existe um tipo com esse nome."
    : "";
  if (erro) { tiposEls.erro.textContent = erro; return; }
  const lista = state.tipos.map((x) => ({ ...x }));
  if (tipoEditando >= 0) lista[tipoEditando] = t; else lista.push(t);
  try {
    await salvarTiposOvos(lista);
    const idx = tipoEditando >= 0 ? tipoEditando : lista.length - 1, cb = tipoAoSalvar;
    state.tipos = lista; // otimista: o snapshot confirma em seguida
    fecharModalTipo();
    if (cb) cb(idx);
  }
  catch (e) { console.error("Erro ao salvar tipo:", e); tiposEls.erro.textContent = "Não foi possível salvar. Tente de novo."; }
});

// --- Login com Google ---
const btnLogin = document.getElementById("btn-google-login");
const loginError = document.getElementById("login-error");

btnLogin.addEventListener("click", async () => {
  loginError.textContent = "";
  btnLogin.disabled = true;
  btnLogin.textContent = "Entrando...";

  try {
    const provider = new firebase.auth.GoogleAuthProvider();
    provider.setCustomParameters({ prompt: "select_account" }); // Fase 56: sempre deixa escolher a conta (trocar de conta)
    const result = await auth.signInWithPopup(provider);
    await criarUsuarioSeNaoExistir(result.user);
  } catch (err) {
    console.error("Erro no login Google:", err);
    loginError.textContent = mensagemDeErro(err);
  } finally {
    btnLogin.disabled = false;
    btnLogin.textContent = "Entrar com Google";
  }
});

async function criarUsuarioSeNaoExistir(user) {
  const ref = db.collection("usuarios").doc(user.uid);
  const snap = await ref.get();
  if (snap.exists) return;

  await ref.set({
    nome: user.displayName ?? "",
    email: user.email ?? "",
    uid: user.uid,
    createdAt: firebase.firestore.FieldValue.serverTimestamp(),
  });
}

function mensagemDeErro(err) {
  switch (err.code) {
    case "auth/popup-closed-by-user":
      return "Login cancelado — a janela foi fechada antes de concluir.";
    case "auth/popup-blocked":
      return "O navegador bloqueou o popup de login. Permita popups para este site.";
    case "auth/network-request-failed":
      return "Falha de conexão. Verifique sua internet e tente de novo.";
    default:
      return "Não foi possível entrar agora. Tente de novo em alguns segundos.";
  }
}

document.getElementById("btn-logout").addEventListener("click", () => {
  auth.signOut();
});

// --- Vincular chocadeira (pareamento por Device ID) ---
// O documento da chocadeira já existe (criado junto com o firmware, ID fixo
// em FIRESTORE_DOCUMENT_PATH no main.cpp — ver tela "Sistema" no display
// físico). Vincular = associar esse documento à conta logada, não criar um
// documento novo. As Security Rules só permitem esse "claim" quando o
// documento ainda não tem donoId (ehClaimValido() em firestore.rules) — por
// isso não dá pra fazer um get() antes pra checar/mostrar dados: o próprio
// update() é a tentativa de vínculo, e o resultado (sucesso/not-found/
// permission-denied) é que decide a mensagem.
const formVincular = document.getElementById("form-vincular");
const vincularError = document.getElementById("vincular-error");

formVincular.addEventListener("submit", async (e) => {
  e.preventDefault();
  vincularError.textContent = "";

  const user = auth.currentUser;
  if (!user) return;

  const deviceId = document.getElementById("input-device-id").value.trim();
  if (!deviceId) return;

  const btn = document.getElementById("btn-vincular");
  btn.disabled = true;
  btn.textContent = "Vinculando...";

  try {
    const ref = db.collection("chocadeiras").doc(deviceId);
    await ref.update({ donoId: user.uid });

    const snap = await ref.get();
    const dados = snap.data() || {};

    state.chocadeiraId = ref.id;
    atualizarBannerChocadeira({ nome: dados.nome || "Chocadeira" });
    escutarAtuadores(ref.id);
    showScreen("dashboard");
  } catch (err) {
    console.error("Erro ao vincular chocadeira:", err);
    if (err.code === "not-found") {
      vincularError.textContent = "Dispositivo não encontrado. Verifique o ID e tente novamente.";
    } else if (err.code === "permission-denied") {
      vincularError.textContent = "Este dispositivo já está vinculado a uma conta.";
    } else {
      vincularError.textContent = "Não foi possível vincular agora. Verifique sua conexão e tente de novo.";
    }
  } finally {
    btn.disabled = false;
    btn.textContent = "Vincular";
  }
});

// --- Service Worker ---
if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker
      .register("/service-worker.js")
      .then((reg) => console.log("Service worker registrado:", reg.scope))
      .catch((err) => console.error("Falha ao registrar service worker:", err));
  });
}

// --- Status online/offline — diferencia "chocadeira offline" (sem heartbeat,
// mas o app tem internet) de "aplicativo offline" (o navegador que não tem
// rede — nesse caso nem dá pra saber se a chocadeira está online ou não). ---
function calcularEstadoConexao() {
  if (!navigator.onLine) return "app-offline";
  const idade = state.firmwareLastSeen ? Date.now() - state.firmwareLastSeen.getTime() : Infinity;
  return idade < HEARTBEAT_STALE_MS ? "online" : "device-offline";
}

function atualizarStatusOnline() {
  const dot = document.getElementById("status-dot");
  const label = document.getElementById("status-label");
  if (!dot) return;

  state.estadoConexao = calcularEstadoConexao();

  dot.classList.remove("online", "offline", "warn");
  if (state.estadoConexao === "online") {
    dot.classList.add("online");
    if (label) label.textContent = "Online";
  } else if (state.estadoConexao === "device-offline") {
    dot.classList.add("offline");
    if (label) label.textContent = "Chocadeira offline";
  } else {
    dot.classList.add("warn");
    if (label) label.textContent = "Sem conexão";
  }

  atualizarUltimoSinal();
  atualizarControlesDisponibilidade();
}

// Diagnóstico visível direto no menu, sem precisar abrir o Console do
// Firebase pra ver se o heartbeat está mesmo chegando (bug de "chocadeira
// offline incorreto" — 2026-09-20): mostra há quanto tempo o
// firmwareLastSeen foi atualizado pela última vez.
function atualizarUltimoSinal() {
  const el = document.getElementById("menu-last-seen");
  if (!el) return;
  if (!state.firmwareLastSeen) {
    el.textContent = "Último sinal: nunca";
    return;
  }
  const segundos = Math.floor((Date.now() - state.firmwareLastSeen.getTime()) / 1000);
  let texto;
  if (segundos < 60) texto = `há ${segundos}s`;
  else if (segundos < 3600) texto = `há ${Math.floor(segundos / 60)}min`;
  else texto = `há ${Math.floor(segundos / 3600)}h`;
  el.textContent = `Último sinal: ${texto}`;
}
window.addEventListener("online", atualizarStatusOnline);
window.addEventListener("offline", atualizarStatusOnline);
// O snapshot só atualiza o status quando um heartbeat novo chega — sem isso,
// se a chocadeira desligar no meio do caminho, o ponto ficaria "online" pra
// sempre (nenhum evento novo dispara a reavaliação). Reavalia sozinho a cada 5s.
setInterval(atualizarStatusOnline, 5000);

// Bug crítico corrigido: antes disso dava pra acionar um atuador (ou "iniciar"
// a chocadeira) com o dispositivo offline — a escrita no Firestore tinha
// sucesso e só o feedback avisava do problema DEPOIS do toque. Agora os
// TOGGLES DE ATUADOR ficam desabilitados de verdade enquanto não houver
// heartbeat recente, então a ação nem chega a ser tentada.
//
// Bug corrigido (2026-09-20): o botão de ciclo NÃO deveria entrar nessa
// regra — iniciar/pausar/retomar/parar o ciclo é só um registro de datas
// no Firestore (cicloIniciadoEm/cicloPausadoEm/cicloDuracaoDias), sem
// nenhum efeito físico que dependa do ESP32 estar alcançável. Bloquear
// isso quando o status (às vezes incorretamente) mostra "offline" só
// impedia o usuário de, por exemplo, parar o ciclo sem nenhum motivo real.
const MOTIVO_OFFLINE = "Chocadeira offline — ações indisponíveis";

function atualizarControlesDisponibilidade() {
  const online = state.estadoConexao === "online";

  RELAY_NAMES.forEach((_, i) => {
    const input = document.getElementById(`atuador-ch${i}`);
    if (!input) return;
    if (i === BOMBA_CH_INDEX) return;   // Fase 87 — sempre bloqueado no app
    if (i === COOLER_CH_INDEX) return;  // Fase 36 — fica sempre bloqueado, online ou não (ver montarGridAtuadores)
    input.disabled = !online;
    const label = input.closest(".toggle");
    if (label) label.title = online ? "" : MOTIVO_OFFLINE;
  });

  // Desligar (deep sleep) só tem efeito de verdade com o dispositivo
  // alcançável — mesmo raciocínio dos atuadores (não do botão de ciclo,
  // que é só Firestore).
  const btnDesligar = document.getElementById("btn-desligar");
  if (btnDesligar) {
    btnDesligar.disabled = !online;
    btnDesligar.title = online ? "" : MOTIVO_OFFLINE;
  }
  if (window.shareAplicarPermissoes) window.shareAplicarPermissoes(); // Fase 88 — convidado: re-bloqueia o que não tem permissão
}

// --- Menu (hambúrguer): agrupa status/perfil/logout, que antes ficavam soltos no topbar ---
const btnMenu = document.getElementById("btn-menu");
const menuDropdown = document.getElementById("menu-dropdown");

btnMenu.addEventListener("click", (e) => {
  e.stopPropagation();
  const aberto = menuDropdown.classList.toggle("open");
  btnMenu.setAttribute("aria-expanded", String(aberto));
});

document.addEventListener("click", (e) => {
  if (!menuDropdown.classList.contains("open")) return;
  if (menuDropdown.contains(e.target) || btnMenu.contains(e.target)) return;
  menuDropdown.classList.remove("open");
  btnMenu.setAttribute("aria-expanded", "false");
});

// --- Tela de Configurações (Atuadores mora aqui agora, não mais no
// dashboard) — abre/fecha por cima do dashboard, sem afetar o listener de
// atuadores (state.chocadeiraId continua o mesmo, só muda a tela visível). ---
function abrirTelaConfiguracoes() {
  menuDropdown.classList.remove("open");
  btnMenu.setAttribute("aria-expanded", "false");
  showScreen("configuracoes");
  if (!state.unsubscribeLogs && state.chocadeiraId) escutarLogs(state.chocadeiraId); // Fase 63: logs só carregam (até 60 docs) quando Configurações abre
  escutarIndiceArquivos(); // Fase 41 — só começa a escutar quando o usuário abre Configurações
  if (typeof onAbrirConfiguracoes === "function") onAbrirConfiguracoes(); // extras.js
  if (typeof window.acAutoCarregar === "function") window.acAutoCarregar(); // Fase 98: o cartão "Acessos e privacidade" já mostra os últimos registros
}
document.getElementById("btn-configuracoes").addEventListener("click", abrirTelaConfiguracoes);

// Fase 98 — atalhos do menu: abre Configurações já rolada até o cartão pedido, com um destaque curto
function irParaCartaoConfig(ids, aoChegar) {
  abrirTelaConfiguracoes();
  setTimeout(() => {
    const el = ids.map((i) => document.getElementById(i)).find((e) => e && !e.hidden);
    if (!el) { mostrarFeedbackHeader("Esse cartão ainda não está disponível nesta conta", "warn"); return; }
    el.scrollIntoView({ behavior: "smooth", block: "start" });
    el.classList.remove("card-destaque"); void el.offsetWidth; el.classList.add("card-destaque");
    setTimeout(() => el.classList.remove("card-destaque"), 2400);
    if (typeof aoChegar === "function") aoChegar();
  }, 350);
}
document.getElementById("btn-atalho-usuarios")?.addEventListener("click", () =>
  irParaCartaoConfig(state.papel === "convidado" ? ["share-card-guest", "share-card-owner"] : ["share-card-owner", "share-card-guest"]));
document.getElementById("btn-atalho-tampa")?.addEventListener("click", () => {
  menuDropdown.classList.remove("open"); btnMenu.setAttribute("aria-expanded", "false");
  if (typeof window.abrirTelaTampa === "function") window.abrirTelaTampa();
});
document.getElementById("btn-atalho-acessos")?.addEventListener("click", () =>
  irParaCartaoConfig(["ac-card"], () => { if (typeof window.acMostrar === "function") window.acMostrar(); }));
document.getElementById("btn-config-voltar").addEventListener("click", () => {
  showScreen("dashboard");
});

// ═══════════════════════════════════════════════════════════════
// GUIA DE INCUBAÇÃO (Fase 37) — lista de tópicos + detalhe na mesma tela.
// Conteúdo estático em guia-data.js (window.GUIA_TOPICOS, gerado a partir
// da mesma fonte do firmware). 100% local: nenhuma leitura no Firestore,
// então não consome cota. Textos entram via textContent (nada de innerHTML).
// Marcação do corpo: "# " subtítulo, "- " item de lista.
// ═══════════════════════════════════════════════════════════════
const guiaEls = {
  lista: document.getElementById("guia-lista"),
  detalhe: document.getElementById("guia-detalhe"),
  icone: document.getElementById("guia-detalhe-icone"),
  passo: document.getElementById("guia-detalhe-passo"),
  titulo: document.getElementById("guia-detalhe-titulo"),
  corpo: document.getElementById("guia-detalhe-corpo"),
  anterior: document.getElementById("btn-guia-anterior"),
  proximo: document.getElementById("btn-guia-proximo"),
  topbarTitulo: document.getElementById("guia-titulo-topbar"),
};
const GUIA_TOPICOS = window.GUIA_TOPICOS || [];
let guiaTopicoAberto = -1; // -1 = lista

function montarListaGuia() {
  guiaEls.lista.textContent = "";
  GUIA_TOPICOS.forEach((t, i) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "guia-item";
    const ic = document.createElement("span");
    ic.className = "guia-item-icone";
    ic.textContent = t.icone;
    ic.setAttribute("aria-hidden", "true");
    const txt = document.createElement("span");
    txt.className = "guia-item-texto";
    const tit = document.createElement("span");
    tit.className = "guia-item-titulo";
    tit.textContent = `${i + 1}. ${t.titulo}`;
    const res = document.createElement("span");
    res.className = "guia-item-resumo";
    res.textContent = t.resumo;
    txt.append(tit, res);
    const seta = document.createElement("span");
    seta.className = "guia-item-seta";
    seta.textContent = "›";
    seta.setAttribute("aria-hidden", "true");
    btn.append(ic, txt, seta);
    btn.addEventListener("click", () => abrirTopicoGuia(i));
    guiaEls.lista.appendChild(btn);
  });
}

function renderCorpoGuia(linhas) {
  guiaEls.corpo.textContent = "";
  let ul = null;
  linhas.forEach((l) => {
    if (l.startsWith("- ")) {
      if (!ul) {
        ul = document.createElement("ul");
        guiaEls.corpo.appendChild(ul);
      }
      const li = document.createElement("li");
      li.textContent = l.slice(2);
      ul.appendChild(li);
    } else if (l.startsWith("# ")) {
      ul = null;
      const h = document.createElement("h3");
      h.textContent = l.slice(2);
      guiaEls.corpo.appendChild(h);
    } else {
      ul = null;
      const p = document.createElement("p");
      p.textContent = l;
      guiaEls.corpo.appendChild(p);
    }
  });
}

function abrirTopicoGuia(i) {
  if (i < 0 || i >= GUIA_TOPICOS.length) return;
  const t = GUIA_TOPICOS[i];
  if (t.galeria) { abrirGaleriaFases(); return; } // Fase 38 — item especial: abre a galeria por cima
  guiaTopicoAberto = i;
  guiaEls.icone.textContent = t.icone;
  guiaEls.passo.textContent = `Passo ${i + 1} de ${GUIA_TOPICOS.length}`;
  guiaEls.titulo.textContent = t.titulo;
  renderCorpoGuia(t.corpo);
  guiaEls.anterior.disabled = i === 0;
  guiaEls.proximo.disabled = i === GUIA_TOPICOS.length - 1;
  guiaEls.lista.hidden = true;
  guiaEls.detalhe.hidden = false;
  guiaEls.topbarTitulo.textContent = "Guia";
  window.scrollTo(0, 0);
}

function fecharTopicoGuia() {
  guiaTopicoAberto = -1;
  guiaEls.detalhe.hidden = true;
  guiaEls.lista.hidden = false;
  guiaEls.topbarTitulo.textContent = "Guia de Incubação";
  window.scrollTo(0, 0);
}

function abrirGuia() {
  if (!guiaEls.lista.childElementCount) montarListaGuia();
  fecharTopicoGuia();
  showScreen("guia");
}

document.getElementById("btn-guia-voltar").addEventListener("click", () => {
  if (guiaTopicoAberto >= 0) fecharTopicoGuia();
  else showScreen("dashboard");
});
guiaEls.anterior.addEventListener("click", () => abrirTopicoGuia(guiaTopicoAberto - 1));
guiaEls.proximo.addEventListener("click", () => abrirTopicoGuia(guiaTopicoAberto + 1));

// ─── Galeria "Fases do Pintinho" (Fase 38) ─────────────────────
// Lista de arquivos vem de guia-data.js (window.GUIA_FASES, já em ordem
// natural pelo número do dia — gerada por tools/gerar_guia.js no repo do
// firmware). Só as imagens, sem texto. Transição: fade + leve slide
// horizontal (Web Animations API), com pré-carregamento dos vizinhos.
const GUIA_FASES = window.GUIA_FASES || [];
const fasesEls = {
  overlay: document.getElementById("fases-overlay"),
  img: document.getElementById("fases-img"),
  fundo: document.getElementById("fases-fundo"), // Fase 57: cópia desfocada da própria imagem atrás dela (no lugar do preto sólido)
};
let faseAtual = 0;
let faseTrocando = false;
const fasesPreload = new Map();

function preloadFase(i) {
  if (i < 0 || i >= GUIA_FASES.length || fasesPreload.has(i)) return;
  const im = new Image();
  im.src = GUIA_FASES[i];
  fasesPreload.set(i, im);
}

function mostrarFase(i, direcao) {
  if (i < 0 || i >= GUIA_FASES.length || faseTrocando) return;
  const aplicar = () => {
    faseAtual = i;
    fasesEls.img.src = GUIA_FASES[i];
    fasesEls.fundo.style.backgroundImage = `url("${GUIA_FASES[i]}")`;
    preloadFase(i - 1);
    preloadFase(i + 1);
    if (direcao && fasesEls.img.animate && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      fasesEls.img.animate(
        [
          { opacity: 0, transform: `translateX(${direcao * 6}%)` },
          { opacity: 1, transform: "translateX(0)" },
        ],
        { duration: 260, easing: "ease-out" }
      );
    }
    faseTrocando = false;
  };
  if (!direcao) { aplicar(); return; }
  // sai (fade curto) → troca → entra (animação acima)
  // (setTimeout, não onfinish: não depende da animação rodar — travava a
  // galeria com a aba em segundo plano)
  faseTrocando = true;
  if (fasesEls.img.animate) fasesEls.img.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 110, fill: "forwards" });
  setTimeout(() => {
    fasesEls.img.getAnimations().forEach((a) => a.cancel());
    aplicar();
  }, 115);
}

function abrirGaleriaFases() {
  if (!GUIA_FASES.length) return;
  fasesEls.overlay.hidden = false;
  document.body.style.overflow = "hidden";
  mostrarFase(0, 0);
}

function fecharGaleriaFases() {
  fasesEls.overlay.hidden = true;
  fasesEls.img.removeAttribute("src");
  fasesEls.fundo.style.backgroundImage = "";
  document.body.style.overflow = "";
}

document.getElementById("fases-prev").addEventListener("click", () => mostrarFase(faseAtual - 1, -1));
document.getElementById("fases-next").addEventListener("click", () => mostrarFase(faseAtual + 1, +1));
document.getElementById("fases-fechar").addEventListener("click", fecharGaleriaFases);
document.addEventListener("keydown", (e) => {
  if (fasesEls.overlay.hidden) return;
  if (e.key === "ArrowLeft") mostrarFase(faseAtual - 1, -1);
  else if (e.key === "ArrowRight") mostrarFase(faseAtual + 1, +1);
  else if (e.key === "Escape") fecharGaleriaFases();
});

const cardGuia = document.getElementById("card-guia");
cardGuia.addEventListener("click", abrirGuia);
cardGuia.addEventListener("keydown", (e) => {
  if (e.key === "Enter" || e.key === " ") { e.preventDefault(); abrirGuia(); }
});

// ═══════════════════════════════════════════════════════════════
// ARQUIVOS DO CARTÃO SD (Fase 41)
// O PWA não enxerga o SD direto. Fluxo:
//  1. o firmware publica o ÍNDICE em chocadeiras/{id}/arquivos/indice
//     (boot, mudanças locais e quando pedimos "*") — escutado aqui;
//  2. ao tocar num arquivo, escutamos arquivos/conteudo e escrevemos
//     arquivoReq = "<idDoPedido>|<caminho>" no doc da chocadeira; o firmware
//     responde com o arquivo em base64 (≤150 KB; logs grandes vêm pelo fim);
//  3. mostramos a prévia e oferecemos Compartilhar (Web Share API com
//     arquivo) — sem suporte, cai em Baixar.
// Cota: tudo sob demanda (1 leitura por mudança de índice; 1 escrita + 1
// leitura por arquivo aberto). Nada periódico.
// ═══════════════════════════════════════════════════════════════
const ARQ_LIMITE_BYTES = 150000;
const ARQ_TIMEOUT_MS = 45000;
let arqRefreshT0 = 0;     // Fase 44 — instante do último pedido de atualização da lista (0 = nenhum em andamento)
let arqRefreshTimeout = null;
let arqAutoPediu = false; // pede a lista sozinho UMA vez por sessão se o aparelho nunca publicou
const arqEls = {
  lista: document.getElementById("arq-lista"),
  sub: document.getElementById("arq-sub"),
  modal: document.getElementById("modal-arquivo-backdrop"),
  titulo: document.getElementById("arq-modal-titulo"),
  status: document.getElementById("arq-modal-status"),
  preview: document.getElementById("arq-modal-preview"),
  btnBaixar: document.getElementById("arq-btn-baixar"),
  btnCompartilhar: document.getElementById("arq-btn-compartilhar"),
};
let arqBlobAtual = null; // { blob, nome, mime }
let arqUnsubConteudo = null;
let arqTimeout = null;
let arqUrlAtual = null;

function formatarBytes(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1048576) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1048576).toFixed(1)} MB`;
}

function iconeArquivo(nome) {
  if (/\.(jpe?g|png|gif|bmp|webp)$/i.test(nome)) return "🖼️";
  if (/\.(log|txt|old|cfg|csv)$/i.test(nome)) return "📄";
  return "📎";
}

function escutarIndiceArquivos() {
  if (state.unsubscribeArquivos || !state.chocadeiraId) return;
  state.unsubscribeArquivos = db
    .collection("chocadeiras").doc(state.chocadeiraId)
    .collection("arquivos").doc("indice")
    .onSnapshot(
      (snap) => renderIndiceArquivos(snap.exists ? snap.data() : null),
      (err) => {
        console.error("Erro ao escutar índice de arquivos:", err);
        arqEls.sub.textContent = "Não foi possível carregar a lista de arquivos.";
      }
    );
}

function arqRefreshConcluido() {
  arqRefreshT0 = 0;
  if (arqRefreshTimeout) { clearTimeout(arqRefreshTimeout); arqRefreshTimeout = null; }
  if (window.arqProg) window.arqProg.fim("Lista atualizada.", true);
}
function renderIndiceArquivos(dados) {
  arqEls.lista.textContent = "";
  const quandoPub = dados && dados.atualizadoEm && dados.atualizadoEm.toDate ? dados.atualizadoEm.toDate() : null;
  if (arqRefreshT0 && quandoPub && quandoPub.getTime() >= arqRefreshT0 - 5000) arqRefreshConcluido();
  if (!dados) {
    arqEls.sub.textContent = "O dispositivo ainda não publicou a lista.";
    if (!arqAutoPediu && !arqRefreshT0 && state.estadoConexao === "online") { arqAutoPediu = true; pedirAtualizacaoIndice(); }
    const v = document.createElement("div");
    v.className = "logs-empty";
    v.textContent = "Nenhum arquivo ainda.";
    arqEls.lista.appendChild(v);
    return;
  }
  let itens = [];
  try { itens = JSON.parse(dados.json || "[]"); } catch (e) { itens = []; }
  itens.sort((a, b) => a.p.localeCompare(b.p, "pt-BR", { numeric: true }));
  const quando = dados.atualizadoEm && dados.atualizadoEm.toDate ? dados.atualizadoEm.toDate().toLocaleString("pt-BR") : "--";
  arqEls.sub.textContent = `${itens.length} arquivo(s) · SD: ${dados.usadoMB ?? "?"} de ${dados.totalMB ?? "?"} MB usados · lista de ${quando}`;
  if (!itens.length) {
    const v = document.createElement("div");
    v.className = "logs-empty";
    v.textContent = "Cartão sem arquivos.";
    arqEls.lista.appendChild(v);
    return;
  }
  itens.forEach((it) => {
    const nome = it.p.split("/").pop();
    const pasta = it.p.slice(0, it.p.length - nome.length) || "/";
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "arq-item";
    const ic = document.createElement("span");
    ic.className = "arq-item-icone";
    ic.textContent = iconeArquivo(nome);
    ic.setAttribute("aria-hidden", "true");
    const tx = document.createElement("span");
    tx.className = "arq-item-texto";
    const n1 = document.createElement("span");
    n1.className = "arq-item-nome";
    n1.textContent = nome;
    const n2 = document.createElement("span");
    n2.className = "arq-item-sub";
    n2.textContent = `${pasta} · ${formatarBytes(it.s)}`;
    tx.append(n1, n2);
    btn.append(ic, tx);
    btn.addEventListener("click", () => abrirArquivoSD(it));
    arqEls.lista.appendChild(btn);
  });
}

function limparPreviaArquivo() {
  if (arqUrlAtual) { URL.revokeObjectURL(arqUrlAtual); arqUrlAtual = null; }
  arqEls.preview.textContent = "";
  arqBlobAtual = null;
  arqEls.btnBaixar.disabled = true;
  arqEls.btnCompartilhar.disabled = true;
}

function fecharModalArquivo() {
  if (window.arqModalProg) window.arqModalProg.esconder();
  if (arqUnsubConteudo) { arqUnsubConteudo(); arqUnsubConteudo = null; }
  if (arqTimeout) { clearTimeout(arqTimeout); arqTimeout = null; }
  arqEls.modal.hidden = true;
  limparPreviaArquivo();
}

function base64ParaBlob(b64, mime) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return new Blob([bytes], { type: mime });
}

async function abrirArquivoSD(item) {
  const nome = item.p.split("/").pop();
  limparPreviaArquivo();
  arqEls.titulo.textContent = nome;
  arqEls.modal.hidden = false;
  const ehTexto = /\.(log|txt|old|cfg|csv)$/i.test(nome);
  if (item.s > ARQ_LIMITE_BYTES && !ehTexto) {
    arqEls.status.textContent = `Arquivo de ${formatarBytes(item.s)} — o limite pelo app é 150 KB. Copie pelo leitor de cartão.`;
    return;
  }
  if (state.estadoConexao !== "online") {
    arqEls.status.textContent = "Chocadeira offline — não é possível buscar o arquivo agora.";
    return;
  }
  arqEls.status.textContent = "";
  const estSeg = 6 + Math.ceil((item.s || 0) / 30000); // 2,5s de poll + leitura do SD + envio (~30 KB/s)
  const cancelarBusca = () => {
    if (arqUnsubConteudo) { arqUnsubConteudo(); arqUnsubConteudo = null; }
    if (arqTimeout) { clearTimeout(arqTimeout); arqTimeout = null; }
    arqEls.status.textContent = "Busca cancelada.";
  };
  window.arqModalProg.iniciar("Buscando no dispositivo…", estSeg, cancelarBusca);
  const reqId = Date.now().toString(36);
  const ref = db.collection("chocadeiras").doc(state.chocadeiraId);
  if (arqUnsubConteudo) arqUnsubConteudo();
  // escuta ANTES de pedir; ignora qualquer snapshot de outro pedido (o doc guarda o último arquivo)
  arqUnsubConteudo = ref.collection("arquivos").doc("conteudo").onSnapshot(
    (snap) => {
      const d = snap.exists ? snap.data() : null;
      if (!d || d.reqId !== reqId) return;
      if (arqTimeout) { clearTimeout(arqTimeout); arqTimeout = null; }
      if (arqUnsubConteudo) { arqUnsubConteudo(); arqUnsubConteudo = null; }
      window.arqModalProg.esconder();
      mostrarConteudoArquivo(nome, d);
    },
    (err) => {
      console.error("Erro ao escutar conteúdo do arquivo:", err);
      window.arqModalProg.fim("Erro ao receber o arquivo.", false);
    }
  );
  arqTimeout = setTimeout(() => {
    if (arqUnsubConteudo) { arqUnsubConteudo(); arqUnsubConteudo = null; }
    window.arqModalProg.fim("O dispositivo não respondeu em 45 s (offline, sem internet ou ocupado). Tente de novo.", false);
  }, ARQ_TIMEOUT_MS);
  try {
    await ref.update({ arquivoReq: `${reqId}|${item.p}` });
  } catch (e) {
    console.error("Erro ao pedir arquivo:", e);
    if (arqTimeout) { clearTimeout(arqTimeout); arqTimeout = null; }
    if (arqUnsubConteudo) { arqUnsubConteudo(); arqUnsubConteudo = null; }
    window.arqModalProg.fim("Não foi possível enviar o pedido.", false);
  }
}

function mostrarConteudoArquivo(nome, d) {
  if (d.erro) { arqEls.status.textContent = d.erro; return; }
  const mime = d.mime || "application/octet-stream";
  let blob;
  try { blob = base64ParaBlob(d.b64 || "", mime); } catch (e) { arqEls.status.textContent = "Arquivo recebido corrompido."; return; }
  arqBlobAtual = { blob, nome, mime };
  arqEls.status.textContent = `${formatarBytes(blob.size)}${d.truncado ? " — mostrando só o FINAL do arquivo (o original é maior)" : ""}`;
  if (mime.startsWith("image/")) {
    arqUrlAtual = URL.createObjectURL(blob);
    const img = document.createElement("img");
    img.src = arqUrlAtual;
    img.alt = nome;
    img.className = "arq-preview-img";
    arqEls.preview.appendChild(img);
  } else if (mime.startsWith("text/")) {
    blob.text().then((t) => {
      const pre = document.createElement("pre");
      pre.className = "arq-preview-texto";
      pre.textContent = t;
      arqEls.preview.appendChild(pre);
      pre.scrollTop = pre.scrollHeight; // logs: o mais recente fica no fim
    });
  }
  arqEls.btnBaixar.disabled = false;
  arqEls.btnCompartilhar.disabled = false;
}

function baixarArquivoAtual() {
  if (!arqBlobAtual) return;
  const url = URL.createObjectURL(arqBlobAtual.blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = arqBlobAtual.nome;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

async function compartilharArquivoAtual() {
  if (!arqBlobAtual) return;
  const { blob, nome, mime } = arqBlobAtual;
  const file = new File([blob], nome, { type: mime });
  // Web Share API com arquivo (Android/iOS/Chrome recentes). Sem suporte → baixa.
  if (navigator.canShare && navigator.canShare({ files: [file] })) {
    try {
      await navigator.share({ files: [file], title: nome });
    } catch (e) {
      if (e && e.name === "AbortError") return; // usuário fechou a folha de compartilhar
      console.error("Falha no compartilhamento, baixando no lugar:", e);
      arqEls.status.textContent = "Não foi possível compartilhar — baixando o arquivo.";
      baixarArquivoAtual();
    }
  } else {
    arqEls.status.textContent = "Este navegador não compartilha arquivos — baixando.";
    baixarArquivoAtual();
  }
}

document.getElementById("arq-modal-close").addEventListener("click", fecharModalArquivo);
arqEls.modal.addEventListener("click", (e) => { if (e.target === arqEls.modal) fecharModalArquivo(); });
arqEls.btnBaixar.addEventListener("click", baixarArquivoAtual);
arqEls.btnCompartilhar.addEventListener("click", compartilharArquivoAtual);
async function pedirAtualizacaoIndice() {
  if (!state.chocadeiraId) return;
  if (state.estadoConexao !== "online") { arqEls.sub.textContent = "Chocadeira offline — não dá pra atualizar a lista agora."; return; }
  if (arqRefreshT0) return; // já tem um pedido em andamento
  arqRefreshT0 = Date.now();
  window.arqProg.iniciar("Pedindo a lista ao dispositivo…", 8, () => {
    arqRefreshT0 = 0;
    if (arqRefreshTimeout) { clearTimeout(arqRefreshTimeout); arqRefreshTimeout = null; }
  });
  arqRefreshTimeout = setTimeout(() => {
    arqRefreshT0 = 0; arqRefreshTimeout = null;
    window.arqProg.fim("O aparelho não respondeu em 45 s — confira se está online e com o cartão SD. Toque em atualizar para tentar de novo.", false);
  }, ARQ_TIMEOUT_MS);
  try {
    await db.collection("chocadeiras").doc(state.chocadeiraId).update({ arquivoReq: `${Date.now().toString(36)}|*` });
  } catch (e) {
    console.error("Erro ao pedir atualização do índice:", e);
    arqRefreshT0 = 0;
    if (arqRefreshTimeout) { clearTimeout(arqRefreshTimeout); arqRefreshTimeout = null; }
    window.arqProg.fim("Não foi possível enviar o pedido.", false);
  }
}
document.getElementById("btn-arq-refresh").addEventListener("click", pedirAtualizacaoIndice);

// Bug do falso "offline" (2026-09-20) — investigado com log de Serial real
// do firmware: heartbeat sendo enviado e confirmado a cada 30s sem falha
// nenhuma por 4+ minutos seguidos, WiFi nunca caiu. Ou seja, o problema
// NÃO é o firmware — é o listener onSnapshot do PWA ficando parado/atrasado
// (comportamento conhecido do SDK do Firestore em aba em segundo plano por
// muito tempo, notebook suspenso, trocas de rede, blips de conexão etc.),
// fazendo state.firmwareLastSeen ficar velho mesmo com o servidor
// atualizado. Corrigido com três camadas (o onSnapshot continua sendo a
// fonte principal/instantânea de atuadores/ciclo, isso é reforço pro status):
// 1. Ao voltar pra aba OU pra janela (visibilitychange + focus — dois
//    eventos, mais chance de pegar o retorno em qualquer navegador/PWA
//    instalado), busca o status direto do SERVIDOR (bypassa cache local).
// 2. Reforço periódico (45s) independente de foco, caso o listener fique
//    parado sem nenhum evento de visibilidade/foco pra disparar a correção
//    (setInterval pode ser desacelerado em aba de fundo, mas ainda roda).
// 3. A cada 5min, o listener onSnapshot inteiro é reconectado do zero
//    (escutarAtuadores já cuida de cancelar o anterior antes) — se a
//    conexão realtime tiver ficado "zumbi" silenciosamente (sem disparar
//    nenhum erro visível), isso força uma conexão nova periodicamente em
//    vez de confiar que ela nunca trava.
function reconferirStatusDoServidor() {
  if (!state.chocadeiraId) return;
  db.collection("chocadeiras")
    .doc(state.chocadeiraId)
    .get({ source: "server" })
    .then((snap) => {
      const dados = snap.data();
      if (dados && dados.firmwareLastSeen) {
        state.firmwareLastSeen = dados.firmwareLastSeen.toDate();
        atualizarStatusOnline();
      }
    })
    .catch((err) => console.error("Erro ao reconferir status direto do servidor:", err));
}

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") reconferirStatusDoServidor();
});
window.addEventListener("focus", reconferirStatusDoServidor);

// Fase 63 — economia de leituras do Firestore:
//  * "cmd.vivo": avisa o aparelho que há um app aberto (ele acelera o polling só enquanto isso; sem app aberto poll lento).
//  * app em segundo plano por > 90 s: solta os listeners (cada mudança do documento custa 1 leitura POR listener); ao voltar, reabre.
let escutasPausadas = false, pausaTimer = null;
function vivoPing() {
  if (!state.chocadeiraId || document.hidden) return;
  db.collection("chocadeiras").doc(state.chocadeiraId).update({ "cmd.vivo": String(Date.now()) }).catch(() => {});
}
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    clearTimeout(pausaTimer);
    pausaTimer = setTimeout(() => {
      if (!document.hidden || !state.chocadeiraId) return;
      ["unsubscribeAtuadores", "unsubscribeArquivos", "unsubscribeLogs"].forEach((k) => { if (state[k]) { state[k](); state[k] = null; } });
      escutasPausadas = true;
    }, 90000);
  } else {
    clearTimeout(pausaTimer);
    if (escutasPausadas && state.chocadeiraId) { escutasPausadas = false; escutarAtuadores(state.chocadeiraId); }
    else vivoPing();
  }
});
setInterval(() => {
  if (state.chocadeiraId && !document.hidden) escutarAtuadores(state.chocadeiraId);
}, 300000);

// ═══════════════════════════════════════════════════════════════
// MODAL DE SENSOR (Fase 22) — gráfico do dia (00:00 de hoje até agora),
// aberto ao clicar nos cards de Temperatura/Umidade (ver
// .card-clickable[data-sensor] no index.html). Mesma coleção que já
// alimenta os cards (leituras/), agregando a janela do dia em vez do
// último valor. Diferente da Fase 21: agora é uma janela ANCORADA na
// meia-noite local (não mais "últimas 24h" rolante) -- acumula o dia
// inteiro e reseta sozinho à meia-noite (reabrindo o modal depois da
// virada já busca a partir do novo 00:00) -- e atualiza AO VIVO via
// onSnapshot em vez de uma busca única (fecharModalSensor cancela o
// listener, senão ficaria consultando pra sempre em segundo plano).
// ═══════════════════════════════════════════════════════════════
let unsubscribeModalSensor = null;
let modalSensorTipoAberto = null;

document.querySelectorAll(".card-clickable[data-sensor]").forEach((el) => {
  el.addEventListener("click", () => abrirModalSensor(el.dataset.sensor));
  el.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") { e.preventDefault(); abrirModalSensor(el.dataset.sensor); }
  });
});

function inicioDoDiaLocal() {
  const d = new Date();
  d.setHours(0, 0, 0, 0);
  return d;
}

function abrirModalSensor(tipo, manterAba) {
  if (!state.chocadeiraId) return;
  if (!manterAba && typeof sensorModalReset === "function") sensorModalReset(); // Fase 47 — volta pra aba "Hoje"
  const faixa = FAIXAS[tipo];
  const titulo = { temperatura: "Temperatura", umidade: "Umidade", co2: "CO₂" }[tipo] || tipo;
  modalSensorTipoAberto = tipo;

  document.getElementById("sensor-modal-titulo").textContent = titulo;
  document.getElementById("sensor-modal-faixa").textContent = tipo === "co2"
    ? `Ideal: até ${faixa.max}${faixa.unidade} · alerta acima de ${faixa.max + faixa.margem}${faixa.unidade}`
    : `Faixa ideal: ${faixa.min}–${faixa.max}${faixa.unidade}`;
  document.getElementById("sensor-modal-min").textContent = "--";
  document.getElementById("sensor-modal-max").textContent = "--";
  document.getElementById("sensor-modal-media").textContent = "--";
  document.getElementById("sensor-modal-contagem").textContent = "carregando...";
  document.getElementById("sensor-modal-live").hidden = true;
  desenharSparkline([], faixa);
  document.getElementById("sensor-modal-backdrop").hidden = false;

  if (unsubscribeModalSensor) unsubscribeModalSensor();
  unsubscribeModalSensor = db
    .collection("chocadeiras")
    .doc(state.chocadeiraId)
    .collection("leituras")
    .where("timestamp", ">=", firebase.firestore.Timestamp.fromDate(inicioDoDiaLocal()))
    .orderBy("timestamp", "asc")
    .onSnapshot(
      (snap) => atualizarModalSensor(snap, tipo, faixa),
      (err) => {
        console.error("Erro ao escutar historico do sensor:", err);
        document.getElementById("sensor-modal-contagem").textContent = "Erro ao carregar histórico.";
      }
    );
}

// Fase 23 — sem valor gigante aqui (removido: o card do dashboard atrás
// do modal já mostra o valor atual e sua cor por faixa, era redundante).
// O rodapé virou "N leituras" + indicador LIVE (ponto pulsante via CSS)
// em vez da frase "Hoje — N leituras, ao vivo".
function atualizarModalSensor(snap, tipo, faixa) {
  const pontos = snap.docs
    .map((d) => ({ valor: d.data()[tipo], quando: d.data().timestamp?.toDate() }))
    .filter((p) => typeof p.valor === "number" && p.quando);

  if (pontos.length === 0) {
    document.getElementById("sensor-modal-contagem").textContent = tipo === "co2"
      ? "Sem leituras de CO₂ hoje ainda (o sensor aquece ~1 min e o aparelho envia a cada 5 min)."
      : "Sem leituras hoje ainda.";
    document.getElementById("sensor-modal-live").hidden = true;
    return;
  }

  const valores = pontos.map((p) => p.valor);
  const min = Math.min(...valores);
  const max = Math.max(...valores);
  const media = valores.reduce((a, b) => a + b, 0) / valores.length;

  const dec = faixa.dec ?? 1;
  document.getElementById("sensor-modal-min").textContent = `${min.toFixed(dec)}${faixa.unidade}`;
  document.getElementById("sensor-modal-max").textContent = `${max.toFixed(dec)}${faixa.unidade}`;
  document.getElementById("sensor-modal-media").textContent = `${media.toFixed(dec)}${faixa.unidade}`;
  document.getElementById("sensor-modal-contagem").textContent = `${pontos.length} leituras hoje`;
  document.getElementById("sensor-modal-live").hidden = false;

  desenharSparkline(pontos, faixa);
}

function fecharModalSensor() {
  if (typeof sensorModalReset === "function") sensorModalReset();
  document.getElementById("sensor-modal-backdrop").hidden = true;
  if (unsubscribeModalSensor) { unsubscribeModalSensor(); unsubscribeModalSensor = null; }
  modalSensorTipoAberto = null;
}

document.getElementById("sensor-modal-close").addEventListener("click", fecharModalSensor);
document.getElementById("sensor-modal-backdrop").addEventListener("click", (e) => {
  if (e.target.id === "sensor-modal-backdrop") fecharModalSensor();
});

// Gráfico em canvas puro (sem lib) — Fase 23: escala do eixo Y agora
// prioriza a variação REAL dos dados (não força mais a janela visível a
// cobrir a faixa ideal inteira). Antes, quando os valores reais ficavam
// bem longe da faixa ideal (ex.: umidade em 42% com ideal 45-65%), a
// escala esticava pra caber os dois, e a variação real (41-42%) ficava
// achatada/invisível numa linha reta no fundo do gráfico -- foi
// literalmente o que apareceu numa foto real do usuário. Agora a faixa
// ideal é desenhada só onde ela cai dentro da janela visível (pode sumir
// da tela se estiver longe demais, o que é o comportamento certo: dado
// real sempre em destaque). Mais grades e marcas de hora (5 em vez de
// 2) no gráfico maior, pra mais detalhe.
function desenharSparkline(pontos, faixa, canvasEl) {
  const canvas = canvasEl || document.getElementById("sensor-modal-sparkline");
  const dpr = window.devicePixelRatio || 1;
  const cssW = canvas.clientWidth || 300;
  const cssH = canvas.clientHeight || 260;
  canvas.width = cssW * dpr;
  canvas.height = cssH * dpr;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  const padL = 40, padR = 10, padT = 10, padB = 20;
  const plotW = cssW - padL - padR;
  const plotH = cssH - padT - padB;

  const valores = pontos.map((p) => p.valor);
  const dataMin = valores.length ? Math.min(...valores) : faixa.min;
  const dataMax = valores.length ? Math.max(...valores) : faixa.max;
  const folga = Math.max((dataMax - dataMin) * 0.15, faixa.folgaMin ?? 0.2);
  const yMin = dataMin - folga, yMax = dataMax + folga;
  const span = Math.max(yMax - yMin, 0.1);
  const yFor = (v) => padT + plotH - ((v - yMin) / span) * plotH;

  // Grade horizontal + rótulos de valor (5 linhas, escala = variação real dos dados)
  ctx.strokeStyle = "rgba(100, 116, 139, 0.18)";
  ctx.fillStyle = "#64748b";
  ctx.font = "10px " + getComputedStyle(document.body).fontFamily;
  ctx.textBaseline = "middle";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const v = yMin + (span * i) / 4;
    const y = yFor(v);
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();
    ctx.textAlign = "right";
    ctx.fillText(v.toFixed(faixa.dec ?? 1), padL - 6, y);
  }

  // Banda da faixa ideal -- só desenha a parte que cai dentro da janela
  // visível (pode não aparecer nada se os dados estiverem longe do ideal,
  // e está certo: o gráfico prioriza mostrar a variação real).
  if (faixa.max >= yMin && faixa.min <= yMax) {
    const yIdealTop = yFor(Math.min(faixa.max, yMax));
    const yIdealBottom = yFor(Math.max(faixa.min, yMin));
    ctx.fillStyle = "rgba(34, 197, 94, 0.12)";
    ctx.fillRect(padL, yIdealTop, plotW, yIdealBottom - yIdealTop);
  }

  if (pontos.length < 2) {
    ctx.fillStyle = "#64748b";
    ctx.textAlign = "center";
    ctx.fillText("Aguardando mais leituras...", padL + plotW / 2, padT + plotH / 2);
    return;
  }

  const xFor = (i) => padL + (i / (pontos.length - 1)) * plotW;

  // Marcas de hora no eixo X -- 5 pontos espalhados (era só inicio/fim)
  ctx.fillStyle = "#64748b";
  ctx.textAlign = "center";
  for (let i = 0; i <= 4; i++) {
    const idx = Math.round((i / 4) * (pontos.length - 1));
    const q = pontos[idx].quando;
    const multiDia = pontos[pontos.length - 1].quando - pontos[0].quando > 30 * 3600 * 1000; // Fase 47 — histórico de 7 dias mostra dia/mês
    const hora = multiDia
      ? q.toLocaleDateString("pt-BR", { day: "2-digit", month: "2-digit" })
      : q.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
    ctx.textAlign = i === 0 ? "left" : i === 4 ? "right" : "center";
    ctx.fillText(hora, xFor(idx), cssH - 6);
  }

  // Fase 24 — cor da linha/área muda conforme a faixa do valor MAIS
  // RECENTE (mesmas 3 cores já usadas nos cards/valores: ideal=verde,
  // alerta=laranja, crítico=vermelho) -- reforça visualmente o mesmo
  // estado que corPorFaixa() já calcula pro resto da UI.
  const estado = corPorFaixa(pontos[pontos.length - 1].valor, faixa);
  const corHex = estado === "critico" ? "#ef4444" : estado === "alerta" ? "#f59e0b" : "#22c55e";
  const corRgb = estado === "critico" ? "239, 68, 68" : estado === "alerta" ? "245, 158, 11" : "34, 197, 94";

  // Área preenchida com gradiente sob a linha
  const grad = ctx.createLinearGradient(0, padT, 0, padT + plotH);
  grad.addColorStop(0, `rgba(${corRgb}, 0.28)`);
  grad.addColorStop(1, `rgba(${corRgb}, 0)`);
  ctx.beginPath();
  ctx.moveTo(xFor(0), padT + plotH);
  pontos.forEach((p, i) => ctx.lineTo(xFor(i), yFor(p.valor)));
  ctx.lineTo(xFor(pontos.length - 1), padT + plotH);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Linha
  ctx.strokeStyle = corHex;
  ctx.lineWidth = 2;
  ctx.beginPath();
  pontos.forEach((p, i) => {
    const x = xFor(i), y = yFor(p.valor);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Ponto destacado no valor mais recente, com o valor anotado ao lado
  // (substitui o "valor gigante" removido do topo do modal -- o dado
  // mais importante continua visível, só que junto do gráfico).
  const ultimoX = xFor(pontos.length - 1), ultimoY = yFor(pontos[pontos.length - 1].valor);
  ctx.fillStyle = `rgba(${corRgb}, 0.25)`;
  ctx.beginPath(); ctx.arc(ultimoX, ultimoY, 7, 0, Math.PI * 2); ctx.fill();
  ctx.fillStyle = corHex;
  ctx.beginPath(); ctx.arc(ultimoX, ultimoY, 3.5, 0, Math.PI * 2); ctx.fill();

  ctx.font = "bold 13px " + getComputedStyle(document.body).fontFamily;
  const rotulo = `${pontos[pontos.length - 1].valor.toFixed(faixa.dec ?? 1)}${faixa.unidade}`;
  const rotuloW = ctx.measureText(rotulo).width;
  const rotuloEncosta = ultimoX + 10 + rotuloW > padL + plotW;
  ctx.textAlign = rotuloEncosta ? "right" : "left";
  ctx.fillStyle = "#e2e8f0";
  ctx.fillText(rotulo, rotuloEncosta ? ultimoX - 10 : ultimoX + 10, Math.max(ultimoY, padT + 8));
}

// ═══════════════════════════════════════════════════════════════
// LOGS (Fase 21, redesenhado na Fase 29) — Configurações → seção Logs.
// Eventos "importantes" que o firmware sincroniza do cartão SD pro
// Firestore (chocadeiras/{id}/logs) -- boot/reset, WiFi cair/voltar,
// comando de desligar/reiniciar. O log COMPLETO (incl. toggle de
// atuador, rotineiro demais pra sincronizar) só existe no SD local do
// dispositivo (tela Logs do firmware).
//
// O listener traz um lote generoso (LOGS_FETCH_LIMIT) já ordenado do
// mais recente pro mais antigo; busca e paginação são 100% client-side
// em cima desse lote (sem cursors do Firestore) -- mais simples e mais
// rápido de responder ao toque que ida-e-volta de rede a cada página.
// ═══════════════════════════════════════════════════════════════
const LOGS_FETCH_LIMIT = 60;   // Fase 63: era 200 (cada abertura lia 200 docs)
const LOGS_PAGE_SIZE = 15;
// Fase 30 — "auto-refresh" tinha virado um setInterval que reassinava
// este listener a cada 60s, forçando uma releitura completa dos até
// LOGS_FETCH_LIMIT documentos toda vez -- isso, com a aba aberta o dia
// inteiro, é fácil o bastante pra estourar a cota diária GRATUITA de
// leituras do Firestore (50 mil/dia) sozinho, e foi exatamente o que
// aconteceu: o Serial do firmware passou a mostrar erro 429
// (RESOURCE_EXHAUSTED) em QUALQUER leitura -- inclusive o poll de
// atuadores/comandoDesligar/comandoReiniciar, por isso "nada funcionava"
// (não é regressão de código nesses botões, é cota estourada). Removido:
// o `onSnapshot` já empurra eventos novos sozinho em tempo real, sem
// nenhum custo extra de "atualizar" — não tinha necessidade nenhuma de
// reassinar por conta própria. "Sincronizar" continua existindo como
// ação MANUAL (rara, um clique por vez), que é barata de verdade.
//
// resetFiltros=false (usado só pelo botão "Sincronizar") reassina o
// listener sem perder o que o usuário já tinha digitado na busca/
// selecionado no filtro de tipo nem a página atual -- só o `true`
// (abertura normal da tela) começa do zero.
function escutarLogs(chocadeiraId, resetFiltros = true) {
  if (state.unsubscribeLogs) state.unsubscribeLogs();
  state.logsRaw = state.logsRaw || [];
  state.logsFiltrados = state.logsFiltrados || [];
  if (resetFiltros) {
    state.logsRaw = [];
    state.logsFiltrados = [];
    state.logsPage = 0;
    state.logsSearchTermo = "";
    state.logsFiltroTipo = "";
    const filtroSelect = document.getElementById("logs-filtro-tipo");
    if (filtroSelect) filtroSelect.value = "";
    const searchInput = document.getElementById("logs-search-input");
    const searchRow = document.getElementById("logs-search-row");
    const searchBtn = document.getElementById("btn-logs-search");
    if (searchInput) searchInput.value = "";
    if (searchRow) searchRow.hidden = true;
    if (searchBtn) searchBtn.classList.remove("active");
    state.logsDia = "";
    const diaInput = document.getElementById("logs-filtro-dia");
    if (diaInput) diaInput.value = "";
    const diaLimpar = document.getElementById("btn-logs-dia-limpar");
    if (diaLimpar) diaLimpar.hidden = true;
  }
  // Dia escolhido: busca única (sem listener ao vivo) -- os mais recentes (60) não alcançam os logs de ontem
  if (state.logsDia) { carregarLogsDoDia(chocadeiraId, state.logsDia); return; }
  logsSubPadrao();
  state.unsubscribeLogs = db
    .collection("chocadeiras")
    .doc(chocadeiraId)
    .collection("logs")
    .orderBy("timestamp", "desc")
    .limit(LOGS_FETCH_LIMIT)
    .onSnapshot(
      (snap) => {
        state.logsRaw = snap.docs.map((d) => d.data());
        aplicarFiltroLogs();
      },
      (err) => console.error("Erro ao escutar logs:", err)
    );
}

// Fase 94 — logs de um dia específico. O listener ao vivo só traz os 60 mais
// recentes; com a automação registrando resumos e alertas, os logs de "ontem"
// ficavam fora do lote e pareciam não existir. Aqui a consulta é por intervalo
// de tempo (um dia local), UMA leitura sob demanda (até LOGS_DIA_LIMIT docs).
const LOGS_DIA_LIMIT = 300;
const LOGS_SUB_TEXTO = "Eventos sincronizados do dispositivo (log completo fica no cartão SD do firmware)";
function logsSubPadrao() {
  const sub = document.getElementById("logs-sub");
  if (sub) sub.textContent = LOGS_SUB_TEXTO;
}
async function carregarLogsDoDia(chocadeiraId, ymd) {
  const partes = String(ymd).split("-").map(Number);
  if (partes.length !== 3 || partes.some((n) => !Number.isFinite(n))) return;
  const [a, m, d] = partes;
  const ini = new Date(a, m - 1, d, 0, 0, 0, 0);
  const fim = new Date(a, m - 1, d + 1, 0, 0, 0, 0);
  const sub = document.getElementById("logs-sub");
  if (sub) sub.textContent = "Carregando os logs do dia…";
  try {
    const snap = await db
      .collection("chocadeiras").doc(chocadeiraId).collection("logs")
      .where("timestamp", ">=", firebase.firestore.Timestamp.fromDate(ini))
      .where("timestamp", "<", firebase.firestore.Timestamp.fromDate(fim))
      .orderBy("timestamp", "desc")
      .limit(LOGS_DIA_LIMIT)
      .get();
    if (state.logsDia !== ymd) return;   // o usuário trocou o dia enquanto carregava
    state.logsRaw = snap.docs.map((doc) => doc.data());
    state.logsPage = 0;
    aplicarFiltroLogs();
    const dd = `${String(d).padStart(2, "0")}/${String(m).padStart(2, "0")}/${a}`;
    if (sub) {
      sub.textContent = snap.size === 0
        ? `Nenhum evento sincronizado em ${dd}.`
        : `${snap.size} evento(s) em ${dd}${snap.size >= LOGS_DIA_LIMIT ? ` (mostrando os ${LOGS_DIA_LIMIT} mais recentes do dia)` : ""}`;
    }
  } catch (err) {
    console.error("Erro ao carregar logs do dia:", err);
    if (sub) sub.textContent = "Não foi possível carregar os logs desse dia.";
  }
}

function categoriaClasse(categoria) {
  const c = (categoria || "").toLowerCase();
  if (c === "wifi") return "log-cat-wifi";
  if (c === "boot") return "log-cat-boot";
  if (c === "desligar") return "log-cat-desligar";
  if (c === "reiniciar") return "log-cat-reiniciar";
  if (c === "atuador") return "log-cat-atuador";
  if (c === "sensor") return "log-cat-sensor";
  if (c === "alerta") return "log-cat-alerta";
  if (c === "erro") return "log-cat-erro";
  if (c === "ciclo") return "log-cat-ciclo";
  if (c === "comando") return "log-cat-comando";
  if (c === "compartilhamento") return "log-cat-comp";
  return "";
}

function logEventoCorresponde(ev, termoLower, tipoFiltro) {
  if (tipoFiltro && ev.categoria !== tipoFiltro) return false;
  if (!termoLower) return true;
  const hora = ev.timestamp ? ev.timestamp.toDate().toLocaleString("pt-BR") : "";
  const alvo = `${ev.categoria || ""} ${ev.mensagem || ""} ${ev.ator || ""} ${hora}`.toLowerCase();
  return alvo.includes(termoLower);
}

// Reaplica busca + filtro de tipo (state.logsSearchTermo/logsFiltroTipo)
// sobre state.logsRaw e redesenha. NÃO mexe em state.logsPage sozinha --
// isso é chamado tanto quando o usuário muda um filtro (nesse caso quem
// chamou já zerou a página antes, ver os listeners mais abaixo) quanto
// quando só o LOTE BRUTO muda (novo evento ao vivo, auto-refresh), caso
// em que resetar a página incomodaria quem está lendo uma página > 1.
// renderLogs() já clampa sozinha se a página atual não existir mais.
function aplicarFiltroLogs() {
  const termo = (state.logsSearchTermo || "").trim().toLowerCase();
  const tipo = state.logsFiltroTipo || "";
  state.logsFiltrados = state.logsRaw.filter((ev) => logEventoCorresponde(ev, termo, tipo));
  renderLogs();
}

// Insere o trecho que bate com o termo dentro de um <mark>, via nós de
// texto reais (não innerHTML) -- evita qualquer risco de HTML injetado,
// mesmo o conteúdo hoje sendo sempre gerado pelo próprio firmware.
function renderMensagemComDestaque(container, texto, termo) {
  container.textContent = "";
  if (!termo) { container.textContent = texto; return; }
  const idx = texto.toLowerCase().indexOf(termo.toLowerCase());
  if (idx < 0) { container.textContent = texto; return; }
  container.appendChild(document.createTextNode(texto.slice(0, idx)));
  const mark = document.createElement("mark");
  mark.textContent = texto.slice(idx, idx + termo.length);
  container.appendChild(mark);
  container.appendChild(document.createTextNode(texto.slice(idx + termo.length)));
}

function renderLogs() {
  const lista = document.getElementById("logs-list");
  const pag = document.getElementById("logs-pagination");
  if (!lista || !pag) return;

  const totalPaginas = Math.max(1, Math.ceil(state.logsFiltrados.length / LOGS_PAGE_SIZE));
  if (state.logsPage >= totalPaginas) state.logsPage = totalPaginas - 1;
  if (state.logsPage < 0) state.logsPage = 0;

  if (state.logsFiltrados.length === 0) {
    lista.innerHTML = `<div class="logs-empty">${
      state.logsSearchTermo ? "Nenhum evento bate com a busca." : "Nenhum evento ainda."
    }</div>`;
    pag.hidden = true;
    return;
  }

  const termo = (state.logsSearchTermo || "").trim();
  const inicio = state.logsPage * LOGS_PAGE_SIZE;
  const pagina = state.logsFiltrados.slice(inicio, inicio + LOGS_PAGE_SIZE);

  lista.innerHTML = "";
  pagina.forEach((ev) => {
    const item = document.createElement("div");
    item.className = "log-item";

    const hora = document.createElement("span");
    hora.className = "log-time";
    hora.textContent = ev.timestamp
      ? ev.timestamp.toDate().toLocaleString("pt-BR", { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" })
      : "--";

    const cat = document.createElement("span");
    cat.className = `log-categoria ${categoriaClasse(ev.categoria)}`;
    cat.textContent = ev.categoria || "?";

    const msg = document.createElement("span");
    msg.className = "log-mensagem";
    renderMensagemComDestaque(msg, ev.mensagem || "", termo);

    item.append(hora, cat);
    if (ev.ator) {   // Fase 89 — quem fez (ação feita pelo app)
      const quem = document.createElement("span");
      quem.className = `log-ator${ev.papel === "convidado" ? " convidado" : ""}`;
      quem.textContent = `${ev.papel === "convidado" ? "👤 convidado" : "👤"} ${ev.ator}`;
      quem.title = ev.papel === "convidado" ? "Ação feita por um convidado" : "Ação feita pelo proprietário";
      item.appendChild(quem);
    }
    item.appendChild(msg);
    lista.appendChild(item);
  });

  pag.hidden = totalPaginas <= 1;
  const indicador = document.getElementById("logs-page-indicator");
  const prevBtn = document.getElementById("logs-page-prev");
  const nextBtn = document.getElementById("logs-page-next");
  if (indicador) indicador.textContent = `${state.logsPage + 1}/${totalPaginas}`;
  if (prevBtn) prevBtn.disabled = state.logsPage === 0;
  if (nextBtn) nextBtn.disabled = state.logsPage >= totalPaginas - 1;
}

// ── Busca (ícone de lupa) ──────────────────────────────────────────
const btnLogsSearch = document.getElementById("btn-logs-search");
const logsSearchRow = document.getElementById("logs-search-row");
const logsSearchInput = document.getElementById("logs-search-input");

if (btnLogsSearch && logsSearchRow && logsSearchInput) {
  btnLogsSearch.addEventListener("click", () => {
    logsSearchRow.hidden = !logsSearchRow.hidden;
    btnLogsSearch.classList.toggle("active", !logsSearchRow.hidden);
    if (!logsSearchRow.hidden) logsSearchInput.focus();
  });

  document.getElementById("btn-logs-search-close").addEventListener("click", () => {
    logsSearchRow.hidden = true;
    btnLogsSearch.classList.remove("active");
    logsSearchInput.value = "";
    state.logsSearchTermo = "";
    state.logsPage = 0;
    aplicarFiltroLogs();
  });

  let logsSearchDebounceTimer = null;
  logsSearchInput.addEventListener("input", (e) => {
    const valor = e.target.value;
    clearTimeout(logsSearchDebounceTimer);
    logsSearchDebounceTimer = setTimeout(() => {
      state.logsSearchTermo = valor;
      state.logsPage = 0;
      aplicarFiltroLogs();
    }, 150);
  });
}

// ── Filtro por tipo de evento (Fase 30) ─────────────────────────────
document.getElementById("logs-filtro-tipo")?.addEventListener("change", (e) => {
  state.logsFiltroTipo = e.target.value;
  state.logsPage = 0;
  aplicarFiltroLogs();
});

// ── Dia específico (Fase 94) ─────────────────────────────────────────
function logsAplicarDia(ymd) {
  if (!state.chocadeiraId) return;
  state.logsDia = ymd || "";
  state.logsPage = 0;
  const limpar = document.getElementById("btn-logs-dia-limpar");
  if (limpar) limpar.hidden = !state.logsDia;
  const input = document.getElementById("logs-filtro-dia");
  if (input && input.value !== state.logsDia) input.value = state.logsDia;
  escutarLogs(state.chocadeiraId, false);
}
document.getElementById("logs-filtro-dia")?.addEventListener("change", (e) => logsAplicarDia(e.target.value));
document.getElementById("btn-logs-dia-limpar")?.addEventListener("click", () => logsAplicarDia(""));

// ── Sincronizar (Fase 30) ── Re-assina o listener na hora, sem esperar
// o próximo evento novo ou o auto-refresh de 60s. Ícone gira brevemente
// como feedback de que a ação rodou (a lista em si só muda de verdade se
// havia algo novo pra buscar).
document.getElementById("btn-logs-sync")?.addEventListener("click", (e) => {
  if (!state.chocadeiraId) return;
  const btn = e.currentTarget;
  btn.classList.remove("syncing");
  void btn.offsetWidth; // força reflow -- reinicia a animação mesmo em cliques seguidos
  btn.classList.add("syncing");
  escutarLogs(state.chocadeiraId, false);
  mostrarFeedbackHeader("Logs sincronizados", "success");
});

// ── Paginação (setas) ───────────────────────────────────────────────
document.getElementById("logs-page-prev")?.addEventListener("click", () => {
  if (state.logsPage > 0) { state.logsPage--; renderLogs(); }
});
document.getElementById("logs-page-next")?.addEventListener("click", () => {
  const totalPaginas = Math.max(1, Math.ceil((state.logsFiltrados || []).length / LOGS_PAGE_SIZE));
  if (state.logsPage < totalPaginas - 1) { state.logsPage++; renderLogs(); }
});

// ── Limpar (ícone de lixeira) ────────────────────────────────────────
// Confirmação inline (mesmo padrão do "Parar ciclo") -- apaga só o
// histórico SINCRONIZADO no Firestore; o log completo do cartão SD do
// firmware tem seu próprio "Limpar" na tela física do dispositivo.
const logsClearConfirm = document.getElementById("logs-clear-confirm");
document.getElementById("btn-logs-clear")?.addEventListener("click", () => {
  if (logsClearConfirm) logsClearConfirm.hidden = !logsClearConfirm.hidden;
});
document.getElementById("btn-logs-clear-cancel")?.addEventListener("click", () => {
  if (logsClearConfirm) logsClearConfirm.hidden = true;
});
document.getElementById("btn-logs-clear-confirm")?.addEventListener("click", async () => {
  if (logsClearConfirm) logsClearConfirm.hidden = true;
  if (!state.chocadeiraId) return;
  try {
    const snap = await db.collection("chocadeiras").doc(state.chocadeiraId).collection("logs").get();
    if (snap.empty) {
      mostrarFeedbackHeader("Nenhum log sincronizado pra apagar", "warn");
      return;
    }
    const batch = db.batch();
    snap.docs.forEach((doc) => batch.delete(doc.ref));
    await batch.commit();
    mostrarFeedbackHeader("Histórico de logs sincronizado apagado", "success");
  } catch (err) {
    console.error("Erro ao limpar logs:", err);
    mostrarFeedbackHeader("Falha ao limpar logs", "error");
  }
});

showScreen("loading");
atualizarStatusOnline();
