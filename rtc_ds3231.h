/*
 * File: rtc_ds3231.h | Module: RTC | File ver: 1.2.1 | Proj ver: 3.6.0
 */
#pragma once
#include <time.h>
#include <stdint.h>

bool  rtcBegin();
bool  rtcFound();
bool  rtcReadTime(struct tm& t);     // Читает время из RTC (всегда UTC)
void  rtcWriteTime(struct tm& t);    // Принимает LOCAL время, конвертирует в UTC перед записью
float rtcReadTemp();
void  initRTC();                     // Читает RTC(=UTC) и устанавливает системное время

// Вспомогательная функция для преобразования UTC struct tm в time_t (замена timegm)
time_t mktime_utc(const struct tm *t);