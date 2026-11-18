/*
 * map.c - generation procedurale de la montagne, utilitaires de grille, RNG.
 */
#include "game.h"

Game G;
const int DX[4] = {1, 0, -1, 0};
const int DY[4] = {0, 1, 0, -1};

/* ---------- RNG xorshift32 (etat dans G => deterministe et sauvegarde) ---------- */
uint32_t rnd(void) {
    uint32_t x = G.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    G.rng = x;
    return x;
}
int rnd_range(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); }
float rnd_f(void) { return (float)(rnd() & 0xFFFFFF) / 16777216.0f; }

/* ---------- Predicats ---------- */
int bld_solid(int b) {
    return b == B_DRILL || b == B_FORGE || b == B_WALL || b == B_TURRET || b == B_CORE;
}
int rock_solid(int t) {
    return t == T_ROCK || t == T_VEIN_IRON || t == T_VEIN_COAL || t == T_VEIN_CRYSTAL;
}
int walkable_dwarf(int x, int y) {
    if (!in_bounds(x, y)) return 0;
    const Tile *t = &G.tiles[IDX(x, y)];
    return t->type == T_FLOOR && !bld_solid(t->bld);
}

/* Revele (brouillard de guerre) un disque de tuiles */
void explore(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx, y = cy + dy;
            if (in_bounds(x, y) && dx * dx + dy * dy <= r * r) G.tiles[IDX(x, y)].explored = 1;
        }
}

/* ---------- Outils de generation ---------- */
static void carve(int x, int y) {
    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) return;
    Tile *t = &G.tiles[IDX(x, y)];
    t->type = T_FLOOR;
}

static void carve_disc(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r / 2) carve(cx + dx, cy + dy);
}

/* Marcheur ivre : creuse des galeries sinueuses */
static void drunk(int x, int y, int steps, int r) {
    int dir = (int)(rnd() & 3);
    for (int i = 0; i < steps; i++) {
        carve_disc(x, y, r);
        if (rnd() % 100 < 28) dir = (int)(rnd() & 3);
        x += DX[dir]; y += DY[dir];
        if (x < 3) x = 3;
        if (x > MAP_W - 4) x = MAP_W - 4;
        if (y < 3) y = 3;
        if (y > MAP_H - 4) y = MAP_H - 4;
    }
    carve_disc(x, y, 3);   /* petite salle au bout de la galerie */
}

/* Amas de minerai / eau / lave : ne remplace que la roche pleine */
static void blob(int x, int y, int n, int type) {
    for (int i = 0; i < n; i++) {
        if (x > 2 && y > 2 && x < MAP_W - 3 && y < MAP_H - 3) {
            Tile *t = &G.tiles[IDX(x, y)];
            if (t->type == T_ROCK) t->type = (uint8_t)type;
        }
        int d = (int)(rnd() & 3);
        x += DX[d]; y += DY[d];
        if (x < 3) x = 3;
        if (x > MAP_W - 4) x = MAP_W - 4;
        if (y < 3) y = 3;
        if (y > MAP_H - 4) y = MAP_H - 4;
    }
}

static int dist2(int ax, int ay, int bx, int by) {
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}

/* Place un amas a une position aleatoire situee a au moins `mind` tuiles de la base */
static void scatter(int count, int size_lo, int size_hi, int type, int mind) {
    for (int i = 0; i < count; i++) {
        for (int tries = 0; tries < 40; tries++) {
            int x = rnd_range(4, MAP_W - 5), y = rnd_range(4, MAP_H - 5);
            if (dist2(x, y, G.core_x, G.core_y) < mind * mind) continue;
            if (dist2(x, y, G.spawn_x, G.spawn_y) < 8 * 8) continue;
            blob(x, y, rnd_range(size_lo, size_hi), type);
            break;
        }
    }
}

/* Garantit au moins `min` cases du filon `type` dans le rayon revele au depart (15 tuiles).
 * Les amas "garantis" peuvent tomber dans une galerie deja creusee et ne rien produire. */
static void ensure_near_core(int type, int min) {
    for (int tries = 0; tries < 200; tries++) {
        int n = 0;
        for (int dy = -15; dy <= 15; dy++)
            for (int dx = -15; dx <= 15; dx++) {
                int x = G.core_x + dx, y = G.core_y + dy;
                if (dx * dx + dy * dy <= 15 * 15 && in_bounds(x, y) && G.tiles[IDX(x, y)].type == type) n++;
            }
        if (n >= min) return;
        int x = G.core_x + rnd_range(-14, 14), y = G.core_y + rnd_range(-14, 14);
        if (dist2(x, y, G.core_x, G.core_y) < 8 * 8 || dist2(x, y, G.core_x, G.core_y) > 14 * 14) continue;
        blob(x, y, 14, type);
    }
}

/* ---------- Generation complete ---------- */
void world_generate(uint32_t seed) {
    memset(&G, 0, sizeof(G));
    G.version = SAVE_VERSION;
    G.size_check = (uint32_t)sizeof(Game);
    G.rng = seed ? seed : 0xC0FFEEu;
    for (int i = 0; i < N_TILES; i++) { G.tiles[i].type = T_ROCK; G.tiles[i].claim = -1; }

    G.core_x = 18; G.core_y = 18;
    G.spawn_x = 86; G.spawn_y = 54;

    /* 1. Grande caverne de depart + galeries sinueuses */
    carve_disc(G.core_x, G.core_y, 8);
    for (int i = 0; i < 9; i++) {
        int sx = G.core_x + rnd_range(-6, 6), sy = G.core_y + rnd_range(-6, 6);
        drunk(sx, sy, rnd_range(120, 280), (rnd() % 3 == 0) ? 1 : 0);
    }

    /* 2. Caverne des monstres (coin sud-est) + tunnel jusqu'a la base */
    carve_disc(G.spawn_x, G.spawn_y, 4);
    int x = G.spawn_x, y = G.spawn_y;
    for (int step = 0; step < 4000; step++) {
        carve_disc(x, y, 1);
        if (dist2(x, y, G.core_x, G.core_y) <= 7 * 7) break;
        int dx = G.core_x - x, dy = G.core_y - y;
        int adx = abs(dx), ady = abs(dy);
        int r = (int)(rnd() % 100);
        int dir;
        if (r < 55)      dir = (adx >= ady) ? (dx > 0 ? DIR_E : DIR_W) : (dy > 0 ? DIR_S : DIR_N);
        else if (r < 80) dir = (adx >= ady && dy != 0) ? (dy > 0 ? DIR_S : DIR_N)
                                                        : (dx != 0 ? (dx > 0 ? DIR_E : DIR_W) : DIR_S);
        else             dir = (int)(rnd() & 3);
        x += DX[dir]; y += DY[dir];
        if (x < 3) x = 3;
        if (x > MAP_W - 4) x = MAP_W - 4;
        if (y < 3) y = 3;
        if (y > MAP_H - 4) y = MAP_H - 4;
    }

    /* 3. Filons de depart garantis (a 3-5 tuiles de creusement de la caverne) */
    blob(31, 15, 14, T_VEIN_IRON);
    blob(12, 8, 12, T_VEIN_IRON);
    blob(14, 29, 12, T_VEIN_COAL);
    blob(27, 28, 12, T_VEIN_COAL);
    ensure_near_core(T_VEIN_IRON, 8);
    ensure_near_core(T_VEIN_COAL, 8);
    /* 4. Filons lointains, cristaux runiques (rares et profonds), eau, lave */
    scatter(7, 8, 18, T_VEIN_IRON, 13);
    scatter(6, 8, 16, T_VEIN_COAL, 13);
    scatter(6, 4, 8, T_VEIN_CRYSTAL, 24);
    scatter(4, 6, 14, T_WATER, 14);
    scatter(6, 6, 14, T_LAVA, 16);

    /* 5. Le coeur de la forteresse (3x3) */
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            Tile *t = &G.tiles[IDX(G.core_x + dx, G.core_y + dy)];
            t->type = T_FLOOR; t->bld = B_CORE; t->built = 1;
        }
    G.core_hp = 500;

    /* 6. Etat initial */
    G.stock[IT_IRON] = 60; G.stock[IT_COAL] = 20; G.stock[IT_INGOT] = 12;
    G.wave_timer = 2700;                      /* 1ere vague apres 135 s */
    G.respawn_timer = 0;
    for (int i = 0; i < 4; i++) {
        Dwarf *d = &G.dw[i];
        d->alive = 1; d->hp = 30;
        d->x = d->px = (float)(G.core_x - 2 + i) + 0.5f;
        d->y = d->py = (float)(G.core_y + 3) + 0.5f;
    }
    /* Releve geologique de depart : rayon 15 = inclut les filons de fer/charbon garantis.
     * Les cristaux runiques (plus loin) restent a decouvrir en creusant. */
    explore(G.core_x, G.core_y, 15);
    clear_fail_cache();
}
