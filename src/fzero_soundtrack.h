#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native course identities, shared by retail and BS Deluxe. */
const char *FzeroSoundtrackCourseId(unsigned course);
const char *FzeroSoundtrackCourseName(unsigned course);
unsigned FzeroSoundtrackNumber(unsigned course);
unsigned FzeroSoundtrackFallback(unsigned course);
unsigned FzeroSoundtrackNative(unsigned course);
#ifdef __cplusplus
}
#endif
