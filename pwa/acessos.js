// acessos.js — ChocaIoT PWA — Fase 90: registro de acessos como medida de segurança (+ transparência/LGPD)
//
// O QUE É COLETADO (por evento, em chocadeiras/{id}/acessos): evento, e-mail e uid de quem fez, papel (dono/convidado),
//   um identificador aleatório do navegador (deviceId, guardado só neste aparelho), plataforma (ex.: Android), navegador,
//   se o app está instalado, idioma, fuso horário, tamanho da tela e a hora do servidor.
// O QUE NÃO É COLETADO: endereço IP, localização, contatos, identificadores de publicidade, impressão digital do navegador.
// RETENÇÃO: 90 dias (o dono limpa automaticamente ao abrir o painel). QUEM VÊ: o dono vê tudo; cada pessoa vê, exporta e apaga o que é dela.
// As regras do Firestore (firestore.rules) impedem assinar como outra pessoa, inventar campos ou forjar a hora.
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const RETENCAO_DIAS = 90;
  const CONSENT_KEY = "choca_priv_v1_";
  const SEEN_KEY = "choca_dev_seen_";
  const ABERT_KEY = "choca_abertura_ts";
  let unsubAlerta = null, primeiraAlerta = true, negados = [], ultNegado = 0, ultAlertaNegado = 0, carregado = [], filtro = "todos";

  // ───────── aparelho (sem impressão digital: só o que o navegador já informa) ─────────
  function deviceId() {
    try {
      let d = localStorage.getItem("choca_dev_id");
      if (!d) { const b = new Uint8Array(12); crypto.getRandomValues(b); d = Array.from(b, (x) => (x % 36).toString(36)).join("") + "x"; localStorage.setItem("choca_dev_id", d); }
      return d;
    } catch (e) { return "semstorage" + Math.random().toString(36).slice(2, 8); }
  }
  function plataforma() {
    const ua = navigator.userAgent || "", uad = navigator.userAgentData && navigator.userAgentData.platform;
    if (/Android/i.test(ua)) return "Android";
    if (/iPhone|iPad|iPod/i.test(ua)) return "iOS";
    if (uad) return String(uad).slice(0, 20);
    if (/Windows/i.test(ua)) return "Windows";
    if (/Mac OS X|Macintosh/i.test(ua)) return "macOS";
    if (/Linux|X11/i.test(ua)) return "Linux";
    return "Desconhecida";
  }
  function navegador() {
    const ua = navigator.userAgent || "";
    if (/Edg\//.test(ua)) return "Edge"; if (/OPR\//.test(ua)) return "Opera"; if (/SamsungBrowser/.test(ua)) return "Samsung Internet";
    if (/Firefox\//.test(ua)) return "Firefox"; if (/Chrome\//.test(ua)) return "Chrome"; if (/Safari\//.test(ua)) return "Safari";
    return "Outro";
  }
  function dispositivo() {
    let inst = false; try { inst = window.matchMedia("(display-mode: standalone)").matches || navigator.standalone === true; } catch (e) { /* ok */ }
    let fuso = ""; try { fuso = Intl.DateTimeFormat().resolvedOptions().timeZone || ""; } catch (e) { /* ok */ }
    return { plataforma: plataforma(), navegador: navegador(), instalado: !!inst, idioma: String(navigator.language || "").slice(0, 12), fuso: fuso.slice(0, 40), tela: `${screen.width}x${screen.height}` };
  }
  const rotuloDisp = (d) => d ? `${d.navegador || "?"} · ${d.plataforma || "?"}${d.instalado ? " · app instalado" : ""}` : "?";

  // ───────── gravação ─────────
  function col() { return db.collection("chocadeiras").doc(state.chocadeiraId).collection("acessos"); }
  function registrar(evento, detalhe, novo) {
    const u = auth.currentUser; if (!u || !state.chocadeiraId) return Promise.resolve();
    const d = { evento, ator: u.email || "", uid: u.uid, papel: state.papel === "convidado" ? "convidado" : "dono", deviceId: deviceId(), dispositivo: dispositivo(), novo: !!novo, timestamp: firebase.firestore.FieldValue.serverTimestamp() };
    if (detalhe) d.detalhe = String(detalhe).slice(0, 160);
    return col().add(d).catch((e) => console.warn("acessos:", e && e.code));
  }
  window.acRegistrar = registrar;
  // aviso ao dono pelo aparelho (push) — campo cmd.seg ("<ms>|<email>|<tipo>": novodisp | negado)
  function avisarAparelho(tipo) {
    const u = auth.currentUser; if (!u || !state.chocadeiraId) return;
    db.collection("chocadeiras").doc(state.chocadeiraId).update({ "cmd.seg": `${Date.now()}|${(u.email || "").toLowerCase()}|${tipo}` }).catch(() => {});
  }

  window.acAoEntrar = function () {
    const u = auth.currentUser; if (!u) return;
    mostrarAviso(u);
    // novo aparelho para esta conta?
    let visto = []; try { visto = JSON.parse(localStorage.getItem(SEEN_KEY + u.uid) || "[]"); } catch (e) { visto = []; }
    const id = deviceId(), novo = !visto.includes(id);
    if (novo) {
      try { localStorage.setItem(SEEN_KEY + u.uid, JSON.stringify(visto.concat(id).slice(-20))); } catch (e) { /* ok */ }
      registrar("novo_dispositivo", `Primeiro acesso deste aparelho: ${rotuloDisp(dispositivo())}`, true);
      avisarAparelho("novodisp");
    }
    // abertura (no máximo 1 a cada 30 min por aparelho)
    const ult = Number(localStorage.getItem(ABERT_KEY + u.uid) || 0);
    if (!novo && Date.now() - ult > 30 * 60000) registrar("abertura", "", false);
    if (!novo || true) { try { localStorage.setItem(ABERT_KEY + u.uid, String(Date.now())); } catch (e) { /* ok */ } }
    montarCard(); iniciarAlertas();
  };
  window.acAoSair = function () {
    if (unsubAlerta) { unsubAlerta(); unsubAlerta = null; }
    const c = $("ac-card"); if (c) c.remove();
    primeiraAlerta = true; carregado = [];
  };
  // chamado pelo fluxo de convite aceito (share.js)
  window.acConviteAceito = () => registrar("convite_aceito", "Convite aceito neste aparelho", false);

  // ───────── tentativas bloqueadas pelas regras (permission-denied) ─────────
  (function vigiarNegados() {
    try {
      const P = firebase.firestore.DocumentReference.prototype;
      ["update", "set", "delete"].forEach((m) => {
        const orig = P[m]; if (typeof orig !== "function") return;
        P[m] = function (...a) {
          const p = orig.apply(this, a);
          try {
            p.catch((e) => {
              if (!e || e.code !== "permission-denied" || /\/(acessos|logs)\//.test(this.path)) return;
              const agora = Date.now(); negados = negados.filter((t) => agora - t < 600000); negados.push(agora);
              if (agora - ultNegado > 20000) { ultNegado = agora; registrar("negado", `${m} em ${this.path.split("/").slice(2).join("/") || "chocadeira"}`, false); }
              if (negados.length >= 3 && agora - ultAlertaNegado > 1800000) { ultAlertaNegado = agora; avisarAparelho("negado"); }
            });
          } catch (e) { /* ok */ }
          return p;
        };
      });
    } catch (e) { console.warn("vigiarNegados:", e); }
  })();

  // ───────── alertas ao vivo para o dono (1 leitura por evento novo) ─────────
  function iniciarAlertas() {
    if (unsubAlerta) { unsubAlerta(); unsubAlerta = null; }
    if (state.papel === "convidado") return;
    primeiraAlerta = true;
    unsubAlerta = col().orderBy("timestamp", "desc").limit(1).onSnapshot((snap) => {
      if (primeiraAlerta) { primeiraAlerta = false; return; }
      snap.docChanges().forEach((ch) => {
        if (ch.type !== "added") return;
        const x = ch.doc.data(), eu = auth.currentUser;
        if (!x || !eu || x.uid === eu.uid && x.deviceId === deviceId()) return;
        const quem = x.ator || "alguém";
        if (typeof window.shareNotificar !== "function") return;
        if (x.evento === "novo_dispositivo") window.shareNotificar("Novo aparelho acessou", `${quem} — ${rotuloDisp(x.dispositivo)}`);
        else if (x.evento === "negado") window.shareNotificar("Ação bloqueada", `${quem} tentou algo sem permissão (${x.detalhe || ""})`);
      });
    }, () => {});
  }

  // ───────── aviso de transparência (1ª vez por conta) ─────────
  function mostrarAviso(u) {
    let ok = false; try { ok = localStorage.getItem(CONSENT_KEY + u.uid) === "1"; } catch (e) { ok = true; }
    if (ok || $("ac-aviso")) return;
    const o = document.createElement("div"); o.id = "ac-aviso"; o.className = "modal-backdrop";
    o.innerHTML = '<div class="modal-card ac-aviso-card"><div class="modal-header"><h3>🔒 Privacidade e segurança</h3></div><div class="modal-body">' +
      "<p>Para proteger a chocadeira, o app registra os <b>acessos</b> de cada pessoa (dono e convidados):</p>" +
      '<ul class="ac-lista-priv"><li><b>Registramos:</b> seu e-mail, data e hora, tipo de aparelho e navegador, idioma, fuso e tamanho da tela, e um código aleatório deste navegador.</li>' +
      "<li><b>Não registramos:</b> IP, localização, contatos nem identificadores de publicidade.</li>" +
      "<li><b>Quem vê:</b> o proprietário vê todos os acessos; você vê, exporta e apaga os seus (Configurações → Acessos e privacidade).</li>" +
      "<li><b>Por quanto tempo:</b> 90 dias; depois são apagados.</li></ul>" +
      '<div class="ciclo-actions"><button type="button" class="btn-primary" id="ac-aviso-ok">Entendi</button></div></div></div>';
    document.body.appendChild(o);
    $("ac-aviso-ok").addEventListener("click", () => { try { localStorage.setItem(CONSENT_KEY + u.uid, "1"); } catch (e) { /* ok */ } o.remove(); });
  }

  // ───────── cartão em Configurações ─────────
  function montarCard() {
    if ($("ac-card")) return;
    const alvo = $("share-card-owner") || $("share-card-guest");
    const host = document.querySelector("#screen-configuracoes .config-content");
    if (!host) return;
    const dono = state.papel !== "convidado";
    const c = document.createElement("div"); c.className = "atuadores-card"; c.id = "ac-card";
    c.innerHTML =
      '<div class="section-title-row"><div class="section-title">Acessos e privacidade</div></div>' +
      `<div class="logs-sub">${dono ? "Veja quem acessou a chocadeira e de qual aparelho. Os dados ficam só no seu Firestore, por 90 dias." : "Veja, exporte ou apague os registros dos seus acessos (guardados por 90 dias)."}</div>` +
      '<div class="ac-resumo" id="ac-resumo"></div>' +
      '<div class="ciclo-actions conta-acoes">' +
      `<button type="button" class="btn-secondary" id="ac-carregar">${dono ? "Atualizar (até 150)" : "Atualizar meus acessos"}</button>` +
      '<button type="button" class="btn-secondary" id="ac-exportar" disabled>Exportar (CSV)</button>' +
      `<button type="button" class="btn-danger" id="ac-apagar" disabled>${dono ? "Apagar tudo" : "Apagar meu histórico"}</button></div>` +
      (dono ? '<select id="ac-filtro" class="logs-filtro-select" aria-label="Filtrar acessos" hidden><option value="todos">Todos os eventos</option><option value="novo_dispositivo">Novos aparelhos</option><option value="negado">Ações bloqueadas</option><option value="abertura">Aberturas do app</option><option value="convite_aceito">Convites aceitos</option></select>' : "") +
      '<div class="ac-lista" id="ac-lista"></div>' +
      '<details class="ac-priv"><summary>O que é coletado?</summary><ul class="ac-lista-priv"><li><b>Sim:</b> e-mail, data/hora, plataforma, navegador, app instalado ou não, idioma, fuso, tamanho da tela, código aleatório do navegador.</li><li><b>Não:</b> IP, localização, contatos, publicidade, impressão digital.</li><li><b>Para quê:</b> segurança (aviso de aparelho novo e de ações bloqueadas) e auditoria.</li><li><b>Prazo:</b> 90 dias. <b>Exclusão:</b> a qualquer momento, por você.</li></ul></details>' +
      '<div class="logs-sub" id="ac-fb"></div>';
    if (alvo && alvo.parentNode === host) alvo.after(c); else host.prepend(c);
    $("ac-carregar").addEventListener("click", () => { autoCarregado = true; carregar(150); });
    $("ac-exportar").addEventListener("click", exportar);
    $("ac-apagar").addEventListener("click", apagar);
    const f = $("ac-filtro"); if (f) f.addEventListener("change", () => { filtro = f.value; desenhar(); });
  }

  // Fase 98 — o cartão só carregava ao tocar em "Ver acessos" e parecia vazio. Agora carrega sozinho ao abrir Configurações
  // (uma vez por sessão, até 50 registros) e o atalho do menu já leva até ele; "Atualizar" busca de novo (até 150).
  let autoCarregado = false;
  window.acAutoCarregar = function () { if (autoCarregado || !$("ac-card")) return; autoCarregado = true; carregar(50); };
  window.acMostrar = function () { if (!$("ac-card")) return; if (!autoCarregado) { autoCarregado = true; carregar(50); } };

  async function carregar(lim) {
    const fb = $("ac-fb"); fb.textContent = "Carregando…";
    const dono = state.papel !== "convidado", u = auth.currentUser;
    const limite = typeof lim === "number" ? lim : 150;
    try {
      let q = col(); if (!dono) q = q.where("uid", "==", u.uid);
      const snap = await q.orderBy("timestamp", "desc").limit(limite).get();
      carregado = snap.docs.map((d) => ({ ref: d.ref, id: d.id, ...d.data() }));
      $("ac-exportar").disabled = $("ac-apagar").disabled = !carregado.length;
      const f = $("ac-filtro"); if (f) f.hidden = false;
      desenhar(); fb.textContent = `${carregado.length} registro(s) carregado(s).`;
      if (dono) limparAntigos();
    } catch (e) { console.error(e); fb.textContent = "Não foi possível carregar agora."; }
  }
  async function limparAntigos() {   // retenção de 90 dias (só o dono, no máximo 100 por vez)
    try {
      const corte = firebase.firestore.Timestamp.fromMillis(Date.now() - RETENCAO_DIAS * 86400000);
      const s = await col().where("timestamp", "<", corte).orderBy("timestamp").limit(100).get();
      if (s.empty) return;
      const b = db.batch(); s.docs.forEach((d) => b.delete(d.ref)); await b.commit();
      $("ac-fb").textContent += ` ${s.size} registro(s) com mais de ${RETENCAO_DIAS} dias apagado(s).`;
    } catch (e) { /* ok */ }
  }
  const ROT = { abertura: "Abriu o app", novo_dispositivo: "Aparelho novo", negado: "Ação bloqueada", convite_aceito: "Aceitou o convite", saida: "Saiu" };
  function desenhar() {
    const box = $("ac-lista"); box.textContent = "";
    const lista = carregado.filter((x) => filtro === "todos" || x.evento === filtro);
    // resumo
    const aparelhos = new Set(carregado.map((x) => x.ator + "|" + x.deviceId)).size, neg = carregado.filter((x) => x.evento === "negado").length, novos = carregado.filter((x) => x.evento === "novo_dispositivo").length;
    $("ac-resumo").innerHTML = `<span class="ac-chip">${aparelhos} aparelho(s)</span><span class="ac-chip">${novos} novo(s)</span><span class="ac-chip ${neg ? "alerta" : ""}">${neg} bloqueio(s)</span>`;
    if (!lista.length) { const v = document.createElement("div"); v.className = "logs-empty"; v.textContent = "Nada para mostrar."; box.appendChild(v); return; }
    lista.slice(0, 60).forEach((x) => {
      const r = document.createElement("div"); r.className = `ac-item ${x.evento}`;
      const h = x.timestamp && x.timestamp.toDate ? x.timestamp.toDate().toLocaleString("pt-BR", { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" }) : "--";
      const a = document.createElement("div"); a.className = "ac-linha1";
      const t = document.createElement("span"); t.className = "ac-hora"; t.textContent = h;
      const e = document.createElement("span"); e.className = "ac-evento"; e.textContent = ROT[x.evento] || x.evento;
      const q = document.createElement("span"); q.className = "ac-quem" + (x.papel === "convidado" ? " convidado" : ""); q.textContent = `${x.papel === "convidado" ? "👤 convidado" : "👤"} ${x.ator}`;
      a.append(t, e, q);
      const b = document.createElement("div"); b.className = "ac-linha2"; b.textContent = `${rotuloDisp(x.dispositivo)}${x.detalhe ? " — " + x.detalhe : ""}`;
      r.append(a, b); box.appendChild(r);
    });
  }
  function exportar() {
    const esc = (v) => '"' + String(v == null ? "" : v).replace(/"/g, '""') + '"';
    const linhas = [["data_hora", "evento", "ator", "papel", "plataforma", "navegador", "app_instalado", "idioma", "fuso", "tela", "novo_aparelho", "detalhe"].join(",")];
    carregado.forEach((x) => { const d = x.dispositivo || {}; linhas.push([x.timestamp && x.timestamp.toDate ? x.timestamp.toDate().toISOString() : "", x.evento, x.ator, x.papel, d.plataforma, d.navegador, d.instalado, d.idioma, d.fuso, d.tela, x.novo, x.detalhe].map(esc).join(",")); });
    const blob = new Blob(["﻿" + linhas.join("\n")], { type: "text/csv;charset=utf-8" });
    const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = `acessos_chocaiot_${new Date().toISOString().slice(0, 10)}.csv`; document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500);
  }
  function apagar() {
    const dono = state.papel !== "convidado";
    confirmarAcao(dono ? "Apagar todos os registros de acesso?" : "Apagar o seu histórico de acessos?", dono ? "Remove os registros de acesso de todas as pessoas desta chocadeira. Isto não pode ser desfeito." : "Remove os registros dos seus acessos. Isto não pode ser desfeito.", "Apagar", async () => {
      try {
        const alvo = carregado.slice(); for (let i = 0; i < alvo.length; i += 400) { const b = db.batch(); alvo.slice(i, i + 400).forEach((x) => b.delete(x.ref)); await b.commit(); }
        $("ac-fb").textContent = `${alvo.length} registro(s) apagado(s).`; carregado = []; desenhar(); $("ac-exportar").disabled = $("ac-apagar").disabled = true;
      } catch (e) { console.error(e); $("ac-fb").textContent = "Não foi possível apagar agora."; }
    });
  }
})();
