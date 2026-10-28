// RSMods: remove the lyrics (RemoveLyricsMod.cpp). Lyrics = on and RemoveLyricsWhen = manual (the key shows and
// hides them) or startup (always hidden). The game renders the lyrics in two render steps of its own, "Lyrics
// Render" (the text into a texture) and "Lyrics Apply" (onto the screen; set up by 0x10013a150, removed by
// 0x10013ca90): both are switched off. (The mesh LyricsRender is drawn directly, so culling it does nothing.)
#include <strings.h>

#include "rsmods.h"

static int g_hidden;

static void tick(void) {
  static const char *const NAMES[] = {"Lyrics Render", "Lyrics Apply", NULL};
  int on = 0;
  if (rsm_on("Lyrics")) {
    const char *when = rsm_get("Toggle Switches", "RemoveLyricsWhen");
    on = (when && !strcasecmp(when, "startup")) || g_hidden;
  } else {
    g_hidden = 0;
  }
  rsm_scene_hide("lyrics", NAMES, RSM_STEP_OFF, on);
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("RemoveLyricsKey") || !rsm_on("Lyrics")) return;
  const char *when = rsm_get("Toggle Switches", "RemoveLyricsWhen");
  if (when && strcasecmp(when, "manual")) return;
  g_hidden = !g_hidden;
  rsm_log("lyrics %s", g_hidden ? "hidden" : "shown");
}

const rsm_mod rsm_mod_lyrics = {"lyrics", tick, key, NULL, NULL};
