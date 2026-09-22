/*
 * File: clock_sync.h | Module: CSYNC | File ver: 1.6.0 | Proj ver: 3.6.0
 * Fix 1.6.0: Версия обновлена для соответствия clock_sync.cpp
 */
#pragma once
#include <stdint.h>
#include <time.h>

struct SyncCfg {
    uint8_t ntp_hour;
    uint8_t ntp_min;
    uint16_t ntp_every_min;
    uint8_t resync_hour;
    uint8_t resync_min;
    uint16_t resync_every_min;
    uint32_t rtc_write_interval_ms;
    int32_t phase_warn_ms;
    int32_t phase_resync_ms;
    bool auto_resync;
};

struct SyncStats {
    bool ntp_ok;
    uint32_t ntp_count;
    uint64_t ntp_last_epoch;
    float ntp_last_corr;
    uint32_t resync_count;
    uint64_t resync_last_epoch;
    int32_t resync_last_phase;
    uint8_t startup_retry_n;
    bool startup_done;
};

void clockSyncInit();
void clockSyncStartup();
void clockSyncTick();

void clockSyncRequestResync();
bool clockSyncForceResync();
bool clockSyncForceNtp();

SyncCfg clockSyncCfgGet();
bool clockSyncCfgSet(const SyncCfg& cfg);

SyncStats clockSyncStats();
float clockSyncLastCorrection();
int32_t clockSyncPhaseOffsetMs();
bool clockSyncResyncRunning();