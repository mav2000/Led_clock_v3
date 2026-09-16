/*
 * File: clock_sync.h | Module: CSYNC | File ver: 1.3.2 | Proj ver: 3.7.2
 * Fix 1.3.2: API без изменений относительно 1.3.0 (SyncCfg/SyncStats/force);
 *            изменена только реализация (см. clock_sync.cpp).
 */
#pragma once
#include <stdint.h>

/* Конфигурация периодичности синхронизаций */
struct SyncCfg {
  uint8_t  ntp_hour;          // час суточного NTP (0..23)
  uint8_t  ntp_min;           // минута суточного NTP (0..59)
  uint16_t ntp_every_min;     // доп. интервал NTP, мин (0 = только суточный)
  uint8_t  resync_hour;       // час суточного ресинка дисплея
  uint8_t  resync_min;        // минута суточного ресинка
  uint16_t resync_every_min;  // доп. интервал ресинка, мин (0 = только суточный)
};

/* Статистика синхронизаций */
struct SyncStats {
  uint32_t ntp_count;         // число успешных NTP
  uint32_t ntp_last_epoch;    // unix-время последнего успешного NTP
  float    ntp_last_corr;     // последняя коррекция RTC (сек)
  bool     ntp_ok;            // был хотя бы один успешный NTP с момента старта
  uint32_t resync_count;      // число ресинков дисплея
  uint32_t resync_last_epoch; // unix-время последнего ресинка
  int32_t  resync_last_phase; // последняя измеренная фаза (мс)
  uint8_t  startup_retry_n;   // номер текущей стартовой retry-попытки (0..10)
  bool     startup_done;      // стартовая NTP-сессия завершена
};

void          clockSyncInit();
void          clockSyncStartup();
void          clockSyncTick();
float         clockSyncLastCorrection();
bool          clockSyncResyncRunning();
int32_t       clockSyncPhaseOffsetMs();
void          clockSyncRequestResync();

/* Принудительные действия (вызываются из веб-эндпоинтов) */
bool          clockSyncForceNtp();
bool          clockSyncForceResync();

/* Конфигурация и статистика */
SyncCfg       clockSyncCfgGet();
bool          clockSyncCfgSet(const SyncCfg& cfg);
SyncStats     clockSyncStats();