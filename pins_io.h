/*
 * File: pins_io.h | Module: PINS | File ver: 1.1.5 | Proj ver: 3.4.3
 * Fix 1.1.5: гарантированно объявлен pinsResetCounters() (использует clock_sync).
 */
#pragma once
#include <stdint.h>

void     pinsIoInit();
void     pinsPulse(uint8_t pin, uint32_t ms);

void     pinsMasterAllow();
void     pinsMasterDisable();
bool     pinsMasterEnabled();

void     pinsMeanderInit();
bool     pinsColonOnNow();
uint32_t pinsSecondEdgeCount();
uint32_t pinsLastEdgeMs();
bool     pinsMeanderAlive();

void     pinsDividerHold();
void     pinsDividerRelease();
bool     pinsDividerHeld();
void     pinsResetCounters();          // импульс Reset_clock_counter (сброс счётчиков)
void     pinsMeanderWatchdogKick();
int32_t  pinsMeanderPhaseOffsetMs();