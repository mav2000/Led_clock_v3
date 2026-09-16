/*
 * File: logging.h | Module: LOG | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once
#include <WString.h>

void loggingInit();
void loggingSetFsReady(bool ready);
String logTimestamp();
void logMsg(const char* tag, const char* fmt, ...);
#define LOG(tag, fmt, ...) logMsg(tag, fmt, ##__VA_ARGS__)