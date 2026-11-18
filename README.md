# IRONCORE : Deep Forge

Colonie naine souterraine : **minez**, **automatisez** (foreuses → tapis → fonderies), **défendez** le cœur ♜ contre des vagues de monstres.
Moteur en **C pur → WebAssembly**, rendu **Canvas « ASCII moderne »**, **PWA** hors-ligne, tactile + clavier/souris.

## Architecture

```
ironcore/
├─ src/                    ← TOUT le jeu est ici (C pur, aucune dépendance)
│  ├─ game.h               types partagés, constantes, enums (carte, nains, ennemis, bâtiments)
│  ├─ map.c                RNG déterministe, génération procédurale (cavernes, filons, eau, lave)
│  ├─ path.c               pathfinding A* (tas binaire, nains et monstres)
│  ├─ sim.c                nains (jobs creuser/construire), tapis, foreuses, fonderies
│  ├─ combat.c             vagues, IA des monstres, tourelles, pièges
│  └─ api.c                interface exportée vers JS : boucle, outils, lumière, particules,
│                          tampons de rendu, sauvegarde/chargement
├─ web/                    ← ce que Vercel publie
│  ├─ index.html style.css app.js     UI, rendu canvas, entrées (le JS ne contient AUCUNE règle de jeu)
│  ├─ manifest.json service-worker.js icons/   PWA (installable, hors-ligne)
│  └─ game.js + game.wasm             générés par la compilation (voir plus bas)
├─ tests/test_core.c       tests natifs (ASAN/UBSAN) : génération, minage, tapis, fonderie, défense, sauvegarde
├─ build.sh                ./build.sh [wasm | test | serve]
├─ setup-emsdk.sh          installe Emscripten (Codespaces)
├─ vercel.json             config de déploiement
└─ .github/workflows/build-wasm.yml   compile le wasm à chaque push et le commit
```

Principe : le C possède l'état entier (`Game G`, sans pointeurs, donc sauvegardable par simple `memcpy`).
Chaque frame, le JS appelle `game_update(dt)` puis lit les tampons (`glyph/color/flags/light/entities/particles`) directement dans la mémoire Wasm.

## Guide pas-à-pas : 100 % en ligne, sans rien installer

### Option A — GitHub Codespaces (recommandé)
1. Créez un dépôt GitHub et uploadez le dossier (bouton **Add file → Upload files**, glissez tout le contenu), ou poussez-le avec git.
2. Dépôt → **Code → Codespaces → Create codespace on main** (gratuit dans le quota mensuel).
3. Dans le terminal du Codespace :
   ```bash
   ./setup-emsdk.sh                 # une seule fois (~2 min)
   source ~/emsdk/emsdk_env.sh      # à refaire dans chaque nouveau terminal
   ./build.sh test                  # (optionnel) tests natifs du moteur
   ./build.sh wasm                  # génère web/game.js + web/game.wasm
   ./build.sh serve                 # serveur sur le port 8080
   ```
4. Codespaces propose d'ouvrir le port **8080** : cliquez → le jeu se lance dans le navigateur (testable aussi sur votre téléphone via l'URL du port, en le passant en *Public*).

### Option B — Zéro terminal : GitHub Actions compile pour vous
1. Poussez le code sur la branche `main`.
2. Le workflow `build-wasm` lance les tests, compile le wasm et **commit** `web/game.js` + `web/game.wasm` automatiquement.
3. *Réglage à vérifier une fois* : Dépôt → **Settings → Actions → General → Workflow permissions → Read and write permissions**.

### Déploiement Vercel
1. [vercel.com](https://vercel.com) → **Add New → Project** → importez le dépôt GitHub.
2. Ne touchez à rien : `vercel.json` fixe *Framework = Other*, *Output Directory = web*, pas de build.
3. **Deploy**. Chaque push sur `main` (donc chaque build wasm de l'option B) redéploie automatiquement.
4. Ouvrez l'URL (HTTPS) sur mobile → *Ajouter à l'écran d'accueil* : l'app s'installe et fonctionne hors-ligne.

> Les fichiers `game.js`/`game.wasm` doivent être présents dans `web/` au moment du déploiement
> (option B les commit pour vous ; sinon commitez-les après `./build.sh wasm`).

## Contrôles

| | PC | Mobile |
|---|---|---|
| Utiliser l'outil | clic / glisser | 1 doigt (tap ou glisser) |
| Déplacer la caméra | clic droit ou milieu + glisser · flèches / WASD | 2 doigts · ou outil ✋ Caméra |
| Zoom | molette · `+` `-` | pinch |
| Outils | `1`…`9`, `0` = caméra | barre du bas |
| Sens du tapis | `R` (ou suivre le tracé) | bouton « ↻ Sens » · le tracé suit le doigt |
| Pause / vitesse | `Espace` / bouton ▶ | bouton ▶ (x1 → x2 → x4) |

## Boucle de jeu (MVP)
1. **Creuser** (rectangle) : les nains minent. Filons colorés : fer `%`, charbon, cristaux runiques `◆` (plus profonds).
2. **Foreuse** sur un filon déjà creusé → **tapis** → **fonderie** (fer + charbon → lingots) → **tapis** → cœur.
3. **Défense** : **tourelle** (consomme lingots, ou cristaux pour ×2,7 de dégâts), **mur** (les monstres le détruisent), **piège** à pointes. Première vague à 2 min 15.
4. Cases rouges hachurées = zone désignée **inaccessible** : creusez un passage depuis la caverne.

Sauvegarde automatique (toutes les 10 s, à la fermeture et au passage en arrière-plan) dans `localStorage`.

## Ajouter du contenu
- **Nouveau bâtiment** : `enum B_*` + `TOOL_*` (game.h), coût dans `api.c` (tables `COST_*`), logique dans `sim.c`/`combat.c`, glyphe dans `prepare_render` (api.c) + `GLYPH`/`TOOLS` dans `app.js`.
- **Nouvel ennemi** : `enum EN_*` + tables `EN_HP/EN_SPEED/EN_DMG` (combat.c) et le glyphe dans `app.js`.
- Si vous changez la structure `Game`, la sauvegarde existante est automatiquement ignorée (contrôle de taille/version) et une nouvelle partie démarre.

## Limites connues de ce MVP
- Pas de son, une seule carte 96×64, un seul type de tourelle.
- Les nains choisissent leurs tâches sans priorités ni stocks d'outils.
- Les tapis déplacent un objet par case (pas d'animation fluide entre cases).

## Version de secours préconstruite
`web/game.js` + `web/game.wasm` sont fournis **déjà compilés** (clang/wasm32 + une petite colle JS) pour que le jeu soit jouable
dès le premier déploiement, sans Emscripten. Ils sont **remplacés** par la vraie compilation Emscripten dès que vous lancez
`./build.sh wasm` ou que la CI GitHub tourne (même interface, mêmes noms de fichiers).
