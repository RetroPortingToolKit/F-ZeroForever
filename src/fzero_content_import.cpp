#include "fzero_content_import.h"
#include "data_pack.h"
#include "data_pack_io.hpp"
#include "fzero_packs.h"
extern "C" {
#include "fzero_menu_music.h"
}
#include "fzero_soundtrack.h"
#include "sha256.h"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace snesrecomp::data_pack;
namespace {
constexpr uint64_t maxFile = 256ull * 1024 * 1024;
constexpr uint64_t maxTotal = 2ull * 1024 * 1024 * 1024;
constexpr const char *baseHash =
    "bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2";
void need(bool b, const std::string &s) {
  if (!b)
    throw std::runtime_error(s);
}
std::string lower(std::string s) {
  for (char &c : s)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool donorExtension(const std::string &ext) {
  return ext == ".ips" || ext == ".bps" || ext == ".sfc" || ext == ".smc" ||
         ext == ".rom" || ext == ".fig";
}
std::string hash(const std::string &bytes) {
  uint8_t digest[32];
  sha256_compute(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(),
                 digest);
  char hex[65];
  for (unsigned i = 0; i < 32; ++i)
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return hex;
}
bool allowed(const fs::path &p) {
  static const std::set<std::string> ext = {
      ".fzm",  ".aip", ".tmx", ".tsx", ".png",     ".gif", ".bmp",
      ".json", ".bin", ".fzc", ".fzt", ".fztitle", ".txt", ".md",
      ".pcm",  ".msu", ".zip", ".ips", ".bps",     ".sfc", ".smc"};
  auto suffix = lower(p.extension().string());
  return ext.count(suffix) != 0 || donorExtension(suffix);
}
void safeRelative(const fs::path &p) {
  need(!p.empty() && !p.is_absolute() && !p.has_root_name(),
       "The archive contains an absolute path.");
  need(p.generic_string().size() <= 768 &&
           std::distance(p.begin(), p.end()) <= 32,
       "The project has an excessively long or deeply nested path.");
  for (auto &part : p) {
    auto s = part.string();
    auto stem = lower(s.substr(0, s.find('.')));
    if (s.empty())
      continue; // A normal ZIP directory name ends with '/'.
    need(s != ".." && s != "." && s.find_first_of(":\\<>\"|?*") == s.npos &&
             s.back() != '.' && s.back() != ' ' &&
             std::none_of(s.begin(), s.end(),
                          [](unsigned char c) { return c < 32; }),
         "The archive contains an unsafe path.");
    need(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
             !(stem.size() == 4 &&
               (stem.starts_with("com") || stem.starts_with("lpt")) &&
               stem[3] >= '0' && stem[3] <= '9'),
         "The archive contains a reserved filename.");
  }
}
bool linked(const fs::path &p) {
#ifdef _WIN32
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
  return fs::is_symlink(fs::symlink_status(p));
#endif
}
struct Budget {
  uint64_t total = 0;
  unsigned count = 0;
  std::set<std::string> paths;
  void add(const fs::path &relative, uint64_t size) {
    safeRelative(relative);
    need(++count <= 20000 && size <= maxFile && size <= maxTotal - total,
         "This project exceeds the import size limit.");
    total += size;
    need(paths.insert(lower(relative.generic_string())).second,
         "The archive contains duplicate filenames.");
  }
};
struct ZipContents {
  bool donor = false, editor = false, audio = false;
};
ZipContents unzip(const fs::path &source, const fs::path &dest,
                  bool inspectOnly = false, bool musicOnly = false) {
  std::unique_ptr<archive, decltype(&archive_read_free)> ar(archive_read_new(),
                                                            archive_read_free);
  archive_read_support_format_zip(ar.get());
  need(archive_read_open_filename(ar.get(), source.string().c_str(), 65536) ==
           ARCHIVE_OK,
       "Cannot open this ZIP.");
  Budget budget;
  ZipContents contents;
  archive_entry *entry;
  int status;
  while ((status = archive_read_next_header(ar.get(), &entry)) == ARCHIVE_OK) {
    auto name = archive_entry_pathname_utf8(entry);
    need(name != nullptr, "A ZIP filename is not valid UTF-8.");
    auto rel = fs::path(reinterpret_cast<const char8_t *>(name));
    safeRelative(rel);
    need(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
         "Linked files cannot be imported.");
    bool hidden = false;
    for (auto &component : rel)
      if (component.string().starts_with('.'))
        hidden = true;
    if (hidden) {
      budget.add(rel, 0);
      archive_read_data_skip(ar.get());
      continue;
    }
    if (archive_entry_filetype(entry) == AE_IFDIR) {
      budget.add(rel, 0);
      if (!inspectOnly)
        fs::create_directories(dest / rel);
      continue;
    }
    need(archive_entry_filetype(entry) == AE_IFREG,
         "Unsupported ZIP entry: " + rel.generic_string());
    auto size = archive_entry_size(entry);
    need(size >= 0, "ZIP entry has an invalid size.");
    budget.add(rel, uint64_t(size));
    auto ext = lower(rel.extension().string());
    contents.donor |= donorExtension(ext);
    contents.editor |= ext == ".fzm" || rel.filename() == "pack.json";
    contents.audio |= ext == ".pcm";
    if (inspectOnly) {
      archive_read_data_skip(ar.get());
      continue;
    }
    if (musicOnly && (donorExtension(ext) || !allowed(rel))) {
      archive_read_data_skip(ar.get());
      continue; // Cover art, tools and emulator patches are not music assets.
    }
    need(allowed(rel), "Unsupported file in ZIP: " + rel.generic_string());
    fs::create_directories((dest / rel).parent_path());
    std::ofstream out(dest / rel, std::ios::binary);
    need(bool(out), "Cannot create an imported file.");
    std::array<char, 65536> buffer;
    uint64_t written = 0;
    la_ssize_t got;
    while ((got = archive_read_data(ar.get(), buffer.data(), buffer.size())) >
           0) {
      written += uint64_t(got);
      need(written <= uint64_t(size), "ZIP entry exceeds its declared size.");
      out.write(buffer.data(), got);
      need(bool(out), "Cannot write the imported project.");
    }
    need(got == 0 && written == uint64_t(size),
         "ZIP data is damaged or incomplete.");
  }
  need(status == ARCHIVE_EOF, "The ZIP directory is damaged.");
  return contents;
}
void copyProject(const fs::path &source, const fs::path &dest) {
  need(!linked(source),
       "Choose the actual project folder, not a linked folder.");
  Budget budget;
  for (fs::recursive_directory_iterator it(source), end; it != end; ++it) {
    auto rel = fs::relative(it->path(), source);
    if (it->path().filename().string().starts_with('.')) {
      if (it->is_directory())
        it.disable_recursion_pending();
      continue;
    }
    need(!linked(it->path()), "Project contains a linked file or folder.");
    if (it->is_directory())
      continue;
    need(it->is_regular_file(), "Project contains a special file.");
    if (!allowed(it->path()))
      continue; // Do not bring tools/executables along with editor assets.
    budget.add(rel, it->file_size());
    fs::create_directories((dest / rel).parent_path());
    fs::copy_file(it->path(), dest / rel, fs::copy_options::none);
  }
}
// Bound expansion of per-course archives before the pack decoder opens them.
// They share one budget with the surrounding unpacked files.
void preflight(const fs::path &root) {
  Budget budget;
  for (auto &e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file())
      continue;
    auto rel = fs::relative(e.path(), root);
    budget.add(rel, e.file_size());
    if (lower(e.path().extension().string()) != ".zip")
      continue;
    std::unique_ptr<archive, decltype(&archive_read_free)> ar(
        archive_read_new(), archive_read_free);
    archive_read_support_format_zip(ar.get());
    need(archive_read_open_filename(ar.get(), e.path().string().c_str(),
                                    65536) == ARCHIVE_OK,
         "Cannot open a course ZIP.");
    archive_entry *entry;
    int status;
    while ((status = archive_read_next_header(ar.get(), &entry)) ==
           ARCHIVE_OK) {
      auto name = archive_entry_pathname_utf8(entry);
      need(name != nullptr, "Invalid course ZIP path.");
      auto path = fs::path(reinterpret_cast<const char8_t *>(name));
      safeRelative(path);
      auto size = archive_entry_size(entry);
      need(size >= 0, "Invalid course ZIP size.");
      budget.add(rel / path, uint64_t(size));
      need(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
           "Linked course files are not supported.");
      need(archive_entry_filetype(entry) == AE_IFDIR ||
               archive_entry_filetype(entry) == AE_IFREG,
           "Unsupported course ZIP entry.");
      auto ext = lower(path.extension().string());
      need(ext != ".zip" && !donorExtension(ext),
           "A course ZIP must contain editor assets, not another archive, ROM "
           "or patch.");
      archive_read_data_skip(ar.get());
    }
    need(status == ARCHIVE_EOF, "Damaged course ZIP.");
  }
}
rapidjson::Document json(const fs::path &p) {
  auto bytes = read(p);
  rapidjson::Document d;
  d.Parse<rapidjson::kParseIterativeFlag>(bytes.c_str());
  need(!d.HasParseError() && d.IsObject(),
       "Invalid JSON: " + p.filename().string());
  validate_json(d);
  return d;
}
void writeJson(const fs::path &p, const rapidjson::Value &d) {
  rapidjson::StringBuffer b;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(b);
  d.Accept(w);
  std::ofstream o(p, std::ios::binary);
  o.write(b.GetString(), b.GetSize());
  need(bool(o), "Cannot write the pack manifest.");
}
// Inspect identities individually: a catalog deliberately drops both members
// of an existing duplicate-ID set, which must not permit a third copy to
// install.
std::string installedId(const fs::path &path) {
  try {
    if (fs::is_directory(path))
      return string(json(path / "pack.json"), "id");
    if (lower(path.extension().string()) != ".zip")
      return {};
    std::unique_ptr<archive, decltype(&archive_read_free)> ar(
        archive_read_new(), archive_read_free);
    archive_read_support_format_zip(ar.get());
    if (archive_read_open_filename(ar.get(), path.string().c_str(), 65536) !=
        ARCHIVE_OK)
      return {};
    archive_entry *entry;
    unsigned count = 0;
    while (++count <= 20000 &&
           archive_read_next_header(ar.get(), &entry) == ARCHIVE_OK) {
      auto name = archive_entry_pathname_utf8(entry);
      if (!name)
        return {};
      auto rel = fs::path(reinterpret_cast<const char8_t *>(name));
      if (rel.filename() != "pack.json" ||
          std::distance(rel.begin(), rel.end()) > 2)
        continue;
      auto size = archive_entry_size(entry);
      if (size < 0 || size > 4 * 1024 * 1024)
        return {};
      std::string bytes(static_cast<size_t>(size), '\0');
      size_t offset = 0;
      while (offset < bytes.size()) {
        auto n = archive_read_data(ar.get(), bytes.data() + offset,
                                   bytes.size() - offset);
        if (n <= 0)
          return {};
        offset += size_t(n);
      }
      rapidjson::Document d;
      d.Parse<rapidjson::kParseIterativeFlag>(bytes.c_str());
      if (d.HasParseError() || !d.IsObject())
        return {};
      validate_json(d);
      return string(d, "id");
    }
  } catch (const std::exception &) {
  }
  return {};
}
void set(rapidjson::Document &d, const char *key, const std::string &s) {
  auto &a = d.GetAllocator();
  rapidjson::Value v(s.c_str(), rapidjson::SizeType(s.size()), a);
  if (d.HasMember(key))
    d[key] = std::move(v);
  else
    d.AddMember(rapidjson::Value(key, a), v, a);
}
void refreshManifest(const fs::path &root, const std::string &title) {
  auto envelope = json(root / "pack.json");
  need(envelope.HasMember("payload") && envelope["payload"].IsObject(),
       "Pack has no payload.");
  auto indexPath = inside(root, string(envelope["payload"], "file"));
  auto index = json(indexPath);
  if (!title.empty()) {
    set(index, "name", title);
    set(envelope, "title", title);
  }
  writeJson(indexPath, index);
  auto digest = hash(read(indexPath));
  auto &a = envelope.GetAllocator();
  envelope["payload"]["sha256"].SetString(digest.c_str(), a);
  writeJson(root / "pack.json", envelope);
}
std::string property(const fs::path &p, const std::string &wanted) {
  std::ifstream in(p);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto eq = line.find('=');
    if (eq == line.npos)
      continue;
    auto key = line.substr(0, eq);
    key.erase(key.find_last_not_of(" \t") + 1);
    key.erase(0, key.find_first_not_of(" \t"));
    if (key == wanted) {
      auto s = line.substr(eq + 1);
      auto start = s.find_first_not_of(" \t");
      return start == s.npos ? "" : s.substr(start);
    }
  }
  return {};
}
void wrapRaw(const fs::path &root, const fs::path &fzm,
             const std::string &title) {
  auto name = property(fzm, "InGameMapName");
  need(!name.empty(), "FZEdit project has no InGameMapName.");
  std::vector<fs::path> files;
  for (auto &e : fs::recursive_directory_iterator(root))
    if (e.is_regular_file())
      files.push_back(e.path());
  std::sort(files.begin(), files.end());
  std::string signature;
  for (auto &p : files)
    signature += fs::relative(p, root).generic_string() + "\n" +
                 hash(read(p, maxFile)) + "\n";
  auto id = "fzedit-" + hash(signature).substr(0, 24);
  rapidjson::Document d;
  d.Parse(
      R"({"format":1,"id":"","name":"","author":"Imported FZEdit project","cups":[{"id":"course","name":"","courses":["course"]}],"courses":[{"id":"course","name":"","source":""}]})");
  auto &a = d.GetAllocator();
  set(d, "id", id);
  set(d, "name", title.empty() ? name : title);
  d["cups"][0]["name"].SetString(name.c_str(), a);
  d["courses"][0]["name"].SetString(name.c_str(), a);
  auto rel = fs::relative(fzm, root).generic_string();
  d["courses"][0]["source"].SetString(rel.c_str(), a);
  writeJson(root / "courses.json", d);
  rapidjson::Document e;
  e.Parse(
      R"({"format":"snesrecomp.data-pack","version":1,"game":"f-zero","id":"","title":"","base_rom_sha256":"","requires":["fzero-course-v1"],"payload":{"format":"fzero.course-index","file":"courses.json","sha256":""}})");
  set(e, "id", id);
  set(e, "title", title.empty() ? name : title);
  set(e, "base_rom_sha256", baseHash);
  auto digest = hash(read(root / "courses.json"));
  e["payload"]["sha256"].SetString(digest.c_str(), e.GetAllocator());
  writeJson(root / "pack.json", e);
}
#ifdef _WIN32
std::wstring quote(const std::wstring &s) {
  std::wstring out = L"\"";
  unsigned slashes = 0;
  for (wchar_t c : s) {
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (c == L'"')
      out += L'\\';
    out += c;
  }
  out.append(slashes * 2, L'\\');
  return out + L'"';
}
#endif
// Helper executables carry the host's executable suffix.
fs::path helperPath(const fs::path &helpers, const char *name) {
#ifdef _WIN32
  return helpers / (std::string(name) + ".exe");
#else
  return helpers / name;
#endif
}
// Run a helper from `cwd` with stdout and stderr captured in `log`. Returns its
// exit status; a helper still running after ten minutes is killed and reports 1.
int runHelper(const fs::path &exe, const std::vector<fs::path> &args,
              const fs::path &cwd, const fs::path &log) {
  constexpr unsigned timeoutMs = 10 * 60 * 1000;
#ifdef _WIN32
  std::wstring cmd = quote(exe.wstring());
  for (auto &arg : args)
    cmd += L" " + quote(arg.wstring());
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE output =
      CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  need(output != INVALID_HANDLE_VALUE, "Cannot create conversion log.");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = output;
  startup.hStdError = output;
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, cwd.c_str(),
                                &startup, &process);
  CloseHandle(output);
  need(started != 0, "Cannot start the conversion tool.");
  CloseHandle(process.hThread);
  DWORD waited = WaitForSingleObject(process.hProcess, timeoutMs);
  DWORD code = 1;
  if (waited == WAIT_TIMEOUT)
    TerminateProcess(process.hProcess, 1);
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return (int)code;
#else
  // Build argv before fork: the child may only make async-signal-safe calls.
  std::vector<std::string> storage{exe.string()};
  for (auto &arg : args)
    storage.push_back(arg.string());
  std::vector<char *> argv;
  for (auto &arg : storage)
    argv.push_back(arg.data());
  argv.push_back(nullptr);
  std::string directory = cwd.string();
  int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  need(output >= 0, "Cannot create conversion log.");
  pid_t child = fork();
  if (child == 0) {
    // dup2 clears close-on-exec on the duplicated descriptors only.
    if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0 ||
        chdir(directory.c_str()) != 0)
      _exit(127);
    execv(argv[0], argv.data());
    _exit(127);
  }
  close(output);
  need(child > 0, "Cannot start the conversion tool.");
  int status = 0;
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    pid_t done = waitpid(child, &status, WNOHANG);
    if (done == child)
      break;
    if (done < 0 && errno != EINTR)
      return 1;
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(child, SIGKILL);
      while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
      }
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}
void convert(const fs::path &source, const fs::path &stock,
             const fs::path &helpers, const fs::path &out,
             const fs::path &reports, const fs::path &answers) {
  auto exe = helperPath(helpers, "FZeroConvertContent");
  need(fs::is_regular_file(exe),
       "Patch conversion tools are not installed in this build. See "
       "mods/CONVERSION.md for the conversion steps.");
  need(fs::is_regular_file(stock),
       "Select your original F-Zero game file on the main page first, then "
       "import the patch.");
  std::vector<fs::path> args{source,
                             "--stock",
                             stock,
                             "--out",
                             out,
                             "--exporter",
                             helperPath(helpers, "FZeroExportCourses"),
                             "--inspector",
                             helperPath(helpers, "FZeroInspectPacks")};
  if (!answers.empty()) {
    args.push_back("--answers");
    args.push_back(answers);
  }
  auto log = out.parent_path() / "conversion.log";
  int code = runHelper(exe, args, helpers, log);
  if (code != 0) {
    std::string message =
        code == 2
            ? "This course format could not be converted. No courses were installed."
            : "The patch could not be converted. No courses were installed.";
    auto report = out / "conversion-report.json";
    fs::create_directories(reports);
    if (fs::is_regular_file(log))
      fs::copy_file(log, reports / "conversion.log");
    if (fs::is_regular_file(report)) {
      auto d = json(report);
      for (const char *field : {"message", "reason"})
        if (d.HasMember(field) && d[field].IsString())
          message += "\n" + std::string(d[field].GetString());
      fs::copy_file(report, reports / "conversion-report.json");
      if (fs::is_regular_file(out / "REVIEW.txt"))
        fs::copy_file(out / "REVIEW.txt", reports / "REVIEW.txt");
    } else if (fs::is_regular_file(log)) {
      auto bytes = read(log, 1024 * 1024);
      message += "\n" + bytes.substr(0, 400);
    }
    message += "\nDetails: " + reports.string() +
               "\nSee mods/CONVERSION.md for the next steps.";
    if (code == 3 && fs::is_regular_file(reports / "conversion-report.json"))
      throw FzeroContentNeedsInput(
          "Review the detected courses before importing.",
          reports / "conversion-report.json");
    throw std::runtime_error(message);
  }
}
using MusicAnswers = std::map<std::string, std::string>;
unsigned musicNumber(const std::map<unsigned, fs::path> &songs, bool standard,
                     unsigned course) {
  unsigned number = standard ? FzeroSoundtrackNumber(course) : 10 + course;
  if (standard && !songs.count(number)) {
    number = FzeroSoundtrackFallback(course);
    if (!songs.count(number)) number = FzeroSoundtrackNative(course);
  }
  return number;
}
MusicAnswers reviewMusic(const fs::path &root, const fs::path &reports,
                         const std::string &title, const std::string &identity,
                         const std::vector<std::string> &groups,
                         const std::string &answersJson,
                         const std::string &coverage = "") {
  if (!answersJson.empty()) {
    rapidjson::Document answers;
    answers.Parse(answersJson.c_str());
    need(!answers.HasParseError() && answers.IsObject(),
         "Invalid music import answers.");
    validate_json(answers);
    need(string(answers, "input_sha256") == identity &&
             string(answers, "target_sha256") == identity,
         "This music pack changed during review. Cancel and import it again.");
    need(answers.HasMember("values") && answers["values"].IsObject(),
         "Missing music choices.");
    MusicAnswers values;
    for (auto v = answers["values"].MemberBegin();
         v != answers["values"].MemberEnd(); ++v)
      values.emplace(v->name.GetString(),
                     string(answers["values"], v->name.GetString()));
    need(values.count("pack_name") && !values["pack_name"].empty() &&
             values["pack_name"].size() < 96 &&
             std::none_of(values["pack_name"].begin(),
                          values["pack_name"].end(),
                          [](unsigned char c) { return c < 32; }),
         "Choose a soundtrack name shorter than 96 bytes.");
    need(values["music_action"] == "use" ||
             values["music_action"] == "enable" ||
             values["music_action"] == "install",
         "Choose whether to use this soundtrack after import.");
    if (!groups.empty()) {
      if (groups.size() == 1 && !values.count("music_set"))
        values["music_set"] = groups.front();
      need(std::find(groups.begin(), groups.end(), values["music_set"]) !=
               groups.end(),
           "Choose one of this download's music sets.");
      need(values["music_layout"] == "standard" ||
               values["music_layout"] == "cgp",
           "Choose a supported music layout.");
    }
    return values;
  }
  rapidjson::Document review;
  review.Parse(
      R"({"target_sha256":"","input_sha256":"","review":{"description":"","fields":[]}})");
  set(review, "target_sha256", identity);
  set(review, "input_sha256", identity);
  auto &a = review.GetAllocator();
  std::string description =
      groups.empty()
          ? "Custom F-Zero Forever music pack. Its manifest selects the "
            "original and/or custom courses and menu events it replaces."
          : "Stock and expanded F-Zero MSU-1 packs are supported. Standard "
            "numbering uses 14 for Mute City I and 6 for Big Blue; CGP "
            "numbering uses 10 and 11. Custom F-Zero Forever packs use "
            "pack.json with explicit course mappings. Choose the layout stated "
            "by the pack author; filenames alone cannot prove it.";
  description += " Recordings and loop points are preserved. Missing songs use "
                 "SNES music. Your download stays unchanged.";
  if (!coverage.empty())
    description += "\n" + coverage;
  review["review"]["description"].SetString(description.c_str(), a);
  auto add =
      [&](const char *id, const char *label, const std::string &value,
          const std::vector<std::pair<std::string, std::string>> &options) {
        rapidjson::Value field(rapidjson::kObjectType);
        field.AddMember("id", rapidjson::Value(id, a), a);
        field.AddMember("label", rapidjson::Value(label, a), a);
        field.AddMember(
            "type", rapidjson::Value(options.empty() ? "text" : "choice", a),
            a);
        field.AddMember("value", rapidjson::Value(value.c_str(), a), a);
        if (!options.empty()) {
          rapidjson::Value choices(rapidjson::kArrayType);
          for (auto &[key, name] : options) {
            rapidjson::Value choice(rapidjson::kObjectType);
            choice.AddMember("value", rapidjson::Value(key.c_str(), a), a);
            choice.AddMember("label", rapidjson::Value(name.c_str(), a), a);
            choices.PushBack(choice, a);
          }
          field.AddMember("options", choices, a);
        }
        review["review"]["fields"].PushBack(field, a);
      };
  add("pack_name", "Soundtrack name",
      title.empty() ? root.filename().string() : title, {});
  if (!groups.empty()) {
    std::vector<std::pair<std::string, std::string>> choices;
    for (auto &group : groups)
      choices.emplace_back(group,
                           fs::path(group).lexically_normal().generic_string());
    if (groups.size() > 1)
      add("music_set", "Music set", groups.front(), choices);
    add("music_layout", "Track numbering", "standard",
        {{"standard", "Standard F-Zero / BS Deluxe"},
         {"cgp", "CGP: original and BS courses"}});
  }
  add("music_action", "After import", "use",
      {{"use", "Use now (keep MSU-1 setting)"},
       {"enable", "Use now and enable MSU-1"},
       {"install", "Install only"}});
  fs::create_directories(reports);
  writeJson(reports / "conversion-report.json", review);
  throw FzeroContentNeedsInput("Review your soundtrack",
                               reports / "conversion-report.json");
}
MusicAnswers wrapMusic(const fs::path &root, const fs::path &dest,
                       const fs::path &reports, const std::string &title,
                       const std::string &answersJson,
                       const std::string &selectedPrefix) {
  std::map<std::string, std::map<unsigned, fs::path>> sets;
  std::string signature;
  std::vector<fs::path> files;
  for (auto &e : fs::recursive_directory_iterator(root))
    if (e.is_regular_file() && lower(e.path().extension().string()) == ".pcm")
      files.push_back(e.path());
  std::sort(files.begin(), files.end());
  need(!files.empty(),
       "No MSU-1 PCM recordings were found. Choose a stock MSU-1 pack, or a "
       "custom F-Zero Forever pack with pack.json.");
  const std::regex numbered(R"(^(.+)-([0-9]{1,3})$)");
  for (auto &file : files) {
    need(FzeroMenuMusicValid(file.string().c_str()),
         "Invalid PCM recording: " + file.filename().string() +
             ". Check its MSU1 header, sample size and loop point.");
    signature += fs::relative(file, root).generic_string() + "\n" +
                 hash(read(file, maxFile)) + "\n";
    std::smatch match;
    auto stem = file.stem().string();
    need(std::regex_match(stem, match, numbered),
         "Unrecognized song filename: " + file.filename().string() +
             ". Stock packs use prefix-number.pcm. Custom mappings need a "
             "F-Zero Forever pack.json.");
    auto number = unsigned(std::stoul(match[2].str()));
    need(number > 0 && number <= 255, "MSU-1 song numbers must be 1 to 255.");
    auto group = fs::relative(file.parent_path(), root).generic_string() + "/" +
                 match[1].str();
    need(group.size() < 96,
         "A music set name is too long; move the pack into a shorter folder.");
    need(sets[group].emplace(number, file).second,
         "Repeated song number in one music set.");
  }
  std::vector<std::string> groups;
  for (auto &[group, songs] : sets) {
    (void)songs;
    if (selectedPrefix.empty() ||
        lower(group).ends_with("/" + lower(selectedPrefix)))
      groups.push_back(group);
  }
  need(!groups.empty(), "No numbered PCM files match this .msu file. Keep its "
                        "original filename beside its recordings.");
  need(groups.size() <= 64, "This download contains too many music sets. "
                            "Choose one extracted set folder.");
  std::string coverage;
  if (groups.size() == 1) {
    const auto &songs = sets.at(groups.front());
    for (bool standard : {true, false}) {
      unsigned original = 0, bs = 0;
      for (unsigned n = 0; n < 25; ++n)
        if (songs.count(musicNumber(songs, standard, n))) {
          if (n < 15)
            ++original;
          else
            ++bs;
        }
      coverage += std::string(standard ? "Standard: " : " | CGP: ") +
                  std::to_string(original) + "/15 original, " +
                  std::to_string(bs) + "/10 BS courses.";
    }
  }
  auto identity = hash(signature);
  auto values = reviewMusic(root, reports, title, identity, groups, answersJson,
                            coverage);
  auto &songs = sets.at(values.at("music_set"));
  fs::create_directories(dest / "music");
  rapidjson::Document index;
  index.Parse(
      R"({"format":1,"id":"","name":"","author":"Imported soundtrack","race_music":{},"menu_music":{}})");
  auto id =
      "music-" + hash(identity + values["music_set"] + values["music_layout"])
                     .substr(0, 24);
  set(index, "id", id);
  set(index, "name", values["pack_name"]);
  auto &a = index.GetAllocator();
  std::set<unsigned> used;
  auto pathFor = [&](unsigned number) {
    auto name = "music/track-" + std::to_string(number) + ".pcm";
    if (used.insert(number).second) {
      fs::rename(songs.at(number), dest / name);
    }
    return name;
  };
  for (const char *target : {"retail", "bs-deluxe"}) {
    rapidjson::Value mappings(rapidjson::kObjectType);
    for (unsigned n = 0; n < (std::string(target) == "retail" ? 15u : 25u);
         ++n) {
      unsigned number =
          musicNumber(songs, values["music_layout"] == "standard", n);
      if (songs.count(number)) {
        auto path = pathFor(number);
        mappings.AddMember(rapidjson::Value(FzeroSoundtrackCourseId(n), a),
                           rapidjson::Value(path.c_str(), a), a);
      } else {
        auto path = "music/track-" + std::to_string(number) + ".pcm";
        mappings.AddMember(rapidjson::Value(FzeroSoundtrackCourseId(n), a),
                           rapidjson::Value(path.c_str(), a), a);
      }
    }
    if (!mappings.ObjectEmpty())
      index["race_music"].AddMember(rapidjson::Value(target, a), mappings, a);
  }
  for (auto &cue : FzeroMenuCues)
    if (songs.count(cue.command)) {
      auto path = pathFor(cue.command);
      index["menu_music"].AddMember(rapidjson::Value(cue.id, a),
                                    rapidjson::Value(path.c_str(), a), a);
    }
  need(!used.empty(), "No songs match this F-Zero layout. Custom numbering "
                      "needs an author-provided F-Zero Forever pack.json.");
  if (index["race_music"].ObjectEmpty())
    index.RemoveMember("race_music");
  writeJson(dest / "courses.json", index);
  rapidjson::Document envelope;
  envelope.Parse(
      R"({"format":"snesrecomp.data-pack","version":1,"game":"f-zero","id":"","title":"","base_rom_sha256":"","requires":["fzero-course-v1"],"payload":{"format":"fzero.course-index","file":"courses.json","sha256":""}})");
  set(envelope, "id", id);
  set(envelope, "title", values["pack_name"]);
  set(envelope, "base_rom_sha256", baseHash);
  envelope["requires"].PushBack(
      rapidjson::Value("fzero-soundtrack-v1", envelope.GetAllocator()),
      envelope.GetAllocator());
  auto digest = hash(read(dest / "courses.json"));
  envelope["payload"]["sha256"].SetString(digest.c_str(),
                                          envelope.GetAllocator());
  writeJson(dest / "pack.json", envelope);
  rapidjson::Document report;
  report.Parse(R"({"warnings":[],"music_summary":""})");
  unsigned original = 0, deluxe = 0, events = 0;
  for (auto group = index["race_music"].MemberBegin();
       group != index["race_music"].MemberEnd(); ++group)
    for (auto song = group->value.MemberBegin();
         song != group->value.MemberEnd(); ++song)
      if (fs::is_regular_file(dest / song->value.GetString())) {
        if (std::string(group->name.GetString()) == "retail")
          ++original;
        else
          ++deluxe;
      }
  events = index["menu_music"].MemberCount();
  set(report, "music_summary",
      std::to_string(original) + "/15 original courses, " +
          std::to_string(deluxe > original ? deluxe - original : 0) +
          "/10 BS courses and " + std::to_string(events) +
          "/6 menu/event songs ready. Missing songs use SNES music.");
  if (used.size() < songs.size()) {
    std::string warning =
        std::to_string(songs.size() - used.size()) +
        " recordings do not match the selected layout and were not installed. "
        "Custom courses need explicit F-Zero Forever mappings.";
    report["warnings"].PushBack(
        rapidjson::Value(warning.c_str(), report.GetAllocator()),
        report.GetAllocator());
  }
  writeJson(dest / "conversion-report.json", report);
  return values;
}
struct Stage {
  fs::path path;
  explicit Stage(fs::path p) : path(std::move(p)) {
    need(fs::create_directory(path), "Cannot create the import workspace.");
  }
  ~Stage() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
} // namespace
bool FzeroContentIsBundled(const fs::path &mods, const std::string &id) {
  auto path = mods / ".bundled-packs.json";
  if (!fs::exists(path))
    return false;
  auto index = json(path);
  need(index.HasMember("packs") && index["packs"].IsArray(),
       "The included-pack index is damaged. Restore it from your download.");
  for (const auto &pack : index["packs"].GetArray())
    if (pack.IsObject() && string(pack, "id") == id)
      return true;
  return false;
}
std::string FzeroContentRemove(const fs::path &modsInput,
                               const std::string &entryId) {
  auto relative = fs::path(reinterpret_cast<const char8_t *>(entryId.c_str()));
  safeRelative(relative);
  need(std::distance(relative.begin(), relative.end()) == 2 &&
           *relative.begin() == "packs",
       "Only installed packs can be removed here.");
  auto mods = fs::canonical(modsInput), parent = mods / "packs",
       path = parent / relative.filename();
  need(!linked(mods) && !linked(parent) && !linked(path) &&
           fs::canonical(path).parent_path() == parent,
       "Linked or external content cannot be removed here.");
  auto id = installedId(path);
  need(!id.empty() && id != "retail" && id != "bs-deluxe" &&
           !FzeroContentIsBundled(mods, id),
       "Included content cannot be removed here.");
  if (fs::is_directory(path))
    for (auto &entry : fs::recursive_directory_iterator(path)) {
      need(!linked(entry.path()) &&
               lower(entry.path().filename().string()) != ".git",
           "This folder contains linked files or a source checkout; nothing "
           "was removed.");
    }
  fs::remove_all(path);
  return id;
}
FzeroContentImportResult
FzeroContentImport(const fs::path &input, const fs::path &modsInput,
                   const fs::path &stockInput, const fs::path &helpersInput,
                   const std::string &displayName,
                   const std::string &answersJson, bool musicOnly) {
  need(displayName.size() < 96 &&
           std::none_of(displayName.begin(), displayName.end(),
                        [](unsigned char c) { return c < 32; }),
       "Choose a name shorter than 96 bytes, without line breaks.");
  need(!linked(input), "Choose the original file, not a linked file.");
  auto source = fs::canonical(input), mods = fs::absolute(modsInput),
       helpers = fs::absolute(helpersInput);
  auto stock = stockInput.empty() ? fs::path() : fs::absolute(stockInput);
  need(!linked(source), "Choose the original file, not a linked file.");
  fs::create_directories(mods / ".imports");
  need(!linked(mods) && !linked(mods / ".imports"),
       "The mods folder must not be a linked folder.");
  static std::atomic<unsigned> sequence{0};
  auto key = std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence++);
  Stage stage{mods / ".imports" / key};
  fs::path answers;
  if (!answersJson.empty()) {
    need(answersJson.size() <= 256 * 1024,
         "Import answers exceed the size limit.");
    answers = stage.path / "answers.json";
    std::ofstream output(answers, std::ios::binary);
    output.write(answersJson.data(), answersJson.size());
    need(bool(output), "Cannot save import answers.");
  }
  // Nested editor source paths otherwise exceed Windows' path limit when
  // the game itself sits in a deep build or Downloads directory.
  Stage cache{fs::temp_directory_path() / ("fzimport-" + key)};
  auto unpacked = stage.path / "unpacked";
  fs::create_directory(unpacked);
  auto extension = lower(source.extension().string());
  if (extension == ".msu")
    musicOnly = true;
  bool patch = donorExtension(extension);
  auto reports = mods / "import-reports" / key;
  if (patch) {
    convert(source, stock, helpers, unpacked / "converted", reports, answers);
  } else if (fs::is_directory(source))
    copyProject(source, unpacked);
  else if (extension == ".fzm" || extension == ".msu")
    copyProject(source.parent_path(), unpacked);
  else if (extension == ".zip") {
    // Identify the submission before extracting potentially large recordings.
    // The converter reads just the selected donor and validated assets; extra
    // documentation or tools in patch downloads are never run or installed.
    auto contents = unzip(source, {}, true);
    if (contents.donor && !contents.editor && !musicOnly)
      convert(source, stock, helpers, unpacked / "converted", reports, answers);
    else
      unzip(source, unpacked, false,
            musicOnly ||
                (contents.audio && !contents.donor && !contents.editor));
  } else
    throw std::runtime_error(
        "Choose a FZEdit project, ZIP, IPS/BPS patch, or SNES ROM hack.");
  std::vector<fs::path> manifests, projects, donors;
  bool audio = false;
  MusicAnswers musicValues;
  for (auto &e : fs::recursive_directory_iterator(unpacked))
    if (e.is_regular_file()) {
      auto ext = lower(e.path().extension().string());
      if (e.path().filename() == "pack.json")
        manifests.push_back(e.path());
      if (ext == ".fzm")
        projects.push_back(e.path());
      if (donorExtension(ext))
        donors.push_back(e.path());
      if (ext == ".pcm" || ext == ".msu")
        audio = true;
    }
  if (manifests.empty() && projects.empty() && !donors.empty() && !musicOnly) {
    need(extension == ".zip" || donors.size() == 1,
         "Choose one patch or ROM hack, or its ZIP download.");
    auto converted = stage.path / "converted";
    convert(extension == ".zip" ? source : donors.front(), stock, helpers,
            converted, reports, answers);
    manifests.push_back(converted / "pack.json");
  }
  auto catalog = stage.path / "catalog";
  fs::create_directory(catalog);
  auto candidate = catalog / "candidate";
  if (!manifests.empty()) {
    need(manifests.size() == 1,
         "This ZIP contains several packs. Import each pack separately.");
    fs::rename(manifests.front().parent_path(), candidate);
    preflight(candidate);
    if (musicOnly) {
      auto envelope = json(candidate / "pack.json");
      need(envelope.HasMember("payload") && envelope["payload"].IsObject(),
           "Music pack has no payload metadata.");
      auto payload =
          json(inside(candidate, string(envelope["payload"], "file")));
      need(!payload.HasMember("courses"),
           "This is a course pack. Use Custom Content > Import to install its "
           "courses and matching music together.");
      // Folder imports may include emulator patches beside their audio.
      for (auto &e : fs::recursive_directory_iterator(candidate))
        if (e.is_regular_file() &&
            donorExtension(lower(e.path().extension().string())))
          fs::remove(e.path());
    }
    // Verify original hashes before an optional display-name change.
    CpPack original{};
    char error[2048]{};
    bool valid = FzeroPacksValidateImport(catalog.string().c_str(),
                                          cache.path.string().c_str(),
                                          &original, error, sizeof(error));
    need(valid, error);
    if (!original.track_count) {
      auto envelope = json(candidate / "pack.json");
      auto payload = inside(candidate, string(envelope["payload"], "file"));
      auto index = json(payload);
      if (!index.HasMember("race_music") && !index.HasMember("menu_music")) {
        auto converted = catalog / "converted-music";
        musicValues = wrapMusic(
            candidate, converted, reports,
            displayName.empty() ? original.name : displayName, answersJson,
            extension == ".msu" ? source.stem().string() : "");
        fs::remove_all(
            candidate); // Only the private staged copy, never the download.
        fs::rename(converted, candidate);
      } else {
        std::string identity = hash(read(payload));
        for (auto &e : fs::recursive_directory_iterator(candidate))
          if (e.is_regular_file() &&
              lower(e.path().extension().string()) == ".pcm")
            need(FzeroMenuMusicValid(e.path().string().c_str()),
                 "A custom soundtrack contains an invalid PCM recording.");
        musicValues =
            reviewMusic(candidate, reports,
                        displayName.empty() ? original.name : displayName,
                        identity, {}, answersJson);
      }
      refreshManifest(candidate, musicValues["pack_name"]);
    } else {
      need(!musicOnly, "This is a course pack. Use Custom Content > Import to "
                       "install its courses and matching music together.");
    }
    refreshManifest(candidate, answersJson.empty() ? displayName : "");
  } else {
    if (projects.empty() && audio) {
      musicValues = wrapMusic(
          unpacked, candidate, reports,
          displayName.empty() ? source.stem().string() : displayName,
          answersJson, extension == ".msu" ? source.stem().string() : "");
    } else {
      need(!musicOnly, "Choose a stock MSU-1 music ZIP/.msu file, or a custom "
                       "F-Zero Forever music pack containing pack.json.");
      need(projects.size() == 1,
           "Choose a ZIP or folder with one FZEdit project, or a pack that "
           "includes pack.json. Multiple projects need explicit league/order "
           "metadata; see mods/CONVERSION.md.");
      auto relative = fs::relative(projects.front(), unpacked);
      fs::rename(unpacked, candidate);
      wrapRaw(candidate, candidate / relative, displayName);
      preflight(candidate);
    }
  }
  CpPack info{};
  char error[2048]{};
  bool valid = FzeroPacksValidateImport(catalog.string().c_str(),
                                        cache.path.string().c_str(), &info,
                                        error, sizeof(error));
  need(valid, error);
  need(std::string(info.id) != "retail" && std::string(info.id) != "bs-deluxe",
       "This pack uses a reserved built-in game ID. Its author must choose a "
       "unique pack ID.");
  need(!FzeroContentIsBundled(mods, info.id),
       "This pack is included with F-Zero Forever. Import cannot replace an "
       "included pack.");
  for (auto &e : fs::recursive_directory_iterator(candidate))
    if (e.is_regular_file()) {
      auto ext = lower(e.path().extension().string());
      need(!donorExtension(ext),
           "The content includes ROM or patch files alongside its assets. "
           "Import that patch directly, or remove those files from the editor "
           "project.");
    }
  auto installed = mods / "packs";
  fs::create_directories(installed);
  need(!linked(installed), "The packs folder must not be a linked folder.");
  for (auto &e : fs::directory_iterator(installed)) {
    if (e.path().filename().string().starts_with('.'))
      continue;
    need(installedId(e.path()) != info.id,
         "This pack is already installed. Importing will not overwrite it.");
  }
  // The shared transport also recognizes installed ZIPs and rejects ID
  // collisions.
  uint8_t base[32];
  cp_hash_parse(baseHash, base);
  const char *caps[] = {"fzero-course-v1", "fzero-soundtrack-v1"};
  std::unique_ptr<SnesDataPacks, decltype(&snes_data_packs_destroy)> existing(
      snes_data_packs_scan(installed.string().c_str(), "f-zero",
                           "fzero.course-index", base, caps, 2, nullptr,
                           nullptr),
      snes_data_packs_destroy);
  need(snes_data_packs_count(existing.get()) < CP_PACKS - 2,
       "The installed pack limit has been reached. Remove an unused pack "
       "before importing another.");
  for (size_t i = 0; i < snes_data_packs_count(existing.get()); ++i)
    need(std::string(snes_data_packs_get(existing.get(), i)->id) != info.id,
         "This pack is already installed as a folder or ZIP.");
  auto dest = installed / info.id;
  need(!fs::exists(dest),
       "A folder already exists for this pack. Nothing was overwritten.");
  std::string warnings;
  std::string musicSummary;
  if (fs::is_regular_file(candidate / "conversion-report.json")) {
    auto report = json(candidate / "conversion-report.json");
    if (report.HasMember("music_summary"))
      musicSummary = string(report, "music_summary");
    if (report.HasMember("warnings") && report["warnings"].IsArray())
      for (auto &warning : report["warnings"].GetArray())
        if (warning.IsString() && warnings.size() < 768) {
          if (!warnings.empty())
            warnings += "\n";
          warnings +=
              std::string(warning.GetString()).substr(0, 768 - warnings.size());
        }
  }
  std::shared_ptr<FzeroPreparedPack> prepared(
      FzeroPacksPrepareImport(candidate.string().c_str(), &info,
                              dest.string().c_str(),
                              cache.path.string().c_str()),
      FzeroPacksDiscardPrepared);
  fs::rename(candidate, dest);
  return {info.id,
          info.name,
          dest,
          info.track_count,
          manifests.empty() && musicValues.empty(),
          warnings,
          !info.track_count,
          musicValues["music_action"] != "install",
          musicValues["music_action"] == "enable",
          musicSummary,
          std::move(prepared)};
}
