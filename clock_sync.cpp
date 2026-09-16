/*
File: clock_sync.cpp | Module: CSYNC | File ver: 1.3.2 | Proj ver: 3.7.2
Fix 1.3.2 (баг старта и «висящего» ресинка):
 1. clockSyncStartup(): после определения времени (NTP, а при неудаче — RTC)
    немедленно запрашивается ресинк дисплея — статичных цифр на старте нет.
 2. CS_WAIT: контроль скачков системного времени (SNTP). Скачок назад/вперёд
    -> текущая попытка прерывается (делитель ОТПУЩЕН) + авто-повтор ресинка.
 3. Абсолютный дедлайн ресинка 15 с по millis(): превышение -> принудительное
    отпускание делителя + повтор. Делитель не может быть удержан бесконечно.
Сохранено полностью (1.3.0): NTP-retry старта (60с x10), настраиваемое
расписание NTP/ресинка (SyncCfg в config.json), SyncStats, force NTP/resync,
самолечение конвенции RTC, phaseCheck, суточная запись RTC, guard calibBusy.
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

/* FIX 1.3.2: защита от зависания */
static uint32_t csDeadlineMs = 0;   // абсолютный дедлайн всей процедуры (millis)
static uint64_t csPrevNow = 0;      // для детекта скачков системного времени

static float    lastCorr = 0;
static uint32_t lastRtcWrite = 0;
static int32_t  phaseOff = 0;
static uint32_t lastPhaseCheck = 0, lastPhaseWarn = 0;
static bool     startupChecked = false;

/* --- Конфигурация по умолчанию (читается из config.json в init) --- */
static SyncCfg cfg = {
  .ntp_hour = 4,  .ntp_min = 55, .ntp_every_min = 0,
  .resync_hour = 5, .resync_min = 0, .resync_every_min = 0
};

/* --- Статистика --- */
static SyncStats stats = {0};

/* --- Стартовый NTP-retry --- */
#define STARTUP_RETRY_MAX     10
#define STARTUP_RETRY_MS      60000UL
static bool     startupNtpActive = true;
static uint8_t  startupRetryN = 0;
static uint32_t startupLastT = 0;
static bool     startupFirstDone = false;

/* --- Интервальные таймеры расписания --- */
static uint32_t lastNtpRun = 0;
static uint32_t lastResyncRun = 0;
static bool     haveNtpEver = false;
static bool     haveResyncEver = false;

static uint64_t epochMsNow() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}
static uint32_t epochNow() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)tv.tv_sec;
}

/* ---------- Загрузка/сохранение конфигурации ---------- */
static void loadCfg() {
  JsonDocument d;
  if (!configLoad(d)) return;
  JsonObject o = d["sync"];
  if (o.isNull()) return;
  cfg.ntp_hour         = o["ntp_hour"]         | cfg.ntp_hour;
  cfg.ntp_min          = o["ntp_min"]          | cfg.ntp_min;
  cfg.ntp_every_min    = o["ntp_every_min"]    | cfg.ntp_every_min;
  cfg.resync_hour      = o["resync_hour"]      | cfg.resync_hour;
  cfg.resync_min       = o["resync_min"]       | cfg.resync_min;
  cfg.resync_every_min = o["resync_every_min"] | cfg.resync_every_min;
  if (cfg.ntp_hour > 23) cfg.ntp_hour = 4;
  if (cfg.ntp_min > 59) cfg.ntp_min = 0;
  if (cfg.resync_hour > 23) cfg.resync_hour = 5;
  if (cfg.resync_min > 59) cfg.resync_min = 0;
}
static void saveCfg() {
  JsonDocument d;
  configLoad(d);
  JsonObject o = d["sync"].to<JsonObject>();
  o["ntp_hour"]         = cfg.ntp_hour;
  o["ntp_min"]          = cfg.ntp_min;
  o["ntp_every_min"]    = cfg.ntp_every_min;
  o["resync_hour"]      = cfg.resync_hour;
  o["resync_min"]       = cfg.resync_min;
  o["resync_every_min"] = cfg.resync_every_min;
  configSave(d);
}

/* ================= NTP + самолечение конвенции RTC ================= */
static bool doNtpSync(const char* why) {
  if (wifiMgrIsAp()) { LOG("NTP", "Режим AP: NTP пропущен."); return false; }
  LOG("NTP", "Ожидание синхронизации (%s)...", why);

  struct tm ntp;
  if (!getLocalTime(&ntp, 8000) || ntp.tm_year <= 120) {
    LOG("NTP", "Ошибка NTP (%s). Используем время из RTC.", why);
    return false;
  }

  long tz = (long)(configTimezone() * 3600);
  float corr = 0;

  if (rtcFound()) {
    struct tm r;
    if (rtcReadTime(r)) {
      corr = (float)difftime(mktime(&ntp), mktime(&r));
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
  stats.ntp_count++;
  stats.ntp_last_epoch = epochNow();
  stats.ntp_last_corr = corr;
  stats.ntp_ok = true;
  haveNtpEver = true;
  return true;
}

void clockSyncInit() {
  long off = (long)(configTimezone() * 3600);
  configTime(off, 0, NTP_SERVER1, NTP_SERVER2);
  LOG("TIME", "Часовой пояс (GMT offset): %ld сек", off);
  loadCfg();
  startupNtpActive = true;
  startupRetryN = 0;
  startupFirstDone = false;
  startupLastT = 0;
  stats = SyncStats{0};
  lastNtpRun = 0;
  lastResyncRun = 0;
}

/* FIX 1.3.2: время определяем сразу (NTP, иначе RTC) и сразу ресинк дисплея */
void clockSyncStartup() {
  bool ok = doNtpSync("startup");
  startupChecked = false;
  lastPhaseCheck = 0;
  startupFirstDone = true;
  if (ok) {
    startupNtpActive = false;
    LOG("CSYNC", "Startup NTP OK -> немедленный ресинк дисплея");
    clockSyncRequestResync();
  } else {
    if (rtcFound()) {
      struct tm r;
      if (rtcReadTime(r)) {
        time_t t = mktime(&r);           // RTC хранит ЛОКАЛЬНОЕ время
        struct timeval tv; tv.tv_sec = t; tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        LOG("TIME", "Системное время установлено из RTC");
        clockSyncRequestResync();        // показать время RTC сразу
      }
    }
    startupLastT = millis();
    LOG("CSYNC", "Startup NTP fail -> retry через 60 с");
  }
}

float   clockSyncLastCorrection() { return lastCorr; }
int32_t clockSyncPhaseOffsetMs()  { return phaseOff; }
bool    clockSyncResyncRunning()  { return csState != CS_IDLE; }
void    clockSyncRequestResync()  { if (csState == CS_IDLE) csPending = true; }

SyncCfg clockSyncCfgGet() { return cfg; }
bool    clockSyncCfgSet(const SyncCfg& c) {
  if (c.ntp_hour > 23 || c.ntp_min > 59) return false;
  if (c.resync_hour > 23 || c.resync_min > 59) return false;
  cfg = c;
  saveCfg();
  LOG("CSYNC", "Cfg: NTP %02u:%02u/%umin  Resync %02u:%02u/%umin",
      cfg.ntp_hour, cfg.ntp_min, cfg.ntp_every_min,
      cfg.resync_hour, cfg.resync_min, cfg.resync_every_min);
  return true;
}
SyncStats clockSyncStats() {
  SyncStats s = stats;
  s.startup_retry_n = startupNtpActive ? (startupRetryN + 1) : 0;
  s.startup_done = !startupNtpActive;
  return s;
}

bool clockSyncForceNtp() {
  if (wifiMgrIsAp()) { LOG("NTP", "Force NTP: режим AP — отклонено"); return false; }
  LOG("NTP", "Force NTP (manual)");
  bool ok = doNtpSync("manual");
  if (ok) clockSyncRequestResync();
  return ok;
}
bool clockSyncForceResync() {
  if (csState != CS_IDLE) { LOG("CSYNC", "Force resync: уже идёт"); return false; }
  LOG("CSYNC", "Force resync (manual)");
  csPending = true;
  return true;
}

/* ================= Ресинк дисплея (вариант A, без гашения) ================= */
static void csStart() {
  uint64_t t = epochMsNow() + 4000;
  csTargetMs = ((t + 999) / 1000) * 1000;
  time_t sec = (time_t)(csTargetMs / 1000);
  struct tm tt;
  localtime_r(&sec, &tt);
  csH = tt.tm_hour; csM = tt.tm_min; csS = tt.tm_sec;
  pinsDividerHold();
  csT = millis();
  csDeadlineMs = csT + 15000UL;   // FIX 1.3.2: абсолютный дедлайн
  csPrevNow = epochMsNow();       // FIX 1.3.2: база детекта скачков
  csState = CS_HOLD;
  LOG("CSYNC", "Resync start, цель %02d:%02d:%02d", csH, csM, csS);
}

/* FIX 1.3.2: прервать попытку (делитель отпустить!) и запланировать повтор */
static void csAbortRetry(const char* why) {
  pinsDividerRelease();
  pinsMeanderWatchdogKick();
  LOG("CSYNC", "WARN: ресинк прерван (%s) -> авто-повтор", why);
  csState = CS_EXIT;
  csPending = true;
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

  /* FIX 1.3.2: глобальная защита — делитель не может быть удержан вечно */
  if (csState != CS_IDLE && millis() > csDeadlineMs) {
    csAbortRetry("дедлайн 15 с");
  }

  switch (csState) {
    case CS_IDLE: break;
    case CS_HOLD:
      if (millis() - csT >= 150) { csSetCounters(); csState = CS_WAIT; }
      break;
    case CS_WAIT: {
      uint64_t now = epochMsNow();
      /* FIX 1.3.2: скачок системного времени (SNTP) — прервать и повторить */
      if (now + 500ULL < csPrevNow) { csAbortRetry("скачок времени назад"); break; }
      if (now > csPrevNow + 5000ULL) { csAbortRetry("скачок времени вперёд"); break; }
      csPrevNow = now;
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
        stats.resync_count++;
        stats.resync_last_epoch = epochNow();
        stats.resync_last_phase = phaseOff;
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

  /* --- Стартовый NTP-retry (60 с x 10) --- */
  if (startupNtpActive && startupFirstDone) {
    uint32_t tnow = millis();
    if (tnow - startupLastT >= STARTUP_RETRY_MS) {
      startupLastT = tnow;
      startupRetryN++;
      LOG("CSYNC", "NTP startup-retry %u/%u", startupRetryN, STARTUP_RETRY_MAX);
      bool ok = doNtpSync("startup-retry");
      if (ok) {
        startupNtpActive = false;
        LOG("CSYNC", "Startup NTP OK на retry %u -> ресинк дисплея", startupRetryN);
        clockSyncRequestResync();
      } else if (startupRetryN >= STARTUP_RETRY_MAX) {
        startupNtpActive = false;
        LOG("CSYNC", "Startup NTP: лимит попыток (%u), переход на суточный режим",
            STARTUP_RETRY_MAX);
      }
    }
  }

  struct tm now;
  if (!getLocalTime(&now, 100)) return;

  /* Плановый NTP (суточное время или интервал) */
  if (!startupNtpActive) {
    static bool ntpEverRan = false;
    bool due = false;
    if (now.tm_hour == cfg.ntp_hour && now.tm_min == cfg.ntp_min) {
      if (!haveNtpEver || millis() - lastNtpRun >= 23UL * 3600UL * 1000UL) due = true;
    }
    if (!due && cfg.ntp_every_min > 0) {
      uint32_t iv = (uint32_t)cfg.ntp_every_min * 60UL * 1000UL;
      if (!haveNtpEver || millis() - lastNtpRun >= iv) due = true;
    }
    if (due) {
      lastNtpRun = millis();
      LOG("CSYNC", "Плановый NTP %02u:%02u / каждые %u мин",
          cfg.ntp_hour, cfg.ntp_min, cfg.ntp_every_min);
      if (doNtpSync("scheduled")) clockSyncRequestResync();
    }
  }

  /* Плановый ресинк (суточное время или интервал) */
  {
    static bool resyncEverRan = false;
    (void)resyncEverRan;
    bool due = false;
    if (now.tm_hour == cfg.resync_hour && now.tm_min == cfg.resync_min) {
      if (!haveResyncEver || millis() - lastResyncRun >= 23UL * 3600UL * 1000UL) due = true;
    }
    if (!due && cfg.resync_every_min > 0) {
      uint32_t iv = (uint32_t)cfg.resync_every_min * 60UL * 1000UL;
      if (!haveResyncEver || millis() - lastResyncRun >= iv) due = true;
    }
    if (due) {
      lastResyncRun = millis();
      LOG("CSYNC", "Плановый ресинк %02u:%02u / каждые %u мин",
          cfg.resync_hour, cfg.resync_min, cfg.resync_every_min);
      csStart();
    }
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