/*
 * IRONCORE : Deep Forge - coeur du jeu (C pur, compile en WebAssembly)
 * Types partages entre tous les modules : carte, nains, ennemis, constructions.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* ---------- Constantes ---------- */
#define MAP_W 96
#define MAP_H 64
#define N_TILES (MAP_W * MAP_H)
#define SIM_HZ 20                 /* ticks de simulation par seconde */
#define MAX_DWARVES 12
#define MAX_ENEMIES 64
#define MAX_PARTICLES 600
#define PATH_CAP 320              /* longueur max d'un chemin memorise */
#define SAVE_VERSION 1

/* ---------- Enumerations ---------- */
enum { T_ROCK, T_FLOOR, T_WATER, T_LAVA, T_VEIN_IRON, T_VEIN_COAL, T_VEIN_CRYSTAL };
enum { IT_NONE, IT_IRON, IT_COAL, IT_CRYSTAL, IT_INGOT, IT_COUNT };
enum { B_NONE, B_CONVEYOR, B_DRILL, B_FORGE, B_WALL, B_TURRET, B_TRAP, B_CORE };
enum { DIR_E, DIR_S, DIR_W, DIR_N };
enum { DW_IDLE, DW_MOVE, DW_WORK };
enum { JOB_NONE, JOB_DIG, JOB_BUILD };
enum { EN_CRAWLER, EN_BRUTE };
enum { TOOL_NONE, TOOL_DIG, TOOL_CANCEL, TOOL_CONVEYOR, TOOL_DRILL, TOOL_FORGE,
       TOOL_WALL, TOOL_TURRET, TOOL_TRAP, TOOL_DEMOLISH };
enum { ERR_NONE, ERR_RESOURCES, ERR_INVALID, ERR_OCCUPIED, ERR_OVER };

/* Indices de palette (cote JS) */
enum { C_NONE, C_ROCK, C_FLOOR, C_WATER, C_LAVA, C_IRON, C_COAL, C_CRYSTAL, C_INGOT,
       C_DWARF, C_CRAWLER, C_BRUTE, C_CONV, C_DRILL, C_FORGE, C_WALL, C_TURRET,
       C_TRAP, C_CORE, C_SPARK, C_EMBER };

/* Identifiants de glyphes (cote JS : table glyphe -> caractere) */
enum { G_NONE, G_ROCK, G_FLOOR, G_WATER, G_LAVA, G_VEIN, G_CONV_E, G_CONV_S, G_CONV_W,
       G_CONV_N, G_DRILL, G_FORGE, G_WALL, G_TURRET, G_TRAP, G_CORE, G_DWARF,
       G_CRAWLER, G_BRUTE, G_SPARK, G_DOT, G_BOLT };

/* ---------- Structures (sans pointeurs : sauvegardables par memcpy) ---------- */
typedef struct {
    uint8_t type;        /* T_* */
    uint8_t dep_type;    /* gisement sous le sol (IT_*) */
    uint16_t dep_amt;    /* quantite restante */
    uint8_t bld, bdir, built;
    uint8_t desig;       /* 1 = zone a creuser */
    int8_t claim;        /* id du nain qui s'en occupe, -1 sinon */
    uint8_t item;        /* objet sur tapis / tampon de sortie de foreuse */
    uint8_t prog;        /* progression tapis / compteur de tirs tourelle */
    uint8_t explored;
    uint8_t light;
    uint8_t in_iron, in_coal, out_ingot;  /* tampons de fonderie */
    uint16_t hp;
    uint16_t timer;
    uint8_t build_prog;
    uint8_t flash;
} Tile;

typedef struct {
    float x, y, px, py;  /* position (centre de tuile = i+0.5), position precedente */
    uint8_t alive, state, job;
    int16_t hp;
    int16_t tx, ty;
    int16_t path_len, path_pos;
    uint16_t work, idle_cd;
    uint16_t path[PATH_CAP];
} Dwarf;

typedef struct {
    float x, y, px, py;
    uint8_t alive, type;
    int16_t hp;
    int16_t path_len, path_pos;
    uint16_t repath, atk_cd, age;
    uint16_t path[PATH_CAP];
} Enemy;

typedef struct {
    uint32_t version, size_check, rng, tick;
    int32_t stock[IT_COUNT];
    int32_t core_hp, core_x, core_y, spawn_x, spawn_y;
    int32_t state;                    /* 0 = en jeu, 1 = perdu */
    int32_t wave, wave_timer, wave_spawn_left, spawn_cd;
    int32_t kills, respawn_timer;
    Dwarf dw[MAX_DWARVES];
    Enemy en[MAX_ENEMIES];
    Tile tiles[N_TILES];
} Game;

typedef struct {                      /* non sauvegarde */
    float x, y, vx, vy, life, maxlife;
    uint8_t color, glyph;
} Particle;

/* ---------- Donnees globales ---------- */
extern Game G;
extern const int DX[4], DY[4];

static inline int IDX(int x, int y) { return y * MAP_W + x; }
static inline int in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }

/* map.c */
uint32_t rnd(void);
int rnd_range(int a, int b);
float rnd_f(void);
int bld_solid(int b);
int rock_solid(int t);
int walkable_dwarf(int x, int y);
void explore(int cx, int cy, int r);
void world_generate(uint32_t seed);

/* path.c */
int find_path(int sx, int sy, int tx, int ty, int mode, uint16_t *out, int cap);

/* sim.c */
void dwarves_update(void);
void conveyors_update(void);
void machines_update(void);
void dwarf_kill(int i);
void clear_fail_cache(void);
int tile_unreachable(int idx);   /* 1 si une case designee est inaccessible aux nains */

/* combat.c */
void waves_update(void);
void enemies_update(void);
void turrets_update(void);
void destroy_building(int x, int y);

/* api.c (effets) */
void fx_spawn(float x, float y, float vx, float vy, float life, int color, int glyph);
void fx_burst(float x, float y, int n, float speed, float life, int color, int glyph);

#endif
