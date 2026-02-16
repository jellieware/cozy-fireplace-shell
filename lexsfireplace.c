#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 60
#define HEIGHT 24

// This array maps to 256-color palette (flame colors: black -> red -> orange -> yellow -> white)
int fire_palette[] = {
    16, 52, 88, 124, 160, 196, 202, 208, 214, 220, 226, 227, 230, 231, 231, 231, 231, 231, 231, 231, 231, 231,
};
#define PALETTE_SIZE 22

int main() {
    int fire[HEIGHT][WIDTH] = {0};
    
    // Initialize ncurses
    initscr();
    start_color();
    use_default_colors();
    curs_set(0); // Hide cursor
    timeout(30); // Frame delay in ms

    // Initialize Color Pairs
    // We pair the fire color with itself (foreground/background) to make a solid block
    for (int i = 0; i < PALETTE_SIZE; i++) {
        init_pair(i + 1, fire_palette[i], fire_palette[i]);
    }

    while (getch() != 'q') {
        // 1. Seed the bottom row with random heat
        for (int x = 0; x < WIDTH; x++) {
            fire[HEIGHT - 1][x] = rand() % PALETTE_SIZE;
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
                if (avg > 0 && rand() % 18 > 16) {
                    fire[y][x] = avg - 6; // Decay
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
                    mvaddstr(y, x * 2, "  ");
                    attroff(COLOR_PAIR(color_idx));
                }
            }
        }
        refresh();
    }

    endwin();
    return 0;
}
