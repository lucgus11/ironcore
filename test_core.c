/*
 * Test d'integration natif : genere une carte, fait miner les nains, construit une
 * chaine foreuse -> tapis -> fonderie -> coeur, pose une tourelle et simule des vagues.
 * Build : voir build.sh (target test)
 */
#include <stdio.h>
#include "../src/game.h"

void game_init(unsigned int seed);
void game_update(float dt_ms);
int game_apply_tool(int tool, int x, int y, int dir);
int game_stat(int id);
int game_save(void);
int game_load(int size);
uint8_t *game_buffer(void);
uint8_t *game_ptr_flags(void);

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } else printf("  ok  : %s\n", msg); } while (0)

static void run(float seconds) {
    for (float t = 0; t < seconds; t += 0.05f) game_update(50.0f);
}

int main(void) {
    game_init(1234);
    printf("== carte ==\n");
    int iron = 0, coal = 0, cry = 0, floor_ = 0;
    for (int i = 0; i < N_TILES; i++) {
        int t = G.tiles[i].type;
        iron += t == T_VEIN_IRON; coal += t == T_VEIN_COAL; cry += t == T_VEIN_CRYSTAL; floor_ += t == T_FLOOR;
    }
    printf("  sol=%d fer=%d charbon=%d cristal=%d\n", floor_, iron, coal, cry);
    CHECK(iron > 20 && coal > 20 && cry > 3, "filons presents");
    uint16_t buf[PATH_CAP];
    int len = find_path(G.spawn_x, G.spawn_y, G.core_x, G.core_y + 2, 1, buf, PATH_CAP);
    printf("  chemin monstres -> coeur : %d pas\n", len);
    CHECK(len > 0, "les monstres peuvent atteindre le coeur");

    G.wave_timer = 1 << 20;                  /* pas de vague pendant les tests d'economie */
    printf("== minage ==\n");
    int n = 0, designated[64][2], nd = 0;
    for (int y = 4; y <= 30 && nd < 6; y++)
        for (int x = 4; x <= 40 && nd < 6; x++) {
            Tile *t = &G.tiles[IDX(x, y)];
            if (rock_solid(t->type) && t->explored &&
                (G.tiles[IDX(x - 1, y)].type == T_FLOOR || G.tiles[IDX(x + 1, y)].type == T_FLOOR ||
                 G.tiles[IDX(x, y - 1)].type == T_FLOOR || G.tiles[IDX(x, y + 1)].type == T_FLOOR)) {
                n += game_apply_tool(TOOL_DIG, x, y, 0);
                designated[nd][0] = x; designated[nd][1] = y; nd++;
            }
        }
    printf("  %d tuiles designees\n", n);
    run(90);
    int dug = 0;
    for (int i = 0; i < nd; i++) dug += G.tiles[IDX(designated[i][0], designated[i][1])].type == T_FLOOR;
    printf("  %d/%d creusees\n", dug, nd);
    CHECK(n > 0 && dug == nd, "les nains creusent toutes les zones designees");

    printf("== automatisation ==\n");
    /* Foreuse sur un gisement -> ligne de tapis -> coeur (route aplanie pour le test) */
    int dx = G.core_x + 5, dy = G.core_y;
    for (int x = G.core_x + 2; x <= dx; x++) {
        Tile *t = &G.tiles[IDX(x, dy)];
        t->type = T_FLOOR; t->explored = 1; t->bld = B_NONE; t->built = 0;
    }
    G.tiles[IDX(dx, dy)].dep_type = IT_IRON;
    G.tiles[IDX(dx, dy)].dep_amt = 200;
    G.stock[IT_IRON] = 100;
    int placed = 0;
    for (int x = dx - 1; x >= G.core_x + 2; x--) placed += game_apply_tool(TOOL_CONVEYOR, x, dy, DIR_W);
    CHECK(placed == 3, "3 tapis poses jusqu'au coeur");
    CHECK(game_apply_tool(TOOL_DRILL, dx, dy, 0) == 1, "foreuse posee");
    int iron0 = G.stock[IT_IRON] + 0;          /* apres paiement de la foreuse */
    run(120);
    printf("  fer : %d -> %d\n", iron0, G.stock[IT_IRON]);
    CHECK(G.stock[IT_IRON] >= iron0 + 10, "la chaine foreuse->tapis->coeur livre du fer");

    /* Fonderie : fer + charbon -> lingot */
    int fx = G.core_x - 5, fy = G.core_y;
    for (int x = fx; x <= G.core_x - 2; x++) { Tile *t = &G.tiles[IDX(x, fy)]; t->type = T_FLOOR; t->explored = 1; t->bld = B_NONE; }
    G.stock[IT_IRON] = 100; G.stock[IT_INGOT] = 0;
    CHECK(game_apply_tool(TOOL_FORGE, fx, fy, 0) == 1, "fonderie posee");
    G.tiles[IDX(fx, fy)].in_iron = 8; G.tiles[IDX(fx, fy)].in_coal = 8;
    for (int x = fx + 1; x < G.core_x - 1; x++) game_apply_tool(TOOL_CONVEYOR, x, fy, DIR_E);
    run(90);
    printf("  lingots : %d\n", G.stock[IT_INGOT]);
    CHECK(G.stock[IT_INGOT] >= 1, "la fonderie produit des lingots acheminés au coeur");

    printf("== defense ==\n");
    G.stock[IT_INGOT] = 40;
    int tx = G.core_x + 5, ty = G.core_y + 4;
    G.tiles[IDX(tx, ty)].type = T_FLOOR; G.tiles[IDX(tx, ty)].explored = 1;
    CHECK(game_apply_tool(TOOL_TURRET, tx, ty, 0) == 1, "tourelle posee");
    G.wave_timer = 1;
    run(240);
    CHECK(G.core_hp > 0 && game_stat(9) == 0, "le coeur survit avec une tourelle");
    CHECK(game_stat(10) > 0, "la tourelle tue des monstres");
    printf("  vague=%d ennemis=%d victimes=%d coeur=%d nains=%d\n",
           game_stat(5), game_stat(8), game_stat(10), game_stat(4), game_stat(7));
    CHECK(game_stat(5) >= 1, "une vague a ete declenchee");

    printf("== sauvegarde ==\n");
    int sz = game_save();
    uint32_t tick = G.tick; int wave = G.wave;
    run(5);
    CHECK(game_load(sz) == 1 && G.tick == tick && G.wave == wave, "chargement restaure l'etat");
    G.version = 999; memcpy(game_buffer(), &G, sizeof(Game));
    CHECK(game_load(sz) == 0, "version incompatible refusee");

    printf("== zones inaccessibles ==\n");
    {
        game_init(7); G.wave_timer = 1 << 20;
        /* bloc de roche massive 3x3 en haut de la carte : la case centrale n'a que des voisins rocheux */
        int bx = G.core_x - 2, by = 2, cx = bx + 1, cy = by + 1;
        for (int y = by; y < by + 3; y++) for (int x = bx; x < bx + 3; x++) {
            G.tiles[IDX(x, y)].type = T_ROCK; G.tiles[IDX(x, y)].explored = 1;
        }
        CHECK(game_apply_tool(TOOL_DIG, cx, cy, 0) == 1, "designation d'une case au coeur du bloc");
        run(15);
        CHECK(game_ptr_flags()[IDX(cx, cy)] & 8, "case inaccessible signalee (drapeau rouge)");
        CHECK(G.tiles[IDX(cx, cy)].type == T_ROCK, "case inaccessible non creusee");
        /* un tunnel arrive jusqu'au pied du bloc ; on designe la case de devant : le joueur "creuse vers" la cible */
        for (int y = by + 3; y <= G.core_y - 5; y++) { G.tiles[IDX(cx, y)].type = T_FLOOR; G.tiles[IDX(cx, y)].explored = 1; }
        clear_fail_cache();
        CHECK(game_apply_tool(TOOL_DIG, cx, cy + 1, 0) == 1, "designation de la case de devant");
        run(40);
        CHECK(G.tiles[IDX(cx, cy + 1)].type == T_FLOOR && G.tiles[IDX(cx, cy)].type == T_FLOOR,
              "les nains percent jusqu'a la case centrale (plus inaccessible)");
    }

    printf("== robustesse de la generation (500 graines) ==\n");
    {
        int bad_rock = 0, bad_iron = 0, bad_coal = 0, bad_path = 0;
        for (unsigned sd = 1; sd <= 500; sd++) {
            world_generate(sd * 2654435761u + 1);
            int rock = 0, fe = 0, co = 0;
            for (int i = 0; i < N_TILES; i++) {
                const Tile *t = &G.tiles[i];
                if (!t->explored) continue;
                rock += rock_solid(t->type); fe += t->type == T_VEIN_IRON; co += t->type == T_VEIN_COAL;
            }
            bad_rock += rock == 0; bad_iron += fe == 0; bad_coal += co == 0;
            uint16_t pb[PATH_CAP];
            bad_path += find_path(G.spawn_x, G.spawn_y, G.core_x, G.core_y + 2, 1, pb, PATH_CAP) < 0;
        }
        CHECK(bad_rock == 0, "toujours de la roche visible au depart (pas de blocage)");
        CHECK(bad_iron == 0 && bad_coal == 0, "toujours du fer et du charbon visibles au depart");
        CHECK(bad_path == 0, "les monstres peuvent toujours atteindre le coeur");
    }

    printf("== endurance (10 min de jeu) ==\n");
    game_init(99);
    for (int i = 0; i < 600 * 20; i++) game_update(50.0f);
    CHECK(G.tick == 12000, "10 min simulees sans plantage");

    printf(fails ? "\nECHECS: %d\n" : "\nTOUS LES TESTS PASSENT\n", fails);
    return fails != 0;
}
