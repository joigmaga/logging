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

// Non-local variables are kept in a separate namespace (logging)
// Those include the main thread of execution and various mutexes
//
//   Identifies the default thread of execution
//   Must get initialized before any thread is launched
//
//     logging::main_thread_id = this_thread::get_id()
//
//   These permit debugging of internal operations using regular loggers
//
//     logging::autolog      (default: true)
//     logging::autolevel    (default: WARNING)
//     logging::autostream   (default: STDERR)
//
//   Mutexes are placed in a separate namespace
//   Must last until other dynamic objects are destroyed
//
//     logging::treemutex   protect logger tree operations
//     logging::filemutex   protect file and stream operations
//     logging::logmutex    protect message creation and delivery
//     logging::fmtmutex    protect formatter creation and manipulation

///////////////  LoggerTree class
//
// LoggerTree is the base class used internally in the logging tree
// 
// LoggerTree constructors
//
LoggerTree::LoggerTree() : modname(ROOT_ALIAS),
                           isroot(true),
                           loglevel(WARNING),
                           outstream(&cerr),
                           formatter(nullptr),
                           propagate(true),
                           parent(nullptr)       {}

LoggerTree::LoggerTree(const string& module) : modname(module),
                                               isroot(false),
                                               loglevel(NOTSET),
                                               outstream(nullptr),
                                               formatter(nullptr),
                                               propagate(true),
                                               parent(nullptr)      {}

// Destructor. Update loggers tree
//
LoggerTree::~LoggerTree() {
  bool destok  = true;       // check for errors during destruction

  // Object tree is locked. Recursion must be prevented during destruction
  //
  lock_guard<mutex> lock(logging::treemutex);

  // logger tree instance
  //
  autolog(DEBUG, "destroying %s logging module", modname.c_str());
  //
  if (dict.size() > 0) {
    // tree cleanup error. Object with active children is being destroyed
    //
    autolog(ERROR, "logging module %s destroyed with active leaves", modname.c_str());
    // don't throw within destructor
    //
    destok = false;
  }
  else if (parent) {
    // check the pointer to 'this' instance stored in parent's instance dict
    //
    if (not parent->dict[modname].expired()) {
      // pointer should have expired since pointed instance is being destoyed
      // mutex should prevent this from happening
      //
      autolog(ERROR, "a new logger has been created during destruction");
      // don't throw within destructor
      //
      destok = false;
    }
    else {
      // child is now an orphan and parent must be updated
      //
      autolog(DEBUG, "module orphaned. update parent's dictionary"); 
      parent->dict.erase(modname);
      // close log file
      //
      if (logfile and logfile->is_open()) {
        logfile->close();
        delete logfile;
      }
      autolog(DEBUG, "tree update complete");
    }
  }
  if (destok)
    autolog(DEBUG, "%s logging module destroyed", modname.c_str());
}

fmtptr_t LoggerTree::get_def_formatter() {
  //
  static fmtptr_t default_formatter = nullptr;

  lock_guard<mutex> lock(logging::fmtmutex);

  if (default_formatter == nullptr) {
    // create the default formatter
    //
    fmtptr_t tmp_formatter(new Formatter);
    tmp_formatter.swap(default_formatter); 
  }

  return default_formatter;
}

logptr_t LoggerTree::get_root_logger() {
  // *** Used internally and exclusively for creating the root instance ***
  // Instance gets created and initialized the first time this method is called
  // The root instance is unique. This method always returns the same pointer
  //
  static logptr_t root_logger = nullptr;

  lock_guard<mutex> lock(logging::treemutex);

  if (root_logger == nullptr) {
    // create the root logger
    //
    logptr_t temp_root_logger(new LoggerTree);
    temp_root_logger.swap(root_logger);
  }

  return root_logger;
}

logptr_t LoggerTree::get_logger_internal(bool is_root, const string& module) {
  //
  // walk down the logging tree looking for a module match
  //
  size_t dotpos = 0;               // position of '.' character in name
  size_t exit_loop = 0;            // avoid excesive number of sub modules

  const size_t max_submod = MAX_MODULE_SUBFIELDS;
  const size_t max_modlen = MAX_MODULE_NAME_SIZE;

  // Always call def_formatter() prior to get_root_logger()
  // Lazy initialization
  //
  fmtptr_t def_formatter = get_def_formatter(); // default formatter
  logptr_t instance = get_root_logger();        // this is the root instance

  if (module.size() > max_modlen) {
    // Too long for a module name. Resize and log error
    instance->autolog(ERROR,
                      "exceeded maximum length (%d) for module name %s...",
                      max_modlen, module.substr(0, max_modlen).c_str());
    exit(1);
  }

  lock_guard<mutex> lock(logging::treemutex);

  while(instance and not is_root) {

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

    // logger already exists. Get current pointer
    //
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
        instance = instance->dict[submod].lock();   // weak -> shared
        instance->autolog(DEBUG,
                    "created shared pointer to instance %p", instance.get());
      }
    }
        
    // new logger. Create shared pointer
    //
    if (not exists or expired) {
      // Create new shared pointer and store it in dict
      //
      logptr_t new_instance(new LoggerTree(submod));
      instance->autolog(DEBUG,
                          "created new logging instance for module %s at %p",
                          submod.c_str(), new_instance.get());

      instance->dict[submod]  = new_instance;   // weak pointer
      new_instance->parent    = instance;       // upwards pointer
      instance = new_instance;
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
Logger::Logger()                {}
Logger::Logger(const string& s) {}
Logger::~Logger()               {}

// Logger factory
//
Logger Logger::get_logger(int level, int stream) {
  Logger logger;

  logptr_t instance = LoggerTree::get_logger_internal(true, "");

  logger.treeptr = instance;
  logger.set_loglevel(level);
  logger.set_streamer(stream);

  return logger;
}

Logger Logger::get_logger(const string& module, int level, int stream) {
  Logger logger(module);

  logptr_t instance = LoggerTree::get_logger_internal(false, module);

  logger.treeptr = instance;
  logger.set_loglevel(level);
  logger.set_streamer(stream);

  return logger;
}

///////////////// User interface - Attach/inspect formatter /////////////////////
//
// These routines are user facing and provide a formatter reference interface
//
void Logger::add_formatter(fmtref_t ref_formatter) {
  // attach a new formatter to this logger
  //
  lock_guard<mutex> lock(logging::fmtmutex);

  treeptr->formatter = fmtptr_t(&ref_formatter);
}

fmtref_t Logger::get_formatter() {
  // return reference to current formatter
  //
  lock_guard<mutex> lock(logging::fmtmutex);

  return *(treeptr->formatter.get());
}

///////////// Logger parameter getting/setting /////////////////
//
// get/set current log level (safe)
//
int Logger::get_loglevel() {
  // return current log level
  //
  lock_guard<mutex> lock(logging::logmutex);
  return treeptr->loglevel;
}

int Logger::set_loglevel(int level) {
  // set log level to new level and return current level
  //
  lock_guard<mutex> lock(logging::logmutex);
  
  int curlevel = treeptr->loglevel;
  if (level != UNCHANGED)
    treeptr->loglevel = min(MAXLOG, max(MINLOG, abs(level)));

  return curlevel;
}

int Logger::get_effective_loglevel() {

  return treeptr->get_effective_loglevel();
}

// set propagation mode for a logger (safe)
//
bool Logger::set_propagation(bool mode) {
  bool curmode;

  lock_guard<mutex> lock(logging::logmutex);

  curmode = treeptr->propagate;
  treeptr->propagate = mode;

  return curmode;
}

// get/set autolog mode
//
bool Logger::get_autolog() {
  //
  lock_guard<mutex> lock(logging::logmutex);
  
  return logging::autolog;
}

bool Logger::set_autolog(bool mode) {
  //
  lock_guard<mutex> lock(logging::logmutex);
  
  bool curdebug = logging::autolog;
  logging::autolog = mode;

  return curdebug;
}

bool Logger::set_autolog_level(int level) {
  //
  lock_guard<mutex> lock(logging::logmutex);
  
  bool curlevel = logging::autolevel;
  logging::autolevel = level;

  return curlevel;
}

bool Logger::set_autolog_streamer(int stream) {
  //
  lock_guard<mutex> lock(logging::logmutex);
  
  bool curstream = logging::autostream;
  logging::autostream = stream;

  return curstream;
}

///////////// Message delivery interface /////////////////

void Logger::critical(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(CRITICAL, format, vl);
  va_end(vl);
} 

void Logger::error(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(ERROR, format, vl);
  va_end(vl);
} 

void Logger::warning(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(WARNING, format, vl);
  va_end(vl);
} 

void Logger::info(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(INFO, format, vl);
  va_end(vl);
} 

void Logger::debug(const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(DEBUG, format, vl);
  va_end(vl);
} 

void Logger::log(int level, const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  treeptr->logaux(level, format, vl);
  va_end(vl);
}

/////////////////
//
/////////////////
int LoggerTree::get_effective_loglevel() {
  int level = NOTSET;

  lock_guard<mutex> lock(logging::logmutex);

  LoggerTree* instance = this;

  while (instance) {
    level = instance->loglevel;
    if (level != NOTSET)
      break;
    instance = instance->parent.get();
  }

  return level;
}

void LoggerTree::autolog(int level, const char* format, ...) {
  va_list vl;

  if (not Logger::get_autolog())
    return;

  va_start(vl, format);

  ostream* curos     = outstream;
  int curlevel       = loglevel;
  bool curpropagate  = propagate;

  outstream = &cerr;
  loglevel  = logging::autolevel;
  propagate = false;

  logaux(level, format, vl);

  loglevel  = curlevel;
  outstream = curos;
  propagate = curpropagate;

  va_end(vl);
}

void LoggerTree::logaux(int level, const char* format, va_list vl) {
  //
  int effective_loglevel = get_effective_loglevel();

  if (level < effective_loglevel)
    return;

  // lock here to prevent other threads from changing level, stream, etc
  //
  lock_guard<mutex> lock(logging::logmutex);

  // Message formatting
  //
  Formatter* def_formatter = get_def_formatter().get();
  Formatter* cur_formatter = formatter.get();
  if (not cur_formatter)
    cur_formatter = def_formatter;

  string message = cur_formatter->format_message(format, vl);

  LoggerTree* instance = this;

  while (instance) {
    string record;
    Formatter* lev_formatter;

    // check for the existence of stream or file handlers at this level
    //
    if (instance->outstream or 
         (instance->logfile and instance->logfile->is_open())) {
      // Record formatting as a log message wrapper
      // retain original 'level' and 'modname' values across potential loggers
      //
      lev_formatter = instance->formatter.get();
      if (not lev_formatter)
        lev_formatter = def_formatter;

      record = lev_formatter->format_record(message, modname, level);

      // log to stream if configured
      //
      if (instance->outstream) {
        *instance->outstream << record;
        if (lev_formatter->eol)
          *instance->outstream << endl;
      }
      if (instance->logfile and instance->logfile->is_open()) {
        // log to log file
        //
        *instance->logfile << record;
        if (lev_formatter->eol)
          *instance->logfile << endl;
        instance->logfile->flush();
      }
    }

    if (not instance->propagate)
      break;

    instance = instance->parent.get();
  }
}
