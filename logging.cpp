/*
 * File: logging.cpp | Module: LOG | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#include <Arduino.h>
#include "logging.h"
#include "board_config.h"
#include <LittleFS.h>
#include <time.h>
#include <stdarg.h>

static bool fsReady = false;

void loggingInit() { fsReady = false; }
void loggingSetFsReady(bool r) { fsReady = r; }

String logTimestamp() {
  time_t now = time(nullptr);
  if (now > 31536000) {
    struct tm t; localtime_r(&now, &t);
    char b[20]; strftime(b, sizeof(b), "%H:%M:%S %d.%m", &t);
    return String(b);
  }
  return "00:00:00 00.00";
}

void logMsg(const char* tag, const char* fmt, ...) {
  char buf[256];
  va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
  String full = logTimestamp() + " [" + tag + "] " + buf + "\n";
  Serial.print(full);
  if (fsReady) {
    File f = LittleFS.open(LOG_FILE, "a");
    if (f) {
      if (f.size() > 100000) { f.close(); LittleFS.remove(LOG_FILE); f = LittleFS.open(LOG_FILE, "a"); }
      if (f) { f.print(full); f.close(); }
    }
  }
}