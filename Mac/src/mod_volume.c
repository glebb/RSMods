// RSMods: volume control (VolumeControl.cpp, VolumeDisplayMod.cpp, GameOverlay::DisplayMixer). The game's
// mixer is Wwise RTPCs (0-100). Each channel's key raises it by VolumeControlInterval, Ctrl + the key lowers
// it; the changed channel then shows at the top left for a few seconds. Holding the mixer key shows every
// channel. The mute keys mute and unmute player 1 / player 2.
#include <stdio.h>

#include "rsmods.h"

static const struct {
  const char *key, *rtpc, *label;
} CHANNELS[] = {
    {"MasterVolumeKey", "Master_Volume", "Master Volume: "},
    {"SongVolumeKey", "Mixer_Music", "Song Volume: "},
    {"Player1VolumeKey", "Mixer_Player1", "Player 1 Volume: "},  // guitar and bass of player 1
    {"Player2VolumeKey", "Mixer_Player2", "Player 2 Volume: "},
    {"MicrophoneVolumeKey", "Mixer_Mic", "Microphone Volume: "},
    {"VoiceOverVolumeKey", "Mixer_VO", "Voice-Over Volume: "},
    {"SFXVolumeKey", "Mixer_SFX", "SFX Volume: "},
};
#define NCH (int)(sizeof CHANNELS / sizeof CHANNELS[0])
#define SHOW_SECONDS 3.0

static int g_mixer;          // the mixer key is held
static int g_current = -1;   // channel shown after a change
static double g_shown_at;
static int g_muted[2];
static float g_before_mute[2] = {100.f, 100.f};

static float get_volume(int ch) {
  float v = 0;
  ak_get_rtpc(CHANNELS[ch].rtpc, AK_INVALID_GAME_OBJECT, &v);
  return v;
}

static void set_volume(int ch, float v) {
  ak_set_rtpc(CHANNELS[ch].rtpc, v, AK_INVALID_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  ak_set_rtpc(CHANNELS[ch].rtpc, v, RS_SONG_GAME_OBJECT, 0, AK_CURVE_LINEAR);
}

static void show(int ch) {
  g_current = ch;
  g_shown_at = rsm_now();
}

static void line(int ch, float y) {
  char t[64];
  snprintf(t, sizeof t, "%s%d%%", CHANNELS[ch].label, (int)get_volume(ch));
  rsm_text(t, 1.f / 96, y, 0.028f, RSM_ALIGN_LEFT, 0xffffffff);
}

static void tick(void) {
  if (!rsm_on("VolumeControl")) return;
  if (g_mixer) {
    for (int i = 0; i < NCH; i++) line(i, 1.f / 54 + i * 1.f / 36);
  } else if (g_current >= 0 && rsm_now() - g_shown_at < SHOW_SECONDS) {
    line(g_current, 1.f / 54);
  }
}

static int audio_key(const rsm_key_event *e, const char *name) {
  return e->keycode == rsm_key_in("Audio Keybindings", name);
}

static void toggle_mute(int p2) {
  int ch = p2 ? 3 : 2;
  if (g_muted[p2]) {
    set_volume(ch, g_before_mute[p2]);
  } else {
    g_before_mute[p2] = get_volume(ch);
    set_volume(ch, 0);
  }
  g_muted[p2] = !g_muted[p2];
  show(ch);
  rsm_log("volume: player %d %s", p2 + 1, g_muted[p2] ? "muted" : "unmuted");
}

static void key(const rsm_key_event *e) {
  // muting works with VolumeControl off too, as in RSMods
  if (!e->down && audio_key(e, "MutePlayer1Key")) toggle_mute(0);
  if (!e->down && audio_key(e, "MutePlayer2Key")) toggle_mute(1);
  if (!rsm_on("VolumeControl")) return;
  if (audio_key(e, "DisplayMixerKey")) {
    g_mixer = e->down;
    return;
  }
  if (!e->down && audio_key(e, "ChangedSelectedVolumeKey")) {
    show(g_current < 0 ? 0 : (g_current + 1) % NCH);
    return;
  }
  if (!e->down) return;
  for (int i = 0; i < NCH; i++) {
    if (!audio_key(e, CHANNELS[i].key)) continue;
    float step = (float)rsm_long("VolumeControlInterval", 5);
    float v = get_volume(i) + (e->ctrl ? -step : step);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    set_volume(i, v);
    show(i);
    rsm_log("volume: %s %.0f", CHANNELS[i].rtpc, v);
    return;
  }
}

const rsm_mod rsm_mod_volume = {"volume", tick, key, NULL, NULL};
