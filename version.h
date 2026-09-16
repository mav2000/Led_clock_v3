/*
File: version.h | Module: VERSION | File ver: 1.1.13 | Proj ver: 3.5.8
*/
#pragma once
#define PROJECT_NAME          "ESP32-C3 LED Clock"
#define PROJECT_VERSION_MAJOR 3
#define PROJECT_VERSION_MINOR 5
#define PROJECT_VERSION_PATCH 8
#define PROJECT_VERSION_SUFFIX ""
#define PROJECT_VERSION       "3.5.8"
struct ModuleVersion { const char* name; const char* ver; };
extern const ModuleVersion MODULE_VERSIONS[];
extern const int           MODULE_VERSIONS_COUNT;
const char* projectVersion();