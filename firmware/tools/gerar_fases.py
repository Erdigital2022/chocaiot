# Converte as imagens da galeria "Fases do Pintinho" pro firmware:
#  - JPEG BASELINE (TJpg_Decoder nao le progressivo), 320x240 com barras pretas
#    (a imagem do dia 21 e retrato 240x320 -> encaixa por altura, centralizada)
#  - gera src/fases_data.h (arrays em flash) e tools/fases_fw/*.jpg (conferencia)
# Uso: python tools/gerar_fases.py   (requer: pip install pillow)
import os, re, io
from PIL import Image
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pwa", "guia", "fases")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "src", "fases_data.h")
OUT_DIR = os.path.join(HERE, "fases_fw")
os.makedirs(OUT_DIR, exist_ok=True)

def dia(f):
    m = re.search(r"(\d+)\.\w+$", f); return int(m.group(1)) if m else 0
files = sorted([f for f in os.listdir(SRC) if re.search(r"\.(jpe?g|png|webp)$", f, re.I)], key=lambda f: (dia(f), f.lower()))

h = ["// GERADO por tools/gerar_fases.py -- nao editar a mao.", "#pragma once", "#include <stdint.h>"]
tabela = []
total = 0
for i, f in enumerate(files):
    im = Image.open(os.path.join(SRC, f)).convert("RGB")
    im.thumbnail((320, 240), Image.LANCZOS)
    canvas = Image.new("RGB", (320, 240), (0, 0, 0))
    canvas.paste(im, ((320 - im.width) // 2, (240 - im.height) // 2))
    buf = io.BytesIO()
    canvas.save(buf, "JPEG", quality=82, progressive=False, optimize=False)
    data = buf.getvalue(); total += len(data)
    open(os.path.join(OUT_DIR, "%02d_%s" % (i, f)), "wb").write(data)
    h.append("static const uint8_t FASE_%d[] = {" % i)
    for k in range(0, len(data), 24):
        h.append("  " + ",".join("0x%02x" % b for b in data[k:k+24]) + ",")
    h.append("};")
    tabela.append("  { FASE_%d, sizeof(FASE_%d) }," % (i, i))
h.append("struct FaseImg { const uint8_t* dados; uint32_t tam; };")
h.append("static const FaseImg FASES[] = {"); h += tabela; h.append("};")
h.append("static const int FASES_N = sizeof(FASES)/sizeof(FASES[0]);")
open(OUT_H, "w").write("\n".join(h) + "\n")
print("OK:", len(files), "imagens,", total // 1024, "KB")
