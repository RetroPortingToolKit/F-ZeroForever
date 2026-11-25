#include "fzero_practice_runtime.h"

#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "fzero_practice_legacy.h"
#include "fzero_ship.h"
#include "fzero_tracks.h"
#include "sha256.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>

extern Snes *g_snes;

enum {
  BUTTON_B = 1, BUTTON_SELECT = 4, BUTTON_START = 8, BUTTON_UP = 16, BUTTON_DOWN = 32,
  BUTTON_LEFT = 64, BUTTON_RIGHT = 128, BUTTON_A = 256, BUTTON_L = 1024, BUTTON_R = 2048,
  BUTTON_CONFIRM = BUTTON_A | BUTTON_START,
  RETAIL_PRACTICE_PROFILE = 1,
  RETAIL_NO_RIVAL_CURSOR = 4,
  RETAIL_NO_RIVAL = 255,
  STATE_RIVAL = 1, STATE_RIVAL_PANEL = 2, STATE_RECORDING = 4, STATE_PLAYBACK = 8,
  STATE_OFFER = 16, STATE_SAVE_YES = 32, STATE_SAVED = 64, STATE_RECORDING_LOST = 128,
};

static char root[CP_PATH];
static struct PracticeGhostSession {
  FzeroGhostLibrary *library;
  unsigned course, selection, sample_count, finish_centiseconds;
  uint16_t previous_input, consumed_input;
  bool rival, was_rival_panel, recording, playback, offer_save, save_yes, saved;
  bool recording_lost;
  FzeroGhostRun target;
  FzeroGhostResult error;
} session;

static bool active(const uint8_t *ram) {
  /* The first host Practice route uses the stock race and menu. Other content
   * providers need to supply their own course and machine identities. */
  return *root && FzeroPracticeGhosts() && !FzeroDeluxeActive() &&
         !FzeroTracksActive() && ram[0x58];
}

static bool rival_panel(const uint8_t *ram) {
  return ram[0x54] == 1 && ram[0x55] == 4;
}

static bool course_panel(const uint8_t *ram) {
  return ram[0x54] == 2 && ram[0x55] == 1 && ram[0x56] == 1;
}

static bool race(const uint8_t *ram) {
  return ram[0x54] == 2 && ram[0x55] == 3;
}

static bool results(const uint8_t *ram) {
  return ram[0x54] == 3 && ram[0x55] == 3 && ram[0x56] == 3;
}

static uint16_t word(const uint8_t *ram, unsigned offset) {
  return (uint16_t)(ram[offset] | (unsigned)ram[offset + 1] << 8);
}

static unsigned decimal(unsigned bcd) { return (bcd >> 4) * 10 + (bcd & 15); }

void FzeroPracticeGhostReset(void) {
  FzeroGhostClose(session.library);
  session = (struct PracticeGhostSession){.course = UINT32_MAX};
}

void FzeroPracticeGhostRoot(const char *path) {
  FzeroPracticeGhostReset();
  int written = snprintf(root, sizeof(root), "%s", path);
  if (written < 0 || (size_t)written >= sizeof(root)) root[0] = 0;
}

static void import_retail_legacy(const uint8_t *ram, const char *relative_path) {
  char path[CP_PATH + sizeof("/" FZERO_LEGACY_MSU_RECORDS "/ghosts/save.srm")];
  int written = snprintf(path, sizeof(path), "%s/%s", root, relative_path);
  if (written < 0 || (size_t)written >= sizeof(path)) return;
  FILE *file = fopen(path, "rb");
  if (!file) return;
  uint8_t sram[0x8000];
  bool read = fread(sram, 1, sizeof(sram), file) == sizeof(sram) && fgetc(file) == EOF;
  fclose(file);
  FzeroLegacyGhost ghost;
  if (!read || !FzeroLegacyGhostRead(sram, sizeof(sram), &ghost) ||
      ghost.course != session.course || ghost.machine >= 4)
    return;
  uint8_t source_identity[36] = { (uint8_t)ghost.course, (uint8_t)ghost.machine,
                                  (uint8_t)ghost.samples, (uint8_t)(ghost.samples >> 8) };
  uint8_t digest[32];
  sha256_compute(ghost.stream, ghost.length, source_identity + 4);
  sha256_compute(source_identity, sizeof(source_identity), digest);
  if (FzeroGhostHasLegacy(session.library, digest)) return;

  unsigned ticks = ghost.samples * 2;
  FzeroGhostPose *poses = malloc(ticks * sizeof(*poses));
  if (!poses) return;
  if (FzeroLegacyGhostDecode(&ghost,
                           (FzeroGhostPose){.x = word(ram, 0xb70), .y = word(ram, 0x13fe)},
                           poses, ticks)) {
    FzeroGhostBeginRun(session.library, ghost.machine);
    for (unsigned i = 0; i < ticks; ++i) FzeroGhostRecord(session.library, poses[i]);
    if (FzeroGhostSaveRun(session.library, 0, 0, digest) == FZERO_GHOST_OK)
      fprintf(stderr, "[practice-ghost] imported legacy course=%u car=%u frames=%u\n",
              ghost.course, ghost.machine, ticks);
  }
  free(poses);
}

static void load_course(const uint8_t *ram, bool import_legacy) {
  unsigned course = ram[0x53];
  if (session.course == course) return;
  FzeroGhostClose(session.library);
  session.library = NULL;
  session.course = course;
  session.selection = 0;
  session.playback = false;
  session.error = FZERO_GHOST_OK;
  const CpTrack *track = FzeroTracksRuntimeTrack(course / 5, course % 5);
  if (track) {
    uint8_t key[32];
    char id[CP_ID + sizeof("retail/")];
    snprintf(id, sizeof(id), "retail/%s", track->id);
    FzeroGhostCompatibilityKey(key, id, NULL, FzeroGameplaySettingsCurrent(),
                               RETAIL_PRACTICE_PROFILE, 0);
    session.library = FzeroGhostOpen(root, key);
    if (!session.library) session.error = FZERO_GHOST_IO_ERROR;
    else if (import_legacy && course_panel(ram)) {
      import_retail_legacy(ram, "ghosts/save.srm");
      import_retail_legacy(ram, FZERO_LEGACY_MSU_RECORDS "/ghosts/save.srm");
    }
  }
}

void FzeroPracticeGhostSaveState(FzeroPracticeGhostState *state) {
  FzeroGhostRun selected = {0};
  FzeroGhostGet(session.library, session.selection, &selected);
  state->course = session.course;
  state->selected_run = selected.id;
  state->active_run = session.playback ? session.target.id : 0;
  state->samples = session.sample_count;
  state->finish = session.finish_centiseconds;
  state->previous_input = session.previous_input;
  state->consumed_input = session.consumed_input;
  state->error = session.error;
  state->flags = (session.rival ? STATE_RIVAL : 0) |
      (session.was_rival_panel ? STATE_RIVAL_PANEL : 0) |
      (session.recording ? STATE_RECORDING : 0) |
      (session.playback ? STATE_PLAYBACK : 0) |
      (session.offer_save ? STATE_OFFER : 0) |
      (session.save_yes ? STATE_SAVE_YES : 0) |
      (session.saved ? STATE_SAVED : 0) |
      (session.recording_lost ? STATE_RECORDING_LOST : 0);
  state->recording = FzeroGhostRecordingPosition(session.library);
}

static bool find_run(unsigned id, unsigned *index, FzeroGhostRun *run) {
  for (unsigned i = 0; id && i < FzeroGhostCount(session.library); ++i) {
    if (FzeroGhostGet(session.library, i, run) && run->id == id) {
      *index = i;
      return true;
    }
  }
  return false;
}

void FzeroPracticeGhostLoadState(const FzeroPracticeGhostState *state, const uint8_t *ram) {
  if (!active(ram)) {
    FzeroPracticeGhostReset();
    return;
  }
  const unsigned flags = state->flags;
  bool have_course = state->course == ram[0x53];
  if (have_course) load_course(ram, false);
  else FzeroPracticeGhostReset();
  session.previous_input = state->previous_input;
  session.consumed_input = state->consumed_input;
  session.rival = (flags & STATE_RIVAL) != 0;
  session.was_rival_panel = (flags & STATE_RIVAL_PANEL) != 0;
  session.recording = have_course && (flags & STATE_RECORDING) != 0;
  session.offer_save = have_course && (flags & STATE_OFFER) != 0;
  session.save_yes = (flags & STATE_SAVE_YES) != 0;
  session.saved = have_course && (flags & STATE_SAVED) != 0;
  session.sample_count = have_course ? state->samples : 0;
  session.finish_centiseconds = have_course ? state->finish : 0;
  session.error = state->error <= FZERO_GHOST_FULL ? state->error : FZERO_GHOST_INVALID;

  FzeroGhostRun selected;
  session.selection = 0;
  find_run(state->selected_run, &session.selection, &selected);
  unsigned active_index = 0;
  bool same_run = session.playback && state->active_run == session.target.id;
  session.playback = have_course && race(ram) && (flags & STATE_PLAYBACK) &&
      find_run(state->active_run, &active_index, &session.target) &&
      (same_run || FzeroGhostSelect(session.library, active_index));

  /* Rewind can resume an exact prefix already in memory. A cold load still
   * restores the rival, but cannot publish an incomplete new recording. */
  session.recording_lost = (flags & STATE_RECORDING_LOST) != 0 ||
      ((session.recording || (session.offer_save && !session.saved)) &&
       !FzeroGhostRestoreRecording(session.library, state->recording));
}

uint16_t FzeroPracticeGhostInput(uint16_t input, const uint8_t *ram) {
  unsigned pressed = input & ~session.previous_input;
  session.previous_input = input;
  session.consumed_input &= input;
  input &= ~session.consumed_input;
  if (!active(ram)) return input;

  if (rival_panel(ram)) {
    if (!session.was_rival_panel) {
      FzeroPracticeGhostReset();
      session.previous_input = input;
      session.was_rival_panel = true;
      return input;
    }
    if (session.rival) {
      if (pressed & BUTTON_UP) {
        session.rival = false;
        session.consumed_input |= input & BUTTON_UP;
        return 0;
      }
      if (pressed & BUTTON_B) session.rival = false;
      /* The retail No Rival choice still owns the actual race transition. */
      return input & (BUTTON_CONFIRM | BUTTON_B);
    }
    if (ram[0x5a] == RETAIL_NO_RIVAL_CURSOR && (pressed & BUTTON_DOWN)) {
      session.consumed_input = input & BUTTON_DOWN;
      session.rival = true;
      return 0;
    }
    return input;
  }
  session.was_rival_panel = false;

  if (course_panel(ram) && session.rival) {
    load_course(ram, true);
    unsigned count = FzeroGhostCount(session.library);
    unsigned shoulder = pressed & (BUTTON_L | BUTTON_R);
    if (count > 1 && (shoulder == BUTTON_L || shoulder == BUTTON_R)) {
      session.selection = (session.selection + (pressed & BUTTON_L ? count - 1 : 1)) % count;
      session.error = FZERO_GHOST_OK;
    }
    if ((pressed & BUTTON_SELECT) && (input & BUTTON_CONFIRM)) {
      session.consumed_input |= input & BUTTON_CONFIRM;
      return input & ~(BUTTON_L | BUTTON_R | BUTTON_CONFIRM);
    }
    if (count && (pressed & BUTTON_CONFIRM) &&
        !FzeroGhostSelect(session.library, session.selection)) {
      session.error = FZERO_GHOST_INVALID;
      session.consumed_input |= input & BUTTON_CONFIRM;
      return input & ~(BUTTON_L | BUTTON_R | BUTTON_CONFIRM);
    }
    return input & ~(BUTTON_L | BUTTON_R);
  }

  if (results(ram) && session.offer_save) {
    if (pressed & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN))
      session.save_yes = !session.save_yes;
    if (pressed & BUTTON_B) {
      session.offer_save = false;
      session.consumed_input = input & BUTTON_B;
    } else if (pressed & BUTTON_CONFIRM) {
      session.consumed_input = input & BUTTON_CONFIRM;
      if (session.saved || session.recording_lost || !session.save_yes) {
        session.offer_save = false;
      } else {
        session.error = FzeroGhostSaveRun(session.library, 0, session.finish_centiseconds, NULL);
        session.saved = session.error == FZERO_GHOST_OK;
        if (session.saved)
          fprintf(stderr, "[practice-ghost] saved course=%u finish=%u frames=%u\n",
                  session.course, session.finish_centiseconds, session.sample_count);
      }
    }
    return 0;
  }
  return input;
}

void FzeroPracticeGhostTick(const uint8_t *ram) {
  if (!active(ram)) return;
  if (course_panel(ram)) {
    if (session.rival) load_course(ram, true);
    session.recording = session.playback = false;
    return;
  }
  if (race(ram) && ram[0xcf2] == RETAIL_NO_RIVAL && !ram[0x5b] &&
      !(ram[0xc3] & 0x70) && (ram[0xd40] == 255 || ram[0xd40] < 5)) {
    if (!session.recording) {
      load_course(ram, true);
      session.sample_count = 0;
      session.finish_centiseconds = 0;
      session.saved = session.offer_save = session.save_yes = false;
      session.recording = true;
      session.recording_lost = false;
      FzeroGhostBeginRun(session.library, ram[0x52]);
      session.playback = session.rival &&
          FzeroGhostGet(session.library, session.selection, &session.target) &&
          FzeroGhostSelect(session.library, session.selection);
      if (session.playback)
        fprintf(stderr, "[practice-ghost] racing course=%u run=%u car=%u frames=%u\n",
                session.course, session.target.id, session.target.machine, session.target.ticks);
    }
    if (!session.recording_lost)
      FzeroGhostRecord(session.library, (FzeroGhostPose){word(ram, 0xb70), word(ram, 0xb90),
                                                       word(ram, 0xbc0), word(ram, 0xbd0)});
    session.finish_centiseconds = decimal(ram[0xc0]) * 6000 +
        decimal(ram[0xc1]) * 100 + decimal(ram[0xc2]);
    ++session.sample_count;
    return;
  }
  if (results(ram) && session.recording) {
    session.recording = session.playback = false;
    if (ram[0xd40] == 5 && session.sample_count) {
      session.finish_centiseconds = decimal(ram[0xc0]) * 6000 +
          decimal(ram[0xc1]) * 100 + decimal(ram[0xc2]);
      session.offer_save = true;
    }
  }
}

FzeroPracticeGhostView FzeroPracticeGhostViewAt(const uint8_t *ram) {
  FzeroPracticeGhostView view = {0};
  if (!active(ram)) return view;
  view.error = session.error;
  if (rival_panel(ram)) {
    view.panel = FZERO_GHOST_PANEL_RIVAL;
    view.selected = session.rival;
  } else if (course_panel(ram) && session.rival) {
    view.panel = FZERO_GHOST_PANEL_CHOOSE;
    if (session.course == ram[0x53]) {
      view.count = FzeroGhostCount(session.library);
      view.index = session.selection;
      FzeroGhostGet(session.library, session.selection, &view.run);
    }
  } else if (results(ram) && session.offer_save) {
    view.panel = session.saved ? FZERO_GHOST_PANEL_SAVED :
        session.recording_lost ? FZERO_GHOST_PANEL_INCOMPLETE : FZERO_GHOST_PANEL_SAVE;
    view.selected = session.save_yes;
  }
  return view;
}

static uint8_t stock_table(void *context, uint32_t address) {
  const uint8_t *data = cart_getRomPtr(context, (uint8_t)(address >> 16), (uint16_t)address);
  return data ? *data : 0;
}

bool FzeroPracticeGhostScene(const uint8_t *ram) {
  return active(ram) && ((race(ram) && session.playback) ||
                        (results(ram) && session.offer_save));
}

bool FzeroPracticeGhostBuildShip(FzeroShip *ship, const uint8_t *ram) {
  /* The original ghost occupies the opponent OBJ reservation on alternate
   * simulation frames. Presentation interpolation must keep that cadence. */
  if (!active(ram) || !race(ram) || !session.playback || !session.sample_count ||
      session.course != ram[0x53] || !(ram[0x51] & 1))
    return false;
  FzeroGhostPose pose;
  /* The published PPU frame starts before the final logic tick of this host frame. */
  unsigned sample = session.sample_count > 1 ? session.sample_count - 2 : 0;
  if (!FzeroGhostPoseAt(session.library, sample, &pose)) return false;
  FzeroActor camera = {.x = word(ram, 0xb70), .y = word(ram, 0xb90),
                       .height = word(ram, 0xbc0), .facing = word(ram, 0xbd0), .machine = ram[0x52]};
  FzeroActor target = {.x = pose.x, .y = pose.y, .height = pose.height,
                       .facing = pose.facing, .machine = (uint8_t)session.target.machine};
  FzeroActorTables tables = {stock_table, g_snes->cart, 0};
  return FzeroShipBuild(ship, &camera, &target, tables) && ship->visible;
}
