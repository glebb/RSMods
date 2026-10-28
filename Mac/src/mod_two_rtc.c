// RSMods: no popup for two Real Tone cables (TwoRTCBypassMod.cpp, QualityOfLife::PatchTwoRTC). RSMods NOPs the
// check in front of the popup; here the check itself, 0x100315220 (a second guitar input is active; only the
// popup's function 0x1000e11a0 calls it), answers no.
#include <stdatomic.h>

#include "rsmods.h"

#define A_TOO_MANY_INPUTS 0x100315220ull

static atomic_int g_on;

static void tick(void) { atomic_store(&g_on, rsm_on("BypassTwoRTCMessageBox")); }

static void after_check(rc_ctx *c, void *user) {
  (void)user;
  if (atomic_load(&g_on)) c->r[0] = 0;
}

__attribute__((constructor)) static void init(void) {
  rsm_hook(A_TOO_MANY_INPUTS, NULL, after_check, "too_many_inputs");
}

const rsm_mod rsm_mod_two_rtc = {"two_rtc", tick, NULL, NULL, NULL};
