/*
 * IRONCORE : Deep Forge - service worker (hors-ligne).
 * Strategie : reseau d'abord, repli sur le cache. Ainsi game.js et game.wasm restent
 * toujours de la meme version quand on est en ligne, et le jeu marche sans reseau
 * apres la premiere visite. Incrementer CACHE force un nettoyage des anciens caches.
 */
const CACHE = 'ironcore-v1';
const ASSETS = [
  './', 'index.html', 'style.css', 'app.js', 'game.js', 'game.wasm',
  'manifest.json', 'icons/icon.svg', 'icons/icon-192.png', 'icons/icon-512.png',
];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE)
      // addAll echoue si un fichier manque : on tolere, chaque fichier est ajoute individuellement
      .then((c) => Promise.all(ASSETS.map((a) => c.add(a).catch(() => {}))))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== self.location.origin) return;
  e.respondWith(
    fetch(req)
      .then((res) => {
        if (res && res.ok) { const copy = res.clone(); caches.open(CACHE).then((c) => c.put(req, copy)); }
        return res;
      })
      .catch(() => caches.match(req).then((hit) => hit || caches.match('index.html')))
  );
});
