// RSMods: remove the skyline, the phrase bars at the top of the screen (RemoveSkylineMod.cpp). Skyline = on and
// ToggleSkylineWhen = song (in songs) or startup (always). The game draws the bar strip itself (textures
// assets/ui/phrasebanner/phrasebar_*) in a render step of its own, BannerRenderStep<player>, which is switched off.
#include <strings.h>

#include "rsmods.h"

static void tick(void) {
  static const char *const NAMES[] = {"BannerRenderStep*", NULL};  // one per player
  int on = 0;
  if (rsm_on("Skyline")) {
    const char *when = rsm_get("Toggle Switches", "ToggleSkylineWhen");
    on = (when && !strcasecmp(when, "startup")) || rsm_menu_in(RSM_SONG_MODES);
  }
  rsm_scene_hide("skyline", NAMES, RSM_STEP_OFF, on);
}

const rsm_mod rsm_mod_skyline = {"skyline", tick, NULL, NULL, NULL};
