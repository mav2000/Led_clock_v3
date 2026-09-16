/*
 * File: power_ina226.h | Module: POWER | File ver: 1.2.0 | Proj ver: 3.3.0
 * Brief: INA226 (шунт 0.165 Ом) + диагностика шины 5V (vbus_source)
 *        + режим калибровки (частый ток / редкое напряжение) + принудительный замер.
 */
#pragma once
#include <stdint.h>

void     ina226Begin();
void     ina226Update();
bool     ina226Present();
bool     ina226Valid();
float    ina226CurrentMa();
float    ina226BusV();
float    ina226PowerW();
uint16_t ina226ShuntUv();
uint16_t ina226Cal();

// --- диагностика шины ---
const char* ina226VbusSource();      // "rail5v" | "transmitter"
const char* ina226RailStatus();      // "ok"|"warn_low"|"crit_low"|"warn_high"|"unknown"
int         ina226RailMv();

// --- режим калибровки ---
void ina226SetCalibPoll(uint32_t current_ms, uint32_t voltage_ms); // voltage_ms=0 -> off
void ina226RestorePoll();
bool ina226SampleCurrentNow(float& ma);   // принудительный мгновенный замер тока