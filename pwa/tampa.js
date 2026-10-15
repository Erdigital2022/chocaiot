// tampa.js — ChocaIoT PWA — Fase 98: tela "Tampa" (sensor ADXL345) ao vivo, com ajustes e calibração remotos.
//
// Protocolo (ver main.cpp, "Fase 98"):
//   aparelho -> app : campo adxlLive (string "v1|up|est|ang10|mov|vib|saude|det|tol|sens|temRef|temAw|vibRef|vibAw|ign|conf|dentro|sep10|auto|tampaEst|msgSeq|msg"),
//                     publicado a cada ~2,5 s SÓ enquanto esta tela está aberta (cmd.adxlVivo renovado a cada 40 s);
//   app -> aparelho : cmd.adxlVivo = "<ms>" · cmd.adxlCfg = "<ms>|det|tol|sens" · cmd.adxlCal = "<ms>|F" (fechada) | "<ms>|O" (fora).
// Custo no Firestore: 1 escrita a cada 2,5 s do aparelho só com a tela aberta; nada com ela fechada.
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const EST = { 0: ["SEM SENSOR", "off"], 1: ["SEM CALIBRAÇÃO", "cal"], 2: ["FECHADA", "ok"], 3: ["MOVENDO", "mov"], 4: ["ABERTA", "open"] };
  const SAUDE = ["sensor ok", "sem sensor", "leitura travada", "|a| fora de 1 g"];
  const ARC = Math.PI * 80;   // comprimento do semicírculo do medidor (raio 80)

  let T = null;                 // última telemetria ACEITA nesta abertura
  let ultDoc = "";              // última string adxlLive vista no documento (qualquer momento)
  let base = "";                // valor que já estava no documento quando a tela abriu (pode ser antigo)
  let recebidoEm = 0, aberta = false, vivoTimer = null, tickTimer = null, ultSeq = null, hist = [];
  let editandoAte = 0, cfgTimer = null, calPend = null;

  // ───────── dados ─────────
  function parse(s) {
    const p = String(s || "").split("|");
    if (p[0] !== "v1" || p.length < 22) return null;
    const n = (i) => Number(p[i]);
    return { up: n(1), est: n(2), ang: n(3) / 10, mov: n(4), vib: n(5), saude: n(6), det: n(7) === 1, tol: n(8), sens: n(9), temRef: n(10) === 1, temAw: n(11) === 1,
      vibRef: n(12), vibAw: n(13), ign: n(14), conf: n(15) === 1, dentro: n(16) === 1, sep: n(17) / 10, auto: n(18) === 1, tampa: n(19), seq: n(20), msg: p.slice(21).join("|") };
  }
  // chamado por app.js a cada snapshot do documento da chocadeira (sem leitura extra)
  window.tampaDoDoc = function (d) {
    const s = d && typeof d.adxlLive === "string" ? d.adxlLive : "";
    if (!s || s === ultDoc) return;
    ultDoc = s;
    if (!aberta || s === base) return;     // só confia no que chegou DEPOIS de abrir a tela
    const t = parse(s); if (!t) return;
    T = t; recebidoEm = Date.now();
    hist.push({ v: t.vib, m: t.mov }); if (hist.length > 48) hist.shift();
    if (ultSeq !== null && t.seq !== ultSeq && t.msg) feedback(t.msg);
    ultSeq = t.seq;
    if (calPend && t.seq !== calPend.seq0) { calPend = null; marcarCalibrando(false); }
    render();
  };

  // ───────── escrita (app -> aparelho) ─────────
  const ehDono = () => state.papel !== "convidado";
  const podeEditar = () => ehDono() || (state.perm || []).includes("total");
  function ref() { return db.collection("chocadeiras").doc(state.chocadeiraId); }
  function feedback(txt, tipo) { if (typeof mostrarFeedbackHeader === "function") mostrarFeedbackHeader(txt, tipo || "ok"); }
  function pedirVivo() { if (!state.chocadeiraId) return; ref().update({ "cmd.adxlVivo": String(Date.now()) }).catch(() => {}); }
  function enviarCfg() {
    if (!podeEditar() || !state.chocadeiraId) return;
    const det = $("tp-det").checked ? 1 : 0, tol = Number($("tp-tol").value), sens = Number($("tp-sens").value);
    ref().update({ "cmd.adxlCfg": `${Date.now()}|${det}|${tol}|${sens}` })
      .then(() => { if (typeof registrarAcao === "function") registrarAcao("Automacao", `Sensor da tampa: detecção ${det ? "ligada" : "desligada"}, tolerância ${tol}°, sensibilidade ${sens} mg`); })
      .catch(() => feedback("Não foi possível enviar os ajustes", "error"));
  }
  function agendarCfg() { editandoAte = Date.now() + 6000; clearTimeout(cfgTimer); cfgTimer = setTimeout(enviarCfg, 600); }
  function calibrar(tipo) {
    if (!podeEditar() || !state.chocadeiraId) return;
    if (!T || Date.now() - recebidoEm > 12000) { feedback("Aguarde o aparelho responder ao vivo e tente de novo", "warn"); return; }
    calPend = { seq0: T.seq, ate: Date.now() + 9000 };
    marcarCalibrando(true, tipo);
    ref().update({ "cmd.adxlCal": `${Date.now()}|${tipo}` }).catch(() => { calPend = null; marcarCalibrando(false); feedback("Não foi possível enviar a calibração", "error"); });
  }
  function marcarCalibrando(on, tipo) {
    ["tp-cal-f", "tp-cal-o"].forEach((id) => { const b = $(id); if (b) { b.classList.toggle("busy", on && ((id === "tp-cal-f") === (tipo === "F"))); b.disabled = on; } });
  }

  // ───────── desenho ─────────
  function setArco(el, graus) {   // arco proporcional (0..30°) sobre o semicírculo
    const f = Math.max(0, Math.min(1, graus / 30)); el.style.strokeDasharray = `${(f * ARC).toFixed(1)} ${ARC.toFixed(1)}`;
  }
  function desenharSpark() {
    const c = $("tp-spark"); if (!c) return; const g = c.getContext("2d"), w = c.width, h = c.height;
    g.clearRect(0, 0, w, h);
    g.strokeStyle = "rgba(148,163,184,.14)"; g.lineWidth = 1;
    for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, (h / 4) * i); g.lineTo(w, (h / 4) * i); g.stroke(); }
    if (hist.length < 2) return;
    const vmax = Math.max(40, ...hist.map((x) => x.v), ...hist.map((x) => x.m));
    const linha = (chave, cor, larg) => {
      g.beginPath(); g.lineWidth = larg; g.strokeStyle = cor; g.lineJoin = "round";
      hist.forEach((p, i) => { const x = (i / 47) * (w - 6) + 3, y = h - 4 - (p[chave] / vmax) * (h - 10); i ? g.lineTo(x, y) : g.moveTo(x, y); });
      g.stroke();
    };
    linha("m", "rgba(250,204,21,.9)", 1.6); linha("v", "rgba(56,189,248,.95)", 2);
    const u = hist[hist.length - 1], x = ((hist.length - 1) / 47) * (w - 6) + 3, y = h - 4 - (u.v / vmax) * (h - 10);
    g.fillStyle = "#38bdf8"; g.beginPath(); g.arc(x, y, 3.2, 0, 7); g.fill();
  }
  function render() {
    const hero = $("tp-hero"); if (!hero) return;
    const vivo = T && Date.now() - recebidoEm < 9000;
    $("tp-live").dataset.on = vivo ? "1" : "0";
    $("tp-live-txt").textContent = vivo ? "ao vivo" : (T ? `há ${Math.round((Date.now() - recebidoEm) / 1000)} s` : "aguardando");
    const banner = $("tp-banner");
    const off = typeof state !== "undefined" && state.estadoConexao && state.estadoConexao !== "online";
    banner.hidden = !off && (T || Date.now() - abertaEm < 12000);
    if (!banner.hidden) banner.textContent = off ? "A chocadeira está offline: os valores abaixo são os da última leitura." : "Aguardando o aparelho publicar a telemetria (até ~15 s)…";
    if (!T) return;
    const e = EST[T.est] || EST[0];
    hero.dataset.est = String(T.est); hero.dataset.vivo = vivo ? "1" : "0"; hero.dataset.conf = T.conf ? "1" : "0";
    hero.dataset.tampa = String(T.tampa);                          // 1 = tampa aberta (status do ambiente) → a tampa do desenho levanta
    hero.dataset.vib = (T.vib >= 12 || T.est === 3) ? "1" : "0";   // ondas de vibração ao redor do módulo
    $("tp-chip").textContent = e[0]; $("tp-chip").dataset.cls = e[1];
    const sub = T.est === 4 ? "Módulo fora da posição da tampa." : T.est === 3 ? "O módulo está se movendo… o aparelho confirma em alguns segundos."
      : T.est === 2 ? (T.det ? "Módulo na tampa. Detecção de abertura ligada." : "Módulo na tampa. Detecção de abertura DESLIGADA (só observando).")
      : T.est === 1 ? "Calibre a posição FECHADA para começar." : "Confira 5 V, GND, SDA e SCL do módulo.";
    $("tp-sub").textContent = sub;
    // medidor de posição
    const ang = Math.max(0, T.ang), tol = T.tol;
    setArco($("tp-g-tol"), tol); setArco($("tp-g-val"), Math.min(30, ang));
    $("tp-g-val").dataset.fora = ang > tol ? "1" : "0";
    $("tp-needle").style.transform = `rotate(${(-90 + (Math.min(30, ang) / 30) * 180).toFixed(1)}deg)`;
    $("tp-ang").textContent = T.temRef ? ang.toFixed(1) + "°" : "--";
    // barras
    $("tp-mov-n").textContent = `${T.mov} mg · limite ${T.sens}`;
    $("tp-mov-b").style.width = Math.min(100, (T.mov / 300) * 100) + "%"; $("tp-mov-b").dataset.alto = T.mov > T.sens ? "1" : "0";
    $("tp-mov-m").style.left = Math.min(100, (T.sens / 300) * 100) + "%";
    $("tp-vib-n").textContent = `${T.vib} mg`;
    $("tp-vib-b").style.width = Math.min(100, (T.vib / 60) * 100) + "%";
    $("tp-vib-r").style.left = Math.min(100, (T.vibRef / 60) * 100) + "%"; $("tp-vib-r").hidden = !T.temRef;
    $("tp-vib-a").style.left = Math.min(100, (T.vibAw / 60) * 100) + "%"; $("tp-vib-a").hidden = !T.temAw;
    // pílulas
    const ps = $("tp-pill-saude"); ps.textContent = SAUDE[T.saude] || "sensor ?"; ps.dataset.ok = T.saude === 0 ? "1" : "0";
    const pg = $("tp-pill-sig");
    if (!T.temRef) { pg.textContent = "assinatura: calibre FECHADA"; pg.dataset.ok = "0"; }
    else if (T.conf) { pg.textContent = T.temAw ? `assinatura confiável · sep ${T.sep.toFixed(1)}° · vib ${T.vibRef}/${T.vibAw} mg` : `assinatura confiável · vib ${T.vibRef} mg`; pg.dataset.ok = "1"; }
    else { pg.textContent = T.temAw ? `tampa ≈ mesa (sep ${T.sep.toFixed(1)}° · vib ${T.vibRef}/${T.vibAw} mg): fecha pela temperatura` : "assinatura fraca: calibre FORA (mesa)"; pg.dataset.ok = "0"; }
    $("tp-pill-ign").textContent = `trancos ignorados: ${T.ign}`;
    const pm = $("tp-pill-modo"); pm.textContent = T.auto ? "Status automático" : "Status manual"; pm.dataset.ok = T.auto ? "1" : "0";
    $("tp-xyz").textContent = T.tampa === 1 ? "A caixa está em “tampa aberta” (status do ambiente)." : T.tampa === 2 ? "A caixa está recuperando o ambiente." : "";
    // controles refletem o que o aparelho tem (sem atrapalhar quem está mexendo)
    if (Date.now() > editandoAte) {
      $("tp-det").checked = T.det;
      $("tp-tol").value = T.tol; $("tp-sens").value = T.sens;
      $("tp-tol-n").textContent = T.tol + "°"; $("tp-sens-n").textContent = T.sens + " mg";
    }
    const pode = podeEditar();
    ["tp-det", "tp-tol", "tp-sens"].forEach((id) => { $(id).disabled = !pode; });
    if (!calPend) ["tp-cal-f", "tp-cal-o"].forEach((id) => { $(id).disabled = !pode; });
    $("tp-ctrl-nota").textContent = pode ? "" : "— somente leitura (sem permissão)";
    desenharSpark();
  }
  let abertaEm = 0;

  // ───────── abrir / fechar ─────────
  window.abrirTelaTampa = function () {
    if (!state.chocadeiraId) return;
    aberta = true; abertaEm = Date.now(); base = ultDoc; T = null; hist = []; ultSeq = null; calPend = null;
    showScreen("tampa");
    pedirVivo(); clearInterval(vivoTimer); vivoTimer = setInterval(pedirVivo, 40000);
    clearInterval(tickTimer); tickTimer = setInterval(() => { if (calPend && Date.now() > calPend.ate) { calPend = null; marcarCalibrando(false); feedback("O aparelho não confirmou a calibração (segure o módulo parado e tente de novo)", "warn"); } render(); }, 1000);
    render();
  };
  function fechar() {
    aberta = false; clearInterval(vivoTimer); clearInterval(tickTimer); vivoTimer = tickTimer = null;
    showScreen("dashboard");
  }

  document.addEventListener("DOMContentLoaded", () => {});
  const voltar = $("btn-tampa-voltar"); if (voltar) voltar.addEventListener("click", fechar);
  if ($("tp-det")) {
    $("tp-det").addEventListener("change", () => { editandoAte = Date.now() + 6000; enviarCfg(); });
    $("tp-tol").addEventListener("input", () => { $("tp-tol-n").textContent = $("tp-tol").value + "°"; agendarCfg(); });
    $("tp-sens").addEventListener("input", () => { $("tp-sens-n").textContent = $("tp-sens").value + " mg"; agendarCfg(); });
    $("tp-cal-f").addEventListener("click", () => calibrar("F"));
    $("tp-cal-o").addEventListener("click", () => calibrar("O"));
  }
  // atalho vindo do Configurações/menu: window.abrirTelaTampa()
})();
