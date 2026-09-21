/*
File: version.cpp | Module: VERSION | File ver: 1.1.13 | Proj ver: 3.5.8
*/
#include "version.h"
const ModuleVersion MODULE_VERSIONS[] = {
  { "logging",      "1.0.0" },
  { "config_store", "1.0.0" },
  { "rtc_ds3231",   "1.1.4" },
  { "light_bh1750", "1.0.0" },
  { "power_ina226", "1.2.0" },
  { "pins_io",      "1.1.4" },
  { "pwm_ctrl",     "1.2.1" },
  { "safety",       "1.1.4" },
  { "display_model","1.0.0" },
  { "wifi_mgr",     "1.0.0" },
  { "clock_sync",   "1.2.0" },
  { "brightness",   "1.1.0" },
  { "calibration",  "1.1.2" },
  { "web_server",   "1.2.8" },
};
const int MODULE_VERSIONS_COUNT = sizeof(MODULE_VERSIONS)/sizeof(MODULE_VERSIONS[0]);
const char* projectVersion(){ return PROJECT_VERSION; }

