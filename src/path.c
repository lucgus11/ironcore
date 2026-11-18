/*
 * path.c - pathfinding A* sur grille 4-connexe (tas binaire, buffers statiques).
 *   mode 0 : nains    -> sol libre uniquement
 *   mode 1 : ennemis  -> sol libre, constructions solides franchissables a cout eleve
 *                        (l'ennemi les attaque quand il bute dessus)
 */
#include "game.h"

typedef struct { int32_t f; uint16_t idx; } HNode;

static uint32_t stamp = 0;
static uint32_t seen[N_TILES], closed_[N_TILES];
static int32_t gscore[N_TILES];
static uint16_t parent_[N_TILES];
static uint16_t tmp_path[N_TILES];
static HNode heap[N_TILES * 4 + 8];
static int hn;

static void hpush(int32_t f, uint16_t idx) {
    int i = hn++;
    heap[i].f = f; heap[i].idx = idx;
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (heap[p].f <= heap[i].f) break;
        HNode t = heap[p]; heap[p] = heap[i]; heap[i] = t;
        i = p;
    }
}

static HNode hpop(void) {
    HNode top = heap[0];
    heap[0] = heap[--hn];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < hn && heap[l].f < heap[m].f) m = l;
        if (r < hn && heap[r].f < heap[m].f) m = r;
        if (m == i) break;
        HNode t = heap[m]; heap[m] = heap[i]; heap[i] = t;
        i = m;
    }
    return top;
}

/* Cout d'entree dans une tuile (-1 = infranchissable) */
static int step_cost(int mode, int x, int y) {
    if (!in_bounds(x, y)) return -1;
    const Tile *t = &G.tiles[IDX(x, y)];
    if (t->type != T_FLOOR) return -1;
    if (mode == 0) return bld_solid(t->bld) ? -1 : 1;
    if (t->bld == B_CORE) return 1;
    if (bld_solid(t->bld)) return 9;
    return 1;
}

/* Renvoie le nombre de pas (sans la case de depart), 0 si depart==arrivee,
 * -1 si aucun chemin. Au plus `cap` pas sont ecrits dans `out`. */
int find_path(int sx, int sy, int tx, int ty, int mode, uint16_t *out, int cap) {
    if (!in_bounds(sx, sy) || !in_bounds(tx, ty)) return -1;
    if (sx == tx && sy == ty) return 0;
    if (step_cost(mode, tx, ty) < 0) return -1;

    stamp++;
    hn = 0;
    int s = IDX(sx, sy), g = IDX(tx, ty);
    seen[s] = stamp; gscore[s] = 0; parent_[s] = (uint16_t)s;
    hpush(abs(sx - tx) + abs(sy - ty), (uint16_t)s);

    int found = 0;
    while (hn > 0) {
        HNode n = hpop();
        int cur = n.idx;
        if (closed_[cur] == stamp) continue;
        closed_[cur] = stamp;
        if (cur == g) { found = 1; break; }
        int cx = cur % MAP_W, cy = cur / MAP_W;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            int c = step_cost(mode, nx, ny);
            if (c < 0) continue;
            int ni = IDX(nx, ny);
            if (closed_[ni] == stamp) continue;
            int32_t ng = gscore[cur] + c;
            if (seen[ni] != stamp || ng < gscore[ni]) {
                seen[ni] = stamp; gscore[ni] = ng; parent_[ni] = (uint16_t)cur;
                hpush(ng + abs(nx - tx) + abs(ny - ty), (uint16_t)ni);
            }
        }
    }
    if (!found) return -1;

    int n = 0, cur = g;
    while (cur != s) { tmp_path[n++] = (uint16_t)cur; cur = parent_[cur]; }
    int stored = n < cap ? n : cap;
    for (int i = 0; i < stored; i++) out[i] = tmp_path[n - 1 - i];
    return stored;
}
