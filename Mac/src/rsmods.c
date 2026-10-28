// RSMods core: the hooks, game state, calls into translated code, and the dispatch to the mods. See rsmods.h.
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rsmods.h"

// The version, findable in the file without loading it (the settings app compares installed and bundled)
__attribute__((used)) const char rsm_version_tag[] = "rearmed-mods-version:" REARMED_MODS_VERSION;

static const rsm_mod *const MODS[] = {
    &rsm_mod_riff_repeater,
    &rsm_mod_looping,
    &rsm_mod_song_previews,
    &rsm_mod_song_timer,
    &rsm_mod_volume,
    &rsm_mod_autoload,
    &rsm_mod_enumeration,
    &rsm_mod_input_volume,
    &rsm_mod_screenshot_scores,
    &rsm_mod_two_rtc,
    &rsm_mod_background_audio,
    &rsm_mod_linear_rr,
    &rsm_mod_loft,
    &rsm_mod_scene_dump,
    &rsm_mod_hide_nodes,
    &rsm_mod_headstock,
    &rsm_mod_lyrics,
    &rsm_mod_skyline,
    &rsm_mod_fretboard,
};
#define NMODS (sizeof MODS / sizeof MODS[0])

// ------------------------------------------------------------------------------------------------
// Log (RS_MODS_LOG=1, like the mods module: stderr, which the launcher writes to logs/last-run.log)
// ------------------------------------------------------------------------------------------------
static int g_log = -1;

void rsm_log(const char *fmt, ...) {
  if (g_log < 0) {
    const char *v = getenv("RS_MODS_LOG");
    g_log = v && *v && *v != '0';
  }
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[rsmods] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// Calls into translated code
// ------------------------------------------------------------------------------------------------
uint64_t rsm_guest(uint64_t vmaddr, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl) {
  return rc_call_guest(RC_A(vmaddr), ints, nint, dbls, ndbl, NULL);
}

// SetRTPCValue(char const*, float, unsigned long long, int, AkCurveInterpolation)
#define A_AK_SET_RTPC_BYNAME 0x100ab1b20
// Query::GetRTPCValue(char const*, unsigned long long, float&, RTPCValue_type&)
#define A_AK_GET_RTPC_BYNAME 0x100af0820
// SetActorMixerEffect(unsigned int, unsigned int, unsigned int)
#define A_AK_SET_AM_EFFECT 0x100ab5ac0
// SeekOnEvent(char const*, unsigned long long, int, bool)
#define A_AK_SEEK_ON_EVENT_BYNAME 0x100ab3160
// Query::QueryAudioObjectIDs(char const*, unsigned int&, AkObjectInfo*)
#define A_AK_QUERY_OBJECTS_BYNAME 0x100af0960
// GetSourcePlayPosition(unsigned int, int*, bool)
#define A_AK_GET_SOURCE_PLAY_POS 0x100ab33a0

int ak_set_rtpc(const char *name, float value, uint64_t obj, int ms, int curve) {
  const uint64_t i[] = {(uint64_t)(uintptr_t)name, obj, (uint32_t)ms, (uint32_t)curve};
  const uint64_t d[] = {rsm_f32bits(value)};
  return (int)rsm_guest(A_AK_SET_RTPC_BYNAME, i, 4, d, 1);
}

int ak_get_rtpc(const char *name, uint64_t obj, float *out) {
  int32_t type = 1;  // RTPCValue_GameObject
  *out = 0;
  const uint64_t i[] = {(uint64_t)(uintptr_t)name, obj, (uint64_t)(uintptr_t)out, (uint64_t)(uintptr_t)&type};
  return (int)rsm_guest(A_AK_GET_RTPC_BYNAME, i, 4, NULL, 0);
}

int ak_set_actor_mixer_effect(uint32_t node, uint32_t slot, uint32_t fx) {
  const uint64_t i[] = {node, slot, fx};
  return (int)rsm_guest(A_AK_SET_AM_EFFECT, i, 3, NULL, 0);
}

int ak_seek_on_event(const char *event, uint64_t obj, int32_t ms, int nearest_marker) {
  const uint64_t i[] = {(uint64_t)(uintptr_t)event, obj, (uint32_t)ms, nearest_marker ? 1 : 0};
  return (int)rsm_guest(A_AK_SEEK_ON_EVENT_BYNAME, i, 4, NULL, 0);
}

int ak_get_source_play_position(uint32_t playing_id, int32_t *ms, int extrapolate) {
  const uint64_t i[] = {playing_id, (uint64_t)(uintptr_t)ms, extrapolate ? 1 : 0};
  return (int)rsm_guest(A_AK_GET_SOURCE_PLAY_POS, i, 3, NULL, 0);
}

// The first audio object of an event: the song's actor-mixer node for "Play_<SongKey>" (as RSMods).
uint32_t ak_query_first_object(const char *event) {
  typedef struct {
    uint32_t obj, parent;
    int32_t depth;
  } ak_object_info;
  uint32_t n = 0;
  uint64_t i[] = {(uint64_t)(uintptr_t)event, (uint64_t)(uintptr_t)&n, 0};
  int r = (int)rsm_guest(A_AK_QUERY_OBJECTS_BYNAME, i, 3, NULL, 0);
  if ((r != AK_SUCCESS && r != AK_PARTIAL_SUCCESS) || n == 0 || n > 4096) return 0;
  ak_object_info *objs = calloc(n, sizeof *objs);
  if (!objs) return 0;
  i[2] = (uint64_t)(uintptr_t)objs;
  r = (int)rsm_guest(A_AK_QUERY_OBJECTS_BYNAME, i, 3, NULL, 0);
  uint32_t first = (r == AK_SUCCESS || r == AK_PARTIAL_SUCCESS) && n ? objs[0].obj : 0;
  free(objs);
  return first;
}

// ------------------------------------------------------------------------------------------------
// Game state
// ------------------------------------------------------------------------------------------------
// The UI manager (0x100365e30 returns this global) has the current menu at +0x48 (0x100365eb0); the
// menu's name is an efd string at +0xe0 (inline unless its +0x18 points elsewhere than its +0x10), as
// 0x1000e0330 reads it.
#define D_UI_MANAGER 0x1018ef7c8ull
#define UI_CURRENT_MENU 0x48
#define MENU_NAME 0xe0

const char *rsm_menu(void) {
  uint64_t mgr = RC_LD(uint64_t, RC_A(D_UI_MANAGER));
  if (!mgr) return "";
  uint64_t m = RC_LD(uint64_t, mgr + UI_CURRENT_MENU);
  if (!m) return "";
  uint64_t s = m + MENU_NAME;
  const char *p = RC_LD(uint64_t, s + 0x18) == s + 0x10 ? (const char *)(uintptr_t)s
                                                        : (const char *)(uintptr_t)RC_LD(uint64_t, s);
  return p ? p : "";
}

int rsm_menu_in(const char *const *list) {
  const char *m = rsm_menu();
  for (; *list; list++)
    if (!strcmp(m, *list)) return 1;
  return 0;
}

// GameState.hpp of RSMods
const char *const RSM_SONG_MODES[] = {
    "LearnASong_Game", "NonStopPlay_Game", "ScoreAttack_Game", "LearnASong_Pause", "NonStopPlay_Pause",
    "ScoreAttack_Pause", "RiffRepeater", "LearnASong_RiffRepeater", "RiffRepeater_AdvancedSettings",
    "RiffRepeater_Pause", "Tuner", "MixerMenu", "HelpList", "SideList", "CalibrationMeter", NULL};
const char *const RSM_FAST_RR_MODES[] = {
    "LearnASong_Game", "NonStopPlay_Game", "LearnASong_Pause", "NonStopPlay_Pause", "LearnASong_RiffRepeater",
    "RiffRepeater", "RiffRepeater_AdvancedSettings", "RiffRepeater_Pause", NULL};
const char *const RSM_SCORE_SCREENS[] = {"LearnASong_SongReview", "ScoreAttack_SongReview", "Duet_SongReview",
                                         "H2H_SongReview", NULL};
const char *const RSM_LAS_PLAYING[] = {"LearnASong_Game", "NonStopPlay_Game", NULL};
const char *const RSM_LAS_MODES[] = {"LearnASong_Game", "NonStopPlay_Game", "LearnASong_Pause", "NonStopPlay_Pause",
                                     NULL};
const char *const RSM_LAS_PAUSE[] = {"LearnASong_Pause", "NonStopPlay_Pause", NULL};

// Song key: the game previews a song with the event "Play_<SongKey>_Preview" ("_Invalid" with previews off).
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_key_posted[128], g_key[128];

const char *rsm_song_key(void) { return g_key; }

// The song itself plays as "Play_<SongKey>": its playing ID (PostEvent's result) gives the position.
static char g_play_event[160];
static uint32_t g_play_id;
static __thread int t_song_event;  // the PostEvent running on this thread is a song's (pre -> post hook)

static void note_event(const char *name) {
  t_song_event = 0;
  if (!name || strncmp(name, "Play_", 5)) return;
  size_t n = strnlen(name, 256);
  if (n <= 5 || n >= 256) return;
  const char *tail = n > 13 ? name + n - 8 : "";
  if (strcmp(tail, "_Preview") && strcmp(tail, "_Invalid")) {
    // only the song itself ("Play_<the previewed song's key>"), not the game's other sounds (Play_Crowd, ...)
    pthread_mutex_lock(&g_key_lock);
    if (*g_key_posted && !strcmp(name + 5, g_key_posted) && n < sizeof g_play_event) {
      memcpy(g_play_event, name, n + 1);
      g_play_id = 0;
      t_song_event = 1;
    }
    pthread_mutex_unlock(&g_key_lock);
    return;
  }
  size_t k = n - 13;
  if (k >= sizeof g_key_posted) return;
  pthread_mutex_lock(&g_key_lock);
  memcpy(g_key_posted, name + 5, k);
  g_key_posted[k] = 0;
  pthread_mutex_unlock(&g_key_lock);
}

static void note_playing_id(uint32_t id) {
  if (!t_song_event) return;
  t_song_event = 0;
  pthread_mutex_lock(&g_key_lock);
  g_play_id = id;
  pthread_mutex_unlock(&g_key_lock);
  rsm_log("%s: playing id %u", g_play_event, id);
}

// The playing ID of "Play_<the current song key>", or 0
static uint32_t song_playing_id(void) {
  char want[160];
  snprintf(want, sizeof want, "Play_%s", g_key);
  pthread_mutex_lock(&g_key_lock);
  uint32_t id = !strcmp(g_play_event, want) ? g_play_id : 0;
  pthread_mutex_unlock(&g_key_lock);
  return id;
}

double rsm_song_time(void) {
  uint32_t id = song_playing_id();
  if (!id) return -1;
  int32_t ms = 0;
  if (ak_get_source_play_position(id, &ms, 1) != AK_SUCCESS) return -1;
  return ms / 1000.0;
}

void rsm_song_seek_ms(int32_t ms) {
  if (!*g_key) return;
  char ev[160];
  snprintf(ev, sizeof ev, "Play_%s", g_key);
  ak_seek_on_event(ev, RS_SONG_GAME_OBJECT, ms < 0 ? 0 : ms, 0);
}

void rsm_format_time(char *out, size_t n, double seconds) {
  int s = seconds > 0 ? (int)seconds : 0, h = s / 3600, m = s / 60 % 60;
  if (h > 0)
    snprintf(out, n, "%02dh:%02dm:%02ds", h, m, s % 60);
  else
    snprintf(out, n, "%02dm:%02ds", m, s % 60);
}

double rsm_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

// Modifier keys, from AppKit (+[NSEvent modifierFlags])
static unsigned long modifier_flags(void) {
  static Class cls;
  static SEL sel;
  if (!cls) {
    cls = objc_getClass("NSEvent");
    sel = sel_registerName("modifierFlags");
  }
  return cls ? ((unsigned long (*)(Class, SEL))objc_msgSend)(cls, sel) : 0;
}
int rsm_ctrl_down(void) { return (modifier_flags() & (1ul << 18)) != 0; }
int rsm_shift_down(void) { return (modifier_flags() & (1ul << 17)) != 0; }

// ------------------------------------------------------------------------------------------------
// Per frame and per key
// ------------------------------------------------------------------------------------------------
static char g_last_menu[64];

static void rsmods_tick(void) {
  if (rsm_ini_poll())
    rsm_log("RSMods.ini (re)loaded: RRSpeedAboveOneHundred=%s", rsm_on("RRSpeedAboveOneHundred") ? "on" : "off");
  const char *m = rsm_menu();
  if (strncmp(m, g_last_menu, sizeof g_last_menu - 1)) {
    rsm_log("menu: %s", m);
    snprintf(g_last_menu, sizeof g_last_menu, "%s", m);
  }
  char key[sizeof g_key_posted];
  pthread_mutex_lock(&g_key_lock);
  memcpy(key, g_key_posted, sizeof key);
  pthread_mutex_unlock(&g_key_lock);
  if (strcmp(key, g_key)) {
    memcpy(g_key, key, sizeof g_key);
    rsm_log("song: %s", g_key);
    for (size_t i = 0; i < NMODS; i++)
      if (MODS[i]->song_changed) MODS[i]->song_changed(g_key);
  }
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->tick) MODS[i]->tick();
  rsm_scene_tick();
  rsm_overlay_draw();
}

// The input sink's codes: the shell's event type + 1 (rsp_gfx_event_code): 1 key down, 2 key up;
// a = virtual key code.
static void rsmods_input(int code, int a) {
  if (code != 1 && code != 2) return;
  rsm_key_event e = {a, code == 1, rsm_ctrl_down(), rsm_shift_down()};
  if (e.down) rsm_log("key down 0x%x%s%s", a, e.ctrl ? " +ctrl" : "", e.shift ? " +shift" : "");
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->key) MODS[i]->key(&e);
}

// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
#define A_GAME_FRAME 0x10017a020ull       // GameFrame() -> bool quit
#define A_INPUT_SINK 0x100ba1190ull       // (int code, int a, int b)

void rsm_send_key(int keycode, uint16_t ch) {
  const uint64_t down[] = {1, (uint32_t)keycode, ch}, up[] = {2, (uint32_t)keycode, ch};
  rsm_guest(A_INPUT_SINK, down, 3, NULL, 0);
  rsm_guest(A_INPUT_SINK, up, 3, NULL, 0);
}
#define A_AK_POST_EVENT_NAME 0x100ab2c60ull  // AK::SoundEngine::PostEvent(const char *, ...)

// After the game's frame; keeps its result.
static void on_frame(rc_ctx *c, void *user) {
  (void)user;
  uint64_t rax = c->r[0];
  rsmods_tick();
  c->r[0] = rax;
}

// The mods see a key first; the game still gets it.
static int on_input(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  int code = OV_I32(a);
  int key = OV_I32(a);
  rsmods_input(code, key);
  return 0;
}

static int on_post_event(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  note_event(name);
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->post_event && name && MODS[i]->post_event(name)) {
      t_song_event = 0;
      ov_ret_u32(c, 0);  // AK_INVALID_PLAYING_ID, as for an event Wwise does not know
      return 1;
    }
  return 0;
}

static void on_posted_event(rc_ctx *c, void *user) {
  (void)user;
  note_playing_id((uint32_t)c->r[0]);  // AkPlayingID
}

void rsm_hook(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, const char *name) {
  if (rc_hook_add(vmaddr, pre, post, NULL))
    fprintf(stderr, "[rsmods] RS Re-ARMed has no hook point %s (0x%llx): this mod needs RS Re-ARMed 1.2.0 or later\n",
            name, (unsigned long long)vmaddr);
}

__attribute__((constructor)) static void rsmods_init(void) {
  int bad = 0;
  bad |= rc_hook_add(A_GAME_FRAME, NULL, on_frame, NULL);
  bad |= rc_hook_add(A_INPUT_SINK, on_input, NULL, NULL);
  bad |= rc_hook_add(A_AK_POST_EVENT_NAME, on_post_event, on_posted_event, NULL);
  if (bad)
    fprintf(stderr, "[rsmods] this RS Re-ARMed lacks hook points: rearmed-mods needs RS Re-ARMed 1.2.0 or later\n");
  rsm_log("rearmed-mods %s loaded, %d mods", REARMED_MODS_VERSION, (int)NMODS);
}
