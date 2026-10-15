// painel.js — ChocaIoT PWA — Fase 104: ajustes de TELA e FANS do aparelho (brilho, reduzir, apagar, relógio sempre ligado, PWM dos fans).
//
// Protocolo (ver main.cpp, "Fase 104"):
//   aparelho -> app : campo "painel" do documento (junto do heartbeat, ~1 min) = "T|brilho|reduzirSeg|apagarSeg|manter|relogio#F|ativo|f1|f2|f3"
//   app -> aparelho : cmd.telaSet = "<ms>|brilho|reduzirSeg|apagarSeg|manter|relogio"  ·  cmd.fansSet = "<ms>|ativo|f1|f2|f3"
// Fase 112 (funções dos fans): o painel ganha "#G|m0,m1,m2|e0,e1,e2|cMin,cMax|rMin,rMax,rIni10,rFull10|vMin,vMax,vIni,vFull|co2On,co2Ini,co2Full"
//   (m = função: 0 manual · 1 circulação · 2 resfriamento · 3 renovação; e = % efetivo) e o comando cmd.fansRegras = "<ms>|m0,m1,m2|cMin,cMax|rMin,rMax,rIni10,rFull10|vMin,vMax,vIni,vFull|co2On,co2Ini,co2Full"
// Custo no Firestore: nenhuma leitura nova (o campo vem no snapshot que o app já recebe); 1 escrita por ajuste confirmado.
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const ehDono = () => state.papel !== "convidado";
  const podeEditar = () => ehDono() || (state.perm || []).includes("total");
  function ref() { return db.collection("chocadeiras").doc(state.chocadeiraId); }
  function feedback(txt, tipo) { if (typeof mostrarFeedbackHeader === "function") mostrarFeedbackHeader(txt, tipo || "ok"); }

  let dados = null, ultRaw = "", editTela = 0, editFans = 0, editReg = 0, tTimer = null, fTimer = null, rTimer = null;

  function parse(s) {
    const m = /^T\|(\d+)\|(\d+)\|(\d+)\|([01])\|([01])#F\|([01])\|(\d+)\|(\d+)\|(\d+)(?:#R\|(-?\d+))?(?:#G\|(\S+))?$/.exec(String(s || ""));
    if (!m) return null;
    return { tela: { brilho: +m[1], dim: +m[2], off: +m[3], man: m[4] === "1", rel: m[5] === "1" },
             fans: { ativo: m[6] === "1", f: [+m[7], +m[8], +m[9]] },
             rpm: m[10] === undefined ? null : +m[10],   // rpm do fan 1 (tacômetro no GPIO17); -1 = sem sinal, null = firmware antigo
             g: parseG(m[11]) };
  }
  function parseG(t) {   // Fase 112
    if (!t) return null;
    const q = t.split("|"); if (q.length < 6) return null;
    const n = (x) => x.split(",").map(Number);
    const modos = n(q[0]), ef = n(q[1]), c = n(q[2]), r = n(q[3]), v = n(q[4]), co2 = n(q[5]);
    if (modos.length < 3 || ef.length < 3 || c.length < 2 || r.length < 4 || v.length < 4) return null;
    return { modos, ef, c, r, v, co2 };
  }
  function mostraRpm(rpm) {
    // Fase 115c: o rpm entra na rotação do card Status (uma informação por vez, a cada 5 s)
    try { if (typeof state !== "undefined") { const novo = rpm !== null && rpm > 0 ? rpm : 0; if (state.rpm !== novo) { state.rpm = novo; if (typeof atualizarCardStatus === "function") atualizarCardStatus(); } } } catch (e) { /* ignora */ }
    const el = $("pn-f1-rpm"); if (!el) return;
    el.textContent = rpm === null ? "tacômetro: —" : rpm < 0 ? "sem sinal do tacômetro" : `${rpm} rpm`;
    el.classList.toggle("pn-rpm-ok", rpm !== null && rpm > 0);
  }

  // ───────── desenho ─────────
  function rotuloDim(v) { return v === 0 ? "nunca" : tempo(v); }
  function tempo(s) { return s >= 60 ? `${Math.floor(s / 60)} min${s % 60 ? " " + (s % 60) + " s" : ""}` : `${s} s`; }
  function ajustaLimiteDim() {
    const off = +$("pn-off").value, dim = $("pn-dim");
    dim.max = String(Math.max(0, off - 10));
    if (+dim.value > +dim.max) dim.value = dim.max;
  }
  function rotulos() {
    $("pn-brilho-v").textContent = `${$("pn-brilho").value}%`;
    $("pn-dim-v").textContent = rotuloDim(+$("pn-dim").value);
    $("pn-off-v").textContent = tempo(+$("pn-off").value);
    for (let i = 1; i <= 3; i++) $(`pn-f${i}-v`).textContent = `${$(`pn-f${i}`).value}%`;
    const man = $("pn-man").checked;
    ["pn-dim", "pn-off", "pn-rel"].forEach((id) => { const e = $(id); if (e) e.closest(".config-row").classList.toggle("pn-apagado", man); });
    const ativo = $("pn-fans-ativo").checked;
    for (let i = 1; i <= 3; i++) $(`pn-f${i}`).closest(".config-row").classList.toggle("pn-apagado", !ativo);
  }
  function habilita() {
    const ok = !!dados && podeEditar();
    ["pn-brilho", "pn-dim", "pn-off", "pn-rel", "pn-man", "pn-fans-ativo", "pn-f1", "pn-f2", "pn-f3", "pn-fn1", "pn-fn2", "pn-fn3",
     "rg-cmin", "rg-cmax", "rg-rmin", "rg-rmax", "rg-rini", "rg-rfull", "rg-vmin", "rg-vmax", "rg-vini", "rg-vfull", "rg-co2on", "rg-co2ini", "rg-co2full"].forEach((id) => { const e = $(id); if (e) e.disabled = !ok; });
    for (let i = 1; i <= 3; i++) { const fn = +($(`pn-fn${i}`).value || 0), sl = $(`pn-f${i}`); if (sl && fn !== 0) sl.disabled = true; }   // função ativa: % vem das regras
    const nota = !dados ? "Aguardando dados do aparelho… (precisa do firmware com a Fase 104 gravado)"
      : !podeEditar() ? "Você não tem permissão para alterar estes ajustes." : "";
    $("pn-tela-nota").textContent = nota; $("pn-fans-nota").textContent = nota;
    const rn = $("rg-nota"); if (rn && nota) rn.textContent = nota;
  }
  function aplicarNaTela(p) {
    const agora = Date.now();
    if (agora > editTela) {
      $("pn-brilho").value = p.tela.brilho; $("pn-off").value = p.tela.off; ajustaLimiteDim();
      $("pn-dim").value = Math.min(p.tela.dim, +$("pn-dim").max); $("pn-man").checked = p.tela.man; $("pn-rel").checked = p.tela.rel;
    }
    if (agora > editFans) {
      $("pn-fans-ativo").checked = p.fans.ativo;
      p.fans.f.forEach((v, i) => { $(`pn-f${i + 1}`).value = v; });
    }
    if (p.g && agora > editReg) {   // Fase 112: funções e regras
      p.g.modos.slice(0, 3).forEach((m, i) => { const sel = $(`pn-fn${i + 1}`); if (sel) sel.value = String(m); });
      const set = (id, v) => { const e = $(id); if (e && document.activeElement !== e) e.value = String(v); };
      set("rg-cmin", p.g.c[0]); set("rg-cmax", p.g.c[1]);
      set("rg-rmin", p.g.r[0]); set("rg-rmax", p.g.r[1]); set("rg-rini", (p.g.r[2] / 10).toFixed(1)); set("rg-rfull", (p.g.r[3] / 10).toFixed(1));
      set("rg-vmin", p.g.v[0]); set("rg-vmax", p.g.v[1]); set("rg-vini", p.g.v[2]); set("rg-vfull", p.g.v[3]);
      { const on = $("rg-co2on"); if (on && p.g.co2.length >= 3) on.checked = p.g.co2[0] === 1;   // Fase 116: renovação por CO₂
        if (p.g.co2.length >= 3) { set("rg-co2ini", p.g.co2[1]); set("rg-co2full", p.g.co2[2]); } }
    }
    if (p.g) p.g.modos.slice(0, 3).forEach((m, i) => { if (m !== 0) { const sl = $(`pn-f${i + 1}`); if (sl) sl.value = String(p.g.ef[i]); } });   // função ativa: mostra o % efetivo
    rotulos(); habilita();
  }
  // chamado por app.js a cada snapshot do documento da chocadeira (sem leitura extra)
  window.painelDoDoc = function (d) {
    const raw = d && typeof d.painel === "string" ? d.painel : "";
    const p = parse(raw);
    if (!p) { if (!dados) habilita(); return; }
    dados = p;
    mostraRpm(p.rpm);
    if (raw !== ultRaw) { ultRaw = raw; aplicarNaTela(p); } else habilita();
  };

  // ── ao vivo: enquanto o cartão Fans está visível o aparelho publica o rpm a cada ~5 s (cmd.fansVivo renovado a cada 40 s; só quem pode editar) ──
  // "Visível" = a tela Configurações está ativa (o cartão tem layout) e a aba está em primeiro plano. Verifica a cada 5 s; renova no máx. a cada 35 s.
  let ultVivo = 0;
  function tentaVivo() {
    const c = $("pn-fans-card");
    if (!c || c.offsetParent === null || document.visibilityState !== "visible") return;
    if (!state.chocadeiraId || !podeEditar() || Date.now() - ultVivo < 35000) return;
    ultVivo = Date.now();
    ref().update({ "cmd.fansVivo": String(ultVivo) }).catch(() => {});
  }
  setInterval(tentaVivo, 5000);
  document.addEventListener("visibilitychange", tentaVivo);

  // ───────── escrita (app -> aparelho) ─────────
  function enviarTela() {
    if (!podeEditar() || !state.chocadeiraId) return;
    const b = +$("pn-brilho").value, off = +$("pn-off").value;
    let dim = +$("pn-dim").value; if (dim > 0 && dim > off - 10) dim = off - 10;
    const man = $("pn-man").checked ? 1 : 0, rel = $("pn-rel").checked ? 1 : 0;
    ref().update({ "cmd.telaSet": `${Date.now()}|${b}|${dim}|${off}|${man}|${rel}` })
      .then(() => {
        feedback("Ajuste da tela enviado ao aparelho");
        if (typeof registrarAcao === "function") registrarAcao("Automacao", `Tela do aparelho: brilho ${b}%, reduzir ${dim ? dim + " s" : "nunca"}, apagar ${off} s, manter ligada ${man ? "sim" : "não"}, relógio sempre ligado ${rel ? "sim" : "não"}`);
      })
      .catch(() => feedback("Não foi possível enviar o ajuste da tela", "error"));
  }
  function enviarFans() {
    if (!podeEditar() || !state.chocadeiraId) return;
    const at = $("pn-fans-ativo").checked ? 1 : 0, f = [1, 2, 3].map((i) => +$(`pn-f${i}`).value);
    ref().update({ "cmd.fansSet": `${Date.now()}|${at}|${f[0]}|${f[1]}|${f[2]}` })
      .then(() => {
        feedback("Ajuste dos fans enviado ao aparelho");
        if (typeof registrarAcao === "function") registrarAcao("Automacao", `Fans do aparelho: PWM ${at ? "ligado" : "desligado"}, fan 1 ${f[0]}%, fan 2 ${f[1]}%, fan 3 ${f[2]}%`);
      })
      .catch(() => feedback("Não foi possível enviar o ajuste dos fans", "error"));
  }
  function enviarRegras() {   // Fase 112
    if (!podeEditar() || !state.chocadeiraId) return;
    const num = (id, d) => { const v = Number($(id).value); return Number.isFinite(v) ? v : d; };
    const cl = (v, a, b) => Math.max(a, Math.min(b, Math.round(v)));
    const m = [1, 2, 3].map((i) => cl(num(`pn-fn${i}`, 0), 0, 3));
    const cMin = cl(num("rg-cmin", 40), 0, 100), cMax = cl(num("rg-cmax", 100), cMin, 100);
    const rMin = cl(num("rg-rmin", 0), 0, 100), rMax = cl(num("rg-rmax", 100), rMin, 100);
    const rIni = cl(num("rg-rini", 0) * 10, 0, 99), rFull = cl(num("rg-rfull", 1) * 10, rIni + 1, 100);
    const vMin = cl(num("rg-vmin", 20), 0, 100), vMax = cl(num("rg-vmax", 100), vMin, 100);
    const vIni = cl(num("rg-vini", 0), 0, 29), vFull = cl(num("rg-vfull", 5), vIni + 1, 30);
    const co2On = $("rg-co2on") && $("rg-co2on").checked ? 1 : 0;   // Fase 116
    const co2Ini = cl(num("rg-co2ini", 1500), 500, 4900), co2Full = cl(num("rg-co2full", 3000), co2Ini + 100, 5000);
    const cmd = `${Date.now()}|${m.join(",")}|${cMin},${cMax}|${rMin},${rMax},${rIni},${rFull}|${vMin},${vMax},${vIni},${vFull}|${co2On},${co2Ini},${co2Full}`;
    ref().update({ "cmd.fansRegras": cmd })
      .then(() => {
        feedback("Funções e regras dos fans enviadas ao aparelho");
        const NOME = ["Manual", "Circulação", "Resfriamento", "Renovação"];
        if (typeof registrarAcao === "function") registrarAcao("Automacao", `Funções dos fans: ${NOME[m[0]]} / ${NOME[m[1]]} / ${NOME[m[2]]}; regras: circulação ${cMin}–${cMax}%, resfriamento ${rMin}–${rMax}% (+${rIni / 10}…+${rFull / 10} °C), renovação ${vMin}–${vMax}% (+${vIni}…+${vFull} pts), CO₂ ${co2On ? `${co2Ini}…${co2Full} ppm` : "desligado"}`);
      })
      .catch(() => feedback("Não foi possível enviar as regras dos fans", "error"));
  }
  function agendaRegras() { editReg = Date.now() + 15000; clearTimeout(rTimer); rTimer = setTimeout(enviarRegras, 900); }
  function agendaTela() { editTela = Date.now() + 15000; clearTimeout(tTimer); tTimer = setTimeout(enviarTela, 700); }
  function agendaFans() { editFans = Date.now() + 15000; clearTimeout(fTimer); fTimer = setTimeout(enviarFans, 700); }

  ["pn-brilho", "pn-dim", "pn-off", "pn-rel", "pn-man"].forEach((id) => {
    const e = $(id); if (!e) return;
    e.addEventListener("input", () => { if (id === "pn-off") ajustaLimiteDim(); rotulos(); agendaTela(); });
    e.addEventListener("change", () => { rotulos(); agendaTela(); });
  });
  ["pn-fans-ativo", "pn-f1", "pn-f2", "pn-f3"].forEach((id) => {
    const e = $(id); if (!e) return;
    e.addEventListener("input", () => { rotulos(); agendaFans(); });
    e.addEventListener("change", () => { rotulos(); agendaFans(); });
  });
  ["pn-fn1", "pn-fn2", "pn-fn3", "rg-cmin", "rg-cmax", "rg-rmin", "rg-rmax", "rg-rini", "rg-rfull", "rg-vmin", "rg-vmax", "rg-vini", "rg-vfull", "rg-co2on", "rg-co2ini", "rg-co2full"].forEach((id) => {
    const e = $(id); if (!e) return;
    e.addEventListener("change", () => { rotulos(); habilita(); agendaRegras(); });
  });
  rotulos(); habilita();
})();
