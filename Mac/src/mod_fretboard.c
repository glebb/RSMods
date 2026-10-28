// RSMods: fretless, no inlays, no lane markers (D3DHooks.cpp: their draws are skipped while the settings are on).
// Here their scene objects are culled: the fret wires (MeshGuitarFretBarNormal), the inlays (GRGuitarInlay,
// inlay_*), the lane markers on the highway (number_lane, number_lane_glow).
#include "rsmods.h"

static void tick(void) {
  static const char *const FRETS[] = {"MeshGuitarFretBarNormal", NULL};
  static const char *const INLAYS[] = {"GRGuitarInlay", "inlay_*", NULL};
  static const char *const LANES[] = {"number_lane", "number_lane_glow", NULL};
  rsm_scene_hide("fretless", FRETS, RSM_CULL, rsm_on("Fretless"));
  rsm_scene_hide("inlays", INLAYS, RSM_CULL, rsm_on("Inlays"));
  rsm_scene_hide("lane_markers", LANES, RSM_CULL, rsm_on("LaneMarkers"));
}

const rsm_mod rsm_mod_fretboard = {"fretboard", tick, NULL, NULL, NULL};
