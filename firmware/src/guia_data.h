// GERADO por tools/gerar_guia.js a partir de tools/guia_content.json -- nao editar a mao.
// Marcacao do corpo: "# " = subtitulo, "- " = item, linhas separadas por \n.
#pragma once
struct GuiaTopico { const char* titulo; const char* resumo; const char* corpo; bool galeria; };
static const GuiaTopico GUIA_TOPICOS[] = {
  { "Selecao e armazenamento",
    "Ate 7 dias, 13-18 C, ponta fina p/ baixo",
    "# Escolha dos ovos\n"
    "- Use ovos ferteis, limpos, de casca integra e formato normal (50 a 65 g). Descarte trincados, deformados, muito grandes ou muito pequenos.\n"
    "- Nao lave os ovos: a lavagem remove a cuticula, camada que protege contra bacterias. Sujeira leve sai a seco.\n"
    "# Efeitos do armazenamento\n"
    "- Ideal: incubar em ate 7 dias. Depois disso a taxa de eclosao cai; com 10 a 14 dias cai muito e os pintinhos nascem atrasados e mais fracos.\n"
    "- Guarde a 13-18 C (ideal 15 C) e umidade de 75 a 80 %. Abaixo de 10 C ou perto de 25 C o embriao e prejudicado. Nunca na geladeira.\n"
    "- Posicao: ponta fina para baixo (ou deitados). Passou de 7 dias? Incline a bandeja 1 vez ao dia.\n"
    "- Longe de sol, corrente de ar e odores fortes; manuseie com cuidado, sem sacudir.\n",
    false },
  { "Preaquecimento",
    "24-27 C por 6 a 12 h antes de incubar",
    "# O que fazer\n"
    "- Tire os ovos do local de armazenamento e deixe-os em ambiente de 24 a 27 C por 6 a 12 horas antes de coloca-los na incubadora.\n"
    "- Deixe em bandeja, ponta fina para baixo, sem encostar um no outro e sem sol direto ou vento.\n"
    "# Por que fazer\n"
    "- Evita o choque termico ao passar de ~15 C para 37,5 C.\n"
    "- Evita a condensacao (\"suor\") na casca, que favorece contaminacao e prejudica a troca de gases.\n"
    "- O embriao \"acorda\" aos poucos, o que melhora a eclosao.\n"
    "# Enquanto isso\n"
    "- Ligue a incubadora vazia e deixe temperatura e umidade estabilizarem (veja o passo 3).\n",
    false },
  { "Momento de incubacao",
    "Dia 0: ovos na maquina, tudo ja estavel",
    "# Antes de colocar os ovos\n"
    "- Incubadora ligada e estavel ha 12 a 24 h: 37,5 C e umidade de 50 a 55 %. Confirme no display ou no PWA.\n"
    "- Maquina limpa e desinfetada, reservatorio de agua cheio.\n"
    "# Colocando os ovos\n"
    "- Todos de uma vez (mesmo lote), com a ponta fina para baixo ou deitados levemente inclinados. Nunca ponta fina para cima.\n"
    "- Marque cada ovo a lapis (X de um lado, O do outro) para conferir a viragem.\n"
    "- Anote a data. Galinha: 21 dias (codorna 17-18, pata 28, ganso 28-30).\n"
    "# No ChocaIoT\n"
    "- Toque em \"Iniciar Chocadeira\" (PWA) ou no card Ciclo (display) para iniciar a contagem dos dias. A duracao e ajustavel de 15 a 35 dias.\n",
    false },
  { "Operacao da incubadora",
    "Rotina diaria, ventilacao e higiene",
    "# Local e rotina\n"
    "- Instale em local estavel, longe de sol, corrente de ar e vibracao, com ambiente de 20 a 25 C.\n"
    "- Confira temperatura, umidade e viragem 2 vezes ao dia. Abra o minimo possivel: cada abertura perde calor e umidade.\n"
    "- Mantenha o reservatorio de agua sempre com nivel, com agua limpa.\n"
    "# Ventilacao\n"
    "- O embriao consome oxigenio e libera CO2. Nao tampe as entradas de ar; a necessidade aumenta perto da eclosao.\n"
    "# Imprevistos\n"
    "- Falta de energia: nao abra a incubadora. Fechada, ela mantem o calor por algum tempo e quedas curtas costumam ser toleradas.\n"
    "- Limpe e desinfete a maquina antes de cada ciclo.\n"
    "# Atuadores\n"
    "- Resistencia aquece; umidificador e bomba de agua controlam a umidade; bomba de ar ventila. O \"Cooler Sistema\" e automatico e so resfria a eletronica.\n",
    false },
  { "Controle de temperatura",
    "37,5 C estavel, tolerancia de +/-0,5 C",
    "# Valores de referencia\n"
    "- Ventilacao forcada: 37,5 C, medidos na altura dos ovos.\n"
    "- Ar parado (sem ventilador): 38,5 a 39 C, medidos no topo dos ovos.\n"
    "- Faixa aceitavel: 37,2 a 37,8 C. Pequenas variacoes de +/-0,5 C por pouco tempo sao toleradas.\n"
    "# Cuidados\n"
    "- Calor e pior que frio: 1 C acima por varias horas causa malformacao e mortes; acima de 40 C por tempo prolongado e letal.\n"
    "- Frio prolongado atrasa a eclosao e enfraquece os pintinhos.\n"
    "- Confira o sensor com um termometro de referencia pelo menos 1 vez por ciclo.\n"
    "# No ChocaIoT\n"
    "- Os alertas usam a faixa minima/maxima do TIPO do ciclo ativo (ex.: Galinha 37,2 a 37,8 C). Ajuste em Configuracoes > Ovos (display) ou Tipos de ovos (PWA).\n",
    false },
  { "Umidade",
    "45-55 % ate o dia 17; 65-70 % na eclosao",
    "# Faixas por fase\n"
    "- Dia 1 ao 17: 45 a 55 % (alvo 50 %).\n"
    "- Dia 18 ao 21 (eclosao): 65 a 70 %, ate 75 %. Amolece a casca e ajuda o pintinho a sair.\n"
    "# Sinais de problema\n"
    "- Umidade alta demais: pintinhos grandes, molhados e atrasados.\n"
    "- Umidade baixa demais: casca dura, pintinho grudado na casca, desidratado.\n"
    "- Os ovos perdem 11 a 13 % do peso ate o dia 18; a camara de ar aumenta. A ovoscopia mostra isso.\n"
    "# Como ajustar\n"
    "- Aumente ou reduza a area de agua e a ventilacao; o umidificador e a bomba de agua ajudam.\n"
    "- Na eclosao, nao abra a incubadora para \"conferir\": a umidade despenca.\n",
    false },
  { "Viragem",
    "3 a 5 vezes por dia, ate o dia 17",
    "# Como virar\n"
    "- No minimo 3 vezes ao dia, ideal 5 ou mais (automatica: a cada 1 a 2 horas), em intervalos regulares.\n"
    "- Incline cerca de 45 graus para cada lado (90 graus no total) ou gire 180 graus se for manual.\n"
    "# Quando\n"
    "- Do dia 1 ao dia 17. Pare no dia 18, na transferencia.\n"
    "- Sem viragem o embriao gruda na membrana da casca e morre. Nos 3 primeiros dias ele e muito fragil: sem sacudir.\n"
    "# Manual\n"
    "- Lave as maos, gire com suavidade e confira as marcas X e O.\n"
    "# No ChocaIoT\n"
    "- O card \"Viragem\" ainda esta em desenvolvimento (aguardando o mecanismo de viragem automatica).\n",
    false },
  { "Ovoscopia (dias 7, 14, 18)",
    "Retire ovos inferteis e embrioes mortos",
    "# O que e\n"
    "- Iluminar o ovo no escuro para ver o interior. Use luz LED fria (nao esquenta) e maos limpas.\n"
    "# Dia 7 (ate o 10)\n"
    "- Fertil: rede de vasos sanguineos e ponto escuro (o embriao).\n"
    "- Ovo claro: infertil. Anel ou mancha de sangue: embriao morto. Retire, pois pode estourar e contaminar os outros.\n"
    "# Dia 14\n"
    "- O embriao ocupa quase todo o ovo e a camara de ar esta maior.\n"
    "# Dia 18\n"
    "- Ultima checagem, junto da transferencia. Retire ovos sem movimento ou sem veias.\n"
    "# Cuidados\n"
    "- Rapido: menos de 1 minuto por ovo e menos de 30 minutos por sessao. Nao deixe os ovos esfriarem.\n",
    false },
  { "Transferencia (dia 18-19)",
    "Pare de virar, deite e suba a umidade",
    "# O que mudar\n"
    "- Pare a viragem no dia 18 (ou 19).\n"
    "- Deite os ovos de lado na cesta de eclosao ou no fundo da incubadora, sem que se encostem.\n"
    "- Suba a umidade para 65 a 70 % e mantenha 37,2 a 37,5 C: os pintinhos ja geram calor proprio.\n"
    "- Aumente a ventilacao, sem abrir a maquina.\n"
    "# Como fazer\n"
    "- Faca a transferencia rapido (1 a 2 minutos), com maos limpas e sem virar os ovos de cabeca para baixo.\n"
    "- Se a sua incubadora tem bandeja unica, apenas pare a rotacao e deite os ovos.\n"
    "# Depois\n"
    "- Nao abra mais ate o fim da eclosao. Nos dias 19-20 ja da para ouvir os piados dentro do ovo.\n",
    false },
  { "Retirada do pintinho",
    "Dia 21: espere secar antes de retirar",
    "# A eclosao\n"
    "- Dia 19-20: bicagem interna (piados); depois surge o primeiro furinho na casca. Da bicada ate sair leva de 12 a 24 horas.\n"
    "- Nao ajude o pintinho: puxar a casca causa sangramento. E nao abra a incubadora.\n"
    "# Retirada\n"
    "- Deixe secar por completo dentro da incubadora (12 a 24 h), ate ficar fofo e ativo. Ele vive 48 h com a reserva da gema, sem comer.\n"
    "- Retire quando a maioria tiver nascido. Ovos que nao eclodiram ate o dia 23 podem ser descartados.\n"
    "# Criadeira\n"
    "- Leve a criadeira ja aquecida a 32-35 C (33-35 C na 1a semana, reduzindo 3 C por semana), com agua e racao inicial.\n"
    "- Molhe o bico de alguns pintinhos para mostrar a agua.\n"
    "# Depois\n"
    "- Limpe e desinfete a incubadora, anote a taxa de eclosao e toque em \"Parar ciclo\" no ChocaIoT.\n",
    false },
  { "Fases do Pintinho",
    "Galeria: desenvolvimento do dia 1 ao 21",
    "",
    true },
};
static const int GUIA_N = sizeof(GUIA_TOPICOS)/sizeof(GUIA_TOPICOS[0]);
