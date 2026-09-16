/*
 * File: clock_sync.h | Module: CSYNC | File ver: 1.1.7 | Proj ver: 3.4.7
 * Fix 1.1.7: гарантированно объявлена clockSyncRequestResync()
 *            (устраняет calibration.cpp:209 'not declared').
 */
#pragma once
#include <stdint.h>

void    clockSyncInit();
void    clockSyncStartup();
void    clockSyncTick();
float   clockSyncLastCorrection();
bool    clockSyncResyncRunning();
int32_t clockSyncPhaseOffsetMs();
void    clockSyncRequestResync();   // запрос ресинка дисплея (использует calibration runExit)