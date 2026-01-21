#ifndef INC_LOGGING
#define INC_LOGGING

#include <stdarg.h>

#include <memory>
#include <mutex>
#include <fstream>
#include <iostream>
#include <map>

#define UNCHANGED  (-1)

/* used for stream selection
   have nothing to do with known file descriptors
*/

#define DEVNULL    0
#define STDOUT     1
#define STDERR     2
#define STDLOG     3

#define NOTSET   0
#define DEBUG    1
#define INFO     2
#define WARNING  3
#define ERROR    4
#define CRITICAL 5
#define MINLOG   NOTSET
#define MAXLOG   CRITICAL

#define MAX_MODULE_NAME_SIZE 256
#define MAX_MODULE_SUBFIELDS  24 

#define TIMEFMT   "%Y/%m/%d:%H:%M:%S"
#define RECORDFMT "%t %I[%l] %N%m"

// The logger class
// 

namespace logspace {
  static std::thread::id main_thread_id = std::this_thread::get_id();
  //
  static bool autolog = false;
  //
  static std::mutex treemutex;
  static std::mutex filemutex;
  static std::mutex logmutex;
}

class Logger;

typedef std::shared_ptr<Logger>     logptr_t;
typedef Logger& logref_t;

class Logger {
  private:
    // local typedefs
    //
    typedef std::weak_ptr<Logger>   logwptr_t;
    // Private constructors prevent instantiation from outside the class
    //
    Logger();
    Logger(const std::string& module);
    //
    static logptr_t _get_logger();
    static logptr_t _get_logger(const std::string& module);
    //
    std::string   modname;      // Module name
    int           loglevel;     // Current log level
    std::ofstream logfile;      // File stream for the log file
    std::string   filename;     // Active log file
    std::ostream* outstream;    // Pointer to output stream
    std::string   timeformat;   // Output Time format
    std::string   recordformat; // Output log record format
    bool          propagate;    // Continue the search upwards to the root
    logptr_t      parent;       // logger's ancestor
    std::map<std::string, logwptr_t> dict;      // Loggers Dictionary
    //
    // formatting of logging records
    void logrecord(std::string& record, const char* timefmt, const char* recfmt,
                      std::string& message, std::string& name, int level);
    void logaux(int level, const char* format, va_list args);
    static std::string level_to_string(int level, bool uppercase=false);
  public:
    //
    ~Logger();
    // Prevent copying (note: delete functions are a C++11 feature)
    Logger(Logger const&)         = delete;   // copy
    void operator=(Logger const&) = delete;   // assignment
    // Factory functions for instatiating the class
    // Root logger
    static logref_t get_logger(int level=UNCHANGED, int stream=UNCHANGED);
    // Regular logger
    static logref_t get_logger(const std::string& module,
                               int level=UNCHANGED, int stream=UNCHANGED);
    // Message formatting
    void set_timefmt(const std::string& timefmt);
    void set_recfmt(const std::string& recfmt);
    // Message dispatch
    void log(int level, const char* format, ...);
    void autolog(int level, const char* format, ...);
    void critical(const char* format, ...);
    void error(const char* format, ...);
    void warning(const char* format, ...);
    void info(const char* format, ...);
    void debug(const char* format, ...);
    // Get/set current log level
    int get_loglevel();
    int set_loglevel(int level);
    // Select log file and streamer
    int set_logfile(const std::string& fname);
    std::ostream* get_streamer();
    std::ostream* set_streamer(int streamval);
    std::ostream* set_streamer(std::ostream* streamer);
    // Control tree navigation
    bool set_propagation(bool mode);
    // Auto log
    static bool get_autolog();
    static bool set_autolog(bool mode);
    // Formatting
    static std::string format_tid();
    static void format_pid(std::string& pidstr);
    static void format_ppid(std::string& ppidstr);
    static void format_time(std::string& timestr, const char* timefmt);
    static void format_message(std::string& msgstr,
                               const char* msgfmt, va_list vl);
    static void format_record(std::string& record, const char* timefmt,
                              const char* recfmt, std::string& message,
                              std::string& name, int level);
};

#endif
