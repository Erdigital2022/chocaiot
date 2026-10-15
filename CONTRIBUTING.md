# Como contribuir

Obrigado pelo interesse! Este é um projeto de hardware + firmware + web; mudanças pequenas e bem explicadas são
as mais fáceis de revisar.

## Antes de abrir um *pull request*

1. **Nunca** inclua segredos (credenciais, senhas, tokens, IDs reais). O `.gitignore` já cobre os arquivos
   conhecidos — confirme com `git status` e `git diff --staged`.
2. Compile o firmware: `cd firmware && pio run -e esp32-s3-controller`.
3. Se mexeu no PWA, teste em `localhost` (veja `docs/04-firebase-e-pwa.md`).
4. Se mexeu em pinos, atualize a tabela de `docs/02-hardware.md` e o esquema correspondente
   (os esquemas são gerados por `docs/esquemas/gerar_*.py`).
5. Se adicionou leituras periódicas no Firestore, **calcule o impacto na cota** e documente em `docs/01-arquitetura.md`.

## Estilo

* Comentários explicam o **porquê** (decisão, medição, bug evitado), não repetem o código.
* Comandos entre app e aparelho usam strings compactas separadas por `|` (veja `docs/06-exemplos.md`).
* Mensagens ao usuário em português.

## Relatando problemas

Use o modelo da seção "Coleta de diagnóstico" de `docs/07-solucao-de-problemas.md` e **remova** e-mails/IDs.
