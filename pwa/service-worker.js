// service-worker.js — ChocaIoT PWA
// Estratégia: NETWORK-FIRST para o shell do app (HTML/CSS/JS) — bug real
// encontrado em 2026-09-20: com cache-first, vários deploys seguidos do
// PWA (fixes de bug reais) nunca chegavam a quem já tinha o app aberto,
// porque o service worker so re-executa `install` (que re-popula o cache)
// quando o PRÓPRIO arquivo service-worker.js muda de bytes — e
// CACHE_VERSION não tinha mudado, então o cache antigo (de muito antes
// dos fixes) era servido pra sempre, mascarando deploys reais como "não
// fizeram nada". Agora a rede é tentada primeiro; o cache só entra como
// fallback quando estiver genuinamente offline. Ainda assim, suba
// CACHE_VERSION quando mudar a LISTA de CORE_ASSETS (novo ícone, etc.).
const CACHE_VERSION = "v41";
const CACHE_NAME = `chocaiot-cache-${CACHE_VERSION}`;

// Arquivos essenciais do "app shell" — precisam funcionar offline
const CORE_ASSETS = [
  "/",
  "/index.html",
  "/style.css",
  "/app.js",
  "/guia-data.js",
  "/extras.js",
  "/share.js",
  "/acessos.js",
  "/tampa.js",
  "/painel.js",
  "/manifest.json",
  "/icons/icon-192.png",
  "/icons/icon-512.png",
];

// INSTALL — guarda o app shell no cache
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(CORE_ASSETS))
  );
  self.skipWaiting();
});

// ACTIVATE — limpa caches de versões antigas
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(
        keys
          .filter((key) => key.startsWith("chocaiot-cache-") && key !== CACHE_NAME)
          .map((key) => caches.delete(key))
      )
    )
  );
  self.clients.claim();
});

// FETCH — network-first: tenta a rede sempre que possível (garante que
// deploys novos cheguem de verdade), cai pro cache só se a rede falhar
// (offline de verdade).
self.addEventListener("fetch", (event) => {
  // Nunca interceptar chamadas para Firebase/HiveMQ — deixa ir direto pra rede
  const url = event.request.url;
  if (url.includes("firebaseio.com") || url.includes("googleapis.com") || url.includes("hivemq.cloud")) {
    return;
  }

  event.respondWith(
    // Fase 88: cache:"no-cache" = sempre revalida com o servidor (a rota "/" nao casa com a regra de Cache-Control do hosting e o cache HTTP
    // podia servir um index.html antigo junto de um app.js novo -> tela em branco)
    fetch(event.request, event.request.mode === "navigate" || new URL(url).origin === self.location.origin ? { cache: "no-cache" } : undefined)
      .then((response) => {
        // Clona e guarda no cache pra servir como fallback offline depois
        const responseClone = response.clone();
        caches.open(CACHE_NAME).then((cache) => {
          cache.put(event.request, responseClone);
        });
        return response;
      })
      .catch(() =>
        caches.match(event.request).then((cached) => {
          if (cached) return cached;
          // Offline e sem cache — fallback simples pra navegação
          if (event.request.mode === "navigate") {
            return caches.match("/index.html");
          }
        })
      )
  );
});

// ═══════════════════════════════════════════════════════════════
// Fase 49 — notificações push (FCM). O aparelho envia mensagens "data"
// ({title, body, tag}); o SDK entrega aqui e nós mostramos a notificação.
// try/catch: se o gstatic estiver inacessível (offline), o service worker
// continua funcionando normalmente — só sem push.
// ═══════════════════════════════════════════════════════════════
try {
  importScripts(
    "https://www.gstatic.com/firebasejs/10.14.0/firebase-app-compat.js",
    "https://www.gstatic.com/firebasejs/10.14.0/firebase-messaging-compat.js"
  );
  firebase.initializeApp({
    apiKey: "SUA_API_KEY_WEB_DO_FIREBASE",
    authDomain: "SEU-PROJETO.firebaseapp.com",
    projectId: "SEU-PROJETO",
    storageBucket: "SEU-PROJETO.firebasestorage.app",
    messagingSenderId: "SEU_MESSAGING_SENDER_ID",
    appId: "SEU_APP_ID",
  });
  const messaging = firebase.messaging();
  // Fase 54: preferências de som/vibração vêm do Cache "choca-prefs" (gravado pelo app); avisos de alerta ficam na tela até tocar
  const lerPrefs = async () => {
    try { const c = await caches.open("choca-prefs"); const r = await c.match("/prefs.json"); return r ? await r.json() : {}; } catch (e) { return {}; }
  };
  messaging.onBackgroundMessage(async (payload) => {
    const d = (payload && payload.data) || {};
    const p = await lerPrefs();
    const alerta = /choca-(0|1|6|7)$/.test(d.tag || "");
    const abertas = await self.clients.matchAll({ type: "window", includeUncontrolled: true });
    abertas.forEach((c) => c.postMessage({ tipo: "push", tag: d.tag })); // página aberta em 2º plano toca o som escolhido
    return self.registration.showNotification(d.title || "ChocaIoT", {
      body: d.body || "",
      tag: d.tag || "choca",
      renotify: true,
      silent: false,
      requireInteraction: alerta,
      vibrate: p.vibrar === false ? undefined : [250, 120, 250, 120, 400],
      icon: "/icons/icon-192.png",
      badge: "/icons/icon-192.png",
    });
  });
} catch (e) {
  // sem push neste carregamento
}

self.addEventListener("notificationclick", (event) => {
  event.notification.close();
  event.waitUntil(
    self.clients.matchAll({ type: "window", includeUncontrolled: true }).then((lista) => {
      for (const c of lista) if ("focus" in c) return c.focus();
      return self.clients.openWindow("/");
    })
  );
});
