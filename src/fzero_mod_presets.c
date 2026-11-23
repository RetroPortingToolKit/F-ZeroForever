#ifdef _WIN32
#include <windows.h>
#undef FORCEINLINE
#else
#include <unistd.h>
#endif
#include "fzero_mod_presets.h"
#include "fzero_menu_music.h"
#include "fzero_title.h"
#include "fzero_tracks.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

enum { MAX_SAVED_PRESETS = 32 };

typedef struct FzeroSavedPreset {
  char id[64], name[96];
  FzeroVideoSettings video;
  bool track_loader, title_enabled, menu_music, msu_enabled;
  char title[CP_ID], menu_pack[CP_ID], menu_cues[FZERO_MENU_CUES][CP_PATH];
} FzeroSavedPreset;

static FzeroSavedPreset saved[MAX_SAVED_PRESETS];
static char settings_path[CP_PATH], error[128];

static bool failed(const char *message) {
  snprintf(error, sizeof(error), "%s", message);
  return false;
}

const char *FzeroSavedPresetError(void) { return error; }

static bool path_for(unsigned slot, char path[CP_PATH]) {
  if (!*settings_path) return false;
  int length = snprintf(path, CP_PATH, "%s.preset-%02u.ini", settings_path, slot + 1);
  return length >= 0 && length < CP_PATH;
}

static bool same_name(const char *a, const char *b) {
  while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
    ++a;
    ++b;
  }
  return *a == *b;
}

static bool valid_name(const char *name) {
  size_t length = strlen(name);
  if (!length || length >= sizeof(saved[0].name) || strstr(name, "##")) return false;
  if (isspace((unsigned char)name[0]) || isspace((unsigned char)name[length - 1])) return false;
  for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    if (*p < 32 || *p == 127) return false;
  return true;
}

static int slot_for(const char *id) {
  if (id && *id)
    for (unsigned i = 0; i < MAX_SAVED_PRESETS; ++i)
      if (*saved[i].id && !strcmp(saved[i].id, id)) return (int)i;
  return -1;
}

static bool copy_field(char *destination, size_t size, const char *value) {
  if (strlen(value) >= size) return false;
  strcpy(destination, value);
  return true;
}

static bool read_preset(unsigned slot, const char *path, FzeroSavedPreset *out) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  char line[CP_PATH + 64];
  unsigned version = 0;
  bool valid = true, video_start = false;
  while (valid && fgets(line, sizeof(line), file)) {
    char *end = strpbrk(line, "\r\n");
    if (!end) { valid = false; break; }
    *end = 0;
    if (!strcmp(line, "[FZeroVideo]")) { video_start = true; break; }
    char *value = strchr(line, '=');
    if (!value) continue;
    *value++ = 0;
    if (!strcmp(line, "PresetFormat")) version = !strcmp(value, "1");
    else if (!strcmp(line, "Name")) valid = valid_name(value) &&
        copy_field(out->name, sizeof(out->name), value);
    else if (!strcmp(line, "TitleStyle")) valid = copy_field(out->title, sizeof(out->title), value);
    else if (!strcmp(line, "TrackLoader")) out->track_loader = !strcmp(value, "1");
    else if (!strcmp(line, "TitleEnabled")) out->title_enabled = !strcmp(value, "1");
    else if (!strcmp(line, "MsuEnabled")) out->msu_enabled = !strcmp(value, "1");
    else if (!strcmp(line, "MenuMusic")) out->menu_music = !strcmp(value, "1");
    else if (!strcmp(line, "MenuPack")) valid = copy_field(out->menu_pack, sizeof(out->menu_pack), value);
    else if (!strncmp(line, "MenuCue-", 8))
      for (unsigned i = 0; i < FZERO_MENU_CUES; ++i)
        if (!strcmp(line + 8, FzeroMenuCues[i].id))
          valid = copy_field(out->menu_cues[i], sizeof(out->menu_cues[i]), value);
  }
  fclose(file);
  if (!valid || !version || !video_start || !out->name[0] ||
      !out->title[0] || !FzeroVideoLoad(&out->video, path))
    return false;
  snprintf(out->id, sizeof(out->id), "saved-%02u", slot + 1);
  return true;
}

void FzeroSavedPresetsLoad(const char *video_path) {
  memset(saved, 0, sizeof(saved));
  settings_path[0] = error[0] = 0;
  if (!video_path || !copy_field(settings_path, sizeof(settings_path), video_path)) return;
  for (unsigned i = 0; i < MAX_SAVED_PRESETS; ++i) {
    char path[CP_PATH];
    if (path_for(i, path)) read_preset(i, path, &saved[i]);
  }
}

int FzeroSavedPresetsCount(void) {
  int count = 0;
  for (unsigned i = 0; i < MAX_SAVED_PRESETS; ++i) count += *saved[i].id != 0;
  return count;
}

int FzeroSavedPresetGet(int index, RecompLauncherCModPreset *out) {
  if (index < 0 || !out) return 0;
  for (unsigned i = 0; i < MAX_SAVED_PRESETS; ++i) {
    if (!*saved[i].id || index-- != 0) continue;
    *out = (RecompLauncherCModPreset){.editable = 1};
    strcpy(out->id, saved[i].id);
    strcpy(out->name, saved[i].name);
    return 1;
  }
  return 0;
}

static void capture(FzeroSavedPreset *preset, const FzeroVideoSettings *video,
                    const RecompLauncherCSettings *launcher) {
  preset->video = *video;
  preset->video.gameplay.enabled &= FZERO_RULE_SELECTABLE_MASK;
  preset->track_loader = FzeroTrackLoaderEnabled();
  preset->title_enabled = FzeroTracksTitleEnabled();
  preset->menu_music = FzeroMenuMusicEnabled();
  preset->msu_enabled = launcher->msu1_enabled != 0;
  snprintf(preset->title, sizeof(preset->title), "%s", FzeroTracksTitleStyle());
  snprintf(preset->menu_pack, sizeof(preset->menu_pack), "%s", FzeroMenuMusicPack());
  for (unsigned i = 0; i < FZERO_MENU_CUES; ++i) {
    preset->menu_cues[i][0] = 0;
    if (FzeroMenuMusicCustom(i))
      FzeroMenuMusicPath(i, preset->menu_cues[i], sizeof(preset->menu_cues[i]));
  }
}

static bool video_matches(const FzeroVideoSettings *a, const FzeroVideoSettings *b) {
  return a->enhanced == b->enhanced && a->aspect == b->aspect &&
         a->fps == b->fps && a->fps_enabled == b->fps_enabled &&
         a->fps_readout == b->fps_readout && a->bs_deluxe == b->bs_deluxe &&
         a->bs_tracks == b->bs_tracks && a->hd_mode7 == b->hd_mode7 &&
         a->hd_scale == b->hd_scale && a->hd_supersample == b->hd_supersample &&
         a->diagnostics == b->diagnostics && a->crash_replay == b->crash_replay &&
         a->local_players == b->local_players && a->local_two_player == b->local_two_player &&
         a->lap_callouts == b->lap_callouts && a->lap_boost_second_lap_only == b->lap_boost_second_lap_only &&
         a->gameplay.enabled == b->gameplay.enabled && a->gameplay.tuning == b->gameplay.tuning &&
         a->gameplay.boost == b->gameplay.boost && a->gameplay.exhaust == b->gameplay.exhaust &&
         a->gameplay.vehicle_packs == b->gameplay.vehicle_packs &&
         a->gameplay.stock_rebalance == b->gameplay.stock_rebalance &&
         a->gameplay.always_records == b->gameplay.always_records &&
         a->gameplay.practice_ghosts == b->gameplay.practice_ghosts &&
         !strcmp(a->lap_boost_wav, b->lap_boost_wav) && !strcmp(a->final_lap_wav, b->final_lap_wav) &&
         !strcmp(a->finish_wav, b->finish_wav);
}

const char *FzeroSavedPresetCurrent(const FzeroVideoSettings *video,
                                    const RecompLauncherCSettings *launcher) {
  if (!launcher) return "";
  FzeroSavedPreset live = {0};
  capture(&live, video, launcher);
  for (unsigned i = 0; i < MAX_SAVED_PRESETS; ++i) {
    const FzeroSavedPreset *preset = &saved[i];
    if (!*preset->id || !video_matches(&live.video, &preset->video) ||
        live.track_loader != preset->track_loader || live.title_enabled != preset->title_enabled ||
        live.menu_music != preset->menu_music || live.msu_enabled != preset->msu_enabled ||
        strcmp(live.title, preset->title) || strcmp(live.menu_pack, preset->menu_pack))
      continue;
    bool matches = true;
    for (unsigned cue = 0; cue < FZERO_MENU_CUES; ++cue)
      if (strcmp(live.menu_cues[cue], preset->menu_cues[cue])) { matches = false; break; }
    if (matches) return preset->id;
  }
  return "";
}

bool FzeroSavedPresetApply(const char *id, FzeroVideoSettings *video, RecompLauncherCSettings *launcher) {
  error[0] = 0;
  int slot = slot_for(id);
  if (slot < 0 || !video || !launcher) return failed("Saved preset is unavailable");
  const FzeroSavedPreset *preset = &saved[slot];
  if (!FzeroTracksSetTitleStyle(preset->title))
    return failed("The saved preset's title artwork is unavailable");
  *video = preset->video;
  FzeroTrackLoaderEnable(preset->track_loader);
  FzeroTracksEnableTitle(preset->title_enabled);
  launcher->msu1_enabled = preset->msu_enabled;
  FzeroMenuMusicInit();
  FzeroMenuMusicEnable(preset->menu_music);
  char line[CP_PATH + 64];
  snprintf(line, sizeof(line), "menu_pack=%s", preset->menu_pack);
  FzeroMenuMusicRead(line);
  for (unsigned i = 0; i < FZERO_MENU_CUES; ++i) {
    snprintf(line, sizeof(line), "menu_%s=%s", FzeroMenuCues[i].id, preset->menu_cues[i]);
    FzeroMenuMusicRead(line);
  }
  return true;
}

static bool write_preset(const char *path, const FzeroSavedPreset *preset, bool replace) {
  char temporary[CP_PATH + 32];
#ifdef _WIN32
  unsigned long process = GetCurrentProcessId();
#else
  unsigned long process = (unsigned long)getpid();
#endif
  snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, process);
  FILE *file = fopen(temporary, "wb");
  if (!file) return false;
  bool ok = fprintf(file, "PresetFormat=1\nName=%s\nTitleStyle=%s\nTrackLoader=%d\nTitleEnabled=%d\n"
                    "MsuEnabled=%d\nMenuMusic=%d\nMenuPack=%s\n",
                    preset->name, preset->title, preset->track_loader, preset->title_enabled,
                    preset->msu_enabled, preset->menu_music, preset->menu_pack) > 0;
  for (unsigned i = 0; ok && i < FZERO_MENU_CUES; ++i)
    ok = fprintf(file, "MenuCue-%s=%s\n", FzeroMenuCues[i].id, preset->menu_cues[i]) > 0;
  ok = ok && FzeroVideoWriteSettings(&preset->video, file);
  if (fclose(file)) ok = false;
#ifdef _WIN32
  if (ok) ok = MoveFileExA(temporary, path, MOVEFILE_WRITE_THROUGH |
                          (replace ? MOVEFILE_REPLACE_EXISTING : 0)) != 0;
#else
  if (ok) ok = replace ? rename(temporary, path) == 0 : link(temporary, path) == 0;
#endif
  remove(temporary);
  return ok;
}

bool FzeroSavedPresetStore(const char *id, const char *name, const FzeroVideoSettings *video,
                           const RecompLauncherCSettings *launcher) {
  error[0] = 0;
  int slot = slot_for(id);
  if (id && slot < 0) return failed("Saved preset is unavailable");
  bool replace = slot >= 0;
  if (!replace && (!name || !video || !launcher)) return failed("Enter a preset name");
  if (name && !valid_name(name)) return failed("Enter a preset name without surrounding spaces or control characters");
  if (name) {
    if (same_name(name, "Vanilla") || same_name(name, "Satellaview") ||
        same_name(name, "Community Grand Prix") || same_name(name, "Custom"))
      return failed("That name is reserved for a built-in preset");
    for (int i = 0; i < MAX_SAVED_PRESETS; ++i)
      if (i != slot && *saved[i].id && same_name(name, saved[i].name))
        return failed("A preset already uses that name");
  }
  char path[CP_PATH];
  if (!replace) {
    /* Preserve malformed files too. Only a vacant file slot may receive a new preset. */
    for (slot = 0; slot < MAX_SAVED_PRESETS; ++slot) {
      if (*saved[slot].id || !path_for((unsigned)slot, path)) continue;
      FILE *existing = fopen(path, "rb");
      if (existing) fclose(existing);
      else if (errno == ENOENT) break;
    }
    if (slot == MAX_SAVED_PRESETS) return failed("The saved preset library is full or unavailable");
  }
  if (!path_for((unsigned)slot, path)) return failed("Preset settings path is unavailable");
  FzeroSavedPreset updated = saved[slot];
  if (name) strcpy(updated.name, name);
  if (video && launcher) capture(&updated, video, launcher);
  snprintf(updated.id, sizeof(updated.id), "saved-%02u", (unsigned)slot + 1);
  if (!write_preset(path, &updated, replace)) return failed("Could not write the saved preset");
  saved[slot] = updated;
  return true;
}

bool FzeroSavedPresetDelete(const char *id) {
  error[0] = 0;
  int slot = slot_for(id);
  char path[CP_PATH];
  if (slot < 0 || !path_for((unsigned)slot, path)) return failed("Saved preset is unavailable");
  if (remove(path) && errno != ENOENT) return failed("Could not delete the saved preset");
  memset(&saved[slot], 0, sizeof(saved[slot]));
  return true;
}
