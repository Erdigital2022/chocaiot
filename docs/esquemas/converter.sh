#!/bin/bash
# Renderiza os SVGs em PNG com o Chrome headless e recorta no tamanho exato
# (a janela headless sai ~100 px menor que a pedida, por isso renderiza maior e recorta).
cd "$(dirname "$0")"
P="$(pwd -W 2>/dev/null || pwd)"
CH="/c/Program Files/Google/Chrome/Application/chrome.exe"
python gerar_esquemas.py || exit 1
python gerar_pullup.py || exit 1
python gerar_irf520.py || exit 1
python gerar_fan4fios.py || exit 1
python gerar_mhz19c.py || exit 1
python gerar_mhz19c_fios.py || exit 1
python gerar_mcp23017.py || exit 1
for f in "01_ligacao_ADXL345:2560:1740" "02_visao_geral_sistema:2400:1380" "03_pullup_rele:2000:1330" "04_irf520_pwm:2000:1200" "05_fan_4fios_pwm:2000:1240" "06_mhz19c_identificar_fios:2000:1480" "07_mhz19c_fios_sem_marcacao:2000:1380" "08_mcp23017_i2c:2000:1340"; do
  n=${f%%:*}; r=${f#*:}; w=${r%%:*}; h=${r#*:}
  "$CH" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --window-size=$w,$((h+200)) --screenshot="$P/$n.tmp.png" "file:///$P/$n.svg" >/dev/null 2>&1
  python -c "from PIL import Image; im=Image.open('$n.tmp.png'); im.crop((0,0,$w,$h)).save('$n.png'); print('$n', im.size, '->', ($w,$h))"
  rm -f "$n.tmp.png"
done
