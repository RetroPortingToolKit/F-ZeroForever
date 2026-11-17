#include "desktop/sdl_compat.h"
#include <jni.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static _Atomic uint32_t input;
static _Atomic int requested_pause = -1, requested_quit;

JNIEXPORT void JNICALL Java_org_retroporting_fzero_GameActivity_nativeSlot(
    JNIEnv *env, jclass cls, jboolean save) {
  (void)env;
  (void)cls;
  SDL_Event event = {0};
  event.type = SDL_KEYDOWN;
  event.key.keysym.sym = SDLK_F1;
  event.key.keysym.scancode = SDL_SCANCODE_F1;
  event.key.keysym.mod = save ? KMOD_SHIFT : KMOD_NONE;
  SDL_PushEvent(&event);
}

JNIEXPORT void JNICALL Java_org_retroporting_fzero_GameActivity_nativeInput(
    JNIEnv *env, jclass cls, jint bits) {
  (void)env;
  (void)cls;
  atomic_store(&input, (uint32_t)bits);
}
JNIEXPORT void JNICALL Java_org_retroporting_fzero_GameActivity_nativePause(
    JNIEnv *env, jclass cls, jboolean pause) {
  (void)env;
  (void)cls;
  atomic_store(&input, 0);
  atomic_store(&requested_pause, pause ? 1 : 0);
}
JNIEXPORT void JNICALL Java_org_retroporting_fzero_GameActivity_nativeGameQuit(
    JNIEnv *env, jclass cls) {
  (void)env;
  (void)cls;
  atomic_store(&input, 0);
  atomic_store(&requested_quit, 1);
}
void FzeroAndroidPrepare(void) {
  const char *path = SDL_AndroidGetInternalStoragePath();
  if (path) {
    setenv("SNESRECOMP_DATA_DIR", path, 1);
    chdir(path);
  }
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
}
uint32_t FzeroAndroidInput(void) { return atomic_load(&input); }
void FzeroAndroidCommands(int *running, int *paused) {
  int next = atomic_exchange(&requested_pause, -1);
  if (next >= 0)
    *paused = next;
  if (atomic_load(&requested_quit))
    *running = 0;
}
