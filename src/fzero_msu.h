#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The former v11 layout uses the PCM files' numeric mapping. */
bool FzeroMsuHasLegacyPatch(const char *pack);
bool FzeroMsuHasInstalledMusic(void);
bool FzeroMsuConfigure(const char *pack, bool cgp, const char *rom_path);
bool FzeroMsuConfigureVersus(const char *pack, bool cgp, const char *rom_path);
/* Once per presented simulation frame, outside rollback resimulation. */
void FzeroMsuUpdate(void);
bool FzeroMsuActive(void);
const char *FzeroMsuError(void);
bool FzeroMsuSelectSaveRoot(void);
enum { FZERO_MSU_AUDIO_STATE_SIZE=328 };
void FzeroMsuSaveAudio(uint8_t out[FZERO_MSU_AUDIO_STATE_SIZE]);
bool FzeroMsuAudioStateValid(const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]);
void FzeroMsuRestoreAudio(const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]);
/* Course soundtrack namespaces coexist beside the user's selected primary
 * soundtrack. NULL source explicitly requests SNES fallback; empty = primary. */
void FzeroMsuSelectTrackSource(const char *source, bool race);
