#ifdef _WIN32
#include <windows.h>
#undef FORCEINLINE
#endif
#include "fzero_course_runtime.h"
#include "fzero_tracks.h"
#include "fzero_deluxe.h"
#include "fzero_records.h"
#include "common_rtl.h"
#include "sha256.h"
#include "snes/saveload.h"
#include <stdio.h>
#include <string.h>

/* Guest records still use their native slots. A cup has a stable private
 * namespace, and the original cartridge's records remain the base context.
 * The backup is part of every library snapshot and rewind frame. */
typedef struct CourseSaveState {
  uint8_t external, backup_valid, read_only, view;
  union { uint8_t key[32]; FzeroRecordsView browser; };
  uint8_t base_sram[0x8000], base_records[0x200];
  uint8_t record_vehicles[15][11];
  uint8_t ghost_borrowed, original_ghost[0x8000 - 0x3a0];
} CourseSaveState;
static CourseSaveState state;
_Static_assert(sizeof(FzeroRecordsView) <= 32, "Records view must fit the snapshot key");
static char base_root[96];
const char *FzeroTracksSaveBaseRoot(void) { return base_root; }
static bool set_root(const uint8_t *key) {
  if (!key) {
    RtlSetSaveRoot(base_root);
    return true;
  }
  char hex[65], root[96];
  cp_hash_format(key, hex);
  /* 128-bit path component; the full digest is checked in records.bin. */
  if (snprintf(root, sizeof(root), "%s/courses/%.32s", base_root, hex) >= (int)sizeof(root))
    return false;
  char parent[96];
  snprintf(parent, sizeof(parent), "%s/courses", base_root);
  /* A first run with only a track pack has no Deluxe/rules directory setup. */
  RtlSetSaveRoot(base_root);
  RtlEnsureSaveDir();
  RtlSetSaveRoot(parent);
  RtlEnsureSaveDir();
  RtlSetSaveRoot(root);
  RtlEnsureSaveDir();
  return true;
}
void FzeroTracksSavesInit(void) {
  memset(&state, 0, sizeof(state));
  snprintf(base_root, sizeof(base_root), "%s", RtlSaveRoot());
  /* The quick slot must remain reachable after returning to the title or
   * changing courses. Course/vehicle battery records still switch roots. */
  RtlSetQuickSaveRoot(base_root);
}
void FzeroTracksFlush(void) {
  if (!state.external || state.read_only)
    return;
  char path[160], temp[164];
  snprintf(path, sizeof(path), "%s/records.bin", RtlSaveRoot());
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  FILE *f = fopen(temp, "wb");
  if (!f) {
    FzeroTracksReport("Cannot save imported cup records");
    return;
  }
  bool ok = fwrite(state.key, 1, 32, f) == 32;
  if (state.ghost_borrowed) {
    ok = ok && fwrite(g_sram, 1, 0x3a0, f) == 0x3a0 &&
        fwrite(state.original_ghost, 1, g_sram_size - 0x3a0, f) == (size_t)g_sram_size - 0x3a0;
  } else ok = ok && fwrite(g_sram, 1, g_sram_size, f) == (size_t)g_sram_size;
  ok = ok && fwrite(g_ram + 0x14800, 1, 0x200, f) == 0x200;
  if (fclose(f))
    ok = false;
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (ok)
    ok = rename(temp, path) == 0;
#endif
  if (!ok)
    FzeroTracksReport("Could not finish saving imported cup records");
}
/* Canonical record reset: 55 BCD times (9:59.99) and their 16-bit
 * byte-sum checksum per five-course league. Verified against the stock
 * $02c541 and Deluxe $1eb7bc initialization routines. Keep unlock flags. */
static void clear_league(unsigned start, unsigned times) {
  unsigned sum = 0;
  static const uint8_t empty_time[] = {0x09, 0x59, 0x99};
  for (unsigned i = 0; i < times * 3; ++i) {
    uint8_t value = empty_time[i % 3];
    g_sram[start + i] = value;
    sum += value;
  }
  g_sram[start + times * 3] = (uint8_t)sum;
  g_sram[start + times * 3 + 1] = (uint8_t)(sum >> 8);
}
static void fresh_records(void) {
  clear_league(0x005, 55);
  clear_league(0x0ac, 55);
  clear_league(0x153, 55);
  if (FzeroDeluxeActive() && g_sram_size >= 0x400) {
    clear_league(0x205, 55);
    clear_league(0x2ac, 55);
    clear_league(0x353, 22);
  }
  memcpy(g_ram + 0x14800, g_sram, 0x200);
}
bool FzeroTracksRecordsSelect(const uint8_t *key) {
  FzeroRecordsViewEnd();
  if ((!key && !state.external) || (key && state.external && !memcmp(key, state.key, 32)))
    return true;
  if (g_sram_size < 0x200 || g_sram_size > 0x8000)
    return false;
  FzeroTracksFlush();
  state.ghost_borrowed = 0;
  if (state.external && state.backup_valid) {
    memcpy(g_sram, state.base_sram, g_sram_size);
    memcpy(g_ram + 0x14800, state.base_records, 0x200);
  }
  state.external = 0;
  state.read_only = 0;
  if (!key) {
    set_root(NULL);
    return true;
  }
  memcpy(state.base_sram, g_sram, g_sram_size);
  memcpy(state.base_records, g_ram + 0x14800, 0x200);
  state.backup_valid = 1;
  if (!set_root(key)) {
    set_root(NULL);
    FzeroTracksReport("Save root too long for imported cup records");
    return false;
  }
  memcpy(state.key, key, 32);
  state.external = 1;
  fresh_records();
  char path[160];
  snprintf(path, sizeof(path), "%s/records.bin", RtlSaveRoot());
  FILE *f = fopen(path, "rb");
  if (f) {
    uint8_t hash[32], sram[0x8000], records[0x200];
    bool ok = fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32) &&
              fread(sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
              fread(records, 1, 0x200, f) == 0x200 && fgetc(f) == EOF;
    fclose(f);
    if (ok) {
      memcpy(g_sram, sram, g_sram_size);
      memcpy(g_ram + 0x14800, records, 0x200);
    } else {
      state.read_only = 1;
      FzeroTracksReport(
          "Imported cup records failed validation; this session will preserve that file");
    }
  }
  return true;
}
FzeroRecordsView *FzeroRecordsViewState(void) {
  return state.view ? &state.browser : NULL;
}
bool FzeroRecordsViewBegin(void) {
  if (state.view) return true;
  if (!FzeroTracksRecordsSelect(NULL)) return false;
  memcpy(state.base_sram, g_sram, g_sram_size);
  memcpy(state.base_records, g_ram + 0x14800, 0x200);
  state.backup_valid = state.view = 1;
  memset(state.key, 0, sizeof(state.key));
  return true;
}
void FzeroRecordsViewEnd(void) {
  if (!state.view) return;
  memcpy(g_sram, state.base_sram, g_sram_size);
  memcpy(g_ram + 0x14800, state.base_records, 0x200);
  state.view = 0;
}
/* Reading a context never changes the save root, creates directories, or
 * installs it as writable SRAM. Missing/bad files yield an empty display. */
bool FzeroRecordsRead(const uint8_t *key, uint8_t records[0x400]) {
  memset(records, 0, 0x400);
  const unsigned starts[] = {5, 0xac, 0x153, 0x205, 0x2ac, 0x353};
  for (unsigned league = 0; league < 6; ++league)
    for (unsigned i = 0; i < (league == 5 ? 22 : 55); ++i)
      memcpy(records + starts[league] + i * 3, "\x09\x59\x99", 3);
  if (!key) {
    memcpy(records, (state.view || state.external) ? state.base_sram : g_sram, 0x400);
    return true;
  }
  char hex[65], path[160];
  cp_hash_format(key, hex);
  snprintf(path, sizeof(path), "%s/courses/%.32s/records.bin", base_root, hex);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint8_t hash[32], sram[0x8000], mirror[0x200];
  bool ok = fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32) &&
      fread(sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
      fread(mirror, 1, sizeof(mirror), f) == sizeof(mirror) && fgetc(f) == EOF;
  fclose(f);
  if (ok) memcpy(records, sram, 0x400);
  else FzeroTracksReport("Records file failed validation; displaying empty records without modifying it");
  return ok;
}
uint8_t *FzeroRecordsRowVehicles(unsigned slot) {
  return state.view && slot < 15 ? state.record_vehicles[slot] : NULL;
}
bool FzeroRecordsImportGhost(const uint8_t key[32], unsigned course) {
  if (!key || !state.external || g_sram_size < 0x400 || g_sram_size > 0x8000 || course > 255) return false;
  char hex[65], path[160];
  cp_hash_format(key, hex);
  snprintf(path, sizeof(path), "%s/courses/%.32s/records.bin", base_root, hex);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint8_t hash[32], sram[0x8000], mirror[0x200];
  bool ok = fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32) &&
      fread(sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
      fread(mirror, 1, sizeof(mirror), f) == sizeof(mirror) && fgetc(f) == EOF;
  fclose(f);
  if (!ok || sram[0x3a0] != course) return false;
  unsigned start = sram[0x3a2] | (unsigned)sram[0x3a3] << 8;
  unsigned length = sram[0x3a4] | (unsigned)sram[0x3a5] << 8;
  unsigned checksum = sram[0x3a6] | (unsigned)sram[0x3a7] << 8, sum = 0;
  if (start < 0x400 || !length || start > (unsigned)g_sram_size ||
      length > (unsigned)g_sram_size - start) return false;
  for (unsigned i = 0; i < length; ++i) sum += sram[start + i];
  if ((sum & 65535) != checksum) return false;
  /* Leave this driver's ranked times and unlocks intact. The native ghost
   * header carries the recording driver's stable identity for its artwork. */
  if (!state.ghost_borrowed)
    memcpy(state.original_ghost, g_sram + 0x3a0, (size_t)g_sram_size - 0x3a0);
  state.ghost_borrowed = 1;
  memcpy(g_sram + 0x3a0, sram + 0x3a0, (size_t)g_sram_size - 0x3a0);
  fprintf(stderr, "[records-ghost] imported course=%u vehicle=%u\n", course, sram[0x3aa]);
  return true;
}
void FzeroRecordsGhostWritten(void) { state.ghost_borrowed = 0; }
void FzeroRecordsGhostUnavailable(void) {
  if (!state.external || g_sram_size < 0x400 || g_sram_size > 0x8000) return;
  if (!state.ghost_borrowed)
    memcpy(state.original_ghost, g_sram + 0x3a0, (size_t)g_sram_size - 0x3a0);
  state.ghost_borrowed = 1;
  g_sram[0x3a0] = 255;
}
static unsigned time_value(const uint8_t *time) {
  return ((unsigned)(time[0] & 15) << 16) | ((unsigned)time[1] << 8) | time[2];
}
void FzeroRecordsMergeCup(uint8_t records[0x400], const uint8_t previous[0x400], unsigned cup) {
  static const unsigned starts[] = {5, 0xac, 0x153, 0x205, 0x2ac};
  if (cup >= 5) return;
  unsigned start = starts[cup];
  for (unsigned course = 0; course < 5; ++course) {
    uint8_t *dest = records + start + course * 33;
    const uint8_t *old = previous + start + course * 33;
    for (unsigned row = 0; row < 10; ++row) {
      const uint8_t *candidate = old + row * 3;
      if ((candidate[0] & 15) >= 9) continue;
      bool duplicate = false;
      for (unsigned i = 0; i < 10; ++i)
        if (!memcmp(dest + i * 3, candidate, 3)) duplicate = true;
      if (duplicate) continue;
      for (unsigned i = 0; i < 10; ++i) {
        if (time_value(candidate) < time_value(dest + i * 3)) {
          memmove(dest + (i + 1) * 3, dest + i * 3, (9 - i) * 3);
          memcpy(dest + i * 3, candidate, 3);
          break;
        }
      }
    }
    if (time_value(old + 30) < time_value(dest + 30)) memcpy(dest + 30, old + 30, 3);
  }
  unsigned sum = 0;
  for (unsigned i = 0; i < 165; ++i) sum += records[start + i];
  records[start + 165] = (uint8_t)sum;
  records[start + 166] = (uint8_t)(sum >> 8);
}
/* Twin program records. Practice ghosts alone run otherwise stock content on
 * the Deluxe program, whose 32 KiB save begins with the stock cartridge's
 * 512-byte record block unchanged ("FZERO", three leagues, class unlocks).
 * Each program reads and writes only its own files. It reads its twin's block
 * and folds those times into what it shows and saves, so changing the ghost
 * setting never hides a record. A twin file is never created or modified. */
static struct RecordTwin {
  char root[96];
  unsigned size;
} twins[3];
static unsigned twin_count;
static bool twin_base_merged;
void FzeroRecordsAddTwin(const char *root, unsigned sram_size) {
  if (!root || twin_count == sizeof(twins) / sizeof(*twins) ||
      strlen(root) >= sizeof(twins[0].root)) return;
  struct RecordTwin *twin = &twins[twin_count++];
  snprintf(twin->root, sizeof(twin->root), "%s", root);
  twin->size = sram_size;
}
void FzeroRecordsSetTwin(const char *root, unsigned sram_size) {
  twin_count = 0;
  twin_base_merged = false;
  FzeroRecordsAddTwin(root, sram_size);
}
static const unsigned league_start[] = {5, 0xac, 0x153};
static bool league_valid(const uint8_t *records, unsigned cup) {
  unsigned start = league_start[cup], sum = 0;
  for (unsigned i = 0; i < 165; ++i) sum += records[start + i];
  return records[start + 165] == (uint8_t)sum && records[start + 166] == (uint8_t)(sum >> 8);
}
static bool twin_read(const struct RecordTwin *twin, const uint8_t *key, uint8_t block[0x400]) {
  char hex[65], path[160];
  long expected = (long)twin->size;
  if (key) {
    cp_hash_format(key, hex);
    snprintf(path, sizeof(path), "%s/courses/%.32s/records.bin", twin->root, hex);
    expected += 32 + 0x200;
  } else snprintf(path, sizeof(path), "%s/save.srm", twin->root);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint8_t hash[32];
  memset(block, 0, 0x400);
  bool ok = (!key || (fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32))) &&
            fread(block, 1, 0x200, f) == 0x200 && !fseek(f, 0, SEEK_END) && ftell(f) == expected;
  fclose(f);
  /* A base save the game has not initialized carries no records. */
  return ok && (key || !memcmp(block, "FZERO", 5));
}
static bool merge_twin(const struct RecordTwin *twin, const uint8_t *key, uint8_t records[0x400]) {
  uint8_t other[0x400];
  if (!twin_read(twin, key, other)) return false;
  bool changed = false;
  for (unsigned cup = 0; cup < 3; ++cup) {
    if (!league_valid(other, cup) || !league_valid(records, cup)) continue;
    uint8_t before[167];
    memcpy(before, records + league_start[cup], sizeof(before));
    FzeroRecordsMergeCup(records, other, cup);
    changed |= memcmp(before, records + league_start[cup], sizeof(before)) != 0;
  }
  /* $70:01FA holds the unlocked classes in both nibbles; $02:C569 clears a
   * byte whose nibbles differ or use bit 3. Unlocks earned in either program
   * count in both. Course-keyed files inherit this byte from the base save. */
  uint8_t own = records[0x1fa], theirs = other[0x1fa];
  if (!key && own >> 4 == (own & 15) && theirs >> 4 == (theirs & 15) && !((own | theirs) & 8)) {
    uint8_t merged = (own | theirs) & 7;
    records[0x1fa] = (uint8_t)(merged | merged << 4);
    changed |= records[0x1fa] != own;
  }
  return changed;
}
bool FzeroRecordsMergeTwin(const uint8_t *key, uint8_t records[0x400]) {
  bool changed = false;
  for (unsigned i = 0; i < twin_count; ++i) {
    if (!merge_twin(&twins[i], key, records)) continue;
    changed = true;
    if (!key) fprintf(stderr, "[records] merged times from %s\n", twins[i].root);
  }
  return changed;
}
void FzeroRecordsMergeTwinBase(void) {
  /* Once per session, after the game has validated or initialized its save
   * and while the base context is the live one. */
  if (twin_base_merged || !twin_count || state.external || state.view || g_sram_size < 0x200 ||
      memcmp(g_sram, "FZERO", 5))
    return;
  twin_base_merged = true;
  uint8_t records[0x400] = {0};
  memcpy(records, g_sram, 0x200);
  if (!FzeroRecordsMergeTwin(NULL, records)) return;
  memcpy(g_sram, records, 0x200);
  memcpy(g_ram + 0x14800, g_sram, 0x200);
}
size_t FzeroTracksSaveStateSize(void) {
  return sizeof(state);
}
void FzeroTracksSaveState(SaveLoadInfo *sli, bool load) {
  sli->func(sli, &state, sizeof(state));
  if (load)
    set_root(state.external ? state.key : NULL);
}
void FzeroTracksSavesFinish(void) {
  FzeroTracksRecordsSelect(NULL);
}
