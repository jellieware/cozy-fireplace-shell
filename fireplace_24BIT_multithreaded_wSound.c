#include <alsa/asoundlib.h>
#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 48
#define HEIGHT 28

// This array maps to 256-color palette (flame colors: black -> red -> orange ->
// yellow -> white)
int fire_palette[] = {16,  52,  88,  124, 160, 196, 202, 208, 214,
                      220, 226, 227, 228, 229, 230, 231, 255, 254,
                      253, 252, 251, 250, 249, 248

};
// 1. Array bounds set exactly to 27 steps
#define PALETTE_SIZE 36

// 2. Struct using short values for ncurses mapping
typedef struct {
  short r; // Red (0 - 1000)
  short g; // Green (0 - 1000)
  short b; // Blue (0 - 1000)
} RGBColor;

// 3. REVERSED 27-Shade Loop: Black -> Deep Red -> Orange -> Brilliant
// White-Gold
RGBColor fire_palette_24bit[PALETTE_SIZE] = {
    // --- START: Pure Black to Faint Embers (Shades 0 - 6) ---
    {0, 0, 0},   // 0: Pure Black / Extinction
    {10, 0, 0},  // 1: Absolute final ember hue
    {30, 0, 0},  // 2: Near darkness
    {60, 0, 0},  // 3: Smoldering trace
    {100, 0, 0}, // 4: Dim glow
    {160, 0, 0}, // 5: Very dark red ember
    {230, 0, 0}, // 6: Faint charcoal red

    // --- ACCELERATION: Deep Reds to Saturated Red (Shades 7 - 13) ---
    {300, 0, 0},  // 7: Dark burgundy
    {380, 0, 0},  // 8: Deep crimson
    {460, 0, 0},  // 9: Dark blood red
    {550, 0, 0},  // 10: Medium red
    {650, 0, 0},  // 11: Intense pure red
    {750, 20, 0}, // 12: Green begins to spark
    {850, 90, 0}, // 13: Saturated red-orange

    // --- TRANSITION: Dense, Rich Oranges (Shades 14 - 20) ---
    {950, 180, 0},  // 14: Dark reddish-orange
    {1000, 250, 0}, // 15: Deep safety orange
    {1000, 320, 0}, // 16: Burnt orange
    {1000, 390, 0}, // 17: Medium dark orange
    {1000, 460, 0}, // 18: Pure vibrant orange
    {1000, 540, 0}, // 19: Vivid orange-yellow
    {1000, 620, 0}, // 20: Deep amber

    // --- PEAK: Deep Yellow up to White-Hot Gold Core (Shades 21 - 26) ---
    {1000, 700, 0},   // 21: Light amber
    {1000, 780, 0},   // 22: Deep yellow
    {1000, 840, 40},  // 23: Warm yellow-gold
    {1000, 900, 150}, // 24: Golden yellow
    {1000, 950, 400}, // 25: Highly saturated intense gold
    {1000, 1000, 700} // 26: White-hot bright gold core
};

// #define PALETTE_SIZE 24
#define PCM_DEVICE "default"
#define SAMPLE_RATE 44100
#define CHANNELS 1
#define BUFFER_SIZE 4096
// #define PALETTE_SIZE 24

void *loop_one(void *arg) {

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
  timeout(50); // Frame delay in ms

  // Initialize Color Pairs
  // We pair the fire color with itself (foreground/background) to make a solid
  // block
  for (int i = 0; i < PALETTE_SIZE; i++) {
    // init_pair(i + 1, fire_palette[i], fire_palette[i]);
    init_color(16 + i, fire_palette_24bit[i].r, fire_palette_24bit[i].g,
               fire_palette_24bit[i].b);
    init_pair(i + 1, 16 + i, 16 + i);
  }

  while (1) {
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
          getmaxyx(stdscr, max_y,
                   max_x); // Get screen dimensions (LINES and COLS)
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

// Function for the second loop (Thread 2)
void *loop_two(void *arg) {

  int rc;
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *params;
  unsigned int val = SAMPLE_RATE;
  snd_pcm_uframes_t frames = BUFFER_SIZE;
  char buffer[BUFFER_SIZE * 2];
  short *samples = (short *)buffer;

  srand(time(NULL));

  rc = snd_pcm_open(&handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    fprintf(stderr, "unable to open pcm device: %s\n", snd_strerror(rc));
    exit(1);
  }

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(handle, params);
  snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
  snd_pcm_hw_params_set_channels(handle, params, CHANNELS);
  snd_pcm_hw_params_set_rate_near(handle, params, &val, 0);
  snd_pcm_hw_params(handle, params);

  // Flame Accumulator to strip noise hiss (Brownian movement emulation)
  double flame_accumulator = 0.0;
  double last_swoosh = 0.0;
  double base_swoosh_alpha = 0.04;

  // Sparse Pop execution states (Rare, heavy)
  double pop_envelope = 0.0;
  double pop_decay = 0.9996;
  double pop_max_vol = 0.35;
  long pop_cooldown = 0;
  double pop_filter_alpha = 0.05;
  double pop_filter_out = 0.0;

  // Organic Ember Wave variables
  double ember_filter_out = 0.0;
  double ember_filter_alpha = 0.07;

  // Global clock tracking for overlapping waves
  unsigned long sample_counter = 0;

  while (1) {

    for (int i = 0; i < BUFFER_SIZE; i++) {
      sample_counter++;
      double raw_noise = ((double)rand() / RAND_MAX * 2.0 - 1.0);

      // 1. Emulate Brown/Red Noise for the flame to drop harsh high-end hiss
      flame_accumulator = (flame_accumulator + (0.12 * raw_noise)) * 0.98;

      // 2. Overlapping Turbulence Grid
      double osc1 = 0.4 * sin(sample_counter * 0.00004);
      double osc2 = 0.3 * sin(sample_counter * 0.00012);
      double osc3 = 0.2 * cos(sample_counter * 0.00045);

      double total_turbulence = osc1 + osc2 + osc3 + (0.1 * raw_noise);
      if (total_turbulence > 1.0)
        total_turbulence = 1.0;
      if (total_turbulence < -1.0)
        total_turbulence = -1.0;

      // 3. Modulate Low-Pass using the dense turbulence index
      double dynamic_swoosh_alpha =
          base_swoosh_alpha + (total_turbulence * 0.015);
      if (dynamic_swoosh_alpha < 0.01)
        dynamic_swoosh_alpha = 0.01;

      last_swoosh = last_swoosh +
                    dynamic_swoosh_alpha * (flame_accumulator - last_swoosh);

      // Tweak: Lowered the base volume and heavily compressed the turbulence
      // swing range. Baseline dropped to 0.07 (down from 0.13), and variance
      // window dropped to 0.02 (down from 0.05).
      double current_flame_vol = 0.07 + (total_turbulence * 0.02);
      double sample = last_swoosh * current_flame_vol;

      // 4. Thermal Oscillator for Ember Sparkles
      double ember_thermal_wave = 0.4 * sin(sample_counter * 0.00002) +
                                  0.3 * sin(sample_counter * 0.000007) +
                                  0.3 * ((double)rand() / RAND_MAX);

      // 5. Dynamic Organic Ember Engine
      double ember_pulse = 0.0;
      double current_ember_chance = 4.0 + (ember_thermal_wave * 12.0);
      if (current_ember_chance < 1.0)
        current_ember_chance = 1.0;

      if (rand() % 4000 < current_ember_chance) {
        double micro_intensity = 0.020 + ((double)rand() / RAND_MAX * 0.040);
        ember_pulse = raw_noise * micro_intensity;
      }

      double dynamic_ember_alpha = ember_filter_alpha + (raw_noise * 0.01);
      if (dynamic_ember_alpha < 0.04)
        dynamic_ember_alpha = 0.04;

      ember_filter_out = ember_filter_out +
                         dynamic_ember_alpha * (ember_pulse - ember_filter_out);
      sample += ember_filter_out * 1.3;

      // 6. Less Frequent, Deep Wood Pops
      if (pop_cooldown > 0)
        pop_cooldown--;

      if (pop_cooldown == 0 && pop_envelope <= 0.001) {
        if (rand() % 900000 < 2) {
          pop_envelope = 1.0;
          pop_max_vol = 0.25 + ((double)rand() / RAND_MAX * 0.15);
          pop_decay = 0.9994 + ((double)rand() / RAND_MAX * 0.0002);
          pop_filter_alpha = 0.04 + ((double)rand() / RAND_MAX * 0.04);
          pop_cooldown = SAMPLE_RATE * (12 + rand() % 18);
        }
      }

      // Process active rare pop envelope
      double pop_signal = 0.0;
      if (pop_envelope > 0.001) {
        pop_signal = raw_noise * pop_envelope * pop_max_vol;
        pop_envelope *= pop_decay;
      }
      pop_filter_out =
          pop_filter_out + pop_filter_alpha * (pop_signal - pop_filter_out);
      sample += pop_filter_out;

      // Safe clipping protection
      if (sample > 1.0)
        sample = 1.0;
      if (sample < -1.0)
        sample = -1.0;

      samples[i] = (short)(sample * 32767);
    }

    rc = snd_pcm_writei(handle, buffer, frames);
    if (rc == -EPIPE) {
      snd_pcm_prepare(handle);
    } else if (rc < 0) {
      fprintf(stderr, "error writing to pcm: %s\n", snd_strerror(rc));
    }
  }
  snd_pcm_drain(handle);
  snd_pcm_close(handle);
  return 0;
}

int main() {

  pthread_t thread1, thread2;

  // 1. Start the first thread to run loop_one
  pthread_create(&thread1, NULL, loop_one, NULL);

  // 2. Start the second thread to run loop_two
  pthread_create(&thread2, NULL, loop_two, NULL);

  // 3. Wait for both threads to finish (they run forever in this example)
  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);

  return 0;
}
