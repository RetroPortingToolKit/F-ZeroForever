#pragma once

#include "fzero_practice_ghosts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FzeroLegacyGhost {
  const uint8_t *stream;
  unsigned length, samples, course, machine;
} FzeroLegacyGhost;

/* Read a saved Deluxe stream from the original SRAM or the SRAM portion of a
 * per-car records file. Caller identifies the course and the source roster. */
bool FzeroLegacyGhostRead(const uint8_t *sram, size_t size, FzeroLegacyGhost *ghost);

/* Legacy coordinates start at the course's No Rival grid position. The codec
 * advances every second tick and uses the previous pose on intervening ticks. */
bool FzeroLegacyGhostDecode(const FzeroLegacyGhost *ghost, FzeroGhostPose grid,
                            FzeroGhostPose *poses, size_t capacity);

#ifdef __cplusplus
}
#endif
