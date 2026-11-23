#include "fzero_practice_ghosts.h"
#include "fzero_machine.h"
#include "fzero_video.h"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr unsigned kMaximumRuns = 128;
constexpr unsigned kMaximumTicks = static_cast<unsigned>(20 * 60 * FZERO_SIMULATION_HZ);
constexpr unsigned kPoseBytes = 8;
constexpr unsigned kMetadataBytes = 52;
constexpr unsigned kHeaderBytes = kMetadataBytes + 32;
constexpr uint8_t kMagic[] = {'F', 'Z', 'G', 1};
static_assert(static_cast<unsigned>(FZERO_GHOST_BLUE_THUNDER) == FZERO_MACHINE_COUNT);

using Bytes = std::vector<uint8_t>;

void put_word(Bytes &bytes, uint16_t word) {
  bytes.push_back(static_cast<uint8_t>(word));
  bytes.push_back(static_cast<uint8_t>(word >> 8));
}

void put_int(Bytes &bytes, uint32_t value) {
  put_word(bytes, static_cast<uint16_t>(value));
  put_word(bytes, static_cast<uint16_t>(value >> 16));
}

unsigned word_at(const uint8_t *p) {
  return p[0] | (unsigned)p[1] << 8;
}

unsigned int_at(const uint8_t *p) {
  return word_at(p) | word_at(p + 2) << 16;
}

std::string hex_key(const uint8_t key[32]) {
  constexpr char digits[] = "0123456789abcdef";
  std::string name;
  name.reserve(64);
  for (unsigned i = 0; i < 32; ++i) {
    name.push_back(digits[key[i] >> 4]);
    name.push_back(digits[key[i] & 15]);
  }
  return name;
}

unsigned slot_id(const std::u8string &name) {
  if (name.size() != 3 || name[0] < '0' || name[0] > '9' ||
      name[1] < '0' || name[1] > '9' || name[2] < '0' || name[2] > '9')
    return 0;
  return (name[0] - '0') * 100 + (name[1] - '0') * 10 + (name[2] - '0');
}

std::string slot_name(unsigned id) {
  auto name = std::to_string(id);
  return std::string(3 - name.size(), '0') + name;
}

void run_digest(uint8_t digest[32], const uint8_t *metadata, const Bytes &data) {
  std::array<uint8_t, kHeaderBytes> proof;
  std::memcpy(proof.data(), metadata, kMetadataBytes);
  sha256_compute(data.data(), data.size(), proof.data() + kMetadataBytes);
  sha256_compute(proof.data(), proof.size(), digest);
}

bool read_run(const fs::path &file, const uint8_t key[32], unsigned id,
              FzeroGhostRun &run, Bytes *poses) {
  std::error_code error;
  auto size = fs::file_size(file, error);
  if (error || size < kHeaderBytes || size > kHeaderBytes + kMaximumTicks * kPoseBytes)
    return false;
  std::ifstream input(file, std::ios::binary);
  std::array<uint8_t, kHeaderBytes> header;
  if (!input.read(reinterpret_cast<char *>(header.data()), header.size()) ||
      std::memcmp(header.data(), kMagic, sizeof(kMagic)) ||
      std::memcmp(header.data() + 4, key, 32) || int_at(header.data() + 36) != id)
    return false;
  run = {id, int_at(header.data() + 40), int_at(header.data() + 44), int_at(header.data() + 48)};
  if (!run.ticks || run.ticks > kMaximumTicks || run.best_lap_ticks > run.ticks ||
      run.machine >= FZERO_GHOST_MACHINE_COUNT || size != kHeaderBytes + run.ticks * kPoseBytes)
    return false;
  if (!poses)
    return true;
  Bytes data(run.ticks * kPoseBytes);
  if (!input.read(reinterpret_cast<char *>(data.data()), data.size()) || input.peek() != EOF)
    return false;
  uint8_t digest[32];
  run_digest(digest, header.data(), data);
  if (std::memcmp(digest, header.data() + kMetadataBytes, sizeof(digest)))
    return false;
  *poses = std::move(data);
  return true;
}

bool run_order(const FzeroGhostRun &left, const FzeroGhostRun &right) {
  return left.ticks != right.ticks ? left.ticks < right.ticks : left.id < right.id;
}
} // namespace

struct FzeroGhostLibrary {
  fs::path folder;
  uint8_t key[32];
  std::vector<FzeroGhostRun> runs;
  Bytes recording, playback;
  unsigned machine = FZERO_GHOST_MACHINE_COUNT;
  FzeroGhostResult recording_status = FZERO_GHOST_INVALID;
};

void FzeroGhostCompatibilityKey(uint8_t key[32], const char *course_id,
                                const FzeroCourse *course,
                                const FzeroGameplaySettings *settings,
                                unsigned sim_version, unsigned race_class) {
  /* One digest per replay contract. Pack colors, objects used only for drawing,
   * titles, soundtrack and display settings are independent of this data. */
  constexpr uint32_t replay_rules = FZERO_RULE_ACTIVE_MASK &
      ~((1u << FZERO_RULE_EXHAUST) | (1u << FZERO_RULE_ANIMATION) | (1u << FZERO_RULE_MSU));
  Bytes values;
  values.reserve(80 + (course ? sizeof(*course) : 0));
  put_int(values, sim_version);
  put_int(values, race_class);
  uint32_t enabled = settings->enabled & replay_rules;
  put_int(values, enabled);
  put_int(values, (enabled & (1u << FZERO_RULE_TUNING)) ? settings->tuning : 0);
  put_int(values, (enabled & (1u << FZERO_RULE_BOOST)) ? settings->boost : 0);
  put_int(values, settings->stock_rebalance & 15);
  values.insert(values.end(), course_id, course_id + std::strlen(course_id) + 1);
  if (course) {
    put_int(values, course->required & FZERO_COURSE_GAMEPLAY_FEATURES);
    put_int(values, course->setting);
    put_word(values, std::min<size_t>(course->block_size, sizeof(course->blocks)));
    put_word(values, std::min<size_t>(course->grid_size, sizeof(course->grid)));
    values.insert(values.end(), std::begin(course->pool), std::end(course->pool));
    values.insert(values.end(), course->blocks, course->blocks + std::min<size_t>(course->block_size, sizeof(course->blocks)));
    values.insert(values.end(), course->grid, course->grid + std::min<size_t>(course->grid_size, sizeof(course->grid)));
    values.insert(values.end(), std::begin(course->terrain), std::end(course->terrain));
    values.insert(values.end(), std::begin(course->path), std::end(course->path));
    values.insert(values.end(), std::begin(course->shortcuts), std::end(course->shortcuts));
    values.insert(values.end(), std::begin(course->opponents), std::end(course->opponents));
    values.push_back(course->last_checkpoint);
    values.push_back(course->finish_checkpoint);
    values.push_back(course->pit_checkpoint);
    values.push_back(course->has_pit);
  }
  sha256_compute(values.data(), values.size(), key);
}

FzeroGhostLibrary *FzeroGhostOpen(const char *root, const uint8_t key[32]) {
  if (!root || !*root || !key)
    return nullptr;
  try {
    auto library = std::make_unique<FzeroGhostLibrary>();
    library->folder = fs::path(reinterpret_cast<const char8_t *>(root)) / "practice-ghosts" / hex_key(key);
    std::memcpy(library->key, key, 32);
    library->runs.reserve(kMaximumRuns);
    if (fs::exists(library->folder)) {
      for (const auto &entry : fs::directory_iterator(library->folder)) {
        unsigned id = slot_id(entry.path().filename().u8string());
        FzeroGhostRun run;
        if (id && id <= kMaximumRuns && read_run(entry.path() / "run.fzg", key, id, run, nullptr))
          library->runs.push_back(run);
      }
      std::sort(library->runs.begin(), library->runs.end(), run_order);
    }
    return library.release();
  } catch (const std::exception &) {
    return nullptr;
  }
}

void FzeroGhostClose(FzeroGhostLibrary *library) { delete library; }

unsigned FzeroGhostCount(const FzeroGhostLibrary *library) {
  return library ? static_cast<unsigned>(library->runs.size()) : 0;
}

bool FzeroGhostGet(const FzeroGhostLibrary *library, unsigned index, FzeroGhostRun *run) {
  if (index >= FzeroGhostCount(library) || !run)
    return false;
  *run = library->runs[index];
  return true;
}

bool FzeroGhostSelect(FzeroGhostLibrary *library, unsigned index) {
  if (!library)
    return false;
  library->playback.clear();
  FzeroGhostRun run;
  if (!FzeroGhostGet(library, index, &run))
    return false;
  try {
    return read_run(library->folder / slot_name(run.id) / "run.fzg", library->key,
                    run.id, run, &library->playback);
  } catch (const std::exception &) {
    return false;
  }
}

bool FzeroGhostPoseAt(const FzeroGhostLibrary *library, unsigned tick, FzeroGhostPose *pose) {
  if (!library || !pose || tick >= library->playback.size() / kPoseBytes)
    return false;
  const uint8_t *p = library->playback.data() + tick * kPoseBytes;
  *pose = {static_cast<uint16_t>(word_at(p)), static_cast<uint16_t>(word_at(p + 2)),
           static_cast<uint16_t>(word_at(p + 4)), static_cast<uint16_t>(word_at(p + 6))};
  return true;
}

void FzeroGhostBeginRun(FzeroGhostLibrary *library, unsigned machine) {
  if (!library)
    return;
  library->recording.clear();
  library->machine = machine;
  library->recording_status = machine < FZERO_GHOST_MACHINE_COUNT ? FZERO_GHOST_OK : FZERO_GHOST_INVALID;
}

void FzeroGhostRecord(FzeroGhostLibrary *library, FzeroGhostPose pose) {
  if (!library || library->recording_status != FZERO_GHOST_OK)
    return;
  auto &data = library->recording;
  if (data.size() == kMaximumTicks * kPoseBytes) {
    library->recording_status = FZERO_GHOST_FULL;
    return;
  }
  try {
    put_word(data, pose.x);
    put_word(data, pose.y);
    put_word(data, pose.height);
    put_word(data, pose.facing);
  } catch (const std::bad_alloc &) {
    library->recording_status = FZERO_GHOST_IO_ERROR;
  }
}

FzeroGhostResult FzeroGhostSaveRun(FzeroGhostLibrary *library, unsigned best_lap_ticks) {
  if (!library)
    return FZERO_GHOST_IO_ERROR;
  if (library->recording_status != FZERO_GHOST_OK)
    return library->recording_status;
  const auto &data = library->recording;
  unsigned ticks = static_cast<unsigned>(data.size() / kPoseBytes);
  if (!ticks || best_lap_ticks > ticks)
    return FZERO_GHOST_INVALID;
  fs::path slot;
  try {
    fs::create_directories(library->folder);
    for (unsigned id = 1; id <= kMaximumRuns; ++id) {
      auto candidate = library->folder / slot_name(id);
      if (!fs::create_directory(candidate))
        continue;
      slot = std::move(candidate);
      Bytes header(std::begin(kMagic), std::end(kMagic));
      header.insert(header.end(), library->key, library->key + 32);
      put_int(header, id);
      put_int(header, ticks);
      put_int(header, best_lap_ticks);
      put_int(header, library->machine);
      uint8_t digest[32];
      run_digest(digest, header.data(), data);
      header.insert(header.end(), std::begin(digest), std::end(digest));
      auto temporary = slot / "run.tmp";
      std::ofstream output(temporary, std::ios::binary);
      output.write(reinterpret_cast<const char *>(header.data()), header.size());
      output.write(reinterpret_cast<const char *>(data.data()), data.size());
      output.close();
      if (!output)
        break;
      fs::rename(temporary, slot / "run.fzg");
      FzeroGhostRun committed{id, ticks, best_lap_ticks, library->machine};
      library->runs.insert(std::lower_bound(library->runs.begin(), library->runs.end(), committed, run_order), committed);
      library->recording.clear();
      return FZERO_GHOST_OK;
    }
    if (slot.empty())
      return FZERO_GHOST_FULL;
  } catch (const std::exception &) {
    /* Recoverable writing errors leave the current run available for retry. */
  }
  if (!slot.empty()) {
    std::error_code error;
    fs::remove(slot / "run.tmp", error);
    fs::remove(slot, error);
  }
  return FZERO_GHOST_IO_ERROR;
}
