#ifndef DOMAINS_PLATFORM_LIBS_EVENT_LOG_EVENT_LOG_H
#define DOMAINS_PLATFORM_LIBS_EVENT_LOG_EVENT_LOG_H

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace event_log {

/// A domain-event log on disk, in the one shape log_shipper recognizes
/// (#1571): one JSON object per line, appended to `<dir>/<name>.log`, and
/// rolled at the first write of a new UTC hour to
/// `<dir>/<name>-YYYY-MM-DDTHH.log`.
///
/// The two names are the whole contract with the shipper, and the
/// asymmetry is the point: its `rolledLog` pattern matches the
/// timestamped name and not the bare active one, so it ships and deletes
/// only files nothing is still writing to. Pinned from the other side in
/// //domains/platform/libs/otel_contract.
///
/// What this is not: a logging framework. Application logs go to stdout
/// under the container's json-file driver; this is for bounded, durable
/// domain events that the stats pipeline reads months later. Each line is
/// flushed as it is written — at a handful of lines an hour the syscall
/// costs nothing and a crash keeps what it had.
///
/// Rolling happens on write, so an idle hour's file waits for the next
/// event rather than for the clock; a reopen rolls whatever the last run
/// left behind. Both mean an event can sit unshipped for as long as the
/// service is quiet, which at these volumes is the right trade against a
/// timer thread.
class EventLog {
 public:
  /// Opens `dir`/`name`.log for append, creating `dir`. An existing
  /// active file keeps its lines and is rolled under the hour its last
  /// write fell in, so a restart never merges two hours into one name.
  static absl::StatusOr<std::unique_ptr<EventLog>> Open(std::string dir, std::string name);
  ~EventLog();

  EventLog(const EventLog&) = delete;
  EventLog& operator=(const EventLog&) = delete;

  /// Appends `line` — one event, no newline of its own — under `when`'s
  /// hour, rolling first if that hour is later than the active file's.
  /// A timestamp older than the active hour is appended where it is
  /// rather than rolling backwards.
  absl::Status Append(absl::Time when, std::string_view line);

  /// `<name>.log`: the file being written, which no rolled-log pattern
  /// may match.
  static std::string ActiveName(std::string_view name);
  /// `<name>-YYYY-MM-DDTHH.log` for the hour `when` falls in. A roll
  /// whose name is taken — two runs of the service in one hour each roll
  /// under it — lands on `<stem>-<n>.log` instead rather than clobbering
  /// it, which the shipper's pattern also matches.
  static std::string RolledName(std::string_view name, absl::Time when);

 private:
  EventLog(std::string dir, std::string name, std::FILE* file,
           std::optional<absl::Time> active_hour);
  absl::Status RollLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  const std::string dir_;
  const std::string name_;
  absl::Mutex mu_;
  std::FILE* file_ ABSL_GUARDED_BY(mu_);
  /// The hour the lines in the active file belong to, or nullopt while
  /// it is empty and has no hour yet.
  std::optional<absl::Time> active_hour_ ABSL_GUARDED_BY(mu_);
};

}  // namespace event_log

#endif
