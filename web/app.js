/*
 * IRONCORE : Deep Forge - pont JS <-> Wasm.
 * Ce fichier ne contient AUCUNE regle de jeu : il lit les tampons exposes par le
 * moteur C (glyphes, couleurs, lumiere, entites, particules), dessine sur le canvas
 * et transmet les ordres du joueur (outils) au moteur.
 */
'use strict';

/* ======================= Constantes alignees sur game.h ======================= */
const MAP_W_FALLBACK = 96, MAP_H_FALLBACK = 64;
const SAVE_KEY = 'ironcore.save.v1';
const ERR_TEXT = { 1: 'Ressources insuffisantes', 2: 'Placement impossible ici', 3: 'Case occupée', 4: 'Partie terminée' };

/* indices C_* -> couleurs */
const PAL = ['#000', '#5a5675', '#4a4466', '#3fa9ff', '#ff6a2b', '#8fb8ff', '#c9b88a', '#c77dff', '#ffb347',
  '#ffd166', '#ff5a4f', '#ff2e88', '#e8c547', '#7ee0ff', '#ff8a3d', '#a9b4c8', '#5ef0a0', '#ff9aa2',
  '#5ef0a0', '#fff2a8', '#ff8a3d'];
const ITEM_COL = [null, PAL[5], PAL[6], PAL[7], PAL[8]];   /* IT_NONE, fer, charbon, cristal, lingot */

/* identifiants G_* -> caracteres */
const GLYPH = ['', '#', '·', '≈', '≈', '%', '→', '↓', '←', '↑', '◎', '⚙', '▓', 'Ψ', '^', '♜', '@', 'x', 'Ω', '*', '·', '•'];
const G_WATER = 3, G_LAVA = 4, G_DWARF_KIND = 1;
const ARROWS = ['→', '↓', '←', '↑'];
const DX = [1, 0, -1, 0], DY = [0, 1, 0, -1];

/* outils : t = identifiant TOOL_* du moteur C (0 = camera, cote JS uniquement) */
const TOOLS = [
  { id: 'pan',  t: 0, name: 'Caméra',    gl: '✋', key: '0', hint: 'Glisser = déplacer la carte · pincer = zoomer.' },
  { id: 'dig',  t: 1, name: 'Creuser',   gl: '▒', key: '1', mode: 'rect', hint: 'Glissez un rectangle sur la roche : les nains creusent. Rouge = inaccessible : creusez depuis la caverne.' },
  { id: 'cancel', t: 2, name: 'Annuler', gl: '✕', key: '2', mode: 'rect', hint: 'Glissez pour retirer des zones à creuser.' },
  { id: 'conv', t: 3, name: 'Tapis',     gl: '→', key: '3', mode: 'line', cost: '1 fer', hint: 'Tracez un chemin : le sens suit votre doigt. Toucher un tapis le fait pivoter.' },
  { id: 'drill', t: 4, name: 'Foreuse',  gl: '◎', key: '4', mode: 'line', cost: '8 fer', hint: 'À poser sur un filon déjà creusé (case colorée). Elle éjecte vers les tapis voisins.' },
  { id: 'forge', t: 5, name: 'Fonderie', gl: '⚙', key: '5', mode: 'line', cost: '15 fer', hint: 'Reçoit fer + charbon par tapis, produit des lingots.' },
  { id: 'wall', t: 6, name: 'Mur',       gl: '▓', key: '6', mode: 'line', cost: '1 lingot', hint: 'Tracez un mur renforcé pour bloquer les monstres (ils le détruiront).' },
  { id: 'turret', t: 7, name: 'Tourelle', gl: 'Ψ', key: '7', mode: 'line', cost: '6 lingots', hint: 'Tire automatiquement. Consomme lingots (ou cristaux : dégâts x2.7).' },
  { id: 'trap', t: 8, name: 'Piège',     gl: '^', key: '8', mode: 'line', cost: '2 lingots', hint: 'Pointes : blesse les monstres qui marchent dessus (usure).' },
  { id: 'demo', t: 9, name: 'Démolir',   gl: '⌫', key: '9', mode: 'rect', hint: 'Glissez pour démolir (remboursement partiel).' },
];

/* ============================= Etat global ============================= */
const $ = (id) => document.getElementById(id);
const canvas = $('game'), ctx = canvas.getContext('2d', { alpha: false });
const lightCv = document.createElement('canvas');
let lightCtx, lightImg;

let M = null;                                   /* module Wasm */
let MW = MAP_W_FALLBACK, MH = MAP_H_FALLBACK;
let ptr = {};                                   /* pointeurs vers les tampons C */
let W = 0, H = 0, DPR = 1;
const cam = { x: 18.5, y: 18.5, cell: 26 };
let tool = TOOLS[1], rotDir = 0;
let speed = 1, paused = false, menuOpen = false, over = false;
let lastT = 0, timeS = 0, lastHud = 0, lastSave = 0;
const keys = new Set();
let hover = null;                               /* case survolee (souris) */
let toastTimer = 0, lastToast = '';

/* ============================= Utilitaires ============================= */
function toast(msg, bad) {
  const el = $('toast');
  if (msg === lastToast && el.classList.contains('show')) return;
  lastToast = msg; el.textContent = msg;
  el.classList.toggle('bad', !!bad); el.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.classList.remove('show'); lastToast = ''; }, 1800);
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const toTile = (sx, sy) => ({
  x: Math.floor((sx - W / 2) / cam.cell + cam.x),
  y: Math.floor((sy - H / 2) / cam.cell + cam.y),
});
function minCell() { return Math.max(7, Math.min(W / MW, H / MH) * 0.9); }
function clampCam() {
  cam.cell = clamp(cam.cell, minCell(), 72);
  cam.x = clamp(cam.x, 0, MW); cam.y = clamp(cam.y, 0, MH);
}
function zoomAt(factor, sx, sy) {                 /* zoom en gardant le point (sx,sy) fixe */
  const before = { x: (sx - W / 2) / cam.cell + cam.x, y: (sy - H / 2) / cam.cell + cam.y };
  cam.cell = clamp(cam.cell * factor, minCell(), 72);
  cam.x = before.x - (sx - W / 2) / cam.cell; cam.y = before.y - (sy - H / 2) / cam.cell;
  clampCam();
}

/* ============================== Wasm / jeu ============================== */
const fn = (n) => M['_' + n];

function refreshPointers() {
  ptr = {
    glyph: fn('game_ptr_glyph')(), color: fn('game_ptr_color')(), flags: fn('game_ptr_flags')(),
    light: fn('game_ptr_light')(), item: fn('game_ptr_item')(), ent: fn('game_ptr_entities')() ,
    part: fn('game_ptr_particles')(),
  };
  MW = fn('game_map_w')(); MH = fn('game_map_h')();
  lightCv.width = MW; lightCv.height = MH;
  lightCtx = lightCv.getContext('2d');
  lightImg = lightCtx.createImageData(MW, MH);
}

function newGame() {
  try { localStorage.removeItem(SAVE_KEY); } catch (e) { /* stockage indisponible */ }
  fn('game_init')((Date.now() & 0x7fffffff) | 1);
  over = false; $('gameover').hidden = true;
  cam.x = fn('game_core_x')() + 0.5; cam.y = fn('game_core_y')() + 0.5; cam.cell = defaultCell();
  clampCam();
  toast('Nouvelle partie — creusez vers les filons colorés');
}
const defaultCell = () => Math.max(minCell(), Math.min(30, Math.max(17, Math.min(W, H) / 19)));

/* ---- sauvegarde locale (localStorage, base64) ---- */
function bytesToB64(u8) {
  let s = ''; const CH = 0x8000;
  for (let i = 0; i < u8.length; i += CH) s += String.fromCharCode.apply(null, u8.subarray(i, i + CH));
  return btoa(s);
}
function saveGame() {
  if (!M || over) return;
  try {
    const n = fn('game_save')(), p = fn('game_buffer')();
    localStorage.setItem(SAVE_KEY, bytesToB64(M.HEAPU8.subarray(p, p + n)));
    localStorage.setItem(SAVE_KEY + '.cam', JSON.stringify(cam));
  } catch (e) { /* quota / mode prive : on ignore */ }
}
function loadGame() {
  try {
    const s = localStorage.getItem(SAVE_KEY);
    if (!s) return false;
    const bin = atob(s), n = bin.length;
    if (n !== fn('game_save_size')()) return false;       /* sauvegarde d'une autre version */
    const p = fn('game_buffer')(), heap = M.HEAPU8;
    for (let i = 0; i < n; i++) heap[p + i] = bin.charCodeAt(i);
    if (!fn('game_load')(n)) return false;
    const c = JSON.parse(localStorage.getItem(SAVE_KEY + '.cam') || 'null');
    if (c) { cam.x = c.x; cam.y = c.y; cam.cell = c.cell; }
    clampCam();
    return true;
  } catch (e) { return false; }
}

/* ============================== Rendu ============================== */
function resize() {
  DPR = Math.min(window.devicePixelRatio || 1, 2.5);
  W = window.innerWidth; H = window.innerHeight;
  canvas.width = Math.round(W * DPR); canvas.height = Math.round(H * DPR);
  clampCam();
}

function drawLightmap() {
  const L = M.HEAPU8, d = lightImg.data, base = ptr.light;
  for (let y = 0, i = 0; y < MH; y++) for (let x = 0; x < MW; x++, i++) {
    const l = L[base + i];
    let v = 0;
    if (l > 0) {
      v = Math.pow(l / 255, 0.78);
      if (l > 40) v *= 1 + 0.05 * Math.sin(timeS * 9 + x * 1.7 + y * 2.3);   /* vacillement des flammes */
    }
    const o = i * 4;
    d[o] = Math.min(255, 255 * v);
    d[o + 1] = Math.min(255, 235 * v);
    d[o + 2] = Math.min(255, 205 * v + (l > 0 ? 14 : 0));    /* ombres legerement bleutees */
    d[o + 3] = 255;
  }
  lightCtx.putImageData(lightImg, 0, 0);
}

function render() {
  const cell = cam.cell;
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  ctx.fillStyle = '#07060b'; ctx.fillRect(0, 0, W, H);

  const ox = W / 2 - cam.x * cell, oy = H / 2 - cam.y * cell;
  const x0 = Math.max(0, Math.floor(-ox / cell)), x1 = Math.min(MW - 1, Math.ceil((W - ox) / cell));
  const y0 = Math.max(0, Math.floor(-oy / cell)), y1 = Math.min(MH - 1, Math.ceil((H - oy) / cell));

  const U8 = M.HEAPU8, F32 = M.HEAPF32;
  ctx.font = `${Math.round(cell * 0.92)}px ui-monospace, "DejaVu Sans Mono", Menlo, Consolas, monospace`;
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';

  /* --- tuiles --- */
  for (let y = y0; y <= y1; y++) {
    for (let x = x0; x <= x1; x++) {
      const i = y * MW + x, g = U8[ptr.glyph + i];
      if (g === 0) continue;
      const px = ox + x * cell, py = oy + y * cell, col = U8[ptr.color + i], fl = U8[ptr.flags + i];
      const rock = g === 1 || g === 5;
      ctx.fillStyle = rock ? '#0f0d17' : (g === G_LAVA ? '#2a0f08' : g === G_WATER ? '#0a1a2e' : '#191625');
      ctx.fillRect(px, py, cell + 0.5, cell + 0.5);

      let gy = py + cell * 0.54;
      if (g === G_WATER || g === G_LAVA) gy += Math.sin(timeS * 2.2 + x * 0.9 + y * 0.5) * cell * 0.07;
      ctx.globalAlpha = (fl & 1) ? 0.4 : 1;
      ctx.fillStyle = PAL[col] || '#fff';
      if (g === 2 && col === 2) ctx.globalAlpha *= 0.55;                  /* sol nu : discret */
      ctx.fillText(GLYPH[g], px + cell / 2, gy);
      ctx.globalAlpha = 1;

      if (fl & 1) { ctx.strokeStyle = '#7ee0ff'; ctx.setLineDash([3, 3]); ctx.strokeRect(px + 1.5, py + 1.5, cell - 3, cell - 3); ctx.setLineDash([]); }
      if (fl & 2) {                       /* zone a creuser : ambre ; rouge hachure si inaccessible */
        const bad = fl & 8;
        ctx.fillStyle = bad ? 'rgba(255,90,79,.30)' : 'rgba(255,179,71,.28)'; ctx.fillRect(px, py, cell, cell);
        ctx.strokeStyle = bad ? '#ff5a4f' : '#ffb347'; if (bad) ctx.setLineDash([4, 3]);
        ctx.strokeRect(px + 1.5, py + 1.5, cell - 3, cell - 3); ctx.setLineDash([]);
      }
      if (fl & 4) { ctx.fillStyle = 'rgba(255,255,255,.55)'; ctx.fillRect(px, py, cell, cell); }
      const it = U8[ptr.item + i];
      if (it) { ctx.fillStyle = ITEM_COL[it]; ctx.beginPath(); ctx.arc(px + cell * 0.5, py + cell * 0.5, cell * 0.13, 0, 6.3); ctx.fill(); }
    }
  }

  /* --- entites (nains / monstres) --- */
  const n = fn('game_entity_count')(), eb = ptr.ent >> 2;
  for (let k = 0; k < n; k++) {
    const e = eb + k * 5, ex = F32[e], ey = F32[e + 1], kind = F32[e + 2], hp = F32[e + 3], st = F32[e + 4];
    if (ex < x0 - 1 || ex > x1 + 2 || ey < y0 - 1 || ey > y1 + 2) continue;
    const sx = ox + ex * cell, sy = oy + ey * cell + (kind === 1 && st === 2 ? Math.sin(timeS * 22 + k) * cell * 0.06 : 0);
    ctx.fillStyle = kind === 1 ? PAL[9] : kind === 2 ? PAL[10] : PAL[11];
    ctx.fillText(kind === 1 ? '@' : kind === 2 ? 'x' : 'Ω', sx, sy + cell * 0.04);
    if (hp < 0.999) {
      ctx.fillStyle = '#000a'; ctx.fillRect(sx - cell * 0.4, sy - cell * 0.62, cell * 0.8, cell * 0.1);
      ctx.fillStyle = hp > 0.5 ? '#5ef0a0' : hp > 0.25 ? '#ffd166' : '#ff5a4f';
      ctx.fillRect(sx - cell * 0.4, sy - cell * 0.62, cell * 0.8 * Math.max(0, hp), cell * 0.1);
    }
  }

  /* --- eclairage dynamique : la carte de lumiere (1 px / tuile) est mise a l'echelle, lissee,
         puis multipliee sur la scene --- */
  drawLightmap();
  ctx.globalCompositeOperation = 'multiply';
  ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(lightCv, ox, oy, MW * cell, MH * cell);
  ctx.globalCompositeOperation = 'source-over';

  /* --- scintillement des filons (auto-eclaire : on repere le minerai meme dans l'ombre) --- */
  ctx.globalCompositeOperation = 'lighter';
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
    const i = y * MW + x;
    if (U8[ptr.glyph + i] !== 5) continue;
    ctx.globalAlpha = 0.28 + 0.2 * Math.sin(timeS * 2.4 + x * 1.3 + y * 0.7);
    ctx.fillStyle = PAL[U8[ptr.color + i]];
    ctx.fillText('%', ox + (x + 0.5) * cell, oy + (y + 0.54) * cell);
  }
  ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';

  /* --- particules (auto-eclairees, additives) --- */
  const pn = fn('game_particle_count')(), stride = fn('game_particle_stride')();
  ctx.globalCompositeOperation = 'lighter';
  for (let k = 0; k < pn; k++) {
    const o = ptr.part + k * stride, life = M.HEAPF32[(o >> 2) + 4];
    if (life <= 0) continue;
    const px = M.HEAPF32[o >> 2], py = M.HEAPF32[(o >> 2) + 1], ml = M.HEAPF32[(o >> 2) + 5];
    ctx.globalAlpha = clamp(life / ml, 0, 1);
    ctx.fillStyle = PAL[U8[o + 24]] || '#fff';
    ctx.fillText(GLYPH[U8[o + 25]] || '*', ox + px * cell, oy + py * cell);
  }
  ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';

  /* --- apercu de l'outil --- */
  if (stroke && stroke.rect) {
    const r = normRect(stroke.rect);
    ctx.fillStyle = 'rgba(255,179,71,.18)'; ctx.strokeStyle = '#ffb347';
    ctx.fillRect(ox + r.x0 * cell, oy + r.y0 * cell, (r.x1 - r.x0 + 1) * cell, (r.y1 - r.y0 + 1) * cell);
    ctx.strokeRect(ox + r.x0 * cell + 1, oy + r.y0 * cell + 1, (r.x1 - r.x0 + 1) * cell - 2, (r.y1 - r.y0 + 1) * cell - 2);
  } else if (hover && tool.t) {
    ctx.strokeStyle = '#ffb347'; ctx.lineWidth = 2;
    ctx.strokeRect(ox + hover.x * cell + 1, oy + hover.y * cell + 1, cell - 2, cell - 2);
    ctx.lineWidth = 1;
    if (tool.t === 3) { ctx.fillStyle = '#ffd24a'; ctx.globalAlpha = .8; ctx.fillText(ARROWS[rotDir], ox + (hover.x + .5) * cell, oy + (hover.y + .54) * cell); ctx.globalAlpha = 1; }
  }
}

/* ============================== Interface ============================== */
function buildToolbar() {
  const bar = $('toolbar'); bar.innerHTML = '';
  TOOLS.forEach((t) => {
    const b = document.createElement('button');
    b.dataset.id = t.id; if (t.id === 'pan') b.classList.add('cam');
    b.innerHTML = `<span class="gl">${t.gl}</span><span>${t.name}</span>` +
      (t.cost ? `<span class="cost">${t.cost}</span>` : '') + `<span class="key">[${t.key}]</span>`;
    b.addEventListener('click', () => selectTool(t));
    bar.appendChild(b);
  });
}
function selectTool(t) {
  tool = t;
  document.querySelectorAll('#toolbar button').forEach((b) => b.classList.toggle('on', b.dataset.id === t.id));
  $('hint').textContent = t.hint;
  $('btn-rot').hidden = t.t !== 3;
  $('btn-rot').textContent = `↻ Sens : ${ARROWS[rotDir]}`;
}
function rotate() {
  rotDir = (rotDir + 1) & 3;
  $('btn-rot').textContent = `↻ Sens : ${ARROWS[rotDir]}`;
}
function setMenu(open) {
  menuOpen = open; $('menu').hidden = !open;
  if (open) saveGame();
}
const fmtTime = (s) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;

function updateHud() {
  const S = fn('game_stat');
  $('r-iron').textContent = S(0); $('r-coal').textContent = S(1);
  $('r-ingot').textContent = S(3); $('r-crystal').textContent = S(2); $('r-dwarf').textContent = S(7);
  const wave = S(5), enemies = S(8), left = S(12), box = $('wave-box');
  $('r-wave').textContent = 'V' + wave;
  $('r-timer').textContent = (enemies > 0 || left > 0) ? `⚠ ${enemies + left} monstres` : fmtTime(S(6));
  box.classList.toggle('alert', enemies > 0 || left > 0 || S(6) <= 10);
  $('core-bar').style.width = Math.round(S(4) / 5) + '%';
  $('core-bar').style.background = S(4) > 250 ? '' : '#ff5a4f';
  if (S(9) && !over) {
    over = true;
    try { localStorage.removeItem(SAVE_KEY); } catch (e) { /* ignore */ }
    $('go-stats').textContent = `Vague ${wave} · ${S(10)} monstres tués · ${fmtTime(S(11))} de survie`;
    $('gameover').hidden = false;
  }
}

/* ============================== Entrees ============================== */
let stroke = null;                       /* trait en cours (outil) */
const pointers = new Map();
let gesture = 'none';                    /* none | pan | paint | pinch | wait */
let pinch = null;

const normRect = (r) => ({ x0: Math.min(r.x0, r.x1), y0: Math.min(r.y0, r.y1), x1: Math.max(r.x0, r.x1), y1: Math.max(r.y0, r.y1) });
function lineCells(a, b) {               /* chemin 4-connexe a -> b (a exclu) */
  const out = []; let x = a.x, y = a.y;
  while (x !== b.x || y !== b.y) {
    const dx = Math.abs(b.x - x), dy = Math.abs(b.y - y);
    if (dx >= dy && dx > 0) x += Math.sign(b.x - x); else y += Math.sign(b.y - y);
    out.push({ x, y });
  }
  return out;
}
function dirOf(a, b) { for (let d = 0; d < 4; d++) if (a.x + DX[d] === b.x && a.y + DY[d] === b.y) return d; return rotDir; }

function apply(x, y, dir) {
  if (!inMap(x, y)) return false;
  const ok = fn('game_apply_tool')(tool.t, x, y, dir);
  if (!ok && !stroke.err) { stroke.err = fn('game_last_error')(); toast(ERR_TEXT[stroke.err] || 'Impossible', true); }
  return !!ok;
}
const inMap = (x, y) => x >= 0 && y >= 0 && x < MW && y < MH;

function beginPaint(p) {
  const t = toTile(p.x, p.y);
  stroke = { last: t, err: 0, dir: rotDir, n: 0 };
  if (tool.mode === 'rect') stroke.rect = { x0: t.x, y0: t.y, x1: t.x, y1: t.y };
  else { apply(t.x, t.y, rotDir); stroke.n++; }
  gesture = 'paint';
}
function movePaint(p) {
  const t = toTile(p.x, p.y);
  if (t.x === stroke.last.x && t.y === stroke.last.y) return;
  if (stroke.rect) { stroke.rect.x1 = t.x; stroke.rect.y1 = t.y; stroke.last = t; return; }
  for (const c of lineCells(stroke.last, t)) {
    if (tool.t === 3) {                                  /* tapis : le sens suit le trait */
      const d = dirOf(stroke.last, c);
      apply(stroke.last.x, stroke.last.y, d); stroke.dir = d; apply(c.x, c.y, d);
    } else apply(c.x, c.y, rotDir);
    stroke.last = c; stroke.n++;
  }
}
function endPaint(commit) {
  if (stroke && stroke.rect && commit) {
    const r = normRect(stroke.rect);
    const n = fn('game_apply_rect')(tool.t, r.x0, r.y0, r.x1, r.y1);
    if (n === 0) toast(tool.id === 'dig' ? 'Rien à creuser ici (roche uniquement)' : 'Aucun effet', true);
  }
  stroke = null; gesture = 'none';
}

canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('pointerdown', (e) => {
  if (menuOpen || over) return;
  canvas.setPointerCapture(e.pointerId);
  const p = { x: e.clientX, y: e.clientY, sx: e.clientX, sy: e.clientY, type: e.pointerType, btn: e.button };
  pointers.set(e.pointerId, p);
  if (pointers.size === 2) {                              /* 2 doigts : camera + pinch */
    if (stroke) endPaint(false);
    const [a, b] = [...pointers.values()];
    pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2 };
    gesture = 'pinch'; return;
  }
  if (pointers.size > 2) return;
  const wantsPan = e.pointerType === 'mouse' ? (e.button === 1 || e.button === 2 || keys.has(' ')) : false;
  if (wantsPan || tool.t === 0) { gesture = 'pan'; return; }
  if (e.pointerType === 'mouse') beginPaint(p);
  else gesture = 'wait';                                  /* tactile : on attend (2e doigt possible) */
});
canvas.addEventListener('pointermove', (e) => {
  const p = pointers.get(e.pointerId);
  if (e.pointerType === 'mouse') hover = toTile(e.clientX, e.clientY);
  if (!p) return;
  const dx = e.clientX - p.x, dy = e.clientY - p.y;
  p.x = e.clientX; p.y = e.clientY;
  if (gesture === 'pinch' && pointers.size >= 2) {
    const [a, b] = [...pointers.values()];
    const d = Math.hypot(a.x - b.x, a.y - b.y), cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
    cam.x -= (cx - pinch.cx) / cam.cell; cam.y -= (cy - pinch.cy) / cam.cell;
    if (pinch.d > 8) zoomAt(d / pinch.d, cx, cy);
    pinch.d = d; pinch.cx = cx; pinch.cy = cy; clampCam();
  } else if (gesture === 'pan') {
    cam.x -= dx / cam.cell; cam.y -= dy / cam.cell; clampCam();
  } else if (gesture === 'wait') {
    if (Math.hypot(p.x - p.sx, p.y - p.sy) > 8) beginPaint({ x: p.sx, y: p.sy }), movePaint(p);
  } else if (gesture === 'paint') movePaint(p);
});
function pointerEnd(e, commit) {
  const p = pointers.get(e.pointerId);
  if (!p) return;
  if (gesture === 'wait' && commit) {                     /* simple tap tactile */
    beginPaint(p); endPaint(true);
  } else if (gesture === 'paint') endPaint(commit);
  pointers.delete(e.pointerId);
  if (pointers.size === 0) gesture = 'none';
  else if (gesture === 'pinch') gesture = 'none';         /* le doigt restant est ignore jusqu'a sa levee */
}
canvas.addEventListener('pointerup', (e) => pointerEnd(e, true));
canvas.addEventListener('pointercancel', (e) => pointerEnd(e, false));
canvas.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse') hover = null; });
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  zoomAt(Math.exp(-e.deltaY * 0.0016), e.clientX, e.clientY);
}, { passive: false });

window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT') return;
  const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
  keys.add(k);
  if (k === ' ') { e.preventDefault(); if (!menuOpen) { paused = !paused; $('btn-speed').textContent = paused ? '⏸ pause' : `▶ x${speed}`; } }
  else if (k === 'Escape') { if (tool.t !== 0) selectTool(TOOLS[0]); else setMenu(!menuOpen); }
  else if (k === 'r') rotate();
  else if (k === '+' || k === '=') zoomAt(1.15, W / 2, H / 2);
  else if (k === '-') zoomAt(1 / 1.15, W / 2, H / 2);
  else { const t = TOOLS.find((t) => t.key === k); if (t) selectTool(t); }
});
window.addEventListener('keyup', (e) => keys.delete(e.key.length === 1 ? e.key.toLowerCase() : e.key));
window.addEventListener('blur', () => keys.clear());

function keyboardPan(dt) {
  let dx = 0, dy = 0;
  if (keys.has('ArrowLeft') || keys.has('a') || keys.has('q')) dx--;
  if (keys.has('ArrowRight') || keys.has('d')) dx++;
  if (keys.has('ArrowUp') || keys.has('w') || keys.has('z')) dy--;
  if (keys.has('ArrowDown') || keys.has('s')) dy++;
  if (dx || dy) { const s = (dt / 1000) * 16 * (26 / cam.cell + 0.4); cam.x += dx * s; cam.y += dy * s; clampCam(); }
}

/* ============================== Boucle ============================== */
function frame(now) {
  requestAnimationFrame(frame);
  const dt = Math.min(100, now - (lastT || now)); lastT = now;
  timeS = now / 1000;
  keyboardPan(dt);
  const run = !paused && !menuOpen && !over;
  fn('game_update')(run ? dt * speed : 0);
  render();
  if (now - lastHud > 200) { lastHud = now; updateHud(); }
  if (now - lastSave > 10000) { lastSave = now; saveGame(); }
}

/* ============================== Demarrage ============================== */
function wireUi() {
  buildToolbar(); selectTool(TOOLS[1]);
  $('btn-rot').addEventListener('click', rotate);
  $('btn-speed').addEventListener('click', () => {
    if (paused) paused = false; else if (speed === 1) speed = 2; else if (speed === 2) speed = 4; else speed = 1;
    $('btn-speed').textContent = `▶ x${speed}`;
  });
  $('btn-menu').addEventListener('click', () => setMenu(true));
  $('m-resume').addEventListener('click', () => setMenu(false));
  $('m-new').addEventListener('click', () => { if (confirm('Abandonner la partie en cours ?')) { newGame(); setMenu(false); } });
  $('go-new').addEventListener('click', () => { newGame(); });
  window.addEventListener('resize', resize);
  window.addEventListener('orientationchange', () => setTimeout(resize, 200));
  document.addEventListener('visibilitychange', () => { if (document.hidden) saveGame(); });
  window.addEventListener('pagehide', saveGame);

  let deferred = null;                                     /* bouton « Installer l'application » */
  window.addEventListener('beforeinstallprompt', (e) => { e.preventDefault(); deferred = e; $('m-install').hidden = false; });
  $('m-install').addEventListener('click', async () => { if (deferred) { deferred.prompt(); deferred = null; $('m-install').hidden = true; } });

  if ('serviceWorker' in navigator && (location.protocol === 'https:' || location.hostname === 'localhost')) {
    navigator.serviceWorker.register('service-worker.js').catch(() => { /* hors-ligne indisponible */ });
  }
}

async function boot() {
  wireUi(); resize();
  setMenu(true); $('m-status').textContent = 'Chargement du moteur WebAssembly…';
  $('m-resume').disabled = $('m-new').disabled = true;
  try {
    M = await IroncoreModule();
  } catch (err) {
    $('m-status').textContent = 'Erreur : impossible de charger game.wasm (' + err + ')';
    return;
  }
  fn('game_init')(1);
  refreshPointers();
  if (loadGame()) toast('Partie restaurée'); else newGame();
  $('m-resume').disabled = $('m-new').disabled = false;
  $('m-status').textContent = 'Moteur prêt · sauvegarde automatique locale';
  setMenu(false);
  window.__ironcore = { M, cam, get over() { return over; }, selectTool, TOOLS };   /* utile pour le debug / tests */
  requestAnimationFrame(frame);
}
boot();
