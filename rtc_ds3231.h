/*
 * File: rtc_ds3231.h | Module: RTC | File ver: 1.1.3 | Proj ver: 3.1.3
 */
#pragma once
#include <time.h>
#include <stdint.h>

bool  rtcBegin();
bool  rtcFound();
bool  rtcReadTime(struct tm& t);
void  rtcWriteTime(struct tm& t);   // КОНВЕНЦИЯ: пишем ЛОКАЛЬНОЕ время
float rtcReadTemp();
void  initRTC();                    // читает RTC(=local) и ставит epoch с компенсацией TZ