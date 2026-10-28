// RSMods: remove the headstock (RemoveHeadstockMod.cpp). Headstock = on and RemoveHeadstockWhen = song (in songs)
// or startup (always). RSMods skips its Direct3D draws; here its scene objects are hidden by name: the frame and
// the tuning pegs (MeshGuitarFrame<type>, MeshGuitarPeg<type>, e.g. Standard3x3).
#include <strings.h>

#include "rsmods.h"

static void tick(void) {
  static const char *const NAMES[] = {"MeshGuitarFrame*", "MeshGuitarPeg*", NULL};
  int on = 0;
  if (rsm_on("Headstock")) {
    const char *when = rsm_get("Toggle Switches", "RemoveHeadstockWhen");
    on = (when && !strcasecmp(when, "startup")) || rsm_menu_in(RSM_SONG_MODES);
  }
  rsm_scene_hide("headstock", NAMES, RSM_CULL, on);
}

const rsm_mod rsm_mod_headstock = {"headstock", tick, NULL, NULL, NULL};
