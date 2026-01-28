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

#define ROOT_ALIAS   "root"

#define MAX_MODULE_NAME_SIZE 256
#define MAX_MODULE_SUBFIELDS  24 

#define MAX_RECORD_LENGTH    512

#define DEFAULT_TIMEFMT   "%Y/%m/%d:%H:%M:%S"
#define DEFAULT_RECORDFMT "%t %I[%l] %N%m"

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
  static std::mutex fmtmutex;
}

class Logger;
class Formatter;

typedef std::shared_ptr<Logger>     logptr_t;
typedef Logger& logref_t;

typedef std::shared_ptr<Formatter>  fmtptr_t;
typedef Formatter& fmtref_t;

class Logger {
  private:
    // local typedefs
    //
    typedef std::weak_ptr<Logger>   logwptr_t;
    // Private constructors prevent instantiation from outside the class
    //
  protected:
    Logger(const std::string& module);
    //
    std::string   modname;      // Module name
    std::string   alias;        // Module name alias
    int           loglevel;     // Current log level
    std::ofstream logfile;      // File stream for the log file
    std::string   filename;     // Active log file
    std::ostream* outstream;    // Pointer to output stream
    fmtptr_t      formatter;    // Pointer to formatter 
    bool          propagate;    // Continue the search upwards to the root
    logptr_t      parent;       // logger's ancestor
    std::map<std::string, logwptr_t> dict;      // Loggers Dictionary
    // logger creation (internal)
    static logptr_t get_root_logger();
    static logptr_t get_regular_logger(const std::string& module);
    // Formatter manipulation using pointers (internal)
    fmtptr_t get_formatter_ptr();
    void set_formatter_ptr(fmtptr_t formatter_ptr);
    // logging record creation
    void logaux(int level, const char* format, va_list args);
  public:
    //
    ~Logger();
    // Prevent copying (note: delete functions are a C++11 feature)
    Logger(Logger const&)         = delete;   // copy
    void operator=(Logger const&) = delete;   // assignment
    // Factory functions for instatiating the class
    // new logger reference
    static logref_t get_logger(const std::string& module,
                               int level=UNCHANGED, int stream=UNCHANGED);
    // Naming
    void set_alias(const std::string& module);
    // Formatter handling
    fmtref_t get_formatter();
    void set_formatter(fmtref_t ref_formatter);
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
};

// The Formatter class
// 

class Formatter {
  private:
    // local typedefs
    //
    typedef std::weak_ptr<Formatter> fmtwptr_t;
  protected:
    // Private constructors prevent instantiation from outside the class
    //
    Formatter(const std::string& format  = DEFAULT_RECORDFMT,
              const std::string& timefmt = DEFAULT_TIMEFMT,
              bool eol = true);
    //
    std::string   recordformat; // Output log record format
    std::string   timeformat;   // Output Time format
    bool          eol;          // Append LF to record
    bool          modifiable;   // Can be changed?
    fmtwptr_t     fmtptr;       // self pointer
    // Formatting
    //
    static std::string level_to_string(int level, bool uppercase=false);
    static std::string format_tid();
    static std::string format_pid();
    static std::string format_ppid();
    static std::string format_message(const char* msgfmt, va_list vl);
    std::string format_time();
    std::string format_record(std::string& message,
                              std::string& name, int level);
  public:
    //
    ~Formatter();
    // Prevent copying (note: delete functions are a C++11 feature)
    Formatter(Formatter const&)      = delete;   // copy
    void operator=(Formatter const&) = delete;   // assignment
    //
    friend class Logger;
    //
    // Factory functions for instantiating the class
    static fmtref_t get_formatter(const std::string& recfmt = DEFAULT_RECORDFMT,
                                  const std::string& timefmt = DEFAULT_TIMEFMT,
                                  bool eol = true);
    //
    // Formatter settings
    void set_timefmt(const std::string& timefmt);
    void set_recfmt(const std::string& recfmt);
    void set_eol(const bool eol);
};

#endif
