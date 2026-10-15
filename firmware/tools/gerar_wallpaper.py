# Gera o wallpaper do screensaver: tools/wallpaper_src.jpg -> src/wallpaper_data.h
# A "opacidade" (escurecimento) e aplicada AQUI, no arquivo: mistura com preto,
# entao no firmware o wallpaper ja e desenhado escuro (sem custo em runtime).
# JPEG BASELINE 320x240 (TJpg_Decoder nao le progressivo).
# Uso: python tools/gerar_wallpaper.py [brilho 0..1, padrao 1.0 -- o escurecimento agora e aplicado no firmware (Config. > Tela)]   (requer pillow)
import io, os, sys
from PIL import Image
BRILHO = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
HERE = os.path.dirname(os.path.abspath(__file__))
im = Image.open(os.path.join(HERE, "wallpaper_src.jpg")).convert("RGB").resize((320, 240), Image.LANCZOS)
im = Image.blend(Image.new("RGB", im.size, (4, 4, 12)), im, BRILHO)   # opacidade sobre um fundo quase preto
buf = io.BytesIO(); im.save(buf, "JPEG", quality=85, progressive=False)
data = buf.getvalue()
im.save(os.path.join(HERE, "wallpaper_fw.jpg"), "JPEG", quality=85, progressive=False)
lines = ["// GERADO por tools/gerar_wallpaper.py -- nao editar a mao.", "#pragma once", "#include <stdint.h>",
         "static const uint8_t WALLPAPER_JPG[] = {"]
for k in range(0, len(data), 24):
    lines.append("  " + ",".join("0x%02x" % b for b in data[k:k+24]) + ",")
lines += ["};", "static const uint32_t WALLPAPER_JPG_LEN = sizeof(WALLPAPER_JPG);"]
open(os.path.join(HERE, "..", "src", "wallpaper_data.h"), "w").write("\n".join(lines) + "\n")
print("OK: %d bytes, brilho %.2f" % (len(data), BRILHO))
