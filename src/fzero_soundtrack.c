#include "fzero_soundtrack.h"
static const char *const ids[] = {
    "knight-1", "knight-2", "knight-3", "knight-4", "knight-5",
    "queen-1",  "queen-2",  "queen-3",  "queen-4",  "queen-5",
    "king-1",   "king-2",   "king-3",   "king-4",   "king-5",
    "bs-1-1",   "bs-1-2",   "bs-1-3",   "bs-1-4",   "bs-1-5",
    "bs-2-1",   "bs-2-2",   "bs-2-3",   "bs-2-4",   "bs-2-5"};
static const char *const names[] = {
    "Mute City I",  "Big Blue",      "Sand Ocean",    "Death Wind I",
    "Silence",      "Mute City II",  "Port Town I",   "Red Canyon I",
    "White Land I", "White Land II", "Mute City III", "Death Wind II",
    "Port Town II", "Red Canyon II", "Fire Field",    "Forest I",
    "Big Blue II",  "Sand Storm I",  "Forest II",     "Silence II",
    "Mute City IV", "Forest III",    "Sand Storm II", "Metal Fort I",
    "Metal Fort II"};
/* Conn/Cubear stock/Deluxe layout; the expanded set adds unique repeat songs.
 */
static const unsigned numbers[] = {14, 6,  15, 16, 10, 17, 13, 8,  11,
                                   12, 19, 20, 18, 21, 9,  22, 23, 24,
                                   25, 26, 27, 28, 29, 30, 31};
/* Try the venue's I recording, then this particular course's native theme.
 * Forest II/III and Metal Fort II do not inherit I's native fallback.
 * Reference: https://www.zeldix.net/t1447p175-f-zero */
static const unsigned fallbacks[] = {14, 6,  15, 16, 10, 14, 13, 8, 11,
                                     11, 14, 16, 13, 8,  9,  22, 6, 24,
                                     22, 10, 14, 22, 24, 30, 30};
static const unsigned native[] = {14, 6,  15, 16, 10, 14, 13, 8, 11,
                                  12, 14, 16, 13, 8,  9,  11, 6, 9,
                                  12, 10, 14, 15, 16, 8,  13};
const char *FzeroSoundtrackCourseId(unsigned n) {
  return n < 25 ? ids[n] : NULL;
}
const char *FzeroSoundtrackCourseName(unsigned n) {
  return n < 25 ? names[n] : NULL;
}
unsigned FzeroSoundtrackNumber(unsigned n) { return n < 25 ? numbers[n] : 0; }
unsigned FzeroSoundtrackFallback(unsigned n) {
  return n < 25 ? fallbacks[n] : 0;
}
unsigned FzeroSoundtrackNative(unsigned n) { return n < 25 ? native[n] : 0; }
