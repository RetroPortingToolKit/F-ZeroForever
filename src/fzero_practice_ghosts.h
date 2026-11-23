#pragma once

#include "fzero_course.h"
#include "fzero_gameplay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FzeroGhostLibrary FzeroGhostLibrary;

/* These are the world-space fields used by the ship projector. A ghost never
 * becomes a collision or traffic actor and does not need a CPU or SRAM slot. */
typedef struct FzeroGhostPose {
  uint16_t x, y, height, facing;
} FzeroGhostPose;

typedef struct FzeroGhostRun {
  unsigned id; /* Stable within the course/rules library. */
  unsigned ticks, best_lap_ticks;
  unsigned machine;
} FzeroGhostRun;

typedef enum FzeroGhostResult {
  FZERO_GHOST_OK,
  FZERO_GHOST_IO_ERROR,
  FZERO_GHOST_INVALID,
  FZERO_GHOST_FULL,
} FzeroGhostResult;

/* course_id is the stable catalog identity, including the built-in course
 * identity for entries whose course data comes from the verified retail ROM.
 * sim_version identifies the producer's physics and race timing contract. */
void FzeroGhostCompatibilityKey(uint8_t key[32], const char *course_id,
                                const FzeroCourse *course,
                                const FzeroGameplaySettings *settings,
                                unsigned sim_version, unsigned race_class);

/* root is the profile save root before records route it through a rule or
 * vehicle namespace. Read-only operations are safe on an absent directory. */
FzeroGhostLibrary *FzeroGhostOpen(const char *root, const uint8_t key[32]);
void FzeroGhostClose(FzeroGhostLibrary *library);
unsigned FzeroGhostCount(const FzeroGhostLibrary *library);
bool FzeroGhostGet(const FzeroGhostLibrary *library, unsigned index, FzeroGhostRun *run);
bool FzeroGhostSelect(FzeroGhostLibrary *library, unsigned index);
bool FzeroGhostPoseAt(const FzeroGhostLibrary *library, unsigned tick, FzeroGhostPose *pose);

void FzeroGhostBeginRun(FzeroGhostLibrary *library, unsigned machine);
void FzeroGhostRecord(FzeroGhostLibrary *library, FzeroGhostPose pose);
FzeroGhostResult FzeroGhostSaveRun(FzeroGhostLibrary *library, unsigned best_lap_ticks);

#ifdef __cplusplus
}
#endif
