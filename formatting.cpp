#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <thread>
#include <ctime>
#include <cstdarg>

#include "logging.h"

using namespace std;

Formatter::Formatter(const string& recfmt, const string& timefmt, bool eol) :
                         recordformat(recfmt),
                         timeformat(timefmt),
                         eol(eol),
                         fmtptr(nullptr) {}

Formatter::~Formatter() {}

// Factory for creating formatters
//
Formatter Formatter::get_formatter(const string& recfmt,
                                   const string& timefmt,
                                   bool eol) {
  // Formatters are anonymous abjects
  //
  Formatter fmt;
  //
  fmtptr_t formatter(new Formatter(recfmt, timefmt, eol));

  fmt = *(formatter.get());
  fmt.fmtptr = formatter;

  return fmt;
}

string Formatter::get_timefmt() {
  // get the time format for this logger (safe)
  //
  lock_guard<mutex> lock(logging::logmutex);

  return timeformat;
}
  
void Formatter::set_timefmt(const string& timefmt) {
  // set the time format for this logger (safe)
  //
  lock_guard<mutex> lock(logging::logmutex);

  timeformat = string(timefmt);
}
  
string Formatter::get_recfmt() {
  // get the time format for ths logger (safe)
  //
  lock_guard<mutex> lock(logging::logmutex);

  return recordformat;
}
  
void Formatter::set_recfmt(const string& recfmt) {
  // set the time format for ths logger (safe)
  //
  lock_guard<mutex> lock(logging::logmutex);

  recordformat = string(recfmt);
}
  
void Formatter::set_eol(const bool new_eol) {
  // end of line setting (safe)
  //
  lock_guard<mutex> lock(logging::logmutex);

  eol = new_eol;
}
  
// Log record formatting functions
//
string Formatter::level_to_string(int level, bool uppercase) {
  switch (level) {
    case NOTSET:   return uppercase ? "UNSET"    : "unset";
    case DEBUG:    return uppercase ? "DEBUG"    : "debug";
    case INFO:     return uppercase ? "INFO"     : "info";
    case WARNING:  return uppercase ? "WARNING"  : "warning";
    case ERROR:    return uppercase ? "ERROR"    : "error";
    case CRITICAL: return uppercase ? "CRITICAL" : "critical";
    default:       return uppercase ? "UNKNOWN"  : "unknown";
  }
}

string Formatter::format_tid() {
  // Thread id formatting
  //
  char tids[64];
  tids[0] = '\0';
  //
  snprintf(tids, sizeof(tids), "%x",
         (unsigned int) hash<thread::id>()(this_thread::get_id()));

  return string(tids);
}

string Formatter::format_pid() {
  // pid formatting
  //
  char pids[64];
  pids[0] = '\0';

  snprintf(pids, sizeof(pids), "%i", getpid());

  return string(pids);
}

string Formatter::format_ppid() {
  // Parent pid formatting
  //
  char ppids[64];
  ppids[0] = '\0';

  snprintf(ppids, sizeof(ppids), "%i", getppid());

  return string(ppids);
}

string Formatter::format_time() {
  //   Get current timestamp
  //
  char timestamp[64];
  time_t now = time(0);

  if (strftime(timestamp, sizeof(timestamp),
               timeformat.c_str(), localtime(&now)) == 0)
    strncpy(timestamp, "time fmt error", sizeof(timestamp));

  return string(timestamp);
}

string Formatter::format_message(const char* msgfmt, va_list vl) {
  // Message formatting
  // note that long messages get truncated to 1023 one-byte characters
  // (less if UTF-8 multi-byte characters are present)
  //
  char msg[1024];
  if (vsnprintf(msg, sizeof(msg), msgfmt, vl) < 0)
    snprintf(msg, sizeof(msg), "logging error: %s", strerror(errno));

  return string(msg);
}

string Formatter::format_record(string& message, string& name, int level) {
  //
  string record;
  string timestamp;
  timestamp = format_time();

  // the final record formatting
  //
  auto s_append = [](string& r, const string& s, size_t max) {
                                if (r.size() > max)
                                  r.resize(max);
                                else if (r.size() < max)
                                  r += s.substr(0, max - r.size()); };
  auto c_append = [](string& r, const char c) { if (r.size() < 4)
                                                  r += c; };
  string pending;

  size_t i = 0;
  while(i < recordformat.size()) {
    char c = recordformat[i++];
    c_append(pending, c);

    if (i >= recordformat.size() or c != '%') {
      s_append(record, pending, MAX_RECORD_LENGTH);
      pending.clear();
      continue;
    }

    c = recordformat[i++];
    c_append(pending, c);

    bool append_colon = false;
    bool uppercase    = false;

    switch(c) {
      case 't':
      case 'T':
                 s_append(record, timestamp, MAX_RECORD_LENGTH);
                 break;
      case 'N':  
                 if (name.size() > 0)
                   append_colon = true; 
      case 'n':
                 s_append(record, name, MAX_RECORD_LENGTH);
                 if (append_colon)
                   record += ": ";
                 break;
      case 'I':
                 if (this_thread::get_id() != logging::main_thread_id)
                   s_append(record, "(" + format_tid() + ") ", MAX_RECORD_LENGTH);
                 break;
      case 'i':
                 s_append(record, format_tid(), MAX_RECORD_LENGTH);
                 break;
      case 'P':
                 s_append(record, format_ppid(), MAX_RECORD_LENGTH);
                 break;
      case 'p':
                 s_append(record, format_pid(), MAX_RECORD_LENGTH);
                 break;
      case 'L':
                 uppercase = true;
      case 'l':
                 s_append(record, level_to_string(level, uppercase), MAX_RECORD_LENGTH);
                 break;
      case 'M':
      case 'm':
                 s_append(record, message, MAX_RECORD_LENGTH);
                 break;
      case '%':
                 s_append(record, "%", MAX_RECORD_LENGTH);
                 break;                 
      default:
                 s_append(record, pending, MAX_RECORD_LENGTH);
                 break;
    }
    pending.clear();
  }
  s_append(record, pending, MAX_RECORD_LENGTH);

  return record;
}    

