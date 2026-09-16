/*
 * File: config_store.cpp | Module: CFG | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#include <Arduino.h>
#include "config_store.h"
#include "board_config.h"
#include "logging.h"
#include <LittleFS.h>

static bool mounted = false;

bool fsInit() {
  LOG("FS", "Монтирование LittleFS...");
  mounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (!mounted) {
    LOG("FS", "Ошибка. Форматирование...");
    LittleFS.format();
    mounted = LittleFS.begin(false, "/littlefs", 10, "spiffs");
  }
  if (!mounted) LOG("FS", "КРИТИЧЕСКАЯ ОШИБКА монтирования!");
  else LOG("FS", "LittleFS OK, %.2f МБ", LittleFS.totalBytes()/1048576.0);
  loggingSetFsReady(mounted);
  return mounted;
}
bool fsMounted() { return mounted; }

bool configLoad(JsonDocument& doc) {
  doc.clear();
  if (!mounted || !LittleFS.exists(CONFIG_FILE)) return false;
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) { LOG("CFG", "Ошибка разбора config: %s", e.c_str()); return false; }
  return true;
}
bool configSave(JsonDocument& doc) {
  if (!mounted) return false;
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f); f.close();
  return true;
}
float configTimezone() {
  JsonDocument d;
  if (configLoad(d) && !d["timezone"].isNull()) return d["timezone"] | DEFAULT_TZ_HOURS;
  return DEFAULT_TZ_HOURS;
}
void configSetTimezone(float tz) {
  JsonDocument d; configLoad(d);
  d["timezone"] = tz;
  configSave(d);
}