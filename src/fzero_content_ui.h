#pragma once
#include "recomp_launcher.h"
#ifdef __cplusplus
extern "C" {
#endif
const RecompLauncherCCustomContentProvider *
FzeroContentProvider(const char *mods, const char *helpers);
/* Runs on the launcher thread after a worker finishes. action: 0 installed,
 * 1 use soundtrack, 2 use and enable music, -1 removed. */
void FzeroContentSetChangeCallback(int (*callback)(void *, const char *, int),
                                   void *ctx);
/* Join an in-flight import even if the launcher was closed. Returns whether
 * content changed. */
int FzeroContentShutdown(void);
#ifdef __cplusplus
}
#endif
