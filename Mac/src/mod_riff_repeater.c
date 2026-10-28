// RSMods: Riff Repeater speed above 100% (RiffRepeater.cpp, RiffRepeaterMod.cpp).
//
// The song speed is the Wwise RTPC "Time_Stretch" (10000 / percent: 100% = 100, 200% = 50). The game only
// goes down to it; above 100% needs the Time Stretch effect in slot 2 of the song's actor-mixer node, the
// first audio object of "Play_<SongKey>", as the game's own setter (0x1002dde00) does for slower speeds.
// The speed key raises the speed by RRSpeedInterval percent, Ctrl + the key lowers it. The speed shows at the
// top of the screen ("Song Speed: N%", D3DOverlay.cpp) and, after a change, in riff_repeater_speed.txt in the
// game folder (for stream overlays).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rsmods.h"

#define MIN_REAL_SPEED 6.25f
#define MAX_REAL_SPEED 400.f
#define AK_ID_DEFAULT_TIME_STRETCH 0xB3745FC2u
#define TIME_STRETCH_SLOT 2

#define MAX_CACHED 256
static struct {
  char key[128];
  uint32_t node;
} g_cache[MAX_CACHED];
static int g_ncache;

static uint32_t g_song_node;        // actor-mixer node of the current song (0: not found yet)
static int g_ready_to_log;          // look the node up once the song plays
static uint32_t g_stretch_node;     // node the effect is attached to
static int g_above100;              // the effect is attached
static float g_real_speed = 100.f;  // percent, refreshed a few times a second
static int g_save_speed;            // write riff_repeater_speed.txt

static float convert(float speed) { return 10000.f / speed; }

static float get_speed(int real) {
  float v = 0;
  ak_get_rtpc("Time_Stretch", RS_SONG_GAME_OBJECT, &v);
  if (v <= 0 || fabsf(v - 100.f) < 0.5f) v = 100.f;  // the game lands near, not on, 100
  return real ? convert(v) : v;
}

static void set_speed(float real) {
  if (real < MIN_REAL_SPEED) real = MIN_REAL_SPEED;
  if (real > MAX_REAL_SPEED) real = MAX_REAL_SPEED;
  float v = convert(real);
  ak_set_rtpc("Time_Stretch", v, RS_SONG_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  ak_set_rtpc("Time_Stretch", v, AK_INVALID_GAME_OBJECT, 0, AK_CURVE_LINEAR);
}

static void enable_stretch(void) {
  if (g_above100 && g_stretch_node == g_song_node) return;
  ak_set_actor_mixer_effect(g_song_node, TIME_STRETCH_SLOT, AK_ID_DEFAULT_TIME_STRETCH);
  g_stretch_node = g_song_node;
  g_above100 = 1;
}

static void remove_stretch(void) {
  if (!g_above100) return;
  ak_set_actor_mixer_effect(g_stretch_node, TIME_STRETCH_SLOT, 0);
  g_above100 = 0;
}

// The effect only in the chain while the speed is not 100% (its phase vocoder colours the sound even at 100%).
static void sync_stretch(void) {
  if (get_speed(0) != 100.f)
    enable_stretch();
  else
    remove_stretch();
}

static void log_song_node(const char *key) {
  if (!*key) return;
  char ev[160];
  snprintf(ev, sizeof ev, "Play_%s", key);
  uint32_t node = ak_query_first_object(ev);
  if (!node) return;
  g_song_node = node;
  g_ready_to_log = 0;
  if (g_ncache < MAX_CACHED) {
    snprintf(g_cache[g_ncache].key, sizeof g_cache[0].key, "%s", key);
    g_cache[g_ncache++].node = node;
  }
  rsm_log("riff repeater: %s -> node 0x%x", ev, node);
}

static void song_changed(const char *key) {
  for (int i = 0; i < g_ncache; i++)
    if (!strcmp(g_cache[i].key, key)) {
      g_song_node = g_cache[i].node;
      g_ready_to_log = 0;
      return;
    }
  g_song_node = 0;
  g_ready_to_log = 1;
}

static void show_speed(void) {
  int on = rsm_on("RRSpeedAboveOneHundred") && g_song_node &&
           (rsm_menu_in(RSM_FAST_RR_MODES) || rsm_menu_in(RSM_SCORE_SCREENS));
  if (!on && !g_above100) return;
  char t[48];
  snprintf(t, sizeof t, "Song Speed: %d%%", (int)roundf(g_real_speed));
  rsm_text(t, 0.5f, 20.f / 1080, 0.028f, RSM_ALIGN_CENTER, 0xffffffff);
}

static void save_speed(void) {
  if (!g_save_speed || !rsm_menu_in(RSM_FAST_RR_MODES)) return;
  g_save_speed = 0;
  FILE *f = fopen("riff_repeater_speed.txt", "w");
  if (!f) return;
  fprintf(f, "%d\n", (int)g_real_speed);
  fclose(f);
}

static void tick(void) {
  static unsigned frame;
  show_speed();
  if (++frame % 8) return;  // the rest a few times a second
  if (g_song_node) g_real_speed = get_speed(1);
  save_speed();
  if (rsm_menu_in(RSM_SONG_MODES)) {
    if (g_ready_to_log) log_song_node(rsm_song_key());
    if (rsm_on("RRSpeedAboveOneHundred") && g_song_node) sync_stretch();
  } else if (g_above100 && !rsm_menu_in(RSM_SCORE_SCREENS)) {
    // leaving the song for anything but the score screens: back to normal
    remove_stretch();
    set_speed(100.f);
    rsm_log("riff repeater: speed reset to 100%%");
  }
}

static void key(const rsm_key_event *e) {
  if (!e->down || e->keycode != rsm_key("RRSpeedKey")) return;
  if (!rsm_on("RRSpeedAboveOneHundred") || !rsm_menu_in(RSM_FAST_RR_MODES) || !g_song_node) return;
  float step = (float)rsm_long("RRSpeedInterval", 2);
  float speed = get_speed(1) + (e->ctrl ? -step : step);
  set_speed(speed);
  sync_stretch();
  g_real_speed = get_speed(1);
  g_save_speed = 1;
  rsm_log("riff repeater: song speed %.2f%%", g_real_speed);
}

const rsm_mod rsm_mod_riff_repeater = {"riff_repeater", tick, key, song_changed, NULL};
