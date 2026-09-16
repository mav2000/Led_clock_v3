/*
 * File: power_ina226.cpp | Module: POWER | File ver: 1.2.0 | Proj ver: 3.3.0
 */
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "power_ina226.h"
#include "board_config.h"
#include "config_store.h"
#include "logging.h"

#define INA226_ADDR 0x40
#define REG_CURRENT 0x04
#define REG_BUS     0x02
#define REG_SHUNT   0x01
#define REG_POWER   0x03

static bool  present = false, valid = false;
static float curMa = 0, busV = 0, pwrW = 0;
static uint16_t shUv = 0, calReg = 0;
static uint32_t lastCur = 0, lastVolt = 0;
static uint32_t curPollMs = 500, voltPollMs = 500;
static uint8_t  fail = 0;

// --- диагностика шины ---
static String vbusSource = "rail5v";
static int warnLowMv = 4600, critLowMv = 4000, warnHighMv = 5600;
static uint8_t railState = 0;   // 0 ok,1 warn_low,2 crit_low,3 warn_high
static const char* RAIL_STR[4] = { "ok", "warn_low", "crit_low", "warn_high" };

static bool wr16(uint8_t r, uint16_t v) {
  Wire.beginTransmission(INA226_ADDR); Wire.write(r);
  Wire.write(v >> 8); Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}
static bool rd16(uint8_t r, uint16_t& v) {
  Wire.beginTransmission(INA226_ADDR); Wire.write(r);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(INA226_ADDR, 2) < 2) return false;
  v = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}
static uint16_t calcCal() {
  float c = 0.00512f / (INA226_CURRENT_LSB_A * SHUNT_OHM);
  if (c > 65535.0f) c = 65535.0f;
  return (uint16_t)(c + 0.5f);
}
static void loadCfg() {
  JsonDocument d;
  if (!configLoad(d)) return;
  JsonObject o = d["ina226"];
  if (o.isNull()) return;
  vbusSource  = o["vbus_source"]      | "rail5v";
  warnLowMv   = o["vbus_warn_low_mv"]  | 4600;
  critLowMv   = o["vbus_crit_low_mv"]  | 4000;
  warnHighMv  = o["vbus_warn_high_mv"] | 5600;
}
static void evalRail() {
  if (vbusSource != "rail5v" || !valid) { railState = 0; return; }
  int mv = (int)(busV * 1000.0f);
  uint8_t ns = railState;
  switch (railState) {
    case 0: if (mv < critLowMv) ns = 2; else if (mv < warnLowMv) ns = 1; else if (mv > warnHighMv) ns = 3; break;
    case 1: if (mv > warnLowMv + 100) ns = 0; else if (mv < critLowMv) ns = 2; break;
    case 2: if (mv > critLowMv + 100) ns = (mv < warnLowMv) ? 1 : 0; break;
    case 3: if (mv < warnHighMv - 100) ns = 0; break;
  }
  if (ns != railState) {
    railState = ns;
    LOG("PWR", "VBUS(%s): %d mV -> %s", vbusSource.c_str(), mv, RAIL_STR[railState]);
  }
}

void ina226Begin() {
  loadCfg();
  Wire.beginTransmission(INA226_ADDR);
  present = (Wire.endTransmission() == 0);
  if (!present) { LOG("POWER", "INA226 not found"); return; }
  calReg = calcCal();
  wr16(0x00, 0x8000); delay(10);
  wr16(0x00, 0x4000 | (3 << 9) | (5 << 6) | (5 << 3) | 0x07);  // cont, avg64, 2.116ms
  wr16(0x05, calReg);
  lastCur = lastVolt = 0;
  LOG("POWER", "INA226 ok cal=%u vbus_source=%s", calReg, vbusSource.c_str());
}

void ina226Update() {
  if (!present) return;
  uint32_t now = millis();
  uint16_t v;
  if (now - lastCur >= curPollMs) {
    lastCur = now;
    if (rd16(REG_CURRENT, v)) { curMa = (float)(int16_t)v * (INA226_CURRENT_LSB_A * 1000.0f); valid = true; fail = 0; }
    else { valid = false; if (++fail >= 5) present = false; }
  }
  if (voltPollMs > 0 && now - lastVolt >= voltPollMs) {
    lastVolt = now;
    if (rd16(REG_BUS, v))  busV = (float)(int16_t)v * 0.00125f;
    if (rd16(REG_SHUNT, v)) shUv = v;
    if (rd16(REG_POWER, v)) pwrW = (float)v * (25.0f * INA226_CURRENT_LSB_A);
    evalRail();
  }
}

bool  ina226Present()  { return present; }
bool  ina226Valid()    { return valid; }
float ina226CurrentMa(){ return curMa; }
float ina226BusV()     { return busV; }
float ina226PowerW()   { return pwrW; }
uint16_t ina226ShuntUv(){ return shUv; }
uint16_t ina226Cal()   { return calReg; }

const char* ina226VbusSource() { return vbusSource.c_str(); }
const char* ina226RailStatus() {
  if (vbusSource != "rail5v") return "unknown";
  if (!valid) return "unknown";
  return RAIL_STR[railState];
}
int ina226RailMv() { return (int)(busV * 1000.0f); }

// --- калибровка ---
void ina226SetCalibPoll(uint32_t current_ms, uint32_t voltage_ms) {
  curPollMs = current_ms ? current_ms : 20;
  voltPollMs = voltage_ms;
  lastCur = lastVolt = 0;
}
void ina226RestorePoll() { curPollMs = 500; voltPollMs = 500; lastCur = lastVolt = 0; }

bool ina226SampleCurrentNow(float& ma) {
  uint16_t v;
  if (!rd16(REG_CURRENT, v)) return false;
  ma = (float)(int16_t)v * (INA226_CURRENT_LSB_A * 1000.0f);
  curMa = ma; valid = true;
  return true;
}