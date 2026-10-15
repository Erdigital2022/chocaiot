# Galeria "Fases do pintinho" e wallpaper

O firmware e o app possuem uma galeria de imagens do desenvolvimento do embrião (um item do *Guia de incubação*)
e um papel de parede para o screensaver. **As imagens originais não acompanham o repositório** (origem na internet,
sem licença conhecida para redistribuir). Use as **suas**.

## Galeria (aparelho e app)

1. Coloque suas imagens em `pwa/guia/fases/` (jpg/png/webp), nomeadas com o número do dia — ex.: `dia-01.jpg`.
2. Regenere a lista do app e o conteúdo do guia:
   ```bash
   node firmware/tools/gerar_guia.js
   ```
3. Converta para o aparelho (JPEG **baseline** 320×240 — o decoder do firmware não lê JPEG progressivo):
   ```bash
   pip install pillow
   python firmware/tools/gerar_fases.py     # gera firmware/src/fases_data.h
   ```
4. Recompile e grave. (Sem imagens, o repositório usa um único marcador de posição para compilar.)

## Wallpaper do screensaver

1. Salve a imagem em `firmware/tools/wallpaper_src.jpg`.
2. `python firmware/tools/gerar_wallpaper.py 0.30` (o número é o brilho: 0,30 = escurecido a 30 %).
3. Recompile. Também é possível definir wallpaper **pelo cartão SD** na tela *Arquivos → Definir como wallpaper*
   e ajustar o escurecimento em *Tela*.
