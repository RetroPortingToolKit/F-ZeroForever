#include "fzero_practice_ghosts.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace fs = std::filesystem;

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __LINE__ << ": " << #condition << '\n'; std::exit(1); \
} } while (false)

using Key = std::array<uint8_t, 32>;
using Library = std::unique_ptr<FzeroGhostLibrary, decltype(&FzeroGhostClose)>;

static Key key_for(const FzeroGameplaySettings &settings, const char *id = "retail/mute-city-1",
                   const FzeroCourse *course = nullptr) {
  Key key;
  FzeroGhostCompatibilityKey(key.data(), id, course, &settings, 1, 0);
  return key;
}

static void record(FzeroGhostLibrary *library, unsigned machine, unsigned ticks, uint16_t x) {
  FzeroGhostBeginRun(library, machine);
  for (unsigned frame = 0; frame < ticks; ++frame)
    FzeroGhostRecord(library, {x, static_cast<uint16_t>(frame), 0x1234, 0x789a});
  CHECK(FzeroGhostSaveRun(library, ticks / 2) == FZERO_GHOST_OK);
}

int main() {
  FzeroGameplaySettings settings{};
  settings.practice_ghosts = true;
  const auto key = key_for(settings);
  settings.practice_ghosts = false;
  settings.enabled = (1u << FZERO_RULE_ANIMATION) | (1u << FZERO_RULE_EXHAUST) | (1u << FZERO_RULE_MSU);
  settings.tuning = 2;
  CHECK(key_for(settings) == key);
  settings.enabled |= (1u << FZERO_RULE_TUNING);
  const auto rebalanced_key = key_for(settings);
  CHECK(rebalanced_key != key);
  settings.tuning = 0;
  CHECK(key_for(settings) != rebalanced_key);
  settings = {};
  CHECK(key_for(settings, "retail/mute-city-2") != key);

  auto course = std::make_unique<FzeroCourse>();
  const auto custom_key = key_for(settings, "test/track", course.get());
  course->palette[0] = 10;
  course->music = 27;
  course->graphics[2] = 14;
  CHECK(key_for(settings, "test/track", course.get()) == custom_key);
  course->path[12] = 3;
  CHECK(key_for(settings, "test/track", course.get()) != custom_key);

  fs::path directory = fs::current_path() / ("practice-ghost-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(directory));
  const auto root = directory.string();
  auto open = [&](const Key &digest) {
    return Library(FzeroGhostOpen(root.c_str(), digest.data()), FzeroGhostClose);
  };
  auto writer = open(key), other_writer = open(key);
  CHECK(writer && other_writer && FzeroGhostCount(writer.get()) == 0);
  record(writer.get(), 0, 10, 0x1010);
  record(writer.get(), 0, 8, 0x3400);
  record(other_writer.get(), 8, 10, 0x2090);
  record(other_writer.get(), FZERO_GHOST_BLUE_THUNDER, 11, 0x1100);
  writer.reset();
  other_writer.reset();

  auto reader = open(key);
  CHECK(reader && FzeroGhostCount(reader.get()) == 4);
  const unsigned ids[] = {2, 1, 3, 4}, machines[] = {0, 0, 8, FZERO_GHOST_BLUE_THUNDER};
  const uint16_t positions[] = {0x3400, 0x1010, 0x2090, 0x1100};
  for (unsigned i = 0; i < 4; ++i) {
    FzeroGhostRun run;
    CHECK(FzeroGhostGet(reader.get(), i, &run));
    CHECK(run.id == ids[i] && run.machine == machines[i] && run.best_lap_ticks == run.ticks / 2);
    CHECK(FzeroGhostSelect(reader.get(), i));
    FzeroGhostPose pose;
    CHECK(FzeroGhostPoseAt(reader.get(), run.ticks - 1, &pose));
    CHECK(pose.x == positions[i] && pose.y == run.ticks - 1 && pose.height == 0x1234 && pose.facing == 0x789a);
    CHECK(!FzeroGhostPoseAt(reader.get(), run.ticks, &pose));
  }
  auto incompatible = open(rebalanced_key);
  CHECK(incompatible && FzeroGhostCount(incompatible.get()) == 0);
  CHECK(FzeroGhostCount(open(key).get()) == 4);

  const auto course_folder = fs::directory_iterator(directory / "practice-ghosts")->path();
  CHECK(fs::create_directory(course_folder / "005"));
  std::ofstream(course_folder / "005" / "run.tmp") << "interrupted write";
  CHECK(FzeroGhostCount(open(key).get()) == 4);
  std::fstream broken(course_folder / "003" / "run.fzg", std::ios::binary | std::ios::in | std::ios::out);
  broken.seekp(-1, std::ios::end);
  broken.put('!');
  broken.close();
  CHECK(!FzeroGhostSelect(reader.get(), 2));
  FzeroGhostPose pose;
  CHECK(!FzeroGhostPoseAt(reader.get(), 0, &pose));
  CHECK(FzeroGhostSelect(reader.get(), 0));
  fs::remove_all(directory);
  return 0;
}
