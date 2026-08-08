#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define MAX_W 512
#define MAX_H 256
#define MAX_LOG_SPARKS 40
#define MAX_ASH_PARTICLES 70
#define MAX_RED_PARTICLES 40

int fire_pixels[MAX_H][MAX_W];

typedef struct {
    int active;
    float x;
    float y;     // from fire_h - 1 (bottom of fire) up to 0.0 (top of flames)
    float speed;
    float life;
} AshParticle;

typedef struct {
    int active;
    float x;
    float y;     // can range from flame top down into negative values (upper grid)
    float speed;
    int color_pair;
    float life;
} RedParticle;

static AshParticle ash_particles[MAX_ASH_PARTICLES];
static RedParticle red_particles[MAX_RED_PARTICLES];

typedef struct {
    int r, g, b;
} RGB;

// Smooth 24-Bit RGB Heat Curve (Ted Martens Fire Aesthetic with Dark Red -> Black Fade)
RGB get_24bit_fire_color(int heat) {
    if (heat <= 0) return (RGB){0, 0, 0};

    float t = (float)heat / 36.0f; // Normalized 0.0 -> 1.0

    if (t > 0.85f) { 
        // 24-Bit Creamy Warm White Core
        float f = (t - 0.85f) / 0.15f;
        return (RGB){255, (int)(230 + f * 25), (int)(160 + f * 95)};
    } else if (t > 0.60f) {
        // 24-Bit Golden Yellow Transition
        float f = (t - 0.60f) / 0.25f;
        return (RGB){255, (int)(145 + f * 85), (int)(10 + f * 150)};
    } else if (t > 0.35f) {
        // 24-Bit Vibrant Pixel Flame Orange
        float f = (t - 0.35f) / 0.25f;
        return (RGB){255, (int)(45 + f * 100), 0};
    } else if (t > 0.15f) {
        // 24-Bit Rich Crimson Red
        float f = (t - 0.15f) / 0.20f;
        return (RGB){(int)(125 + f * 130), (int)(5 + f * 20), 0};
    } else {
        // Deep Dark Red fading smoothly down to near-black
        float f = t / 0.15f;
        return (RGB){(int)(f * 70), 0, 0};
    }
}

void setup_native_fire_palette() {
    start_color();
    use_default_colors();
    for (int heat = 1; heat <= 36; heat++) {
        RGB c = get_24bit_fire_color(heat);
        if (can_change_color()) {
            init_color(heat, (c.r * 1000) / 255, (c.g * 1000) / 255, (c.b * 1000) / 255);
            init_pair(heat, heat, -1);
        } else {
            int pair_color = (heat > 23) ? COLOR_WHITE : (heat > 15) ? COLOR_YELLOW : COLOR_RED;
            init_pair(heat, pair_color, -1);
        }
    }
    if (can_change_color()) {
        init_color(41, 240, 160, 50);  // Bright Spark Yellow

        // Black Ash / Soot Pixel Color
        init_color(50, 0, 0, 0);

        // Fading Red Palette for Upper Grid Rising Pixels
        init_color(51, 1000, 120, 120); // Bright Red (Flame top)
        init_color(52, 750,  80,  80);  // Medium Red
        init_color(53, 500,  45,  45);  // Dark Red Fade
        init_color(54, 250,  20,  20);  // Faint Upper Grid Red Fade
    }
    init_pair(41, 41, -1);
    init_pair(50, can_change_color() ? 50 : COLOR_BLACK, -1);
    init_pair(51, can_change_color() ? 51 : COLOR_RED, -1);
    init_pair(52, can_change_color() ? 52 : COLOR_RED, -1);
    init_pair(53, can_change_color() ? 53 : COLOR_RED, -1);
    init_pair(54, can_change_color() ? 54 : COLOR_RED, -1);
}

void update_fire(int fire_w, int fire_h) {
    static float time_counter = 0.0f;
    time_counter += 0.08f;

    float r1 = -0.28f;
    float r2 = 0.00f;
    float r3 = 0.28f;

    float morph1 = (sinf(time_counter * 1.8f) * 0.05f) + (cosf(time_counter * 3.4f) * 0.02f);
    float morph2 = (cosf(time_counter * 2.3f) * 0.06f) + (sinf(time_counter * 4.2f) * 0.02f);
    float morph3 = (sinf(time_counter * 2.0f) * 0.05f) + (cosf(time_counter * 3.1f) * 0.02f);

    float height_mod1 = 0.15f * sinf(time_counter * 2.5f);
    float height_mod2 = 0.18f * cosf(time_counter * 2.1f);
    float height_mod3 = 0.15f * sinf(time_counter * 2.9f);

    for (int x = 0; x < fire_w; x++) {
        float norm_x = ((float)x / (float)fire_w) * 2.0f - 1.0f;
        float c1 = r1 + morph1 * 0.3f;
        float c2 = r2 + morph2 * 0.3f;
        float c3 = r3 + morph3 * 0.3f;

        float g1 = expf(-powf((norm_x - c1) / 0.22f, 2.0f)) * 32.0f;
        float g2 = expf(-powf((norm_x - c2) / 0.22f, 2.0f)) * 36.0f;
        float g3 = expf(-powf((norm_x - c3) / 0.22f, 2.0f)) * 32.0f;

        int base_heat = (int)(g1 + g2 + g3);
        if (base_heat > 0 && rand() % 5 == 0) {
            base_heat += (rand() % 5) - 2;
        }
        if (base_heat > 36) base_heat = 36;
        if (base_heat < 0) base_heat = 0;

        fire_pixels[fire_h - 1][x] = base_heat;
    }

    for (int x = 1; x < fire_w - 1; x++) {
        fire_pixels[fire_h - 1][x] = (fire_pixels[fire_h - 1][x - 1] + fire_pixels[fire_h - 1][x] * 2 + fire_pixels[fire_h - 1][x + 1]) / 4;
    }

    for (int y = 1; y < fire_h; y++) {
        for (int x = 0; x < fire_w; x++) {
            int left = (x > 0) ? fire_pixels[y][x - 1] : fire_pixels[y][x];
            int right = (x < fire_w - 1) ? fire_pixels[y][x + 1] : fire_pixels[y][x];
            int curr = fire_pixels[y][x];

            if (curr == 0 && left == 0 && right == 0) {
                fire_pixels[y - 1][x] = 0;
                continue;
            }

            int blended = (curr * 4 + left + right) / 6;
            int drift = 0;
            int rnd = rand() % 100;
            if (rnd < 40) drift = 0;
            else if (rnd < 70) drift = (sinf(time_counter * 2.0f + y * 0.15f) > 0) ? 1 : -1;
            else drift = (rand() % 3) - 1;

            int dst_x = x + drift;
            if (dst_x < 0) dst_x = 0;
            if (dst_x >= fire_w) dst_x = fire_w - 1;

            float norm_x = ((float)x / (float)fire_w) * 2.0f - 1.0f;
            float height_factor = (float)y / (float)fire_h;

            float noise_field = sinf(time_counter * 3.2f + height_factor * 10.0f + norm_x * 4.0f) * cosf(time_counter * 2.1f - height_factor * 6.0f);
            float spire_sway = noise_field * 0.12f * (1.0f - height_factor);

            float c1 = r1 + morph1 + spire_sway;
            float c2 = r2 + morph2 + spire_sway;
            float c3 = r3 + morph3 + spire_sway;

            float d1 = fabsf(norm_x - c1);
            float d2 = fabsf(norm_x - c2);
            float d3 = fabsf(norm_x - c3);

            float min_dist = d1;
            if (d2 < min_dist) min_dist = d2;
            if (d3 < min_dist) min_dist = d3;

            int decay = 1;

            if (height_factor < 0.50f) {
                if (noise_field > 0.35f && min_dist < 0.12f) {
                    decay = -1; 
                } else if (noise_field < -0.30f && min_dist < 0.18f) {
                    decay = 4;  
                } else {
                    decay = 2;
                }
            } else if (height_factor < 0.85f) {
                if (min_dist > 0.11f && min_dist <= 0.22f) decay += 2;
                if (min_dist > 0.22f) decay += 5;
            } else {
                if (fabsf(norm_x) > 0.62f) decay += 3;
            }

            if (height_factor < (0.60f + height_mod2) && d2 >= 0.08f) decay += 2;
            if (height_factor < (0.44f + height_mod1) && d1 < 0.14f) decay += 2;
            if (height_factor < (0.44f + height_mod3) && d3 < 0.14f) decay += 2;
            if (height_factor < 0.35f) decay += 2;
            if (height_factor < 0.15f) decay += 4;

            int new_heat = blended - decay;

            if (new_heat < 1) {
                new_heat = 0; 
            } else if (new_heat > 36) {
                new_heat = 36;
            }

            fire_pixels[y - 1][dst_x] = new_heat;
        }
    }

    if (rand() % 2 == 0) {
        float roots[3] = {r1 + morph1, r2 + morph2, r3 + morph3};
        float target_r = roots[rand() % 3];
        int ex = (int)(((target_r + 1.0f) / 2.0f) * fire_w) + ((rand() % 3) - 1);
        int ey = (fire_h / 3) + (rand() % (fire_h / 3));
        if (ex >= 0 && ex < fire_w && ey > 1) {
            if (fire_pixels[ey][ex] > 4 && fire_pixels[ey][ex] < 20) {
                fire_pixels[ey - 2][ex] = fire_pixels[ey][ex] + 10;
            }
        }
    }
}

void update_ash_particles(int fire_w, int fire_h) {
    for (int i = 0; i < MAX_ASH_PARTICLES; i++) {
        if (ash_particles[i].active) {
            ash_particles[i].y -= ash_particles[i].speed;
            ash_particles[i].x += ((float)(rand() % 3) - 1.0f) * 0.2f;
            if (ash_particles[i].x < 0) ash_particles[i].x = 0;
            if (ash_particles[i].x >= fire_w) ash_particles[i].x = fire_w - 1;

            ash_particles[i].life -= 0.035f;
            if (ash_particles[i].y < 0.0f || ash_particles[i].life <= 0.0f) {
                ash_particles[i].active = 0;
            }
        }
    }

    for (int i = 0; i < MAX_ASH_PARTICLES; i++) {
        if (!ash_particles[i].active) continue;
        int x1 = (int)(ash_particles[i].x + 0.5f);
        int y1 = (int)(ash_particles[i].y + 0.5f);

        for (int j = i + 1; j < MAX_ASH_PARTICLES; j++) {
            if (!ash_particles[j].active) continue;
            int x2 = (int)(ash_particles[j].x + 0.5f);
            int y2 = (int)(ash_particles[j].y + 0.5f);

            int dx = abs(x1 - x2);
            int dy = abs(y1 - y2);
            if (dx + dy <= 1) {
                ash_particles[j].active = 0;
            }
        }
    }

    for (int i = 0; i < MAX_ASH_PARTICLES; i++) {
        if (!ash_particles[i].active) {
            if (rand() % 45 == 0) {
                float new_x = (float)(rand() % fire_w);
                float new_y = (float)(fire_h - 1);
                int nx = (int)(new_x + 0.5f);
                int ny = (int)(new_y + 0.5f);

                int can_spawn = 1;
                for (int j = 0; j < MAX_ASH_PARTICLES; j++) {
                    if (!ash_particles[j].active) continue;
                    int jx = (int)(ash_particles[j].x + 0.5f);
                    int jy = (int)(ash_particles[j].y + 0.5f);
                    if (abs(nx - jx) + abs(ny - jy) <= 1) {
                        can_spawn = 0;
                        break;
                    }
                }

                if (can_spawn) {
                    ash_particles[i].active = 1;
                    ash_particles[i].x = new_x;
                    ash_particles[i].y = new_y;
                    ash_particles[i].speed = 0.35f + ((float)(rand() % 25) / 100.0f);
                    ash_particles[i].life = 1.0f;
                }
            }
        }
    }
}

void update_red_particles(int fire_w, int fire_h) {
    for (int i = 0; i < MAX_RED_PARTICLES; i++) {
        if (red_particles[i].active) {
            red_particles[i].y -= red_particles[i].speed;
            red_particles[i].x += ((float)(rand() % 3) - 1.0f) * 0.25f;
            if (red_particles[i].x < 0) red_particles[i].x = 0;
            if (red_particles[i].x >= fire_w) red_particles[i].x = fire_w - 1;

            red_particles[i].life -= 0.03f;
            
            int color_idx = (int)((1.0f - red_particles[i].life) * 3.99f);
            if (color_idx < 0) color_idx = 0;
            if (color_idx > 3) color_idx = 3;
            red_particles[i].color_pair = 51 + color_idx;

            if (red_particles[i].y < -20.0f || red_particles[i].life <= 0.0f) {
                red_particles[i].active = 0;
            }
        }
    }

    for (int i = 0; i < MAX_RED_PARTICLES; i++) {
        if (!red_particles[i].active) continue;
        int x1 = (int)(red_particles[i].x + 0.5f);
        int y1 = (int)(red_particles[i].y + 0.5f);

        for (int j = i + 1; j < MAX_RED_PARTICLES; j++) {
            if (!red_particles[j].active) continue;
            int x2 = (int)(red_particles[j].x + 0.5f);
            int y2 = (int)(red_particles[j].y + 0.5f);

            int dx = abs(x1 - x2);
            int dy = abs(y1 - y2);
            if (dx + dy <= 1) {
                red_particles[j].active = 0;
            }
        }
    }

    for (int i = 0; i < MAX_RED_PARTICLES; i++) {
        if (!red_particles[i].active) {
            if (rand() % 6 == 0) {
                int new_x_int = rand() % fire_w;
                
                int flame_top_y = -1;
                for (int y = 0; y < fire_h; y++) {
                    if (fire_pixels[y][new_x_int] > 0) {
                        flame_top_y = y;
                        break;
                    }
                }

                if (flame_top_y != -1) {
                    float new_x = (float)new_x_int;
                    float new_y = (float)flame_top_y;
                    int nx = new_x_int;
                    int ny = flame_top_y;

                    int can_spawn = 1;
                    for (int j = 0; j < MAX_RED_PARTICLES; j++) {
                        if (!red_particles[j].active) continue;
                        int jx = (int)(red_particles[j].x + 0.5f);
                        int jy = (int)(red_particles[j].y + 0.5f);
                        if (abs(nx - jx) + abs(ny - jy) <= 1) {
                            can_spawn = 0;
                            break;
                        }
                    }

                    if (can_spawn) {
                        red_particles[i].active = 1;
                        red_particles[i].x = new_x;
                        red_particles[i].y = new_y;
                        red_particles[i].speed = 0.25f + ((float)(rand() % 20) / 100.0f);
                        red_particles[i].life = 1.0f;
                        red_particles[i].color_pair = 51;
                    }
                }
            }
        }
    }
}

void render_scene(int fire_w, int fire_h) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int offset_y = max_y - fire_h;
    int offset_x = (max_x - (fire_w * 2)) / 2;
    if (offset_y < 0) offset_y = 0;
    if (offset_x < 0) offset_x = 0;

    update_ash_particles(fire_w, fire_h);
    update_red_particles(fire_w, fire_h);

    // 1. Render Fire Section
    for (int y = 0; y < fire_h; y++) {
        for (int x = 0; x < fire_w; x++) {
            int is_ash = 0;
            for (int i = 0; i < MAX_ASH_PARTICLES; i++) {
                if (ash_particles[i].active) {
                    int ay = (int)(ash_particles[i].y + 0.5f);
                    int ax = (int)(ash_particles[i].x + 0.5f);
                    if (ay == y && ax == x) {
                        is_ash = 1;
                        break;
                    }
                }
            }

            int px = offset_x + (x * 2);
            int py = offset_y + y;

            if (is_ash) {
                attron(COLOR_PAIR(50));
                mvaddstr(py, px, "██");
                attroff(COLOR_PAIR(50));
            } else {
                int heat = fire_pixels[y][x];
                if (heat <= 0) {
                    if (y >= fire_h - 2 && (rand() % 35 == 0)) {
                        attron(COLOR_PAIR(41)); 
                        mvaddstr(py, px, "..");
                        attroff(COLOR_PAIR(41));
                    } else {
                        continue;
                    }
                } else {
                    if (heat > 36) heat = 36;
                    attron(COLOR_PAIR(heat));
                    mvaddstr(py, px, "██");
                    attroff(COLOR_PAIR(heat));
                }
            }
        }
    }

    // 2. Render Rising Red Particles
    for (int i = 0; i < MAX_RED_PARTICLES; i++) {
        if (red_particles[i].active) {
            int py = offset_y + (int)(red_particles[i].y + 0.5f);
            int px = offset_x + ((int)(red_particles[i].x + 0.5f) * 2);
            if (py >= 0 && py < max_y && px >= 0 && px + 1 < max_x) {
                attron(COLOR_PAIR(red_particles[i].color_pair));
                mvaddstr(py, px, "██");
                attroff(COLOR_PAIR(red_particles[i].color_pair));
            }
        }
    }
}

int main() {
    srand(time(NULL));
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(45);

    if (!has_colors()) {
        endwin();
        printf("Your terminal does not support colors.\n");
        return 1;
    }

    setup_native_fire_palette();

    int last_w = 0, last_h = 0;

    while (1) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        float target_aspect = 2.0f;
        int max_fire_w = max_x / 2;
        int max_fire_h = max_y;
        int fire_w, fire_h;

        if (max_fire_h < 5) max_fire_h = 5;

        if ((float)max_fire_w / max_fire_h > target_aspect) {
            fire_h = max_fire_h;
            fire_w = (int)(fire_h * target_aspect);
        } else {
            fire_w = max_fire_w;
            fire_h = (int)(fire_w / target_aspect);
        }

        if (fire_w > MAX_W) fire_w = MAX_W;
        if (fire_h > MAX_H) fire_h = MAX_H;

        if (fire_w != last_w || fire_h != last_h) {
            erase();
            last_w = fire_w;
            last_h = fire_h;
        }

        erase();
        update_fire(fire_w, fire_h);
        render_scene(fire_w, fire_h);
        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        }
    }

    endwin();
    return 0;
}
