// RSMods: screenshot the score screens (ScreenShotScoresMod.cpp). 8 s after a score screen opens, once per
// screen. RSMods presses F12 for a Steam screenshot; RS Re-ARMed runs without the Steam client, so the frame is
// saved here, as a PNG in ~/Pictures/RS Re-ARMed/ (<song key> <date> <time>.png).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "rsmods.h"

static char g_menu[64];
static double g_at;
static int g_taken;

static void tick(void) {
  if (!rsm_on("ScreenShotScores")) return;
  if (!rsm_menu_in(RSM_SCORE_SCREENS)) {
    *g_menu = 0;
    return;
  }
  const char *m = rsm_menu();
  if (strcmp(m, g_menu)) {
    snprintf(g_menu, sizeof g_menu, "%s", m);
    g_at = rsm_now() + 8;
    g_taken = 0;
    return;
  }
  if (g_taken || rsm_now() < g_at) return;
  g_taken = 1;
  const char *home = getenv("HOME");
  if (!home) return;
  char dir[900], path[1024], stamp[32];
  snprintf(dir, sizeof dir, "%s/Pictures/RS Re-ARMed", home);
  mkdir(dir, 0755);
  time_t t = time(NULL);
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", localtime(&t));
  snprintf(path, sizeof path, "%s/%s %s.png", dir, *rsm_song_key() ? rsm_song_key() : "score", stamp);
  rsm_screenshot(path);
}

const rsm_mod rsm_mod_screenshot_scores = {"screenshot_scores", tick, NULL, NULL, NULL};
