// RSMods: auto enter the last used profile (AutoLoadProfileMod.cpp). Until the main menu shows, presses
// Enter (and Escape on dialogs) twice a second, as RSMods does, which goes from the title screen into the top
// profile of the list: the last used one. The first-time setup screens are left alone.
//
// Not ported yet: ProfileToLoad (a named profile); it needs the highlighted profile's name, not found in the
// Mac build yet. With it set, the top profile is still the one entered.
#include <string.h>

#include "rsmods.h"

#define KVK_RETURN 0x24
#define KVK_ESCAPE 0x35

static const char *const DONT_AUTO_ENTER[] = {
    "TextEntryDialog", "PlayedRS1Select", "ExperienceSelect", "PathSelect", "HandSelect", "HeadstockSelect",
    "FE_InputSelect", "FECalibrationMeter", "VideoPlayer", "FETuner", "FirstEncounter_Game", "RefresherSelect",
    NULL};

static int g_done;  // the main menu was reached: only at startup
static double g_next;

static void tick(void) {
  if (g_done) return;
  const char *m = rsm_menu();
  if (!strcmp(m, "MainMenu")) {
    g_done = 1;
    return;
  }
  if (!rsm_on("ForceProfileLoad") || !*m || rsm_menu_in(DONT_AUTO_ENTER)) return;
  double t = rsm_now();
  if (t < g_next) return;
  g_next = t + 0.5;
  if (!strcmp(m, "SelectionListDialog") || !strcmp(m, "UplayLoginDialog")) rsm_send_key(KVK_ESCAPE, 0x1b);
  rsm_send_key(KVK_RETURN, 0x0d);
  rsm_log("auto load profile: Enter (%s)", m);
}

const rsm_mod rsm_mod_autoload = {"autoload", tick, NULL, NULL, NULL};
