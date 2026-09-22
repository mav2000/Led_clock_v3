/*
 * File: clock_sync.cpp | Module: CSYNC | File ver: 1.6.0 | Proj ver: 3.6.0
 * Fix 1.6.0: После холодного старта всегда запрашивается ресинк дисплея,
 *            так как аппаратные счётчики не знают системного времени.
 *            Исправлена проблема, когда дисплей показывал 00:00:00 после старта.
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
static uint32_t lastStartupRetry = 0;

// Конфигурация синхронизации (значения по умолчанию)
static SyncCfg syncCfg = {
    .ntp_hour = 4,
    .ntp_min = 55,
    .ntp_every_min = 0,
    .resync_hour = 5,
    .resync_min = 0,
    .resync_every_min = 0,
    .rtc_write_interval_ms = 86400000UL,
    .phase_warn_ms = 200,
    .phase_resync_ms = 300,
    .auto_resync = true
};

// Статистика синхронизации
static SyncStats syncStats = {
    .ntp_ok = false,
    .ntp_count = 0,
    .ntp_last_epoch = 0,
    .ntp_last_corr = 0.0f,
    .resync_count = 0,
    .resync_last_epoch = 0,
    .resync_last_phase = 0,
    .startup_retry_n = 0,
    .startup_done = false
};

static uint64_t epochMsNow() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static uint64_t epochSecNow() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec;
}

// ================= NTP + запись в RTC =================
static void phaseCheck();

static void doNtpSync(const char* why) {
  if (wifiMgrIsAp()) { 
    LOG("NTP", "Режим AP: NTP пропущен."); 
    syncStats.ntp_ok = false;
    if (!syncStats.startup_done) syncStats.startup_retry_n++;
    return; 
  }
  LOG("NTP", "Ожидание синхронизации (%s)...", why);

  struct tm ntp;
  if (!getLocalTime(&ntp, 2000) || ntp.tm_year <= 120) {
    LOG("NTP", "Ошибка NTP (%s). Fallback на RTC.", why);
    syncStats.ntp_ok = false;
    if (!syncStats.startup_done) {
      syncStats.startup_retry_n++;
      LOG("NTP", "Стартовая синхронизация не удалась (попытка %d)", syncStats.startup_retry_n);
    }
    if (rtcFound()) {
      struct tm r_utc;
      if (rtcReadTime(r_utc)) {
        struct timeval tv = { .tv_sec = mktime_utc(&r_utc), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        LOG("NTP", "Системное время восстановлено из RTC (UTC)");
        if (!syncStats.startup_done) syncStats.startup_done = true;
      }
    }
    return;
  }

  float corr = 0;

  if (rtcFound()) {
    struct tm r_utc;
    if (rtcReadTime(r_utc)) {
      time_t t_ntp = mktime(&ntp);
      time_t t_rtc = mktime_utc(&r_utc);
      corr = (float)difftime(t_ntp, t_rtc);
      
      LOG("NTP", "%s: коррекция %+.2f с", why, corr);
      
      if (syncCfg.auto_resync && fabsf(corr) > 2.0f) {
        LOG("NTP", "Рассинхронизация дисплея -> запрос внепланового ресинка");
        clockSyncRequestResync();
      }
    }
    rtcWriteTime(ntp);
    lastRtcWrite = millis();
  }
  
  lastCorr = corr;
  syncStats.ntp_ok = true;
  syncStats.ntp_count++;
  syncStats.ntp_last_epoch = epochSecNow();
  syncStats.ntp_last_corr = corr;
  
  if (!syncStats.startup_done) {
    syncStats.startup_done = true;
    LOG("NTP", "Стартовая синхронизация завершена успешно");
  }
}

void clockSyncInit() {
  long off = (long)(configTimezone() * 3600);
  configTime(off, 0, NTP_SERVER1, NTP_SERVER2);
  LOG("TIME", "Часовой пояс (GMT offset): %ld сек", off);
}

void clockSyncStartup() { 
  doNtpSync("startup"); 
  startupChecked = false; 
  lastPhaseCheck = 0; 
  lastStartupRetry = millis();
  
  // ИСПРАВЛЕНО: После холодного старта всегда синхронизируем дисплей
  // Аппаратные счётчики (CD4518) не знают системного времени после сброса питания
  if (syncCfg.auto_resync) {
    LOG("CSYNC", "Холодный старт -> принудительный ресинк дисплея");
    clockSyncRequestResync();
  }
  
  phaseCheck(); 
}

float   clockSyncLastCorrection() { return lastCorr; }
int32_t clockSyncPhaseOffsetMs()  { return phaseOff; }
bool    clockSyncResyncRunning()  { return csState != CS_IDLE; }
void    clockSyncRequestResync()  { if (csState == CS_IDLE) csPending = true; }

bool clockSyncForceResync() {
  if (csState == CS_IDLE) {
    csPending = true;
    LOG("CSYNC", "Принудительный ресинк запрошен из веб-интерфейса");
    return true;
  } else {
    LOG("CSYNC", "WARN: Принудительный ресинк отклонен (ресинк уже выполняется)");
    return false;
  }
}

bool clockSyncForceNtp() {
  LOG("CSYNC", "Принудительная NTP-синхронизация запрошена из веб-интерфейса");
  doNtpSync("manual");
  return syncStats.ntp_ok;
}

SyncCfg clockSyncCfgGet() { return syncCfg; }

bool clockSyncCfgSet(const SyncCfg& cfg) { 
  syncCfg = cfg; 
  LOG("CSYNC", "Конфигурация обновлена: NTP %02d:%02d (каждые %d мин), Resync %02d:%02d (каждые %d мин)", 
      syncCfg.ntp_hour, syncCfg.ntp_min, syncCfg.ntp_every_min, 
      syncCfg.resync_hour, syncCfg.resync_min, syncCfg.resync_every_min);
  return true; 
}

SyncStats clockSyncStats() { return syncStats; }

// ================= Ресинк дисплея (вариант A, без гашения) =================
static void csStart() {
  uint64_t t = epochMsNow() + 4000;
  csTargetMs = ((t + 999) / 1000) * 1000;
  time_t sec = (time_t)(csTargetMs / 1000);
  struct tm tt;
  
  if (localtime_r(&sec, &tt) == NULL) {
    LOG("CSYNC", "ERROR: localtime_r failed in csStart");
    csState = CS_IDLE;
    return;
  }
  
  csH = tt.tm_hour; csM = tt.tm_min; csS = tt.tm_sec;
  pinsDividerHold();
  csT = millis();
  csState = CS_HOLD;
  LOG("CSYNC", "Resync start, цель %02d:%02d:%02d", csH, csM, csS);
  
  // Обновление статистики
  syncStats.resync_count++;
  syncStats.resync_last_epoch = epochSecNow();
  syncStats.resync_last_phase = 0;
}

static void csSetCounters() {
  pinsResetCounters();
  for (uint8_t i = 0; i < csH; i++) { pinsPulse(PIN_SET_HOUR,   8); delayMicroseconds(12000); }
  for (uint8_t i = 0; i < csM; i++) { pinsPulse(PIN_SET_MINUTE, 8); delayMicroseconds(12000); }
  for (uint8_t i = 0; i < csS; i++) { pinsPulse(PIN_SET_SECOND, 8); delayMicroseconds(12000); }
}

static void phaseCheck() {
  if (!safetyEnabled() || pinsDividerHeld() || !pinsMeanderAlive()) return;
  int32_t off = pinsMeanderPhaseOffsetMs();
  phaseOff = off;
  
  if (!startupChecked) {
    startupChecked = true;
    if (syncCfg.auto_resync && (off > syncCfg.phase_resync_ms || off < -syncCfg.phase_resync_ms)) {
      LOG("CSYNC", "Стартовое фазовое смещение %ld мс -> внеплановый ресинк", (long)off);
      clockSyncRequestResync(); 
    }
    return;
  }
  if ((off > syncCfg.phase_warn_ms || off < -syncCfg.phase_warn_ms) && millis() - lastPhaseWarn > 3600000UL) {
    lastPhaseWarn = millis();
    LOG("CSYNC", "Фазовое смещение %ld мс > %ld мс", (long)off, (long)syncCfg.phase_warn_ms);
  }
}

void clockSyncTick() {
  if (calibBusy()) {
    if (csState != CS_IDLE) {
      pinsDividerRelease();
      csState = CS_IDLE;
      csPending = false;
      LOG("CSYNC", "Resync aborted due to calibration");
    }
    return;   
  }

  switch (csState) {
    case CS_IDLE: break;
    case CS_HOLD:
      if (millis() - csT >= 150) { csSetCounters(); csState = CS_WAIT; }
      break;
    case CS_WAIT: {
      uint64_t now;
      uint64_t deadline = csTargetMs + 2000;
      do {
        yield();
        now = epochMsNow();
        if (now >= csTargetMs) {
          pinsDividerRelease();
          pinsMeanderWatchdogKick();
          csEdge = pinsSecondEdgeCount();
          csT = millis();
          csState = CS_VERIFY;
          break;
        }
      } while (now < deadline);
      
      if (csState == CS_WAIT) {
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
        syncStats.resync_last_phase = phaseOff;
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

  if (!syncStats.startup_done && millis() - lastStartupRetry >= 30000UL) {
    lastStartupRetry = millis();
    doNtpSync("startup_retry");
  }

  // ================= NTP синхронизация =================
  if (syncCfg.ntp_every_min > 0) {
    uint64_t now_epoch = epochSecNow();
    if (syncStats.ntp_last_epoch == 0 || 
        (now_epoch - syncStats.ntp_last_epoch) >= (uint64_t)syncCfg.ntp_every_min * 60) {
      doNtpSync("interval");
    }
  } else {
    if (now.tm_hour == syncCfg.ntp_hour && now.tm_min == syncCfg.ntp_min && now.tm_yday != lastNtpDay) {
      lastNtpDay = now.tm_yday;
      doNtpSync("daily");
    }
  }
  
  // ================= Ресинк дисплея =================
  if (syncCfg.resync_every_min > 0) {
    uint64_t now_epoch = epochSecNow();
    if (syncStats.resync_last_epoch == 0 || 
        (now_epoch - syncStats.resync_last_epoch) >= (uint64_t)syncCfg.resync_every_min * 60) {
      csStart();
    }
  } else {
    if (now.tm_hour == syncCfg.resync_hour && now.tm_min == syncCfg.resync_min && now.tm_yday != lastResyncDay) {
      lastResyncDay = now.tm_yday;
      csStart();
    }
  }
  
  // Периодическая запись в RTC
  if (rtcFound() && millis() - lastRtcWrite >= syncCfg.rtc_write_interval_ms) {
    lastRtcWrite = millis();
    rtcWriteTime(now);
  }
  
  // Проверка фазы (раз в минуту)
  if (millis() - lastPhaseCheck >= 60000UL) {
    lastPhaseCheck = millis();
    phaseCheck();
  }
}