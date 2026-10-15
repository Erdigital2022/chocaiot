// firebase_credentials.example.h — MODELO. NAO coloque segredos aqui.
//
// Como usar:
//   1) copie este arquivo para  firmware/src/firebase_credentials.h   (esse nome ja esta no .gitignore);
//   2) no Console do Firebase: Configuracoes do projeto > Contas de servico > "Gerar nova chave privada";
//   3) abra o JSON baixado e copie os 3 valores abaixo;
//   4) NUNCA faca commit do firebase_credentials.h nem do JSON (quem tem a chave tem acesso de ADMIN ao seu banco).
//
// A chave privada vai numa unica linha, com as quebras de linha escritas como \n (igual esta no JSON).
#pragma once

#define FIREBASE_PROJECT_ID   "seu-projeto-firebase"
#define FIREBASE_CLIENT_EMAIL "firebase-adminsdk-xxxxx@seu-projeto-firebase.iam.gserviceaccount.com"
#define FIREBASE_PRIVATE_KEY  "-----BEGIN PRIVATE KEY-----\nCOLE_AQUI_A_CHAVE_PRIVADA\n-----END PRIVATE KEY-----\n"
