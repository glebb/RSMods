// RSMods: no song previews (SongPreviewsMod.cpp, VolumeControl::DisableSongPreviewAudio). RSMods renames
// the game's "Play_%s_Preview" event to "Play_%s_Invalid", which Wwise does not have; here the preview
// events are dropped before Wwise sees them, with the same result. The song key is still taken from them.
#include <stdatomic.h>
#include <string.h>

#include "rsmods.h"

static atomic_int g_off;  // previews off (the setting, cached for post_event)

static void tick(void) {
  int off = rsm_on("SongPreviews");
  if (off != atomic_load(&g_off)) rsm_log("song previews %s", off ? "off" : "on");
  atomic_store(&g_off, off);
}

static int post_event(const char *name) {
  if (!atomic_load(&g_off) || strncmp(name, "Play_", 5)) return 0;
  size_t n = strnlen(name, 256);
  return n > 13 && !strcmp(name + n - 8, "_Preview");
}

const rsm_mod rsm_mod_song_previews = {"song_previews", tick, NULL, NULL, post_event};
