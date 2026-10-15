// Gera o conteudo do Guia a partir de guia_content.json (fonte unica):
//   - PWA:      frontend/guia-data.js  (UTF-8, com acentos; inclui a lista ordenada das imagens da galeria)
//   - Firmware: src/guia_data.h        (ASCII puro -- a fonte do TFT nao tem acentos)
// As imagens da galeria do firmware sao geradas a parte: python tools/gerar_fases.py
// Uso: node tools/gerar_guia.js
const fs = require("fs");
const path = require("path");

const src = JSON.parse(fs.readFileSync(path.join(__dirname, "guia_content.json"), "utf8"));
const PWA_OUT = require("path").join(__dirname, "..", "..", "pwa", "guia-data.js");
const FW_OUT = path.join(__dirname, "..", "src", "guia_data.h");
const FASES_DIR = require("path").join(__dirname, "..", "..", "pwa", "guia", "fases");

function ascii(s) {
  const r = s
    .replace(/[–—]/g, "-")
    .replace(/°C/g, "C")
    .replace(/°/g, " graus")
    .replace(/±/g, "+/-")
    .replace(/×/g, "x")
    .replace(/[“”]/g, '"')
    .normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "");
  for (const ch of r) if (ch.charCodeAt(0) > 126) throw new Error("Char nao-ASCII: " + ch + " em: " + s);
  return r;
}
const cstr = (s) =>
  '"' + s.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n") + '"';

// Galeria "Fases do Pintinho": ordem natural pelo numero do dia no nome
// (Day-2 antes de Day-10; independe de maiuscula/minuscula).
const diaDe = (f) => parseInt((f.match(/(\d+)\.\w+$/) || [0, 0])[1], 10);
const fases = fs.readdirSync(FASES_DIR)
  .filter((f) => /\.(jpe?g|png|webp)$/i.test(f))
  .sort((a, b) => diaDe(a) - diaDe(b) || a.localeCompare(b))
  .map((f) => "guia/fases/" + f);

// PWA
fs.writeFileSync(
  PWA_OUT,
  "// GERADO por firmware/tools/gerar_guia.js -- nao editar a mao.\n" +
    "window.GUIA_TOPICOS = " + JSON.stringify(src, null, 2) + ";\n" +
    "window.GUIA_FASES = " + JSON.stringify(fases, null, 2) + ";\n"
);

// Firmware
let h = "// GERADO por tools/gerar_guia.js a partir de tools/guia_content.json -- nao editar a mao.\n";
h += "// Marcacao do corpo: \"# \" = subtitulo, \"- \" = item, linhas separadas por \\n.\n#pragma once\n";
h += "struct GuiaTopico { const char* titulo; const char* resumo; const char* corpo; bool galeria; };\n";
h += "static const GuiaTopico GUIA_TOPICOS[] = {\n";
src.forEach((t) => {
  const titulo = ascii(t.titulo), resumo = ascii(t.resumo);
  if (resumo.length > 40) throw new Error("Resumo > 40 chars (" + resumo.length + "): " + resumo);
  if (titulo.length > 38) throw new Error("Titulo > 38 chars: " + titulo);
  const corpo = t.corpo.length
    ? t.corpo.map((l) => "    " + cstr(ascii(l) + "\n")).join("\n")
    : '    ""';
  h += "  { " + cstr(titulo) + ",\n    " + cstr(resumo) + ",\n" + corpo + ",\n    " + (t.galeria ? "true" : "false") + " },\n";
});
h += "};\nstatic const int GUIA_N = sizeof(GUIA_TOPICOS)/sizeof(GUIA_TOPICOS[0]);\n";
fs.writeFileSync(FW_OUT, h);
console.log("OK:", src.length, "topicos,", fases.length, "fases");
