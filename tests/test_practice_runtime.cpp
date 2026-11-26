#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

extern "C" {
#include "fzero_practice_runtime.h"
#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "snes/cart.h"
#include "snes/snes.h"

Snes *g_snes;
static FzeroGameplaySettings settings;
bool FzeroPracticeGhosts(void) { return settings.practice_ghosts; }
bool FzeroDeluxeActive(void) { return false; }
bool FzeroTracksActive(void) { return false; }
const FzeroGameplaySettings *FzeroGameplaySettingsCurrent(void) { return &settings; }
const CpTrack *FzeroTracksRuntimeTrack(unsigned, unsigned) {
  static CpTrack track;
  track.id[0] = '0';
  return &track;
}
uint8_t *cart_getRomPtr(Cart *, uint8_t, uint16_t) { return nullptr; }
}

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __LINE__ << ": " << #condition << '\n'; std::exit(1); \
} } while (false)

int main() {
  namespace fs = std::filesystem;
  auto root = fs::current_path() / ("practice-runtime-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(root));
  settings.practice_ghosts = true;
  std::array<uint8_t, 0x20000> ram{};
  const auto view = [&] { return FzeroPracticeGhostViewAt(ram.data()); };
  const auto input = [&](unsigned button) { return FzeroPracticeGhostInput(button, ram.data()); };
  const auto tick = [&] { FzeroPracticeGhostTick(ram.data()); };
  const auto finish = [&] {
    FzeroPracticeGhostRoot(root.string().c_str());
    ram.fill(0); ram[0x58] = 1;
    ram[0x54] = 2; ram[0x55] = 3; ram[0xcf2] = ram[0xd40] = 255;
    tick(); tick(); tick();
    ram[0x54] = ram[0x55] = ram[0x56] = 3; ram[0xd40] = 5;
    tick();
  };
  unsigned saved = 0;
  for (unsigned button : {1u, 256u, 8u}) {
    finish();
    for (unsigned frame = 0; frame < 180; ++frame) { input(0); tick(); }
    CHECK(view().panel == FZERO_GHOST_PANEL_NONE);
    // A pending result survives a current-build restore without skipping it.
    FzeroPracticeGhostState state;
    FzeroPracticeGhostSaveState(&state);
    FzeroPracticeGhostLoadState(&state, ram.data());
    CHECK(view().panel == FZERO_GHOST_PANEL_NONE);
    CHECK(input(button) == 0);
    CHECK(view().panel == FZERO_GHOST_PANEL_SAVE);
    CHECK(!view().selected);
    for (unsigned held = 0; held < 10; ++held) CHECK(input(button) == 0);
    CHECK(view().panel == FZERO_GHOST_PANEL_SAVE);
    input(0); input(64); input(0);
    CHECK(view().selected);
    CHECK(input(button) == button);
    CHECK(view().panel == FZERO_GHOST_PANEL_NONE);
    FzeroPracticeGhostSaveState(&state);
    CHECK(state.selected_run != 0);
    ++saved;
    CHECK(input(button) == 0); // Held confirmation cannot act a second time.
    input(0);

    finish(); input(0); input(button); input(0);
    CHECK(input(button) == button);
    CHECK(input(button) == 0); input(0);
    CHECK(view().panel == FZERO_GHOST_PANEL_NONE); // No also confirms with B.
  }
  // A cold state load cannot publish a recording prefix that is no longer in memory.
  finish();
  FzeroPracticeGhostState pending;
  FzeroPracticeGhostSaveState(&pending);
  FzeroPracticeGhostRoot(root.string().c_str());
  FzeroPracticeGhostLoadState(&pending, ram.data());
  input(0); CHECK(input(1) == 1);
  CHECK(view().panel == FZERO_GHOST_PANEL_NONE);
  // Removing the course-info panel must preserve multiple-run selection.
  ram[0x54] = 1; ram[0x55] = 4; ram[0x56] = 0; ram[0x5a] = 4;
  input(0); input(32); input(0);
  ram[0x54] = 2; ram[0x55] = 1; ram[0x56] = 1;
  input(0);
  FzeroPracticeGhostState chosen;
  FzeroPracticeGhostSaveState(&chosen);
  CHECK(view().count == saved && chosen.selected_run != 0);
  input(2048); input(0);
  FzeroPracticeGhostState next;
  FzeroPracticeGhostSaveState(&next);
  CHECK(next.selected_run != chosen.selected_run);
  input(1024); input(0);
  FzeroPracticeGhostSaveState(&next);
  CHECK(next.selected_run == chosen.selected_run);
  FzeroPracticeGhostReset();
  unsigned files = 0;
  for (const auto &entry : fs::recursive_directory_iterator(root))
    if (entry.path().extension() == ".fzg") ++files;
  CHECK(files == saved);
  fs::remove_all(root);
  std::cout << "Practice result wait, restore, held input, and A/B/Start choices passed\n";
}
