#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/clock.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define MAX_UNITS 20
#define FPS_DELAY 40
#define MATCH_TICKS (180000 / FPS_DELAY)

#define ARENA_TOP 5
#define ARENA_BOTTOM 48
#define RIVER_LEFT 60
#define RIVER_RIGHT 66
#define PLAYER_OWN_EDGE 58
#define ENEMY_OWN_EDGE 68
#define PLAYER_POCKET_MAX 112
#define ENEMY_POCKET_MIN 15
#define CURSOR_STEP 4

typedef enum { SIDE_PLAYER=0, SIDE_ENEMY=1 } Side;
typedef enum { U_KNIGHT=0, U_ARCHER=1, U_GIANT=2 } UnitType;

typedef struct {
    int16_t x, y;
    int16_t hp;
    int8_t type;
    int8_t side;
    int8_t lane;
    int8_t cooldown;
    bool active;
} Unit;

typedef struct {
    int16_t x, y;
    int16_t hp, max_hp;
    int8_t side;
    int8_t kind; /* 0 princess, 1 king */
    int8_t cooldown;
} Tower;

static Unit units[MAX_UNITS];
static Tower towers[6];
static int player_elixir, enemy_elixir;
static int selected_card;
static int cursor_x, cursor_y;
static int invalid_flash;
static int tick_count;
static uint32_t rng_state = 0x35e2u;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int card_cost(int card)
{
    static const int costs[4] = {3, 3, 5, 4};
    return costs[card & 3];
}

static const char *card_name(int card)
{
    static const char *names[4] = {"CH", "AR", "GE", "BF"};
    return names[card & 3];
}

static void reset_game(void)
{
    memset(units, 0, sizeof(units));
    player_elixir = 5 * 100;
    enemy_elixir = 5 * 100;
    selected_card = 0;
    cursor_x = 40;
    cursor_y = 15;
    invalid_flash = 0;
    tick_count = 0;

    towers[0] = (Tower){ 12, 13, 700, 700, SIDE_PLAYER, 0, 0 };
    towers[1] = (Tower){ 12, 39, 700, 700, SIDE_PLAYER, 0, 0 };
    towers[2] = (Tower){ 4,  26, 1100,1100,SIDE_PLAYER, 1, 0 };
    towers[3] = (Tower){115, 13, 700, 700, SIDE_ENEMY, 0, 0 };
    towers[4] = (Tower){115, 39, 700, 700, SIDE_ENEMY, 0, 0 };
    towers[5] = (Tower){123, 26,1100,1100,SIDE_ENEMY, 1, 0 };
}

static bool point_on_living_tower(int x, int y)
{
    for(int i = 0; i < 6; i++) {
        Tower *t = &towers[i];
        if(t->hp <= 0) continue;
        int radius = t->kind ? 6 : 5;
        if(iabs(x - t->x) <= radius && iabs(y - t->y) <= radius) return true;
    }
    return false;
}

/* Regles de placement inspirees de Clash Royale:
   - une troupe se pose d'abord uniquement dans son propre camp;
   - la riviere reste interdite;
   - detruire une tour princesse ouvre une poche de deploiement dans cette voie;
   - les sorts peuvent viser toute l'arene. */
static bool troop_position_valid(Side side, int x, int y)
{
    if(y < ARENA_TOP || y > ARENA_BOTTOM) return false;
    if(point_on_living_tower(x, y)) return false;

    if(side == SIDE_PLAYER) {
        if(x >= 1 && x <= PLAYER_OWN_EDGE) return true;

        if(x >= ENEMY_OWN_EDGE && x <= PLAYER_POCKET_MAX) {
            if(y <= 25 && towers[3].hp <= 0) return true;
            if(y >= 27 && towers[4].hp <= 0) return true;
        }
    }
    else {
        if(x >= ENEMY_OWN_EDGE && x <= 126) return true;

        if(x >= ENEMY_POCKET_MIN && x <= PLAYER_OWN_EDGE) {
            if(y <= 25 && towers[0].hp <= 0) return true;
            if(y >= 27 && towers[1].hp <= 0) return true;
        }
    }

    return false;
}

static bool card_position_valid(Side side, int card, int x, int y)
{
    if(card == 3) {
        return x >= 1 && x <= 126 && y >= ARENA_TOP && y <= ARENA_BOTTOM;
    }
    return troop_position_valid(side, x, y);
}

static bool spawn_unit(Side side, UnitType type, int x, int y)
{
    for(int i = 0; i < MAX_UNITS; i++) {
        if(units[i].active) continue;
        units[i].active = true;
        units[i].side = side;
        units[i].type = type;
        units[i].lane = (y >= 26) ? 1 : 0;
        units[i].x = x;
        units[i].y = y;
        units[i].cooldown = 0;
        if(type == U_KNIGHT) units[i].hp = 280;
        else if(type == U_ARCHER) units[i].hp = 170;
        else units[i].hp = 520;
        return true;
    }
    return false;
}

static Tower *nearest_enemy_tower(Unit *u)
{
    Tower *best = NULL;
    int bestd = 9999;
    for(int i = 0; i < 6; i++) {
        Tower *t = &towers[i];
        if(t->hp <= 0 || t->side == u->side) continue;
        int lane_penalty = 0;
        if(t->kind == 0) lane_penalty = iabs(t->y - u->y) * 3;
        int d = iabs(t->x - u->x) + lane_penalty;
        if(d < bestd) { bestd = d; best = t; }
    }
    return best;
}

static Unit *nearest_enemy_unit(Unit *u, int range)
{
    Unit *best = NULL;
    int bestd = 9999;
    for(int i = 0; i < MAX_UNITS; i++) {
        Unit *v = &units[i];
        if(!v->active || v->side == u->side) continue;
        int d = iabs(v->x - u->x) + iabs(v->y - u->y);
        if(d <= range && d < bestd) { bestd = d; best = v; }
    }
    return best;
}

static void cast_fireball(Side side, int cx, int cy)
{
    Side target_side = (side == SIDE_PLAYER) ? SIDE_ENEMY : SIDE_PLAYER;
    for(int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &units[i];
        if(!u->active || u->side != (int8_t)target_side) continue;
        if(iabs(u->x - cx) <= 16 && iabs(u->y - cy) <= 9) {
            u->hp -= 155;
            if(u->hp <= 0) u->active = false;
        }
    }
    for(int i = 0; i < 6; i++) {
        Tower *t = &towers[i];
        if(t->side != (int8_t)target_side || t->hp <= 0) continue;
        if(iabs(t->x - cx) <= 18 && iabs(t->y - cy) <= 12) {
            t->hp -= 90;
            if(t->hp < 0) t->hp = 0;
        }
    }
}

static bool deploy_card(Side side, int card, int x, int y)
{
    int *elixir = (side == SIDE_PLAYER) ? &player_elixir : &enemy_elixir;
    int cost = card_cost(card) * 100;
    if(*elixir < cost) return false;
    if(!card_position_valid(side, card, x, y)) return false;

    bool used = false;
    if(card == 0) used = spawn_unit(side, U_KNIGHT, x, y);
    else if(card == 1) used = spawn_unit(side, U_ARCHER, x, y);
    else if(card == 2) used = spawn_unit(side, U_GIANT, x, y);
    else { cast_fireball(side, x, y); used = true; }

    if(used) *elixir -= cost;
    return used;
}

static void movement_goal(Unit *u, Tower *target, int *gx, int *gy)
{
    int bridge_y = u->lane ? 37 : 15;
    *gx = target->x;
    *gy = target->y;

    /* Les troupes traversent la riviere par le pont de leur voie. */
    if(u->side == SIDE_PLAYER && u->x < ENEMY_OWN_EDGE) {
        if(u->x < RIVER_LEFT) {
            *gx = RIVER_LEFT - 1;
            *gy = bridge_y;
        }
        else {
            *gx = RIVER_RIGHT + 2;
            *gy = bridge_y;
        }
    }
    else if(u->side == SIDE_ENEMY && u->x > PLAYER_OWN_EDGE) {
        if(u->x > RIVER_RIGHT) {
            *gx = RIVER_RIGHT + 2;
            *gy = bridge_y;
        }
        else {
            *gx = RIVER_LEFT - 2;
            *gy = bridge_y;
        }
    }
}

static void update_units(void)
{
    for(int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &units[i];
        if(!u->active) continue;
        if(u->hp <= 0) { u->active = false; continue; }
        if(u->cooldown > 0) u->cooldown--;

        int range = (u->type == U_ARCHER) ? 14 : 6;
        Unit *enemy = NULL;

        /* Comme dans Clash Royale, le geant ignore les troupes et vise les batiments. */
        if(u->type != U_GIANT) enemy = nearest_enemy_unit(u, range);

        if(enemy) {
            if(u->cooldown == 0) {
                int dmg = (u->type == U_ARCHER) ? 32 : 44;
                enemy->hp -= dmg;
                if(enemy->hp <= 0) enemy->active = false;
                u->cooldown = (u->type == U_ARCHER) ? 13 : 10;
            }
            continue;
        }

        Tower *target = nearest_enemy_tower(u);
        if(!target) continue;
        int dx = target->x - u->x;
        int dy = target->y - u->y;
        int dist = iabs(dx) + iabs(dy);
        int attack_range = (u->type == U_ARCHER) ? 13 : 5;

        if(dist <= attack_range) {
            if(u->cooldown == 0) {
                int dmg = (u->type == U_GIANT) ? 58 : (u->type == U_ARCHER ? 27 : 38);
                target->hp -= dmg;
                if(target->hp < 0) target->hp = 0;
                u->cooldown = (u->type == U_ARCHER) ? 14 : 11;
            }
        }
        else {
            int gx, gy;
            movement_goal(u, target, &gx, &gy);
            dx = gx - u->x;
            dy = gy - u->y;

            /* Mouvement lent, mieux adapte a l'echelle 128x64. */
            int move_interval = 5;
            if(u->type == U_ARCHER) move_interval = 6;
            else if(u->type == U_GIANT) move_interval = 8;

            if((tick_count % move_interval) == 0) {
                if(iabs(dx) >= iabs(dy) && dx != 0) u->x += (dx > 0) ? 1 : -1;
                else if(dy != 0) u->y += (dy > 0) ? 1 : -1;
                u->y = clampi(u->y, ARENA_TOP, ARENA_BOTTOM);
            }
        }
    }
}

static void update_towers(void)
{
    for(int ti = 0; ti < 6; ti++) {
        Tower *t = &towers[ti];
        if(t->hp <= 0) continue;
        if(t->cooldown > 0) { t->cooldown--; continue; }

        Unit *best = NULL;
        int bestd = 9999;
        for(int i = 0; i < MAX_UNITS; i++) {
            Unit *u = &units[i];
            if(!u->active || u->side == t->side) continue;
            int d = iabs(u->x - t->x) + iabs(u->y - t->y);
            int range = t->kind ? 22 : 18;
            if(d <= range && d < bestd) { best = u; bestd = d; }
        }
        if(best) {
            best->hp -= t->kind ? 34 : 27;
            if(best->hp <= 0) best->active = false;
            t->cooldown = t->kind ? 8 : 10;
        }
    }
}

static void update_ai(void)
{
    if((tick_count % 45) != 0) return;

    int card = (int)(rng_next() % 4u);
    if(enemy_elixir < card_cost(card) * 100) return;

    if(card == 3) {
        /* Le sort ennemi peut viser partout dans notre moitie. */
        int x = 18 + (int)(rng_next() % 36u);
        int y = (rng_next() & 1u) ? 38 : 14;
        deploy_card(SIDE_ENEMY, card, x, y);
    }
    else {
        /* Les troupes de l'IA suivent les memes regles de pose que le joueur. */
        int x = 78 + (int)(rng_next() % 25u);
        int y = (rng_next() & 1u) ? 38 : 14;
        y += (int)(rng_next() % 9u) - 4;
        deploy_card(SIDE_ENEMY, card, x, y);
    }
}

static void update_game(void)
{
    tick_count++;
    if(invalid_flash > 0) invalid_flash--;

    if((tick_count % 2) == 0) {
        if(player_elixir < 1000) player_elixir += 1;
        if(enemy_elixir < 1000) enemy_elixir += 1;
    }
    update_ai();
    update_units();
    update_towers();
}

static void draw_hpbar(int x, int y, int w, int hp, int maxhp)
{
    if(maxhp <= 0 || w < 3) return;

    int inner_w = w - 2;
    int fill = clampi((hp * inner_w) / maxhp, 0, inner_w);
    drect_border(x, y, x+w-1, y+2, C_WHITE, 1, C_BLACK);
    if(fill > 0) drect(x+1, y+1, x+fill, y+1, C_BLACK);
}

static void draw_tower(const Tower *t)
{
    if(t->hp <= 0) {
        dline(t->x-3,t->y-3,t->x+3,t->y+3,C_BLACK);
        dline(t->x+3,t->y-3,t->x-3,t->y+3,C_BLACK);
        return;
    }
    int s = t->kind ? 4 : 3;
    drect_border(t->x-s, t->y-s, t->x+s, t->y+s, C_WHITE, 1, C_BLACK);
    if(t->kind) {
        dline(t->x-2,t->y-1,t->x+2,t->y-1,C_BLACK);
        dpixel(t->x,t->y+1,C_BLACK);
    }
    int bar_x = clampi(t->x - 5, 0, 118);
    draw_hpbar(bar_x, t->y-s-4, 10, t->hp, t->max_hp);
}

static void draw_unit(const Unit *u)
{
    if(!u->active) return;
    if(u->type == U_GIANT) {
        drect_border(u->x-2,u->y-2,u->x+2,u->y+2,C_WHITE,1,C_BLACK);
    }
    else if(u->type == U_ARCHER) {
        drect(u->x-1,u->y-1,u->x+1,u->y+1,C_BLACK);
        dline(u->x+2,u->y-2,u->x+2,u->y+2,C_BLACK);
    }
    else {
        drect(u->x-1,u->y-1,u->x+1,u->y+1,C_BLACK);
        dpixel(u->x, u->y-2, C_BLACK);
    }
    if(u->side == SIDE_ENEMY) dpixel(u->x+3,u->y,C_BLACK);
}

static void draw_deploy_cursor(void)
{
    bool valid = card_position_valid(SIDE_PLAYER, selected_card, cursor_x, cursor_y);

    if(valid && invalid_flash == 0) {
        if(selected_card == 3) {
            /* Viseur de sort. */
            dline(cursor_x-4, cursor_y, cursor_x+4, cursor_y, C_BLACK);
            dline(cursor_x, cursor_y-4, cursor_x, cursor_y+4, C_BLACK);
            drect_border(cursor_x-2,cursor_y-2,cursor_x+2,cursor_y+2,C_WHITE,1,C_BLACK);
        }
        else {
            /* Case de pose valide. */
            drect_border(cursor_x-3,cursor_y-3,cursor_x+3,cursor_y+3,C_WHITE,1,C_BLACK);
            dpixel(cursor_x,cursor_y,C_BLACK);
        }
    }
    else {
        /* Croix = zone interdite ou tentative ratee. */
        dline(cursor_x-3,cursor_y-3,cursor_x+3,cursor_y+3,C_BLACK);
        dline(cursor_x+3,cursor_y-3,cursor_x-3,cursor_y+3,C_BLACK);
    }
}

static void draw_arena(void)
{
    dclear(C_WHITE);
    dline(0, 51, 127, 51, C_BLACK);
    dline(63, 2, 63, 49, C_BLACK);

    /* Ponts. */
    drect(60, 11, 66, 19, C_WHITE);
    drect_border(60,11,66,19,C_WHITE,1,C_BLACK);
    drect(60, 33, 66, 41, C_WHITE);
    drect_border(60,33,66,41,C_WHITE,1,C_BLACK);

    /* Petits reperes de la limite de pose normale du joueur. */
    for(int y = 6; y <= 48; y += 6) dpixel(58, y, C_BLACK);

    for(int i = 0; i < 6; i++) draw_tower(&towers[i]);
    for(int i = 0; i < MAX_UNITS; i++) draw_unit(&units[i]);
    draw_deploy_cursor();

    char buf[20];
    int secs = (MATCH_TICKS - tick_count) * FPS_DELAY / 1000;
    if(secs < 0) secs = 0;
    snprintf(buf, sizeof(buf), "%d:%02d", secs/60, secs%60);
    dtext(51, 0, C_BLACK, buf);

    snprintf(buf, sizeof(buf), "E%d", player_elixir/100);
    dtext(1, 54, C_BLACK, buf);

    for(int c = 0; c < 4; c++) {
        int x = 28 + c*25;
        char cardbuf[8];
        snprintf(cardbuf, sizeof(cardbuf), "%s%d", card_name(c), card_cost(c));
        if(c == selected_card) drect_border(x-2,53,x+20,63,C_WHITE,1,C_BLACK);
        dtext(x,55,C_BLACK,cardbuf);
    }
    dupdate();
}

static int winner(void)
{
    if(towers[5].hp <= 0) return 1;
    if(towers[2].hp <= 0) return -1;
    if(tick_count < MATCH_TICKS) return 0;

    int php = towers[0].hp + towers[1].hp + towers[2].hp;
    int ehp = towers[3].hp + towers[4].hp + towers[5].hp;
    if(php > ehp) return 1;
    if(ehp > php) return -1;
    return 2;
}

static void title_screen(void)
{
    dclear(C_WHITE);
    dtext(19, 8, C_BLACK, "ROYALE 35+E II");
    drect_border(25,23,102,43,C_WHITE,1,C_BLACK);
    dtext(35,27,C_BLACK,"EXE: JOUER");
    dtext(7,48,C_BLACK,"F1-F4 + fleches + EXE");
    dupdate();
    while(1) {
        key_event_t ev = getkey();
        if(ev.key == KEY_EXE) return;
        if(ev.key == KEY_EXIT) return;
    }
}

static void end_screen(int w)
{
    dclear(C_WHITE);
    if(w == 1) dtext(38,18,C_BLACK,"VICTOIRE!");
    else if(w == -1) dtext(38,18,C_BLACK,"DEFAITE...");
    else dtext(43,18,C_BLACK,"EGALITE");
    dtext(20,36,C_BLACK,"EXE: revanche  EXIT: fin");
    dupdate();
}

int main(void)
{
    title_screen();

    bool running = true;
    while(running) {
        reset_game();
        clearevents();
        int result = 0;

        while(running && result == 0) {
            key_event_t ev;
            do {
                ev = pollevent();
                if(ev.type != KEYEV_DOWN) continue;

                if(ev.key == KEY_EXIT) { running = false; break; }
                if(ev.key == KEY_F1) selected_card = 0;
                else if(ev.key == KEY_F2) selected_card = 1;
                else if(ev.key == KEY_F3) selected_card = 2;
                else if(ev.key == KEY_F4) selected_card = 3;
                else if(ev.key == KEY_LEFT)
                    cursor_x = clampi(cursor_x - CURSOR_STEP, 1, 126);
                else if(ev.key == KEY_RIGHT)
                    cursor_x = clampi(cursor_x + CURSOR_STEP, 1, 126);
                else if(ev.key == KEY_UP)
                    cursor_y = clampi(cursor_y - CURSOR_STEP, ARENA_TOP, ARENA_BOTTOM);
                else if(ev.key == KEY_DOWN)
                    cursor_y = clampi(cursor_y + CURSOR_STEP, ARENA_TOP, ARENA_BOTTOM);
                else if(ev.key == KEY_EXE) {
                    if(!deploy_card(SIDE_PLAYER, selected_card, cursor_x, cursor_y))
                        invalid_flash = 8;
                }
            } while(ev.type != KEYEV_NONE);

            if(!running) break;
            update_game();
            draw_arena();
            result = winner();
            sleep_ms(FPS_DELAY);
        }

        if(!running) break;
        end_screen(result);
        while(1) {
            key_event_t ev = getkey();
            if(ev.key == KEY_EXE) break;
            if(ev.key == KEY_EXIT) { running = false; break; }
        }
    }
    return 1;
}
