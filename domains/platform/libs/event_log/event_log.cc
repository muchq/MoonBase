#include "domains/platform/libs/event_log/event_log.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace event_log {
namespace {

// The hour stamp both names are built from. Caddy's roller and logback's
// rolling appender both write the date this way, which is why
// log_shipper recognizes one pattern and not three.
constexpr char kHourFormat[] = "%Y-%m-%dT%H";
constexpr char kSuffix[] = ".log";

absl::Status FromErrno(std::string_view what, const std::string& path) {
  return absl::UnavailableError(absl::StrCat(what, " ", path, ": ", std::strerror(errno)));
}

// The UTC hour a time falls in, which is the unit a file is named for.
absl::Time HourOf(absl::Time when) {
  return absl::FromCivil(absl::ToCivilHour(when, absl::UTCTimeZone()), absl::UTCTimeZone());
}

}  // namespace

std::string EventLog::ActiveName(std::string_view name) { return absl::StrCat(name, kSuffix); }

std::string EventLog::RolledName(std::string_view name, absl::Time when) {
  return absl::StrCat(name, "-", absl::FormatTime(kHourFormat, HourOf(when), absl::UTCTimeZone()),
                      kSuffix);
}

absl::StatusOr<std::unique_ptr<EventLog>> EventLog::Open(std::string dir, std::string name) {
  std::error_code code;
  std::filesystem::create_directories(dir, code);
  const std::filesystem::path active = std::filesystem::path(dir) / ActiveName(name);

  // What the previous run left, if anything: its lines belong to the
  // hour it last wrote in, so that is the hour this one rolls them
  // under. An empty or absent file has no hour and takes the first
  // event's.
  std::optional<absl::Time> active_hour;
  const auto modified = std::filesystem::last_write_time(active, code);
  if (!code && std::filesystem::file_size(active, code) > 0 && !code) {
    active_hour =
        HourOf(absl::FromChrono(std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::file_clock::to_sys(modified))));
  }

  std::FILE* file = std::fopen(active.c_str(), "a");
  if (file == nullptr) return FromErrno("opening", active.string());
  return std::unique_ptr<EventLog>(
      new EventLog(std::move(dir), std::move(name), file, active_hour));
}

EventLog::EventLog(std::string dir, std::string name, std::FILE* file,
                   std::optional<absl::Time> active_hour)
    : dir_(std::move(dir)), name_(std::move(name)), file_(file), active_hour_(active_hour) {}

EventLog::~EventLog() {
  const absl::MutexLock lock(mu_);
  if (file_ != nullptr) std::fclose(file_);
}

absl::Status EventLog::Append(absl::Time when, std::string_view line) {
  const absl::MutexLock lock(mu_);
  const absl::Time hour = HourOf(when);
  if (file_ != nullptr && active_hour_.has_value() && hour > *active_hour_) {
    if (const absl::Status rolled = RollLocked(); !rolled.ok()) return rolled;
  }

  const std::string path = (std::filesystem::path(dir_) / ActiveName(name_)).string();
  // A roll that failed part way closed the active file and did not get
  // another open, so there is nothing to write through. Take one back
  // rather than carry the loss forward: the caller was told its own
  // event failed, and the events after it are not that event's to lose.
  // Whatever is opened here is empty, so it belongs to this line's hour.
  if (file_ == nullptr) {
    file_ = std::fopen(path.c_str(), "a");
    if (file_ == nullptr) return FromErrno("reopening", path);
    active_hour_.reset();
  }
  // An hour earlier than the active file's lands in it unchanged: rolling
  // backwards would name a file for an hour that has already been shipped.
  if (!active_hour_.has_value()) active_hour_ = hour;

  if (std::fwrite(line.data(), 1, line.size(), file_) != line.size() ||
      std::fputc('\n', file_) == EOF || std::fflush(file_) != 0) {
    return FromErrno("writing", path);
  }
  return absl::OkStatus();
}

absl::Status EventLog::RollLocked() {
  const std::filesystem::path dir(dir_);
  const std::filesystem::path active = dir / ActiveName(name_);

  // A run that starts and ends inside one hour rolls under a name the
  // previous run already took; -1, -2 keeps both, and the shipper's
  // pattern matches them the same way.
  std::filesystem::path rolled = dir / RolledName(name_, *active_hour_);
  const std::string stem = rolled.string().substr(0, rolled.string().size() - std::strlen(kSuffix));
  for (int n = 1; std::filesystem::exists(rolled); ++n) {
    rolled = absl::StrCat(stem, "-", n, kSuffix);
  }

  // Every return below this point leaves file_ null, which Append takes
  // as "reopen before writing" rather than as a handle — and which is
  // why Append is the only caller and checks for one first.
  if (std::fclose(file_) != 0) {
    file_ = nullptr;
    return FromErrno("closing", active.string());
  }
  file_ = nullptr;
  std::error_code code;
  std::filesystem::rename(active, rolled, code);
  if (code) {
    return absl::UnavailableError(
        absl::StrCat("rolling ", active.string(), " to ", rolled.string(), ": ", code.message()));
  }
  file_ = std::fopen(active.c_str(), "a");
  if (file_ == nullptr) return FromErrno("reopening", active.string());
  active_hour_.reset();
  return absl::OkStatus();
}

}  // namespace event_log
