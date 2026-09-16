/*
 * File: clock_sync.cpp | Module: CSYNC | File ver: 1.2.1 | Proj ver: 3.5.1
 * Fix 1.2.1: возвращена ветка самолечения конвенции RTC в doNtpSync()
 *            (|corr - TZ| <= 2 c -> RTC был UTC -> перезапись в local, corr=0).
 *            Определён clockSyncRequestResync(). Guard calibBusy() сохранён.
 */
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "clock_sync.h"
#include "board_config.h"
#include "config_store.h"
#include "rtc_ds3231.h"
#include "wifi_mgr.h"
#include "pins_io.h"
#include "safety.h"
#include "calibration.h"
#include "logging.h"

enum CsState { CS_IDLE, CS_HOLD, CS_SET, CS_WAIT, CS_VERIFY, CS_EXIT };
static CsState  csState = CS_IDLE;
static bool     csPending = false;
static uint32_t csT = 0;
static uint64_t csTargetMs = 0;
static uint32_t csEdge = 0;
static uint8_t  csH = 0, csM = 0, csS = 0;

static float    lastCorr = 0;
static uint32_t lastRtcWrite = 0;
static int      lastNtpDay = -1, lastResyncDay = -1;
static int32_t  phaseOff = 0;
static uint32_t lastPhaseCheck = 0, lastPhaseWarn = 0;
static bool     startupChecked = false;

static uint64_t epochMsNow() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

// ================= NTP + самолечение конвенции RTC =================
static void doNtpSync(const char* why) {
  if (wifiMgrIsAp()) { LOG("NTP", "Режим AP: NTP пропущен."); return; }
  LOG("NTP", "Ожидание синхронизации (%s)...", why);

  struct tm ntp;
  if (!getLocalTime(&ntp, 8000) || ntp.tm_year <= 120) {
    LOG("NTP", "Ошибка NTP (%s). Используем время из RTC.", why);
    return;
  }

  long tz = (long)(configTimezone() * 3600);
  float corr = 0;

  if (rtcFound()) {
    struct tm r;
    if (rtcReadTime(r)) {
      corr = (float)difftime(mktime(&ntp), mktime(&r));
      // RTC отстаёт ровно на TZ => RTC хранит UTC, а не local -> самолечение
      if (fabsf(corr - (float)tz) <= 2.0f) {
        corr = 0.0f;
        LOG("NTP", "%s: конвенция RTC исправлена (было UTC, стало local); коррекция 0.00 с", why);
      } else {
        LOG("NTP", "%s: коррекция %+.2f с", why, corr);
      }
    }
    rtcWriteTime(ntp);   // всегда пишем ЛОКАЛЬНОЕ время
  }
  lastCorr = corr;
}

void clockSyncInit() {
  long off = (long)(configTimezone() * 3600);
  configTime(off, 0, NTP_SERVER1, NTP_SERVER2);
  LOG("TIME", "Часовой пояс (GMT offset): %ld сек", off);
}

void clockSyncStartup() { doNtpSync("startup"); startupChecked = false; lastPhaseCheck = 0; }
float   clockSyncLastCorrection() { return lastCorr; }
int32_t clockSyncPhaseOffsetMs()  { return phaseOff; }
bool    clockSyncResyncRunning()  { return csState != CS_IDLE; }
void    clockSyncRequestResync()  { if (csState == CS_IDLE) csPending = true; }

// ================= Ресинк дисплея (вариант A, без гашения) =================
static void csStart() {
  uint64_t t = epochMsNow() + 4000;
  csTargetMs = ((t + 999) / 1000) * 1000;
  time_t sec = (time_t)(csTargetMs / 1000);
  struct tm tt;
  localtime_r(&sec, &tt);
  csH = tt.tm_hour; csM = tt.tm_min; csS = tt.tm_sec;
  pinsDividerHold();
  csT = millis();
  csState = CS_HOLD;
  LOG("CSYNC", "Resync start, цель %02d:%02d:%02d", csH, csM, csS);
}

static void csSetCounters() {
  pinsResetCounters();
  for (uint8_t i = 0; i < csH; i++) { pinsPulse(PIN_SET_HOUR,   8); delay(12); }
  for (uint8_t i = 0; i < csM; i++) { pinsPulse(PIN_SET_MINUTE, 8); delay(12); }
  for (uint8_t i = 0; i < csS; i++) { pinsPulse(PIN_SET_SECOND, 8); delay(12); }
}

static void phaseCheck() {
  if (!safetyEnabled() || pinsDividerHeld() || !pinsMeanderAlive()) return;
  int32_t off = pinsMeanderPhaseOffsetMs();
  phaseOff = off;
  if (!startupChecked) {
    startupChecked = true;
    if (off > 300 || off < -300) {
      LOG("CSYNC", "Стартовое фазовое смещение %ld мс -> внеплановый ресинк", (long)off);
      csStart();
    }
    return;
  }
  if ((off > 200 || off < -200) && millis() - lastPhaseWarn > 3600000UL) {
    lastPhaseWarn = millis();
    LOG("CSYNC", "Фазовое смещение %ld мс > 200 мс", (long)off);
  }
}

void clockSyncTick() {
  if (calibBusy()) return;   // калибровка владеет делителем/счётчиками

  switch (csState) {
    case CS_IDLE: break;
    case CS_HOLD:
      if (millis() - csT >= 150) { csSetCounters(); csState = CS_WAIT; }
      break;
    case CS_WAIT: {
      uint64_t now = epochMsNow();
      if (now >= csTargetMs) {
        pinsDividerRelease();
        pinsMeanderWatchdogKick();
        csEdge = pinsSecondEdgeCount();
        csT = millis();
        csState = CS_VERIFY;
      } else if (now > csTargetMs + 2000) {
        LOG("CSYNC", "WARN: окно отпускания пропущено");
        pinsDividerRelease();
        pinsMeanderWatchdogKick();
        csEdge = pinsSecondEdgeCount();
        csT = millis();
        csState = CS_VERIFY;
      }
      break;
    }
    case CS_VERIFY:
      if (pinsSecondEdgeCount() > csEdge) {
        phaseOff = pinsMeanderPhaseOffsetMs();
        LOG("CSYNC", "Resync done, фаза %ld мс", (long)phaseOff);
        csState = CS_EXIT;
      } else if (millis() - csT > 4000) {
        LOG("CSYNC", "WARN: нет фронтов меандра после отпускания");
        csState = CS_EXIT;
      }
      break;
    case CS_EXIT:
      csState = CS_IDLE;
      break;
  }
  if (csState != CS_IDLE) return;

  if (csPending) { csPending = false; csStart(); return; }

  struct tm now;
  if (!getLocalTime(&now, 100)) return;

  if (now.tm_hour == 4 && now.tm_min == 55 && now.tm_yday != lastNtpDay) {
    lastNtpDay = now.tm_yday;
    doNtpSync("daily");
  }
  if (now.tm_hour == 5 && now.tm_min == 0 && now.tm_yday != lastResyncDay) {
    lastResyncDay = now.tm_yday;
    csStart();
  }
  if (rtcFound() && millis() - lastRtcWrite >= 86400000UL) {
    lastRtcWrite = millis();
    rtcWriteTime(now);
  }
  if (millis() - lastPhaseCheck >= 60000UL) {
    lastPhaseCheck = millis();
    phaseCheck();
  }
}