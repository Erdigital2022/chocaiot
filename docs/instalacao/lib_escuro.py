# -*- coding: utf-8 -*-
"""Biblioteca minima de desenho SVG em TEMA ESCURO para os guias de instalacao do ChocaIoT."""
import io, os, subprocess, textwrap, math

FONT = "Segoe UI, Roboto, Arial, sans-serif"
BG, CARD, CARD2, LINE = "#0b1020", "#121a30", "#18213d", "#2a3558"
TXT, MUT, DIM = "#e6ebff", "#8da0c8", "#5b6b92"
HEAT, WATER, AIR, PWR = "#ff7a45", "#38bdf8", "#67e8f9", "#f43f5e"
SIG, V5, OK, BAD, VIO, GOLD = "#a3e635", "#fbbf24", "#34d399", "#f87171", "#a78bfa", "#d9a441"


class Tela:
    def __init__(self, w=1800, h=1100, titulo="", sub="", rodape="ChocaIoT · guia de instalação · tema escuro"):
        self.w, self.h, self.p = w, h, []
        self.p.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">')
        self.p.append(
            "<defs>"
            f'<linearGradient id="gfundo" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0d1430"/><stop offset="1" stop-color="{BG}"/></linearGradient>'
            f'<linearGradient id="gcalor" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffb199"/><stop offset="1" stop-color="{HEAT}"/></linearGradient>'
            f'<linearGradient id="gagua" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7dd3fc"/><stop offset="1" stop-color="#0369a1"/></linearGradient>'
            f'<linearGradient id="gouro" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f6d27a"/><stop offset="1" stop-color="#a87a1e"/></linearGradient>'
            f'<filter id="brilho" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="6"/></filter>'
            '<marker id="seta" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="context-stroke"/></marker>'
            "</defs>")
        self.p.append(f'<rect width="100%" height="100%" fill="url(#gfundo)"/>')
        if titulo:
            self.texto(50, 66, titulo, 38, TXT, "700")
        if sub:
            self.texto(50, 102, sub, 19, MUT)
        self.texto(w - 50, h - 24, rodape, 14, DIM, anchor="end")

    # ── primitivas ──
    def add(self, s): self.p.append(s)

    def texto(self, x, y, t, size=18, fill=TXT, weight="400", anchor="start", italic=False):
        t = str(t).replace("&", "&amp;").replace("<", "&lt;")
        st = ' font-style="italic"' if italic else ""
        self.p.append(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{st}>{t}</text>')

    def linhas(self, x, y, arr, size=17, fill=TXT, gap=None, weight="400"):
        gap = gap or int(size * 1.4)
        for i, t in enumerate(arr):
            self.texto(x, y + i * gap, t, size, fill, weight)
        return y + len(arr) * gap

    def par(self, x, y, t, largura_px, size=17, fill=TXT, weight="400", gap=None):
        """paragrafo com quebra automatica (aproximada) em largura_px; devolve o proximo y"""
        n = max(10, int(largura_px / (size * 0.52)))
        return self.linhas(x, y, textwrap.wrap(t, n), size, fill, gap, weight)

    def rect(self, x, y, w, h, fill=CARD, stroke=LINE, sw=2, rx=12, dash=None, op=1.0):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        o = f' opacity="{op}"' if op != 1.0 else ""
        self.p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}{o}/>')

    def linha(self, x1, y1, x2, y2, cor=LINE, sw=3, dash=None, cap="round"):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.p.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{cor}" stroke-width="{sw}" stroke-linecap="{cap}"{d}/>')

    def caminho(self, pts, cor=LINE, sw=3, dash=None, seta=False, fill="none"):
        d = "M" + " L".join(f"{x},{y}" for x, y in pts)
        da = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#seta)"' if seta else ""
        self.p.append(f'<path d="{d}" fill="{fill}" stroke="{cor}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{da}{m}/>')

    def seta(self, x1, y1, x2, y2, cor=AIR, sw=4, dash=None):
        self.caminho([(x1, y1), (x2, y2)], cor, sw, dash, seta=True)

    def circulo(self, cx, cy, r, fill=CARD2, stroke=LINE, sw=2):
        self.p.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def elipse(self, cx, cy, rx, ry, fill=CARD2, stroke=LINE, sw=2):
        self.p.append(f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def poligono(self, pts, fill=CARD2, stroke=LINE, sw=2):
        self.p.append('<polygon points="' + " ".join(f"{x},{y}" for x, y in pts) + f'" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round"/>')

    def brilho(self, cx, cy, r, cor):
        self.p.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{cor}" opacity="0.45" filter="url(#brilho)"/>')

    # ── componentes ──
    def num(self, x, y, n, cor=V5):
        self.circulo(x, y, 17, cor, BG, 3)
        self.texto(x, y + 6, str(n), 18, BG, "800", "middle")

    def painel(self, x, y, w, h, titulo, cor=MUT):
        self.rect(x, y, w, h, CARD, LINE, 2, 16)
        self.texto(x + 22, y + 38, titulo, 21, cor, "700")
        self.linha(x + 22, y + 52, x + w - 22, y + 52, LINE, 2)

    def etiqueta(self, x, y, t, cor=OK, size=15, pad=10):
        w = int(len(t) * size * 0.56) + 2 * pad
        self.rect(x, y - size - 4, w, size + 14, cor, cor, 1, 10, op=0.18)
        self.rect(x, y - size - 4, w, size + 14, "none", cor, 2, 10)
        self.texto(x + pad, y + 3, t, size, cor, "700")
        return w

    def passos(self, x, y, itens, largura, cor=V5, size=17):
        """lista numerada com quebra; devolve y final"""
        for i, t in enumerate(itens, 1):
            self.num(x + 16, y - 6, i, cor)
            y = self.par(x + 46, y, t, largura - 46, size) + 12
        return y

    def faz_nao_faz(self, x, y, w, faz, nao, size=16):
        self.rect(x, y, w // 2 - 8, 40 + 26 * len(faz) * 2, "#0f2a22", OK, 2, 12)
        self.texto(x + 16, y + 28, "✔ FAÇA", 18, OK, "700")
        yy = y + 56
        for t in faz:
            yy = self.par(x + 16, yy, t, w // 2 - 48, size) + 6
        self.rect(x + w // 2 + 8, y, w // 2 - 8, 40 + 26 * len(nao) * 2, "#2b1216", BAD, 2, 12)
        self.texto(x + w // 2 + 24, y + 28, "✖ NÃO FAÇA", 18, BAD, "700")
        yy2 = y + 56
        for t in nao:
            yy2 = self.par(x + w // 2 + 24, yy2, t, w // 2 - 48, size) + 6
        return max(yy, yy2)

    # ── icones ──
    def lampada(self, x, y, w=220):
        self.rect(x, y, w, 26, "#2a2f45", "#3a4a7a", 2, 6)
        self.brilho(x + w / 2, y + 40, 60, HEAT)
        self.rect(x + 14, y + 26, w - 28, 22, "url(#gcalor)", HEAT, 2, 8)
        for i in range(5):
            xx = x + 30 + i * (w - 60) / 4
            self.seta(xx, y + 56, xx, y + 100, HEAT, 3, "5 6")

    def fan(self, cx, cy, r=34, cor=AIR):
        self.rect(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, CARD2, "#4a5a8a", 3, 10)
        self.circulo(cx, cy, r, "#0f1730", "#3a4a7a", 2)
        for k in range(7):
            a = math.radians(k * 360 / 7)
            self.poligono([(cx, cy), (cx + r * 0.95 * math.cos(a - 0.2), cy + r * 0.95 * math.sin(a - 0.2)),
                           (cx + r * 0.9 * math.cos(a + 0.45), cy + r * 0.9 * math.sin(a + 0.45))], cor, "#0b1020", 1)
        self.circulo(cx, cy, r * 0.22, "#243053", "#8da0c8", 2)

    def ovos(self, x, y, n=7, passo=52):
        for i in range(n):
            self.elipse(x + i * passo, y, 19, 25, "#f1dfbb", "#b79b62", 2)

    def sonda(self, x, y, cor=OK):
        self.rect(x - 8, y - 34, 16, 68, "#c9d3ee", "#7d8fb8", 2, 8)
        self.circulo(x, y + 38, 9, cor, BG, 2)

    def co2(self, x, y):
        self.rect(x, y, 76, 52, "url(#gouro)", "#7a5b14", 2, 6)
        self.rect(x + 8, y + 30, 36, 18, "#f5f1e6", "#9a8f70", 1, 3)
        self.rect(x + 76, y + 8, 14, 36, "#e9e6dc", "#8d8774", 1, 3)

    def jarra(self, x, y, w=170, h=200, nivel=0.72):
        self.rect(x, y, w, h, "#0f1730", "#6b86c9", 3, 14)
        nh = h * nivel
        self.rect(x + 3, y + h - nh, w - 6, nh - 3, "url(#gagua)", "none", 0, 10, op=0.9)
        self.rect(x - 8, y - 12, w + 16, 16, "#2a3558", "#6b86c9", 2, 6)

    def esp32(self, x, y, w=240, h=120, rot="ESP32-S3"):
        self.rect(x, y, w, h, "#10261f", "#1fbf8e", 3, 10)
        self.rect(x + 18, y + 14, 90, 52, "#0b1020", "#5eead4", 2, 6)
        self.texto(x + 63, y + 46, "S3", 20, "#5eead4", "700", "middle")
        for i in range(10):
            self.rect(x + 14 + i * 22, y + h - 18, 10, 12, GOLD, "none", 0, 2)
        self.texto(x + w - 14, y + 32, rot, 17, "#5eead4", "700", "end")

    def rele(self, x, y, w=300, h=110, canais=8):
        self.rect(x, y, w, h, "#2b0f19", "#b91c4a", 3, 10)
        cw = (w - 20) / canais
        for i in range(canais):
            xx = x + 10 + i * cw
            self.rect(xx + 2, y + 14, cw - 8, 44, "#1e2540", "#4a5a8a", 2, 4)
            self.texto(xx + cw / 2 - 2, y + 44, str(i + 1), 14, TXT, "700", "middle")
            self.circulo(xx + cw / 2 - 2, y + 80, 5, OK, BG, 1)

    # ── saida ──
    def salvar(self, caminho_svg):
        self.p.append("</svg>")
        with io.open(caminho_svg, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(self.p))


def svg_para_png(svg, png, w, h):
    chrome = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
    tmp = png + ".tmp.png"
    url = "file:///" + os.path.abspath(svg).replace("\\", "/")
    subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    f"--window-size={w},{h + 200}", f"--screenshot={tmp}", url], capture_output=True, timeout=120)
    from PIL import Image
    Image.open(tmp).crop((0, 0, w, h)).save(png, optimize=True)
    os.remove(tmp)
