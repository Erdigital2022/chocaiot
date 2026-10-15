// share.js — ChocaIoT PWA — Fase 88: compartilhamento da chocadeira (convites + permissões)
//
// Modelo (Firestore):  chocadeiras/{id}/shared_with/{e-mail do convidado em minúsculo}
//   { email, permissoes[], status: pendente|aceito|recusado, token, dataConvite, donoNome, donoEmail, chocNome,
//     uid, respondidoEm, tokenAceite, ultimoAcesso, nomeConvidado, permAlteradaEm }
// Permissões (array): "ver" · "ch0".."ch7" (atuador) · "ciclo" (iniciar/pausar/encerrar) · "total" (tudo, menos convidados)
// Segurança de verdade está nas regras do Firestore (firestore.rules, testadas no emulador); este arquivo só adapta a interface.
// Sem servidor de e-mail neste projeto: o convite sai por link (e-mail via "mailto:", WhatsApp, Compartilhar, Copiar).
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const PEND_KEY = "choca_convite_pendente";
  const LAST_KEY = "choca_acesso_compartilhado";
  const ATUADORES_CONVITE = [0, 1, 3, 4, 5, 7];   // Bomba Água (CH3, 220 V) e Cooler Sistema (CH7) nunca são liberados ao convidado
  const REQ_TOTAL_CARDS = ["Chocadeira", "Tipos de ovos", "Hardware", "Automação", "Arquivos (cartão SD)", "Notificações", "Segurança"];

  let unsubOwner = null, unsubGuest = null, primeiraOwner = true, ownerDocs = [], editando = null;

  // ───────── util ─────────
  const emailNorm = (e) => String(e || "").trim().toLowerCase();
  const emailOk = (e) => /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(e);
  function lerJson(k) { try { return JSON.parse(localStorage.getItem(k) || "null"); } catch (e) { return null; } }
  function gravarJson(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* storage bloqueado */ } }
  function apagar(k) { try { localStorage.removeItem(k); } catch (e) { /* ok */ } }
  function tokenAleatorio(n) {
    const abc = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const buf = new Uint8Array(n); crypto.getRandomValues(buf);
    return Array.from(buf, (b) => abc[b % abc.length]).join("");
  }
  function msAgo(ts) {
    if (!ts) return null;
    const d = ts.toDate ? ts.toDate() : new Date(ts);
    return Date.now() - d.getTime();
  }
  function relativo(ts) {
    const ms = msAgo(ts);
    if (ms == null) return "nunca";
    const s = Math.max(0, Math.round(ms / 1000));
    if (s < 90) return "agora há pouco";
    if (s < 3600) return `há ${Math.round(s / 60)} min`;
    if (s < 86400) return `há ${Math.round(s / 3600)} h`;
    return `há ${Math.round(s / 86400)} d`;
  }
  function nomeDono() {
    const u = auth.currentUser;
    return (u && (u.displayName || u.email)) || "O proprietário";
  }
  function nomeChoc() {
    const b = $("cycle-banner");
    return (b && b.dataset.nomeChocadeira) || "Chocadeira";
  }
  function notificar(titulo, corpo) {
    try { if (typeof mostrarFeedbackHeader === "function") mostrarFeedbackHeader(`${titulo}${corpo ? " — " + corpo : ""}`, "ok"); } catch (e) { /* ok */ }
    try {
      if ("Notification" in window && Notification.permission === "granted" && document.hidden && navigator.serviceWorker) {
        navigator.serviceWorker.ready.then((r) => r.showNotification(titulo, { body: corpo || "", tag: "choca-share", icon: "/icons/icon-192.png" })).catch(() => {});
      }
    } catch (e) { /* ok */ }
  }

  window.shareNotificar = notificar;   // Fase 90 — usado por acessos.js

  // ───────── textos de permissão ─────────
  const NOMES_CH = () => (typeof RELAY_NAMES !== "undefined" ? RELAY_NAMES : ["Resistência", "Umidificador", "Bomba Água", "Bomba Ar", "LED Ovoscópio", "Áudio", "Cooler Sistema", "Fans"]);
  function listaPerms(perms) {
    const p = perms || [];
    if (p.includes("total")) return ["Ver tudo (temperatura, umidade, gráficos, histórico)", "Controle total: atuadores, ciclo, automação, tipos de ovos e comandos", "Não gerencia outros convidados"];
    const out = ["Ver temperatura, umidade, gráficos, ciclo e histórico"];
    const chs = ATUADORES_CONVITE.filter((i) => p.includes("ch" + i)).map((i) => NOMES_CH()[i]);
    if (chs.length) out.push("Ligar/desligar: " + chs.join(", ")); else out.push("Não controla atuadores");
    out.push(p.includes("ciclo") ? "Gerenciar o ciclo (iniciar, pausar, encerrar)" : "Não gerencia o ciclo");
    return out;
  }
  function resumoPerms(perms) {
    const p = perms || [];
    if (p.includes("total")) return "Controle total";
    const chs = ATUADORES_CONVITE.filter((i) => p.includes("ch" + i)).length;
    const partes = [];
    if (chs) partes.push(`${chs} atuador${chs > 1 ? "es" : ""}`);
    if (p.includes("ciclo")) partes.push("ciclo");
    return partes.length ? "Controle parcial: " + partes.join(" + ") : "Visualização";
  }

  // ───────── link do convite → guarda e limpa a URL ─────────
  (function capturarLink() {
    try {
      const q = new URLSearchParams(location.search);
      const c = q.get("convite"), t = q.get("t");
      if (c && t) {
        gravarJson(PEND_KEY, { c, t, em: Date.now() });
        history.replaceState(null, "", location.pathname + location.hash);
      }
    } catch (e) { /* ok */ }
    if (lerJson(PEND_KEY)) { const a = $("login-convite-aviso"); if (a) a.hidden = false; }
  })();

  // ═════════════ CONVIDADO: aceitar / recusar ═════════════
  function erroConvite(msg, mostrarTrocar) {
    $("convite-resumo").textContent = "Não foi possível abrir o convite";
    $("convite-info").hidden = true; $("convite-acoes").hidden = true;
    $("convite-erro").textContent = msg;
    $("convite-acoes-erro").hidden = false;
    $("btn-convite-trocar").hidden = !mostrarTrocar;
    irParaTela("convite");
  }
  $("btn-convite-trocar").addEventListener("click", () => { auth.signOut(); });   // volta ao login com o convite ainda guardado
  $("btn-convite-sair").addEventListener("click", async () => {
    apagar(PEND_KEY);
    const u = auth.currentUser; if (!u) { irParaTela("login"); return; }
    showScreen("loading");
    const ch = await buscarChocadeiraDoUsuario(u.uid).catch(() => null);
    if (ch) { entrarNaChocadeira(ch, "dono", null); return; }
    const g = await window.shareBuscarAcessoConvidado(u).catch(() => null);
    if (g) entrarNaChocadeira(g.chocadeira, "convidado", g.conv); else irParaTela("vincular");
  });

  window.shareProcessarConvitePendente = async function (user) {
    const p = lerJson(PEND_KEY);
    if (!p) return false;
    if (Date.now() - (p.em || 0) > 14 * 86400000) { apagar(PEND_KEY); return false; }   // link velho demais
    const mail = emailNorm(user.email);
    const ref = db.collection("chocadeiras").doc(p.c).collection("shared_with").doc(mail);
    let snap = null;
    try { snap = await ref.get(); } catch (e) { snap = null; }
    if (!snap || !snap.exists) { erroConvite(`Este convite não é para ${user.email} (ou foi cancelado pelo proprietário). Entre com a conta Google do e-mail que foi convidado.`, true); return true; }
    const d = snap.data();
    if (d.status === "aceito") { gravarJson(LAST_KEY, { c: p.c }); apagar(PEND_KEY); return false; }   // já aceito: segue o fluxo normal
    if (d.status !== "pendente") { apagar(PEND_KEY); erroConvite("Este convite foi recusado ou cancelado. Peça um novo link ao proprietário.", false); return true; }
    if (d.token !== p.t) { erroConvite("Este link foi substituído por um convite mais novo. Use o link mais recente que o proprietário enviou.", false); return true; }
    mostrarConvite(user, p, d, ref);
    return true;
  };

  function mostrarConvite(user, p, d, ref) {
    $("convite-resumo").textContent = "Você foi convidado(a) para acompanhar uma chocadeira.";
    $("convite-dono").textContent = d.donoNome || d.donoEmail || "Proprietário";
    $("convite-choc").textContent = d.chocNome || "Chocadeira";
    $("convite-email").textContent = user.email || "";
    const ul = $("convite-perms"); ul.textContent = "";
    listaPerms(d.permissoes).forEach((t) => { const li = document.createElement("li"); li.textContent = t; ul.appendChild(li); });
    $("convite-info").hidden = false; $("convite-acoes").hidden = false; $("convite-acoes-erro").hidden = true; $("convite-erro").textContent = "";
    const bA = $("btn-convite-aceitar"), bR = $("btn-convite-recusar");
    bA.disabled = bR.disabled = false;
    bA.onclick = async () => {
      bA.disabled = bR.disabled = true; bA.textContent = "Aceitando…";
      try {
        await ref.update({ status: "aceito", uid: user.uid, tokenAceite: p.t, nomeConvidado: user.displayName || "", respondidoEm: firebase.firestore.FieldValue.serverTimestamp(), ultimoAcesso: firebase.firestore.FieldValue.serverTimestamp() });
        await db.collection("usuarios").doc(user.uid).set({ email: user.email || "", nome: user.displayName || "", uid: user.uid, acessos: firebase.firestore.FieldValue.arrayUnion(p.c) }, { merge: true });
        gravarJson(LAST_KEY, { c: p.c }); apagar(PEND_KEY);
        db.collection("chocadeiras").doc(p.c).update({ "cmd.convite": `${Date.now()}|${emailNorm(user.email)}|aceito` }).catch(() => {});   // o aparelho avisa o dono por push
        const [ch, cv] = await Promise.all([db.collection("chocadeiras").doc(p.c).get(), ref.get()]);
        bA.textContent = "Aceitar";
        entrarNaChocadeira({ id: p.c, ...ch.data() }, "convidado", cv.data());
        if (window.acConviteAceito) window.acConviteAceito();
      } catch (err) {
        console.error("Erro ao aceitar convite:", err);
        $("convite-erro").textContent = "Não foi possível aceitar agora. Confira a internet e tente de novo.";
        bA.disabled = bR.disabled = false; bA.textContent = "Aceitar";
      }
    };
    bR.onclick = async () => {
      bA.disabled = bR.disabled = true;
      try {
        await ref.update({ status: "recusado", tokenAceite: p.t, respondidoEm: firebase.firestore.FieldValue.serverTimestamp() });
        apagar(PEND_KEY);
        $("convite-resumo").textContent = "Convite recusado.";
        $("convite-info").hidden = true; $("convite-acoes").hidden = true; $("convite-erro").textContent = "O proprietário será avisado.";
        $("btn-convite-trocar").hidden = true; $("convite-acoes-erro").hidden = false;
      } catch (err) {
        console.error(err); $("convite-erro").textContent = "Não foi possível recusar agora. Tente de novo.";
        bA.disabled = bR.disabled = false;
      }
    };
    irParaTela("convite");
  }

  // Login seguinte de quem já aceitou: procura o acesso compartilhado
  window.shareBuscarAcessoConvidado = async function (user) {
    const mail = emailNorm(user.email);
    const ids = new Set();
    const ult = lerJson(LAST_KEY); if (ult && ult.c) ids.add(ult.c);
    try { const u = await db.collection("usuarios").doc(user.uid).get(); ((u.data() || {}).acessos || []).forEach((x) => ids.add(x)); } catch (e) { /* ok */ }
    for (const id of ids) {
      try {
        const cv = await db.collection("chocadeiras").doc(id).collection("shared_with").doc(mail).get();
        if (!cv.exists) continue;
        const c = cv.data();
        if (c.status !== "aceito" || c.uid !== user.uid) continue;
        const ch = await db.collection("chocadeiras").doc(id).get();
        if (ch.exists) { gravarJson(LAST_KEY, { c: id }); return { chocadeira: { id, ...ch.data() }, conv: c }; }
      } catch (e) { /* sem acesso a este: tenta o próximo */ }
    }
    return null;
  };

  // ═════════════ ENTRADA / SAÍDA ═════════════
  function esconderCards() { $("share-card-owner").hidden = true; $("share-card-guest").hidden = true; }
  window.shareAoSair = function () {
    if (unsubOwner) { unsubOwner(); unsubOwner = null; }
    if (unsubGuest) { unsubGuest(); unsubGuest = null; }
    ownerDocs = []; primeiraOwner = true;
    esconderCards();
    document.body.classList.remove("papel-convidado", "sem-ciclo", "sem-total");
  };
  window.shareAoEntrar = function (papel, conv) {
    window.shareAoSair();
    if (papel === "convidado") {
      $("share-card-guest").hidden = false;
      atualizarCardConvidado();
      iniciarListenerConvidado();
    } else {
      $("share-card-owner").hidden = false;
      iniciarListenerDono();
    }
    window.shareAplicarPermissoes();
  };

  function atualizarCardConvidado() {
    const c = state.conv || {};
    $("share-g-dono").textContent = c.donoNome || c.donoEmail || "--";
    $("share-g-perm").textContent = listaPerms(state.perm).join(" · ");
  }
  $("btn-share-sair").addEventListener("click", () => {
    confirmarAcao("Sair do compartilhamento?", "Você deixa de ver e controlar esta chocadeira. O proprietário pode convidar você de novo.", "Sair", async () => {
      const u = auth.currentUser; if (!u || !state.chocadeiraId) return;
      try {
        if (window.acRegistrar) window.acRegistrar("saida", "Saiu do compartilhamento");
        await db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with").doc(emailNorm(u.email)).delete();
        apagar(LAST_KEY);
        await db.collection("usuarios").doc(u.uid).set({ acessos: firebase.firestore.FieldValue.arrayRemove(state.chocadeiraId) }, { merge: true }).catch(() => {});
      } catch (e) { console.error(e); }
      acessoRemovido("Você saiu do compartilhamento.");
    });
  });
  function acessoRemovido(msg) {
    window.shareAoSair();
    if (state.unsubscribeAtuadores) { state.unsubscribeAtuadores(); state.unsubscribeAtuadores = null; }
    if (state.unsubscribeLogs) { state.unsubscribeLogs(); state.unsubscribeLogs = null; }
    state.chocadeiraId = null; state.papel = "dono"; state.perm = null; state.conv = null;
    const e = $("vincular-error"); if (e) e.textContent = msg || "";
    irParaTela("vincular");
  }

  function iniciarListenerConvidado() {
    const u = auth.currentUser; if (!u) return;
    const ref = db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with").doc(emailNorm(u.email));
    const chave = `choca_perm_${state.chocadeiraId}`;
    // "último acesso" (no máximo 1 escrita a cada 20 min por aparelho)
    const ult = Number(localStorage.getItem(chave + "_acc") || 0);
    if (Date.now() - ult > 20 * 60000) { ref.update({ ultimoAcesso: firebase.firestore.FieldValue.serverTimestamp() }).then(() => { try { localStorage.setItem(chave + "_acc", String(Date.now())); } catch (e) { /* ok */ } }).catch(() => {}); }
    unsubGuest = ref.onSnapshot((snap) => {
      if (!snap.exists) { acessoRemovido("O proprietário removeu o seu acesso a esta chocadeira."); return; }
      const d = snap.data();
      if (d.status !== "aceito") { acessoRemovido("Seu acesso a esta chocadeira foi encerrado pelo proprietário."); return; }
      const novas = JSON.stringify((d.permissoes || []).slice().sort());
      const antes = localStorage.getItem(chave);
      state.conv = d; state.perm = d.permissoes || ["ver"];
      atualizarCardConvidado();
      window.shareAplicarPermissoes();
      if (antes !== null && antes !== novas) notificar("Permissões alteradas", "O proprietário mudou o que você pode fazer: " + resumoPerms(state.perm));
      try { localStorage.setItem(chave, novas); } catch (e) { /* ok */ }
    }, (err) => { console.error("Listener do convite:", err); });
  }

  // ═════════════ APLICAR PERMISSÕES NA INTERFACE ═════════════
  function temPerm(p) { return state.papel !== "convidado" || (state.perm || []).includes("total") || (state.perm || []).includes(p); }
  window.sharePodeAtuador = (i) => temPerm("ch" + i);
  window.sharePodeCiclo = () => temPerm("ciclo");
  function cadeado(el, motivo) {
    el.classList.add("perm-bloq");
    el.title = motivo;
    if ("disabled" in el) el.disabled = true;
    el.setAttribute("aria-disabled", "true");
  }
  function cardPorTitulo(t) {
    return Array.from(document.querySelectorAll("#screen-configuracoes .atuadores-card")).find((c) => { const s = c.querySelector(".section-title"); return s && s.textContent.trim() === t; });
  }
  function travarCard(card, travar) {
    if (!card) return;
    card.classList.toggle("perm-bloq-card", travar);
    let lk = card.querySelector(":scope > .perm-lock");
    if (travar && !lk) { lk = document.createElement("div"); lk.className = "perm-lock"; lk.textContent = "🔒 Sem permissão"; lk.title = "Sem permissão — peça ao proprietário"; card.prepend(lk); }
    if (!travar && lk) lk.remove();
  }
  function botoesDinamicos(raiz) {
    // Janelas montadas por extras.js: "ovos colocados" pede ciclo; "abrir a caixa/manter ambiente" pedem total
    const total = temPerm("total"), ciclo = temPerm("ciclo");
    (raiz || document).querySelectorAll("#status-overlay button, #ciclo-preparo button").forEach((b) => {
      if (b.classList.contains("prep-fechar")) return;
      const txt = (b.textContent || "").toLowerCase();
      const precisa = /ovos colocados|cancelar/.test(txt) ? ciclo : total;
      b.hidden = state.papel === "convidado" && !precisa;
    });
  }
  window.shareAplicarPermissoes = function () {
    const conv = state.papel === "convidado";
    const total = !conv || temPerm("total"), ciclo = !conv || temPerm("ciclo");
    document.body.classList.toggle("papel-convidado", conv);
    document.body.classList.toggle("sem-ciclo", !ciclo);
    document.body.classList.toggle("sem-total", !total);
    if (!conv) {
      document.querySelectorAll(".perm-bloq").forEach((el) => { el.classList.remove("perm-bloq"); if ("disabled" in el) el.disabled = false; el.removeAttribute("aria-disabled"); el.title = ""; });
      document.querySelectorAll(".perm-bloq-card").forEach((c) => travarCard(c, false));
      document.querySelectorAll(".atuador-sem-perm").forEach((el) => el.classList.remove("atuador-sem-perm"));
      return;
    }
    // atuadores: sem permissão → desabilitado + cadeado
    NOMES_CH().forEach((_, i) => {
      const input = $(`atuador-ch${i}`); if (!input) return;
      const item = input.closest(".atuador-item");
      if (i === 2 || i === 6) return;   // Bomba/Cooler já têm bloqueio próprio
      const ok = temPerm("ch" + i);
      if (item) item.classList.toggle("atuador-sem-perm", !ok);
      if (!ok) { input.disabled = true; const l = input.closest(".toggle"); if (l) l.title = "Sem permissão"; }
    });
    // cartões de configuração que exigem controle total
    REQ_TOTAL_CARDS.forEach((t) => travarCard(cardPorTitulo(t), !total));
    // controles soltos
    const alvosTotal = ["btn-desligar", "btn-camera-foto", "camera-auto-toggle", "btn-logs-clear", "btn-conta-rec", "btn-nome-choc"];
    alvosTotal.forEach((id) => { const el = $(id); if (!el) return; if (!total) cadeado(el, "Sem permissão"); });
    botoesDinamicos();
  };
  new MutationObserver((muts) => {
    if (state.papel !== "convidado") return;
    for (const m of muts) for (const n of m.addedNodes) if (n.nodeType === 1 && (n.id === "status-overlay" || (n.querySelector && n.querySelector("#status-overlay, #ciclo-preparo button")))) { botoesDinamicos(); return; }
  }).observe(document.body, { childList: true, subtree: true });

  // convidado sem "ciclo" não inicia ciclo pelo card (o wizard abriria e falharia na gravação)
  document.addEventListener("click", (ev) => {
    if (state.papel !== "convidado" || temPerm("ciclo")) return;
    const card = ev.target.closest && ev.target.closest("#card-ciclo");
    if (card && !state.cicloIniciadoEm) { ev.stopImmediatePropagation(); ev.preventDefault(); notificar("Sem permissão", "Você não pode iniciar o ciclo."); }
  }, true);

  // ═════════════ DONO: lista, convites e notificações ═════════════
  let statusAnt = {};
  function iniciarListenerDono() {
    if (!state.chocadeiraId) return;
    primeiraOwner = true; statusAnt = {};
    const chave = `choca_share_visto_${state.chocadeiraId}`;
    const msgResp = (x) => `${x.nomeConvidado || x.email} ${x.status === "aceito" ? "aceitou" : "recusou"} o convite.`;
    unsubOwner = db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with").onSnapshot((snap) => {
      ownerDocs = snap.docs.map((d) => ({ id: d.id, ...d.data() }));
      renderLista();
      const visto = Number(localStorage.getItem(chave) || 0);
      snap.docs.forEach((d) => {
        const x = d.data(), t = x.respondidoEm && x.respondidoEm.toMillis ? x.respondidoEm.toMillis() : 0;
        const resp = x.status === "aceito" || x.status === "recusado";
        if (primeiraOwner) { if (resp && t > visto) notificar(x.status === "aceito" ? "Convite aceito" : "Convite recusado", msgResp(x)); }   // aconteceu com o app fechado
        else if (resp && statusAnt[d.id] && statusAnt[d.id] !== x.status) notificar(x.status === "aceito" ? "Convite aceito" : "Convite recusado", msgResp(x));
      });
      primeiraOwner = false;
      statusAnt = {}; snap.docs.forEach((d) => { statusAnt[d.id] = d.data().status; });
      try { localStorage.setItem(chave, String(Date.now())); } catch (e) { /* ok */ }
    }, (err) => { console.error("Listener de convidados:", err); });
  }

  const STATUS_TXT = { pendente: ["Pendente", "pendente"], aceito: ["Ativo", "aceito"], recusado: ["Recusado", "recusado"] };
  function renderLista() {
    const box = $("share-lista"); box.textContent = "";
    const docs = ownerDocs.filter((d) => d.email).sort((a, b) => String(a.email).localeCompare(String(b.email)));
    if (!docs.length) { const v = document.createElement("div"); v.className = "logs-empty"; v.textContent = "Ninguém tem acesso além de você."; box.appendChild(v); return; }
    docs.forEach((d) => {
      const row = document.createElement("div"); row.className = "share-item";
      const info = document.createElement("div"); info.className = "share-info";
      const em = document.createElement("div"); em.className = "share-email"; em.textContent = d.nomeConvidado ? `${d.nomeConvidado} · ${d.email}` : d.email;
      const meta = document.createElement("div"); meta.className = "share-meta";
      const [rot, cls] = STATUS_TXT[d.status] || [d.status, "pendente"];
      const bd = document.createElement("span"); bd.className = `share-badge ${cls}`; bd.textContent = rot;
      const pm = document.createElement("span"); pm.textContent = resumoPerms(d.permissoes);
      const ac = document.createElement("span"); ac.className = "share-acesso"; ac.textContent = d.status === "aceito" ? `Último acesso: ${relativo(d.ultimoAcesso)}` : d.status === "pendente" ? `Convidado ${relativo(d.dataConvite)}` : "";
      meta.append(bd, pm, ac);
      info.append(em, meta);
      const acts = document.createElement("div"); acts.className = "share-acoes";
      const mk = (txt, fn, cls2) => { const b = document.createElement("button"); b.type = "button"; b.className = cls2 || "btn-secondary"; b.textContent = txt; b.addEventListener("click", fn); return b; };
      acts.appendChild(mk("Permissões", () => abrirModal(d)));
      if (d.status !== "aceito") acts.appendChild(mk(d.status === "pendente" ? "Reenviar" : "Convidar de novo", () => reenviar(d), "btn-warn"));
      acts.appendChild(mk(d.status === "aceito" ? "Remover" : "Cancelar", () => remover(d), "btn-danger"));
      row.append(info, acts); box.appendChild(row);
    });
  }
  function remover(d) {
    confirmarAcao(d.status === "aceito" ? "Remover o acesso?" : "Cancelar o convite?", `${d.email} ${d.status === "aceito" ? "deixa de ver e controlar a chocadeira agora" : "não poderá mais usar o link enviado"}.`, d.status === "aceito" ? "Remover" : "Cancelar convite", async () => {
      try { await db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with").doc(d.id).delete(); $("share-fb").textContent = `Acesso de ${d.email} removido.`; }
      catch (e) { console.error(e); $("share-fb").textContent = "Não foi possível remover agora."; }
    });
  }
  async function reenviar(d) {
    try {
      const token = tokenAleatorio(24);
      await db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with").doc(d.id).update({ token, status: "pendente", uid: firebase.firestore.FieldValue.delete(), tokenAceite: firebase.firestore.FieldValue.delete(), dataConvite: firebase.firestore.FieldValue.serverTimestamp(), donoNome: nomeDono(), chocNome: nomeChoc() });
      mostrarLink(d.email, token, true);
    } catch (e) { console.error(e); $("share-fb").textContent = "Não foi possível reenviar agora."; }
  }

  // ───────── modal: convidar / editar permissões ─────────
  function montarAtuadores() {
    const box = $("share-atuadores"); if (box.dataset.ok) return; box.dataset.ok = "1";
    ATUADORES_CONVITE.forEach((i) => {
      const l = document.createElement("label"); l.className = "share-chk";
      const c = document.createElement("input"); c.type = "checkbox"; c.value = "ch" + i; c.className = "share-ch";
      const sp = document.createElement("span"); sp.textContent = NOMES_CH()[i];
      l.append(c, sp); box.appendChild(l);
    });
  }
  function nivelAtual() { return (document.querySelector('input[name="share-nivel"]:checked') || {}).value || "ver"; }
  function sincronizarNivel() {
    const n = nivelAtual();
    $("share-atuadores").hidden = n !== "parcial";
    $("share-ciclo-wrap").classList.toggle("desab", n === "total");
    $("share-ciclo").disabled = n === "total";
    if (n === "total") $("share-ciclo").checked = true;
    if (n === "ver") $("share-ciclo").checked = false;
    $("share-ciclo").disabled = n === "total" || n === "ver";
    $("share-ciclo-wrap").classList.toggle("desab", n === "total" || n === "ver");
  }
  document.querySelectorAll('input[name="share-nivel"]').forEach((r) => r.addEventListener("change", sincronizarNivel));
  function permsDoFormulario() {
    const n = nivelAtual();
    if (n === "total") return ["ver", "total"];
    const p = ["ver"];
    if (n === "parcial") document.querySelectorAll(".share-ch:checked").forEach((c) => p.push(c.value));
    if (n === "parcial" && $("share-ciclo").checked) p.push("ciclo");
    return p;
  }
  function abrirModal(existente) {
    montarAtuadores();
    editando = existente || null;
    $("share-passo1").hidden = false; $("share-passo2").hidden = true; $("share-erro").textContent = "";
    $("share-modal-titulo").textContent = existente ? "Permissões de " + existente.email : "Convidar usuário";
    $("share-enviar").textContent = existente ? "Salvar permissões" : "Criar convite";
    const em = $("share-email"); em.value = existente ? existente.email : ""; em.disabled = !!existente;
    const p = (existente && existente.permissoes) || ["ver"];
    const nivel = p.includes("total") ? "total" : ATUADORES_CONVITE.some((i) => p.includes("ch" + i)) || p.includes("ciclo") ? "parcial" : "ver";
    document.querySelector(`input[name="share-nivel"][value="${nivel}"]`).checked = true;
    document.querySelectorAll(".share-ch").forEach((c) => { c.checked = p.includes(c.value); });
    $("share-ciclo").checked = p.includes("ciclo") || p.includes("total");
    sincronizarNivel();
    $("modal-share-backdrop").hidden = false;
    if (!existente) setTimeout(() => em.focus(), 50);
  }
  function fecharModal() { $("modal-share-backdrop").hidden = true; editando = null; }
  $("btn-share-convidar").addEventListener("click", () => abrirModal(null));
  $("share-modal-close").addEventListener("click", fecharModal);
  $("share-cancelar").addEventListener("click", fecharModal);
  $("share-fechar").addEventListener("click", fecharModal);
  $("modal-share-backdrop").addEventListener("click", (e) => { if (e.target.id === "modal-share-backdrop") fecharModal(); });

  $("share-enviar").addEventListener("click", async () => {
    const erro = $("share-erro"); erro.textContent = "";
    const u = auth.currentUser; if (!u || !state.chocadeiraId) return;
    const perms = permsDoFormulario();
    if (nivelAtual() === "parcial" && perms.length === 1) { erro.textContent = "Marque ao menos um atuador (ou escolha Visualização)."; return; }
    const col = db.collection("chocadeiras").doc(state.chocadeiraId).collection("shared_with");
    $("share-enviar").disabled = true;
    try {
      if (editando) {
        await col.doc(editando.id).update({ permissoes: perms, permAlteradaEm: firebase.firestore.FieldValue.serverTimestamp() });
        $("share-fb").textContent = `Permissões de ${editando.email} atualizadas${editando.status === "aceito" ? " — a pessoa é avisada no app" : ""}.`;
        fecharModal(); return;
      }
      const email = emailNorm($("share-email").value);
      if (!emailOk(email)) { erro.textContent = "Digite um e-mail válido."; return; }
      if (email === emailNorm(u.email)) { erro.textContent = "Este é o seu próprio e-mail."; return; }
      const ex = ownerDocs.find((d) => d.id === email);
      if (ex) { erro.textContent = ex.status === "aceito" ? "Esta pessoa já tem acesso. Use \"Permissões\" na lista." : "Já existe um convite para este e-mail. Use \"Reenviar\" na lista."; return; }
      const token = tokenAleatorio(24);
      await col.doc(email).set({
        email, permissoes: perms, status: "pendente", token,
        dataConvite: firebase.firestore.FieldValue.serverTimestamp(),
        donoUid: u.uid, donoNome: nomeDono(), donoEmail: u.email || "", chocNome: nomeChoc(),
      });
      mostrarLink(email, token, false);
    } catch (e) {
      console.error("Erro ao criar convite:", e);
      erro.textContent = "Não foi possível salvar agora (" + (e.code || "erro") + "). Confira a internet e tente de novo.";
    } finally { $("share-enviar").disabled = false; }
  });

  function linkConvite(token) { return `${location.origin}/?convite=${encodeURIComponent(state.chocadeiraId)}&t=${encodeURIComponent(token)}`; }
  function mostrarLink(email, token, reenvio) {
    const link = linkConvite(token);
    const msg = `${nomeDono()} te convidou para monitorar a chocadeira "${nomeChoc()}" no ChocaIoT.\nAbra o link e entre com a conta Google ${email} para aceitar:\n${link}`;
    $("share-passo1").hidden = true; $("share-passo2").hidden = false;
    $("share-modal-titulo").textContent = reenvio ? "Convite reenviado" : "Convite criado";
    $("share-ok-txt").textContent = reenvio ? "Novo link gerado (o anterior deixou de valer)." : "Convite criado!";
    $("share-link-email").textContent = email; $("share-link").value = link;
    $("share-env-mail").href = `mailto:${encodeURIComponent(email)}?subject=${encodeURIComponent("Convite para a chocadeira " + nomeChoc())}&body=${encodeURIComponent(msg)}`;
    $("share-env-wa").href = `https://wa.me/?text=${encodeURIComponent(msg)}`;
    $("share-env-share").hidden = !navigator.share;
    $("share-env-share").onclick = () => navigator.share({ title: "Convite ChocaIoT", text: msg, url: link }).catch(() => {});
    $("share-env-copiar").onclick = async () => {
      try { await navigator.clipboard.writeText(msg); $("share-env-copiar").textContent = "✔ Copiado"; }
      catch (e) { $("share-link").select(); document.execCommand && document.execCommand("copy"); $("share-env-copiar").textContent = "✔ Copiado"; }
      setTimeout(() => { $("share-env-copiar").textContent = "📋 Copiar link"; }, 2000);
    };
    $("modal-share-backdrop").hidden = false;
    $("share-fb").textContent = `Convite para ${email} criado. Envie o link; ele só funciona entrando com esse e-mail.`;
  }
})();
