# 05 — Segurança

## 1. O que NUNCA vai para o repositório

| Item | Por quê | Onde fica |
|---|---|---|
| `firmware/src/firebase_credentials.h` | Chave privada da conta de serviço = **admin** do seu banco | só na sua máquina (`.gitignore`) |
| JSON `*firebase-adminsdk*.json` | idem | gerenciador de senhas / pasta fora do repo |
| `camera/src/servidor/wifi_config.h` | Senha do seu WiFi | só na sua máquina |
| Tokens (GitHub `ghp_…`, `github_pat_…`) | Acesso à sua conta | gerenciador de credenciais (`gh auth login`) |
| ID da sua chocadeira e e-mails | Identificam você | substituídos por marcadores |

> **Nunca coloque token na URL do remote** (`https://ghp_XXXX@github.com/...`): ele fica em texto claro no
> `.git/config`, é copiado por backups/OneDrive e aparece em logs. Use `gh auth login` ou o Git Credential Manager.

## 2. Verificação antes de publicar

```bash
# 1) nenhum segredo conhecido?
grep -rEn "BEGIN (RSA )?PRIVATE KEY|ghp_|github_pat_|AIza[0-9A-Za-z_-]{30,}" . --include=*.{h,cpp,js,json,md,ini} 
# 2) os arquivos ignorados realmente não estão versionados?
git ls-files | grep -E "firebase_credentials\.h|wifi_config\.h|adminsdk"     # deve sair vazio
# 3) histórico limpo? (uma chave que "sumiu" num commit antigo continua no histórico!)
git log --all -S"BEGIN PRIVATE KEY" --oneline
```

Ferramentas melhores: [`gitleaks`](https://github.com/gitleaks/gitleaks) e o *secret scanning* / *push protection*
do GitHub (Settings → Code security).

**Se um segredo vazou:** (1) revogue/rotacione **primeiro** (apagar do repositório não basta — já foi copiado);
(2) só depois limpe o histórico (`git filter-repo`); (3) confirme no log de auditoria do provedor se houve uso.

## 3. Modelo de ameaça do sistema

| Superfície | Proteção |
|---|---|
| Dados no Firestore | Regras: só o dono (e convidados com permissão) leem/escrevem; `donoId` imutável |
| Comando de cargas 220 V pelo app | Confirmação extra ao **ligar**; bomba de água (CH3) só no aparelho |
| Aparelho físico | PIN opcional (hash SHA-256 + sal) com recuperação pelo app logado |
| App no celular | Bloqueio por biometria (WebAuthn) ou PIN; auditoria de aparelhos novos e acessos negados |
| Convidados | Permissões mínimas; log de tudo com e-mail verificado |
| Cota | Polling com *DocumentMask*; botões de ação nunca re-assinam listeners |

Limites assumidos (não há servidor próprio): sem revogação de sessões Google, sem captura de IP, "aparelho novo"
é por navegador.

## 4. Segurança elétrica (220 V)

* Trabalhe **sempre desligado da tomada**; confirme com multímetro.
* Disjuntor/DR e **fusível** no circuito das cargas; a resistência de aquecimento deve ter **proteção térmica
  independente** do firmware (termostato mecânico/fusível térmico) — software falha.
* Emendas isoladas, cabos de 220 V longe do I2C/SPI e da água; classe de isolamento adequada na bomba.
* **Pull-up nos relés** (10 kΩ ao 3V3) para que nada ligue durante reset/gravação.
* Teste cargas novas primeiro com lâmpada comum e, depois, com a carga real, sempre observando a temperatura.
* Reservatório de água elétrico: ponta da mangueira acima do nível máximo (evita sifão), ladrão de segurança,
  dose limitada por pulso (o firmware limita volume por hora/dia e persiste os contadores).

## 5. Reportar vulnerabilidade

Veja [`SECURITY.md`](../SECURITY.md).
