#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 68
#define HEIGHT 28

// This array maps to 256-color palette (flame colors: black -> red -> orange -> yellow -> white)
int fire_palette[] = {
    16, 52, 88, 124, 160, 196, 202, 208, 214, 220, 226, 227, 228, 229, 230, 231, 255, 254, 253, 252, 251, 250, 249, 248

};
// 1. Array bounds set exactly to 27 steps
#define PALETTE_SIZE 36

// 2. Struct using short values for ncurses mapping
typedef struct {
    short r; // Red (0 - 1000)
    short g; // Green (0 - 1000)
    short b; // Blue (0 - 1000)
} RGBColor;

// 3. REVERSED 27-Shade Loop: Black -> Deep Red -> Orange -> Brilliant White-Gold
RGBColor fire_palette_24bit[PALETTE_SIZE] = {
    // --- START: Pure Black to Faint Embers (Shades 0 - 6) ---
    {0, 0, 0},          // 0: Pure Black / Extinction
    {10, 0, 0},         // 1: Absolute final ember hue
    {30, 0, 0},         // 2: Near darkness
    {60, 0, 0},         // 3: Smoldering trace
    {100, 0, 0},        // 4: Dim glow
    {160, 0, 0},        // 5: Very dark red ember
    {230, 0, 0},        // 6: Faint charcoal red

    // --- ACCELERATION: Deep Reds to Saturated Red (Shades 7 - 13) ---
    {300, 0, 0},        // 7: Dark burgundy
    {380, 0, 0},        // 8: Deep crimson
    {460, 0, 0},        // 9: Dark blood red
    {550, 0, 0},        // 10: Medium red
    {650, 0, 0},        // 11: Intense pure red
    {750, 20, 0},       // 12: Green begins to spark
    {850, 90, 0},       // 13: Saturated red-orange

    // --- TRANSITION: Dense, Rich Oranges (Shades 14 - 20) ---
    {950, 180, 0},      // 14: Dark reddish-orange
    {1000, 250, 0},     // 15: Deep safety orange
    {1000, 320, 0},     // 16: Burnt orange
    {1000, 390, 0},     // 17: Medium dark orange
    {1000, 460, 0},     // 18: Pure vibrant orange
    {1000, 540, 0},     // 19: Vivid orange-yellow
    {1000, 620, 0},     // 20: Deep amber

    // --- PEAK: Deep Yellow up to White-Hot Gold Core (Shades 21 - 26) ---
    {1000, 700, 0},     // 21: Light amber
    {1000, 780, 0},     // 22: Deep yellow
    {1000, 840, 40},    // 23: Warm yellow-gold
    {1000, 900, 150},   // 24: Golden yellow
    {1000, 950, 400},   // 25: Highly saturated intense gold
    {1000, 1000, 700}   // 26: White-hot bright gold core
};


//#define PALETTE_SIZE 24

int main() {
    int fire[HEIGHT][WIDTH] = {0};
    int x = 0;
    int y;
    int z;
    int max_x, max_y;

    // Initialize ncurses
    initscr();
    start_color();
    use_default_colors();
    curs_set(0); // Hide cursor
    timeout(20); // Frame delay in ms
    
    // Initialize Color Pairs
    // We pair the fire color with itself (foreground/background) to make a solid block
    for (int i = 0; i < PALETTE_SIZE; i++) {
        //init_pair(i + 1, fire_palette[i], fire_palette[i]);
        init_color(16 + i, fire_palette_24bit[i].r, fire_palette_24bit[i].g, fire_palette_24bit[i].b);
        init_pair(i + 1, 16 + i, 16 + i);
    }

    while (getch() != 'q') {
        // 1. Seed the bottom row with random heat
        for (int x = 0; x < WIDTH; x++) {
            fire[HEIGHT - 2][x] = rand() % PALETTE_SIZE;
        }

        // 2. Propagate heat upwards
        for (int y = 0; y < HEIGHT - 1; y++) {
            for (int x = 0; x < WIDTH; x++) {
                // Average the pixels below and add a bit of decay
                int total = fire[(y + 1) % HEIGHT][(x - 1 + WIDTH) % WIDTH] +
                            fire[(y + 1) % HEIGHT][x] +
                            fire[(y + 1) % HEIGHT][(x + 1) % WIDTH] +
                            fire[(y + 2) % HEIGHT][x];
                
                int avg = total / 4;
                if (avg > 0 && rand() % 24 > 20) {
                    fire[y][x] = avg - 2; // Decay
                } else {
                    fire[y][x] = avg;
                }
            }
        }

        // 3. Render to screen
        erase();
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int color_idx = fire[y][x];
                if (color_idx > 0) {
                    attron(COLOR_PAIR(color_idx));
                    // Use two spaces to create a "square" block look
                    getmaxyx(stdscr, max_y, max_x); // Get screen dimensions (LINES and COLS)
                    z = max_y - 28;
                    mvaddstr(y + z, x * 2, "  ");
                    attroff(COLOR_PAIR(color_idx));
                }
            }
        }
        refresh();
    }

    endwin();
    return 0;
}
