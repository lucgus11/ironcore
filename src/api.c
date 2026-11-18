/*
 * api.c - interface exportee vers JavaScript : boucle de jeu, outils du joueur,
 * eclairage dynamique, particules, tampons de rendu, sauvegarde/chargement.
 *
 * Le JS ne contient AUCUNE regle de jeu : il lit les tampons ci-dessous et dessine.
 */
#include "game.h"

#define TICK_MS (1000.0f / SIM_HZ)

/* ============================ PARTICULES ============================ */
static Particle P[MAX_PARTICLES];
static int p_next = 0;

void fx_spawn(float x, float y, float vx, float vy, float life, int color, int glyph) {
    Particle *p = &P[p_next];
    p_next = (p_next + 1) % MAX_PARTICLES;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->maxlife = life;
    p->color = (uint8_t)color; p->glyph = (uint8_t)glyph;
}

void fx_burst(float x, float y, int n, float speed, float life, int color, int glyph) {
    for (int i = 0; i < n; i++) {
        float a = rnd_f() * 6.2831853f, s = speed * (0.3f + rnd_f() * 0.7f);
        fx_spawn(x, y, cosf(a) * s, sinf(a) * s, life * (0.6f + 0.4f * rnd_f()), color, glyph);
    }
}

static void fx_update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &P[i];
        if (p->life <= 0) continue;
        p->x += p->vx * dt; p->y += p->vy * dt;
        p->vx *= 0.96f; p->vy *= 0.96f;
        p->life -= dt;
    }
}

/* ============================ ECLAIRAGE ============================ */
static uint16_t lt[N_TILES];

static void add_light(int cx, int cy, int r, int inten) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx, y = cy + dy, d2 = dx * dx + dy * dy;
            if (!in_bounds(x, y) || d2 > r * r) continue;
            float f = 1.0f - sqrtf((float)d2) / (float)r;
            lt[IDX(x, y)] += (uint16_t)(inten * f * f);
        }
}

static void compute_light(void) {
    memset(lt, 0, sizeof(lt));
    add_light(G.core_x, G.core_y, 10, 170);
    for (int i = 0; i < N_TILES; i++) {
        const Tile *t = &G.tiles[i];
        int x = i % MAP_W, y = i / MAP_W;
        if (t->type == T_LAVA && t->explored) add_light(x, y, 4, 150);
        else if (t->built && t->bld == B_FORGE) add_light(x, y, 6, (t->in_iron && t->in_coal) ? 230 : 130);
        else if (t->built && t->bld == B_TURRET) add_light(x, y, 5, 110);
        else if (t->built && t->bld == B_DRILL) add_light(x, y, 2, 50);
    }
    for (int i = 0; i < MAX_DWARVES; i++)
        if (G.dw[i].alive) add_light((int)G.dw[i].x, (int)G.dw[i].y, 5, 130);
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (G.en[i].alive) add_light((int)G.en[i].x, (int)G.en[i].y, 3, 70);
    for (int i = 0; i < N_TILES; i++) {
        int v = lt[i] + (G.tiles[i].explored ? 58 : 0);   /* ambiance : la roche reste lisible */
        G.tiles[i].light = (uint8_t)(v > 255 ? 255 : v);
    }
}

/* ============================ TAMPONS DE RENDU ============================ */
static uint8_t R_glyph[N_TILES], R_color[N_TILES], R_flags[N_TILES], R_light[N_TILES], R_item[N_TILES];
static float   R_ent[(MAX_DWARVES + MAX_ENEMIES) * 5];
static int     R_ent_n = 0;
static float   alpha = 0;

static void prepare_render(void) {
    for (int i = 0; i < N_TILES; i++) {
        const Tile *t = &G.tiles[i];
        R_glyph[i] = G_NONE; R_color[i] = C_NONE; R_flags[i] = 0; R_item[i] = 0; R_light[i] = t->light;
        if (!t->explored) { R_light[i] = 0; continue; }
        switch (t->type) {
            case T_ROCK:        R_glyph[i] = G_ROCK;  R_color[i] = C_ROCK; break;
            case T_VEIN_IRON:   R_glyph[i] = G_VEIN;  R_color[i] = C_IRON; break;
            case T_VEIN_COAL:   R_glyph[i] = G_VEIN;  R_color[i] = C_COAL; break;
            case T_VEIN_CRYSTAL:R_glyph[i] = G_VEIN;  R_color[i] = C_CRYSTAL; break;
            case T_WATER:       R_glyph[i] = G_WATER; R_color[i] = C_WATER; break;
            case T_LAVA:        R_glyph[i] = G_LAVA;  R_color[i] = C_LAVA; break;
            default:
                R_glyph[i] = G_FLOOR; R_color[i] = C_FLOOR;
                if (t->dep_amt > 0) R_color[i] = (t->dep_type == IT_IRON) ? C_IRON : (t->dep_type == IT_COAL) ? C_COAL : C_CRYSTAL;
        }
        switch (t->bld) {
            case B_CONVEYOR: R_glyph[i] = (uint8_t)(G_CONV_E + t->bdir); R_color[i] = C_CONV; R_item[i] = t->item; break;
            case B_DRILL:    R_glyph[i] = G_DRILL;  R_color[i] = C_DRILL;  R_item[i] = t->item; break;
            case B_FORGE:    R_glyph[i] = G_FORGE;  R_color[i] = C_FORGE;  break;
            case B_WALL:     R_glyph[i] = G_WALL;   R_color[i] = C_WALL;   break;
            case B_TURRET:   R_glyph[i] = G_TURRET; R_color[i] = C_TURRET; break;
            case B_TRAP:     R_glyph[i] = G_TRAP;   R_color[i] = C_TRAP;   break;
            case B_CORE:     R_glyph[i] = G_CORE;   R_color[i] = C_CORE;   break;
            default: break;
        }
        if (t->bld && !t->built) R_flags[i] |= 1;                 /* plan de construction */
        if (t->desig && rock_solid(t->type)) {                    /* zone a creuser */
            R_flags[i] |= 2;
            if (tile_unreachable(i)) R_flags[i] |= 8;             /* ... mais inaccessible */
        }
        if (t->flash) R_flags[i] |= 4;                            /* degats recus */
    }

    R_ent_n = 0;
    for (int i = 0; i < MAX_DWARVES; i++) {
        const Dwarf *d = &G.dw[i];
        if (!d->alive) continue;
        float *e = &R_ent[R_ent_n++ * 5];
        e[0] = d->px + (d->x - d->px) * alpha; e[1] = d->py + (d->y - d->py) * alpha;
        e[2] = 1; e[3] = d->hp / 30.0f; e[4] = d->state;
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *n = &G.en[i];
        if (!n->alive) continue;
        float *e = &R_ent[R_ent_n++ * 5];
        e[0] = n->px + (n->x - n->px) * alpha; e[1] = n->py + (n->y - n->py) * alpha;
        e[2] = n->type == EN_BRUTE ? 3.0f : 2.0f;
        e[3] = n->hp / (n->type == EN_BRUTE ? 80.0f : 22.0f); e[4] = 0;
    }
}

/* ============================ BOUCLE DE JEU ============================ */
static float acc = 0;

static void sim_tick(void) {
    for (int i = 0; i < MAX_DWARVES; i++) { G.dw[i].px = G.dw[i].x; G.dw[i].py = G.dw[i].y; }
    for (int i = 0; i < MAX_ENEMIES; i++) { G.en[i].px = G.en[i].x; G.en[i].py = G.en[i].y; }
    G.tick++;
    if (!G.state) {
        dwarves_update();
        conveyors_update();
        machines_update();
        turrets_update();
        waves_update();
    }
    enemies_update();
    if ((G.tick & 3) == 0) compute_light();
}

EXPORT void game_init(unsigned int seed) {
    world_generate(seed);
    memset(P, 0, sizeof(P));
    acc = 0; alpha = 0;
    compute_light();
    prepare_render();
}

/* dt_ms : temps reel ecoule (deja multiplie par la vitesse de jeu par le JS) */
EXPORT void game_update(float dt_ms) {
    if (dt_ms > 250.0f) dt_ms = 250.0f;
    acc += dt_ms;
    int steps = 0;
    while (acc >= TICK_MS && steps < 10) { acc -= TICK_MS; steps++; sim_tick(); }
    if (steps == 10) acc = 0;
    alpha = acc / TICK_MS;
    fx_update(dt_ms / 1000.0f);
    prepare_render();
}

/* ============================ OUTILS DU JOUEUR ============================ */
static int g_err = ERR_NONE;

/*                              none conv  drill forge wall turret trap core */
static const int COST_IRON[8]  = {0,   1,   8,    15,   0,   0,     0,   0};
static const int COST_INGOT[8] = {0,   0,   0,    0,    1,   6,     2,   0};
static const int BLD_HP[8]     = {0,   0,   40,   50,   80,  60,    10,  0};
static const int BUILD_INSTANT[8] = {1, 1, 0, 0, 0, 0, 0, 1};

static int dwarf_on_tile(int x, int y) {
    for (int i = 0; i < MAX_DWARVES; i++)
        if (G.dw[i].alive && (int)G.dw[i].x == x && (int)G.dw[i].y == y) return 1;
    return 0;
}

static int fail(int e) { g_err = e; return 0; }

static int place_building(int bt, int x, int y, int dir) {
    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) return fail(ERR_INVALID);
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->type != T_FLOOR || !t->explored) return fail(ERR_INVALID);

    if (bt == B_CONVEYOR && t->bld == B_CONVEYOR) {          /* rotation gratuite */
        t->bdir = (uint8_t)dir;
        return 1;
    }
    if (t->bld != B_NONE) return fail(ERR_OCCUPIED);
    if (bt == B_DRILL && t->dep_amt == 0) return fail(ERR_INVALID);
    if (bld_solid(bt) && dwarf_on_tile(x, y)) return fail(ERR_OCCUPIED);
    if (G.stock[IT_IRON] < COST_IRON[bt] || G.stock[IT_INGOT] < COST_INGOT[bt]) return fail(ERR_RESOURCES);

    G.stock[IT_IRON] -= COST_IRON[bt];
    G.stock[IT_INGOT] -= COST_INGOT[bt];
    t->bld = (uint8_t)bt; t->bdir = (uint8_t)dir;
    t->built = (uint8_t)BUILD_INSTANT[bt];
    t->hp = (uint16_t)BLD_HP[bt];
    t->item = 0; t->prog = 0; t->timer = 0; t->build_prog = 0;
    t->in_iron = t->in_coal = t->out_ingot = 0;
    fx_burst(x + 0.5f, y + 0.5f, 4, 1.5f, 0.3f, C_SPARK, G_SPARK);
    return 1;
}

static int demolish(int x, int y) {
    if (!in_bounds(x, y)) return fail(ERR_INVALID);
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->bld == B_NONE || t->bld == B_CORE) return fail(ERR_INVALID);
    int b = t->bld, full = !t->built;                          /* plan : remboursement total */
    G.stock[IT_IRON]  += full ? COST_IRON[b]  : COST_IRON[b] / 2;
    G.stock[IT_INGOT] += full ? COST_INGOT[b] : COST_INGOT[b] / 2;
    t->bld = B_NONE; t->built = 0; t->item = 0; t->hp = 0; t->claim = -1;
    t->in_iron = t->in_coal = t->out_ingot = 0; t->build_prog = 0;
    return 1;
}

/* Applique un outil sur une tuile. Renvoie 1 si succes, sinon 0 (voir game_last_error). */
EXPORT int game_apply_tool(int tool, int x, int y, int dir) {
    g_err = ERR_NONE;
    if (G.state) return fail(ERR_OVER);
    if (!in_bounds(x, y)) return fail(ERR_INVALID);
    Tile *t = &G.tiles[IDX(x, y)];
    dir &= 3;
    switch (tool) {
    case TOOL_DIG:
        if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1 || !rock_solid(t->type) || !t->explored) return fail(ERR_INVALID);
        t->desig = 1; return 1;
    case TOOL_CANCEL:
        if (!t->desig) return fail(ERR_INVALID);
        t->desig = 0; return 1;
    case TOOL_CONVEYOR: return place_building(B_CONVEYOR, x, y, dir);
    case TOOL_DRILL:    return place_building(B_DRILL, x, y, dir);
    case TOOL_FORGE:    return place_building(B_FORGE, x, y, dir);
    case TOOL_WALL:     return place_building(B_WALL, x, y, dir);
    case TOOL_TURRET:   return place_building(B_TURRET, x, y, dir);
    case TOOL_TRAP:     return place_building(B_TRAP, x, y, dir);
    case TOOL_DEMOLISH: return demolish(x, y);
    default: return fail(ERR_INVALID);
    }
}

/* Applique un outil sur un rectangle ; renvoie le nombre de tuiles modifiees */
EXPORT int game_apply_rect(int tool, int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int a = x0; x0 = x1; x1 = a; }
    if (y0 > y1) { int a = y0; y0 = y1; y1 = a; }
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) n += game_apply_tool(tool, x, y, 0);
    return n;
}

EXPORT int game_last_error(void) { return g_err; }

/* ============================ LECTURE D'ETAT ============================ */
EXPORT int game_map_w(void) { return MAP_W; }
EXPORT int game_map_h(void) { return MAP_H; }
EXPORT uint8_t *game_ptr_glyph(void) { return R_glyph; }
EXPORT uint8_t *game_ptr_color(void) { return R_color; }
EXPORT uint8_t *game_ptr_flags(void) { return R_flags; }
EXPORT uint8_t *game_ptr_light(void) { return R_light; }
EXPORT uint8_t *game_ptr_item(void)  { return R_item; }
EXPORT float *game_ptr_entities(void) { return R_ent; }
EXPORT int game_entity_count(void) { return R_ent_n; }
EXPORT int game_entity_stride(void) { return 5; }
EXPORT Particle *game_ptr_particles(void) { return P; }
EXPORT int game_particle_count(void) { return MAX_PARTICLES; }
EXPORT int game_particle_stride(void) { return (int)sizeof(Particle); }
EXPORT int game_core_x(void) { return G.core_x; }
EXPORT int game_core_y(void) { return G.core_y; }

/* 0 fer, 1 charbon, 2 cristal, 3 lingots, 4 PV coeur, 5 vague, 6 s avant vague,
 * 7 nains, 8 ennemis, 9 etat (1 = perdu), 10 victimes, 11 temps (s), 12 ennemis restants a sortir */
EXPORT int game_stat(int id) {
    int n;
    switch (id) {
    case 0: return G.stock[IT_IRON];
    case 1: return G.stock[IT_COAL];
    case 2: return G.stock[IT_CRYSTAL];
    case 3: return G.stock[IT_INGOT];
    case 4: return G.core_hp < 0 ? 0 : G.core_hp;
    case 5: return G.wave;
    case 6: return G.wave_timer / SIM_HZ;
    case 7: n = 0; for (int i = 0; i < MAX_DWARVES; i++) n += G.dw[i].alive; return n;
    case 8: n = 0; for (int i = 0; i < MAX_ENEMIES; i++) n += G.en[i].alive; return n;
    case 9: return G.state;
    case 10: return G.kills;
    case 11: return (int)(G.tick / SIM_HZ);
    case 12: return G.wave_spawn_left;
    default: return 0;
    }
}

/* ============================ SAUVEGARDE ============================ */
static uint8_t save_buf[sizeof(Game)];

EXPORT uint8_t *game_buffer(void) { return save_buf; }
EXPORT int game_save_size(void) { return (int)sizeof(Game); }

EXPORT int game_save(void) {
    memcpy(save_buf, &G, sizeof(Game));
    return (int)sizeof(Game);
}

/* JS ecrit la sauvegarde dans game_buffer() puis appelle game_load(taille).
 * En cas de donnees invalides (autre version), une nouvelle partie est generee. */
EXPORT int game_load(int size) {
    if (size != (int)sizeof(Game)) return 0;
    uint32_t hdr[2];                    /* version, size_check : 2 premiers champs de Game */
    memcpy(hdr, save_buf, sizeof(hdr)); /* (pas de Game sur la pile : 230 Ko > pile Wasm) */
    if (hdr[0] != SAVE_VERSION || hdr[1] != (uint32_t)sizeof(Game)) return 0;
    memcpy(&G, save_buf, sizeof(Game));
    memset(P, 0, sizeof(P));
    clear_fail_cache();
    acc = 0; alpha = 0;
    compute_light();
    prepare_render();
    return 1;
}
