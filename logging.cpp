/*
A basic logging interface

    Ignacio Martinez (igmartin@movistar.es)
    January 2023

*/

#include <string.h>

#include <thread>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <sstream>

#include "logging.h"

using namespace std;

//////////// A basic logging facility 
//

// Non-local variables are kept in a separate namespace (logspace)
// Those include the main thread of execution and various mutexes
//
//   Identifies the default thread of execution
//   Must get initialized before any thread is launched
//
//     logspace::main_thread_id = this_thread::get_id()
//
//   Flag to allow debugging of internal operations using regular loggers
//
//     logspace::autolog
//
//   Mutexes are placed in a separate namespace
//   Must last until other dynamic objects are destroyed
//
//     logspace::treemutex   protect logger tree operations
//     logspace::filemutex   protect file and stream operations
//     logspace::logmutex    protect message creation and delivery
//     logspace::fmtmutex    protect formatter creation and manipulation

///////////// Interface
//
// Access to all interface methods through a reference to
// the Logger and Formatter classes
//
//   Logger& logger = Logger::get_logger("name")  // logger for entitity "name" 
//
//   or using existing type   "typedef logref_t Logger&"
//
//   logref_t logger = Logger::get_logger("name")
//
///////////// Logger class
//
// Logger instances are dynamic objects built internally using smart pointers
// for efficient memory management
// Formatter instances are member of the Logger class and are responsible for
// the final output aspect of logging records
//
// Logger and Formatter clases offer a reference based user interface
// Module duration is tied to storage duration of the returned (smart) pointer
// Loggers get instantiated using a factory function (get_logger(module))
// Loggers form a hierarchy. There is a 'root' Logger that gets instantiated
// automatically and can be modified using the empty string ("") as module name.
// Once a logger is out of scope, the object pointed to is destroyed and the
// instance tree is rebuilt
//
// User interface
//
//   logger creation:
//
//     Logger& logger = Logger::get_logger(const string& name,
//                                         int level,      // optional         
//                                         int streamer)   // optional
//
//         - the special value "" for name specifies the root logger
//         - level is the log level and can be one of:
//               NOTSET, DEBUG, INFO, WARNING, ERROR, CRITICAL, UNCHANGED
//           the special value of NOTSET prevents any logging output
//           the special value of UNCHANGED does not alter the current setting 
//         - streamer opens an output stream for the logger and can be one of:
//               STDOUT, STDERR, STDLOG, DEVNULL
//
//  formatter creation:
//
//    Formatter& formatter = Formatter::get_formatter(
//                             const string& recfmt,    // optional
//                             const string& timefmt,   // optional
//                             bool eol)                // optional
//
//         - recfmt is the record format. Expansions include:
//                 default value is "%t %I[%l] %N%m"   (DEFAULT_RECORDFMT)
//                     %t -- time format (see below)
//                     %i -- thread id (hash value)
//                     %I -- skip if main thread otherwise enclose in "()"
//                     %l -- log level as a lowercase string
//                     %L -- log level as an uppercase string
//                     %N -- logger name folowed by ": " if not empty
//                     %n -- logger name
//                     %m -- log message
//         - timefmt is the time format. Uses the strftime(3) expansions
//                 default value is "%Y/%m/%d:%H:%M:%S" (DEFAULT_TIMEFMT)
//         - eol is a flag that enables/inhibits the output of a final LF
//                 default value is 'true'
//
//  public logger methods:
//
//    - Factory functions for instatiating the class
//        static logref_t Logger::get_logger(const string& module,
//                          int level=UNCHANGED, int stream=UNCHANGED)
//    - Naming
//        void Logger::set_alias(const string& module)
//    - Formatter handling
//        fmtref_t Logger::get_formatter()
//        void Logger::set_formatter(fmtref_t ref_formatter)
//    - Message dispatch
//        void Logger::log(int level, const char* format, ...)
//        void Logger::autolog(int level, const char* format, ...)
//        void Logger::critical(const char* format, ...)
//        void Logger::error(const char* format, ...)
//        void Logger::warning(const char* format, ...)
//        void Logger::info(const char* format, ...)
//        void Logger::debug(const char* format, ...)
//    - Get/set current log level
//        int Logger::get_loglevel()
//        int Logger::set_loglevel(int level)
//    - Select log file and streamer
//        int Logger::set_logfile(const string& fname)
//        ostream* Logger::get_streamer()
//        ostream* Logger::set_streamer(int streamval)
//        ostream* Logger::set_streamer(ostream* streamer)
//    - Control tree navigation
//        bool Logger::set_propagation(bool mode)
//    - Auto log
//        static bool Logger::get_autolog()
//        static bool Logger::set_autolog(bool mode)
//
//  public logger methods:
//
//    - Factory functions for instantiating the class
//        static fmtref_t Formatter::get_formatter(
//                                  const string& recfmt = DEFAULT_RECORDFMT,
//                                  const string& timefmt = DEFAULT_TIMEFMT,
//                                  bool eol = true)
//
//    - Formatter settings
//        void Formatter::set_timefmt(const string& timefmt)
//        void Formatter::set_recfmt(const string& recfmt)
//        void Formatter::set_eol(const bool eol)
//
//  one can create logger hierarchies using names separated by '.'
//
//      logref_t logger = Logger::get_logger("myapp", WARNING, STDLOG);
//      logref_t logger = Logger::get_logger("myapp.util", DEBUG, STDERR);
//
//  in such a case, records created by 'myapp.util' will be logged at
//  'myapp.util' and 'myapp' level, with the corresponding level and stream
//  settings, while 'myapp' will use only the 'myapp' logger.
//  This can be further controlled by 'propagation' setting in the logger
//      
//  usage:
//
//    using default formatting
//
//      logref_t logger = Logger::get_logger("myapp", WARNING, STDLOG);
//      logger.log(ERROR, "I have a strong pain in my %s", "head");
//      
//    using a specific formatter
//
//      logref_t logger = Logger::get_logger("myapp", WARNING, STDLOG);
//      fmtref_t formatter = Formatter::get_formatter("%t (%i) [%l] %n %m");
//      logger.set_formatter(formatter);
//      logger.log(ERROR, "I have a strong pain in my %s", "head");

///////////////  Logger class methods
//
// Logger constructor
//
Logger::Logger(const string& module) : modname(module),
                                       alias(module),
                                       loglevel(NOTSET),
                                       outstream(nullptr),
                                       formatter(nullptr),
                                       propagate(true),
                                       parent(nullptr)       {}

// Destructor. Update loggers tree and close log file
//
Logger::~Logger() {
  logptr_t temp_ptr = nullptr;    // delay parent destruction
  bool     destok   = true;       // check for errors during destruction

  lock_guard<mutex> lock(logspace::treemutex);
  
  autolog(DEBUG, "destroying %s logging module", alias.c_str());

  if (dict.size() > 0) {
    // tree cleanup error. Object with active children is being destructed
    //
    autolog(ERROR, "logging module %s destroyed with active leaves", alias.c_str());
    // don't throw within destructor
    //
    destok = false;
  }
  else if (parent) {
    // check the pointer to 'this' instance stored in parent's instance dict
    //
    if (not parent->dict[modname].expired()) {
      // pointer should have expired since pointed instance is being destructed
      // mutex should prevent this from happening
      //
      autolog(ERROR, "a new logger has been created during destruction");
      // don't throw within destructor
      //
      destok = false;
    }
    else {
      // child is now an orphan and parent has to be updated
      //
      autolog(DEBUG, "orphaned. update parent's dictionary"); 
      parent->dict.erase(modname);
      autolog(DEBUG, "removing pointer to parent"); 
      // delay parent destruction by assigning an extra pointer to it
      // prevents parent pointer deadlock
      //
      temp_ptr = parent;
      // this will clear the pointer to the parent
      // parent destruction -if required- is delayed until this completes
      //
      parent.reset();
      autolog(DEBUG, "parent updated"); 
      // close log file
      //
      if (logfile.is_open())
        logfile.close();
      autolog(DEBUG, "tree update complete");
    }
  }
  if (destok)
    autolog(DEBUG, "%s logging module destructed", alias.c_str());
}

// Factories for root and regular loggers
//
logptr_t Logger::get_root_logger() {
// *** Used internally and exclusively for creating the root instance ***
// Instance gets created and initialized the first time this method is called
// The root instance is unique. This method always returns the same pointer
//
  static logptr_t root_logger = nullptr;

  lock_guard<mutex> lock(logspace::treemutex);

  if (not root_logger) {
    // create the root logger and its associated default formatter
    //
    logptr_t temp_root_logger(new Logger(""));
    fmtptr_t def_formatter(new Formatter);
    // add a weak pointer to itself in the formatter so it can
    // resolve back from reference to pointer
    //
    def_formatter->fmtptr = def_formatter;

    temp_root_logger.swap(root_logger);

    root_logger->set_formatter_ptr(def_formatter);
    root_logger->set_alias(ROOT_ALIAS);
  }

  return root_logger;
}

logptr_t Logger::get_regular_logger(const string& module) {
  // walk down the logging tree looking for a module match
  //
  size_t dotpos = 0;               // position of '.' character in name
  size_t exit_loop = 0;            // avoid excesive number of sub modules

  const size_t max_submod = MAX_MODULE_SUBFIELDS;
  const size_t max_modlen = MAX_MODULE_NAME_SIZE;

  // locked (treemutex)
  //
  logptr_t instance = get_root_logger();        // this is the root instance

  if (module.size() > max_modlen) {
    // Too long for a module name. Resize and log error
    instance->autolog(ERROR,
                      "exceeded maximum length (%d) for module name %s...",
                      max_modlen, module.substr(0, max_modlen).c_str());
    exit(1);
  }

  lock_guard<mutex> lock(logspace::treemutex);

  // get default formatter pointer
  //
  fmtptr_t def_formatter = instance->get_formatter_ptr();

  while(instance) {
    // find a module name token ('.' separated)
    //
    size_t pos    = module.find('.', dotpos);
    string submod = module.substr(0, pos);

    if (++exit_loop > max_submod) {
      // too many submodules. Don't do any more searching
      instance->autolog(ERROR,
                        "max number of module subfields (%d) exceeded for %s",
                        max_submod, module.c_str());
      exit(1);
    }

    instance->autolog(DEBUG, "looking for module %s in dict", submod.c_str());

    bool exists  = instance->dict.count(submod) > 0;
    bool expired = false;

    if (exists) {
      // (sub) module exists. Fetch its pointer in the dictionary
      //
      instance->autolog(DEBUG, "found existing logging instance for module %s",
                       submod.c_str());
      
      expired = instance->dict[submod].expired();
      if (expired) {
        // this thread has completed, pointer is not valid and
        // object instance will be destroyed later
        // 
        instance->autolog(DEBUG, "logging instance expired in this thread");
      }
      else {
        // obtain a shared pointer to the logging instance
        //
        instance = instance->dict[submod].lock();   // weak pointer -> shared
        instance->autolog(DEBUG,
                    "created shared pointer to instance %p", instance.get());
      }
    }
        
    if (not exists or expired) {
      // Create new shared pointer and store it in dict
      //
      logptr_t new_instance(new Logger(submod));
      instance->autolog(DEBUG,
                          "created new logging instance for module %s at %p",
                          submod.c_str(), new_instance.get());

      instance->dict[submod]  = new_instance;   // store as weak pointer
      new_instance->set_formatter_ptr(def_formatter);
                                                // use default formatter
      new_instance->parent    = instance;       // upwards pointer
      instance = new_instance;                  // instance refcount++
    }

    if (pos == string::npos)                   // end of string reached
      break;

    dotpos = pos+1;              // move one char beyond the '.'
  }

  if (not instance)
    throw runtime_error(string("null instance returned for module ") + module);

  return instance;
}

// Simple routines to get safe formatter pointer manipulation
//
fmtptr_t Logger::get_formatter_ptr() {

  lock_guard<mutex> lock(logspace::fmtmutex);

  return formatter;
}

void Logger::set_formatter_ptr(fmtptr_t formatter_ptr) {

  lock_guard<mutex> lock(logspace::fmtmutex);

  formatter = formatter_ptr;
}

///////////////// User interface - logger creation /////////////////////
//
// Logger factory
//
logref_t Logger::get_logger(const string& module, int level, int stream) {
  //
  logptr_t instance;

  if (module.empty())
    instance = get_root_logger();
  else
    instance = get_regular_logger(module);

  instance->set_loglevel(level);
  instance->set_streamer(stream);

  return *(instance.get());
}

///////////////// User interface - Attach/inspect formatter /////////////////////
//
// These routines are user facing and provide a formatter reference interface
//
void Logger::set_formatter(fmtref_t ref_formatter) {
  // attach a new formatter to this logger
  //
  lock_guard<mutex> lock(logspace::fmtmutex);

  if (ref_formatter.fmtptr.expired()) {
    autolog(ERROR, "formatter destructed while reference still in scope");
    exit(1);
  }

  formatter = ref_formatter.fmtptr.lock();
}
 
fmtref_t Logger::get_formatter() {
  // return reference to current formatter
  //
  lock_guard<mutex> lock(logspace::fmtmutex);

  return *(formatter.get());
}

void Logger::set_alias(const string& new_alias) {

  alias = new_alias;
}

///////////// Logger parameter getting/setting /////////////////
//
// get/set current log level (safe)
//
int Logger::get_loglevel() {
  // return current log level
  //
  lock_guard<mutex> lock(logspace::logmutex);
  return loglevel;
}

int Logger::set_loglevel(int level) {
  // set log level to new level and return current level
  //
  lock_guard<mutex> lock(logspace::logmutex);
  int curlevel = loglevel;
  if (level != UNCHANGED)
    loglevel = min(MAXLOG, max(MINLOG, abs(level)));

  return curlevel;
}

// set propagation mode for a logger (safe)
//
bool Logger::set_propagation(bool mode) {

  lock_guard<mutex> lock(logspace::logmutex);
  bool curmode = propagate;
  propagate = mode;

  return curmode;
}

// get/set autolog mode
//
bool Logger::get_autolog() {
  //
  lock_guard<mutex> lock(logspace::logmutex);
  
  return logspace::autolog;
}

bool Logger::set_autolog(bool mode) {
  //
  lock_guard<mutex> lock(logspace::logmutex);
  
  bool curdebug = logspace::autolog;
  logspace::autolog = mode;

  return curdebug;
}

////////////  Logging output. Files and output streams
//
// Configure the log file for a logger (safe)
//
int Logger::set_logfile(const string& fname) {
  string newfname;
  char*  errmsg = nullptr;

  // If file is provided and is different from current file, open it and
  // close existing file
  // Use absolute pathnames for file name comparison

  lock_guard<mutex> lock(logspace::filemutex);

  if (not fname.empty()) {
    char*    p;

    // Convert file path name to absolute
    p = realpath(fname.c_str(), nullptr);
    if (p) {
      newfname = string(p);
      free(p);
    }
    else {
      ofstream ofs;

      // Log file does not exist. Try to create it
      ofs.open(fname, ios::trunc); 
      if (ofs.is_open()) {
        ofs.close();
        // Try to build the path name again
        p = realpath(fname.c_str(), nullptr);
        newfname = string(p);
        free(p);
      }
      else
        errmsg = strerror(errno);
    }
  }

  if (newfname != filename) {
    // close current log file
    if (logfile.is_open())
      logfile.close();
    filename = string();

    if (not newfname.empty()) {
      // open new log file
      logfile.open(newfname, ios::app);
      if (logfile.is_open())
        filename = newfname;
      else
        errmsg = strerror(errno);
    }
  }

  if (errmsg) {
    error("error opening log file '%s': %s", fname.c_str(), errmsg);
    return 1;
  }

  return 0;
}

ostream* Logger::get_streamer() {
  // check whether an output stream is enabled (safe)
  //
  lock_guard<mutex> lock(logspace::filemutex);

  return outstream;
}

ostream* Logger::set_streamer(int streamval) {
  // select an output stream (safe)
  //
  lock_guard<mutex> lock(logspace::filemutex);

  ostream* curos = outstream;
  switch(streamval) {
    case STDOUT:     outstream = &cout;
                     break;
    case STDERR:     outstream = &cerr;
                     break;
    case STDLOG:     outstream = &clog;
                     break;
    case DEVNULL:    outstream = nullptr;
                     break;
    case UNCHANGED:
    default:         break;
  }

  return curos;
}

ostream* Logger::set_streamer(ostream* stream) {
  // set the output stream (safe)
  //
  lock_guard<mutex> lock(logspace::filemutex);

  ostream* curos = outstream;
  outstream = stream;

  return curos;
}

///////////// Message delivery interface /////////////////

void Logger::critical(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(CRITICAL, format, vl);
  va_end(vl);
} 

void Logger::error(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(ERROR, format, vl);
  va_end(vl);
} 

void Logger::warning(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(WARNING, format, vl);
  va_end(vl);
} 

void Logger::info(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(INFO, format, vl);
  va_end(vl);
} 

void Logger::debug(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(DEBUG, format, vl);
  va_end(vl);
} 

void Logger::log(int level, const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  logaux(level, format, vl);
  va_end(vl);
}

void Logger::autolog(int level, const char* format, ...) {
  va_list vl;

  if (not get_autolog())
    return;

  va_start(vl, format);

  ostream* curos = set_streamer(STDERR);
  int curlevel   = set_loglevel(DEBUG);

  logaux(level, format, vl);

  set_streamer(curos);
  set_loglevel(curlevel);
      
  va_end(vl);
}

void Logger::logaux(int level, const char* format, va_list vl) {
  string record;
  string message;
  Logger* instance;

  // lock here to prevent other threads from changing level, stream, etc
  //
  lock_guard<mutex> lock(logspace::logmutex);

  // Message formatting
  //
  message = formatter->format_message(format, vl);

  instance = this;
  while (instance) {
    string record;

    // Record formatting as a log message wrapper
    // retain original 'level' and 'modname' values across potential loggers
    //
    record = instance->formatter->format_record(message, modname, level);

    if (level >= instance->loglevel) {
      // log to stream if configured
      //
      if (instance->outstream) {
        *instance->outstream << record;
        if (instance->formatter->eol)
          *instance->outstream << endl;
      }

      // log to log file if open
      //
      if (instance->logfile.is_open()) {
        instance->logfile << record;
        if (instance->formatter->eol)
          instance->logfile << endl;
        instance->logfile.flush();
      }
    }

    if (not instance->propagate)
      break;

    instance = instance->parent.get();
  }
}
