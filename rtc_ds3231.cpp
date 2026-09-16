/*
 * File: rtc_ds3231.cpp | Module: RTC | File ver: 1.1.5 | Proj ver: 3.5.1
 * Fix 1.1.5: возвращена TZ-компенсация в initRTC():
 *            epoch = mktime(rtc_local) - tz  (иначе на старте время съезжает на TZ
 *            до прихода NTP -> регресс "+25199 с").
 */
#include <Arduino.h>
#include <Wire.h>
#include "rtc_ds3231.h"
#include "board_config.h"
#include "config_store.h"
#include "logging.h"

#define DS3231_ADDR 0x68

static bool found = false;

static uint8_t b2d(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t d2b(uint8_t v) { return (v / 10 << 4) | (v % 10); }

bool rtcBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  Wire.beginTransmission(DS3231_ADDR);
  found = (Wire.endTransmission() == 0);
  return found;
}

bool rtcFound() { return found; }

bool rtcReadTime(struct tm& t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(DS3231_ADDR, 7) < 7) return false;
  t.tm_sec  = b2d(Wire.read() & 0x7F);
  t.tm_min  = b2d(Wire.read() & 0x7F);
  uint8_t h = Wire.read();
  if (h & 0x40) {
    uint8_t hr = b2d(h & 0x1F);
    t.tm_hour = (h & 0x20) ? ((hr == 12) ? 12 : hr + 12) : ((hr == 12) ? 0 : hr);
  } else {
    t.tm_hour = b2d(h & 0x3F);
  }
  t.tm_wday = Wire.read() & 0x07;
  t.tm_mday = b2d(Wire.read() & 0x3F);
  t.tm_mon  = b2d(Wire.read() & 0x1F) - 1;
  t.tm_year = b2d(Wire.read()) + 100;
  return true;
}

void rtcWriteTime(struct tm& t) {   // пишем ЛОКАЛЬНОЕ время
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  Wire.write(d2b(t.tm_sec));
  Wire.write(d2b(t.tm_min));
  Wire.write(d2b(t.tm_hour));
  Wire.write(d2b(t.tm_wday == 0 ? 7 : t.tm_wday));
  Wire.write(d2b(t.tm_mday));
  Wire.write(d2b(t.tm_mon + 1));
  Wire.write(d2b(t.tm_year - 100));
  Wire.endTransmission();
}

float rtcReadTemp() {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x11);
  if (Wire.endTransmission() != 0) return -999.0f;
  if (Wire.requestFrom(DS3231_ADDR, 2) < 2) return -999.0f;
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  float t = (msb & 0x7F) + (lsb >> 6) * 0.25f;
  if (msb & 0x80) t = -t;
  return t;
}

void initRTC() {
  LOG("RTC", "Инициализация DS3231...");
  if (!rtcBegin()) { found = false; LOG("RTC", "Модуль DS3231 не найден!"); return; }
  found = true;
  LOG("RTC", "DS3231 найден");

  struct tm t;
  if (!rtcReadTime(t)) { LOG("RTC", "Ошибка чтения времени"); return; }
  if (t.tm_year < 124) { LOG("RTC", "Время в модуле не установлено"); return; }

  // RTC хранит LOCAL -> epoch = mktime(local) - tz  (FIX 1.1.5)
  long tz = (long)(configTimezone() * 3600);
  struct timeval tv;
  tv.tv_sec  = mktime(&t) - tz;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  LOG("RTC", "Системное время из RTC (local, TZ-компенсация %ld с)", tz);
}