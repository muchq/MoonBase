#include "domains/games/apis/one_d4_worker/job.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace one_d4_worker {

absl::StatusOr<YearMonth> YearMonth::Parse(std::string_view text) {
  const auto bad = [text] {
    return absl::InvalidArgumentError(absl::StrCat("not a YYYY-MM month: ", text));
  };
  if (text.size() != 7 || text[4] != '-') return bad();

  int year = 0;
  unsigned month = 0;
  if (!absl::SimpleAtoi(text.substr(0, 4), &year)) return bad();
  if (!absl::SimpleAtoi(text.substr(5, 2), &month)) return bad();
  if (month < 1 || month > 12) return bad();
  return YearMonth{year, month};
}

std::string YearMonth::ToString() const { return absl::StrFormat("%04d-%02u", year, month); }

YearMonth YearMonth::Next() const {
  return month == 12 ? YearMonth{year + 1, 1} : YearMonth{year, month + 1};
}

int64_t YearMonth::FirstInstant() const {
  // Howard Hinnant's days_from_civil, for day 1 of the month. Shifting the
  // year to start in March puts the leap day at the end of it, which is what
  // makes the era arithmetic exact rather than nearly right.
  const int y = year - (month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return days * 86400;
}

absl::StatusOr<std::vector<YearMonth>> IndexJob::Months(std::string_view start,
                                                        std::string_view end) {
  const absl::StatusOr<YearMonth> from = YearMonth::Parse(start);
  if (!from.ok()) return from.status();
  const absl::StatusOr<YearMonth> to = YearMonth::Parse(end);
  if (!to.ok()) return to.status();
  if (*to < *from) {
    return absl::InvalidArgumentError(
        absl::StrCat("range runs backwards: ", from->ToString(), " to ", to->ToString()));
  }

  std::vector<YearMonth> months;
  for (YearMonth month = *from; !(*to < month); month = month.Next()) months.push_back(month);
  return months;
}

absl::StatusOr<std::vector<YearMonth>> IndexJob::Months() const {
  return Months(start_month, end_month);
}

}  // namespace one_d4_worker
