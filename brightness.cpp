/*
File: brightness.cpp | Module: BRIGHT | File ver: 1.1.1 | Proj ver: 3.6.1
Fix 1.1.1:
 - gamma = множитель наклона log-log; gamma=1 -> степенная кривая точно через
   опорные точки (lux_min,ma_min)-(lux_max,ma_max); экстраполяция выше lux_max.
 - ma_max больше НЕ жёсткий потолок (это вторая опорная точка / наклон).
   Реальный потолок = min(target_mA/сегм из меты активной кривой,
   максимальный ток кривой при 0% ШИМ). Ток растёт с lux без ограничения,
   пока не упрётся в потолок (0% ШИМ или target mA/сегм — что раньше).
AUTO: период 2 c, slew (половина дельты, мин 1%); при отказе BH1750 держим последнее.
MANUAL: фиксированный manual_pct. Во время калибровки ШИМ не трогаем.
*/
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <math.h>
#include "brightness.h"
#include "pwm_ctrl.h"
#include "board_config.h"
#include "config_store.h"
#include "light_bh1750.h"
#include "calibration.h"
#include "logging.h"

#define BRIGHT_PERIOD_MS 2000
#define BRIGHT_CURVE_MAX 96

static struct {
  uint8_t mode       = BRIGHT_AUTO;
  uint8_t manual_pct = 50;
  float lux_min = 0.7f, lux_max = 500.0f;   // опорные точки lux
  float ma_min  = 0.1f, ma_max  = 2.0f;    // опорные точки тока (наклон)
  float gamma   = 1.0f;                    // множитель наклона log-log
} cfgB;

static float curveD[BRIGHT_CURVE_MAX];
static float curveI[BRIGHT_CURVE_MAX];
static uint16_t curveN = 0;
static float curveTargetMa = 0.0f;   // target mA/seg из меты активной кривой
static BrightStatus st;
static uint32_t lastTick = 0;
static bool haveTarget = false;
static bool curveWarned = false;

static void syncStatus() {
  st.mode = cfgB.mode;
  st.manual_pct = cfgB.manual_pct;
  st.lux_min = cfgB.lux_min; st.lux_max = cfgB.lux_max;
  st.ma_min = cfgB.ma_min;   st.ma_max = cfgB.ma_max;
  st.gamma = cfgB.gamma;
  st.hw_percent = pwmGetPercent();
  st.curve_n = curveN;
}

static void loadCfg() {
  JsonDocument d;
  if (!configLoad(d)) return;
  JsonObject o = d["brightness"];
  if (o.isNull()) return;
  cfgB.mode       = o["mode"]       | cfgB.mode;
  cfgB.manual_pct = o["manual_pct"] | cfgB.manual_pct;
  cfgB.lux_min    = o["lux_min"]    | cfgB.lux_min;
  cfgB.lux_max    = o["lux_max"]    | cfgB.lux_max;
  cfgB.ma_min     = o["ma_min"]     | cfgB.ma_min;
  cfgB.ma_max     = o["ma_max"]     | cfgB.ma_max;
  cfgB.gamma      = o["gamma"]      | cfgB.gamma;
  if (cfgB.lux_min < 0.01f) cfgB.lux_min = 0.01f;
  if (cfgB.lux_max <= cfgB.lux_min) cfgB.lux_max = cfgB.lux_min * 10.0f;
  if (cfgB.ma_min  < 0.01f) cfgB.ma_min = 0.01f;
  if (cfgB.ma_max  <= cfgB.ma_min) cfgB.ma_max = cfgB.ma_min * 2.0f;
  if (cfgB.gamma  < 0.1f) cfgB.gamma = 0.1f;
  if (cfgB.gamma  > 4.0f) cfgB.gamma = 4.0f;
  if (cfgB.mode   > 1) cfgB.mode = 1;
}

static void saveCfg() {
  JsonDocument d;
  configLoad(d);
  JsonObject o = d["brightness"].to<JsonObject>();
  o["mode"] = cfgB.mode;
  o["manual_pct"] = cfgB.manual_pct;
  o["lux_min"] = cfgB.lux_min; o["lux_max"] = cfgB.lux_max;
  o["ma_min"] = cfgB.ma_min;   o["ma_max"] = cfgB.ma_max;
  o["gamma"] = cfgB.gamma;
  configSave(d);
}

static void loadCurve() {
  curveN = 0; curveTargetMa = 0.0f;
  JsonDocument d; configLoad(d);
  String path = d["calib"]["active_file"] | "";
  if (path.length() == 0) { st.curve_loaded = false; syncStatus(); return; }
  File f = LittleFS.open(path, "r");
  if (!f) { st.curve_loaded = false; syncStatus(); return; }
  JsonDocument cd;
  DeserializationError e = deserializeJson(cd, f);
  f.close();
  if (e) { st.curve_loaded = false; syncStatus(); return; }
  curveTargetMa = cd["meta"]["target_ma"] | 0.0f;   // FIX: читаем target из меты
  JsonArray c = cd["curve"];
  for (JsonObject p : c) {
    if (curveN >= BRIGHT_CURVE_MAX) break;
    curveD[curveN] = p["d"] | 0.0f;
    curveI[curveN] = p["i"] | 0.0f;
    curveN++;
  }
  st.curve_loaded = (curveN >= 2);
  if (st.curve_loaded) { curveWarned = false; LOG("BRIGHT", "curve loaded: %s n=%u target=%.1f", path.c_str(), curveN, curveTargetMa); }
  else if (!curveWarned) { curveWarned = true; LOG("BRIGHT", "curve NOT loaded (auto disabled)"); }
  syncStatus();
}

// FIX 1.1.1: автоматический потолок = min(target из меты, макс ток кривой при 0% ШИМ)
static float curveCeil() {
  if (curveN < 2) return 0.0f;
  float c = curveI[curveN - 1];              // макс ток кривой (мин duty)
  if (curveTargetMa > 0.0f && curveTargetMa < c) c = curveTargetMa;
  return c;
}

// mA/сегмент -> duty% по кривой (d убывает, i возрастает); -1 если кривой нет
static float dutyForMa(float ma) {
  if (curveN < 2) return -1.0f;
  if (ma <= curveI[0]) return curveD[0];
  if (ma >= curveI[curveN - 1]) return curveD[curveN - 1];
  for (uint16_t k = 1; k < curveN; k++) {
    if (ma <= curveI[k]) {
      float sp = curveI[k] - curveI[k-1];
      if (sp < 1e-6f) return curveD[k];
      float t = (ma - curveI[k-1]) / sp;
      return curveD[k-1] + t * (curveD[k] - curveD[k-1]);
    }
  }
  return curveD[curveN - 1];
}

// FIX 1.1.1: степенной закон через опорные точки + экстраполяция; потолок авто
static float luxToMa(float lux) {
  if (lux <= cfgB.lux_min) return cfgB.ma_min;
  float k = logf(cfgB.ma_max / cfgB.ma_min) / logf(cfgB.lux_max / cfgB.lux_min);
  k *= cfgB.gamma;                            // gamma=1 -> чисто через опорные точки
  float ma = cfgB.ma_min * powf(lux / cfgB.lux_min, k);
  float ceil = curveCeil();
  if (ceil > 0.0f && ma > ceil) ma = ceil;    // упираемся в 0% ШИМ или target
  return ma;
}

// плавное применение duty: половина дельты, минимум 1%
static void applyDuty(float d) {
  if (d < 0.0f) d = 0.0f;
  if (d > 100.0f) d = 100.0f;
  uint8_t cur = pwmGetPercent();
  int target = (int)(d + 0.5f);
  int delta = target - (int)cur;
  st.duty = d;
  if (delta == 0) { st.hw_percent = cur; return; }
  int step = delta / 2;
  if (step == 0) step = (delta > 0) ? 1 : -1;
  uint8_t nd = (uint8_t)((int)cur + step);
  pwmSetPercent(nd);
  st.hw_percent = nd;
}

void brightnessInit() {
  loadCfg();
  loadCurve();
  st.lux = 0; st.lux_valid = false; st.target_ma = 0; st.duty = 0;
  haveTarget = false;
  pwmSetPercent(PWM_SAFE_PERCENT);
  syncStatus();
  LOG("BRIGHT", "init mode=%s manual=%u%%", cfgB.mode ? "MANUAL" : "AUTO", cfgB.manual_pct);
}

void brightnessReload() {
  loadCfg();
  loadCurve();
}

void brightnessTick() {
  uint32_t now = millis();
  if (now - lastTick < BRIGHT_PERIOD_MS) return;
  lastTick = now;
  if (calibIsRunning()) return;   // калибровка владеет ШИМ
  syncStatus();
  if (cfgB.mode == BRIGHT_MANUAL) {
    st.target_ma = 0.0f;
    applyDuty((float)cfgB.manual_pct);
    haveTarget = true;
    return;
  }
  // AUTO
  if (bh1750Valid()) {
    st.lux = bh1750Lux();
    st.lux_valid = true;
    st.target_ma = luxToMa(st.lux);
    haveTarget = true;
  } else {
    st.lux_valid = false;   // держим последнее target_ma / duty
  }
  if (!haveTarget) return;
  if (!st.curve_loaded) { loadCurve(); if (!st.curve_loaded) return; }
  float d = dutyForMa(st.target_ma);
  if (d < 0.0f) return;
  applyDuty(d);
}

void brightnessSetHwPercent(uint8_t p) { pwmSetPercent(p); st.hw_percent = p; }
uint8_t brightnessGetHwPercent() { return pwmGetPercent(); }

void brightnessSetMode(uint8_t mode) {
  cfgB.mode = mode ? BRIGHT_MANUAL : BRIGHT_AUTO;
  saveCfg(); syncStatus();
  LOG("BRIGHT", "mode -> %s", cfgB.mode ? "MANUAL" : "AUTO");
}
uint8_t brightnessGetMode() { return cfgB.mode; }

void brightnessSetManualPercent(uint8_t p) {
  cfgB.manual_pct = p;
  saveCfg(); syncStatus();
  if (cfgB.mode == BRIGHT_MANUAL && !calibIsRunning()) applyDuty((float)p);
}

void brightnessSetParams(float lux_min, float lux_max, float ma_min, float ma_max, float gamma) {
  cfgB.lux_min = lux_min; cfgB.lux_max = lux_max;
  cfgB.ma_min = ma_min;   cfgB.ma_max = ma_max;
  cfgB.gamma = gamma;
  if (cfgB.lux_min < 0.01f) cfgB.lux_min = 0.01f;
  if (cfgB.lux_max <= cfgB.lux_min) cfgB.lux_max = cfgB.lux_min * 10.0f;
  if (cfgB.ma_min  < 0.01f) cfgB.ma_min = 0.01f;
  if (cfgB.ma_max  <= cfgB.ma_min) cfgB.ma_max = cfgB.ma_min * 2.0f;
  if (cfgB.gamma  < 0.1f) cfgB.gamma = 0.1f;
  if (cfgB.gamma  > 4.0f) cfgB.gamma = 4.0f;
  saveCfg(); syncStatus();
  LOG("BRIGHT", "trim: lux[%.2f..%.1f] mA[%.2f..%.2f] g=%.2f",
      cfgB.lux_min, cfgB.lux_max, cfgB.ma_min, cfgB.ma_max, cfgB.gamma);
}

const BrightStatus& brightnessStatus() { return st; }