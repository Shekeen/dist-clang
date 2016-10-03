#include <base/logging.h>

#include <base/const_string.h>

#include STL(cctype)
#include STL(iostream)

#if !defined(OS_WIN)
#include <syslog.h>
#endif

#include <base/using_log.h>

namespace dist_clang {
namespace base {

// static
void Log::SetMode(Mode mode) {
#if !defined(OS_WIN)
  if (Log::mode() == CONSOLE && mode == SYSLOG) {
    openlog(nullptr, 0, LOG_DAEMON);
  } else if (Log::mode() == SYSLOG && mode == CONSOLE) {
    closelog();
  }
#else
// TODO: implement syslog alternative on Windows.
#endif  // !defined(OS_WIN)

  Log::mode() = mode;
}

// static
void Log::Reset(ui32 error_mark, RangeSet&& ranges) {
  ui32 prev = 0;
  for (const auto& range : ranges) {
    if ((prev > 0 && range.second <= prev) || range.second > range.first) {
      // FIXME: there should be NOTREACHED(), but it will add dependency on the
      // |assert_*.cc| part of the base target.
      return;
    }
    prev = range.first;
  }

  Log::error_mark() = error_mark;
  Log::ranges().reset(new RangeSet(std::move(ranges)));
}

// static
void Log::Reset(ui32 error_mark, RangeSet&& ranges, const String& format) {
  ui32 prev = 0;
  for (const auto& range : ranges) {
    if ((prev > 0 && range.second <= prev) || range.second > range.first) {
      // FIXME: there should be NOTREACHED(), but it will add dependency on the
      // |assert_*.cc| part of the base target.
      return;
    }
    prev = range.first;
  }

  Log::error_mark() = error_mark;
  Log::ranges().reset(new RangeSet(std::move(ranges)));
  Log::formatter() = Log::Formatter(format);
}

Log::Log(ui32 level, String&& file, int line)
    : level_(level),
      error_mark_(error_mark()),
      ranges_(ranges()),
      file_(std::move(file)),
      line_(line),
      time_(std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now())),
      mode_(mode()) {}

Log::~Log() {
  auto it = ranges_->lower_bound(std::make_pair(level_, 0));
  if ((it != ranges_->end() && level_ >= it->second) ||
      level_ == named_levels::ASSERT) {
    stream_ << std::endl;

    String log_prefix = formatter().format(*this);

    if (mode_ == CONSOLE) {
      auto& output_stream = (level_ <= error_mark_) ? std::cerr : std::cout;
      output_stream << log_prefix << stream_.str() << std::flush;
    } else if (mode_ == SYSLOG) {
#if !defined(OS_WIN)
      // FIXME: not really a fair mapping.
      switch (level_) {
        case named_levels::FATAL:
        case named_levels::ASSERT:
          syslog(LOG_CRIT, "%s%s", log_prefix.c_str(), stream_.str().c_str());
          break;

        case named_levels::ERROR:
          syslog(LOG_ERR, "%s%s", log_prefix.c_str(), stream_.str().c_str());
          break;

        case named_levels::WARNING:
          syslog(LOG_WARNING, "%s%s", log_prefix.c_str(),
                 stream_.str().c_str());
          break;

        case named_levels::INFO:
          syslog(LOG_NOTICE, "%s%s", log_prefix.c_str(), stream_.str().c_str());
          break;

        default:
          syslog(LOG_INFO, "%s%s", log_prefix.c_str(), stream_.str().c_str());
      }
#endif  // !defined(OS_WIN)
    }
  }

  if (level_ == named_levels::FATAL) {
    exit(1);
  }

  if (level_ == named_levels::ASSERT) {
    std::abort();
  }
}

Log& Log::operator<<(std::ostream& (*func)(std::ostream&)) {
  stream_ << func;
  return *this;
}

ui32 Log::level() const {
  return level_;
}

const String& Log::file() const {
  return file_;
}

int Log::line() const {
  return line_;
}

const std::time_t& Log::time() const {
  return time_;
}

HashMap<String, Log::Formatter::LogVar> Log::Formatter::log_vars_{
    {"level", LEVEL}, {"file", FILE}, {"line", LINE}, {"datetime", DATETIME},
};

Log::Formatter::Formatter(const String& format)
    : log_entry_chunks_(), logvar_chunks_() {
  size_t prev_index = 0;
  size_t cur_index = 0;
  size_t keyword_index = 0;
  while (cur_index < format.length()) {
    if (format[cur_index] == '%') {
      keyword_index = cur_index + 1;
      while (keyword_index < format.length() &&
             std::isalpha(format[keyword_index])) {
        ++keyword_index;
      }
      if (cur_index + 1 < keyword_index) {
        if (cur_index > prev_index) {
          log_entry_chunks_.emplace_back(
              String(format, prev_index, cur_index - prev_index));
        }
        log_entry_chunks_.emplace_back(
            String(format, cur_index, keyword_index - cur_index));
        prev_index = cur_index = keyword_index;
      }
    }
    ++cur_index;
  }
  if (cur_index > prev_index) {
    log_entry_chunks_.emplace_back(
        String(format, prev_index, cur_index - prev_index));
  }

  logvar_chunks_.resize(log_entry_chunks_.size());
  for (size_t i = 0; i < log_entry_chunks_.size(); ++i) {
    if (log_entry_chunks_[i][0] == '%') {
      String log_var(log_entry_chunks_[i], 1);
      auto log_vars_entry = log_vars_.find(log_var);
      if (log_vars_entry != log_vars_.end()) {
        logvar_chunks_[i] = log_vars_entry->second;
      } else {
        logvar_chunks_[i] = LOGVAR_NONE;
      }
    }
  }
}

String Log::Formatter::format(const Log& log) {
  std::stringstream log_prefix_stream;
  for (size_t i = 0; i < log_entry_chunks_.size(); ++i) {
    if (logvar_chunks_[i] != LOGVAR_NONE) {
      switch (logvar_chunks_[i]) {
        case LEVEL:
          log_prefix_stream << log.level();
          break;
        case FILE:
          log_prefix_stream << log.file();
          break;
        case LINE:
          log_prefix_stream << log.line();
          break;
        case DATETIME:
          log_prefix_stream << std::ctime(&log.time());
          break;
        default:
          break;
      }
    } else {
      log_prefix_stream << log_entry_chunks_[i];
    }
  }
  log_prefix_stream << " ";
  return log_prefix_stream.str();
}

// static
ui32& Log::error_mark() {
  static ui32 error_mark = 0;
  return error_mark;
}

// static
SharedPtr<Log::RangeSet>& Log::ranges() {
  static SharedPtr<RangeSet> ranges(
      new RangeSet{std::make_pair(named_levels::FATAL, named_levels::FATAL)});
  return ranges;
}

// static
Log::Mode& Log::mode() {
  static Mode mode = CONSOLE;
  return mode;
}

// static
Log::Formatter& Log::formatter() {
  static Formatter formatter("[%level]");
  return formatter;
}

}  // namespace base
}  // namespace dist_clang
