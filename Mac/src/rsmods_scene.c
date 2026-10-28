// RSMods: the game's Gamebryo scene graph and render steps, for the mods that hide parts of the picture (the
// venue, the headstock, the skyline, the lyrics) where RSMods skips their Direct3D draws.
//
// Layout (this build):
//   NiObjectNET   name, an NiFixedString (char *), at +0x20
//   NiAVObject    flags (16 bits) at +0x3c, bit 0 = app culled (the object and its children are not drawn; tested
//                 by the cull code, e.g. 0x100e351a0); parent at +0x40; local transform at +0x74, world at +0xac
//                 (NiTransform: +0x4 rotation, +0x28 translation, +0x34 scale; constructor 0x100f07c00)
//   NiRenderStep  name at +0x30, active (byte) at +0x38: NiRenderFrame's draw (0x100ea8e20) skips inactive steps
// An object's class: vtable slot 3 is GetRTTI, `push rbp; mov rbp,rsp; lea rax,[rip+X]; pop rbp; ret`, decoded
// from the image's x86 bytes; NiRTTI is {name, base}.
//
// Three ways to hide by name (rsm_scene_hide):
//   RSM_CULL      the app-culled bit: objects in the scene tree
//   RSM_COLLAPSE  scale 0 (local and world): objects the game draws directly, outside the tree's culling
//                 (parentless meshes such as the skyline's Banner*), for which the culled bit does nothing
//   RSM_STEP_OFF  a render step's active byte (the lyrics are their own steps)
// Objects are on the heap: a thread scans the game's writable memory for pointers to vtables of these classes,
// reading with mach_vm_read_overwrite (safe against memory freed meanwhile); the game thread re-reads an object's
// name before it writes. The diagnostic key (SceneDumpKey) writes every object to scene-dump.txt in the game folder.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rsmods.h"

#define NAME 0x20
#define FLAGS 0x3c
#define PARENT 0x40
#define LOCAL_SCALE (0x74 + 0x34)
#define WORLD_SCALE (0xac + 0x34)
#define STEP_NAME 0x30
#define STEP_ACTIVE 0x38
#define GETRTTI_SLOT 3

static int safe_read(uint64_t a, void *out, size_t n) {
  mach_vm_size_t got = 0;
  return !mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)(uintptr_t)out, &got) && got == n;
}

static int in_image(uint64_t a) { return a >= rc_img_lo && a < rc_img_hi; }

// The image is mapped for the whole run: read it directly
static int image_read(uint64_t a, void *out, size_t n) {
  if (!in_image(a) || a + n > rc_img_hi) return 0;
  memcpy(out, (const void *)(uintptr_t)a, n);
  return 1;
}

// The RTTI of a vtable (host address), or 0 if its slot 3 is not a GetRTTI
static uint64_t vtable_rtti(uint64_t vt) {
  uint64_t fn;
  uint8_t code[12];
  if ((vt & 7) || !image_read(vt + 8 * GETRTTI_SLOT, &fn, 8) || !image_read(fn, code, sizeof code)) return 0;
  static const uint8_t pre[] = {0x55, 0x48, 0x89, 0xe5, 0x48, 0x8d, 0x05};
  if (memcmp(code, pre, sizeof pre) || code[11] != 0x5d) return 0;
  int32_t rel;
  memcpy(&rel, code + 7, 4);
  uint64_t rtti = fn + 11 + (int64_t)rel;
  return in_image(rtti) ? rtti : 0;
}

static int rtti_name(uint64_t rtti, char *out, size_t n) {
  uint64_t p;
  if (!image_read(rtti, &p, 8) || !p) return 0;
  if (!image_read(p, out, n - 1) && !safe_read(p, out, n - 1)) return 0;
  out[n - 1] = 0;
  return 1;
}

// Class name, and which of NiAVObject / NiRenderStep is among its bases, cached per vtable. The scan meets every
// pointer into the image (strings, data, code), not only vtables: a direct-mapped cache that overwrites, never
// fills up. Per thread (the dump and the hide scan may run at once).
#define VT_CACHE (1 << 16)
typedef struct {
  uint64_t vt;
  int kind;  // RSM_KIND_AV, RSM_KIND_STEP or 0
  char cls[24];
} vt_entry;
static __thread vt_entry *t_vt;

static int vtable_class(uint64_t vt, const char **cls) {
  if (!t_vt && !(t_vt = calloc(VT_CACHE, sizeof *t_vt))) return 0;
  vt_entry *e = &t_vt[(vt >> 3) % VT_CACHE];
  if (e->vt != vt) {
    e->vt = vt;
    e->kind = 0;
    e->cls[0] = 0;
    uint64_t r = vtable_rtti(vt);
    if (r) rtti_name(r, e->cls, sizeof e->cls);
    for (int depth = 0; r && depth < 16; depth++) {
      char nm[24];
      if (!rtti_name(r, nm, sizeof nm)) break;
      if (!strcmp(nm, "NiAVObject")) {
        e->kind = RSM_KIND_AV;
        break;
      }
      if (!strcmp(nm, "NiRenderStep")) {
        e->kind = RSM_KIND_STEP;
        break;
      }
      if (!image_read(r + 8, &r, 8)) break;
    }
  }
  *cls = e->cls;
  return e->kind;
}

static int read_name(uint64_t obj, int kind, char *out, size_t n) {
  uint64_t p;
  if (!safe_read(obj + (kind == RSM_KIND_STEP ? STEP_NAME : NAME), &p, 8) || !p || !safe_read(p, out, n - 1)) {
    *out = 0;
    return 0;
  }
  out[n - 1] = 0;
  for (char *c = out; *c; c++)
    if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e) {
      *c = 0;
      break;
    }
  return 1;
}

int rsm_scene_name(uint64_t obj, char *out, size_t n) { return read_name(obj, RSM_KIND_AV, out, n); }

void rsm_scene_each(void (*fn)(uint64_t obj, int kind, const char *cls, void *ctx), void *ctx) {
  enum { CHUNK = 1 << 20 };
  uint64_t *buf = malloc(CHUNK);
  if (!buf) return;
  // The buffer itself is not scanned: read into itself, it would hold a copy of the last chunk, and the objects in
  // that copy would be reported at addresses in the buffer
  const mach_vm_address_t buf_lo = (uintptr_t)buf, buf_hi = buf_lo + CHUNK;
  mach_vm_address_t addr = 0;
  for (;;) {
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t o;
    if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &cnt, &o))
      break;
    int rw = (info.protection & (VM_PROT_READ | VM_PROT_WRITE)) == (VM_PROT_READ | VM_PROT_WRITE);
    int image = addr < rc_img_hi && addr + size > rc_img_lo;
    mach_vm_address_t end = addr + size;
    for (mach_vm_address_t p = addr; rw && !image && p < end;) {
      if (p >= buf_lo && p < buf_hi) {
        p = buf_hi;
        continue;
      }
      mach_vm_size_t len = end - p < CHUNK ? end - p : CHUNK, got = 0;
      if (p < buf_lo && p + len > buf_lo) len = buf_lo - p;
      mach_vm_address_t chunk = p;
      p += len;
      if (mach_vm_read_overwrite(mach_task_self(), chunk, len, buf_lo, &got)) continue;
      for (mach_vm_size_t i = 0; i < got / 8; i++) {
        if (!in_image(buf[i])) continue;
        const char *cls;
        int kind = vtable_class(buf[i], &cls);
        if (kind) fn(chunk + i * 8, kind, cls, ctx);
      }
    }
    addr += size;
  }
  free(buf);
}

// ------------------------------------------------------------------------------------------------
// Hiding by name: each owner (a mod) gives a list of names (a trailing '*' matches a prefix; the ":N" suffixes of
// instanced names are ignored), how to hide them, and whether they should be hidden now. A thread finds the
// objects after scene changes; the game thread hides and shows them, re-reading the name first.
// ------------------------------------------------------------------------------------------------
#define MAX_OWNERS 8
#define MAX_NAMES 32
#define MAX_HIDDEN 512

static struct {
  const char *owner;
  char names[MAX_NAMES][64];
  int nnames, on, how;
} g_owner[MAX_OWNERS];
static int g_nowner;

static pthread_mutex_t g_hide_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  uint64_t obj;
  int kind;
  char name[64];
  int set_by_us;  // hidden by us (and shown again by us)
  int how;        // how it was hidden
  float scale[2]; // RSM_COLLAPSE: the scales before
} g_found[MAX_HIDDEN];
static int g_nfound;
static int g_scan_wanted, g_names_changed;

static int name_matches(const char *pattern, const char *name) {
  size_t pl = strlen(pattern);
  if (pl && pattern[pl - 1] == '*') return !strncmp(name, pattern, pl - 1);
  size_t nl = strlen(name);
  const char *colon = strrchr(name, ':');
  if (colon && colon[1] >= '0' && colon[1] <= '9') nl = (size_t)(colon - name);
  return nl == pl && !strncmp(name, pattern, pl);
}

// The owner whose rules cover this object (steps only for RSM_STEP_OFF rules, objects for the others)
static int owner_of(const char *name, int kind) {
  for (int o = 0; o < g_nowner; o++) {
    if ((g_owner[o].how == RSM_STEP_OFF) != (kind == RSM_KIND_STEP)) continue;
    for (int i = 0; i < g_owner[o].nnames; i++)
      if (name_matches(g_owner[o].names[i], name)) return o;
  }
  return -1;
}

typedef struct {
  int n;
  uint64_t obj[MAX_HIDDEN];
  int kind[MAX_HIDDEN];
  char name[MAX_HIDDEN][64];
} scan_result;

static void collect(uint64_t obj, int kind, const char *cls, void *ctx) {
  (void)cls;
  scan_result *r = ctx;
  char name[64];
  if (r->n == MAX_HIDDEN || !read_name(obj, kind, name, sizeof name) || !*name) return;
  if (owner_of(name, kind) < 0) return;  // owners only change on the game thread; the next scan fixes a stale read
  r->obj[r->n] = obj;
  r->kind[r->n] = kind;
  snprintf(r->name[r->n], 64, "%s", name);
  r->n++;
}

static void *hide_scanner(void *arg) {
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&g_hide_lock);
    int want = g_scan_wanted;
    g_scan_wanted = 0;
    pthread_mutex_unlock(&g_hide_lock);
    if (!want) {
      usleep(100000);
      continue;
    }
    scan_result *r = calloc(1, sizeof *r);
    if (!r) continue;
    rsm_scene_each(collect, r);
    pthread_mutex_lock(&g_hide_lock);
    // keep the objects already known (and what we did to them); add the new ones
    for (int i = 0; i < r->n; i++) {
      int known = 0;
      for (int j = 0; j < g_nfound; j++) known |= g_found[j].obj == r->obj[i];
      if (known || g_nfound == MAX_HIDDEN) continue;
      memset(&g_found[g_nfound], 0, sizeof g_found[0]);
      g_found[g_nfound].obj = r->obj[i];
      g_found[g_nfound].kind = r->kind[i];
      snprintf(g_found[g_nfound].name, 64, "%s", r->name[i]);
      g_nfound++;
    }
    pthread_mutex_unlock(&g_hide_lock);
    rsm_log("scene: scan matched %d objects", r->n);
    free(r);
  }
  return NULL;
}

void rsm_scene_hide(const char *owner, const char *const *names, int how, int on) {
  int o;
  for (o = 0; o < g_nowner; o++)
    if (!strcmp(g_owner[o].owner, owner)) break;
  if (o == g_nowner) {
    if (g_nowner == MAX_OWNERS) return;
    g_owner[o].owner = owner;
    g_nowner++;
  }
  int n = 0, changed = 0;
  for (; names && names[n] && n < MAX_NAMES; n++) {
    if (n >= g_owner[o].nnames || strcmp(g_owner[o].names[n], names[n])) changed = 1;
    snprintf(g_owner[o].names[n], 64, "%s", names[n]);
  }
  if (n != g_owner[o].nnames || how != g_owner[o].how) changed = 1;
  g_owner[o].nnames = n;
  g_owner[o].how = how;
  if (changed || (on && !g_owner[o].on)) g_names_changed = 1;  // new names, or switched on: scan
  g_owner[o].on = on;
}

static void request_hide_scan(void) {
  static int started;
  if (!started) {
    pthread_t t;
    if (pthread_create(&t, NULL, hide_scanner, NULL) == 0) pthread_detach(t);
    started = 1;
  }
  pthread_mutex_lock(&g_hide_lock);
  g_scan_wanted = 1;
  pthread_mutex_unlock(&g_hide_lock);
}

static void put_f32(uint64_t a, float v) { memcpy((void *)(uintptr_t)a, &v, 4); }

static void hide(int i, int how) {
  uint64_t obj = g_found[i].obj;
  if (how == RSM_STEP_OFF) {
    uint8_t active;
    if (!safe_read(obj + STEP_ACTIVE, &active, 1) || !active) return;
    *(volatile uint8_t *)(uintptr_t)(obj + STEP_ACTIVE) = 0;
  } else if (how == RSM_COLLAPSE) {
    float s[2];
    if (!safe_read(obj + LOCAL_SCALE, &s[0], 4) || !safe_read(obj + WORLD_SCALE, &s[1], 4)) return;
    if (s[0] == 0 && s[1] == 0) return;
    if (!g_found[i].set_by_us) memcpy(g_found[i].scale, s, sizeof s);
    put_f32(obj + LOCAL_SCALE, 0);
    put_f32(obj + WORLD_SCALE, 0);
  } else {
    uint16_t flags;
    if (!safe_read(obj + FLAGS, &flags, 2) || (flags & 1)) return;
    *(volatile uint16_t *)(uintptr_t)(obj + FLAGS) = flags | 1;
  }
  if (!g_found[i].set_by_us) rsm_log("scene: hid %s (%llx)", g_found[i].name, (unsigned long long)obj);
  g_found[i].set_by_us = 1;
  g_found[i].how = how;
}

static void show(int i) {
  uint64_t obj = g_found[i].obj;
  if (g_found[i].how == RSM_STEP_OFF) {
    *(volatile uint8_t *)(uintptr_t)(obj + STEP_ACTIVE) = 1;
  } else if (g_found[i].how == RSM_COLLAPSE) {
    put_f32(obj + LOCAL_SCALE, g_found[i].scale[0]);
    put_f32(obj + WORLD_SCALE, g_found[i].scale[1]);
  } else {
    uint16_t flags;
    if (safe_read(obj + FLAGS, &flags, 2)) *(volatile uint16_t *)(uintptr_t)(obj + FLAGS) = flags & ~1;
  }
  g_found[i].set_by_us = 0;
  rsm_log("scene: showed %s (%llx)", g_found[i].name, (unsigned long long)obj);
}

// From the core's tick, after the mods': scans after scene changes, hides and shows.
void rsm_scene_tick(void) {
  static char last_menu[64];
  static double rescan_at, periodic_at;
  double now = rsm_now();
  int any_on = 0;
  for (int o = 0; o < g_nowner; o++) any_on |= g_owner[o].on && g_owner[o].nnames;
  const char *m = rsm_menu();
  if (strncmp(m, last_menu, sizeof last_menu - 1)) {
    snprintf(last_menu, sizeof last_menu, "%s", m);
    rescan_at = now + 2;  // the scene of a new menu loads over a moment: scan now and again in 2 s
    if (any_on) request_hide_scan();
  }
  // objects created later in a scene (the tone's gear a few seconds into a song): scan every 15 s while hiding
  if (any_on && (g_names_changed || (rescan_at && now >= rescan_at) || now >= periodic_at)) {
    g_names_changed = 0;
    rescan_at = 0;
    periodic_at = now + 15;
    request_hide_scan();
  }
  static unsigned frame;
  if (++frame % 15) return;
  pthread_mutex_lock(&g_hide_lock);
  for (int i = 0; i < g_nfound; i++) {
    char name[64];
    if (!read_name(g_found[i].obj, g_found[i].kind, name, sizeof name) || strcmp(name, g_found[i].name)) {
      g_found[i] = g_found[--g_nfound];  // gone (freed or reused)
      i--;
      continue;
    }
    int o = owner_of(name, g_found[i].kind);
    if (o >= 0 && g_owner[o].on)
      hide(i, g_owner[o].how);
    else if (g_found[i].set_by_us)
      show(i);
  }
  pthread_mutex_unlock(&g_hide_lock);
}

// ------------------------------------------------------------------------------------------------
// Diagnostic: SceneDumpKey writes every object and render step to scene-dump.txt
// ------------------------------------------------------------------------------------------------
static void dump_one(uint64_t obj, int kind, const char *cls, void *ctx) {
  FILE *f = ctx;
  char name[96];
  read_name(obj, kind, name, sizeof name);
  if (kind == RSM_KIND_STEP) {
    uint8_t active = 0;
    safe_read(obj + STEP_ACTIVE, &active, 1);
    fprintf(f, "%llx\t%s\t%s\tactive %d\tstep\n", (unsigned long long)obj, cls, name, active);
    return;
  }
  uint16_t flags = 0;
  uint64_t parent = 0;
  safe_read(obj + FLAGS, &flags, 2);
  safe_read(obj + PARENT, &parent, 8);
  fprintf(f, "%llx\t%s\t%s\tflags %04x\tparent %llx\n", (unsigned long long)obj, cls, name, flags,
          (unsigned long long)parent);
}

static void *dump_thread(void *arg) {
  char menu[64];
  snprintf(menu, sizeof menu, "%s", (const char *)arg);
  free(arg);
  FILE *f = fopen("scene-dump.txt", "w");
  if (!f) return NULL;
  fprintf(f, "# NiAVObjects and NiRenderSteps in memory (menu %s): address, class, name, flags (bit 0 app culled) "
             "or active, parent\n",
          menu);
  rsm_scene_each(dump_one, f);
  fclose(f);
  free(t_vt);
  t_vt = NULL;
  rsm_log("scene: dump written to scene-dump.txt");
  return NULL;
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("SceneDumpKey")) return;
  pthread_t t;
  char *menu = strdup(rsm_menu());
  if (menu && pthread_create(&t, NULL, dump_thread, menu) == 0)
    pthread_detach(t);
  else
    free(menu);
  rsm_log("scene: dumping");
}

const rsm_mod rsm_mod_scene_dump = {"scene_dump", NULL, key, NULL, NULL};
