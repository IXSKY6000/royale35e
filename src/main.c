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
static int selected_card, selected_lane;
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
    static const char *names[4] = {"KNT", "ARC", "GNT", "FBL"};
    return names[card & 3];
}

static void reset_game(void)
{
    memset(units, 0, sizeof(units));
    player_elixir = 5 * 100;
    enemy_elixir = 5 * 100;
    selected_card = 0;
    selected_lane = 0;
    tick_count = 0;

    towers[0] = (Tower){ 12, 13, 700, 700, SIDE_PLAYER, 0, 0 };
    towers[1] = (Tower){ 12, 39, 700, 700, SIDE_PLAYER, 0, 0 };
    towers[2] = (Tower){ 4,  26, 1100,1100,SIDE_PLAYER, 1, 0 };
    towers[3] = (Tower){115, 13, 700, 700, SIDE_ENEMY, 0, 0 };
    towers[4] = (Tower){115, 39, 700, 700, SIDE_ENEMY, 0, 0 };
    towers[5] = (Tower){123, 26,1100,1100,SIDE_ENEMY, 1, 0 };
}

static bool spawn_unit(Side side, UnitType type, int lane)
{
    for(int i = 0; i < MAX_UNITS; i++) {
        if(units[i].active) continue;
        units[i].active = true;
        units[i].side = side;
        units[i].type = type;
        units[i].lane = lane;
        units[i].x = (side == SIDE_PLAYER) ? 26 : 101;
        units[i].y = lane ? 37 : 15;
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

static void cast_fireball(Side side, int lane)
{
    int cx = (side == SIDE_PLAYER) ? 89 : 38;
    int cy = lane ? 37 : 15;
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
        if(iabs(t->x - cx) <= 18 && iabs(t->y - cy) <= 12) t->hp -= 90;
    }
}

static void deploy_card(Side side, int card, int lane)
{
    int *elixir = (side == SIDE_PLAYER) ? &player_elixir : &enemy_elixir;
    int cost = card_cost(card) * 100;
    if(*elixir < cost) return;

    bool used = false;
    if(card == 0) used = spawn_unit(side, U_KNIGHT, lane);
    else if(card == 1) used = spawn_unit(side, U_ARCHER, lane);
    else if(card == 2) used = spawn_unit(side, U_GIANT, lane);
    else { cast_fireball(side, lane); used = true; }

    if(used) *elixir -= cost;
}

static void update_units(void)
{
    for(int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &units[i];
        if(!u->active) continue;
        if(u->hp <= 0) { u->active = false; continue; }
        if(u->cooldown > 0) u->cooldown--;

        int range = (u->type == U_ARCHER) ? 14 : 6;
        Unit *enemy = nearest_enemy_unit(u, range);
        if(enemy) {
            if(u->cooldown == 0) {
                int dmg = (u->type == U_GIANT) ? 0 : (u->type == U_ARCHER ? 32 : 44);
                if(dmg) {
                    enemy->hp -= dmg;
                    if(enemy->hp <= 0) enemy->active = false;
                }
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
            int speed_tick = (u->type == U_GIANT) ? 2 : 1;
            if((tick_count % speed_tick) == 0) {
                if(iabs(dx) >= iabs(dy)) u->x += (dx > 0) ? 1 : -1;
                else u->y += (dy > 0) ? 1 : -1;
                u->y = clampi(u->y, 7, 45);
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
    int lane = (int)(rng_next() & 1u);
    if(enemy_elixir >= card_cost(card) * 100) deploy_card(SIDE_ENEMY, card, lane);
}

static void update_game(void)
{
    tick_count++;
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
    if(maxhp <= 0) return;
    int fill = clampi((hp * w) / maxhp, 0, w);
    drect(x, y, x+w, y, C_BLACK);
    if(fill > 0) drect(x, y, x+fill, y, C_BLACK);
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
    draw_hpbar(t->x-4, t->y-s-2, 8, t->hp, t->max_hp);
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

static void draw_arena(void)
{
    dclear(C_WHITE);
    dline(0, 51, 127, 51, C_BLACK);
    dline(63, 2, 63, 49, C_BLACK);
    /* Bridges */
    drect(60, 11, 66, 19, C_WHITE);
    drect_border(60,11,66,19,C_WHITE,1,C_BLACK);
    drect(60, 33, 66, 41, C_WHITE);
    drect_border(60,33,66,41,C_WHITE,1,C_BLACK);

    for(int i = 0; i < 6; i++) draw_tower(&towers[i]);
    for(int i = 0; i < MAX_UNITS; i++) draw_unit(&units[i]);

    char buf[20];
    int secs = (MATCH_TICKS - tick_count) * FPS_DELAY / 1000;
    if(secs < 0) secs = 0;
    snprintf(buf, sizeof(buf), "%d:%02d", secs/60, secs%60);
    dtext(51, 0, C_BLACK, buf);

    snprintf(buf, sizeof(buf), "E%d", player_elixir/100);
    dtext(1, 54, C_BLACK, buf);

    for(int c = 0; c < 4; c++) {
        int x = 26 + c*25;
        if(c == selected_card) drect_border(x-2,53,x+22,63,C_WHITE,1,C_BLACK);
        dtext(x,55,C_BLACK,card_name(c));
    }
    dtext(113,54,C_BLACK, selected_lane ? "B" : "H");
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
    dtext(18,48,C_BLACK,"F1-F4 cartes  EXIT quitter");
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
                else if(ev.key == KEY_UP) selected_lane = 0;
                else if(ev.key == KEY_DOWN) selected_lane = 1;
                else if(ev.key == KEY_EXE) deploy_card(SIDE_PLAYER, selected_card, selected_lane);
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
