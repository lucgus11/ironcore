/*
 * sim.c - simulation : nains (jobs + A*), tapis roulants, foreuses, fonderies.
 */
#include "game.h"

#define DIG_TICKS 30
#define CONV_TICKS 5
#define DRILL_TICKS 36
#define FORGE_TICKS 50
#define DWARF_SPEED 0.16f

/* Duree de construction par type de batiment (0 = instantane) */
static const int BUILD_TICKS[8] = {0, 0, 30, 60, 25, 80, 15, 0};

/* Evite de relancer un A* inutile sur une cible inaccessible */
static uint32_t fail_until[N_TILES];
void clear_fail_cache(void) { memset(fail_until, 0, sizeof(fail_until)); }
int tile_unreachable(int idx) { return fail_until[idx] > G.tick; }

/* ============================ NAINS ============================ */

static void release(Dwarf *d, int di) {
    if (d->job != JOB_NONE && in_bounds(d->tx, d->ty)) {
        Tile *t = &G.tiles[IDX(d->tx, d->ty)];
        if (t->claim == di) t->claim = -1;
    }
    d->job = JOB_NONE; d->state = DW_IDLE; d->path_len = d->path_pos = 0; d->work = 0;
}

void dwarf_kill(int i) {
    Dwarf *d = &G.dw[i];
    if (!d->alive) return;
    release(d, i);
    d->alive = 0;
    for (int k = 0; k < N_TILES; k++) if (G.tiles[k].claim == i) G.tiles[k].claim = -1;
    fx_burst(d->x, d->y, 14, 3.0f, 0.7f, C_CRAWLER, G_SPARK);
    if (G.respawn_timer <= 0) G.respawn_timer = 600;
}

static int job_valid(const Dwarf *d) {
    if (!in_bounds(d->tx, d->ty)) return 0;
    const Tile *t = &G.tiles[IDX(d->tx, d->ty)];
    if (d->job == JOB_DIG)   return t->desig && rock_solid(t->type);
    if (d->job == JOB_BUILD) return t->bld != B_NONE && !t->built;
    return 0;
}

static void dig_complete(int x, int y) {
    Tile *t = &G.tiles[IDX(x, y)];
    switch (t->type) {
        case T_VEIN_IRON:    t->dep_type = IT_IRON;    t->dep_amt = 160; G.stock[IT_IRON] += 3; break;
        case T_VEIN_COAL:    t->dep_type = IT_COAL;    t->dep_amt = 160; G.stock[IT_COAL] += 3; break;
        case T_VEIN_CRYSTAL: t->dep_type = IT_CRYSTAL; t->dep_amt = 60;  G.stock[IT_CRYSTAL] += 2; break;
        default: break;
    }
    t->type = T_FLOOR; t->desig = 0; t->claim = -1;
    explore(x, y, 3);
    clear_fail_cache();            /* un nouveau passage peut rendre des cases voisines accessibles */
    fx_burst(x + 0.5f, y + 0.5f, 8, 2.2f, 0.5f, C_ROCK, G_DOT);
}

/* Choisit la tache la plus proche ; renvoie 1 si une tache a ete attribuee */
static int pick_job(Dwarf *d, int di) {
    int sx = (int)d->x, sy = (int)d->y;
    for (int attempt = 0; attempt < 4; attempt++) {
        int best = -1, bestd = 1 << 30, bestjob = JOB_NONE;
        for (int i = 0; i < N_TILES; i++) {
            const Tile *t = &G.tiles[i];
            if (t->claim >= 0 || fail_until[i] > G.tick) continue;
            int job;
            if (t->bld != B_NONE && !t->built) job = JOB_BUILD;
            else if (t->desig && rock_solid(t->type)) job = JOB_DIG;
            else continue;
            int dd = abs(i % MAP_W - sx) + abs(i / MAP_W - sy) - (job == JOB_BUILD ? 6 : 0);
            if (dd < bestd) { bestd = dd; best = i; bestjob = job; }
        }
        if (best < 0) return 0;

        int bx = best % MAP_W, by = best / MAP_W;
        int order[4] = {0, 1, 2, 3}, dist[4];
        for (int k = 0; k < 4; k++) dist[k] = abs(bx + DX[k] - sx) + abs(by + DY[k] - sy);
        for (int a = 0; a < 3; a++)                       /* tri par distance */
            for (int b = a + 1; b < 4; b++)
                if (dist[order[b]] < dist[order[a]]) { int tt = order[a]; order[a] = order[b]; order[b] = tt; }

        int len = -1;
        for (int k = 0; k < 4 && len < 0; k++) {
            int nx = bx + DX[order[k]], ny = by + DY[order[k]];
            if (!walkable_dwarf(nx, ny)) continue;
            if (nx == sx && ny == sy) { len = 0; break; }
            len = find_path(sx, sy, nx, ny, 0, d->path, PATH_CAP);
        }
        if (len >= 0) {
            d->job = (uint8_t)bestjob; d->tx = (int16_t)bx; d->ty = (int16_t)by;
            d->path_len = (int16_t)len; d->path_pos = 0; d->work = 0;
            d->state = len > 0 ? DW_MOVE : DW_WORK;
            G.tiles[best].claim = (int8_t)di;
            return 1;
        }
        fail_until[best] = G.tick + 120;                  /* inaccessible : on reessaie dans 6 s */
    }
    return 0;
}

void dwarves_update(void) {
    for (int i = 0; i < MAX_DWARVES; i++) {
        Dwarf *d = &G.dw[i];
        if (!d->alive) continue;
        if ((G.tick & 3) == 0) explore((int)d->x, (int)d->y, 5);

        switch (d->state) {
        case DW_IDLE:
            if (d->idle_cd) { d->idle_cd--; break; }
            if (!pick_job(d, i)) d->idle_cd = (uint16_t)(10 + rnd() % 10);
            break;

        case DW_MOVE: {
            if (!job_valid(d)) { release(d, i); break; }
            if (d->path_pos >= d->path_len) { d->state = DW_WORK; d->work = 0; break; }
            int idx = d->path[d->path_pos];
            if (!walkable_dwarf(idx % MAP_W, idx / MAP_W)) { release(d, i); d->idle_cd = 5; break; }
            float tx = (float)(idx % MAP_W) + 0.5f, ty = (float)(idx / MAP_W) + 0.5f;
            float dx = tx - d->x, dy = ty - d->y, dist = sqrtf(dx * dx + dy * dy);
            if (dist <= DWARF_SPEED) { d->x = tx; d->y = ty; d->path_pos++; }
            else { d->x += dx / dist * DWARF_SPEED; d->y += dy / dist * DWARF_SPEED; }
            break;
        }

        case DW_WORK: {
            if (!job_valid(d)) { release(d, i); break; }
            Tile *t = &G.tiles[IDX(d->tx, d->ty)];
            d->work++;
            if (d->job == JOB_DIG) {
                if (d->work % 7 == 0)
                    fx_burst(d->tx + 0.5f, d->ty + 0.5f, 2, 1.8f, 0.35f, C_SPARK, G_SPARK);
                if (d->work >= DIG_TICKS) { dig_complete(d->tx, d->ty); release(d, i); }
            } else {
                t->build_prog++;
                if (d->work % 9 == 0)
                    fx_burst(d->tx + 0.5f, d->ty + 0.5f, 2, 1.5f, 0.3f, C_EMBER, G_SPARK);
                if (t->build_prog >= BUILD_TICKS[t->bld]) {
                    t->built = 1;
                    fx_burst(d->tx + 0.5f, d->ty + 0.5f, 10, 2.5f, 0.5f, C_SPARK, G_SPARK);
                    release(d, i);
                }
            }
            break;
        }
        }
    }
}

/* ======================= TAPIS / MACHINES ======================= */

/* Tente de deposer un objet dans le batiment en (x,y), provenant de (fx,fy) */
static int deliver(int x, int y, int item, int fx, int fy) {
    if (!in_bounds(x, y)) return 0;
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->type != T_FLOOR || t->bld == B_NONE || !t->built) return 0;
    switch (t->bld) {
    case B_CONVEYOR: {
        if (t->item) return 0;
        if (x + DX[t->bdir] == fx && y + DY[t->bdir] == fy) return 0;   /* ne pas refluer */
        t->item = (uint8_t)item; t->prog = 0;
        return 1;
    }
    case B_FORGE:
        if (item == IT_IRON && t->in_iron < 8) { t->in_iron++; return 1; }
        if (item == IT_COAL && t->in_coal < 8) { t->in_coal++; return 1; }
        return 0;
    case B_CORE:
        G.stock[item]++;
        return 1;
    default: return 0;
    }
}

static int try_push(int x, int y, int item) {
    int start = (int)((G.tick / 3 + (uint32_t)x + (uint32_t)y) & 3);
    for (int k = 0; k < 4; k++) {
        int d = (start + k) & 3;
        if (deliver(x + DX[d], y + DY[d], item, x, y)) return 1;
    }
    return 0;
}

void conveyors_update(void) {
    for (int i = 0; i < N_TILES; i++) {
        Tile *t = &G.tiles[i];
        if (t->bld != B_CONVEYOR || !t->built || !t->item) continue;
        if (t->prog < CONV_TICKS) { t->prog++; continue; }
        int x = i % MAP_W, y = i / MAP_W;
        if (deliver(x + DX[t->bdir], y + DY[t->bdir], t->item, x, y)) t->item = 0;
    }
}

void machines_update(void) {
    for (int i = 0; i < N_TILES; i++) {
        Tile *t = &G.tiles[i];
        if (t->flash) t->flash--;
        if (!t->built) continue;
        int x = i % MAP_W, y = i / MAP_W;
        if (t->bld == B_DRILL) {
            if (t->item) { if (try_push(x, y, t->item)) t->item = 0; }
            else if (t->dep_amt > 0 && ++t->timer >= DRILL_TICKS) {
                t->timer = 0; t->item = t->dep_type; t->dep_amt--;
                if ((G.tick & 3) == 0) fx_burst(x + 0.5f, y + 0.5f, 1, 1.2f, 0.3f, C_ROCK, G_DOT);
            }
        } else if (t->bld == B_FORGE) {
            if (t->out_ingot) { if (try_push(x, y, IT_INGOT)) t->out_ingot = 0; }
            else if (t->in_iron && t->in_coal) {
                if (++t->timer >= FORGE_TICKS) {
                    t->timer = 0; t->in_iron--; t->in_coal--; t->out_ingot = 1;
                    fx_burst(x + 0.5f, y + 0.5f, 6, 2.2f, 0.6f, C_EMBER, G_SPARK);
                }
            } else t->timer = 0;
        }
    }
}
