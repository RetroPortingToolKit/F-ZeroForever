#pragma once

#include "fzero_practice_ghosts.h"

typedef enum FzeroPracticeGhostPanel {
  FZERO_GHOST_PANEL_NONE,
  FZERO_GHOST_PANEL_RIVAL,
  FZERO_GHOST_PANEL_CHOOSE,
  FZERO_GHOST_PANEL_SAVE,
  FZERO_GHOST_PANEL_SAVED,
  FZERO_GHOST_PANEL_INCOMPLETE,
} FzeroPracticeGhostPanel;

typedef struct FzeroPracticeGhostView {
  FzeroPracticeGhostPanel panel;
  bool selected;
  unsigned count, index;
  FzeroGhostRun run;
  FzeroGhostResult error;
} FzeroPracticeGhostView;

typedef struct FzeroPracticeGhostState {
  FzeroGhostRecordingPoint recording;
  uint32_t course, selected_run, active_run, samples, finish;
  uint16_t previous_input, consumed_input;
  uint8_t flags, error;
} FzeroPracticeGhostState;

void FzeroPracticeGhostRoot(const char *path);
void FzeroPracticeGhostReset(void);
void FzeroPracticeGhostSaveState(FzeroPracticeGhostState *state);
void FzeroPracticeGhostLoadState(const FzeroPracticeGhostState *state, const uint8_t *ram);
uint16_t FzeroPracticeGhostInput(uint16_t input, const uint8_t *ram);
void FzeroPracticeGhostTick(const uint8_t *ram);
FzeroPracticeGhostView FzeroPracticeGhostViewAt(const uint8_t *ram);
void FzeroPracticeGhostDrawShip(uint32_t *pixels, unsigned width, unsigned height,
                               size_t pitch, const uint8_t *ram);
