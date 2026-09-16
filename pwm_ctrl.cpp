/*
 * File: pwm_ctrl.cpp | Module: PWM | File ver: 1.1.2 | Proj ver: 3.1.2
 * ВАЖНО: #include <Arduino.h> ПЕРВЫМ — иначе не виден ESP_ARDUINO_VERSION_MAJOR
 *        и выбирается удалённый старый LEDC API.
 */
#include <Arduino.h>
#include "pwm_ctrl.h"
#include "board_config.h"
#include "logging.h"

static uint8_t  pct = PWM_SAFE_PERCENT;
static uint8_t  res = 12;
static uint32_t maxDuty = 4095;
static uint32_t lastChange = 0;

void pwmInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  bool ok = false;
  for (uint8_t r = 13; r >= 10; r--) {
    if (ledcAttach(PIN_PWM, PWM_FREQ_HZ, r)) { res = r; ok = true; break; }
  }
  if (!ok) { res = 10; ledcAttach(PIN_PWM, PWM_FREQ_HZ, res); }
#else
  res = 12;
  ledcSetup(0, PWM_FREQ_HZ, res);
  ledcAttachPin(PIN_PWM, 0);
#endif
  maxDuty = (1UL << res) - 1;
  lastChange = millis();
  pwmSetPercent(PWM_SAFE_PERCENT);
  LOG("PWM", "GPIO%u %lu Гц %u бит, safe=%u%%",
      PIN_PWM, (unsigned long)PWM_FREQ_HZ, res, pct);
}

void pwmSetPercent(uint8_t p) {
  if (p > 100) p = 100;
  pct = p;
  uint32_t d = (maxDuty * p) / 100;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(PIN_PWM, d);
#else
  ledcWrite(0, d);
#endif
  lastChange = millis();
}

uint8_t  pwmGetPercent()      { return pct; }
uint32_t pwmMsSinceChange()   { return millis() - lastChange; }
bool     pwmSettled(uint32_t r){ return pwmMsSinceChange() >= r; }
uint8_t  pwmResolution()      { return res; }