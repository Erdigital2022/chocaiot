// extras.js — ChocaIoT PWA — Fases 44–50
//  · progresso com estimativa (arquivos do SD)          · notificações push (FCM)
//  · segurança: e-mail de recuperação / recuperar PIN   · relatórios de ciclos encerrados
//  · histórico de 7 dias (temperatura/umidade)          · câmera (preparação da integração)
// Carrega DEPOIS de app.js (usa db, auth, state, FAIXAS, desenharSparkline... do escopo global).
// Cota do Firestore: nada periódico aqui — tudo é sob demanda (abrir tela/clicar).

// ═══════════════════════════════════════════════════════════════
// Progresso com estimativa (barra + segundos decorridos + cancelar)
// ═══════════════════════════════════════════════════════════════
function criarProgresso(slot) {
  slot.textContent = "";
  const wrap = document.createElement("div");
  wrap.className = "prog";
  wrap.hidden = true;
  const bar = document.createElement("div");
  bar.className = "prog-bar";
  const fill = document.createElement("span");
  bar.appendChild(fill);
  const row = document.createElement("div");
  row.className = "prog-row";
  const txt = document.createElement("span");
  txt.className = "prog-txt";
  const cancel = document.createElement("button");
  cancel.type = "button";
  cancel.className = "prog-cancel";
  cancel.textContent = "Cancelar";
  row.append(txt, cancel);
  wrap.append(bar, row);
  slot.appendChild(wrap);

  let timer = null, t0 = 0, est = 8, rotulo = "", onCancel = null;
  function tick() {
    const s = (Date.now() - t0) / 1000;
    fill.style.width = Math.min(92, 4 + (s / est) * 86) + "%";
    txt.textContent = s < est
      ? `${rotulo} ${Math.floor(s)}s (costuma levar ~${est}s)`
      : `${rotulo} ${Math.floor(s)}s — demorando mais que o normal…`;
  }
  const api = {
    iniciar(r, estSeg, cb) {
      rotulo = r; est = estSeg; onCancel = cb || null; t0 = Date.now();
      wrap.hidden = false; wrap.classList.remove("prog-erro", "prog-ok");
      cancel.hidden = false; fill.style.width = "4%";
      clearInterval(timer); tick(); timer = setInterval(tick, 250);
    },
    fim(msg, ok = true) {
      clearInterval(timer); timer = null;
      wrap.hidden = false; fill.style.width = "100%";
      wrap.classList.toggle("prog-ok", ok); wrap.classList.toggle("prog-erro", !ok);
      txt.textContent = msg; cancel.hidden = true;
      if (ok) setTimeout(() => { if (!timer) wrap.hidden = true; }, 1800);
    },
    esconder() { clearInterval(timer); timer = null; wrap.hidden = true; },
  };
  cancel.addEventListener("click", () => { api.esconder(); if (onCancel) onCancel(); });
  return api;
}
window.arqProg = criarProgresso(document.getElementById("arq-prog-slot"));
window.arqModalProg = criarProgresso(document.getElementById("arq-modal-prog-slot"));

// Ligações com o restante do app
function chocadeiraRef() {
  return db.collection("chocadeiras").doc(state.chocadeiraId);
}

// Chamado por app.js a cada snapshot do documento da chocadeira (sem leitura extra)
window.onChocadeiraDoc = function (d) {
  const cmd = (d && d.cmd) || {};   // mapa "cmd": campos de comando/config no mesmo poll do aparelho (máscara curta — ver main.cpp)
  state.emailRec = cmd.emailRec || "";
  if (typeof autoDoDoc === "function") autoDoDoc(cmd.auto || "");
  state.pinAtivo = d && typeof d.pinAtivo === "boolean" ? d.pinAtivo : null;
  state.pushTokens = (d && d.pushTokens) || [];
  state.cameraOnline = !!(d && d.cameraOnline === true);   // publicado pelo aparelho quando o módulo aparece/some na rede local
  state.cameraIP = (d && d.cameraIP) || "";   // Fase 66 -- o app monta http://IP:81/stream sozinho (nunca passa pela nuvem)
  camAtualizarLiveDisponibilidade();
  if (camEls.autoToggle && document.activeElement !== camEls.autoToggle) camEls.autoToggle.checked = !!(d && d.camAutoAtivo !== false);   // Fase 65: padrão ligado (mesmo default do firmware)
  segAtualizar();
  pushAtualizarStatus();
  camAtualizar();
  if (typeof lockAtualizarFw === "function") lockAtualizarFw();
  if (typeof nomeChocDoDoc === "function") nomeChocDoDoc(d && d.nome);
  if (typeof contaAtualizar === "function") contaAtualizar();
};
// Chamado por app.js ao abrir Configurações
window.onAbrirConfiguracoes = function () {
  carregarRelatorios(false);
  pushAtualizarStatus();
  segAtualizar();
};

// ═══════════════════════════════════════════════════════════════
// Notificações push (FCM). O aparelho envia; aqui só registramos o token
// deste navegador em chocadeiras/{id}.pushTokens (arrayUnion/arrayRemove).
// ═══════════════════════════════════════════════════════════════
const PUSH_KEY = "choca_push_token";
const pushEls = {
  toggle: document.getElementById("push-toggle"),
  status: document.getElementById("push-status"),
  teste: document.getElementById("btn-push-teste"),
};
let pushMessaging = null;

function pushSuportado() {
  try {
    return "serviceWorker" in navigator && "PushManager" in window && "Notification" in window &&
      typeof firebase.messaging === "function" && firebase.messaging.isSupported();
  } catch (e) { return false; }
}
function pushGetMessaging() {
  if (!pushMessaging) {
    pushMessaging = firebase.messaging();
    pushMessaging.onMessage((payload) => {   // app aberto: mostra a notificação também
      const d = (payload && payload.data) || {};
      navigator.serviceWorker.ready.then((reg) =>
        reg.showNotification(d.title || "ChocaIoT", { body: d.body || "", tag: d.tag || "choca", icon: "/icons/icon-192.png", renotify: true, silent: false })
      );
      if (typeof somAviso === "function") somAviso(); // Fase 54: som/vibração com o app aberto
    });
  }
  return pushMessaging;
}
function pushAtualizarStatus() {
  if (!pushEls.toggle) return;
  if (!pushSuportado()) {
    pushEls.toggle.checked = false; pushEls.toggle.disabled = true; pushEls.teste.disabled = true;
    pushEls.status.textContent = "Este navegador não suporta notificações push.";
    pushAplicarOpcoes(false);
    return;
  }
  const perm = Notification.permission;
  const token = localStorage.getItem(PUSH_KEY);
  const registrado = !!token && pushEntradas(token).length > 0;
  pushEls.toggle.disabled = false;
  pushEls.toggle.checked = perm === "granted" && registrado;
  pushAplicarOpcoes(perm === "granted" && registrado);
  pushEls.teste.disabled = !(perm === "granted" && registrado);
  if (perm === "denied") pushEls.status.textContent = "Permissão bloqueada nas configurações do navegador — libere para receber avisos.";
  else if (perm === "granted" && registrado) pushEls.status.textContent = "Ativas neste dispositivo. Você será avisado mesmo com o app fechado.";
  else pushEls.status.textContent = "Desativadas neste dispositivo.";
}
async function pushAtivar() {
  pushEls.status.textContent = "Pedindo permissão...";
  const perm = await Notification.requestPermission();
  if (perm !== "granted") { pushAtualizarStatus(); return; }
  const reg = await navigator.serviceWorker.ready;
  const token = await pushGetMessaging().getToken({ serviceWorkerRegistration: reg });
  if (!token) throw new Error("Sem token");
  const anterior = localStorage.getItem(PUSH_KEY);
  if (anterior && anterior !== token) {
    const velhas = pushEntradas(anterior);
    if (velhas.length) await chocadeiraRef().update({ pushTokens: firebase.firestore.FieldValue.arrayRemove(...velhas) });
  }
  localStorage.setItem(PUSH_KEY, token);
  await pushGravarEntrada(token);
  pushAtualizarStatus();
}
async function pushDesativar() {
  const token = localStorage.getItem(PUSH_KEY);
  if (token) {
    const entradas = pushEntradas(token);
    if (entradas.length) await chocadeiraRef().update({ pushTokens: firebase.firestore.FieldValue.arrayRemove(...entradas) });
  }
  try { await pushGetMessaging().deleteToken(); } catch (e) { /* já inválido */ }
  localStorage.removeItem(PUSH_KEY);
  state.pushTokens = (state.pushTokens || []).filter((t) => String(t).split("|")[0] !== token);
  pushAtualizarStatus();
}
if (pushEls.toggle) {
  pushEls.toggle.addEventListener("change", async () => {
    pushEls.toggle.disabled = true;
    try {
      if (pushEls.toggle.checked) await pushAtivar(); else await pushDesativar();
    } catch (e) {
      console.error("Erro nas notificações:", e);
      pushEls.status.textContent = "Não foi possível alterar: " + (e && e.message ? e.message : e);
      pushEls.toggle.checked = !pushEls.toggle.checked;
    } finally {
      pushEls.toggle.disabled = !pushSuportado();
    }
  });
  pushEls.teste.addEventListener("click", async () => {
    if (state.estadoConexao !== "online") { pushEls.status.textContent = "Chocadeira offline — o teste parte do aparelho."; return; }
    pushEls.teste.disabled = true;
    pushEls.status.textContent = "Teste pedido ao aparelho — chega em até ~15 s...";
    try { await chocadeiraRef().update({ "cmd.pushTeste": Date.now().toString(36) }); }
    catch (e) { pushEls.status.textContent = "Não foi possível pedir o teste."; }
    setTimeout(() => { pushEls.teste.disabled = false; }, 8000);
  });
  // Mantém o token em dia: se o navegador renovou, atualiza silenciosamente (1 escrita só quando muda)
  window.addEventListener("load", () => {
    setTimeout(async () => {
      try {
        if (!pushSuportado() || Notification.permission !== "granted" || !localStorage.getItem(PUSH_KEY) || !state.chocadeiraId) return;
        const reg = await navigator.serviceWorker.ready;
        const token = await pushGetMessaging().getToken({ serviceWorkerRegistration: reg });
        if (token && token !== localStorage.getItem(PUSH_KEY)) await pushAtivar();
      } catch (e) { /* silencioso */ }
    }, 6000);
  });
}

// ═══════════════════════════════════════════════════════════════
// Segurança — e-mail de recuperação do PIN do aparelho
// O PIN é definido/verificado no aparelho. Aqui: vincular o e-mail da conta logada
// (obrigatório antes de ativar o PIN) e confirmar o código de recuperação.
// ═══════════════════════════════════════════════════════════════
const segEls = {
  email: document.getElementById("seg-email"),
  pin: document.getElementById("seg-pin"),
  vincular: document.getElementById("btn-seg-vincular"),
  desvincular: document.getElementById("btn-seg-desvincular"),
  rec: document.getElementById("seg-rec"),
  codigo: document.getElementById("seg-rec-codigo"),
  btnRec: document.getElementById("btn-seg-rec"),
  fb: document.getElementById("seg-feedback"),
};
function emailLogado() { return (auth.currentUser && auth.currentUser.email) || ""; }
function segMsg(t, erro) { segEls.fb.textContent = t; segEls.fb.style.color = erro ? "var(--danger, #ef4444)" : ""; }
function segAtualizar() {
  if (!segEls.email) return;
  const meu = emailLogado().toLowerCase(), vinc = (state.emailRec || "").toLowerCase();
  segEls.email.textContent = state.emailRec || "não vinculado";
  segEls.pin.textContent = state.pinAtivo === true ? "Ativo" : state.pinAtivo === false ? "Desativado" : "--";
  const ehMeu = !!vinc && vinc === meu;
  segEls.vincular.hidden = ehMeu;
  segEls.vincular.textContent = vinc ? "Trocar para o meu e-mail" : "Vincular meu e-mail";
  segEls.desvincular.hidden = !vinc;
  segEls.desvincular.disabled = state.pinAtivo === true;
  segEls.desvincular.title = state.pinAtivo === true ? "Desative o PIN no aparelho antes de desvincular" : "";
  segEls.rec.hidden = !(vinc && state.pinAtivo !== false);
}
segEls.vincular.addEventListener("click", async () => {
  const email = emailLogado();
  if (!email) { segMsg("Sua conta não tem e-mail disponível.", true); return; }
  if (state.pinAtivo === true && state.emailRec && !confirm(`Trocar o e-mail de recuperação atual (${state.emailRec}) por ${email}?`)) return;
  try { await chocadeiraRef().update({ "cmd.emailRec": email }); segMsg(`E-mail ${email} vinculado. O aparelho recebe em alguns segundos.`); }
  catch (e) { console.error(e); segMsg("Não foi possível vincular.", true); }
});
segEls.desvincular.addEventListener("click", async () => {
  if (state.pinAtivo === true) { segMsg("Desative o PIN no aparelho antes de desvincular o e-mail.", true); return; }
  try { await chocadeiraRef().update({ "cmd.emailRec": firebase.firestore.FieldValue.delete() }); segMsg("E-mail desvinculado."); }
  catch (e) { console.error(e); segMsg("Não foi possível desvincular.", true); }
});
segEls.btnRec.addEventListener("click", async () => {
  const cod = segEls.codigo.value.replace(/\D/g, "");
  if (cod.length !== 6) { segMsg("Digite os 6 dígitos mostrados no aparelho.", true); return; }
  if (emailLogado().toLowerCase() !== (state.emailRec || "").toLowerCase()) {
    segMsg(`Só a conta ${state.emailRec} pode recuperar o PIN. Você está logado como ${emailLogado()}.`, true);
    return;
  }
  if (state.estadoConexao !== "online") { segMsg("Chocadeira offline — precisa estar online para confirmar.", true); return; }
  try {
    await chocadeiraRef().update({ "cmd.pinRec": `${cod}|${Date.now()}` });
    segEls.codigo.value = "";
    segMsg("Código enviado. Em alguns segundos o aparelho pede o novo PIN.");
  } catch (e) { console.error(e); segMsg("Não foi possível enviar o código.", true); }
});

// ═══════════════════════════════════════════════════════════════
// Relatórios de ciclos (chocadeiras/{id}/ciclos, criados pelo aparelho ao encerrar)
// ═══════════════════════════════════════════════════════════════
const relEls = {
  lista: document.getElementById("rel-lista"),
  sub: document.getElementById("rel-sub"),
  modal: document.getElementById("modal-rel-backdrop"),
  titulo: document.getElementById("rel-modal-titulo"),
  grid: document.getElementById("rel-modal-grid"),
  texto: document.getElementById("rel-modal-texto"),
};
let relCache = null; // { t, itens }
let relAtual = null;

function fmtDH(d) {
  return d ? d.toLocaleString("pt-BR", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" }) : "--";
}
function fmtNum(v, un, casas = 1) { return typeof v === "number" ? `${v.toFixed(casas).replace(".", ",")}${un}` : "--"; }
function fmtMin(m) { return typeof m === "number" ? `${Math.floor(m / 60)}h ${String(m % 60).padStart(2, "0")}min` : "--"; }

async function carregarRelatorios(forcar) {
  if (!state.chocadeiraId) return;
  if (!forcar && relCache && Date.now() - relCache.t < 10 * 60000) { renderRelatorios(relCache.itens); return; }
  relEls.sub.textContent = "Carregando relatórios...";
  try {
    const snap = await chocadeiraRef().collection("ciclos").orderBy("fim", "desc").limit(20).get();
    const itens = snap.docs.map((d) => ({ id: d.id, ref: d.ref, ...d.data() }));
    relCache = { t: Date.now(), itens };
    renderRelatorios(itens);
  } catch (e) {
    console.error("Erro ao carregar relatórios:", e);
    relEls.sub.textContent = "Não foi possível carregar os relatórios.";
  }
}
function renderRelatorios(itens) {
  relEls.lista.textContent = "";
  relEls.sub.textContent = itens.length
    ? `${itens.length} relatório(s) — do mais recente ao mais antigo`
    : "Gerado automaticamente quando um ciclo é encerrado (Parar ou Lixeira).";
  if (!itens.length) {
    const v = document.createElement("div");
    v.className = "logs-empty";
    v.textContent = "Nenhum relatório ainda.";
    relEls.lista.appendChild(v);
    return;
  }
  itens.forEach((r) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "arq-item";
    const ic = document.createElement("span");
    ic.className = "arq-item-icone";
    ic.textContent = r.situacao === "concluido" ? "✅" : "⚠️";
    ic.setAttribute("aria-hidden", "true");
    const tx = document.createElement("span");
    tx.className = "arq-item-texto";
    const n1 = document.createElement("span");
    n1.className = "arq-item-nome";
    n1.textContent = `${r.nome || "Lote"} · ${r.tipo || "--"}`;
    const n2 = document.createElement("span");
    n2.className = "arq-item-sub";
    n2.textContent = `${fmtDH(r.fim && r.fim.toDate())} · ${r.situacao === "concluido" ? "Concluído" : "Interrompido"} · ${fmtNum(r.tMin, "", 1)}–${fmtNum(r.tMax, "°C", 1)}`;
    tx.append(n1, n2);
    btn.append(ic, tx);
    btn.addEventListener("click", () => abrirRelatorio(r));
    relEls.lista.appendChild(btn);
  });
}
function abrirRelatorio(r) {
  relAtual = r;
  relEls.titulo.textContent = `${r.nome || "Lote"} — ${r.situacao === "concluido" ? "concluído" : "interrompido"}`;
  const linhas = [
    ["Tipo", `${r.tipo || "--"} (${r.dias || "--"} dias)`],
    ["Início", fmtDH(r.inicio && r.inicio.toDate())],
    ["Fim", fmtDH(r.fim && r.fim.toDate())],
    ["Temperatura (mín / máx / média)", `${fmtNum(r.tMin, "")} / ${fmtNum(r.tMax, "")} / ${fmtNum(r.tMed, "°C")}`],
    ["Umidade (mín / máx / média)", `${fmtNum(r.hMin, "", 0)} / ${fmtNum(r.hMax, "", 0)} / ${fmtNum(r.hMed, "%", 0)}`],
    ["Tempo fora da faixa", `temp. ${fmtMin(r.foraTempMin)} · umid. ${fmtMin(r.foraUmidMin)}`],
    ["Alertas", `temp. ${r.alertasTemp ?? "--"} · umid. ${r.alertasUmid ?? "--"}`],
    ["Cooler do sistema ligou", `${r.coolerOn ?? "--"} vez(es)`],
    ["Reinícios do aparelho", `${r.reinicios ?? "--"}`],
    ["Amostras", `${r.amostras ?? "--"}`],
  ];
  relEls.grid.textContent = "";
  linhas.forEach(([k, v]) => {
    const row = document.createElement("div");
    row.className = "rel-row";
    const a = document.createElement("span"); a.className = "rel-k"; a.textContent = k;
    const b = document.createElement("span"); b.className = "rel-v"; b.textContent = v;
    row.append(a, b);
    relEls.grid.appendChild(row);
  });
  relEls.texto.textContent = r.texto || "";
  relEls.modal.hidden = false;
}
function relNomeArquivo() {
  const d = relAtual && relAtual.fim ? relAtual.fim.toDate() : new Date();
  const p = (n) => String(n).padStart(2, "0");
  const lote = String((relAtual && relAtual.nome) || "lote").replace(/[^a-z0-9]+/gi, "_");
  return `relatorio_${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}_${lote}.txt`;
}
document.getElementById("rel-modal-close").addEventListener("click", () => { relEls.modal.hidden = true; });
relEls.modal.addEventListener("click", (e) => { if (e.target === relEls.modal) relEls.modal.hidden = true; });
document.getElementById("btn-rel-refresh").addEventListener("click", () => carregarRelatorios(true));
document.getElementById("rel-btn-baixar").addEventListener("click", () => {
  if (!relAtual) return;
  const url = URL.createObjectURL(new Blob([relAtual.texto || ""], { type: "text/plain" }));
  const a = document.createElement("a");
  a.href = url; a.download = relNomeArquivo();
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
});
document.getElementById("rel-btn-compartilhar").addEventListener("click", async () => {
  if (!relAtual) return;
  const file = new File([relAtual.texto || ""], relNomeArquivo(), { type: "text/plain" });
  try {
    if (navigator.canShare && navigator.canShare({ files: [file] })) await navigator.share({ files: [file], title: relEls.titulo.textContent });
    else if (navigator.share) await navigator.share({ title: relEls.titulo.textContent, text: relAtual.texto || "" });
    else document.getElementById("rel-btn-baixar").click();
  } catch (e) { if (!e || e.name !== "AbortError") document.getElementById("rel-btn-baixar").click(); }
});
document.getElementById("rel-btn-excluir").addEventListener("click", async () => {
  if (!relAtual || !confirm("Excluir este relatório do histórico do app? (a cópia no cartão SD do aparelho não é afetada)")) return;
  try {
    await relAtual.ref.delete();
    relCache = null; relEls.modal.hidden = true; carregarRelatorios(true);
  } catch (e) { console.error(e); alert("Não foi possível excluir."); }
});

// ═══════════════════════════════════════════════════════════════
// Histórico de 7 dias — 1 documento por dia em chocadeiras/{id}/historico/AAAA-MM-DD
// (campo "pts" = "slot,temp*10,umid*10;..." a cada 10 min) → 7 leituras no total.
// ═══════════════════════════════════════════════════════════════
let sensorPeriodo = "hoje";
let hist7dCache = null; // { t, docs }
window.sensorModalReset = function () {
  sensorPeriodo = "hoje";
  document.querySelectorAll(".sensor-tab").forEach((b) => b.classList.toggle("active", b.dataset.periodo === "hoje"));
};
async function carregarSensor7d(tipo) {
  const faixa = FAIXAS[tipo];
  const contagem = document.getElementById("sensor-modal-contagem");
  contagem.textContent = "carregando 7 dias...";
  document.getElementById("sensor-modal-live").hidden = true;
  try {
    let docs;
    if (hist7dCache && Date.now() - hist7dCache.t < 5 * 60000) docs = hist7dCache.docs;
    else {
      const snap = await chocadeiraRef().collection("historico").orderBy(firebase.firestore.FieldPath.documentId(), "desc").limit(7).get();
      docs = snap.docs.map((d) => ({ id: d.id, pts: d.data().pts || "", m: d.data().m || null }));   // Fase 115: lê também o mapa "m" (formato atual)
      hist7dCache = { t: Date.now(), docs };
    }
    if (sensorPeriodo !== "7d" || modalSensorTipoAberto !== tipo) return; // usuário trocou de aba/sensor enquanto carregava
    const pontos = [];
    docs.slice().reverse().forEach((d) => {
      const [y, m, dia] = d.id.split("-").map(Number);
      // Dois formatos por dia: "pts" = "slot,t10,h10;..." (legado) e "m" = {s123: "t10,h10[,co2]"} (atual).
      const slots = {};
      String(d.pts || "").split(";").forEach((p) => {
        const a = p.split(",");
        if (a.length >= 3) slots[a[0]] = [Number(a[1]), Number(a[2]), NaN];
      });
      if (d.m && typeof d.m === "object") Object.keys(d.m).forEach((k) => {
        const a = String(d.m[k]).split(",");
        if (a.length >= 2) slots[k.replace(/^s/, "")] = [Number(a[0]), Number(a[1]), a.length >= 3 ? Number(a[2]) : NaN];
      });
      Object.keys(slots).forEach((sl) => {
        const r = slots[sl];
        const v = tipo === "temperatura" ? r[0] / 10 : tipo === "umidade" ? r[1] / 10 : r[2];
        if (Number.isFinite(v)) pontos.push({ valor: v, quando: new Date(y, m - 1, dia, 0, Number(sl) * 10) });
      });
    });
    pontos.sort((a, b) => a.quando - b.quando);
    if (!pontos.length) {
      contagem.textContent = tipo === "co2"
        ? "Sem CO₂ no histórico ainda (1 ponto a cada 10 min; começa depois que o aparelho com o sensor gravar o 1º ponto)."
        : "Sem histórico ainda (o aparelho grava 1 ponto a cada 10 min).";
      ["min", "max", "media"].forEach((k) => (document.getElementById(`sensor-modal-${k}`).textContent = "--"));
      desenharSparkline([], faixa);
      return;
    }
    const valores = pontos.map((p) => p.valor);
    const dec = faixa.dec ?? 1;
    document.getElementById("sensor-modal-min").textContent = `${Math.min(...valores).toFixed(dec)}${faixa.unidade}`;
    document.getElementById("sensor-modal-max").textContent = `${Math.max(...valores).toFixed(dec)}${faixa.unidade}`;
    document.getElementById("sensor-modal-media").textContent = `${(valores.reduce((a, b) => a + b, 0) / valores.length).toFixed(dec)}${faixa.unidade}`;
    contagem.textContent = `${pontos.length} pontos · ${docs.length} dia(s) · 1 ponto a cada 10 min`;
    desenharSparkline(pontos, faixa);
  } catch (e) {
    console.error("Erro no histórico de 7 dias:", e);
    contagem.textContent = "Não foi possível carregar o histórico.";
  }
}
document.querySelectorAll(".sensor-tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    if (!modalSensorTipoAberto || btn.dataset.periodo === sensorPeriodo) return;
    sensorPeriodo = btn.dataset.periodo;
    document.querySelectorAll(".sensor-tab").forEach((b) => b.classList.toggle("active", b === btn));
    if (sensorPeriodo === "7d") {
      if (unsubscribeModalSensor) { unsubscribeModalSensor(); unsubscribeModalSensor = null; }
      carregarSensor7d(modalSensorTipoAberto);
    } else {
      abrirModalSensor(modalSensorTipoAberto, true);
    }
  });
});

// ═══════════════════════════════════════════════════════════════
// Câmera — preparação da integração (módulo ESP32-S3-CAM ainda a caminho).
// Contrato (ver chocaiot-camera/docs/INTEGRACAO.md):
//   PWA  → chocadeiras/{id}.cmd.camReq = "<idDoPedido>"         (este botão)
//   CTRL → chocadeiras/{id}.cameraOnline (bool; o aparelho publica quando o módulo aparece/some)
//   CTRL → chocadeiras/{id}/camera/ultima = { reqId, b64, mime, tam, tirouEm, erro }
//   (o módulo de câmera fala só com o aparelho, por HTTP na rede local — zero leituras extras)
// ═══════════════════════════════════════════════════════════════
const camEls = {
  img: document.getElementById("camera-img"),
  vazioSub: document.getElementById("camera-vazio-sub"),
  status: document.getElementById("camera-status"),
  btn: document.getElementById("btn-camera-foto"),
  painel: document.getElementById("camera-panel"),
  visao: document.getElementById("camera-visao"),
  visaoFase: document.getElementById("camera-visao-fase"),
  visaoMov: document.getElementById("camera-visao-mov"),
  autoToggle: document.getElementById("camera-auto-toggle"),
  visaoLinha: document.getElementById("camera-visao-linha"),
  liveImg: document.getElementById("camera-live-img"),
  liveCanvas: document.getElementById("camera-live-canvas"),
  liveBadge: document.getElementById("camera-live-badge"),
  liveAviso: document.getElementById("camera-live-aviso"),
  liveToggleBtn: document.getElementById("btn-camera-live-toggle"),
  liveFotoBtn: document.getElementById("btn-camera-live-foto"),
  liveAbrir: document.getElementById("camera-live-abrir"),
};
// Fase 65 — toggle de captura automática (cmd.camAuto, "0"/"1" — mesmo padrão de outros toggles em cmd.*).
// O valor exibido vem do campo topo "camAutoAtivo" (o aparelho publica; ver onChocadeiraDoc/camAutoDoDoc).
if (camEls.autoToggle) camEls.autoToggle.addEventListener("change", async () => {
  if (!state.chocadeiraId) return;
  try { await chocadeiraRef().update({ "cmd.camAuto": camEls.autoToggle.checked ? "1" : "0" }); }
  catch (e) { console.error(e); camEls.autoToggle.checked = !camEls.autoToggle.checked; }
});
const CAM_TIMEOUT_MS = 45000;
let camUnsub = null, camTimeout = null, camCarregouUltima = false, camBlob = null;

function camOnline() {
  return state.cameraOnline === true && state.estadoConexao === "online";
}
function camAtualizar() {
  if (!camEls.btn) return;
  const on = camOnline();
  camEls.btn.disabled = !on || !!camUnsub;
  if (!camUnsub) camEls.status.textContent = on ? "Câmera conectada" : "Módulo não conectado";
  camEls.vazioSub.textContent = on ? "Toque em Tirar foto" : "Módulo ainda não conectado";
  if (!camCarregouUltima && state.chocadeiraId) { camCarregouUltima = true; camCarregarUltima(); }
}
function camMostrar(d) {
  if (!d) return;
  // Fase 65 — estatísticas de visão (estimativa; nunca é contagem de ovos real). Contagem de ovos em si
  // ainda não existe (precisaria de fotos reais da chocadeira pra calibrar) — mostrado sempre como indisponível.
  if (camEls.visaoLinha) {
    const temMov = typeof d.camMov === "number" && d.camMov >= 0;
    const fase = d.camFase ? `Fase: ${d.camFase}` : "";
    const mov = temMov ? `Movimento: ${d.camMov.toFixed(1)} · Brilho: ${Math.round(d.camBrilho)}` : "Sem análise de imagem ainda";
    camEls.visaoLinha.textContent = `${fase ? fase + " · " : ""}${mov} · Contagem de ovos: indisponível`;
  }
  if (!d.b64) return;
  camBlob = { b64: d.b64, mime: d.mime || "image/jpeg" };
  camEls.img.src = `data:${camBlob.mime};base64,${d.b64}`;
  camEls.img.hidden = false;
  camEls.painel.classList.add("camera-com-foto");
  const q = d.tirouEm && d.tirouEm.toDate ? d.tirouEm.toDate() : null;
  if (!camUnsub) camEls.status.textContent = q ? `Foto de ${fmtDH(q)}` : "Última foto";
}
async function camCarregarUltima() {
  try {
    const snap = await chocadeiraRef().collection("camera").doc("ultima").get();
    if (snap.exists) camMostrar(snap.data());
  } catch (e) { /* sem foto ainda / regras */ }
}
function camFinalizar(msg) {
  if (camUnsub) { camUnsub(); camUnsub = null; }
  if (camTimeout) { clearTimeout(camTimeout); camTimeout = null; }
  camAtualizar();
  camEls.status.textContent = msg;
}
if (camEls.btn) {
  camEls.btn.addEventListener("click", async () => {
    if (!camOnline() || camUnsub) return;
    const reqId = Date.now().toString(36);
    camEls.status.textContent = "Pedindo foto...";
    camEls.btn.disabled = true;
    camUnsub = chocadeiraRef().collection("camera").doc("ultima").onSnapshot((snap) => {
      const d = snap.exists ? snap.data() : null;
      if (!d || d.reqId !== reqId) return;
      if (d.erro) { camFinalizar(d.erro); return; }
      camMostrar(d);
      camFinalizar(`Foto de ${fmtDH(new Date())}`);
    }, () => camFinalizar("Erro ao receber a foto."));
    camTimeout = setTimeout(() => camFinalizar("A câmera não respondeu. Tente de novo."), CAM_TIMEOUT_MS);
    try { await chocadeiraRef().update({ "cmd.camReq": reqId }); camEls.status.textContent = "Aguardando a câmera..."; }
    catch (e) { console.error(e); camFinalizar("Não foi possível enviar o pedido."); }
  });
  camEls.img.addEventListener("click", async () => {   // toque na foto: compartilha (ou baixa)
    if (!camBlob) return;
    const bin = atob(camBlob.b64), bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    const file = new File([bytes], "foto_chocadeira.jpg", { type: camBlob.mime });
    try {
      if (navigator.canShare && navigator.canShare({ files: [file] })) { await navigator.share({ files: [file], title: "Foto da chocadeira" }); return; }
    } catch (e) { if (e && e.name === "AbortError") return; }
    const a = document.createElement("a");
    a.href = URL.createObjectURL(file); a.download = file.name; document.body.appendChild(a); a.click(); a.remove();
  });
}

// ═══════════════════════════════════════════════════════════════
// Fase 66 — vídeo ao vivo. O navegador fala DIRETO com a câmera (http://<cameraIP>:81/stream, MJPEG) —
// o vídeo em si NUNCA passa pela nuvem/Firebase (só um avisinho — cmd.camStreamPing — quando abre, pra
// aparecer no log do aparelho). Só funciona com o celular na MESMA rede Wi-Fi da câmera; fora de casa o
// botão fica desabilitado com a explicação. Detecção de movimento e "Capturar" (baixar o quadro atual)
// rodam 100% no navegador via <canvas> — nenhum custo de nuvem.
const LIVE_IDLE_MS = 2 * 60 * 1000;      // "câmera desliga automaticamente após 2 min de inatividade"
const LIVE_MOTION_INTERVAL_MS = 1000;
const LIVE_MOTION_W = 80, LIVE_MOTION_H = 60;
let liveAtiva = false, liveIdleTimer = null, liveMotionTimer = null, liveMotionAnterior = null, liveMotionSuportado = true;

function camStreamUrl() { return state.cameraIP ? `http://${state.cameraIP}:81/stream` : ""; }

function camAtualizarLiveDisponibilidade() {
  if (!camEls.liveToggleBtn) return;
  const disponivel = camOnline() && !!state.cameraIP;
  camEls.liveToggleBtn.disabled = !disponivel && !liveAtiva;
  camEls.liveToggleBtn.title = disponivel ? "" : "Precisa do celular na mesma rede Wi-Fi da câmera";
  if (!disponivel && liveAtiva) camPararAoVivo();
}

function camResetarIdle() {
  if (liveIdleTimer) clearTimeout(liveIdleTimer);
  if (liveAtiva) liveIdleTimer = setTimeout(() => { camPararAoVivo(); camEls.status.textContent = "Câmera ao vivo pausada por inatividade."; }, LIVE_IDLE_MS);
}

function camMotionTick() {
  if (!liveMotionSuportado || !camEls.liveImg.naturalWidth) return;
  const ctx = camEls.liveCanvas.getContext("2d", { willReadFrequently: true });
  try {
    ctx.drawImage(camEls.liveImg, 0, 0, LIVE_MOTION_W, LIVE_MOTION_H);
    const dados = ctx.getImageData(0, 0, LIVE_MOTION_W, LIVE_MOTION_H).data;
    const atual = new Uint8ClampedArray(LIVE_MOTION_W * LIVE_MOTION_H);
    for (let i = 0, p = 0; i < dados.length; i += 4, p++) atual[p] = (dados[i] * 0.3 + dados[i + 1] * 0.59 + dados[i + 2] * 0.11) | 0;
    if (liveMotionAnterior) {
      let diff = 0;
      for (let p = 0; p < atual.length; p++) diff += Math.abs(atual[p] - liveMotionAnterior[p]);
      const score = diff / atual.length;
      camEls.liveAviso.hidden = false;
      camEls.liveAviso.textContent = `Movimento: ${score.toFixed(1)} (estimativa local, não é contagem de ovos)`;
    }
    liveMotionAnterior = atual;
  } catch (e) {
    // Canvas "manchado" (CORS) — a câmera ainda não confirma o cabeçalho Access-Control-Allow-Origin nesta rede/navegador.
    // O vídeo continua funcionando; só a detecção de movimento no navegador é que fica indisponível.
    liveMotionSuportado = false;
    camEls.liveAviso.hidden = false;
    camEls.liveAviso.textContent = "Vídeo ao vivo ok — detecção de movimento indisponível neste navegador.";
    console.warn("Detecção de movimento desativada (canvas tainted):", e);
  }
}

function camIniciarAoVivo() {
  const url = camStreamUrl();
  if (!url) return;
  liveAtiva = true;
  liveMotionAnterior = null; liveMotionSuportado = true;
  camEls.liveCanvas.width = LIVE_MOTION_W; camEls.liveCanvas.height = LIVE_MOTION_H;
  camEls.liveImg.hidden = false; camEls.liveBadge.hidden = false;
  camEls.liveFotoBtn.hidden = false; camEls.liveAbrir.hidden = false;
  camEls.liveAbrir.href = url;
  camEls.liveAviso.hidden = true;
  camEls.liveToggleBtn.textContent = "Parar";
  camEls.painel.classList.add("camera-ao-vivo");
  camEls.img.hidden = true;   // esconde a última foto estática enquanto o vídeo estiver ligado
  camEls.btn.disabled = true;   // a câmera só atende um pedido por vez (foto avulsa OU stream)
  camEls.liveImg.onerror = () => {
    camEls.liveAviso.hidden = false;
    camEls.liveAviso.textContent = "Não carregou (rede diferente da câmera, ou o navegador bloqueou o vídeo). Use o link abaixo.";
  };
  camEls.liveImg.src = url;
  liveMotionTimer = setInterval(camMotionTick, LIVE_MOTION_INTERVAL_MS);
  camResetarIdle();
  if (state.chocadeiraId) chocadeiraRef().update({ "cmd.camStreamPing": String(Date.now()) }).catch(() => {});
}
function camPararAoVivo() {
  liveAtiva = false;
  if (liveIdleTimer) { clearTimeout(liveIdleTimer); liveIdleTimer = null; }
  if (liveMotionTimer) { clearInterval(liveMotionTimer); liveMotionTimer = null; }
  camEls.liveImg.src = "";   // fecha a conexão MJPEG de verdade (o aparelho desliga a câmera assim que percebe)
  camEls.liveImg.hidden = true; camEls.liveBadge.hidden = true;
  camEls.liveFotoBtn.hidden = true; camEls.liveAbrir.hidden = true; camEls.liveAviso.hidden = true;
  camEls.liveToggleBtn.textContent = "Ao vivo";
  camEls.painel.classList.remove("camera-ao-vivo");
  camEls.img.hidden = !camBlob;
  camEls.btn.disabled = !camOnline();
}
if (camEls.liveToggleBtn) {
  camEls.liveToggleBtn.addEventListener("click", () => { liveAtiva ? camPararAoVivo() : camIniciarAoVivo(); });
  camEls.painel.addEventListener("click", () => { if (liveAtiva) camResetarIdle(); });
  document.addEventListener("visibilitychange", () => { if (document.hidden && liveAtiva) camPararAoVivo(); });
  camEls.liveFotoBtn.addEventListener("click", () => {
    // "Capturar" = baixa o quadro atual pro celular (100% local); salvar no cartão SD do aparelho já é o botão "Tirar foto" acima.
    if (!camEls.liveImg.naturalWidth) return;
    const c = document.createElement("canvas"); c.width = camEls.liveImg.naturalWidth; c.height = camEls.liveImg.naturalHeight;
    try {
      c.getContext("2d").drawImage(camEls.liveImg, 0, 0);
      c.toBlob((blob) => {
        if (!blob) return;
        const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = `chocadeira_${Date.now()}.jpg`;
        document.body.appendChild(a); a.click(); a.remove();
      }, "image/jpeg", 0.9);
    } catch (e) { alert("Não foi possível capturar (proteção do navegador). Use o link \"Abrir numa aba nova\" e salve a imagem de lá."); }
  });
}

// ═══════════════════════════════════════════════════════════════
// Fase 51 — enviar imagem do PWA para o aparelho (cai em /download no cartão SD)
// A imagem é convertida aqui no navegador para JPEG baseline (o firmware não lê PNG/WEBP/progressivo)
// e reduzida até ≤ ~80 KB (cabe no documento do Firestore, base64 ≈ +33%).
// Fluxo: uploads/atual = {reqId,nome,b64,status:"pendente"} + cmd.upReq = reqId → o aparelho lê, grava e
// responde em uploads/atual.status ("ok"/"erro") + msg (caminho no SD).
// ═══════════════════════════════════════════════════════════════
const UP_MAX_BYTES = 80000;
const UP_TIMEOUT_MS = 60000;
const upEls = {
  btn: document.getElementById("btn-up-escolher"),
  input: document.getElementById("up-input"),
};
const upProg = criarProgresso(document.getElementById("up-prog-slot"));
let upUnsub = null, upTimer = null;

function upBlobDe(canvas, q) {
  return new Promise((res) => canvas.toBlob(res, "image/jpeg", q));
}
async function upConverter(file) {
  let bmp;
  try { bmp = await createImageBitmap(file); }
  catch (e) { throw new Error("Não consegui ler esta imagem (formato não suportado pelo navegador)."); }
  let escala = Math.min(1, 640 / Math.max(bmp.width, bmp.height));
  for (let tent = 0; tent < 6; tent++) {
    const w = Math.max(16, Math.round(bmp.width * escala)), h = Math.max(16, Math.round(bmp.height * escala));
    const c = document.createElement("canvas");
    c.width = w; c.height = h;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "#000"; ctx.fillRect(0, 0, w, h); // PNG transparente vira fundo preto
    ctx.drawImage(bmp, 0, 0, w, h);
    for (const q of [0.88, 0.78, 0.66, 0.55, 0.45]) {
      const blob = await upBlobDe(c, q);
      if (blob && blob.size <= UP_MAX_BYTES) return { blob, w, h };
    }
    escala *= 0.78;
  }
  throw new Error("Não consegui reduzir a imagem para o tamanho aceito.");
}
function upBase64(blob) {
  return new Promise((res, rej) => {
    const r = new FileReader();
    r.onload = () => res(String(r.result).split(",")[1] || "");
    r.onerror = () => rej(new Error("Falha ao ler a imagem."));
    r.readAsDataURL(blob);
  });
}
function upNomeSeguro(nome) {
  const base = String(nome || "imagem").replace(/\.[^.]+$/, "").normalize("NFKD").replace(/[^\w-]+/g, "_").replace(/^_+|_+$/g, "");
  return (base || "imagem").slice(0, 32);
}
function upLimpar() {
  if (upUnsub) { upUnsub(); upUnsub = null; }
  if (upTimer) { clearTimeout(upTimer); upTimer = null; }
  upEls.btn.disabled = false;
}
if (upEls.btn) {
  upEls.btn.addEventListener("click", () => {
    if (state.estadoConexao !== "online") { upProg.fim("Chocadeira offline — o envio precisa do aparelho ligado e com internet.", false); return; }
    upEls.input.value = "";
    upEls.input.click();
  });
  upEls.input.addEventListener("change", async () => {
    const file = upEls.input.files && upEls.input.files[0];
    if (!file) return;
    upEls.btn.disabled = true;
    try {
      upProg.iniciar("Preparando a imagem…", 4, () => upLimpar());
      const { blob, w, h } = await upConverter(file);
      const b64 = await upBase64(blob);
      const reqId = Date.now().toString(36);
      const nome = upNomeSeguro(file.name);
      const ref = chocadeiraRef();
      const docRef = ref.collection("uploads").doc("atual");
      upProg.iniciar(`Enviando ${nome}.jpg (${w}×${h}, ${(blob.size / 1024).toFixed(0)} KB) ao aparelho…`, 14, () => upLimpar());
      upUnsub = docRef.onSnapshot((snap) => {
        const d = snap.exists ? snap.data() : null;
        if (!d || d.reqId !== reqId || (d.status !== "ok" && d.status !== "erro")) return;
        upLimpar();
        if (d.status === "ok") upProg.fim(`Salvo no aparelho: ${d.msg || "/download"}`, true);
        else upProg.fim(`O aparelho recusou: ${d.msg || "erro"}`, false);
      }, () => { upLimpar(); upProg.fim("Erro ao acompanhar o envio.", false); });
      upTimer = setTimeout(() => { upLimpar(); upProg.fim("O aparelho não confirmou em 60 s (offline ou ocupado). Confira em Arquivos > download.", false); }, UP_TIMEOUT_MS);
      await docRef.set({ reqId, nome, mime: "image/jpeg", b64, status: "pendente", msg: "" });
      await ref.update({ "cmd.upReq": reqId });
    } catch (e) {
      console.error("Erro no envio de mídia:", e);
      upLimpar();
      upProg.fim(e && e.message ? e.message : "Não foi possível enviar.", false);
    }
  });
}

// ═══════════════════════════════════════════════════════════════
// Fase 52 — Automação (uma regra por relé). Mesma string compacta do firmware (cmd.auto):
//   "A1|ativo|soCiclo|r0|...|r7"   r = modo,alvoCustom,alvo10,hist10,onSeg,cadaMin,hIni,hFim,autoOffMin
// modo: 0 manual · 1 aquecer · 2 resfriar · 3 umidificar · 4 desumidificar · 5 periódico · 6 horário · 7 viragem · 8 circular
// alvo10/hist10 em décimos (°C ou %). hIni/hFim em minutos do dia. O relé 7 (Cooler) é automático pelo chip.
// ═══════════════════════════════════════════════════════════════
const AUTO_N = 8, AUTO_COOLER = 6;
const AUTO_MODOS = ["Manual (sem automação)", "Aquecer (termostato)", "Resfriar (termostato)", "Umidificar", "Desumidificar / ventilar", "Periódico (liga N s a cada M min)", "Horário (janela do dia)", "Viragem (motor, segue o tipo do ovo)", "Circular (sempre ligado, circula o ar)"];
function autoPadrao() {
  const base = () => ({ modo: 0, alvoCustom: 0, alvo10: 375, hist10: 2, onSeg: 60, cadaMin: 60, hIni: 480, hFim: 1080, autoOffMin: 0 });
  const regras = Array.from({ length: AUTO_N }, base);
  regras[0].modo = 1;
  Object.assign(regras[1], { modo: 3, alvo10: 550, hist10: 10 });
  regras[2].autoOffMin = 5;
  Object.assign(regras[3], { modo: 5, onSeg: 120, cadaMin: 60 });
  regras[4].autoOffMin = 10;
  regras[7].modo = 8; // Fans: circulam o ar enquanto a automação está ativa
  return { ativo: false, soCiclo: true, regras, adv: [], retoma: 10 };
}
function autoParse(str) {
  if (!str || !str.startsWith("A1|")) return null;
  const p = str.split("|");
  if (p.length < AUTO_N + 3) return null;
  const regras = p.slice(3, 3 + AUTO_N).map((tk) => {
    const v = tk.split(",").map(Number);
    const g = (i, d) => (Number.isFinite(v[i]) ? v[i] : d);
    return { modo: g(0, 0), alvoCustom: g(1, 0) ? 1 : 0, alvo10: g(2, 375), hist10: Math.max(1, g(3, 2)), onSeg: g(4, 60), cadaMin: Math.max(1, g(5, 60)), hIni: g(6, 480) % 1440, hFim: g(7, 1080) % 1440, autoOffMin: g(8, 0) };
  });
  const adv = [];
  let off = 3 + AUTO_N, hw = null;
  if (typeof p[off] === "string" && p[off].startsWith("H,")) { hw = hwParse(p[off]); off++; }
  let manter;   // Fase 79: "M,1|0" = o aparelho mantém o ambiente do último tipo quando não há ciclo
  if (typeof p[off] === "string" && p[off].startsWith("M,")) { manter = p[off].endsWith("1") ? 1 : 0; off++; }
  let retoma = 10;   // Fase 111: "R,<min>" = religar a automação sozinha após N min desligada (0 = nunca)
  if (typeof p[off] === "string" && p[off].startsWith("R,")) { const rv = Number(p[off].slice(2)); retoma = Number.isFinite(rv) ? rv : 10; off++; }
  if (typeof p[off] === "string" && p[off].startsWith("L,")) off++;   // Fase 82: comando de tampa (consumido pelo aparelho) — não é guardado   // Fase 78: hardware "H,vol,iso,aqTipo,aqW,aqQtd,rele,umTipo,umMlH,umQtd,fanQtd"
  if (p[off] === "X") {   // Fase 57: regras avançadas "|X|r1|r2..." (16 campos por regra)
    p.slice(off + 1).slice(0, ADV_MAX).forEach((tk) => {
      const v = tk.split(",").map(Number); const g = (i, d) => (Number.isFinite(v[i]) ? v[i] : d);
      adv.push({ ativa: g(0, 1) ? 1 : 0, s1: g(1, 0) % 4, op1: g(2, 0) & 3, v1: g(3, 375), comb: g(4, 0) % 3, s2: g(5, 1) % 4, op2: g(6, 3) & 3, v2: g(7, 500),
        temOff: g(8, 0) ? 1 : 0, sOff: g(9, 0) % 4, opOff: g(10, 1) & 3, vOff: g(11, 373), acaoA: g(12, 0), acaoI: g(13, 0), hIni: g(14, 0) % 1440, hFim: g(15, 0) % 1440 });
    });
  }
  return { ativo: p[1] === "1", soCiclo: p[2] === "1", regras, adv, hw, manter, retoma };
}
const ADV_MAX = 8;
function autoSerialize(cfg) {
  const base = ["A1", cfg.ativo ? 1 : 0, cfg.soCiclo ? 1 : 0, ...cfg.regras.map((r) => [r.modo, r.alvoCustom ? 1 : 0, Math.round(r.alvo10), Math.round(r.hist10), Math.round(r.onSeg), Math.round(r.cadaMin), Math.round(r.hIni), Math.round(r.hFim), Math.round(r.autoOffMin)].join(","))].join("|");
  const hwStr = cfg.hw ? "|" + hwSerializar(cfg.hw) : "";   // Fase 78 (sem hw = o aparelho mantém o que tem)
  const mStr = (cfg.manter === undefined ? "" : "|M," + (cfg.manter ? 1 : 0)) + (cfg.retoma === undefined ? "" : "|R," + Math.max(0, Math.min(240, Math.round(cfg.retoma)))) + (cfg.tampaCmd ? "|L," + cfg.tampaCmd : "");   // Fase 79 / 82 (L,1 = vou abrir · L,2 = fechei)
  const adv = (cfg.adv || []).length ? "|X|" + cfg.adv.map((r) => [r.ativa ? 1 : 0, r.s1, r.op1, Math.round(r.v1), r.comb, r.s2, r.op2, Math.round(r.v2), r.temOff ? 1 : 0, r.sOff, r.opOff, Math.round(r.vOff), r.acaoA, r.acaoI, Math.round(r.hIni), Math.round(r.hFim)].join(",")).join("|") : "";
  return base + hwStr + mStr + adv;
}
const autoEls = {
  ativo: document.getElementById("auto-ativo"),
  soCiclo: document.getElementById("auto-socilo"),
  retoma: document.getElementById("auto-retoma"),
  lista: document.getElementById("auto-lista"),
  salvar: document.getElementById("btn-auto-salvar"),
  padrao: document.getElementById("btn-auto-padrao"),
  fb: document.getElementById("auto-feedback"),
};
let autoCfg = autoPadrao();
let autoSujo = false;

function autoMsg(t, erro) { autoEls.fb.textContent = t; autoEls.fb.style.color = erro ? "var(--danger)" : ""; }
function minParaHHMM(m) { return `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`; }
function hhmmParaMin(s) { const [h, m] = String(s || "00:00").split(":").map(Number); return ((h || 0) * 60 + (m || 0)) % 1440; }

// Ajuda "?" — cada campo/função da Automação tem um botão que abre um cartão flutuante (fundo desfocado); toque em qualquer lugar fecha
const AJUDA = {
  ativo: ["Automação ativa", "Liga ou desliga TODA a automação. Desligada, nenhum relé é comandado pelas regras (só você, manualmente). Ao desligar, o aparelho desliga o que a automação tinha ligado."],
  soCiclo: ["Só com ciclo em andamento", "Se ligado, as regras só funcionam enquanto existe um ciclo de incubação rodando (não pausado). Sem ciclo, os relés ficam livres para uso manual. Exceção: o aquecedor e o umidificador (termostatos) só agem com ciclo ou com a \"manutenção do ambiente\" (card Status) — com o Status em OFF eles ficam desligados, mesmo com esta chave desligada."],
  modo: ["Modo do relé", "Define o que o aparelho faz com este relé.\n• Manual: a automação não mexe.\n• Aquecer: liga quando a temperatura está ABAIXO do alvo (menos a histerese) e desliga ao atingir o alvo — é um termostato de aquecimento.\n• Resfriar: liga quando está ACIMA do alvo e desliga ao voltar.\n• Umidificar / Desumidificar: o mesmo, com a umidade.\n• Periódico: liga por N segundos a cada M minutos.\n• Horário: fica ligado dentro de uma janela do dia.\n• Viragem: aciona o motor de viragem em pulsos, N vezes por dia, na hora da 1ª viragem e até o dia definidos no Tipo de ovo do ciclo.\nAtenção: se este relé for usado numa regra avançada, o modo daqui é ignorado."],
  alvo: ["Alvo", "De onde vem o valor de referência: “Do tipo do ciclo” usa a temperatura/umidade ideal do tipo de ovo (com a temperatura e a umidade de eclosão a partir do dia de eclosão). “Personalizado” deixa você digitar o valor."],
  valorAlvo: ["Valor", "Temperatura (°C) ou umidade (%) que o relé tenta manter."],
  hist: ["Histerese", "Folga que evita ligar e desligar toda hora. Ex.: Aquecer com alvo 37,5 e histerese 0,2 → liga só abaixo de 37,3 e desliga ao chegar em 37,5."],
  ligaPor: ["Liga por (segundos)", "Quanto tempo o relé fica ligado em cada repetição."],
  cadaMin: ["A cada (minutos)", "De quanto em quanto tempo a repetição acontece (sincronizada com o relógio do aparelho)."],
  horaLiga: ["Liga às / Desliga às", "Janela do dia em que o relé fica ligado. Se a hora final for menor que a inicial, a janela atravessa a meia-noite."],
  autoOff: ["Desliga sozinho", "Trava de segurança: se o relé ficar ligado mais que este tempo (por qualquer motivo) o aparelho o desliga. 0 = nunca."],
  vira: ["Viragem", "O relé liga por alguns segundos (o pulso) nos horários de viragem do tipo de ovo do ciclo: N viragens por dia, igualmente espaçadas a partir da hora da 1ª viragem, até o dia “virar até”. Ajuste esses valores em Configurações > Tipos de ovos. Ajuste o pulso ao tempo que o seu motor/bandeja leva para completar uma viragem. Só age com ciclo em andamento."],
  advAtiva: ["Regra ligada/desligada", "Liga ou desliga só esta regra sem apagá-la."],
  advSensor: ["Sensor", "O que a condição mede: temperatura do ambiente, umidade, temperatura do chip (a eletrônica do aparelho) ou CO₂ em ppm (precisa do sensor MH-Z19B, ainda não instalado: enquanto não houver leitura, a regra fica parada por segurança)."],
  advOp: ["Operador", "≥ maior ou igual · ≤ menor ou igual · > maior que · < menor que. A condição fica “verdadeira” quando a leitura do sensor satisfaz esta comparação com o valor."],
  advValor: ["Valor", "Número de comparação: °C para temperaturas, % para umidade."],
  advComb: ["2ª condição", "Junte duas condições. E = as duas precisam ser verdadeiras. OU = basta uma. Ex.: temperatura ≥ 37,5 E umidade < 50%."],
  advOff: ["Desligar quando", "Como a regra volta ao estado inativo.\n• Inverso da condição: assim que a condição deixa de valer (ex.: liga com T ≥ 37,5 e desliga com T < 37,5).\n• Condição própria (histerese): você define o ponto de desligar, ex.: liga com T ≥ 37,5 e só desliga com T ≤ 37,3 — evita o liga/desliga rápido."],
  advEntao: ["ENTÃO (regra ativa)", "O que fazer com cada relé quando a regra está ATIVA: “Liga” liga o relé; “Desliga” desliga; “—” não mexe. Um relé marcado “Liga” aqui e sem ação no SENÃO é DESLIGADO automaticamente quando a regra ficar inativa (liga enquanto a regra vale)."],
  advSenao: ["SENÃO (regra inativa)", "O que fazer quando a regra está INATIVA. Ex.: ENTÃO CH1 liga, SENÃO CH4 liga — enquanto a condição vale o CH1 trabalha; quando deixa de valer o CH4 assume."],
  advJanela: ["Janela de horário", "Limita a regra a um período do dia (das … até …). Horas iguais = o dia todo. Fora da janela a regra fica inativa."],
  advGeral: ["Regras avançadas", "Combine sensores e relés livremente: SE condição ENTÃO ações SENÃO ações. Até 8 regras. Um relé citado numa regra avançada ativa deixa de seguir o modo da regra por relé acima. Segurança: o aquecedor (CH1) nunca é ligado em sobretemperatura, mexer num relé manualmente pausa a automação dele por 5 min e cada relé muda no máximo a cada 30 s."],
};
function ajudaBtn(chave) {
  const b = document.createElement("button");
  b.type = "button"; b.className = "ajuda-btn"; b.dataset.ajuda = chave; b.textContent = "?";
  b.setAttribute("aria-label", "Ajuda: " + (AJUDA[chave] ? AJUDA[chave][0] : chave));
  return b;
}
function ajudaFechar() { const o = document.getElementById("ajuda-overlay"); if (o) o.remove(); }
function ajudaAbrir(chave) {
  const a = AJUDA[chave]; if (!a) return;
  ajudaFechar();
  const o = document.createElement("div"); o.id = "ajuda-overlay"; o.className = "ajuda-overlay";
  const c = document.createElement("div"); c.className = "ajuda-card"; c.setAttribute("role", "dialog");
  const t = document.createElement("div"); t.className = "ajuda-titulo"; t.textContent = a[0];
  const p = document.createElement("div"); p.className = "ajuda-texto"; p.textContent = a[1];
  const d = document.createElement("div"); d.className = "ajuda-dica"; d.textContent = "Toque em qualquer lugar para fechar";
  c.append(t, p, d); o.appendChild(c); document.body.appendChild(o);
}
document.addEventListener("click", (e) => {
  const b = e.target.closest && e.target.closest(".ajuda-btn");
  if (b) { e.preventDefault(); e.stopPropagation(); ajudaAbrir(b.dataset.ajuda); return; }
  if (document.getElementById("ajuda-overlay")) { e.preventDefault(); e.stopPropagation(); ajudaFechar(); }
}, true);
document.addEventListener("keydown", (e) => { if (e.key === "Escape") ajudaFechar(); });

function autoCampo(rotulo, el, largo, ajuda) {
  const l = document.createElement("label");
  if (largo) l.className = "auto-largo";
  const rot = document.createElement("span"); rot.className = "auto-rot";
  rot.append(document.createTextNode(rotulo));
  if (ajuda) rot.appendChild(ajudaBtn(ajuda));
  l.append(rot, el);
  return l;
}
function autoNumero(valor, min, max, step, onChange) {
  const i = document.createElement("input");
  i.type = "number"; i.min = min; i.max = max; i.step = step; i.value = valor; i.inputMode = "decimal";
  i.addEventListener("input", () => { const v = Number(i.value); if (Number.isFinite(v)) onChange(v); });
  return i;
}
function autoRenderCanal(idx) {
  const r = autoCfg.regras[idx];
  const box = document.createElement("div");
  box.className = "auto-canal";
  const topo = document.createElement("div");
  topo.className = "auto-canal-topo";
  const nome = document.createElement("span");
  nome.textContent = `CH${idx + 1} · ${RELAY_NAMES[idx]}`;
  topo.appendChild(nome);
  if (idx !== AUTO_COOLER) topo.appendChild(ajudaBtn("modo"));
  box.appendChild(topo);
  if (idx === AUTO_COOLER) {
    const f = document.createElement("div");
    f.className = "auto-fixo";
    f.textContent = "Automático pela temperatura do chip (liga ≥ 55 °C, desliga ≤ 48 °C) — não editável.";
    box.appendChild(f);
    return box;
  }
  const marcar = () => { autoSujo = true; autoMsg("Alterações não salvas."); };
  const sel = document.createElement("select");
  AUTO_MODOS.forEach((n, m) => { const o = document.createElement("option"); o.value = m; o.textContent = n; sel.appendChild(o); });
  sel.value = r.modo;
  sel.addEventListener("change", () => {
    r.modo = Number(sel.value);
    if (r.modo === 1 || r.modo === 2) Object.assign(r, { alvo10: 375, hist10: 2, alvoCustom: 0 });
    if (r.modo === 3 || r.modo === 4) Object.assign(r, { alvo10: 550, hist10: 10, alvoCustom: 0 });
    if (r.modo === 7) r.onSeg = 30;
    marcar(); autoRender();
  });
  box.appendChild(sel);
  const campos = document.createElement("div");
  campos.className = "auto-campos";
  const temp = r.modo === 1 || r.modo === 2, umid = r.modo === 3 || r.modo === 4;
  if (temp || umid) {
    const alvoSel = document.createElement("select");
    [["0", "Do tipo do ciclo"], ["1", "Personalizado"]].forEach(([v, t]) => { const o = document.createElement("option"); o.value = v; o.textContent = t; alvoSel.appendChild(o); });
    alvoSel.value = String(r.alvoCustom);
    alvoSel.addEventListener("change", () => { r.alvoCustom = Number(alvoSel.value); marcar(); autoRender(); });
    campos.appendChild(autoCampo("Alvo", alvoSel, !r.alvoCustom, "alvo"));
    if (r.alvoCustom) campos.appendChild(autoCampo(temp ? "Valor (°C)" : "Valor (%)", autoNumero((r.alvo10 / 10).toFixed(1), temp ? 30 : 20, temp ? 42 : 95, "0.1", (v) => { r.alvo10 = Math.round(v * 10); marcar(); }), false, "valorAlvo"));
    campos.appendChild(autoCampo(temp ? "Histerese (°C)" : "Histerese (%)", autoNumero((r.hist10 / 10).toFixed(1), 0.1, temp ? 5 : 20, "0.1", (v) => { r.hist10 = Math.max(1, Math.round(v * 10)); marcar(); }), false, "hist"));
  } else if (r.modo === 7) {
    campos.appendChild(autoCampo("Pulso do motor (segundos)", autoNumero(r.onSeg, 5, 600, 5, (v) => { r.onSeg = Math.round(v); marcar(); }), true, "vira"));
  } else if (r.modo === 5) {
    campos.appendChild(autoCampo("Liga por (segundos)", autoNumero(r.onSeg, 5, 3600, 5, (v) => { r.onSeg = Math.round(v); marcar(); }), false, "ligaPor"));
    campos.appendChild(autoCampo("A cada (minutos)", autoNumero(r.cadaMin, 1, 1440, 1, (v) => { r.cadaMin = Math.max(1, Math.round(v)); marcar(); }), false, "cadaMin"));
  } else if (r.modo === 6) {
    const a = document.createElement("input"); a.type = "time"; a.value = minParaHHMM(r.hIni);
    a.addEventListener("input", () => { r.hIni = hhmmParaMin(a.value); marcar(); });
    const b = document.createElement("input"); b.type = "time"; b.value = minParaHHMM(r.hFim);
    b.addEventListener("input", () => { r.hFim = hhmmParaMin(b.value); marcar(); });
    campos.append(autoCampo("Liga às", a, false, "horaLiga"), autoCampo("Desliga às", b, false, "horaLiga"));
  }
  campos.appendChild(autoCampo("Desliga sozinho após (min, 0 = nunca)", autoNumero(r.autoOffMin, 0, 720, 1, (v) => { r.autoOffMin = Math.max(0, Math.round(v)); marcar(); }), true, "autoOff"));
  box.appendChild(campos);
  return box;
}
// Fase 110 — chave da automação também no card Atuadores (espelha "Automação ativa"; o aparelho a desliga sozinho quando o usuário mexe
// num atuador controlado pela automação, e o PWA mostra isso porque recebe de volta o cmd.auto).
const atuAutoEl = document.getElementById("atu-auto"), atuAutoEstado = document.getElementById("atu-auto-estado");
function atuAutoRender() {
  if (!atuAutoEl) return;
  atuAutoEl.checked = !!autoCfg.ativo;
  if (atuAutoEstado) atuAutoEstado.textContent = autoCfg.ativo ? "ativa — o aparelho controla o ambiente" : ("desligada — o ambiente NÃO está sendo controlado" + ((autoCfg.retoma > 0) ? ` (volta sozinha em até ${autoCfg.retoma} min se houver ciclo)` : ""));
  const semPermissao = typeof state !== "undefined" && state.papel === "convidado" && !(state.perm || []).includes("total");
  atuAutoEl.disabled = semPermissao;
}
function autoRender() {
  autoEls.ativo.checked = autoCfg.ativo;
  autoEls.soCiclo.checked = autoCfg.soCiclo;
  atuAutoRender();
  if (autoEls.retoma && document.activeElement !== autoEls.retoma) autoEls.retoma.value = String(autoCfg.retoma === undefined ? 10 : autoCfg.retoma);
  autoEls.lista.textContent = "";
  for (let i = 0; i < AUTO_N; i++) autoEls.lista.appendChild(autoRenderCanal(i));
  if (typeof advRender === "function") advRender();
}
async function autoGravar(msgOk) {
  if (!state.chocadeiraId) return;
  try {
    await chocadeiraRef().update({ "cmd.auto": autoSerialize(autoCfg) });
    registrarAcao("Automacao", "Alterou a configuração de automação");
    autoSujo = false;
    autoMsg(msgOk);
  } catch (e) { console.error(e); autoMsg("Não foi possível salvar.", true); }
}
autoEls.ativo.addEventListener("change", () => {
  autoCfg.ativo = autoEls.ativo.checked;
  autoGravar(autoCfg.ativo ? "Automação ATIVADA — o aparelho passa a comandar os relés conforme as regras." : "Automação desativada — o aparelho desliga o que ela tinha ligado.");
});
if (atuAutoEl) atuAutoEl.addEventListener("change", () => {   // Fase 110
  autoCfg.ativo = atuAutoEl.checked; autoEls.ativo.checked = autoCfg.ativo; atuAutoRender();
  autoGravar(autoCfg.ativo ? "Automação ATIVADA — o aparelho volta a comandar os relés." : "Automação desativada — o aparelho para de comandar os relés.");
});
autoEls.soCiclo.addEventListener("change", () => { autoCfg.soCiclo = autoEls.soCiclo.checked; autoGravar("Salvo."); });
if (autoEls.retoma) autoEls.retoma.addEventListener("change", () => {   // Fase 111
  const v = Math.max(0, Math.min(240, Math.round(Number(autoEls.retoma.value) || 0)));
  autoEls.retoma.value = String(v); autoCfg.retoma = v; autoRender();
  autoGravar(v > 0 ? `Salvo — com ciclo ativo/pausado a automação volta sozinha após ${v} min desligada.` : "Salvo — a automação não volta sozinha.");
});
autoEls.salvar.addEventListener("click", () => autoGravar("Regras salvas. O aparelho aplica em alguns segundos."));
autoEls.padrao.addEventListener("click", () => {
  if (!confirm("Restaurar as regras padrão? (só vale depois de Salvar; a automação continua como está)")) return;
  const ativo = autoCfg.ativo, soCiclo = autoCfg.soCiclo;
  const advAtual = autoCfg.adv || [];
  autoCfg = autoPadrao(); autoCfg.ativo = ativo; autoCfg.soCiclo = soCiclo; autoCfg.adv = advAtual; // "Restaurar padrão" só mexe nas regras por relé
  autoSujo = true; autoMsg("Padrão carregado — toque em Salvar regras."); autoRender();
});
// Recebe o valor do documento (chamado por onChocadeiraDoc). Não sobrescreve edição em andamento.
let autoUltimoRaw = null;
let hwUltimoRaw = null;
window.autoDoDoc = function (raw) {
  if (raw !== hwUltimoRaw) {   // Fase 78: o card Hardware segue o servidor enquanto o usuário não estiver editando
    hwUltimoRaw = raw;
    const ph = autoParse(raw);
    if (!hwSujo) { hwEd = ph && ph.hw ? { ...ph.hw } : (hwEd || hwPadrao()); hwRender(); }
  }
  if (autoSujo || raw === autoUltimoRaw) return; // só redesenha quando o valor do servidor mudou (heartbeat não tira o foco dos campos)
  autoUltimoRaw = raw;
  const p = autoParse(raw);
  autoCfg = p || autoPadrao();
  autoRender();
};
autoRender();

// ═══════════════════════════════════════════════════════════════
// Fase 78 — Card "Hardware" (Configurações). Mesmos dados da tela Configurações > Hardware do aparelho; vão dentro de cmd.auto.
//   "H,vol,iso,aqTipo,aqW,aqQtd,rele,umTipo,umMlH,umQtd,fanQtd,bomba,bombaMlS10,bombaPrimeS10"
// As estimativas do resumo repetem as contas do firmware (hwPtot/hwCap/hwRonPrior/hwIssPrior) só para mostrar ao usuário.
// ═══════════════════════════════════════════════════════════════
const HW_ISO = ["Isopor (EPS)", "Isolada (comercial)", "Madeira", "Plástico", "Metal"];
const HW_ISO_K = [1.0, 0.5, 2.0, 3.0, 5.0];
const HW_AQ = ["Resistência", "Lâmpada", "PTC cerâmico", "Manta / cabo"];
const HW_AQ_LAG = [1.0, 0.8, 1.3, 2.0];
const HW_REL = ["Mecânico", "Estado sólido (SSR)"];
const HW_UM = ["Ultrassônico", "Vapor", "Evaporativo"];
const HW_BOMBA = ["Não instalada", "Instalada"];
function hwPadrao() { return { volL: 3, iso: 0, aqTipo: 0, aqW: 100, aqQtd: 1, rele: 0, umTipo: 0, umMlH: 300, umQtd: 1, fanQtd: 1, bomba: 0, bombaMlS: 0, bombaPrimeS: 0 }; }
function hwParse(tk) {
  const v = tk.split(",").map(Number); const g = (i, d) => (Number.isFinite(v[i]) ? v[i] : d);
  const cl = (x, a, b) => Math.min(b, Math.max(a, Math.round(x)));
  return { volL: cl(g(1, 3), 1, 500), iso: cl(g(2, 0), 0, 4), aqTipo: cl(g(3, 0), 0, 3), aqW: cl(g(4, 100), 5, 2000), aqQtd: cl(g(5, 1), 1, 8),
    rele: g(6, 0) ? 1 : 0, umTipo: cl(g(7, 0), 0, 2), umMlH: cl(g(8, 300), 10, 3000), umQtd: cl(g(9, 1), 0, 4), fanQtd: cl(g(10, 1), 0, 8), bomba: g(11, 0) ? 1 : 0, bombaMlS: Math.min(50, Math.max(0, g(12, 0) / 10)), bombaPrimeS: Math.min(10, Math.max(0, g(13, 0) / 10)) };
}
function hwSerializar(h) { return ["H", h.volL, h.iso, h.aqTipo, h.aqW, h.aqQtd, h.rele, h.umTipo, h.umMlH, h.umQtd, h.fanQtd, h.bomba || 0, Math.round((h.bombaMlS || 0) * 10), Math.round((h.bombaPrimeS || 0) * 10)].join(","); }
Object.assign(AJUDA, {
  hwGeral: ["Hardware", "Diga ao aparelho o que está instalado. Com a potência do aquecedor e o volume da caixa ele calcula o período de chaveamento, o ganho do controle e o duty inicial — em vez de descobrir tudo por tentativa. As estimativas continuam sendo refinadas sozinhas durante o uso (o aparelho mede a subida real de temperatura e de umidade)."],
  hwVol: ["Volume da caixa (L)", "Volume interno útil. Caixa maior = mais massa térmica = o aquecedor precisa de mais energia e responde mais devagar. O assistente de ciclo usa este valor."],
  hwIso: ["Material / isolamento", "Define quanto calor a caixa perde. Isopor com frestas é o ponto de partida; chocadeira comercial isolada perde bem menos; madeira, plástico e metal perdem mais (o aquecedor trabalha com duty maior)."],
  hwAq: ["Tipo de aquecedor", "Resistência e lâmpada respondem rápido; PTC é autolimitado e aquece mais devagar; manta/cabo tem inércia grande (o controle antecipa mais)."],
  hwAqW: ["Potência de cada aquecedor (W)", "Potência elétrica real de UM aquecedor. A soma (potência × quantidade) dividida pelo volume diz se o aquecedor é fraco, adequado ou forte demais para a caixa."],
  hwRele: ["Relé do aquecedor", "Relé mecânico tem vida útil limitada (~100 mil acionamentos): o aparelho nunca chaveia mais rápido que 15 s. Relé de estado sólido (SSR) aguenta chaveamento rápido (a partir de 3 s) e deixa a temperatura mais estável com aquecedores fortes numa caixa pequena."],
  hwUm: ["Umidificador", "Tipo e vazão (ml/h) de cada umidificador. O aparelho estima quantos %UR cada segundo ligado acrescenta na caixa e depois mede o valor real."],
  hwBomba: ["Bomba de água (CH3) — em desenvolvimento", "Bomba submersa (ex.: VARIALL VA-BA01, ligada na rede 110–220 V) no galão externo (~5 L), com mangueira até o reservatório interno do umidificador (~150 ml). Só entra em ação quando o umidificador trabalha e a umidade NÃO sobe (~6,5 min, tampa fechada). Para NÃO transbordar: a bomba fica DESATIVADA até você informar a vazão medida; dá pulsos de ~15 ml (até 4 por vez, 150 s de observação entre eles, para ao primeiro sinal de resposta), no máximo 1 sequência por hora e 120 ml por dia; e o relé do CH3 nunca fica ligado mais de 8 s seguidos, de qualquer origem. Sem a bomba (\"Não instalada\"): só avisa que a água acabou."],
  hwBombaVaz: ["Vazão com a mangueira cheia (ml/s)", "Meça no aparelho (Configurações > Atuadores > toque no CH3; o app não liga a bomba): ponta da mangueira num copo FORA da caixa, mesma mangueira, mesma altura e regulagem no mínimo, bomba ligada por ~5 s (o limite é 8 s). Vazão = ml ÷ segundos DEPOIS que a água começou a sair. Com 0 a bomba fica desativada. A ponta da mangueira no reservatório precisa ficar ACIMA do nível da água do galão (senão há sifão e transborda)."],
  hwBombaPrime: ["Tempo até a água sair (s)", "Sem válvula de retenção a mangueira esvazia sozinha entre os pulsos: a cada pulso a bomba gasta alguns segundos só para ENCHER a mangueira antes de entregar água. Meça com a mangueira vazia: ligue o CH3 e cronometre até a primeira gota sair (ex.: 3 s). O pulso de ~15 ml passa a durar: tempo de enchimento + 15 ml ÷ vazão (sempre limitado a 8 s)."],
  hwFan: ["Fans (CH8)", "Quantidade de ventoinhas circulando o ar. O aquecedor só liga com as Fans ligadas."],
});
let hwEd = null, hwSujo = false;
function hwMsg(t, erro) { const f = document.getElementById("hw-feedback"); if (f) { f.textContent = t; f.style.color = erro ? "var(--danger)" : ""; } }
function hwResumo() {
  const h = hwEd || hwPadrao(), el = document.getElementById("hw-resumo"); if (!el) return;
  const pt = h.aqW * h.aqQtd, wl = pt / h.volL;
  const cap = 3500 * Math.pow(h.volL / 3, 0.8), ron = 0.5 * pt / cap;
  const perIdeal = 0.15 / ron, perMin = h.rele ? 3 : 15, per = Math.min(30, Math.max(perMin, perIdeal));
  const ua = 4.4 * Math.pow(h.volL / 3, 0.667) * HW_ISO_K[h.iso];
  const duty = (alvo) => Math.round(Math.min(1.2, ua * (alvo - 25) / pt) * 100);
  const linhas = [
    `Aquecimento total: <b>${pt} W</b> (${wl.toFixed(1)} W/L) · subida estimada ≈ ${(ron * 60).toFixed(2).replace(".", ",")} °C/min`,
    `Ciclo de chaveamento do aquecedor: <b>${Math.round(per)} s</b> · duty de regime estimado: ${duty(37.5)}% a 37,5 °C, ${duty(40.5)}% a 40,5 °C`,
    `Umidificação total: <b>${h.umMlH * h.umQtd} ml/h</b>${h.umQtd ? "" : " (nenhum umidificador)"} · Fans: ${h.fanQtd}`,
  ];
  const avisos = [];
  if (duty(37.5) > 85) avisos.push("⚠ Potência provavelmente insuficiente para 37,5 °C nesta caixa (o aquecedor ficaria no limite). Aumente a potência/quantidade ou melhore o isolamento.");
  if (wl > 40) avisos.push("⚠ Potência muito alta para o volume (> 40 W/L): risco de ondulação e sobretemperatura. Prefira relé SSR ou um aquecedor menor.");
  if (!h.rele && perIdeal < 12) avisos.push("ℹ Com relé mecânico o ciclo mínimo é 15 s; para esta potência o ideal seria ≈ " + Math.max(3, Math.round(perIdeal)) + " s. Um relé SSR deixaria a temperatura mais estável.");
  if (h.fanQtd === 0) avisos.push("⚠ Sem Fans o aquecedor não liga (o ar precisa circular).");
  if (h.bomba) avisos.push("ℹ Bomba de água (CH3) em desenvolvimento: se a umidade não subir com o umidificador ligado, o aparelho dará pulsos de ~15 ml (até 4, só com a vazão informada) antes de avisar que a água acabou. A bomba VA-BA01 é de rede (220 V): ligue pelo relé CH3, com fusível/DR, longe da água do galão.");
  el.innerHTML = linhas.map((l) => `<div>${l}</div>`).join("") + avisos.map((a) => `<div class="hw-aviso">${a}</div>`).join("");
}
function hwRender() {
  const box = document.getElementById("hw-campos"); if (!box) return;
  const h = hwEd || (hwEd = hwPadrao());
  box.textContent = ""; box.classList.add("auto-lista");
  const marcar = () => { hwSujo = true; hwMsg("Alterações não salvas."); hwResumo(); };
  const sel = (lista, val, set) => {
    const s = document.createElement("select");
    lista.forEach((n, i) => { const o = document.createElement("option"); o.value = i; o.textContent = n; s.appendChild(o); });
    s.value = val; s.addEventListener("change", () => { set(Number(s.value)); marcar(); });
    return s;
  };
  const num = (val, min, max, step, set) => autoNumero(val, min, max, step, (v) => { set(Math.min(max, Math.max(min, Math.round(v)))); marcar(); });
  const grupo = (titulo, campos) => {
    // mesmo visual das caixas da Automação (.auto-canal): fundo escuro, campos escuros, texto claro
    const w = document.createElement("div"); w.className = "auto-canal";
    const t = document.createElement("div"); t.className = "auto-canal-topo"; t.textContent = titulo; w.appendChild(t);
    const g = document.createElement("div"); g.className = "auto-campos"; campos.forEach((c) => g.appendChild(c)); w.appendChild(g);
    box.appendChild(w);
  };
  grupo("Caixa", [
    autoCampo("Volume (L)", num(h.volL, 1, 500, 1, (v) => (h.volL = v)), false, "hwVol"),
    autoCampo("Material", sel(HW_ISO, h.iso, (v) => (h.iso = v)), false, "hwIso"),
  ]);
  grupo("Aquecedor (CH1)", [
    autoCampo("Tipo", sel(HW_AQ, h.aqTipo, (v) => (h.aqTipo = v)), false, "hwAq"),
    autoCampo("Potência (W)", num(h.aqW, 5, 2000, 5, (v) => (h.aqW = v)), false, "hwAqW"),
    autoCampo("Qtd.", num(h.aqQtd, 1, 8, 1, (v) => (h.aqQtd = v)), false, "hwAqW"),
    autoCampo("Relé", sel(HW_REL, h.rele, (v) => (h.rele = v)), false, "hwRele"),
  ]);
  grupo("Umidificador (CH2) e Fans (CH8)", [
    autoCampo("Tipo", sel(HW_UM, h.umTipo, (v) => (h.umTipo = v)), false, "hwUm"),
    autoCampo("Vazão (ml/h)", num(h.umMlH, 10, 3000, 10, (v) => (h.umMlH = v)), false, "hwUm"),
    autoCampo("Qtd. umidificadores", num(h.umQtd, 0, 4, 1, (v) => (h.umQtd = v)), false, "hwUm"),
    autoCampo("Qtd. Fans", num(h.fanQtd, 0, 8, 1, (v) => (h.fanQtd = v)), false, "hwFan"),
  ]);
  grupo("Bomba de água (CH3) · EM DESENVOLVIMENTO", [
    autoCampo("Bomba", sel(HW_BOMBA, h.bomba || 0, (v) => (h.bomba = v)), false, "hwBomba"),
    autoCampo("Vazão cheia (ml/s)", autoNumero(h.bombaMlS || 0, 0, 50, 0.5, (v) => { h.bombaMlS = Math.min(50, Math.max(0, Math.round(v * 2) / 2)); marcar(); }), false, "hwBombaVaz"),
    autoCampo("Enchimento (s)", autoNumero(h.bombaPrimeS || 0, 0, 10, 0.5, (v) => { h.bombaPrimeS = Math.min(10, Math.max(0, Math.round(v * 2) / 2)); marcar(); }), false, "hwBombaPrime"),
  ]);
  hwResumo();
}
// Fase 80 — o assistente de ciclo informou o volume da caixa: grava no Hardware (só se mudou)
async function hwAtualizarVolume(v) {
  try {
    const raw = hwUltimoRaw || autoUltimoRaw; if (!raw || !state.chocadeiraId) return;
    const base = autoParse(raw); if (!base) return;
    const atual = base.hw || hwPadrao();
    if (atual.volL === v || !(v >= 1 && v <= 500)) return;
    base.hw = { ...atual, volL: v };
    await chocadeiraRef().update({ "cmd.auto": autoSerialize(base) });
    hwUltimoRaw = null; autoUltimoRaw = null; if (!hwSujo) hwEd = { ...base.hw };
  } catch (err) { console.error("hwAtualizarVolume:", err); }
}
async function hwSalvar() {
  if (!state.chocadeiraId) return;
  try {
    // parte do que o servidor tem (não leva junto edições pendentes do card Automação) e troca só o hardware
    const base = autoParse(autoUltimoRaw || hwUltimoRaw || "") || autoPadrao();
    base.hw = { ...hwEd };
    await chocadeiraRef().update({ "cmd.auto": autoSerialize(base) });
    registrarAcao("Automacao", "Alterou o hardware (caixa, aquecedor, umidificador, fans ou bomba)");
    hwSujo = false; hwUltimoRaw = null; autoUltimoRaw = null;
    hwMsg("Salvo — o aparelho aplica no próximo ciclo de sincronização (≈ 1 min).");
  } catch (err) { console.error(err); hwMsg("Não foi possível salvar.", true); }
}
document.getElementById("btn-hw-salvar").addEventListener("click", hwSalvar);
document.getElementById("btn-hw-padrao").addEventListener("click", () => {
  if (!confirm("Voltar ao padrão (caixa de isopor de 3 L, 1 resistência de 100 W, relé mecânico, 1 umidificador, 1 fan)? Só vale depois de Salvar.")) return;
  hwEd = hwPadrao(); hwSujo = true; hwMsg("Padrão carregado — toque em Salvar hardware."); hwRender();
});
hwRender();

// ═══════════════════════════════════════════════════════════════
// Fase 53 — Notificações: o que receber (por dispositivo)
// Cada entrada em pushTokens agora é "token|flags" (bit 1 queda de energia · 2 internet · 4 faixa T/U · 8 fim de ciclo);
// entradas antigas sem "|" valem como "tudo". O aparelho só envia para quem marcou o tipo do evento.
// Queda de energia e perda de internet são avisos PÓS-FATO (o aparelho sem energia/sem rede não consegue avisar antes):
// ele avisa quando religa / quando a internet volta. Com o app aberto, o PWA também avisa na hora em que o aparelho some.
// ═══════════════════════════════════════════════════════════════
const PUSH_FLAGS_KEY = "choca_push_flags";
const pushOptEls = {
  energia: document.getElementById("push-opt-energia"),
  conexao: document.getElementById("push-opt-conexao"),
  faixa: document.getElementById("push-opt-faixa"),
  fim: document.getElementById("push-opt-fim"),
  visao: document.getElementById("push-opt-visao"),
};
function pushFlags() {
  const v = parseInt(localStorage.getItem(PUSH_FLAGS_KEY), 10);
  return Number.isFinite(v) ? v : 15;
}
function pushEntradas(token) {
  return (state.pushTokens || []).filter((t) => String(t).split("|")[0] === token);
}
async function pushGravarEntrada(token) {
  const ref = chocadeiraRef();
  const novo = `${token}|${pushFlags()}`;
  const velhas = pushEntradas(token).filter((t) => t !== novo);
  if (velhas.length) await ref.update({ pushTokens: firebase.firestore.FieldValue.arrayRemove(...velhas) });
  await ref.update({ pushTokens: firebase.firestore.FieldValue.arrayUnion(novo) });
  state.pushTokens = [...(state.pushTokens || []).filter((t) => String(t).split("|")[0] !== token), novo];
}
function pushAplicarOpcoes(ativas) {
  const f = pushFlags();
  pushOptEls.energia.checked = !!(f & 1);
  pushOptEls.conexao.checked = !!(f & 2);
  pushOptEls.faixa.checked = !!(f & 4);
  pushOptEls.fim.checked = !!(f & 8);
  pushOptEls.visao.checked = !!(f & 16);
  Object.values(pushOptEls).forEach((el) => { el.disabled = !ativas; });
}
Object.values(pushOptEls).forEach((el) => {
  el.addEventListener("change", async () => {
    const f = (pushOptEls.energia.checked ? 1 : 0) | (pushOptEls.conexao.checked ? 2 : 0) | (pushOptEls.faixa.checked ? 4 : 0) | (pushOptEls.fim.checked ? 8 : 0) | (pushOptEls.visao.checked ? 16 : 0);
    localStorage.setItem(PUSH_FLAGS_KEY, String(f));
    const token = localStorage.getItem(PUSH_KEY);
    if (!token) return;
    try { await pushGravarEntrada(token); pushEls.status.textContent = "Preferências salvas."; }
    catch (e) { console.error(e); pushEls.status.textContent = "Não foi possível salvar as preferências."; }
  });
});
// Aparelho ficou offline com o app aberto → notificação local (se "perda de conexão" estiver marcada).
// Só com o app VISÍVEL (em segundo plano o listener do Firestore para e gerava falsos "offline") e depois de
// confirmar no SERVIDOR que o último sinal está mesmo velho (1 leitura, só na transição).
let ultEstadoConexao = null, offlineConfirmando = false;
setInterval(async () => {
  const atual = state.estadoConexao;
  const transicao = ultEstadoConexao === "online" && atual === "device-offline";
  ultEstadoConexao = atual;
  if (!transicao || offlineConfirmando || document.hidden || !(pushFlags() & 2) ||
      !("Notification" in window) || Notification.permission !== "granted" || !localStorage.getItem(PUSH_KEY)) return;
  offlineConfirmando = true;
  try {
    const snap = await chocadeiraRef().get({ source: "server" });
    const d = snap.data() || {};
    const ts = d.firmwareLastSeen && d.firmwareLastSeen.toDate ? d.firmwareLastSeen.toDate() : null;
    if (ts) state.firmwareLastSeen = ts;
    const velho = !ts || Date.now() - ts.getTime() >= HEARTBEAT_STALE_MS;
    if (velho && !document.hidden) {
      const reg = await navigator.serviceWorker.ready;
      reg.showNotification("ChocaIoT: aparelho offline", { body: "A chocadeira parou de enviar sinal (sem energia ou sem internet).", tag: "choca-offline", icon: "/icons/icon-192.png" });
    }
  } catch (e) { /* sem rede: não avisa */ }
  offlineConfirmando = false;
}, 5000);

// ═══════════════════════════════════════════════════════════════
// Fase 53 — Bloqueio do app: biometria (WebAuthn, autenticador do próprio celular) no mobile; PIN no desktop.
// Bloqueio LOCAL a este navegador (não sincroniza): trava na abertura, ao voltar depois de 30 s e após 5 min parado.
// Independente do PIN do aparelho (firmware). Esqueceu/sem biometria → "sair da conta" limpa o bloqueio (relogin pelo Google).
// ═══════════════════════════════════════════════════════════════
const LOCK_KEY = "choca_applock";
const LOCK_VOLTA_MS = 30000, LOCK_IDLE_MS = 5 * 60000;
const lockEls = {
  overlay: document.getElementById("app-lock"),
  titulo: document.getElementById("lock-titulo"),
  msg: document.getElementById("lock-msg"),
  pin: document.getElementById("lock-pin"),
  pin2: document.getElementById("lock-pin2"),
  ok: document.getElementById("lock-ok"),
  bio: document.getElementById("lock-bio"),
  cancel: document.getElementById("lock-cancel"),
  sair: document.getElementById("lock-sair"),
  toggle: document.getElementById("seg-app-lock"),
  label: document.getElementById("seg-app-label"),
  info: document.getElementById("seg-app-info"),
  fw: document.getElementById("seg-fw-lock"),
};
const ehMobile = () => /Android|iPhone|iPad|iPod|Mobile/i.test(navigator.userAgent) || (navigator.maxTouchPoints > 1 && window.matchMedia("(pointer: coarse)").matches);
function lockCfg() { try { return JSON.parse(localStorage.getItem(LOCK_KEY) || "null"); } catch (e) { return null; } }
function lockSalvar(c) { if (c) localStorage.setItem(LOCK_KEY, JSON.stringify(c)); else localStorage.removeItem(LOCK_KEY); }
async function bioDisponivel() {
  try { return !!(window.PublicKeyCredential && PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable && await PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable()); }
  catch (e) { return false; }
}
const b64u = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const deB64u = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));
async function lockHash(pin, salt) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(`${salt}:${pin}`));
  return Array.from(new Uint8Array(d)).map((x) => x.toString(16).padStart(2, "0")).join("");
}
async function bioRegistrar() {
  const email = (auth.currentUser && auth.currentUser.email) || "usuario";
  const cred = await navigator.credentials.create({ publicKey: {
    challenge: crypto.getRandomValues(new Uint8Array(32)),
    rp: { name: "ChocaIoT", id: location.hostname },
    user: { id: crypto.getRandomValues(new Uint8Array(16)), name: email, displayName: email },
    pubKeyCredParams: [{ type: "public-key", alg: -7 }, { type: "public-key", alg: -257 }],
    authenticatorSelection: { authenticatorAttachment: "platform", userVerification: "required" },
    timeout: 60000, attestation: "none",
  } });
  return b64u(cred.rawId);
}
async function bioVerificar(credId) {
  await navigator.credentials.get({ publicKey: {
    challenge: crypto.getRandomValues(new Uint8Array(32)),
    allowCredentials: [{ type: "public-key", id: deB64u(credId), transports: ["internal"] }],
    userVerification: "required", timeout: 60000, rpId: location.hostname,
  } });
  return true;
}

let lockResolver = null, lockErros = 0, lockBloqAte = 0, lockModo = "unlock";
function lockAbrirOverlay(modo, titulo, msg) {
  lockModo = modo;
  lockEls.titulo.textContent = titulo;
  lockEls.msg.textContent = msg || "";
  lockEls.pin.value = ""; lockEls.pin2.value = "";
  const cfg = lockCfg();
  lockEls.pin.hidden = modo === "unlock" && cfg && cfg.tipo === "bio";
  lockEls.pin2.hidden = modo !== "novo";
  lockEls.bio.hidden = !(modo === "unlock" && cfg && cfg.tipo === "bio");
  lockEls.cancel.hidden = modo === "unlock" && !lockResolver;
  lockEls.ok.textContent = modo === "novo" ? "Definir PIN" : "Desbloquear";
  lockEls.ok.hidden = modo === "unlock" && cfg && cfg.tipo === "bio";
  lockEls.sair.hidden = modo === "novo";
  lockEls.overlay.hidden = false;
  document.body.classList.add("bloqueado");
  setTimeout(() => { if (!lockEls.pin.hidden) lockEls.pin.focus(); }, 50);
}
function lockFecharOverlay() {
  lockEls.overlay.hidden = true;
  document.body.classList.remove("bloqueado");
  lockAtividade = Date.now();
}
// Pede a verificação (usada pra desligar o bloqueio). Resolve true/false.
function lockVerificar(titulo) {
  return new Promise((res) => {
    lockResolver = res;
    lockAbrirOverlay("unlock", titulo || "Confirme para continuar", "");
    lockEls.cancel.hidden = false;
    if (lockCfg() && lockCfg().tipo === "bio") lockTentarBio();
  });
}
async function lockTentarBio() {
  const cfg = lockCfg();
  if (!cfg || cfg.tipo !== "bio") return;
  try { await bioVerificar(cfg.credId); lockSucesso(); }
  catch (e) { lockEls.msg.textContent = "Não foi possível verificar. Toque em \"Usar biometria\" para tentar de novo."; }
}
function lockSucesso() {
  lockErros = 0;
  lockFecharOverlay();
  if (lockResolver) { const r = lockResolver; lockResolver = null; r(true); }
}
async function lockConfirmarPin() {
  const cfg = lockCfg();
  const agora = Date.now();
  if (lockModo === "novo") {
    const a = lockEls.pin.value.trim(), b = lockEls.pin2.value.trim();
    if (!/^\d{4,8}$/.test(a)) { lockEls.msg.textContent = "Use de 4 a 8 números."; return; }
    if (a !== b) { lockEls.msg.textContent = "Os PINs não conferem."; return; }
    const salt = b64u(crypto.getRandomValues(new Uint8Array(12)));
    lockFecharOverlay();
    if (lockResolver) { const r = lockResolver; lockResolver = null; r({ tipo: "pin", salt, hash: await lockHash(a, salt) }); }
    return;
  }
  if (!cfg || cfg.tipo !== "pin") return;
  if (agora < lockBloqAte) { lockEls.msg.textContent = `Aguarde ${Math.ceil((lockBloqAte - agora) / 1000)} s.`; return; }
  const h = await lockHash(lockEls.pin.value.trim(), cfg.salt);
  if (h === cfg.hash) { lockSucesso(); return; }
  lockErros++;
  lockEls.pin.value = "";
  if (lockErros >= 5) { lockErros = 0; lockBloqAte = Date.now() + 30000; lockEls.msg.textContent = "Muitas tentativas — aguarde 30 s."; }
  else lockEls.msg.textContent = "PIN incorreto.";
}
function lockPedirNovoPin() {
  return new Promise((res) => { lockResolver = res; lockAbrirOverlay("novo", "Definir PIN do app", "Escolha de 4 a 8 números. Vale só neste computador."); lockEls.cancel.hidden = false; });
}
lockEls.ok.addEventListener("click", lockConfirmarPin);
lockEls.pin.addEventListener("keydown", (e) => { if (e.key === "Enter") { if (lockModo === "novo") lockEls.pin2.focus(); else lockConfirmarPin(); } });
lockEls.pin2.addEventListener("keydown", (e) => { if (e.key === "Enter") lockConfirmarPin(); });
lockEls.bio.addEventListener("click", lockTentarBio);
lockEls.cancel.addEventListener("click", () => {
  lockEls.overlay.hidden = true; document.body.classList.remove("bloqueado");
  if (lockResolver) { const r = lockResolver; lockResolver = null; r(lockModo === "novo" ? null : false); }
});
lockEls.sair.addEventListener("click", async () => {
  if (!confirm("Sair da conta? O bloqueio deste app será removido e você entra de novo com o Google.")) return;
  lockSalvar(null);
  lockEls.overlay.hidden = true; document.body.classList.remove("bloqueado");
  lockAtualizarUI();
  try { await auth.signOut(); } catch (e) { location.reload(); }
});

let lockAtividade = Date.now(), lockOcultoEm = 0;
function lockBloquear() {
  if (!lockCfg() || !lockEls.overlay.hidden) return;
  lockResolver = null;
  lockAbrirOverlay("unlock", "ChocaIoT bloqueado", lockCfg().tipo === "bio" ? "Use a biometria do celular para entrar." : "Digite o PIN do app.");
  if (lockCfg().tipo === "bio") lockTentarBio();
}
["pointerdown", "keydown", "scroll"].forEach((ev) => window.addEventListener(ev, () => { lockAtividade = Date.now(); }, { passive: true }));
setInterval(() => { if (lockCfg() && lockEls.overlay.hidden && Date.now() - lockAtividade > LOCK_IDLE_MS) lockBloquear(); }, 15000);
document.addEventListener("visibilitychange", () => {
  if (document.hidden) lockOcultoEm = Date.now();
  else if (lockCfg() && lockOcultoEm && Date.now() - lockOcultoEm >= LOCK_VOLTA_MS) lockBloquear();
});

function lockAtualizarUI() {
  const cfg = lockCfg();
  const mobile = ehMobile();
  lockEls.label.textContent = mobile ? "Bloquear este app com biometria" : "Bloquear este app com PIN";
  lockEls.toggle.checked = !!cfg;
  lockEls.info.textContent = cfg
    ? (cfg.tipo === "bio" ? "Ativo neste celular: biometria (digital/rosto) ao abrir o app, ao voltar depois de 30 s e após 5 min parado."
                          : "Ativo neste navegador: PIN ao abrir o app, ao voltar depois de 30 s e após 5 min parado.")
    : (mobile ? "Celular: usa a biometria do próprio aparelho (digital ou rosto)." : "Computador: usa um PIN de 4 a 8 números.");
}
lockEls.toggle.addEventListener("change", async () => {
  lockEls.toggle.disabled = true;
  try {
    if (lockEls.toggle.checked) {
      const usaBio = ehMobile() && await bioDisponivel();
      if (usaBio) lockSalvar({ tipo: "bio", credId: await bioRegistrar() });
      else {
        if (ehMobile()) lockEls.info.textContent = "Este celular não oferece biometria para o navegador — usando PIN.";
        const novo = await lockPedirNovoPin();
        if (!novo) { lockEls.toggle.checked = false; return; }
        lockSalvar(novo);
      }
    } else {
      const ok = await lockVerificar("Confirme para desativar o bloqueio");
      if (!ok) { lockEls.toggle.checked = true; return; }
      lockSalvar(null);
    }
  } catch (e) {
    console.error("Bloqueio do app:", e);
    lockEls.toggle.checked = !lockEls.toggle.checked;
    lockEls.info.textContent = "Não foi possível configurar: " + (e && e.message ? e.message : e);
    return;
  } finally {
    lockEls.toggle.disabled = false;
    lockAtualizarUI();
  }
});
// Bloqueio do aparelho (firmware): só dá para DESATIVAR daqui (cmd.pinOff); definir o PIN é no aparelho.
function lockAtualizarFw() {
  if (!lockEls.fw) return;
  lockEls.fw.checked = state.pinAtivo === true;
  lockEls.fw.disabled = state.pinAtivo === null;
}
lockEls.fw.addEventListener("change", async () => {
  if (lockEls.fw.checked) { lockEls.fw.checked = false; segMsg("O PIN do aparelho é definido no próprio aparelho (Configurações > Segurança), depois de vincular o e-mail.", false); return; }
  if (!confirm("Desativar o PIN da tela do aparelho? Qualquer pessoa junto dele poderá usá-lo.")) { lockEls.fw.checked = true; return; }
  if (state.estadoConexao !== "online") { lockEls.fw.checked = true; segMsg("Chocadeira offline — não dá para desativar agora.", true); return; }
  try { await chocadeiraRef().update({ "cmd.pinOff": Date.now().toString(36) }); segMsg("Pedido enviado — o aparelho desativa o PIN em alguns segundos."); }
  catch (e) { console.error(e); lockEls.fw.checked = true; segMsg("Não foi possível enviar o pedido.", true); }
});
lockAtualizarUI();
lockBloquear(); // trava já na abertura (antes de qualquer dado aparecer por trás do overlay)

// ═══════════════════════════════════════════════════════════════
// Fase 55 — Histórico POR CICLO. Cada ciclo (lote) tem nome, tipo e período; o usuário filtra por situação
// (em andamento · concluídos · interrompidos · excluídos), ordena e abre os gráficos de UM ciclo.
// Lista = chocadeiras/{id}/ciclos (relatórios gerados pelo aparelho) + o ciclo ativo (estado local).
// Gráfico = leitura dos dias do período em chocadeiras/{id}/historico/AAAA-MM-DD (1 leitura por dia). Dois formatos por dia:
//   "m" = mapa {s123: "t10,h10"} (atual)  ·  "pts" = "slot,t10,h10;..." (legado)
// ═══════════════════════════════════════════════════════════════
const histEls = {
  voltar: document.getElementById("btn-hist-voltar"),
  titulo: document.getElementById("hist-titulo"),
  vistaLista: document.getElementById("hist-lista-view"),
  vistaDet: document.getElementById("hist-detalhe-view"),
  chips: document.querySelectorAll(".hist-chip"),
  busca: document.getElementById("hist-busca"),
  ordem: document.getElementById("hist-ordem"),
  sub: document.getElementById("hist-sub"),
  ciclos: document.getElementById("hist-ciclos"),
  detNome: document.getElementById("hist-det-nome"),
  detMeta: document.getElementById("hist-det-meta"),
  detSub: document.getElementById("hist-det-sub"),
  detBadge: document.getElementById("hist-det-badge"),
  canvasT: document.getElementById("hist-canvas-temp"),
  canvasU: document.getElementById("hist-canvas-umid"),
  statsT: document.getElementById("hist-stats-temp"),
  statsU: document.getElementById("hist-stats-umid"),
  csv: document.getElementById("btn-hist-csv"),
  detVoltar: document.getElementById("btn-hist-lista"),
};
let histCiclos = [], histCarregadoEm = 0, histFiltro = "todos", histOrdem = "recentes", histBuscaTxt = "";
let histPontos = [], histSeq = 0, histAtual = null, histOrigem = "configuracoes";
const p2 = (n) => String(n).padStart(2, "0");
const ymdDe = (d) => `${d.getFullYear()}-${p2(d.getMonth() + 1)}-${p2(d.getDate())}`;
function inicioDia(d) { const x = new Date(d); x.setHours(0, 0, 0, 0); return x; }
const fmtDia = (d) => (d ? d.toLocaleDateString("pt-BR", { day: "2-digit", month: "2-digit", year: "numeric" }) : "--");
const SIT_ROTULO = { andamento: "Em andamento", concluido: "Concluído", interrompido: "Interrompido", excluido: "Excluído" };

async function histBuscar(inicio, fim) {
  const snap = await chocadeiraRef().collection("historico")
    .orderBy(firebase.firestore.FieldPath.documentId()).startAt(ymdDe(inicio)).endAt(ymdDe(fim)).get();
  const pontos = [];
  snap.docs.forEach((doc) => {
    const [y, m, d] = doc.id.split("-").map(Number);
    const dat = doc.data();
    const slots = {};
    if (typeof dat.pts === "string") dat.pts.split(";").forEach((p) => { const a = p.split(","); if (a.length >= 3) slots[a[0]] = [Number(a[1]), Number(a[2])]; });
    if (dat.m && typeof dat.m === "object") Object.keys(dat.m).forEach((k) => { const a = String(dat.m[k]).split(","); if (a.length >= 2) slots[k.replace(/^s/, "")] = [Number(a[0]), Number(a[1])]; });
    Object.keys(slots).forEach((s) => {
      const [t, h] = slots[s];
      if (Number.isFinite(t) && Number.isFinite(h)) pontos.push({ q: new Date(y, m - 1, d, 0, Number(s) * 10), t: t / 10, h: h / 10 });
    });
  });
  pontos.sort((a, b) => a.q - b.q);
  return { pontos, dias: snap.size };
}
function histReduzir(pontos, max) {
  if (pontos.length <= max) return pontos;
  const passo = pontos.length / max, out = [];
  for (let i = 0; i < max; i++) {
    const fatia = pontos.slice(Math.floor(i * passo), Math.floor((i + 1) * passo));
    if (!fatia.length) continue;
    const n = fatia.length;
    out.push({ q: new Date(fatia.reduce((s, p) => s + p.q.getTime(), 0) / n), t: fatia.reduce((s, p) => s + p.t, 0) / n, h: fatia.reduce((s, p) => s + p.h, 0) / n });
  }
  return out;
}
function histStats(el, valores, un, casas) {
  el.textContent = "";
  const add = (rot, txt) => {
    const d = document.createElement("div"); d.className = "sensor-stat";
    const a = document.createElement("span"); a.className = "sensor-stat-label"; a.textContent = rot;
    const b = document.createElement("span"); b.className = "sensor-stat-value"; b.textContent = txt;
    d.append(a, b); el.appendChild(d);
  };
  if (!valores.length) { add("Mínimo", "--"); add("Máximo", "--"); add("Média", "--"); return; }
  add("Mínimo", `${Math.min(...valores).toFixed(casas)}${un}`);
  add("Máximo", `${Math.max(...valores).toFixed(casas)}${un}`);
  add("Média", `${(valores.reduce((a, b) => a + b, 0) / valores.length).toFixed(casas)}${un}`);
}

// ── Lista de ciclos ──
async function histCarregarCiclos(forcar) {
  if (!state.chocadeiraId) return;
  if (!forcar && histCiclos.length && Date.now() - histCarregadoEm < 5 * 60000) { histRenderLista(); return; }
  histEls.sub.textContent = "Carregando ciclos...";
  const lista = [];
  try {
    const snap = await chocadeiraRef().collection("ciclos").orderBy("fim", "desc").limit(60).get();
    snap.docs.forEach((d) => {
      const r = d.data();
      lista.push({ id: d.id, nome: r.nome || "Lote", tipo: r.tipo || "--", inicio: r.inicio && r.inicio.toDate(), fim: r.fim && r.fim.toDate(),
        situacao: ["concluido", "interrompido", "excluido"].includes(r.situacao) ? r.situacao : "concluido", dias: r.dias, rel: r });
    });
  } catch (e) { console.error("Erro ao listar ciclos:", e); histEls.sub.textContent = "Não foi possível carregar os ciclos."; }
  histCiclos = lista;
  histCarregadoEm = Date.now();
  histRenderLista();
}
function histListaCompleta() {
  const todos = [...histCiclos];
  if (state.cicloIniciadoEm) {
    todos.unshift({ id: "ativo", nome: state.cicloNome || "Ciclo atual", tipo: state.cicloTipoNome || "--", inicio: state.cicloIniciadoEm, fim: null,
      situacao: "andamento", pausado: !!state.cicloPausadoEm, dias: cicloTotalDias() });
  }
  return todos;
}
function histRenderLista() {
  const todos = histListaCompleta();
  const q = histBuscaTxt.trim().toLowerCase();
  let lista = todos.filter((c) => (histFiltro === "todos" || c.situacao === histFiltro) && (!q || `${c.nome} ${c.tipo}`.toLowerCase().includes(q)));
  const t = (c) => (c.inicio ? c.inicio.getTime() : 0);
  lista.sort(histOrdem === "antigos" ? (a, b) => t(a) - t(b)
    : histOrdem === "nome" ? (a, b) => a.nome.localeCompare(b.nome, "pt-BR", { numeric: true })
    : histOrdem === "duracao" ? (a, b) => ((b.fim || new Date()) - b.inicio) - ((a.fim || new Date()) - a.inicio)
    : (a, b) => t(b) - t(a));
  histEls.ciclos.textContent = "";
  histEls.sub.textContent = todos.length ? `${lista.length} de ${todos.length} ciclo(s)` : "Nenhum ciclo ainda — os gráficos de cada lote aparecem aqui.";
  if (!lista.length) {
    const v = document.createElement("div"); v.className = "logs-empty";
    v.textContent = todos.length ? "Nenhum ciclo com esse filtro." : "Nenhum ciclo ainda.";
    histEls.ciclos.appendChild(v);
    return;
  }
  lista.forEach((c) => {
    const btn = document.createElement("button");
    btn.type = "button"; btn.className = "hist-ciclo";
    const l1 = document.createElement("span"); l1.className = "hist-ciclo-nome"; l1.textContent = c.nome;
    const l2 = document.createElement("span"); l2.className = "hist-ciclo-sub";
    const dur = c.inicio ? Math.max(1, Math.round(((c.fim || new Date()) - c.inicio) / 86400000)) : "--";
    l2.textContent = `${c.tipo} · ${fmtDia(c.inicio)} → ${c.fim ? fmtDia(c.fim) : "hoje"} · ${dur} dia(s)`;
    const b = document.createElement("span"); b.className = `hist-badge sit-${c.situacao}`; b.textContent = SIT_ROTULO[c.situacao] + (c.pausado ? " (pausado)" : "");
    btn.append(l1, l2, b);
    btn.addEventListener("click", () => histAbrirCiclo(c));
    histEls.ciclos.appendChild(btn);
  });
}

// ── Detalhe (gráficos) de um ciclo ──
function histMostrarVista(detalhe) {
  histEls.vistaLista.hidden = detalhe; histEls.vistaDet.hidden = !detalhe;
  histEls.titulo.textContent = detalhe ? "Histórico do ciclo" : "Histórico de ciclos";
}
async function histAbrirCiclo(c) {
  histAtual = c;
  histMostrarVista(true);
  histEls.detNome.textContent = c.nome;
  histEls.detMeta.textContent = `${c.tipo} · ${fmtDia(c.inicio)} → ${c.fim ? fmtDia(c.fim) : "em andamento"}`;
  histEls.detBadge.textContent = SIT_ROTULO[c.situacao] + (c.pausado ? " (pausado)" : "");
  histEls.detBadge.className = `hist-badge sit-${c.situacao}`;
  histEls.detSub.textContent = "Carregando gráficos...";
  const seq = ++histSeq;
  const fimReal = c.fim || new Date();
  let ini = inicioDia(c.inicio || fimReal); const fimDia = inicioDia(fimReal);
  if ((fimDia - ini) / 86400000 > 61) { ini = new Date(fimDia); ini.setDate(ini.getDate() - 61); }
  try {
    const { pontos, dias } = await histBuscar(ini, fimDia);
    if (seq !== histSeq) return;
    histPontos = pontos.filter((p) => p.q >= (c.inicio || 0) && p.q <= new Date(fimReal.getTime() + 600000));
    histEls.detSub.textContent = histPontos.length
      ? `${histPontos.length} pontos (1 a cada 10 min) em ${dias} dia(s) com dados`
      : "Sem leituras gravadas nesse período (o histórico só existe a partir da versão que grava a cada 10 min).";
    const red = histReduzir(histPontos, 700);
    desenharSparkline(red.map((p) => ({ valor: p.t, quando: p.q })), FAIXAS.temperatura, histEls.canvasT);
    desenharSparkline(red.map((p) => ({ valor: p.h, quando: p.q })), FAIXAS.umidade, histEls.canvasU);
    histStats(histEls.statsT, histPontos.map((p) => p.t), "°C", 1);
    histStats(histEls.statsU, histPontos.map((p) => p.h), "%", 1);
  } catch (e) {
    console.error("Erro no histórico do ciclo:", e);
    if (seq === histSeq) histEls.detSub.textContent = "Não foi possível carregar o histórico.";
  }
}
// abrirAtivo=true (vindo do card de temperatura/umidade): já abre o gráfico do ciclo em andamento, se houver
window.abrirHistorico = function (abrirAtivo) {
  histOrigem = screens.dashboard.classList.contains("active") ? "dashboard" : "configuracoes";
  showScreen("historico");
  histMostrarVista(false);
  histCarregarCiclos(false);
  if (abrirAtivo === true && state.cicloIniciadoEm) histAbrirCiclo(histListaCompleta().find((c) => c.id === "ativo"));
};
histEls.chips.forEach((c) => c.addEventListener("click", () => {
  histFiltro = c.dataset.filtro;
  histEls.chips.forEach((x) => x.classList.toggle("active", x === c));
  histRenderLista();
}));
histEls.busca.addEventListener("input", () => { histBuscaTxt = histEls.busca.value; histRenderLista(); });
histEls.ordem.addEventListener("change", () => { histOrdem = histEls.ordem.value; histRenderLista(); });
histEls.detVoltar.addEventListener("click", () => histMostrarVista(false));
histEls.voltar.addEventListener("click", () => {
  if (!histEls.vistaDet.hidden) { histMostrarVista(false); return; }
  showScreen(histOrigem);
});
document.getElementById("btn-abrir-historico").addEventListener("click", () => abrirHistorico(false));
window.addEventListener("resize", () => {
  if (screens.historico.classList.contains("active") && !histEls.vistaDet.hidden && histPontos.length) {
    const red = histReduzir(histPontos, 700);
    desenharSparkline(red.map((p) => ({ valor: p.t, quando: p.q })), FAIXAS.temperatura, histEls.canvasT);
    desenharSparkline(red.map((p) => ({ valor: p.h, quando: p.q })), FAIXAS.umidade, histEls.canvasU);
  }
});
histEls.csv.addEventListener("click", async () => {
  if (!histPontos.length) { histEls.detSub.textContent = "Nada para exportar."; return; }
  const linhas = ["data_hora,temperatura_C,umidade_pct", ...histPontos.map((p) => `${p.q.getFullYear()}-${p2(p.q.getMonth() + 1)}-${p2(p.q.getDate())} ${p2(p.q.getHours())}:${p2(p.q.getMinutes())},${p.t.toFixed(1)},${p.h.toFixed(1)}`)];
  const nome = String((histAtual && histAtual.nome) || "ciclo").replace(/[^a-z0-9]+/gi, "_");
  const file = new File([linhas.join("\n")], `historico_${nome}.csv`, { type: "text/csv" });
  try {
    if (navigator.canShare && navigator.canShare({ files: [file] })) { await navigator.share({ files: [file], title: `Histórico — ${histAtual.nome}` }); return; }
  } catch (e) { if (e && e.name === "AbortError") return; }
  const a = document.createElement("a");
  a.href = URL.createObjectURL(file); a.download = file.name; document.body.appendChild(a); a.click(); a.remove();
});
// O modal do sensor (aba "7 dias") abre o histórico do ciclo em andamento (ou a lista)
document.querySelectorAll(".sensor-tab").forEach((btn) => {
  btn.addEventListener("click", (ev) => {
    if (btn.dataset.periodo !== "7d") return;
    ev.stopImmediatePropagation();
    fecharModalSensor();
    abrirHistorico(true);
  }, true);
});

// ═══════════════════════════════════════════════════════════════
// Fase 54 — Enter fecha o teclado (qualquer campo) e som/vibração das notificações
// ═══════════════════════════════════════════════════════════════
document.addEventListener("keydown", (e) => {
  if (e.key !== "Enter" || e.defaultPrevented) return;
  const el = e.target;
  if (el && el.tagName === "INPUT" && !["checkbox", "radio", "button", "submit", "range", "file"].includes(el.type)) el.blur();
});

const SOM_KEY = "choca_som";
const somEls = {
  ativo: document.getElementById("som-ativo"),
  volume: document.getElementById("som-volume"),
  tom: document.getElementById("som-tom"),
  vibrar: document.getElementById("som-vibrar"),
  testar: document.getElementById("btn-som-testar"),
};
function somPrefs() {
  const base = { ativo: true, volume: 70, tom: "alerta", vibrar: true };
  try { return { ...base, ...JSON.parse(localStorage.getItem(SOM_KEY) || "{}") }; } catch (e) { return base; }
}
async function somSalvar() {
  const p = { ativo: somEls.ativo.checked, volume: Number(somEls.volume.value), tom: somEls.tom.value, vibrar: somEls.vibrar.checked };
  localStorage.setItem(SOM_KEY, JSON.stringify(p));
  try { // o service worker lê daqui (vibração/prioridade da notificação em segundo plano)
    const c = await caches.open("choca-prefs");
    await c.put("/prefs.json", new Response(JSON.stringify(p), { headers: { "Content-Type": "application/json" } }));
  } catch (e) { /* sem Cache API */ }
}
let somCtx = null;
function somTocar(tom, volume) {
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) return;
  const ctx = somCtx || (somCtx = new Ctx());
  if (ctx.state === "suspended") ctx.resume();
  const g = ctx.createGain();
  g.gain.value = Math.max(0, Math.min(1, volume / 100)) * 0.6;
  g.connect(ctx.destination);
  const nota = (freq, t0, dur, tipo) => {
    const o = ctx.createOscillator(), e = ctx.createGain(), t = ctx.currentTime + t0;
    o.type = tipo || "sine"; o.frequency.value = freq;
    e.gain.setValueAtTime(0, t); e.gain.linearRampToValueAtTime(1, t + 0.02); e.gain.linearRampToValueAtTime(0, t + dur);
    o.connect(e); e.connect(g); o.start(t); o.stop(t + dur + 0.05);
  };
  if (tom === "suave") { nota(660, 0, 0.18); nota(880, 0.2, 0.3); }
  else if (tom === "sirene") { for (let i = 0; i < 4; i++) nota(i % 2 ? 1000 : 640, i * 0.3, 0.28, "sawtooth"); }
  else { nota(880, 0, 0.16, "square"); nota(880, 0.24, 0.16, "square"); nota(1175, 0.48, 0.3, "square"); }
}
// Chamado quando chega um aviso (app aberto: direto; app em segundo plano: pelo service worker)
window.somAviso = function () {
  const p = somPrefs();
  if (p.ativo && p.volume > 0) { try { somTocar(p.tom, p.volume); } catch (e) { /* autoplay bloqueado */ } }
  if (p.vibrar && navigator.vibrate) navigator.vibrate([250, 120, 250, 120, 400]);
};
(function iniciarSom() {
  const p = somPrefs();
  somEls.ativo.checked = p.ativo; somEls.volume.value = p.volume; somEls.tom.value = p.tom; somEls.vibrar.checked = p.vibrar;
  [somEls.ativo, somEls.volume, somEls.tom, somEls.vibrar].forEach((el) => el.addEventListener("change", somSalvar));
  somSalvar(); // garante que o service worker tenha as preferências mesmo sem mexer
  somEls.testar.addEventListener("click", async () => {
    await somSalvar();
    const p2 = somPrefs();
    try { somTocar(p2.tom, p2.volume); } catch (e) { /* ignore */ }
    if (p2.vibrar && navigator.vibrate) navigator.vibrate([250, 120, 250, 120, 400]);
    if ("Notification" in window && Notification.permission === "granted") {
      const reg = await navigator.serviceWorker.ready;
      reg.showNotification("ChocaIoT: teste de som", { body: "Se ouviu/sentiu, está configurado. O som em segundo plano segue o canal de notificação do celular.", tag: "choca-som", silent: false, vibrate: p2.vibrar ? [250, 120, 250] : undefined, icon: "/icons/icon-192.png" });
    }
  });
  if ("serviceWorker" in navigator) navigator.serviceWorker.addEventListener("message", (ev) => { if (ev.data && ev.data.tipo === "push") window.somAviso(); });
})();

// ═══════════════════════════════════════════════════════════════
// Fase 55 — nome da chocadeira (campo "nome" do documento; o aparelho lê no mesmo poll e também pode editar)
// ═══════════════════════════════════════════════════════════════
const nomeEls = { input: document.getElementById("nome-choc-input"), btn: document.getElementById("btn-nome-choc"), fb: document.getElementById("nome-choc-fb") };
window.nomeChocDoDoc = function (nome) {
  if (!nome) return;
  if (document.activeElement !== nomeEls.input) nomeEls.input.value = nome;
  const b = document.getElementById("cycle-banner");
  if (b && b.dataset.nomeChocadeira !== nome) {
    const antigo = b.dataset.nomeChocadeira || "";
    b.dataset.nomeChocadeira = nome;
    if (antigo && b.textContent.startsWith(antigo)) b.textContent = nome + b.textContent.slice(antigo.length);
  }
};
nomeEls.btn.addEventListener("click", async () => {
  const nome = nomeEls.input.value.replace(/[;|"\\<>]/g, "").trim().slice(0, 24);
  if (!nome) { nomeEls.fb.textContent = "Digite um nome."; return; }
  try {
    await chocadeiraRef().update({ nome, "cmd.nomeChoc": nome });
    registrarAcao("Comando", `Renomeou a chocadeira para "${nome}"`);
    nomeEls.fb.textContent = "Nome salvo. O aparelho atualiza em alguns segundos.";
    nomeChocDoDoc(nome);
  } catch (err) { console.error(err); nomeEls.fb.textContent = "Não foi possível salvar."; }
});

// ═══════════════════════════════════════════════════════════════
// Fase 56 — menu como cartão (fundo desfocado), Guia flutuante e card "Conta"
// ═══════════════════════════════════════════════════════════════
(function iniciarMenuECconta() {
  const menu = document.getElementById("menu-dropdown");
  if (menu) new MutationObserver(() => document.body.classList.toggle("menu-aberto", menu.classList.contains("open"))).observe(menu, { attributes: true, attributeFilter: ["class"] });
  // toque no fundo desfocado do Guia fecha o cartão (só no celular)
  screens.guia.addEventListener("click", (e) => { if (e.target === screens.guia && cicloFlutuante()) showScreen("dashboard"); });

  const emailEl = document.getElementById("user-email");
  const recEl = document.getElementById("conta-email-rec");
  const fb = document.getElementById("conta-fb");
  const btnCopiar = document.getElementById("btn-conta-copiar"), btnRec = document.getElementById("btn-conta-rec"), btnTrocar = document.getElementById("btn-conta-trocar");
  window.contaAtualizar = function () {
    const meu = emailLogado();
    if (meu && emailEl.textContent !== meu) emailEl.textContent = meu;
    recEl.textContent = state.emailRec || "não vinculado";
    btnRec.disabled = !meu || (state.emailRec || "").toLowerCase() === meu.toLowerCase() || state.pinAtivo === true;
    btnRec.title = state.pinAtivo === true ? "Desative o PIN do aparelho antes de trocar o e-mail de recuperação" : "";
  };
  btnCopiar.addEventListener("click", async () => {
    const meu = emailLogado();
    if (!meu) return;
    try { await navigator.clipboard.writeText(meu); fb.textContent = "E-mail copiado."; } catch (e) { fb.textContent = meu; }
  });
  btnRec.addEventListener("click", async () => {
    const meu = emailLogado();
    if (!meu) return;
    try { await chocadeiraRef().update({ "cmd.emailRec": meu }); fb.textContent = `${meu} agora é o e-mail de recuperação do PIN do aparelho.`; }
    catch (e) { console.error(e); fb.textContent = "Não foi possível vincular."; }
  });
  btnTrocar.addEventListener("click", async () => {
    if (!confirm("Trocar de conta? Você sai desta conta e escolhe outra no Google. A nova conta precisa ser a dona da chocadeira; senão será pedido o vínculo de novo.")) return;
    try { await auth.signOut(); } catch (e) { location.reload(); }
  });
  contaAtualizar();
})();

// ═══════════════════════════════════════════════════════════════
// Fase 57 — regras avançadas (mesma string do firmware: "...|X|regra|regra")
// regra = ativa,s1,op1,v1,comb,s2,op2,v2,temOff,sOff,opOff,vOff,acaoA,acaoI,hIni,hFim
//   sensor: 0 temperatura · 1 umidade · 2 temp. do chip   op: 0 ≥ · 1 ≤ · 2 > · 3 <   valores em décimos
//   acaoA/acaoI: 2 bits por relé (0 nada · 1 liga · 2 desliga)
// ═══════════════════════════════════════════════════════════════
const ADV_SENSORES = ["Temperatura (°C)", "Umidade (%)", "Temp. do chip (°C)", "CO₂ (ppm) — sensor ainda não instalado"];
const ADV_SENS_CURTO = ["Temp", "Umid", "Chip", "CO₂"];
const ADV_OPS = ["≥", "≤", ">", "<"];
function advNova() { return { ativa: 1, s1: 0, op1: 0, v1: 375, comb: 0, s2: 1, op2: 3, v2: 500, temOff: 0, sOff: 0, opOff: 1, vOff: 373, acaoA: 4, acaoI: 0, hIni: 0, hFim: 0 }; }
const advVal = (s, v) => (s === 3 ? `${v} ppm` : s === 1 ? `${Math.round(v / 10)}%` : `${(v / 10).toFixed(1)}°C`);
function advAcoesTxt(mask) {
  const t = [];
  for (let ch = 0; ch < AUTO_N; ch++) { const c = (mask >> (2 * ch)) & 3; if (c) t.push(`CH${ch + 1} ${c === 1 ? "liga" : "desliga"}`); }
  return t.length ? t.join(", ") : "nada";
}
function advResumoTxt(r) {
  let s = `SE ${ADV_SENS_CURTO[r.s1]} ${ADV_OPS[r.op1]} ${advVal(r.s1, r.v1)}`;
  if (r.comb) s += ` ${r.comb === 1 ? "E" : "OU"} ${ADV_SENS_CURTO[r.s2]} ${ADV_OPS[r.op2]} ${advVal(r.s2, r.v2)}`;
  if (r.temOff) s += ` — desliga quando ${ADV_SENS_CURTO[r.sOff]} ${ADV_OPS[r.opOff]} ${advVal(r.sOff, r.vOff)}`;
  s += ` → ENTÃO ${advAcoesTxt(r.acaoA)}`;
  if (r.acaoI) s += ` · SENÃO ${advAcoesTxt(r.acaoI)}`;
  if (r.hIni !== r.hFim) s += ` · das ${minParaHHMM(r.hIni)} às ${minParaHHMM(r.hFim)}`;
  return s;
}
function advSel(opcoes, valor, onChange) {
  const s = document.createElement("select");
  opcoes.forEach((t, i) => { const o = document.createElement("option"); o.value = i; o.textContent = t; s.appendChild(o); });
  s.value = valor;
  s.addEventListener("change", () => onChange(Number(s.value)));
  return s;
}
function advCondicao(rotulo, r, ks, ko, kv, marcar, rerender) {
  const box = document.createElement("div");
  box.className = "auto-campos adv-cond";
  const sens = advSel(ADV_SENSORES, r[ks], (v) => {
    const antes = r[ks];
    r[ks] = v;
    if (antes !== v) r[kv] = v === 3 ? 1000 : v === 1 ? 550 : 375;   // valor padrão do novo sensor (unidades diferentes)
    marcar(); rerender();
  });
  const op = advSel(["≥ maior ou igual", "≤ menor ou igual", "> maior que", "< menor que"], r[ko], (v) => { r[ko] = v; marcar(); });
  const co2 = r[ks] === 3;
  const val = co2
    ? autoNumero(String(r[kv]), 300, 5000, "50", (v) => { r[kv] = Math.round(v); marcar(); })
    : autoNumero((r[kv] / 10).toFixed(r[ks] === 1 ? 0 : 1), r[ks] === 1 ? 0 : 20, r[ks] === 1 ? 100 : 90, r[ks] === 1 ? "1" : "0.1", (v) => { r[kv] = Math.round(v * 10); marcar(); });
  box.append(autoCampo(rotulo + " — sensor", sens, true, "advSensor"), autoCampo("Operador", op, false, "advOp"), autoCampo("Valor", val, false, "advValor"));
  return box;
}
function advChipsAcoes(titulo, r, chave, marcar) {
  const wrap = document.createElement("div");
  wrap.className = "adv-acoes";
  const t = document.createElement("div"); t.className = "adv-acoes-titulo"; t.textContent = titulo; t.appendChild(ajudaBtn(chave === "acaoA" ? "advEntao" : "advSenao"));
  const grade = document.createElement("div"); grade.className = "adv-acoes-grade";
  for (let ch = 0; ch < AUTO_N; ch++) {
    const l = document.createElement("label");
    const nm = document.createElement("span"); nm.textContent = `CH${ch + 1} ${RELAY_NAMES[ch]}`;
    const s = advSel(["—", "Liga", "Desliga"], (r[chave] >> (2 * ch)) & 3, (v) => { r[chave] = (r[chave] & ~(3 << (2 * ch))) | (v << (2 * ch)); marcar(); });
    if (ch === AUTO_COOLER) { s.disabled = true; s.title = "O cooler é automático pela temperatura do chip"; }
    l.append(nm, s); grade.appendChild(l);
  }
  wrap.append(t, grade);
  return wrap;
}
function advRender() {
  // busca aqui (não em const global): autoRender() chama advRender antes da Fase 57 ser avaliada — a const global travava o arquivo inteiro (TDZ)
  const advLista = document.getElementById("auto-adv-lista");
  if (!advLista) return;
  advLista.textContent = "";
  const arr = autoCfg.adv || (autoCfg.adv = []);
  const marcar = () => { autoSujo = true; autoMsg("Alterações não salvas."); };
  arr.forEach((r, i) => {
    const box = document.createElement("div");
    box.className = "auto-canal adv-regra";
    const topo = document.createElement("div"); topo.className = "auto-canal-topo";
    const nome = document.createElement("span"); nome.textContent = `Regra ${i + 1}`;
    const acoes = document.createElement("span"); acoes.className = "adv-topo-acoes";
    const tg = document.createElement("label"); tg.className = "toggle";
    const cb = document.createElement("input"); cb.type = "checkbox"; cb.checked = !!r.ativa; cb.addEventListener("change", () => { r.ativa = cb.checked ? 1 : 0; marcar(); });
    tg.append(cb, Object.assign(document.createElement("span"), { className: "slider" }));
    const rm = document.createElement("button"); rm.type = "button"; rm.className = "icon-btn icon-btn-danger"; rm.textContent = "🗑"; rm.title = "Apagar regra"; rm.setAttribute("aria-label", `Apagar regra ${i + 1}`);
    rm.addEventListener("click", () => { arr.splice(i, 1); marcar(); advRender(); });
    acoes.append(ajudaBtn("advAtiva"), tg, rm); topo.append(nome, acoes);
    const resumo = document.createElement("div"); resumo.className = "adv-resumo"; resumo.textContent = advResumoTxt(r);
    const marcarR = () => { marcar(); resumo.textContent = advResumoTxt(r); };
    box.append(topo, resumo, advCondicao("SE", r, "s1", "op1", "v1", marcarR, advRender));
    const comb = document.createElement("div"); comb.className = "auto-campos";
    comb.appendChild(autoCampo("Combinar com 2ª condição", advSel(["Nenhuma", "E (as duas)", "OU (uma delas)"], r.comb, (v) => { r.comb = v; marcarR(); advRender(); }), true, "advComb"));
    box.appendChild(comb);
    if (r.comb) box.appendChild(advCondicao(r.comb === 1 ? "E" : "OU", r, "s2", "op2", "v2", marcarR, advRender));
    const off = document.createElement("div"); off.className = "auto-campos";
    off.appendChild(autoCampo("Desligar (voltar ao SENÃO) quando", advSel(["Inverso da condição", "Condição própria (histerese)"], r.temOff, (v) => { r.temOff = v; marcarR(); advRender(); }), true, "advOff"));
    box.appendChild(off);
    if (r.temOff) box.appendChild(advCondicao("DESLIGAR", r, "sOff", "opOff", "vOff", marcarR, advRender));
    box.append(advChipsAcoes("ENTÃO (regra ativa)", r, "acaoA", marcarR), advChipsAcoes("SENÃO (regra inativa)", r, "acaoI", marcarR));
    const jan = document.createElement("div"); jan.className = "auto-campos";
    const a = document.createElement("input"); a.type = "time"; a.value = minParaHHMM(r.hIni); a.addEventListener("input", () => { r.hIni = hhmmParaMin(a.value); marcarR(); });
    const b = document.createElement("input"); b.type = "time"; b.value = minParaHHMM(r.hFim); b.addEventListener("input", () => { r.hFim = hhmmParaMin(b.value); marcarR(); });
    jan.append(autoCampo("Janela — das (iguais = o dia todo)", a, false, "advJanela"), autoCampo("até", b, false, "advJanela"));
    box.appendChild(jan);
    advLista.appendChild(box);
  });
  document.getElementById("btn-auto-adv-nova").disabled = arr.length >= ADV_MAX;
}
document.getElementById("btn-auto-adv-nova").addEventListener("click", () => {
  (autoCfg.adv || (autoCfg.adv = [])).push(advNova());
  autoSujo = true; autoMsg("Regra criada — ajuste e toque em Salvar regras."); advRender();
});
autoRender();


// ─── Fase 77 — card Status: janela completa (mesma do aparelho) com a confirmação "ovos colocados" ───
// `ambiente` (heartbeat): "aguardando;<seg estáveis>;<seg necessários>" | "pronto" | "" — ver statusAmbInfo() em app.js.
// Fase 82 — avisa o aparelho que a caixa vai ser aberta (1) / foi fechada (2); ele também detecta sozinho a queda brusca
async function tampaComando(cod) {
  if (!state.chocadeiraId) return;
  const raw = hwUltimoRaw || autoUltimoRaw;
  if (!raw) { mostrarFeedbackHeader("Aguarde o aparelho sincronizar e tente de novo", "warn"); return; }
  try {
    const base = autoParse(raw) || autoPadrao();
    base.tampaCmd = cod;
    await chocadeiraRef().update({ "cmd.auto": autoSerialize(base) });
    registrarAcao("Ciclo", cod === 1 ? "Avisou que vai abrir a caixa" : "Avisou que fechou a caixa");
    hwUltimoRaw = null; autoUltimoRaw = null;
    mostrarFeedbackHeader(cod === 1 ? "Aviso enviado: pode abrir a caixa — o aparelho cuida do resto" : "Aviso enviado: caixa fechada — recuperando o ambiente", "ok");
  } catch (err) { console.error(err); mostrarFeedbackHeader("Não foi possível enviar o aviso", "error"); }
}
// Fase 79 — liga/desliga a manutenção do ambiente (token M da string cmd.auto; o aparelho aplica no próximo poll)
async function manterAlternar(ligar) {
  if (!state.chocadeiraId) return;
  const raw = hwUltimoRaw || autoUltimoRaw;
  if (!raw) { mostrarFeedbackHeader("Aguarde o aparelho sincronizar e tente de novo", "warn"); return; }
  try {
    const base = autoParse(raw) || autoPadrao();
    base.manter = ligar ? 1 : 0;
    await chocadeiraRef().update({ "cmd.auto": autoSerialize(base) });
    registrarAcao("Ciclo", ligar ? "Ligou a manutenção do ambiente" : "Parou a manutenção do ambiente");
    hwUltimoRaw = null; autoUltimoRaw = null;
    mostrarFeedbackHeader(ligar ? "Mantendo o ambiente — o aparelho aplica em até ~1 min" : "Manutenção do ambiente parada — o aparelho aplica em até ~1 min", "ok");
  } catch (err) { console.error(err); mostrarFeedbackHeader("Não foi possível enviar o comando", "error"); }
}
function statusJanelaFechar() { const o = document.getElementById("status-overlay"); if (o) o.remove(); }
function statusJanelaAtualizar() {
  const o = document.getElementById("status-overlay"); if (!o) return;
  const card = o.querySelector(".status-janela"); if (card) statusJanelaMontar(card);
}
function statusJanelaMontar(card) {
  const s = statusAmbInfo(), f = (n, d = 1) => (n == null ? "--" : Number(n).toFixed(d).replace(".", ","));
  card.textContent = "";
  const t = document.createElement("div"); t.className = "ajuda-titulo status-titulo"; t.textContent = "Status do ambiente";
  card.appendChild(t);
  if (s.ativo && !s.prep) {   // Fase 98: o campo de estado da tampa é um ATALHO para a tela "Tampa" (sensor ao vivo, ajustes, calibração)
    const mm0 = Math.floor(s.tampaSeg / 60), ss0 = String(Math.floor(s.tampaSeg % 60)).padStart(2, "0");
    const at = document.createElement("button"); at.type = "button"; at.className = "tp-atalho"; at.dataset.est = String(s.tampa || 0);
    const ic = s.tampa === 1 ? "🔓" : s.tampa === 2 ? "♻" : "🔒";
    const tx = s.tampa === 1 ? `Tampa aberta há ${mm0}:${ss0}` : s.tampa === 2 ? `Recuperando o ambiente (${mm0}:${ss0})` : "Nenhuma abertura detectada";   // Fase 112: "fechada" não é afirmável sem confirmação
    at.innerHTML = `<span class="tp-at-ic">${ic}</span><span class="tp-at-tx"><b>${tx}</b><small>${s.tampaAuto ? "automática · sensor ADXL345" : "aviso manual"} — toque para ver o sensor ao vivo</small></span><span class="tp-at-seta">›</span>`;
    at.addEventListener("click", () => { statusJanelaFechar(); if (typeof window.abrirTelaTampa === "function") window.abrirTelaTampa(); });
    card.appendChild(at);
  }
  if (s.tampa) {   // Fase 82: tampa aberta / recuperando — sem alertas, o aparelho compensa sozinho
    const tp = document.createElement("p"); tp.className = "prep-diag";
    tp.textContent = s.tampa === 1 ? "Alertas e umidificador em pausa; o aquecedor compensa. Não precisa pausar o ciclo." : "Volta ao normal após 1 min na faixa.";
    card.appendChild(tp);
  }
  if (s.ativo && !s.prep && (!s.tampa || s.tampa === 1)) {
    if (s.tampaAuto) {   // Fase 95: o sensor ADXL345 detecta sozinho -- sem o botão grande; só um aviso manual pequeno
      const pa = document.createElement("p"); pa.className = "prep-nota";
      pa.textContent = s.tampa === 1 ? "🛰 Tampa automática: o sensor viu o módulo ser retirado. Ao recolocá-lo o aparelho volta ao controle normal." : "🛰 Tampa automática: o aparelho percebe sozinho quando o módulo ADXL é retirado da tampa.";
      const bt = document.createElement("button"); bt.type = "button"; bt.className = "btn-secondary";
      bt.textContent = s.tampa === 1 ? "Avisar manualmente: fechei" : "Avisar manualmente: vou abrir";
      bt.addEventListener("click", () => { tampaComando(s.tampa === 1 ? 2 : 1); statusJanelaFechar(); });
      card.append(pa, bt);
    } else {
      const bt = document.createElement("button"); bt.type = "button"; bt.className = "prep-confirmar" + (s.tampa === 1 ? " pronto" : "");
      bt.textContent = s.tampa === 1 ? "Fechei a caixa" : "Vou abrir a caixa (ovoscopia, água, virar)";
      bt.addEventListener("click", () => { tampaComando(s.tampa === 1 ? 2 : 1); statusJanelaFechar(); });
      card.appendChild(bt);
    }
  }
  if (s.diag && !s.prep) { const dg = document.createElement("p"); dg.className = "prep-diag"; dg.textContent = `⚠ ${s.diag}`; card.appendChild(dg); }   // Fase 80: causa provável (10 min fora da faixa)
  if (!s.ativo) {
    if (s.man) {   // Fase 79: mantendo o ambiente do último ciclo
      const ul = document.createElement("ul"); ul.className = "prep-lista";
      [[s.tOk, `Temperatura: ${f(state.tempAtual)} °C (faixa ${f(FAIXAS.temperatura.min)}–${f(FAIXAS.temperatura.max)})`],
       [s.uOk, `Umidade: ${f(state.umidAtual, 0)}% (faixa ${FAIXAS.umidade.min}–${FAIXAS.umidade.max})`],
       [s.aOk, `Automação: ${s.aOk ? "ligada" : "desligada"}`]].forEach(([ok, tx]) => { const li = document.createElement("li"); li.className = ok ? "ok" : "nok"; li.textContent = `${ok ? "✓" : "✗"} ${tx}`; ul.appendChild(li); });
      const p = document.createElement("p"); p.className = "prep-nota";
      p.textContent = `Sem ciclo: mantendo o ambiente do último tipo (${s.tipoUlt}). ${s.manOk ? "Tudo na faixa." : "Ajustando temperatura e umidade…"}${s.aOk ? "" : " Ligue a Automação para o aparelho agir."}`;
      const b = document.createElement("button"); b.type = "button"; b.className = "prep-confirmar"; b.textContent = "Parar manutenção do ambiente";
      b.addEventListener("click", () => confirmarAcao("Parar a manutenção?", "O aparelho deixa de controlar temperatura e umidade e desliga o aquecedor e o umidificador que a automação tinha ligado.", "Parar", () => { manterAlternar(false); statusJanelaFechar(); }));
      card.append(ul, p, b);
    } else {
      const p = document.createElement("p"); p.className = "ajuda-texto";
      p.textContent = "OFF — nenhum ciclo em andamento.\nInicie um ciclo (card Ciclo): a chocadeira prepara o ambiente antes de você colocar os ovos.";
      card.appendChild(p);
      if (s.parado) {
        const b = document.createElement("button"); b.type = "button"; b.className = "prep-confirmar pronto"; b.textContent = `Manter o ambiente (${s.tipoUlt})`;
        b.addEventListener("click", () => { manterAlternar(true); statusJanelaFechar(); });
        card.appendChild(b);
      }
    }
    const fecharB = document.createElement("button"); fecharB.type = "button"; fecharB.className = "prep-fechar"; fecharB.textContent = "Fechar";
    fecharB.addEventListener("click", statusJanelaFechar); card.appendChild(fecharB);
    return;
  }
  if (s.prep) {
    const bloco = document.createElement("div"); bloco.className = "ciclo-preparo"; renderPreparoBloco(bloco); card.appendChild(bloco);
  } else {
    const ul = document.createElement("ul"); ul.className = "prep-lista";
    [[s.tOk, `Temperatura: ${f(state.tempAtual)} °C (faixa ${f(FAIXAS.temperatura.min)}–${f(FAIXAS.temperatura.max)})`],
     [s.uOk, `Umidade: ${f(state.umidAtual, 0)}% (faixa ${FAIXAS.umidade.min}–${FAIXAS.umidade.max})`],
     [s.aOk, `Automação: ${s.aOk ? "ligada" : "desligada"}`]].forEach(([ok, tx]) => { const li = document.createElement("li"); li.className = ok ? "ok" : "nok"; li.textContent = `${ok ? "✓" : "✗"} ${tx}`; ul.appendChild(li); });
    const p = document.createElement("p"); p.className = "prep-nota";
    if (!s.aOk) p.textContent = `Automação DESLIGADA: nada controla o ambiente. ${(typeof autoCfg !== "undefined" && autoCfg.retoma > 0) ? `Volta sozinha em até ${autoCfg.retoma} min com ciclo ativo.` : "Ligue a chave em Atuadores ou em Automação."}`;
    else p.textContent = s.tOk && s.uOk ? "OK — ciclo em andamento, tudo normal." : "Atenção: fora da faixa ideal.";   // Fase 112
    card.append(ul, p);
  }
  const fechar = document.createElement("button"); fechar.type = "button"; fechar.className = "prep-fechar"; fechar.textContent = "Fechar";
  fechar.addEventListener("click", statusJanelaFechar); card.appendChild(fechar);
}
function statusJanelaAbrir() {
  statusJanelaFechar();
  const o = document.createElement("div"); o.id = "status-overlay"; o.className = "ajuda-overlay";
  const c = document.createElement("div"); c.className = "ajuda-card status-janela"; c.setAttribute("role", "dialog");
  statusJanelaMontar(c); o.appendChild(c);
  o.addEventListener("click", (ev) => { if (ev.target === o) statusJanelaFechar(); });
  document.body.appendChild(o);
}
(function () {
  const el = document.getElementById("card-status");
  if (!el) return;
  el.addEventListener("click", statusJanelaAbrir);
  el.addEventListener("keydown", (ev) => { if (ev.key === "Enter" || ev.key === " ") { ev.preventDefault(); statusJanelaAbrir(); } });
})();
