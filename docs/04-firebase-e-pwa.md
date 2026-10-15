# 04 — Firebase e PWA

## 1. Criar o projeto Firebase

1. Em <https://console.firebase.google.com> crie um projeto (plano **Spark**, gratuito, basta).
2. **Authentication → Método de login → Google**: ative.
3. **Firestore Database**: crie em modo *produção* (as regras do repositório substituem o padrão), região próxima.
4. **Hosting**: ative (será usado pelo `firebase deploy`).
5. **Configurações do projeto → Seus apps → Web (`</>`)**: registre um app Web e copie o objeto `firebaseConfig`.
6. **Contas de serviço → Gerar nova chave privada**: baixa um JSON. Ele vira o `firebase_credentials.h` do firmware
   (ver [`firmware/src/firebase_credentials.example.h`](../firmware/src/firebase_credentials.example.h)).

> 🔐 O JSON da conta de serviço dá **acesso de administrador** ao banco. Guarde fora do repositório, não envie por
> chat/e-mail e gere uma nova chave se suspeitar de vazamento (Console → Contas de serviço → gerenciar chaves).

## 2. Configurar o PWA

Edite **dois** lugares com o mesmo `firebaseConfig` (valores do passo 5):

* `pwa/app.js` — constante `firebaseConfig` no topo;
* `pwa/service-worker.js` — bloco `firebase.initializeApp({...})` (usado pelas notificações em segundo plano).

```js
const firebaseConfig = {
  apiKey: "AIza...",                          // chave WEB: identifica o app, NÃO é segredo de admin
  authDomain: "seu-projeto.firebaseapp.com",
  projectId: "seu-projeto",
  storageBucket: "seu-projeto.firebasestorage.app",
  messagingSenderId: "SEU_MESSAGING_SENDER_ID",
  appId: "1:123456789012:web:abcdef...",
};
```

A *API key* web não dá acesso aos dados — quem protege o banco são as **regras** (`pwa/firestore.rules`). Mesmo assim,
no Google Cloud Console restrinja a chave por *referrer HTTP* (`seu-projeto.web.app/*`) e às APIs necessárias.

## 3. Publicar regras e app

```bash
npm i -g firebase-tools
firebase login
cp pwa/.firebaserc.example pwa/.firebaserc     # edite o nome do projeto
cd pwa
firebase deploy --only firestore:rules --project SEU-PROJETO
firebase deploy --only hosting        --project SEU-PROJETO
```

`firebase.json` já define `Cache-Control: no-cache` para HTML/JS/CSS e para o *service worker* — sem isso o navegador
pode servir a versão antiga do app depois de um deploy.

## 4. Vincular a chocadeira

1. No Firestore crie o documento `chocadeiras/<ID>` (qualquer ID, ex.: gerado automaticamente), **sem** o campo `donoId`.
2. Coloque `<ID>` em `FIRESTORE_DOCUMENT_PATH` no `firmware/src/main.cpp` e grave.
3. No aparelho: *Configurações → Sistema* mostra o **ID Disp.**
4. No app, depois do login, escolha "Vincular chocadeira" e digite o ID. As regras só permitem esse *claim* uma vez
   (documento sem `donoId` e escrita só desse campo).

## 5. Notificações push (FCM)

* O **próprio aparelho** envia (FCM v1, com a conta de serviço) — não há Cloud Functions.
* O app registra o token em `chocadeiras/<ID>.pushTokens` como `"token|flags"`:
  `1` energia · `2` internet · `4` faixa T/U/CO₂ · `8` fim de ciclo.
* O Android só toca som no canal de notificação do sistema; o app aberto toca o tom escolhido.

## 6. Compartilhar com outras pessoas

Config. → *Usuários compartilhados*: gera um **link de convite** (e-mail/WhatsApp/copiar). Permissões por
pessoa: `ver`, `ch0…ch7`, `ciclo`, `total`. Convidado nunca controla a bomba de água nem o cooler. Tudo o que o convidado
faz é registrado em `logs` com o e-mail dele (as regras impedem assinar como outra pessoa).

## 7. Boas práticas de cota

Veja a tabela em [`01-arquitetura.md`](01-arquitetura.md). Regra de ouro: **não crie leituras periódicas novas
sem calcular o custo**.

## Testar o PWA localmente

```bash
cd pwa
python -m http.server 8080       # ou: npx serve .
# abra http://localhost:8080  (login Google exige domínio autorizado: adicione "localhost" em
# Authentication → Configurações → Domínios autorizados)
```
