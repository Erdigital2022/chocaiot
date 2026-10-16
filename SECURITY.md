# Política de segurança

## Reportar uma vulnerabilidade

Se você encontrou uma falha de segurança (regras do Firestore, exposição de dados, acionamento indevido de cargas),
**não abra uma *issue* pública**. Use o recurso *Report a vulnerability* da aba **Security** deste repositório
(relatório privado) ou entre em contato diretamente com o mantenedor pelo e-mail do perfil do GitHub.

Informe: o que é, como reproduzir, impacto e (se possível) uma sugestão de correção.

## Escopo

* `pwa/firestore.rules` e o fluxo de autenticação/compartilhamento;
* firmware (`firmware/`) — comandos remotos, PIN do aparelho, limites de segurança das cargas;
* documentação que possa induzir a uma montagem insegura.

## Proteções ativas neste repositório

* Conta do mantenedor com **autenticação em duas etapas**.
* **Ruleset** na branch `main`: não pode ser apagada, não aceita *force-push* e exige *pull request* de colaboradores
  (apenas o administrador do repositório tem exceção).
* **Secret scanning** e **push protection** do GitHub ativos.
* A chave web do Firebase da instância do mantenedor é **restrita por referrer HTTP** e por API; as regras do
  Firestore (`pwa/firestore.rules`) é que protegem os dados.

## Se você acabou de forkar este projeto

* **Gere suas próprias chaves** (Firebase e conta de serviço); jamais reutilize valores de terceiros.
* Revise `pwa/firestore.rules` antes de publicar — são as regras que protegem os dados.
* Leia [`docs/05-seguranca.md`](docs/05-seguranca.md), inclusive a parte de **segurança elétrica (220 V)**.
