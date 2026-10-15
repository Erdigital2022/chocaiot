// GERADO por firmware/tools/gerar_guia.js -- nao editar a mao.
window.GUIA_TOPICOS = [
  {
    "icone": "📦",
    "titulo": "Seleção e armazenamento",
    "resumo": "Até 7 dias, 13-18 °C, ponta fina p/ baixo",
    "corpo": [
      "# Escolha dos ovos",
      "- Use ovos férteis, limpos, de casca íntegra e formato normal (50 a 65 g). Descarte trincados, deformados, muito grandes ou muito pequenos.",
      "- Não lave os ovos: a lavagem remove a cutícula, camada que protege contra bactérias. Sujeira leve sai a seco.",
      "# Efeitos do armazenamento",
      "- Ideal: incubar em até 7 dias. Depois disso a taxa de eclosão cai; com 10 a 14 dias cai muito e os pintinhos nascem atrasados e mais fracos.",
      "- Guarde a 13-18 °C (ideal 15 °C) e umidade de 75 a 80 %. Abaixo de 10 °C ou perto de 25 °C o embrião é prejudicado. Nunca na geladeira.",
      "- Posição: ponta fina para baixo (ou deitados). Passou de 7 dias? Incline a bandeja 1 vez ao dia.",
      "- Longe de sol, corrente de ar e odores fortes; manuseie com cuidado, sem sacudir."
    ]
  },
  {
    "icone": "🌡️",
    "titulo": "Preaquecimento",
    "resumo": "24-27 °C por 6 a 12 h antes de incubar",
    "corpo": [
      "# O que fazer",
      "- Tire os ovos do local de armazenamento e deixe-os em ambiente de 24 a 27 °C por 6 a 12 horas antes de colocá-los na incubadora.",
      "- Deixe em bandeja, ponta fina para baixo, sem encostar um no outro e sem sol direto ou vento.",
      "# Por que fazer",
      "- Evita o choque térmico ao passar de ~15 °C para 37,5 °C.",
      "- Evita a condensação (\"suor\") na casca, que favorece contaminação e prejudica a troca de gases.",
      "- O embrião \"acorda\" aos poucos, o que melhora a eclosão.",
      "# Enquanto isso",
      "- Ligue a incubadora vazia e deixe temperatura e umidade estabilizarem (veja o passo 3)."
    ]
  },
  {
    "icone": "🥚",
    "titulo": "Momento de incubação",
    "resumo": "Dia 0: ovos na máquina, tudo já estável",
    "corpo": [
      "# Antes de colocar os ovos",
      "- Incubadora ligada e estável há 12 a 24 h: 37,5 °C e umidade de 50 a 55 %. Confirme no display ou no PWA.",
      "- Máquina limpa e desinfetada, reservatório de água cheio.",
      "# Colocando os ovos",
      "- Todos de uma vez (mesmo lote), com a ponta fina para baixo ou deitados levemente inclinados. Nunca ponta fina para cima.",
      "- Marque cada ovo a lápis (X de um lado, O do outro) para conferir a viragem.",
      "- Anote a data. Galinha: 21 dias (codorna 17-18, pata 28, ganso 28-30).",
      "# No ChocaIoT",
      "- Toque em \"Iniciar Chocadeira\" (PWA) ou no card Ciclo (display) para iniciar a contagem dos dias. A duração é ajustável de 15 a 35 dias."
    ]
  },
  {
    "icone": "⚙️",
    "titulo": "Operação da incubadora",
    "resumo": "Rotina diária, ventilação e higiene",
    "corpo": [
      "# Local e rotina",
      "- Instale em local estável, longe de sol, corrente de ar e vibração, com ambiente de 20 a 25 °C.",
      "- Confira temperatura, umidade e viragem 2 vezes ao dia. Abra o mínimo possível: cada abertura perde calor e umidade.",
      "- Mantenha o reservatório de água sempre com nível, com água limpa.",
      "# Ventilação",
      "- O embrião consome oxigênio e libera CO2. Não tampe as entradas de ar; a necessidade aumenta perto da eclosão.",
      "# Imprevistos",
      "- Falta de energia: não abra a incubadora. Fechada, ela mantém o calor por algum tempo e quedas curtas costumam ser toleradas.",
      "- Limpe e desinfete a máquina antes de cada ciclo.",
      "# Atuadores",
      "- Resistência aquece; umidificador e bomba de água controlam a umidade; bomba de ar ventila. O \"Cooler Sistema\" é automático e só resfria a eletrônica."
    ]
  },
  {
    "icone": "🔥",
    "titulo": "Controle de temperatura",
    "resumo": "37,5 °C estável, tolerância de ±0,5 °C",
    "corpo": [
      "# Valores de referência",
      "- Ventilação forçada: 37,5 °C, medidos na altura dos ovos.",
      "- Ar parado (sem ventilador): 38,5 a 39 °C, medidos no topo dos ovos.",
      "- Faixa aceitável: 37,2 a 37,8 °C. Pequenas variações de ±0,5 °C por pouco tempo são toleradas.",
      "# Cuidados",
      "- Calor é pior que frio: 1 °C acima por várias horas causa malformação e mortes; acima de 40 °C por tempo prolongado é letal.",
      "- Frio prolongado atrasa a eclosão e enfraquece os pintinhos.",
      "- Confira o sensor com um termômetro de referência pelo menos 1 vez por ciclo.",
      "# No ChocaIoT",
      "- Os alertas usam a faixa mínima/máxima do TIPO do ciclo ativo (ex.: Galinha 37,2 a 37,8 °C). Ajuste em Configurações > Ovos (display) ou Tipos de ovos (PWA)."
    ]
  },
  {
    "icone": "💧",
    "titulo": "Umidade",
    "resumo": "45-55 % até o dia 17; 65-70 % na eclosão",
    "corpo": [
      "# Faixas por fase",
      "- Dia 1 ao 17: 45 a 55 % (alvo 50 %).",
      "- Dia 18 ao 21 (eclosão): 65 a 70 %, até 75 %. Amolece a casca e ajuda o pintinho a sair.",
      "# Sinais de problema",
      "- Umidade alta demais: pintinhos grandes, molhados e atrasados.",
      "- Umidade baixa demais: casca dura, pintinho grudado na casca, desidratado.",
      "- Os ovos perdem 11 a 13 % do peso até o dia 18; a câmara de ar aumenta. A ovoscopia mostra isso.",
      "# Como ajustar",
      "- Aumente ou reduza a área de água e a ventilação; o umidificador e a bomba de água ajudam.",
      "- Na eclosão, não abra a incubadora para \"conferir\": a umidade despenca."
    ]
  },
  {
    "icone": "🔄",
    "titulo": "Viragem",
    "resumo": "3 a 5 vezes por dia, até o dia 17",
    "corpo": [
      "# Como virar",
      "- No mínimo 3 vezes ao dia, ideal 5 ou mais (automática: a cada 1 a 2 horas), em intervalos regulares.",
      "- Incline cerca de 45° para cada lado (90° no total) ou gire 180° se for manual.",
      "# Quando",
      "- Do dia 1 ao dia 17. Pare no dia 18, na transferência.",
      "- Sem viragem o embrião gruda na membrana da casca e morre. Nos 3 primeiros dias ele é muito frágil: sem sacudir.",
      "# Manual",
      "- Lave as mãos, gire com suavidade e confira as marcas X e O.",
      "# No ChocaIoT",
      "- O card \"Viragem\" ainda está em desenvolvimento (aguardando o mecanismo de viragem automática)."
    ]
  },
  {
    "icone": "🔦",
    "titulo": "Ovoscopia (dias 7, 14, 18)",
    "resumo": "Retire ovos inférteis e embriões mortos",
    "corpo": [
      "# O que é",
      "- Iluminar o ovo no escuro para ver o interior. Use luz LED fria (não esquenta) e mãos limpas.",
      "# Dia 7 (até o 10)",
      "- Fértil: rede de vasos sanguíneos e ponto escuro (o embrião).",
      "- Ovo claro: infértil. Anel ou mancha de sangue: embrião morto. Retire, pois pode estourar e contaminar os outros.",
      "# Dia 14",
      "- O embrião ocupa quase todo o ovo e a câmara de ar está maior.",
      "# Dia 18",
      "- Última checagem, junto da transferência. Retire ovos sem movimento ou sem veias.",
      "# Cuidados",
      "- Rápido: menos de 1 minuto por ovo e menos de 30 minutos por sessão. Não deixe os ovos esfriarem."
    ]
  },
  {
    "icone": "📥",
    "titulo": "Transferência (dia 18-19)",
    "resumo": "Pare de virar, deite e suba a umidade",
    "corpo": [
      "# O que mudar",
      "- Pare a viragem no dia 18 (ou 19).",
      "- Deite os ovos de lado na cesta de eclosão ou no fundo da incubadora, sem que se encostem.",
      "- Suba a umidade para 65 a 70 % e mantenha 37,2 a 37,5 °C: os pintinhos já geram calor próprio.",
      "- Aumente a ventilação, sem abrir a máquina.",
      "# Como fazer",
      "- Faça a transferência rápido (1 a 2 minutos), com mãos limpas e sem virar os ovos de cabeça para baixo.",
      "- Se a sua incubadora tem bandeja única, apenas pare a rotação e deite os ovos.",
      "# Depois",
      "- Não abra mais até o fim da eclosão. Nos dias 19-20 já dá para ouvir os piados dentro do ovo."
    ]
  },
  {
    "icone": "🐣",
    "titulo": "Retirada do pintinho",
    "resumo": "Dia 21: espere secar antes de retirar",
    "corpo": [
      "# A eclosão",
      "- Dia 19-20: bicagem interna (piados); depois surge o primeiro furinho na casca. Da bicada até sair leva de 12 a 24 horas.",
      "- Não ajude o pintinho: puxar a casca causa sangramento. E não abra a incubadora.",
      "# Retirada",
      "- Deixe secar por completo dentro da incubadora (12 a 24 h), até ficar fofo e ativo. Ele vive 48 h com a reserva da gema, sem comer.",
      "- Retire quando a maioria tiver nascido. Ovos que não eclodiram até o dia 23 podem ser descartados.",
      "# Criadeira",
      "- Leve à criadeira já aquecida a 32-35 °C (33-35 °C na 1ª semana, reduzindo 3 °C por semana), com água e ração inicial.",
      "- Molhe o bico de alguns pintinhos para mostrar a água.",
      "# Depois",
      "- Limpe e desinfete a incubadora, anote a taxa de eclosão e toque em \"Parar ciclo\" no ChocaIoT."
    ]
  },
  {
    "icone": "🖼️",
    "titulo": "Fases do Pintinho",
    "resumo": "Galeria: desenvolvimento do dia 1 ao 21",
    "galeria": true,
    "corpo": []
  }
];
window.GUIA_FASES = [
  "guia/fases/Egg-Embryo-Development-Day-1.jpg",
  "guia/fases/Egg-Embryo-Development-Day-2.jpg",
  "guia/fases/Egg-Embryo-Development-Day-3.jpg",
  "guia/fases/Egg-Embryo-Development-Day-4.jpg",
  "guia/fases/Egg-Embryo-Development-Day-5.jpg",
  "guia/fases/Egg-Embryo-Development-Day-6.jpg",
  "guia/fases/Egg-Embryo-Development-Day-7.jpg",
  "guia/fases/Egg-Embryo-Development-Day-8.jpg",
  "guia/fases/Egg-Embryo-Development-Day-9.jpg",
  "guia/fases/Egg-Embryo-Development-day-10.jpg",
  "guia/fases/Egg-Embryo-Development-Day-11.jpg",
  "guia/fases/Egg-Embryo-Development-Day-12.jpg",
  "guia/fases/Egg-Embryo-Development-Day-13.jpg",
  "guia/fases/Egg-Embryo-Development-Day-14.jpg",
  "guia/fases/Egg-Embryo-Development-Day-15.jpg",
  "guia/fases/Egg-Embryo-Development-Day-16.jpg",
  "guia/fases/Egg-Embryo-Development-Day-17.jpg",
  "guia/fases/Egg-Embryo-Development-Day-18.jpg",
  "guia/fases/Egg-Embryo-Development-Day-19.jpg",
  "guia/fases/Egg-Embryo-Development-Day-21.jpg"
];
