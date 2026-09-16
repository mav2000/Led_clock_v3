/*
 * File: pins_io.cpp | Module: PINS | File ver: 1.1.5 | Proj ver: 3.4.3
 * Fix 1.1.5: восстановлено ОПРЕДЕЛЕНИЕ pinsResetCounters()
 *            (undefined reference в clock_sync.cpp на линковке).
 */
#include <Arduino.h>
#include <sys/time.h>
#include "pins_io.h"
#include "board_config.h"

static volatile uint32_t edgeCnt = 0;
static volatile uint32_t lastEdgeMs = 0;
static bool masterOn = false;
static bool dividerHeld = false;

static void IRAM_ATTR meanderISR() {
  edgeCnt = edgeCnt + 1u;
  lastEdgeMs = millis();
}

void pinsIoInit() {
  pinMode(PIN_RESET_DIVIDER, OUTPUT); digitalWrite(PIN_RESET_DIVIDER, LOW);
  pinMode(PIN_RESET_COUNTER, OUTPUT); digitalWrite(PIN_RESET_COUNTER, LOW);
  pinMode(PIN_SET_SECOND,  OUTPUT); digitalWrite(PIN_SET_SECOND,  LOW);
  pinMode(PIN_SET_MINUTE,  OUTPUT); digitalWrite(PIN_SET_MINUTE,  LOW);
  pinMode(PIN_SET_HOUR,    OUTPUT); digitalWrite(PIN_SET_HOUR,    LOW);
  dividerHeld = false;
  lastEdgeMs = millis();
  pinsMasterDisable();
}

void pinsPulse(uint8_t pin, uint32_t ms) {
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
}

void pinsMasterAllow()   { digitalWrite(PIN_MASTER_ENABLE, LOW);  pinMode(PIN_MASTER_ENABLE, OUTPUT); masterOn = true;  }
void pinsMasterDisable() { pinMode(PIN_MASTER_ENABLE, INPUT); masterOn = false; }
bool  pinsMasterEnabled(){ return masterOn; }

void pinsMeanderInit() {
  pinMode(PIN_MEANDER, INPUT);
  lastEdgeMs = millis();
  attachInterrupt(digitalPinToInterrupt(PIN_MEANDER), meanderISR, RISING);
}
bool     pinsColonOnNow()     { return digitalRead(PIN_MEANDER) == MEANDER_COLON_ON_LEVEL; }
uint32_t pinsSecondEdgeCount(){ return edgeCnt; }
uint32_t pinsLastEdgeMs()     { return lastEdgeMs; }
bool     pinsMeanderAlive()   { return masterOn && (millis() - lastEdgeMs) < 3000; }

void pinsDividerHold()    { digitalWrite(PIN_RESET_DIVIDER, HIGH); dividerHeld = true;  }
void pinsDividerRelease() { digitalWrite(PIN_RESET_DIVIDER, LOW);  dividerHeld = false; }
bool  pinsDividerHeld()   { return dividerHeld; }

// FIX 3.4.3: определение, которого не хватало на линковке
void pinsResetCounters() {
  pinsPulse(PIN_RESET_COUNTER, 20);
  delay(30);
}

void pinsMeanderWatchdogKick() {
  lastEdgeMs = millis();
}

int32_t pinsMeanderPhaseOffsetMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint32_t nowMs = millis();
  uint32_t sysMsInSecNow = (uint32_t)(tv.tv_usec / 1000);
  int32_t delta = (int32_t)(lastEdgeMs - nowMs);
  int32_t edgeSys = (int32_t)(((int64_t)sysMsInSecNow + delta) % 1000);
  if (edgeSys < 0)   edgeSys += 1000;
  if (edgeSys > 500) edgeSys -= 1000;
  return edgeSys;
}