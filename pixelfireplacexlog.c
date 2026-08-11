#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define MAX_W 512
#define MAX_H 256
#define MAX_SPARKS 1500      // Buffer for active sparks
#define MAX_LOG_PIXELS 40    // Buffer for rising pixels inside the log

// Adjustable speed parameter for rising log pixels (higher = faster)
#define LOG_PIXEL_SPEED 0.25f  

#define SAMPLE_RATE 44100
#define CHANNELS 1
#define BUFFER_SIZE 4096

#define NUM_BROWN_SHADES 8
#define BROWN_PAIR_START 37

// Global flag to cleanly stop threads on exit
volatile int running = 1;

int fire_pixels[MAX_H][MAX_W];

typedef struct {
    float x, y;
    float vx, vy;
    int life;
    int max_life;
} Spark;

Spark sparks[MAX_SPARKS];

typedef struct {
    int x;
    float y;
    float speed;
    int active;
} LogPixel;

LogPixel log_pixels[MAX_LOG_PIXELS];

typedef struct {
    int r, g, b;
} RGB;

// Ted Martens Pixel Art Fire Palette (Deep Burgundy -> Crimson -> Orange -> Golden -> White Core)
RGB get_ted_martens_color(int heat) {
    if (heat <= 0) return (RGB){0, 0, 0};

    float t = (float)heat / 36.0f;

    if (t > 0.88f) { 
        float factor = (t - 0.88f) / 0.12f;
        return (RGB){255, (int)(240 + factor * 15), (int)(180 + factor * 75)};
    } else if (t > 0.65f) { 
        float factor = (t - 0.65f) / 0.23f;
        return (RGB){255, (int)(160 + factor * 80), (int)(10 + factor * 170)};
    } else if (t > 0.40f) { 
        float factor = (t - 0.40f) / 0.25f;
        return (RGB){255, (int)(60 + factor * 100), 0};
    } else if (t > 0.18f) { 
        float factor = (t - 0.18f) / 0.22f;
        return (RGB){(int)(120 + factor * 135), (int)(5 + factor * 15), 0};
    } else { 
        float factor = t / 0.18f;
        return (RGB){(int)(factor * 120), 0, (int)(factor * 20)};
    }
}

void setup_native_fire_palette() {
    start_color();
    use_default_colors();

    // Setup 36 Fire Heat Color Pairs
    for (int heat = 1; heat <= 36; heat++) {
        RGB c = get_ted_martens_color(heat);

        if (can_change_color()) {
            init_color(heat, (c.r * 1000) / 255, (c.g * 1000) / 255, (c.b * 1000) / 255);
            init_pair(heat, heat, -1);
        } else {
            int pair_color = (heat > 28) ? COLOR_WHITE : (heat > 18) ? COLOR_YELLOW : COLOR_RED;
            init_pair(heat, pair_color, -1);
        }
    }

    // Setup 8 Brown Color Pairs for 3D Log
    RGB brown_colors[NUM_BROWN_SHADES] = {
        { 45,  22,   8},  // 37: Deep Shadow Brown
        { 75,  38,  12},  // 38: Dark Bark
        {105,  52,  18},  // 39: Medium Dark Bark
        {135,  68,  24},  // 40: Medium Brown
        {165,  85,  30},  // 41: Warm Wood
        {195, 105,  38},  // 42: Light Wood Highlight
        {225, 130,  48},  // 43: Bright Top Highlight
        {250, 160,  65}   // 44: Ember Lighted Wood Grain
    };

    for (int i = 0; i < NUM_BROWN_SHADES; i++) {
        int pair_id = BROWN_PAIR_START + i;
        if (can_change_color()) {
            init_color(pair_id, (brown_colors[i].r * 1000) / 255,
                               (brown_colors[i].g * 1000) / 255,
                               (brown_colors[i].b * 1000) / 255);
            init_pair(pair_id, pair_id, -1);
        } else {
            init_pair(pair_id, COLOR_YELLOW, -1);
        }
    }
}

// Procedural 3D Log Geometry Generator
int get_log_base_shade(int x, int y, int fire_w, int fire_h) {
    if (fire_h < 8 || fire_w < 10) return -1;

    int x1_front = (int)(fire_w * 0.25);
    int x2_front = (int)(fire_w * 0.75);
    int cy_front = fire_h - 4;
    int radius_front = 3;

    // 1. 2-Pixel Wide Stem extending upward from front log
    int stem_x1 = (int)(fire_w * 0.62);
    int stem_x2 = stem_x1 + 1;
    int stem_top = cy_front - 6;
    int stem_bottom = cy_front - 4;

    if (x >= stem_x1 && x <= stem_x2 && y >= stem_top && y <= stem_bottom) {
        if (y == stem_top) return 6;      
        if (y == stem_top + 1) return 4;  
        if (y == stem_bottom) return 3;   
    }

    // 2. Front Main Log
    if (x >= x1_front && x <= x2_front) {
        int dy = y - cy_front;
        if (abs(dy) <= radius_front) {
            // Eliminate the 4 outer corner pixels of the log
            if ((x == x1_front || x == x2_front) && abs(dy) == radius_front) {
                return -1;
            }

            if (x <= x1_front + 1 || x >= x2_front - 1) {
                if (dy == 0) return 6;                
                if (abs(dy) == 1 || abs(dy) == 2) return 2; 
                return 4;                             
            }
            if (dy == -3) return 6; 
            if (dy == -2) return 5; 
            if (dy == -1) return 4; 
            if (dy == 0)  return 3; 
            if (dy == 1)  return 2; 
            if (dy == 2)  return 1; 
            if (dy == 3)  return 0; 
        }
    }

    return -1;
}

// Checks if a candidate position touches any existing active log pixel (above/below/left/right/diagonal)
int is_valid_log_pixel_pos(int x, int y, int self_idx) {
    for (int i = 0; i < MAX_LOG_PIXELS; i++) {
        if (i == self_idx || !log_pixels[i].active) continue;
        int px = log_pixels[i].x;
        int py = (int)log_pixels[i].y;
        if (abs(px - x) <= 1 && abs(py - y) <= 1) {
            return 0; // Touching or adjacent
        }
    }
    return 1;
}

void update_log_pixels(int fire_w, int fire_h) {
    int x1_front = (int)(fire_w * 0.25);
    int x2_front = (int)(fire_w * 0.75);
    int cy_front = fire_h - 4;
    int y_bottom = cy_front + 3;
    int y_top = cy_front - 3;

    // 1. Move active rising pixels
    for (int i = 0; i < MAX_LOG_PIXELS; i++) {
        if (!log_pixels[i].active) continue;

        float new_y = log_pixels[i].y - log_pixels[i].speed;
        int current_y_int = (int)log_pixels[i].y;
        int new_y_int = (int)new_y;

        if (new_y < y_top) {
            log_pixels[i].active = 0; // Faded out at top edge of log
        } else if (new_y_int != current_y_int) {
            if (is_valid_log_pixel_pos(log_pixels[i].x, new_y_int, i)) {
                log_pixels[i].y = new_y;
            } else {
                log_pixels[i].active = 0; // Deactivate if blocked to maintain spacing
            }
        } else {
            log_pixels[i].y = new_y;
        }
    }

    // 2. Spawn new pixels at the bottom of the log
    if (rand() % 2 == 0) {
        int spawn_x = x1_front + 2 + (rand() % (x2_front - x1_front - 3));
        if (is_valid_log_pixel_pos(spawn_x, y_bottom, -1)) {
            for (int i = 0; i < MAX_LOG_PIXELS; i++) {
                if (!log_pixels[i].active) {
                    log_pixels[i].x = spawn_x;
                    log_pixels[i].y = (float)y_bottom;
                    // Speed driven by LOG_PIXEL_SPEED macro with slight random variation
                    log_pixels[i].speed = LOG_PIXEL_SPEED + ((rand() % 100) / 100.0f) * (LOG_PIXEL_SPEED * 0.5f);
                    log_pixels[i].active = 1;
                    break;
                }
            }
        }
    }
}

void update_fire(int fire_w, int fire_h) {
    static float time_counter = 0.0f;
    static float wind_bias = 0.0f;
    time_counter += 0.08f;

    wind_bias = sinf(time_counter * 0.4f) * 0.6f;

    int base_y = fire_h - 7;
    if (base_y < 0) base_y = fire_h - 1;

    // Update internal log rising pixels
    update_log_pixels(fire_w, fire_h);

    // 1. Base Heat: Center-concentrated Gaussian curve
    for (int x = 0; x < fire_w; x++) {
        float dx = ((float)x / (float)fire_w) * 2.0f - 1.0f;
        float center_weight = expf(-powf(dx, 2.0f) * 5.0f);

        float spire1 = sinf(dx * 8.0f + time_counter * 2.5f);
        float spire2 = cosf(dx * 14.0f - time_counter * 3.0f);
        float base_wave = (spire1 * 0.5f) + (spire2 * 0.5f);

        int base_heat = (int)((26.0f + base_wave * 10.0f) * center_weight);

        if (fabsf(dx) < 0.3f && (rand() % 5 == 0)) {
            base_heat += rand() % 8;
        }

        if (base_heat > 36) base_heat = 36;
        if (base_heat < 0)  base_heat = 0;

        fire_pixels[base_y][x] = base_heat;
    }

    // 2. Propagate Upward 
    for (int y = 1; y < fire_h; y++) {
        for (int x = 0; x < fire_w; x++) {
            int left  = (x > 0) ? fire_pixels[y][x - 1] : fire_pixels[y][x];
            int right = (x < fire_w - 1) ? fire_pixels[y][x + 1] : fire_pixels[y][x];
            int curr  = fire_pixels[y][x];

            if (curr == 0 && left == 0 && right == 0) {
                fire_pixels[y - 1][x] = 0;
                continue;
            }

            int blended = (curr * 3 + left + right) / 5;

            int drift = 0;
            int rnd = rand() % 100;
            if (rnd < 30) drift = 0;
            else if (rnd < 60) drift = (wind_bias > 0) ? 1 : -1;
            else if (rnd < 80) drift = (wind_bias > 0) ? -1 : 1;
            else drift = (rand() % 3) - 1;

            int dst_x = x + drift;
            if (dst_x < 0) dst_x = 0;
            if (dst_x >= fire_w) dst_x = fire_w - 1;

            float dx = ((float)x / (float)fire_w) * 2.0f - 1.0f;
            float dist_from_center = fabsf(dx);

            int decay = 1;

            if (dist_from_center > 0.25f) decay += 1;
            if (dist_from_center > 0.50f) decay += 2;
            if (dist_from_center > 0.70f) decay += 4;

            if (y < fire_h * 0.70f && dist_from_center > 0.3f) decay += 1;
            if (y < fire_h * 0.40f) decay += 2;
            if (y < fire_h * 0.20f) decay += 3;

            if (abs(curr - left) > 6 || abs(curr - right) > 6) {
                decay += 2;
            }

            int new_heat = blended - decay;
            fire_pixels[y - 1][dst_x] = (new_heat > 0) ? new_heat : 0;
        }
    }

    // 3. Spire Top Embers
    if (rand() % 2 == 0) {
        int center_offset = (rand() % (fire_w / 3)) - (fire_w / 6);
        int ex = (fire_w / 2) + center_offset;
        int ey = (fire_h / 4) + (rand() % (fire_h / 3));

        if (ex >= 0 && ex < fire_w && ey > 1) {
            if (fire_pixels[ey][ex] > 6 && fire_pixels[ey][ex] < 24) {
                fire_pixels[ey - 1][ex] = fire_pixels[ey][ex] + 8;
            }
        }
    }

    // 4. Rising spark generator
    for (int y = 0; y < fire_h; y++) {
        for (int x = 0; x < fire_w; x++) {
            int heat = fire_pixels[y][x];

            if (heat >= 4 && heat <= 15 && (rand() % 40 == 0)) {
                for (int i = 0; i < MAX_SPARKS; i++) {
                    if (sparks[i].life <= 0) {
                        sparks[i].x = x;
                        sparks[i].y = y;
                        sparks[i].vx = ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
                        sparks[i].vy = 0.6f + ((rand() % 100) / 100.0f) * 1.0f;
                        sparks[i].max_life = 5 + rand() % 8;
                        sparks[i].life = sparks[i].max_life;
                        break;
                    }
                }
            }
        }
    }

    // Update active sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) {
            sparks[i].x += sparks[i].vx + (wind_bias * 0.4f);
            sparks[i].y -= sparks[i].vy;
            sparks[i].life--;
        }
    }
}

void render_scene(int fire_w, int fire_h) {
    int cy_front = fire_h - 4;
    int y_bottom = cy_front + 3;
    int y_top = cy_front - 3;

    for (int y = 0; y < fire_h; y++) {
        for (int x = 0; x < fire_w; x++) {
            int heat = fire_pixels[y][x];
            int px = x * 2;

            int log_shade = get_log_base_shade(x, y, fire_w, fire_h);

            // Check if position contains an active rising log pixel
            int log_pixel_heat = -1;
            if (log_shade >= 0) {
                for (int p = 0; p < MAX_LOG_PIXELS; p++) {
                    if (log_pixels[p].active && log_pixels[p].x == x && (int)log_pixels[p].y == y) {
                        float progress = (float)(y_bottom - y) / (float)(y_bottom - y_top);
                        if (progress < 0.0f) progress = 0.0f;
                        if (progress > 1.0f) progress = 1.0f;
                        // Color transition: Bright Yellow (heat ~30) -> Dark Orange (heat ~10)
                        log_pixel_heat = (int)(30.0f - progress * 20.0f);
                        break;
                    }
                }
            }

            // Render log pixel, log background, or flame
            if (log_pixel_heat >= 0) {
                attron(COLOR_PAIR(log_pixel_heat));
                mvaddstr(y, px, "██");
                attroff(COLOR_PAIR(log_pixel_heat));
            } else if (log_shade >= 0 && heat <= 5) {
                int jitter = (rand() % 3) - 1;
                int final_shade = log_shade + jitter + (heat > 0 ? 1 : 0);

                if (final_shade < 0) final_shade = 0;
                if (final_shade >= NUM_BROWN_SHADES) final_shade = NUM_BROWN_SHADES - 1;

                int pair = BROWN_PAIR_START + final_shade;
                attron(COLOR_PAIR(pair));
                mvaddstr(y, px, "██");
                attroff(COLOR_PAIR(pair));
            } else if (heat <= 0) {
                mvaddstr(y, px, "  ");
            } else {
                if (heat > 36) heat = 36;
                attron(COLOR_PAIR(heat));
                mvaddstr(y, px, "██");
                attroff(COLOR_PAIR(heat));
            }
        }
    }

    // Render spark overlay
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) {
            int sx = (int)sparks[i].x;
            int sy = (int)sparks[i].y;

            if (sx >= 0 && sx < fire_w && sy >= 0 && sy < fire_h) {
                int heat = 4 + (sparks[i].life * 12 / sparks[i].max_life);

                attron(COLOR_PAIR(heat));
                mvaddstr(sy, sx * 2, "██");
                attroff(COLOR_PAIR(heat));
            }
        }
    }
}

void *loop_one(void *arg) {
    srand(time(NULL));

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(45);

    if (!has_colors()) {
        endwin();
        printf("Your terminal does not support colors.\n");
        running = 0;
        return 0;
    }

    setup_native_fire_palette();

    int last_w = 0, last_h = 0;

    while (running) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        if (max_y > MAX_H) max_y = MAX_H;
        if (max_x > MAX_W * 2) max_x = MAX_W * 2;

        int fire_w = max_x / 2;
        int fire_h = max_y;

        if (fire_w != last_w || fire_h != last_h) {
            erase();
            last_w = fire_w;
            last_h = fire_h;
        }

        update_fire(fire_w, fire_h);
        render_scene(fire_w, fire_h);
        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            running = 0;
            break;
        }
    }

    endwin();
    return 0;
}

void *loop_two(void *arg) {
    int rc;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int val = SAMPLE_RATE;
    snd_pcm_uframes_t frames = BUFFER_SIZE;
    char buffer[BUFFER_SIZE * 2];
    short *samples = (short *)buffer;

    srand(time(NULL));

    const char *device = getenv("PCM_DEVICE");
    if (!device) {
        device = "pulse";
    }

    rc = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        device = "default";
        rc = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0);
        if (rc < 0) {
            fprintf(stderr, "unable to open pcm device: %s\n", snd_strerror(rc));
            running = 0;
            return 0;
        }
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, CHANNELS);
    snd_pcm_hw_params_set_rate_near(handle, params, &val, 0);
    snd_pcm_hw_params(handle, params);

    double flame_accumulator = 0.0;
    double last_swoosh = 0.0;
    double base_swoosh_alpha = 0.04;

    double pop_envelope = 0.0;
    double pop_decay = 0.9996;
    double pop_max_vol = 0.35;
    long pop_cooldown = 0;
    double pop_filter_alpha = 0.05;
    double pop_filter_out = 0.0;

    double ember_filter_out = 0.0;
    double ember_filter_alpha = 0.07;

    unsigned long sample_counter = 0;

    while (running) {
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sample_counter++;
            double raw_noise = ((double)rand() / RAND_MAX * 2.0 - 1.0);

            flame_accumulator = (flame_accumulator + (0.12 * raw_noise)) * 0.98;

            double osc1 = 0.4 * sin(sample_counter * 0.00004);
            double osc2 = 0.3 * sin(sample_counter * 0.00012);
            double osc3 = 0.2 * cos(sample_counter * 0.00045);

            double total_turbulence = osc1 + osc2 + osc3 + (0.1 * raw_noise);
            if (total_turbulence > 1.0) total_turbulence = 1.0;
            if (total_turbulence < -1.0) total_turbulence = -1.0;

            double dynamic_swoosh_alpha = base_swoosh_alpha + (total_turbulence * 0.015);
            if (dynamic_swoosh_alpha < 0.01) dynamic_swoosh_alpha = 0.01;

            last_swoosh = last_swoosh + dynamic_swoosh_alpha * (flame_accumulator - last_swoosh);

            double current_flame_vol = 0.07 + (total_turbulence * 0.02);
            double sample = last_swoosh * current_flame_vol;

            double ember_thermal_wave = 0.4 * sin(sample_counter * 0.00002) +
                                       0.3 * sin(sample_counter * 0.000007) +
                                       0.3 * ((double)rand() / RAND_MAX);

            double ember_pulse = 0.0;
            double current_ember_chance = 4.0 + (ember_thermal_wave * 12.0);
            if (current_ember_chance < 1.0) current_ember_chance = 1.0;

            if (rand() % 4000 < current_ember_chance) {
                double micro_intensity = 0.020 + ((double)rand() / RAND_MAX * 0.040);
                ember_pulse = raw_noise * micro_intensity;
            }

            double dynamic_ember_alpha = ember_filter_alpha + (raw_noise * 0.01);
            if (dynamic_ember_alpha < 0.04) dynamic_ember_alpha = 0.04;

            ember_filter_out = ember_filter_out + dynamic_ember_alpha * (ember_pulse - ember_filter_out);
            sample += ember_filter_out * 1.3;

            if (pop_cooldown > 0) pop_cooldown--;

            if (pop_cooldown == 0 && pop_envelope <= 0.001) {
                if (rand() % 900000 < 2) {
                    pop_envelope = 1.0;
                    pop_max_vol = 0.25 + ((double)rand() / RAND_MAX * 0.15);
                    pop_decay = 0.9994 + ((double)rand() / RAND_MAX * 0.0002);
                    pop_filter_alpha = 0.04 + ((double)rand() / RAND_MAX * 0.04);
                    pop_cooldown = SAMPLE_RATE * (12 + rand() % 18);
                }
            }

            double pop_signal = 0.0;
            if (pop_envelope > 0.001) {
                pop_signal = raw_noise * pop_envelope * pop_max_vol;
                pop_envelope *= pop_decay;
            }
            pop_filter_out = pop_filter_out + pop_filter_alpha * (pop_signal - pop_filter_out);
            sample += pop_filter_out;

            if (sample > 1.0) sample = 1.0;
            if (sample < -1.0) sample = -1.0;

            samples[i] = (short)(sample * 32767);
        }

        rc = snd_pcm_writei(handle, buffer, frames);
        if (rc < 0) {
            rc = snd_pcm_recover(handle, rc, 0);
            if (rc < 0) {
                fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(rc));
                break;
            }
        }
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    return 0;
}

int main() {
    pthread_t thread1, thread2;

    pthread_create(&thread1, NULL, loop_one, NULL);
    pthread_create(&thread2, NULL, loop_two, NULL);

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    return 0;
}
