#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

// Platform Specific Includes & Input Handling
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <conio.h>
    #define SLEEP_MS(ms) Sleep(ms)
    #define BEEP() Beep(750, 50)
    #define COL_WATER 1
    #define COL_FOOD  14
    #define COL_AGENT 10
    #define COL_ENEMY 12
    #define COL_BULLET 15
    #define COL_RESET 7
    void set_color(int color) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color); }
    void gotoxy(int x, int y) { COORD c = { (SHORT)x, (SHORT)y }; SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c); }
    void init_term() { 
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO ci; GetConsoleCursorInfo(hOut, &ci);
        ci.bVisible = FALSE; SetConsoleCursorInfo(hOut, &ci);
    }
    void end_term() { set_color(COL_RESET); }
    #define DRAW_CH(x, y, ch, col) { gotoxy(x, y); set_color(col); putchar(ch); }
    
    int get_input() {
        if (_kbhit()) {
            int c = _getch();
            if (c == 224 || c == 0) { // Arrow keys
                c = _getch();
                if (c == 72) return 1000; // UP ARROW
                if (c == 80) return 1001; // DOWN ARROW
                if (c == 75) return 1002; // LEFT ARROW
                if (c == 77) return 1003; // RIGHT ARROW
            }
            return c;
        }
        return -1;
    }
#else
    #include <ncurses.h>
    #include <unistd.h>
    #define SLEEP_MS(ms) usleep(ms * 1000)
    #define BEEP() printf("\a"); fflush(stdout)
    #define COL_WATER 1
    #define COL_FOOD  2
    #define COL_AGENT 3
    #define COL_ENEMY 4
    #define COL_BULLET 5
    #define COL_RESET 0 
    #define init_term() { initscr(); start_color(); noecho(); curs_set(0); nodelay(stdscr, TRUE); keypad(stdscr, TRUE); \
                          init_pair(1, COLOR_BLUE, COLOR_BLACK); init_pair(2, COLOR_YELLOW, COLOR_BLACK); \
                          init_pair(3, COLOR_GREEN, COLOR_BLACK); init_pair(4, COLOR_RED, COLOR_BLACK); \
                          init_pair(5, COLOR_WHITE, COLOR_BLACK); }
    #define end_term() { endwin(); }
    #define DRAW_CH(x, y, ch, col_idx) { if(col_idx > 0) attron(COLOR_PAIR(col_idx)); mvaddch(y, x, ch); if(col_idx > 0) attroff(COLOR_PAIR(col_idx)); }
    
    int get_input() {
        int c = getch();
        if (c == ERR) return -1;
        if (c == KEY_UP) return 1000;
        if (c == KEY_DOWN) return 1001;
        if (c == KEY_LEFT) return 1002;
        if (c == KEY_RIGHT) return 1003;
        return c;
    }
#endif

// Game Constants
#define MAP_W 60
#define MAP_H 20
#define MAX_ENEMIES 30
#define MAX_BULLETS 15
#define FIRE_COOLDOWN 4

// Structures
typedef struct { int x, y; } Point;
typedef struct { float x, y; } Vec2;
typedef struct { Vec2 pos; int hp, score, reload, active; Point path[MAP_W*MAP_H]; int path_ptr; } Entity;
typedef struct { Vec2 pos, vel; int active; } Bullet;

// Globals
int world[MAP_H][MAP_W];
Entity player;
Entity mobs[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
unsigned int frame_count = 0;
int game_level = 1;

// Helper: Count active enemies
int get_active_enemy_count() {
    int c = 0;
    for(int i=0; i<MAX_ENEMIES; i++) if(mobs[i].active) c++;
    return c;
}

void spawn_food() {
    int x, y; 
    do { x = rand() % MAP_W; y = rand() % MAP_H; } while (world[y][x] == 1 || (x == (int)player.pos.x && y == (int)player.pos.y));
    world[y][x] = 2; 
}

void try_spawn_enemy() {
    for(int i = 0; i < MAX_ENEMIES; i++) {
        if (!mobs[i].active) {
            int x, y; 
            int attempts = 0;
            // Ensure enemy doesn't spawn on walls, or too close to the player
            do { 
                x = rand() % MAP_W; 
                y = rand() % MAP_H; 
                attempts++;
            } while ((world[y][x] == 1 || hypotf(x - player.pos.x, y - player.pos.y) < 10.0) && attempts < 100); 
            
            if (attempts >= 100) return; // Map is too crowded, abort this spawn gracefully
            
            mobs[i].pos = (Vec2){(float)x, (float)y};
            mobs[i].active = 1;
            mobs[i].path_ptr = -1;
            return; 
        }
    }
}

// Breadth-First Search for Enemy Pathfinding
int bfs(Entity *e, Point start, Point goal) {
    if (start.x == goal.x && start.y == goal.y) return 0;
    static Point q[MAP_W * MAP_H];
    static Point p[MAP_H][MAP_W];
    static int v[MAP_H][MAP_W];
    memset(v, 0, sizeof(v));
    
    int h = 0, t = 0;
    q[t++] = start; 
    v[start.y][start.x] = 1;

    while (h < t) {
        Point c = q[h++];
        if (c.x == goal.x && c.y == goal.y) {
            int len = 0; 
            while (c.x != start.x || c.y != start.y) { e->path[len++] = c; c = p[c.y][c.x]; }
            e->path_ptr = len - 1; 
            return 1; 
        }
        Point d[] = {{0,1},{0,-1},{1,0},{-1,0}};
        for (int i=0; i<4; i++) {
            int nx = c.x + d[i].x, ny = c.y + d[i].y;
            if (nx>=0 && nx<MAP_W && ny>=0 && ny<MAP_H && world[ny][nx] != 1 && !v[ny][nx]) {
                v[ny][nx] = 1; p[ny][nx] = c; q[t++] = (Point){nx, ny};
            }
        }
    }
    return 0; 
}

void move_player(int dx, int dy) {
    int nx = (int)player.pos.x + dx;
    int ny = (int)player.pos.y + dy;
    
    if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H && world[ny][nx] != 1) {
        player.pos.x = (float)nx;
        player.pos.y = (float)ny;
        
        if (world[ny][nx] == 2) {
            world[ny][nx] = 0;
            player.score += 15;
            player.hp += 10; 
            if(player.hp > 100) player.hp = 100;
            spawn_food();
        }
    }
}

void shoot_player(int dx, int dy) {
    if (player.reload > 0) return; 
    
    for(int b=0; b<MAX_BULLETS; b++) {
        if(!bullets[b].active) {
            bullets[b] = (Bullet){{player.pos.x, player.pos.y}, {(float)dx, (float)dy}, 1};
            player.reload = FIRE_COOLDOWN; 
            return;
        }
    }
}

void update() {
    // 1. Leveling & Dynamic Timer Spawning Logic
    game_level = (player.score / 100) + 1; 
    
    // Spawn faster as you level up
    int spawn_rate = 70 - (game_level * 10);
    if (spawn_rate < 10) spawn_rate = 10; // Cap at 1 enemy every 10 frames
    
    // Always force a minimum number of enemies on the map
    int min_enemies = game_level + 2; 
    if (min_enemies > MAX_ENEMIES) min_enemies = MAX_ENEMIES;

    if (frame_count % spawn_rate == 0 || get_active_enemy_count() < min_enemies) {
        try_spawn_enemy();
    }

    if (player.reload > 0) player.reload--;

    // 2. Update Bullets (Bullets move 3 times per frame so they can hit fast enemies)
    for(int b=0; b<MAX_BULLETS; b++) {
        if(!bullets[b].active) continue;
        
        for(int step=0; step<3; step++) {
            bullets[b].pos.x += bullets[b].vel.x; 
            bullets[b].pos.y += bullets[b].vel.y;
            int bx = (int)bullets[b].pos.x;
            int by = (int)bullets[b].pos.y;
            
            if(bx<0||bx>=MAP_W||by<0||by>=MAP_H||world[by][bx]==1) { bullets[b].active = 0; break; }
            
            for(int m=0; m<MAX_ENEMIES; m++) {
                if(mobs[m].active && (int)mobs[m].pos.x==bx && (int)mobs[m].pos.y==by) { 
                    mobs[m].active = 0; 
                    bullets[b].active = 0; 
                    player.score += 20; 
                    BEEP(); 
                    break;
                }
            }
        }
    }

    // 3. Update Enemies (Extreme speed logic)
    int frame_delay = 5 - game_level;
    int moves_per_frame = 1;
    
    // Once Level 4 is reached, delay hits 1. At Level 5+, they take multiple steps per frame.
    if (frame_delay < 1) {
        frame_delay = 1;
        moves_per_frame = game_level - 3; // Lvl 4: 1 step. Lvl 5: 2 steps. Lvl 6: 3 steps!
    }

    if (frame_count % frame_delay == 0) {
        for(int m=0; m<MAX_ENEMIES; m++) {
            if(!mobs[m].active) continue;
            
            // Loop for multiple steps per frame at high levels
            for(int step = 0; step < moves_per_frame; step++) {
                if(!mobs[m].active) break; // Check if died during a previous step
                
                bfs(&mobs[m], (Point){(int)mobs[m].pos.x, (int)mobs[m].pos.y}, (Point){(int)player.pos.x, (int)player.pos.y});
                
                if(mobs[m].path_ptr >= 0) {
                    Point next = mobs[m].path[mobs[m].path_ptr];
                    mobs[m].pos.x = (float)next.x; 
                    mobs[m].pos.y = (float)next.y;
                    
                    // LETHAL COLLISION: 40 DAMAGE!
                    if((int)mobs[m].pos.x == (int)player.pos.x && (int)mobs[m].pos.y == (int)player.pos.y) {
                        player.hp -= 40; 
                        mobs[m].active = 0; 
                        BEEP(); 
                        break; // End movement loop for this monster
                    }
                }
            }
        }
    }

    if (frame_count % 10 == 0) player.hp--; // Passive hunger drain
    frame_count++;
}

int main() {
    init_term();
    srand((unsigned int)time(NULL));

    // Map Gen
    for(int y=0; y<MAP_H; y++) {
        for(int x=0; x<MAP_W; x++) {
            if (x==0 || x==MAP_W-1 || y==0 || y==MAP_H-1) world[y][x] = 1;
            else world[y][x] = (rand()%100 < 10) ? 1 : 0;
        }
    }
     
    // Init Player
    player = (Entity){{(float)MAP_W/2, (float)MAP_H/2}, 100, 0, 0, 1};
    world[(int)player.pos.y][(int)player.pos.x] = 0; 

    spawn_food();
    for(int i=0; i<3; i++) try_spawn_enemy(); // Initial spawn

    while(player.hp > 0) {
        // --- INPUT HANDLING ---
        int input = get_input();
        if (input != -1) {
            if (input == 'w' || input == 'W') move_player(0, -1);
            if (input == 's' || input == 'S') move_player(0, 1);
            if (input == 'a' || input == 'A') move_player(-1, 0);
            if (input == 'd' || input == 'D') move_player(1, 0);
            
            if (input == 1000) shoot_player(0, -1); 
            if (input == 1001) shoot_player(0, 1);  
            if (input == 1002) shoot_player(-1, 0); 
            if (input == 1003) shoot_player(1, 0);  
            
            if (input == 'q' || input == 'Q') break; 
        }

        // --- DRAWING ---
        #if !defined(_WIN32)
            erase();
        #endif
        
        for(int y=0; y<MAP_H; y++) for(int x=0; x<MAP_W; x++) {
            if(world[y][x] == 1) DRAW_CH(x, y, '#', COL_WATER)
            else if(world[y][x] == 2) DRAW_CH(x, y, '!', COL_FOOD)
            else DRAW_CH(x, y, ' ', COL_RESET);
        }

        for(int b=0; b<MAX_BULLETS; b++) if(bullets[b].active) DRAW_CH((int)bullets[b].pos.x, (int)bullets[b].pos.y, '*', COL_BULLET);
        for(int m=0; m<MAX_ENEMIES; m++) if(mobs[m].active) DRAW_CH((int)mobs[m].pos.x, (int)mobs[m].pos.y, 'M', COL_ENEMY);
        DRAW_CH((int)player.pos.x, (int)player.pos.y, '@', COL_AGENT);

        int mobs_alive = get_active_enemy_count();
        #if defined(_WIN32)
            gotoxy(0, MAP_H + 1); printf("HP: %3d | SCORE: %4d | LEVEL: %d | MOBS: %2d   ", player.hp, player.score, game_level, mobs_alive);
            gotoxy(0, MAP_H + 3); printf("--- CONTROLS ---");
            gotoxy(0, MAP_H + 4); printf("Move : W A S D");
            gotoxy(0, MAP_H + 5); printf("Shoot: ARROW KEYS");
            gotoxy(0, MAP_H + 6); printf("Quit : Q");
            gotoxy(20, MAP_H + 3); printf("--- LEGEND ---");
            gotoxy(20, MAP_H + 4); set_color(COL_AGENT); printf("@"); set_color(COL_RESET); printf(" : Player");
            gotoxy(20, MAP_H + 5); set_color(COL_ENEMY); printf("M"); set_color(COL_RESET); printf(" : Enemy (40 DMG!)");
            gotoxy(20, MAP_H + 6); set_color(COL_FOOD); printf("!"); set_color(COL_RESET); printf(" : Food");
        #else
            mvprintw(MAP_H+1, 0, "HP: %3d | SCORE: %4d | LEVEL: %d | MOBS: %2d", player.hp, player.score, game_level, mobs_alive);
            mvprintw(MAP_H+3, 0, "--- CONTROLS ---");
            mvprintw(MAP_H+4, 0, "Move : W A S D");
            mvprintw(MAP_H+5, 0, "Shoot: ARROW KEYS");
            mvprintw(MAP_H+6, 0, "Quit : Q");
            mvprintw(MAP_H+3, 20, "--- LEGEND ---");
            attron(COLOR_PAIR(COL_AGENT)); mvaddch(MAP_H+4, 20, '@'); attroff(COLOR_PAIR(COL_AGENT)); printw(" : Player");
            attron(COLOR_PAIR(COL_ENEMY)); mvaddch(MAP_H+5, 20, 'M'); attroff(COLOR_PAIR(COL_ENEMY)); printw(" : Enemy (40 DMG!)");
            attron(COLOR_PAIR(COL_FOOD)); mvaddch(MAP_H+6, 20, '!'); attroff(COLOR_PAIR(COL_FOOD)); printw(" : Food");
            refresh();
        #endif

        update(); 
        SLEEP_MS(50); 
    }

    // Game Over 
    #if defined(_WIN32)
        gotoxy(MAP_W/2-5, MAP_H/2); 
        if (player.hp <= 0) printf(" YOU DIED! SCORE: %d ", player.score);
        else printf("  QUITTER  ");
        while(!_kbhit()); _getch();
    #else
        nodelay(stdscr, FALSE); 
        if (player.hp <= 0) mvprintw(MAP_H/2, MAP_W/2-10, " YOU DIED! SCORE: %d ", player.score);
        else mvprintw(MAP_H/2, MAP_W/2-5, "  QUITTER  ");
        refresh(); getch();
    #endif
    
    end_term();
    return 0;
}