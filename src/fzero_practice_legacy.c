#include "fzero_practice_legacy.h"

static unsigned word(const uint8_t *p) {
  return p[0] | (unsigned)p[1] << 8;
}

bool FzeroLegacyGhostRead(const uint8_t *sram, size_t size, FzeroLegacyGhost *ghost) {
  enum { HEADER = 0x3a0, FIRST_STREAM_BYTE = 0x400, SRAM_CAPACITY = 0x8000 };
  if (!sram || !ghost || size < FIRST_STREAM_BYTE || size > SRAM_CAPACITY) return false;
  unsigned course = word(sram + HEADER);
  unsigned start = word(sram + HEADER + 2);
  unsigned length = word(sram + HEADER + 4);
  unsigned checksum = word(sram + HEADER + 6);
  unsigned samples = word(sram + HEADER + 8);
  unsigned machine = sram[HEADER + 10];
  /* Deluxe stored a course slot in 0..38. A host-specific CGP ID uses bit 7
   * in the machine byte, with the underlying stable ID in 0..11. */
  if (course >= 39 || start < FIRST_STREAM_BYTE || start >= size ||
      !length || length > size - start || !samples ||
      (machine >= 8 && (machine < 0x80 || machine >= 0x8c))) return false;
  unsigned sum = 0;
  for (unsigned i = 0; i < length; ++i) sum += sram[start + i];
  if ((sum & 0xffff) != checksum) return false;
  *ghost = (FzeroLegacyGhost){sram + start, length, samples, course, machine};
  return true;
}

bool FzeroLegacyGhostDecode(const FzeroLegacyGhost *ghost, FzeroGhostPose grid,
                            FzeroGhostPose *poses, size_t capacity) {
  if (!ghost || !ghost->stream || !ghost->samples || !poses ||
      ghost->samples > capacity / 2 || grid.x >= 8192 || grid.y >= 4096)
    return false;
  int delta[4] = {0};
  unsigned repeat[4] = {0}, used = 0;
  FzeroGhostPose pose = {.x = grid.x, .y = grid.y, .height = 0x0400, .facing = 0x3000};
  for (unsigned i = 0; i < ghost->samples; ++i) {
    poses[2 * i] = pose;
    for (unsigned channel = 0; channel < 4; ++channel) {
      if (repeat[channel]) {
        --repeat[channel];
      } else {
        if (used == ghost->length) return false;
        unsigned next = ghost->stream[used++];
        if (next & 128) {
          repeat[channel] = next & 127;
        } else {
          delta[channel] = (int)next - ((next & 64) ? 128 : 0);
        }
      }
    }
    pose.x = (uint16_t)(pose.x + delta[0]) & 8191;
    pose.y = (uint16_t)(pose.y + delta[1]) & 4095;
    pose.facing = (uint16_t)(((pose.facing >> 8) + delta[2] + 192) % 192 << 8);
    pose.height = (uint16_t)((uint8_t)((pose.height >> 8) + delta[3]) << 8);
    poses[2 * i + 1] = pose;
  }
  return true;
}
