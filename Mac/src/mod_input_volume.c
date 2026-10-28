// RSMods: override input volume (MicrophoneVolumeOverrideMod.cpp, AudioDevices.cpp). Keeps an input device
// (the Real Tone cable) at OverrideInputVolume percent, every 2 s, only writing when it differs, as RSMods does
// with the Windows audio API. Here with Core Audio: OverrideInputVolumeDevice is matched against the device
// names (either may contain the other, ignoring case: Windows calls the cable "Microphone (Rocksmith USB Guitar
// Adapter)"); empty means the default input device. Beyond RSMods: several devices, separated by ';', each
// optionally with its own percent: "Rocksmith USB Guitar Adapter; Rocksmith Game (OBS) = 100".
#include <CoreAudio/CoreAudio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "rsmods.h"

static AudioObjectID find_device(const char *want) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  if (!want || !*want) {
    AudioObjectID dev = 0;
    UInt32 sz = sizeof dev;
    return AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev) ? 0 : dev;
  }
  a.mSelector = kAudioHardwarePropertyDevices;
  UInt32 sz = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz)) return 0;
  AudioObjectID *devs = malloc(sz), found = 0;
  if (!devs || AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, devs)) {
    free(devs);
    return 0;
  }
  for (UInt32 i = 0; i < sz / sizeof *devs && !found; i++) {
    CFStringRef name = NULL;
    UInt32 nsz = sizeof name;
    AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(devs[i], &na, 0, NULL, &nsz, &name) || !name) continue;
    char buf[256];
    if (CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8) && *buf) {
      for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
      char w[256];
      strlcpy(w, want, sizeof w);
      for (char *p = w; *p; p++) *p = (char)tolower((unsigned char)*p);
      if (strstr(buf, w) || strstr(w, buf)) found = devs[i];
    }
    CFRelease(name);
  }
  free(devs);
  return found;
}

// The input volume (0-1): the main element if it has one, else every channel's.
static int volume(AudioObjectID dev, float *get, const float *set) {
  int done = 0;
  for (UInt32 el = 0; el <= 8; el++) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeInput, el};
    if (!AudioObjectHasProperty(dev, &a)) {
      if (el == 0) continue;
      break;
    }
    Float32 v;
    UInt32 sz = sizeof v;
    if (get && !AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &v)) {
      *get = v;
      return 1;
    }
    if (set) {
      Boolean settable = 0;
      if (!AudioObjectIsPropertySettable(dev, &a, &settable) && settable) {
        v = *set;
        done |= !AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof v, &v);
      }
      if (el == 0 && done) return 1;
    }
  }
  return done;
}

static void apply(const char *name, long want) {
  AudioObjectID dev = find_device(name);
  if (!dev) return;
  if (want > 100) want %= 100;  // as RSMods
  if (want < 0) want = 0;
  float cur = -1, v = want / 100.f;
  volume(dev, &cur, NULL);
  if (cur >= 0 && (int)(cur * 100 + 0.5f) == want) return;
  if (volume(dev, NULL, &v)) rsm_log("input volume: %s set to %ld%%", *name ? name : "default input", want);
}

static void tick(void) {
  static time_t next;
  if (!rsm_on("OverrideInputVolumeEnabled")) return;
  time_t now = time(NULL);
  if (now < next) return;
  next = now + 2;
  const char *devices = rsm_get("Toggle Switches", "OverrideInputVolumeDevice");
  long dflt = rsm_long("OverrideInputVolume", 17);
  char list[512];
  strlcpy(list, devices ? devices : "", sizeof list);
  if (!strchr(list, ';') && !strchr(list, '=')) {  // RSMods' form: one device
    apply(rsm_trim(list), dflt);
    return;
  }
  char *save = NULL;
  for (char *t = strtok_r(list, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
    char *eq = strrchr(t, '=');
    long want = dflt;
    if (eq) {
      *eq = 0;
      want = strtol(eq + 1, NULL, 10);
    }
    char *name = rsm_trim(t);
    if (*name) apply(name, want);
  }
}

const rsm_mod rsm_mod_input_volume = {"input_volume", tick, NULL, NULL, NULL};
