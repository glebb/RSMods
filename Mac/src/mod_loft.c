// RSMods: toggle the loft (Loft.cpp, LoftMod.cpp): the venue behind the note highway goes black (for streams,
// keyed out with a luma key). RSMods sets the far plane of the loft's camera from 10000 to 1. ToggleLoft = on
// and ToggleLoftWhen: manual (the key toggles it), song (off in songs) or startup (always off). Lessons
// (Guided Experience) keep the loft unless it is manual.
//
// The camera is a Gamebryo NiCamera: view frustum at +0x168, far plane at +0x17c; the loft's is the one with
// far 10000, as RSMods finds it. On this build the game sets every camera's frustum each frame through
// NiCamera::SetViewFrustum (0x100e438c0, a hook point), so a far plane written from outside does not last:
// after each SetViewFrustum, a camera whose far plane is 10000 gets 1 while the loft is off. The cameras it did
// that to get 10000 back when the loft comes on (the next SetViewFrustum would also restore them).
// The far plane takes the stage's props; the song's backdrop with Venue Mode off (the "Narnia" stage: fog,
// portal, back wall), the Loft menu's room and the tone's gear are hidden by name in the scene graph
// (rsm_scene_hide). Some cameras (the back wall's) get their frustum once, when the scene is built: every camera
// the hook has seen is kept, and while the loft is off those still at 10000 are set directly too, after a safe
// read shows they are still NiCameras.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods.h"

#define NICAMERA_VTABLE 0x101841d60ull

#define A_CAMERA_SET_FRUSTUM 0x100e438c0ull
#define CAM_FAR 0x17c
#define LOFT_FAR 10000.f
#define OFF_FAR 1.f
#define MAX_CAMS 64

static const char *const LESSONS[] = {"GuidedExperience_Game", "GuidedExperience_Pause", NULL};

static atomic_int g_want_off;  // set by the tick, read by the hook (game thread both, but kept simple)
static uint64_t g_cams[MAX_CAMS];
static float g_cam_far[MAX_CAMS];  // each one's far plane before
static int g_ncams;
// Far planes to cut besides 10000 ([Mod Settings] LoftExtraFar, comma separated): this build's scene has the
// back wall under another camera than RSMods' loft camera
#define MAX_EXTRA 8
static float g_extra[MAX_EXTRA];
static int g_nextra;

static int cut(float f) {
  if (f == LOFT_FAR) return 1;
  for (int i = 0; i < g_nextra; i++)
    if (f == g_extra[i]) return 1;
  return 0;
}
static int g_manual_off, g_was_off;

static __thread uint64_t t_cam;  // the camera of the SetViewFrustum running on this thread

static int before_set_frustum(rc_ctx *c, void *user) {
  (void)user;
  t_cam = c->r[7];  // rdi: this (rdi is not preserved across the call)
  return 0;
}

// Every camera seen, once (logged with RS_MODS_LOG: which cameras the scene has and their planes)
#define MAX_SEEN 128
static uint64_t g_seen[MAX_SEEN];
static int g_nseen;

static void log_camera(uint64_t cam) {
  for (int i = 0; i < g_nseen; i++)
    if (g_seen[i] == cam) return;
  if (g_nseen < MAX_SEEN)
    g_seen[g_nseen++] = cam;
  else
    memmove(g_seen, g_seen + 1, sizeof g_seen - sizeof *g_seen), g_seen[MAX_SEEN - 1] = cam;
  float fr[6];
  memcpy(fr, (void *)(uintptr_t)(cam + 0x168), sizeof fr);
  rsm_log("loft: camera %llx frustum l %.3f r %.3f t %.3f b %.3f near %.3f far %.1f (menu %s)",
          (unsigned long long)cam, fr[0], fr[1], fr[2], fr[3], fr[4], fr[5], rsm_menu());
}

static void after_set_frustum(rc_ctx *c, void *user) {
  (void)c, (void)user;
  uint64_t cam = t_cam;
  if (!cam) return;
  log_camera(cam);
  if (!atomic_load(&g_want_off)) return;
  float f;
  memcpy(&f, (void *)(uintptr_t)(cam + CAM_FAR), 4);
  if (!cut(f)) return;
  float off = OFF_FAR;
  memcpy((void *)(uintptr_t)(cam + CAM_FAR), &off, 4);
  for (int i = 0; i < g_ncams; i++)
    if (g_cams[i] == cam) return;
  if (g_ncams < MAX_CAMS) {
    g_cam_far[g_ncams] = f;
    g_cams[g_ncams++] = cam;
  }
  rsm_log("loft: off (camera at %llx, far %.1f)", (unsigned long long)cam, f);
}

// The far plane of a seen camera, if it is still a NiCamera (a safe read: it may have been freed)
static int read_far(uint64_t cam, float *far) {
  uint8_t buf[CAM_FAR + 4];
  mach_vm_size_t got = 0;
  if (mach_vm_read_overwrite(mach_task_self(), cam, sizeof buf, (mach_vm_address_t)(uintptr_t)buf, &got) ||
      got != sizeof buf)
    return 0;
  uint64_t vt;
  memcpy(&vt, buf, 8);
  memcpy(far, buf + CAM_FAR, 4);
  return vt == RC_A(NICAMERA_VTABLE);
}

// Diagnostic (RS_MODS_LOG): every NiCamera in the game's writable memory, with its frustum, to compare with the
// cameras the hook sees. A thread scans; the cameras are read safely.
static void log_scanned_camera(uint64_t cam, int kind, const char *cls, void *ctx) {
  (void)kind;
  int *total = ctx;
  if (strcmp(cls, "NiCamera")) return;
  uint8_t b[0x168 + 24];
  mach_vm_size_t got = 0;
  if (mach_vm_read_overwrite(mach_task_self(), cam, sizeof b, (mach_vm_address_t)(uintptr_t)b, &got) || got != sizeof b)
    return;
  float fr[6];
  memcpy(fr, b + 0x168, sizeof fr);
  rsm_log("loft scan: camera %llx l %.3f r %.3f t %.3f b %.3f near %.3f far %.1f", (unsigned long long)cam, fr[0],
          fr[1], fr[2], fr[3], fr[4], fr[5]);
  (*total)++;
}

static void *scan_cameras(void *arg) {
  (void)arg;
  int total = 0;
  rsm_scene_each(log_scanned_camera, &total);
  rsm_log("loft scan: %d cameras", total);
  return NULL;
}

// Loft off for the cameras whose frustum is not set every frame
static void cut_camera(uint64_t cam) {
  float f;
  if (!read_far(cam, &f) || !cut(f)) return;
  float off = OFF_FAR;
  memcpy((void *)(uintptr_t)(cam + CAM_FAR), &off, 4);
  int known = 0;
  for (int j = 0; j < g_ncams; j++) known |= g_cams[j] == cam;
  if (!known && g_ncams < MAX_CAMS) {
    g_cam_far[g_ncams] = f;
    g_cams[g_ncams++] = cam;
  }
  rsm_log("loft: off (camera at %llx, far %.1f, set once)", (unsigned long long)cam, f);
}

static void cut_seen(void) {
  for (int i = 0; i < g_nseen; i++) cut_camera(g_seen[i]);
}

static void restore(void) {
  for (int i = 0; i < g_ncams; i++) {
    float f;
    if (!read_far(g_cams[i], &f)) continue;
    if (f == OFF_FAR) {
      f = g_cam_far[i];
      memcpy((void *)(uintptr_t)(g_cams[i] + CAM_FAR), &f, 4);
    }
  }
  g_ncams = 0;
}

static void tick(void) {
  int want_off = 0;
  const char *extra = rsm_get("Mod Settings", "LoftExtraFar");
  g_nextra = 0;
  for (const char *p = extra; p && *p && g_nextra < MAX_EXTRA;) {
    char *end;
    float v = strtof(p, &end);
    if (end == p) break;
    g_extra[g_nextra++] = v;
    p = end;
    while (*p == ',' || *p == ' ') p++;
  }
  if (rsm_on("ToggleLoft")) {
    const char *when = rsm_get("Toggle Switches", "ToggleLoftWhen");
    if (!when || !strcasecmp(when, "manual"))
      want_off = g_manual_off;
    else if (rsm_menu_in(LESSONS))
      want_off = 0;
    else if (!strcasecmp(when, "startup"))
      want_off = 1;
    else
      want_off = rsm_menu_in(RSM_SONG_MODES);
  } else {
    g_manual_off = 0;
  }
  atomic_store(&g_want_off, want_off);
  // the venue's props and floor (GRVenueActor*), and the tone's gear (amps, cabinets) placed in the venue
  static const char *const BACKDROP[] = {"NarniaFogModel", "NarniaPortalModel", "backwall_*", "loft_root_node",
                                         "GearPlacementNode", "GRVenueActor", "GRVenueActorAlpha", NULL};
  rsm_scene_hide("loft", BACKDROP, RSM_CULL, want_off);
  static unsigned frame;
  if (want_off && ++frame % 30 == 0) cut_seen();  // twice a second
  if (!want_off && g_was_off) {
    restore();
    rsm_log("loft: on");
  }
  g_was_off = want_off;
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("ToggleLoftKey") || !rsm_on("ToggleLoft")) return;
  const char *when = rsm_get("Toggle Switches", "ToggleLoftWhen");
  if (when && strcasecmp(when, "manual")) return;
  g_manual_off = !g_manual_off;
  if (g_manual_off && getenv("RS_MODS_LOG") && *getenv("RS_MODS_LOG") != '0') {  // diagnostic only
    pthread_t t;
    if (pthread_create(&t, NULL, scan_cameras, NULL) == 0) pthread_detach(t);
  }
}

__attribute__((constructor)) static void init(void) {
  rsm_hook(A_CAMERA_SET_FRUSTUM, before_set_frustum, after_set_frustum, "camera_set_frustum");
}

const rsm_mod rsm_mod_loft = {"loft", tick, key, NULL, NULL};
