#pragma once

#include "fzero_practice_ghosts.h"

typedef enum FzeroPracticeGhostPanel {
  FZERO_GHOST_PANEL_NONE,
  FZERO_GHOST_PANEL_RIVAL,
  FZERO_GHOST_PANEL_CHOOSE,
  FZERO_GHOST_PANEL_SAVE,
  FZERO_GHOST_PANEL_SAVED,
} FzeroPracticeGhostPanel;

typedef struct FzeroPracticeGhostView {
  FzeroPracticeGhostPanel panel;
  bool selected;
  unsigned count, index;
  FzeroGhostRun run;
  FzeroGhostResult error;
} FzeroPracticeGhostView;

void FzeroPracticeGhostRoot(const char *path);
void FzeroPracticeGhostReset(void);
uint16_t FzeroPracticeGhostInput(uint16_t input, const uint8_t *ram);
void FzeroPracticeGhostTick(const uint8_t *ram);
FzeroPracticeGhostView FzeroPracticeGhostViewAt(const uint8_t *ram);
void FzeroPracticeGhostDrawShip(uint32_t *pixels, unsigned width, unsigned height,
                               size_t pitch, const uint8_t *ram);
