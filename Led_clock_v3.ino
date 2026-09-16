/*
 * File: Led_clock_v3.ino | Module: MAIN | File ver: 1.0.0 | Proj ver: 3.0.0
 * Glue: порядок инициализации и цикл
 */
#include "version.h"
#include "board_config.h"
#include "logging.h"
#include "config_store.h"
#include "rtc_ds3231.h"
#include "light_bh1750.h"
#include "power_ina226.h"
#include "pins_io.h"
#include "pwm_ctrl.h"
#include "safety.h"
#include "display_model.h"
#include "wifi_mgr.h"
#include "clock_sync.h"
#include "brightness.h"
#include "calibration.h"
#include "web_server.h"

void setup(){
  Serial.begin(115200);
  delay(2000);

  // 1) безопасность ДО всего: выходы LOW, master в запрет
  pinsIoInit();
  // 2) ШИМ сразу в safe
  pwmInit();

  LOG("SYS","==============================================");
  LOG("SYS","%s v%s",PROJECT_NAME,projectVersion());
  LOG("SYS","==============================================");

  fsInit();
  rtcBegin();
  // при валидном RTC выставить системное время
  if(rtcFound()){ struct tm t; if(rtcReadTime(t) && t.tm_year>=124){
    struct timeval tv; tv.tv_sec=mktime(&t); tv.tv_usec=0; settimeofday(&tv,NULL);
    LOG("RTC","Системное время из RTC"); } }

  clockSyncInit();
  bh1750Begin();
  ina226Begin();
  pinsMeanderInit();
  safetyInit();
  brightnessInit();

  wifiMgrInit();
  wifiMgrStart();
  clockSyncStartup();
  webServerInit();

  LOG("SYS","Инициализация завершена");
}

void loop(){
  wifiMgrTick();
  clockSyncTick();
  bh1750Update();
  ina226Update();
  safetyTick();
  brightnessTick();
  calibTick();
  webServerTick();
  delay(10);
}