// RSMods: keep playing, with sound, when the game is not the active app (AllowAudioInBackgroundMod.cpp,
// VolumeControl::AllowAltTabbingWithAudio). RSMods patches three places; the Mac build's equivalents:
//   - SetAppFocus(bool) 0x1000e1e70 (pauses the audio): always told "focused";
//   - the pause the shell sends on focus loss, UI event 0x98 through 0x100314f70, right after SetAppFocus
//     (from applicationWillResignActive, while the app still counts as active): the next one after a focus
//     loss is dropped;
//   - the focus byte 0x10194b391 the shell clears just before SetAppFocus: set again there (and every frame).
#include <stdatomic.h>

#include "rsmods.h"

#define A_SET_APP_FOCUS 0x1000e1e70ull
#define A_UI_EVENT 0x100314f70ull
#define D_FOCUS_BYTE 0x10194b391ull
#define UI_EVENT_PAUSE 0x98

static atomic_int g_on;
static int g_lost;  // a focus loss was ignored: drop the shell's pause that follows it (same thread, same call)

static void tick(void) {
  int on = rsm_on("AllowAudioInBackground");
  atomic_store(&g_on, on);
  if (on) *(volatile uint8_t *)(uintptr_t)RC_A(D_FOCUS_BYTE) = 1;
  g_lost = 0;  // the pause comes in the same call as the focus loss, never a frame later
}

static int before_set_focus(rc_ctx *c, void *user) {
  (void)user;
  if (atomic_load(&g_on) && !(c->r[7] & 0xff)) {
    c->r[7] = 1;  // rdi: focused
    // the shell has just cleared the focus byte; the game reads it in the next frame, before our tick
    *(volatile uint8_t *)(uintptr_t)RC_A(D_FOCUS_BYTE) = 1;
    g_lost = 1;
    rsm_log("background audio: focus loss ignored");
  }
  return 0;
}

static int before_ui_event(rc_ctx *c, void *user) {
  (void)user;
  if (!g_lost || (int32_t)c->r[6] != UI_EVENT_PAUSE) return 0;  // rsi: event
  g_lost = 0;
  rsm_log("background audio: pause on focus loss dropped");
  ov_ret_void(c);
  return 1;
}

__attribute__((constructor)) static void init(void) {
  rsm_hook(A_SET_APP_FOCUS, before_set_focus, NULL, "set_app_focus");
  rsm_hook(A_UI_EVENT, before_ui_event, NULL, "ui_event");
}

const rsm_mod rsm_mod_background_audio = {"background_audio", tick, NULL, NULL, NULL};
