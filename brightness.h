/*
File: brightness.h | Module: BRIGHT | File ver: 1.1.1 | Proj ver: 3.6.1
Fix 1.1.1: ma_max — только вторая ОПОРНАЯ точка (наклон), НЕ потолок.
       Потолок = min(target_mA/сегм из меты кривой, макс ток кривой при 0% ШИМ).
       gamma — множитель наклона log-log (1 = чисто через опорные точки).
*/
#pragma once
#include <stdint.h>

#define BRIGHT_AUTO     0
#define BRIGHT_MANUAL   1

struct BrightStatus {
  uint8_t  mode;          // BRIGHT_AUTO / BRIGHT_MANUAL
  bool     lux_valid;
  float    lux;
  float    target_ma;     // целевой ток сегмента (AUTO)
  float    duty;          // целевой duty %
  uint8_t  hw_percent;    // фактический ШИМ %
  bool     curve_loaded;
  uint16_t curve_n;
  uint8_t  manual_pct;
  float    lux_min, lux_max;   // опорные точки по lux
  float    ma_min, ma_max;     // опорные точки по току (ma_max = наклон, НЕ потолок)
  float    gamma;              // множитель наклона log-log (1 = нейтрально)
};

void brightnessInit();
void brightnessTick();
void brightnessReload();                       // перечитать конфиг + активную кривую
void brightnessSetHwPercent(uint8_t p);
uint8_t brightnessGetHwPercent();
void brightnessSetMode(uint8_t mode);          // 0=AUTO, 1=MANUAL
uint8_t brightnessGetMode();
void brightnessSetManualPercent(uint8_t p);
void brightnessSetParams(float lux_min, float lux_max, float ma_min, float ma_max, float gamma);
const BrightStatus& brightnessStatus();