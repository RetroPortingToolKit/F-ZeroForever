#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "content_pack.h"
#include "fzero_course.h"
#include "fzero_music_sources.h"
/* Catalog owns copied CpPack entries; this module owns their resource paths. */
void FzeroPacksDiscover(CpCatalog *catalog, const char *directory);
/* Validate a staged, single-pack catalog without changing the live catalog. */
bool FzeroPacksValidateImport(const char *directory, const char *cache_directory, CpPack *info,
                             char *error, size_t cap);
bool FzeroPacksContains(const char *id);
typedef struct FzeroPreparedPack FzeroPreparedPack;
/* Worker prepares metadata; launcher publication performs no filesystem I/O.
 * The gameplay catalog is rebuilt normally when leaving the launcher. */
FzeroPreparedPack *FzeroPacksPrepareImport(const char *root, const CpPack *info,
                                           const char *installed,
                                           const char *cache_directory);
void FzeroPacksDiscardPrepared(FzeroPreparedPack *prepared);
void FzeroPacksPublishPrepared(FzeroPreparedPack *prepared);
void FzeroPacksForgetImport(const char *id);
bool FzeroPacksLoadCourse(const char *id, unsigned index, FzeroCourse *out,
                          char *error, size_t cap);
/* courses/name.fzc or name.fzm -> this pack's music/name.pcm. */
bool FzeroPacksCourseMusic(const char *id, unsigned index, char *out, size_t cap);
bool FzeroPacksMusicSources(FzeroMusicSources *out);
bool FzeroPacksResolveMusic(const char *source, unsigned track, char *out,
                            size_t cap);
bool FzeroPacksHasMusic(void);
/* A selected soundtrack overrides only its explicitly mapped courses. Missing
 * mapped recordings fall back to SNES audio, never to a different soundtrack.
 */
const char *FzeroPacksSoundtrackId(unsigned index);
const char *FzeroPacksSoundtrackName(const char *id);
const char *FzeroPacksSelectedSoundtrack(void);
bool FzeroPacksSelectSoundtrack(const char *id);
void FzeroPacksRestoreSoundtrack(const char *id);
bool FzeroPacksSoundtrackMusic(const char *soundtrack, const char *pack,
                               unsigned index, char *out, size_t cap);
const char *FzeroPacksMenuMusicId(unsigned index);
const char *FzeroPacksMenuMusicName(const char *id);
bool FzeroPacksMenuMusic(const char *id, const char *cue, char *out, size_t cap);
#ifdef __cplusplus
}
#endif
