#pragma once

#include "fzero_video.h"
#include "recomp_launcher.h"

void FzeroSavedPresetsLoad(const char *video_path);
int FzeroSavedPresetsCount(void);
int FzeroSavedPresetGet(int index, RecompLauncherCModPreset *out);
const char *FzeroSavedPresetCurrent(const FzeroVideoSettings *video,
                                    const RecompLauncherCSettings *launcher);
bool FzeroSavedPresetApply(const char *id, FzeroVideoSettings *video,
                           RecompLauncherCSettings *launcher);
bool FzeroSavedPresetStore(const char *id, const char *name,
                           const FzeroVideoSettings *video,
                           const RecompLauncherCSettings *launcher);
bool FzeroSavedPresetDelete(const char *id);
const char *FzeroSavedPresetError(void);
