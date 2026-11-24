#ifdef _WIN32
#include <windows.h>
#undef FORCEINLINE
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif
#include "fzero_course_runtime.h"
#include "fzero_records.h"
#include "common_rtl.h"
#include "snes/saveload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                   \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
uint8 g_ram[0x20000];
static uint8 sram[0x8000];
uint8 *g_sram = sram;
int g_sram_size = 0x800;
static char root[96] = "test-course-records";
static char quick_root[96];
const char *RtlSaveRoot(void) {
  return root;
}
void RtlSetSaveRoot(const char *s) {
  snprintf(root, sizeof(root), "%s", s);
}
void RtlSetQuickSaveRoot(const char *s) {
  snprintf(quick_root, sizeof(quick_root), "%s", s);
}
void RtlEnsureSaveDir(void) {
  MKDIR(root);
}
bool FzeroDeluxeActive(void) {
  return true;
}
void FzeroTracksReport(const char *s) {
  fprintf(stderr, "%s\n", s);
}
/* Fill an untouched league with empty times, then write its checksum. */
static void seal(uint8_t *save, unsigned cup) {
  unsigned start = cup == 0 ? 5 : cup == 1 ? 0xac : 0x153, sum = 0;
  for (unsigned i = 0; i < 55; ++i)
    if (!memcmp(save + start + i * 3, "\0\0\0", 3)) memcpy(save + start + i * 3, "\x09\x59\x99", 3);
  for (unsigned i = 0; i < 165; ++i) sum += save[start + i];
  save[start + 165] = (uint8_t)sum;
  save[start + 166] = (uint8_t)(sum >> 8);
}
static void write_file(const char *path, const uint8_t *key, const uint8_t *save, size_t size) {
  FILE *f = fopen(path, "wb");
  CHECK(f && (!key || fwrite(key, 1, 32, f) == 32) && fwrite(save, 1, size, f) == size);
  if (key) CHECK(fwrite(save, 1, 0x200, f) == 0x200);
  CHECK(!fclose(f));
}
int main(void) {
  uint8_t unified[0x400], previous[0x400], merged[0x400];
  for (unsigned i = 0; i < sizeof(unified); ++i) unified[i] = previous[i] = 0;
  for (unsigned i = 0; i < 55; ++i) {
    memcpy(unified + 5 + i * 3, "\x09\x59\x99", 3);
    memcpy(previous + 5 + i * 3, "\x09\x59\x99", 3);
  }
  memcpy(unified + 5, "\x81\x30\x32", 3);
  memcpy(previous + 5, "\x81\x20\x15", 3);
  memcpy(previous + 8, "\x81\x30\x32", 3); /* Existing time is not duplicated. */
  memcpy(unified + 35, "\x80\x20\x52", 3);
  memcpy(previous + 35, "\x80\x19\x32", 3);
  FzeroRecordsMergeCup(unified, previous, 0);
  CHECK(!memcmp(unified + 5, "\x81\x20\x15\x81\x30\x32\x09\x59\x99", 9));
  CHECK(!memcmp(unified + 35, "\x80\x19\x32", 3));
  CHECK(!memcmp(unified + 0xac, previous + 0xac, 0x400 - 0xac));
  unsigned checksum = 0;
  for (unsigned i = 5; i < 0xaa; ++i) checksum += unified[i];
  CHECK(unified[0xaa] == (uint8_t)checksum && unified[0xab] == checksum >> 8);
  memcpy(merged, unified, sizeof(merged));
  FzeroRecordsMergeCup(unified, previous, 0);
  CHECK(!memcmp(merged, unified, sizeof(merged)));
  uint8_t a[32] = {1}, b[32] = {2}, original[0x800], mirror[0x200];
  MKDIR(root);
  FzeroTracksSavesInit();
  CHECK(!strcmp(quick_root, "test-course-records"));
  memset(sram, 0x42, sizeof(sram));
  memset(g_ram + 0x14800, 0x73, 0x200);
  memcpy(original, sram, sizeof(original));
  memcpy(mirror, g_ram + 0x14800, sizeof(mirror));
  remove("test-course-records/courses/01000000000000000000000000000000/records.bin");
  remove("test-course-records/courses/02000000000000000000000000000000/records.bin");
  CHECK(FzeroTracksRecordsSelect(a));
  CHECK(!memcmp(sram + 5, "\x09\x59\x99", 3) && sram[0xaa] == 0xed && sram[0xab] == 0x35);
  CHECK(sram[0x1fa] == 0x42);
  sram[5] = 0x81;
  g_ram[0x14805] = 0x81;
  CHECK(FzeroTracksRecordsSelect(b) && sram[5] == 9);
  sram[5] = 0x82;
  g_ram[0x14805] = 0x82;
  CHECK(FzeroTracksRecordsSelect(a) && sram[5] == 0x81 && g_ram[0x14805] == 0x81);
  CHECK(!strcmp(quick_root, "test-course-records"));
  FzeroTracksSavesFinish();
  CHECK(!memcmp(original, sram, sizeof(original)) &&
        !memcmp(mirror, g_ram + 0x14800, sizeof(mirror)));
  CHECK(!strcmp(root, "test-course-records"));
  CHECK(FzeroTracksRecordsSelect(b) && sram[5] == 0x82);
  uint8_t readback[0x400], missing[32] = {3};
  CHECK(FzeroRecordsViewBegin() && FzeroRecordsViewState());
  CHECK(!strcmp(root, "test-course-records"));
  CHECK(FzeroRecordsRead(a, readback) && readback[5] == 0x81);
  CHECK(FzeroRecordsRead(b, readback) && readback[5] == 0x82);
  CHECK(FzeroRecordsRead(NULL, readback) && !memcmp(readback, original, sizeof(readback)));
  CHECK(!FzeroRecordsRead(missing, readback) && readback[5] == 9);
  memset(sram, 0xee, sizeof(sram)); /* Synthetic three-cup view is disposable. */
  memset(g_ram + 0x14800, 0xdd, 0x200);
  FzeroTracksFlush();
  CHECK(FzeroTracksRecordsSelect(a) && !FzeroRecordsViewState() && sram[5] == 0x81);
  CHECK(FzeroRecordsViewBegin());
  FzeroTracksSavesFinish();
  CHECK(!memcmp(original, sram, sizeof(original)) && !memcmp(mirror, g_ram + 0x14800, sizeof(mirror)));
  /* A borrowed ghost is transient even if a flush or course switch occurs.
   * Only an actual native Save Ghost commit releases the owned backup. */
  CHECK(FzeroTracksRecordsSelect(b));
  const uint8_t ghost_header[] = {4, 0, 0, 4, 4, 0, 10, 0};
  memcpy(sram + 0x3a0, ghost_header, sizeof(ghost_header));
  memcpy(sram + 0x400, "\x01\x02\x03\x04", 4);
  sram[0x3aa] = 0x85;
  FzeroTracksFlush();
  CHECK(FzeroTracksRecordsSelect(a));
  uint8_t owned[0x800 - 0x3a0];
  memcpy(owned, sram + 0x3a0, sizeof(owned));
  CHECK(!FzeroRecordsImportGhost(b, 3)); /* A different course cannot play. */
  CHECK(!memcmp(owned, sram + 0x3a0, sizeof(owned)));
  CHECK(FzeroRecordsImportGhost(b, 4) && sram[0x3aa] == 0x85);
  sram[5] = 0x83;
  FzeroTracksFlush();
  CHECK(FzeroTracksRecordsSelect(b));
  CHECK(FzeroTracksRecordsSelect(a) && sram[5] == 0x83);
  CHECK(!memcmp(owned, sram + 0x3a0, sizeof(owned)));
  FzeroRecordsGhostUnavailable();
  CHECK(sram[0x3a0] == 255);
  CHECK(FzeroTracksRecordsSelect(b));
  CHECK(FzeroTracksRecordsSelect(a));
  CHECK(!memcmp(owned, sram + 0x3a0, sizeof(owned)));
  CHECK(FzeroRecordsImportGhost(b, 4));
  sram[0x3aa] = 0x80;
  FzeroRecordsGhostWritten();
  CHECK(FzeroTracksRecordsSelect(b));
  CHECK(FzeroTracksRecordsSelect(a) && sram[0x3aa] == 0x80);
  CHECK(FzeroTracksRecordsSelect(b));
  sram[0x400] ^= 1; /* Reject a damaged stream without touching either save. */
  FzeroTracksFlush();
  CHECK(FzeroTracksRecordsSelect(a));
  CHECK(!FzeroRecordsImportGhost(b, 4) && sram[0x3aa] == 0x80);
  FzeroTracksSavesFinish();
  CHECK(!memcmp(original, sram, sizeof(original)) && !memcmp(mirror, g_ram + 0x14800, sizeof(mirror)));
  const char *path = "test-course-records/courses/01000000000000000000000000000000/records.bin";
  FILE *f = fopen(path, "wb");
  CHECK(f);
  fputs("damaged", f);
  fclose(f);
  CHECK(FzeroTracksRecordsSelect(a) && sram[5] == 9);
  CHECK(FzeroRecordsViewBegin() && !FzeroRecordsRead(a, readback) && readback[5] == 9);
  FzeroTracksSavesFinish();
  f = fopen(path, "rb");
  CHECK(f);
  char saved[8] = {0};
  CHECK(fread(saved, 1, 8, f) == 7);
  fclose(f);
  CHECK(!strcmp(saved, "damaged") && !memcmp(original, sram, sizeof(original)));

  /* Twin program: the same content on the other program keeps its own files.
   * This side reads the twin's first 512 bytes and never writes them. */
  static uint8_t twin[0x8000];
  for (unsigned side = 0; side < 2; ++side) {
    uint8_t *save = side ? twin : sram;
    memset(save, 0, side ? sizeof(twin) : sizeof(sram));
    memcpy(save, "FZERO", 5);
    for (unsigned cup = 0; cup < 3; ++cup) seal(save, cup);
  }
  memcpy(sram + 5, "\x81\x30\x32", 3);
  seal(sram, 0);
  sram[0x1fa] = 0x11;
  memcpy(twin + 5, "\x81\x20\x15", 3);          /* Better time on the first course. */
  memcpy(twin + 0xac + 33, "\xc1\x41\x27", 3); /* A league this side has not raced. */
  memcpy(twin + 0x153, "\x80\x10\x00", 3);     /* Damaged league: checksum not updated. */
  seal(twin, 0);
  seal(twin, 1);
  twin[0x1fa] = 0x22;
  memcpy(twin + 0x3a0, ghost_header, sizeof(ghost_header));
  MKDIR("test-course-records/twin");
  write_file("test-course-records/twin/save.srm", NULL, twin, sizeof(twin));
  memset(g_ram + 0x14800, 0, 0x200);
  uint8_t alone[0x800];
  memcpy(alone, sram, sizeof(alone));
  FzeroRecordsMergeTwinBase(); /* No twin configured. */
  CHECK(!memcmp(alone, sram, sizeof(alone)));
  FzeroRecordsSetTwin("test-course-records/twin", 0x800); /* Not the twin's save size. */
  FzeroRecordsMergeTwinBase();
  CHECK(!memcmp(alone, sram, sizeof(alone)));
  FzeroRecordsSetTwin("test-course-records/twin", sizeof(twin));
  FzeroRecordsMergeTwinBase();
  CHECK(!memcmp(sram + 5, "\x81\x20\x15\x81\x30\x32\x09\x59\x99", 9));
  CHECK(!memcmp(sram + 0xac + 33, "\xc1\x41\x27", 3) && !memcmp(sram + 0x153, "\x09\x59\x99", 3));
  CHECK(sram[0x1fa] == 0x33 && !memcmp(g_ram + 0x14800, sram, 0x200));
  CHECK(sram[0x3a0] == 0); /* Only the shared record block is read. */
  for (unsigned cup = 0; cup < 3; ++cup) {
    unsigned start = cup == 0 ? 5 : cup == 1 ? 0xac : 0x153, sum = 0;
    for (unsigned i = 0; i < 165; ++i) sum += sram[start + i];
    CHECK(sram[start + 165] == (uint8_t)sum && sram[start + 166] == sum >> 8);
  }
  memcpy(twin + 5, "\x81\x10\x00", 3);
  seal(twin, 0);
  write_file("test-course-records/twin/save.srm", NULL, twin, sizeof(twin));
  FzeroRecordsMergeTwinBase(); /* Once per session; the live save is not re-read mid-game. */
  CHECK(!memcmp(sram + 5, "\x81\x20\x15", 3));
  uint8_t view[0x400] = {0}, check[0x8000];
  memcpy(view, sram, 0x200);
  CHECK(FzeroRecordsMergeTwin(NULL, view) && !memcmp(view + 5, "\x81\x10\x00\x81\x20\x15", 6));
  CHECK(!FzeroRecordsMergeTwin(NULL, view)); /* Idempotent. */
  f = fopen("test-course-records/twin/save.srm", "rb");
  CHECK(f && fread(check, 1, sizeof(check), f) == sizeof(check) && !memcmp(check, twin, sizeof(twin)));
  fclose(f);
  /* Course-keyed records use the same block behind the file's key. */
  MKDIR("test-course-records/twin/courses");
  MKDIR("test-course-records/twin/courses/02000000000000000000000000000000");
  const char *keyed = "test-course-records/twin/courses/02000000000000000000000000000000/records.bin";
  write_file(keyed, b, twin, sizeof(twin));
  memset(view, 0, sizeof(view));
  memcpy(view, alone, 0x200);
  CHECK(FzeroRecordsMergeTwin(b, view) && !memcmp(view + 5, "\x81\x10\x00\x81\x30\x32", 6));
  CHECK(view[0x1fa] == 0x11); /* Class unlocks belong to the base save. */
  CHECK(!FzeroRecordsMergeTwin(a, view)); /* No such twin file. */
  write_file(keyed, a, twin, sizeof(twin));
  memcpy(view, alone, 0x200);
  CHECK(!FzeroRecordsMergeTwin(b, view) && !memcmp(view, alone, 0x200)); /* Another cup's key. */
  FzeroRecordsSetTwin(NULL, 0);
  puts("Independent cup records, clean defaults, base restoration, corrupt-file preservation "
       "and twin-program record merging passed");
  return 0;
}
