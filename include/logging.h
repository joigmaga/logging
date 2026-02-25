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

namespace logging {
  static std::thread::id main_thread_id = std::this_thread::get_id();
  //
  static bool autolog = true;
  static int autolevel = DEBUG;
  static int autostream = STDERR;
  //
  static std::mutex treemutex;
  static std::mutex filemutex;
  static std::mutex logmutex;
  static std::mutex fmtmutex;
}

// The loggerTree and Formatter class
// 
class LoggerTree;
class Formatter;

typedef std::shared_ptr<LoggerTree>     logptr_t;
typedef LoggerTree&                     logref_t;

typedef std::shared_ptr<Formatter>  fmtptr_t;
typedef Formatter&                  fmtref_t;

class Logger {
  private:
    // members
    logptr_t       treeptr;      // internal object
    // Constructor
    Logger();
    Logger(const std::string& module);
  public:
    // destructor
    ~Logger();
    // Default copy constructor required for Logger creation and passing
    Logger(Logger const&) = default;
    // Prevent assignment (note: delete functions are a C++11 feature)
    Logger& operator=(Logger const&) = default;
    // Factory functions for instatiating the class
    static Logger get_logger(int level=UNCHANGED, int stream=UNCHANGED);
    static Logger get_logger(const std::string& module,
                               int level=UNCHANGED, int stream=UNCHANGED);
    // Formatter handling
    fmtref_t get_formatter();
    void add_formatter(fmtref_t ref_formatter);
    // Message dispatch
    void log(int level, const char* format, ...);
    void critical(const char* format, ...);
    void error(const char* format, ...);
    void warning(const char* format, ...);
    void info(const char* format, ...);
    void debug(const char* format, ...);
    // Get/set current log level
    int get_loglevel();
    int set_loglevel(int level);
    int get_effective_loglevel();
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
    static bool set_autolog_level(int level);
    static bool set_autolog_streamer(int stream);
};
    
class LoggerTree {
  private:
    // local typedefs
    //
    typedef std::weak_ptr<LoggerTree>   logwptr_t;
    // Private constructors prevent instantiation from outside the class
    //
  protected:
    // members
    std::string    modname;      // Module name
    bool           isroot;       // Identifies root logger
    int            loglevel;     // Current log level
    std::ofstream* logfile;      // File stream for the log file
    std::string    filename;     // Active log file
    std::ostream*  outstream;    // Pointer to output stream
    fmtptr_t       formatter;    // Pointer to formatter 
    bool           propagate;    // Continue the search upwards to the root
    logptr_t       parent;       // logger's ancestor
    std::map<std::string, logwptr_t> dict;      // Loggers Dictionary
    // Constructor
    LoggerTree();
    LoggerTree(const std::string& module);
    // logger creation (internal)
    static logptr_t get_root_logger();
    static logptr_t get_logger_internal(bool is_root,
                                        const std::string& module);
    // Formatter manipulation using pointers (internal)
    static fmtptr_t get_def_formatter();
    // logging record creation
    int get_effective_loglevel();
    void autolog(int level, const char* format, ...);
    void logaux(int level, const char* format, va_list args);
  public:
    //
    ~LoggerTree();
    // Default copy constructor required for Logger creation and passing
    LoggerTree(LoggerTree const&) = default;
    // Prevent assignment (note: delete functions are a C++11 feature)
    LoggerTree& operator=(LoggerTree const&) = default;
    //
    friend class Logger;
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
    fmtptr_t      fmtptr;       // internal pointer used by external view
    //
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
    Formatter(Formatter const&)            = default;   // copy constructor
    Formatter& operator=(Formatter const&) = default;   // assignment
    //
    friend class LoggerTree;
    friend class Logger;
    //
    // Factory functions for instantiating the class
    static Formatter get_formatter(
                            const std::string& recfmt = DEFAULT_RECORDFMT,
                            const std::string& timefmt = DEFAULT_TIMEFMT,
                            bool eol = true);
    //
    // Formatter settings
    std::string get_timefmt();
    void set_timefmt(const std::string& timefmt);
    std::string get_recfmt();
    void set_recfmt(const std::string& recfmt);
    void set_eol(const bool eol);
};

#endif
