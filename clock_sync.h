/*
 * File: clock_sync.h | Module: CSYNC | File ver: 1.5.1 | Proj ver: 3.6.0
 * Fix 1.5.1: Добавлено недостающее поле resync_count в SyncStats
 *            (требовалось для web_server.cpp).
 */
#pragma once
#include <stdint.h>
#include <time.h>

// Конфигурация синхронизации (управляется через веб-интерфейс)
struct SyncCfg {
    uint8_t ntp_hour;               // Час ежедневной NTP-синхронизации (0-23), если ntp_every_min == 0
    uint8_t ntp_min;                // Минута ежедневной NTP-синхронизации (0-59)
    uint16_t ntp_every_min;         // Интервал NTP в минутах (0 = ежедневно в указанное время)
    uint8_t resync_hour;            // Час ежедневного ресинка дисплея (0-23), если resync_every_min == 0
    uint8_t resync_min;             // Минута ежедневного ресинка дисплея (0-59)
    uint16_t resync_every_min;      // Интервал ресинка в минутах (0 = ежедневно в указанное время)
    uint32_t rtc_write_interval_ms; // Интервал записи в RTC (мс)
    int32_t phase_warn_ms;          // Порог предупреждения по фазе (мс)
    int32_t phase_resync_ms;        // Порог внепланового ресинка по фазе (мс)
    bool auto_resync;               // Автоматический ресинк при большом расхождении NTP/RTC
};

// Статистика синхронизации (для веб-интерфейса и диагностики)
struct SyncStats {
    bool ntp_ok;                    // Успех последней NTP-синхронизации
    uint32_t ntp_count;             // Количество NTP-синхронизаций
    uint64_t ntp_last_epoch;        // Время последней NTP-синхронизации (epoch)
    float ntp_last_corr;            // Последняя коррекция (секунды)
    uint32_t resync_count;          // Количество ресинков дисплея
    uint64_t resync_last_epoch;     // Время последнего ресинка дисплея (epoch)
    int32_t resync_last_phase;      // Фаза последнего ресинка (мс)
    uint8_t startup_retry_n;        // Количество попыток при старте
    bool startup_done;              // Завершена ли стартовая синхронизация
};

// Инициализация и запуск
void clockSyncInit();
void clockSyncStartup();
void clockSyncTick();

// Запрос действий
void clockSyncRequestResync();
bool clockSyncForceResync();
bool clockSyncForceNtp();

// Конфигурация
SyncCfg clockSyncCfgGet();
bool clockSyncCfgSet(const SyncCfg& cfg);

// Статистика и диагностика
SyncStats clockSyncStats();
float clockSyncLastCorrection();
int32_t clockSyncPhaseOffsetMs();
bool clockSyncResyncRunning();