// RSMods: the song timer at the top right (ShowSongTimerMod.cpp, GameOverlay::DisplaySongTimer).
// ShowSongTimer = on enables it; with ShowSongTimerWhen = automatic it shows in every song, and the
// ShowSongTimerKey shows or hides it (the only way with ShowSongTimerWhen = manual).
#include <strings.h>

#include "rsmods.h"

static int g_show, g_in_song;

static void tick(void) {
  if (!rsm_on("ShowSongTimer")) return;
  int in_song = rsm_menu_in(RSM_SONG_MODES);
  if (in_song != g_in_song) {  // song enter / exit
    g_in_song = in_song;
    const char *when = rsm_get("Toggle Switches", "ShowSongTimerWhen");
    if (when && !strcasecmp(when, "automatic")) g_show = in_song;
  }
  if (!g_show || !in_song) return;
  double t = rsm_song_time();
  if (t <= 0) return;
  char txt[32];
  rsm_format_time(txt, sizeof txt, t);
  rsm_text(txt, 1.f - 1.f / 96, 1.f / 54, 0.028f, RSM_ALIGN_RIGHT, 0xffffffff);
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("ShowSongTimerKey") || !rsm_on("ShowSongTimer")) return;
  g_show = !g_show;
  rsm_log("song timer %s", g_show ? "shown" : "hidden");
}

const rsm_mod rsm_mod_song_timer = {"song_timer", tick, key, NULL, NULL};
