// Experiment knobs, for finding what a visual mod should hide (see scene-dump.txt from SceneDumpKey), toggled
// together with [Keybinds] HideNodesKey:
//   [Toggle Switches] HideNodes = name, name*, ...   scene objects to cull
//   [Toggle Switches] HideSteps = name, ...          render steps to switch off
#include <stdio.h>
#include <string.h>

#include "rsmods.h"

static int g_on = 1;

// Splits a comma-separated setting into names (pointers into buf); returns the count
static int split(const char *key, char *buf, size_t n, const char **names, int max) {
  const char *v = rsm_get("Toggle Switches", key);
  snprintf(buf, n, "%s", v ? v : "");
  int k = 0;
  for (char *save = NULL, *t = strtok_r(buf, ",", &save); t && k < max; t = strtok_r(NULL, ",", &save)) {
    t = rsm_trim(t);
    if (*t) names[k++] = t;
  }
  names[k] = NULL;
  return k;
}

static void tick(void) {
  static char nodes_buf[1024], steps_buf[1024];
  static const char *nodes[33], *steps[33];
  int nn = split("HideNodes", nodes_buf, sizeof nodes_buf, nodes, 32);
  int ns = split("HideSteps", steps_buf, sizeof steps_buf, steps, 32);
  rsm_scene_hide("HideNodes", nodes, RSM_CULL, g_on && nn > 0);
  rsm_scene_hide("HideSteps", steps, RSM_STEP_OFF, g_on && ns > 0);
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("HideNodesKey")) return;
  g_on = !g_on;
  rsm_log("HideNodes/HideSteps %s", g_on ? "on" : "off");
}

const rsm_mod rsm_mod_hide_nodes = {"hide_nodes", tick, key, NULL, NULL};
