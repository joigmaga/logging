/*
A basic logging interface

    Ignacio Martinez (igmartin@movistar.es)
    January 2023

*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <thread>
#include <ctime>
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

///////////// Interface
//
// Access to all interface methods through a reference to the Logger class
//
//   Logger& logger = Logger::get_logger()        // root_logger
//   Logger& logger = Logger::get_logger("name")  // logger for entitity "name" 
//
//   or using existing type   "typedef logref_t Logger&"
//
//   logref_t logger = Logger::get_logger("name")
//
///////////// Logger class
//
// Logger instances are dynamic objects pointed to by smart pointers 
// Offers a reference based user interface
// Module duration is tied to storage duration of the returned (smart) pointer
// Loggers get instantiated using a factory function (get_logger(module))
// Loggers form a hierarchy. There is a 'root' Logger that gets instantiated
// using get_logger(), which invokes an especial constructor
// All calls to get_logger() for the same module return the same instance 
// Once a logger is out of scope, the object pointed to is destroyed and the
// instance tree is rebuilt
//

// Logger class methods
//
// root Logger constructor (default)
// note that "" as a name is meaningless and only intended for visual purposes
// It could be used as a name by any non-root Logger eventually
//
Logger::Logger() : modname(""),
                   loglevel(WARNING),
                   outstream(nullptr),
                   timeformat(TIMEFMT),                   
                   recordformat(RECORDFMT),                   
                   propagate(false),
                   parent(nullptr)      { };

// non-root Logger constructor
//
Logger::Logger(const string& module) : modname(module),
                                       loglevel(NOTSET),
                                       outstream(nullptr),
                                       timeformat(TIMEFMT),                   
                                       recordformat(RECORDFMT),                   
                                       propagate(true),
                                       parent(nullptr)      { };

// Destructor. Update loggers tree and close log file
//
Logger::~Logger() {
  logptr_t temp_ptr = nullptr;    // delay parent destruction
  string   module   = modname;    // allow for a different root module name
  bool     destok   = true;       // check for errors during destruction

  lock_guard<mutex> lock(logspace::treemutex);
  
  if (not parent)
    module = "root";

  autolog(DEBUG, "destroying %s logging module", module.c_str());

  if (dict.size() > 0) {
    // tree cleanup error. Object with active children is being destructed
    //
    autolog(ERROR, "logging module %s destroyed with active leaves", module.c_str());
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
    autolog(DEBUG, "%s logging module destructed", module.c_str());
}

// Factories for root and regular loggers
//
logptr_t Logger::_get_logger() {
// *** Used exclusively for creating the root instance ***
// Instance gets created and initialized the first time this method is called
// The root instance is unique. This method always returns the same pointer
//
  static logptr_t root_logger = nullptr;

  lock_guard<mutex> lock(logspace::treemutex);

  if (not root_logger) {
    logptr_t temp_root_logger(new Logger);

    temp_root_logger.swap(root_logger);
  }

  return root_logger;
}

logptr_t Logger::_get_logger(const string& module) {
  // walk down the logging tree looking for a module match
  //
  size_t dotpos = 0;               // position of '.' character in name
  size_t exit_loop = 0;            // avoid excesive number of sub modules

  const size_t max_submod = MAX_MODULE_SUBFIELDS;
  const size_t max_modlen = MAX_MODULE_NAME_SIZE;

  // //logptr_t instance = nullptr;
  // //
  // //lock_guard<mutex> lock(logspace::treemutex);
  // //
  // //instance = _get_logger();         // this is the root instance

  logptr_t instance = _get_logger();         // this is the root instance

  lock_guard<mutex> lock(logspace::treemutex);

  if (module.size() > max_modlen) {
    // Too long for a module name. Resize and log error
    instance->error("exceeded maximum length (%d) for module name %s...",
                      max_modlen, module.substr(0, max_modlen).c_str());
    exit(1);
  }

  while(instance) {
    // find a module name token ('.' separated)
    //
    size_t pos    = module.find('.', dotpos);
    string submod = module.substr(0, pos);

    if (++exit_loop > max_submod) {
      // too many submodules. Don't do any more searching
      instance->error("max number of module subfields (%d) exceeded for %s",
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

      instance->dict[submod] = new_instance;   // store as weak pointer
      new_instance->parent = instance;         // upwards pointer
      instance = new_instance;                 // instance refcount++
    }

    if (pos == string::npos)                   // end of string reached
      break;

    dotpos = pos+1;              // move one char beyond the '.'
  }

  if (not instance)
    throw runtime_error(string("null instance returned for module ") + module);

  return instance;
}

///////////////// User interface - logger creation /////////////////////
//
// Factories for root and regular loggers
//
logref_t Logger::get_logger(int level, int stream) {
  //
  logptr_t root_logger = _get_logger();

  root_logger->set_loglevel(level);
  root_logger->set_streamer(stream);

  return *(root_logger.get());
}

logref_t Logger::get_logger(const string& module, int level, int stream) {
  //
  logptr_t instance = _get_logger(module);

  instance->set_loglevel(level);
  instance->set_streamer(stream);

  return *(instance.get());
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

/////////////// The formatting part ///////////////////////

// Parse the message part using format and variable parameter list
//
//void Logger::get_timefmt(const string& timefmt) {
//  // get the time format for ths logger (safe)
//  //
//  lock_guard<mutex> lock(logspace::logmutex);
//
//  return timeformat;
//}

void Logger::set_timefmt(const string& timefmt) {
  // set the time format for ths logger (safe)
  //
  lock_guard<mutex> lock(logspace::logmutex);

  timeformat = timefmt;
}
  
//void Logger::get_recfmt(const string& recfmt) {
//  // get the time format for ths logger (safe)
//  //
//  lock_guard<mutex> lock(logspace::logmutex);
//
//  return recordformat;
//}
  
void Logger::set_recfmt(const string& recfmt) {
  // set the time format for ths logger (safe)
  //
  lock_guard<mutex> lock(logspace::logmutex);

  recordformat = recfmt;
}
  
string Logger::level_to_string(int level, bool uppercase) {
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

void Logger::logrecord(string& record,
                       const char* timefmt,
                       const char* recfmt,
                       string& message,
                       string& name, int level) {

  // Create a logging record
  //
  //   Get current timestamp
  //
  char timestamp[32];
  time_t now = time(0);
  tm* timeinfo = localtime(&now);
  if (strftime(timestamp, sizeof(timestamp), timefmt, timeinfo) == 0)
    strncpy(timestamp, "time fmt error", sizeof(timestamp));
    
  string msep;
  if (name.size() > 0)
    msep = ": ";

  char tids[64];
  tids[0] = '\0';
  thread::id tid = this_thread::get_id();
  if (tid != logspace::main_thread_id)
    snprintf(tids, sizeof(tids), "(%x) ",
             (unsigned int) hash<thread::id>()(this_thread::get_id()));

  // the final record formatting
  //
  char entry[256];
  if (snprintf(entry, sizeof(entry), "%s %s[%s] %s%s%s",
        timestamp,
        tids,
        level_to_string(level).c_str(),
        name.c_str(),
        msep.c_str(),
        message.c_str()) < 0)
    snprintf(entry, sizeof(entry), "logging error: %s", strerror(errno));
    
  record = string(entry);
}  

string Logger::format_tid() {
  // Thread id formatting
  //
  char tids[64];
  tids[0] = '\0';
  //
  snprintf(tids, sizeof(tids), "%x",
         (unsigned int) hash<thread::id>()(this_thread::get_id()));

  return string(tids);
}

void Logger::format_pid(string& pidstr) {
  // pid formatting
  //
  char pids[64];
  pids[0] = '\0';

  snprintf(pids, sizeof(pids), "%i", getpid());

  pidstr = string(pids);
}

void Logger::format_ppid(string& ppidstr) {
  // Parent pid formatting
  //
  char ppids[64];
  ppids[0] = '\0';

  snprintf(ppids, sizeof(ppids), "%i", getppid());

  ppidstr = string(ppids);
}

void Logger::format_time(string& timestr, const char* timefmt) {
  //   Get current timestamp
  //
  char timestamp[64];
  time_t now = time(0);
  tm* timeinfo = localtime(&now);

  if (strftime(timestamp, sizeof(timestamp), timefmt, timeinfo) == 0)
    strncpy(timestamp, "time fmt error", sizeof(timestamp));

  timestr = string(timestamp);
}

void Logger::format_message(string& msgstr, const char* msgfmt, va_list vl) {
  // Message formatting
  // note that long messages get truncated to 1023 one-byte characters
  // (less if UTF-8 multi-byte characters are present)
  //
  char msg[1024];
  if (vsnprintf(msg, sizeof(msg), msgfmt, vl) < 0)
    snprintf(msg, sizeof(msg), "logging error: %s", strerror(errno));

  msgstr = string(msg);
}

void Logger::format_record(string& record,
                           const char* timefmt,
                           const char* recfmt,
                           string& message,
                           string& name, int level) {

  string timestamp;
  format_time(timestamp, timefmt);
  string format = recfmt;

  // the final record formatting
  //
  string pending;

  size_t i = 0;
  while(i < format.size()) {
    char c = format[i++];

    record += pending;
    pending.clear();

    pending += c;
    if (c != '%')
      continue;

    c = format[i++];
    pending += c;
    bool append_colon = false;
    bool uppercase = false;
    switch(c) {
      case 't':
      case 'T':
                 record += timestamp;
                 pending.clear();
                 break;
      case 'N':  
                 if (name.size() > 0)
                   append_colon = true; 
      case 'n':
                 record += name; 
                 if (append_colon)
                   record += ": ";
                 pending.clear();
                 break;
      case 'I':
                 if (this_thread::get_id() != logspace::main_thread_id)
                   record += "(" + format_tid() + ") ";
                 pending.clear();
                 break;
      case 'i':
                 record += format_tid();
                 pending.clear();
                 break;
      case 'L':
                 uppercase = true;
      case 'l':
                 record += level_to_string(level, uppercase);
                 pending.clear();
                 break;
      case 'M':
      case 'm':
                 record += message;
                 pending.clear();
                 break;
      case '%':
                 record += '%';
                 pending.clear();
                 break;                 
    }
  }
  record += pending;
}    

void Logger::logaux(int level, const char* format, va_list vl) {
  string message;
  Logger* instance;

  // lock here to prevent other threads from changing level, stream, etc
  //
  lock_guard<mutex> lock(logspace::logmutex);

  // Message formatting
  //
  format_message(message, format, vl);

  instance = this;
  while (instance) {
    string record;
    // safe reads protected by mutex
    const char* timefmt = instance->timeformat.c_str();
    const char* recfmt = instance->recordformat.c_str();

    // Record formatting as a log message wrapper
    // retain original 'level' and 'modname' values across potential loggers
    //
    format_record(record, timefmt, recfmt, message, modname, level);

    if (level >= instance->loglevel) {
      // log to stream if configured
      //
      if (instance->outstream) {
        *instance->outstream << record << endl;
      }

      // log to log file if open
      //
      if (instance->logfile.is_open()) {
        instance->logfile << record << endl;
        instance->logfile.flush();
      }
    }

    if (not instance->propagate)
      break;

    instance = instance->parent.get();
  }
}
