#include <string.h>

#include <thread>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <sstream>

#include "logging.h"

using namespace std;

//////////// File and stream handling functions
//
// Configure the log file for a logger (safe)
//

/*
Handler::Handler(const string& name, int type, const string& filename, ...

Handler::~Handler() {}
*/

int Logger::set_logfile(const string& fname) {
  string newfname;
  char*  errmsg = nullptr;

  // If file is provided and is different from current file, open it and
  // close existing file
  // Use absolute pathnames for file name comparison

  lock_guard<mutex> lock(logging::filemutex);

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

  if (newfname != treeptr->filename) {
    // close current log file
    if (treeptr->logfile and treeptr->logfile->is_open()) {
      treeptr->logfile->close();
      delete treeptr->logfile;
    }
    treeptr->filename = string();

    if (not newfname.empty()) {
      // open new log file
      treeptr->logfile = new ofstream();
      treeptr->logfile->open(newfname, ios::app);
      if (treeptr->logfile->is_open())
        treeptr->filename = newfname;
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
  lock_guard<mutex> lock(logging::filemutex);

  return treeptr->outstream;
}

ostream* Logger::set_streamer(int streamval) {
  // select an output stream (safe)
  //
  lock_guard<mutex> lock(logging::filemutex);

  ostream* curos = treeptr->outstream;
  switch(streamval) {
    case STDOUT:     treeptr->outstream = &cout;
                     break;
    case STDERR:     treeptr->outstream = &cerr;
                     break;
    case STDLOG:     treeptr->outstream = &clog;
                     break;
    case DEVNULL:    treeptr->outstream = nullptr;
                     break;
    case UNCHANGED:
    default:         break;
  }

  return curos;
}

ostream* Logger::set_streamer(ostream* stream) {
  // set the output stream (safe)
  //
  lock_guard<mutex> lock(logging::filemutex);

  ostream* curos = treeptr->outstream;
  treeptr->outstream = stream;

  return curos;
}
