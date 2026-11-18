/*
 * combat.c - vagues de monstres, IA ennemie, tourelles, pieges.
 */
#include "game.h"

static const int   EN_HP[2]    = {22, 80};
static const float EN_SPEED[2] = {0.085f, 0.050f};
static const int   EN_DMG[2]   = {4, 14};

#define TURRET_RANGE 9.5f

/* ---------- Batiments detruits ---------- */
void destroy_building(int x, int y) {
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->bld == B_CORE) return;
    fx_burst(x + 0.5f, y + 0.5f, 14, 3.0f, 0.7f, C_EMBER, G_SPARK);
    t->bld = B_NONE; t->bdir = 0; t->built = 0; t->item = 0; t->prog = 0; t->hp = 0;
    t->in_iron = t->in_coal = t->out_ingot = 0; t->timer = 0; t->build_prog = 0;
    if (t->claim >= 0) t->claim = -1;
}

/* ---------- Vagues ---------- */
static void spawn_enemy(int type) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        if (e->alive) continue;
        int x = G.spawn_x, y = G.spawn_y;
        for (int tries = 0; tries < 12; tries++) {
            int cx = G.spawn_x + rnd_range(-3, 3), cy = G.spawn_y + rnd_range(-3, 3);
            if (in_bounds(cx, cy) && G.tiles[IDX(cx, cy)].type == T_FLOOR) { x = cx; y = cy; break; }
        }
        memset(e, 0, sizeof(*e));
        e->alive = 1; e->type = (uint8_t)type; e->hp = (int16_t)EN_HP[type];
        e->x = e->px = x + 0.5f; e->y = e->py = y + 0.5f;
        e->age = (uint16_t)(rnd() % 80);
        return;
    }
}

void waves_update(void) {
    if (G.state) return;

    if (G.wave_timer > 0) G.wave_timer--;
    if (G.wave_timer <= 0) {
        G.wave++;
        G.wave_spawn_left = 3 + G.wave * 2;
        if (G.wave_spawn_left > 40) G.wave_spawn_left = 40;
        int interval = 1800 - G.wave * 60;
        G.wave_timer = interval < 1000 ? 1000 : interval;
        G.spawn_cd = 0;
    }
    if (G.wave_spawn_left > 0 && --G.spawn_cd <= 0) {
        int brute = (G.wave >= 3 && rnd() % 100 < 25);
        spawn_enemy(brute ? EN_BRUTE : EN_CRAWLER);
        G.wave_spawn_left--;
        G.spawn_cd = 14;
    }

    /* Renforts : un nain reapparait au coeur si la colonie est trop petite */
    int alive = 0;
    for (int i = 0; i < MAX_DWARVES; i++) alive += G.dw[i].alive;
    if (alive < 4 && G.respawn_timer > 0 && --G.respawn_timer == 0) {
        for (int i = 0; i < MAX_DWARVES; i++) {
            Dwarf *d = &G.dw[i];
            if (d->alive) continue;
            memset(d, 0, sizeof(*d));
            d->alive = 1; d->hp = 30;
            d->x = d->px = G.core_x + 0.5f; d->y = d->py = G.core_y + 3.5f;
            break;
        }
        if (alive + 1 < 4) G.respawn_timer = 600;
    }
}

/* ---------- Ennemis ---------- */
static void enemy_die(Enemy *e) {
    e->alive = 0; G.kills++;
    G.stock[IT_IRON] += 1;
    if (rnd() % 100 < 12) G.stock[IT_CRYSTAL] += 1;
    fx_burst(e->x, e->y, 12, 3.0f, 0.6f, e->type == EN_BRUTE ? C_BRUTE : C_CRAWLER, G_SPARK);
}

void enemies_update(void) {
    if (G.core_hp <= 0 && !G.state) { G.state = 1; fx_burst(G.core_x + 0.5f, G.core_y + 0.5f, 60, 6.0f, 1.5f, C_EMBER, G_SPARK); }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        if (!e->alive) continue;
        if (e->hp <= 0) { enemy_die(e); continue; }
        if (e->atk_cd) e->atk_cd--;
        if (G.state) continue;
        e->age++;
        int dmg = EN_DMG[e->type];

        /* 1. Un nain a portee ? on l'attaque */
        int tgt = -1;
        for (int k = 0; k < MAX_DWARVES; k++) {
            const Dwarf *d = &G.dw[k];
            if (!d->alive) continue;
            float dx = d->x - e->x, dy = d->y - e->y;
            if (dx * dx + dy * dy < 1.6f * 1.6f) { tgt = k; break; }
        }
        if (tgt >= 0) {
            if (!e->atk_cd) {
                e->atk_cd = 18;
                G.dw[tgt].hp -= (int16_t)dmg;
                fx_burst(G.dw[tgt].x, G.dw[tgt].y, 4, 2.0f, 0.4f, C_CRAWLER, G_SPARK);
                if (G.dw[tgt].hp <= 0) dwarf_kill(tgt);
            }
            continue;
        }

        /* 2. Chemin vers le coeur (recalcule regulierement) */
        if (e->age % 80 == 0) e->path_pos = e->path_len;
        if (e->path_pos >= e->path_len) {
            if (e->repath) { e->repath--; continue; }
            int len = find_path((int)e->x, (int)e->y, G.core_x, G.core_y, 1, e->path, PATH_CAP);
            e->path_len = (int16_t)(len < 0 ? 0 : len);
            e->path_pos = 0;
            e->repath = 10;
            if (len <= 0) continue;
        }

        /* 3. Obstacle solide devant ? on l'attaque, sinon on avance */
        int nidx = e->path[e->path_pos];
        int nx = nidx % MAP_W, ny = nidx / MAP_W;
        Tile *nt = &G.tiles[nidx];
        if (bld_solid(nt->bld)) {
            if (!e->atk_cd) {
                e->atk_cd = 22;
                fx_burst(nx + 0.5f, ny + 0.5f, 3, 2.0f, 0.3f, C_SPARK, G_SPARK);
                if (nt->bld == B_CORE) G.core_hp -= dmg;
                else { nt->flash = 4; if (nt->hp <= (uint16_t)dmg) destroy_building(nx, ny); else nt->hp -= (uint16_t)dmg; }
            }
            continue;
        }
        float tx = nx + 0.5f, ty = ny + 0.5f;
        float dx = tx - e->x, dy = ty - e->y, dist = sqrtf(dx * dx + dy * dy);
        float sp = EN_SPEED[e->type];
        if (dist <= sp) {
            e->x = tx; e->y = ty; e->path_pos++;
            if (nt->bld == B_TRAP && nt->built) {              /* piege a pointes */
                e->hp -= 20;
                fx_burst(tx, ty, 6, 2.5f, 0.4f, C_TRAP, G_SPARK);
                if (nt->hp <= 1) destroy_building(nx, ny); else nt->hp--;
            }
        } else { e->x += dx / dist * sp; e->y += dy / dist * sp; }
    }
}

/* ---------- Tourelles : hitscan, munitions prises dans le stock du coeur ---------- */
void turrets_update(void) {
    if (G.state) return;
    for (int i = 0; i < N_TILES; i++) {
        Tile *t = &G.tiles[i];
        if (t->bld != B_TURRET || !t->built) continue;
        if (t->timer) { t->timer--; continue; }
        float tx = (float)(i % MAP_W) + 0.5f, ty = (float)(i / MAP_W) + 0.5f;

        int best = -1; float bd = TURRET_RANGE * TURRET_RANGE;
        for (int k = 0; k < MAX_ENEMIES; k++) {
            const Enemy *e = &G.en[k];
            if (!e->alive || e->hp <= 0) continue;
            float dx = e->x - tx, dy = e->y - ty, d2 = dx * dx + dy * dy;
            if (d2 < bd) { bd = d2; best = k; }
        }
        if (best < 0) continue;

        int dmg, color;
        if (G.stock[IT_CRYSTAL] > 0) {                     /* tir runique : degats x2.7 */
            dmg = 16; color = C_CRYSTAL;
            if (t->prog++ % 6 == 0) G.stock[IT_CRYSTAL]--;
        } else if (G.stock[IT_INGOT] > 0) {
            dmg = 6; color = C_SPARK;
            if (t->prog++ % 5 == 0) G.stock[IT_INGOT]--;
        } else continue;                                    /* plus de munitions */

        Enemy *e = &G.en[best];
        e->hp -= (int16_t)dmg;
        t->timer = 8;
        for (int s = 1; s <= 6; s++) {                      /* trace du tir */
            float f = (float)s / 7.0f;
            fx_spawn(tx + (e->x - tx) * f, ty + (e->y - ty) * f, 0, 0, 0.12f, color, G_BOLT);
        }
        fx_burst(e->x, e->y, 3, 1.8f, 0.3f, color, G_SPARK);
    }
}
