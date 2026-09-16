/*
 * File: config_store.h | Module: CFG | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once
#include <ArduinoJson.h>

bool  fsInit();
bool  fsMounted();
bool  configLoad(JsonDocument& doc);
bool  configSave(JsonDocument& doc);
float configTimezone();
void  configSetTimezone(float tz);