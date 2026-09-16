# Changelog — ESP32-C3 LED Clock

## [3.5.9] — calibration: подсветка по завершению
- calibration 1.2.4: в CAL_RESTORE ШИМ ставится НЕ в 100% (индикаторы погашены),
  а в duty_set по снятой таблице под уставку i_set_ma (дефолт 1.4 мА/сегмент);
  вызов brightnessSetHwPercent() синхронизирует модуль яркости.
- calibration.h: дефолт i_set_ma = 1.4.
- version: проект 3.5.9; calibration 1.2.4.

## [3.5.8] — calibration: компактизация кривой, mA_per_segment
## [3.5.7] — web_server: calibStateStr в calibration, dir-aware files
## [3.5.6] — link fix: calib* только в calibration.cpp
## [3.5.x] — calibration tick-machine, INA226 rail5v, safety
## [3.4.x] — calibration module, web calib endpoints
## [3.3.x] — resync/phase, meander watchdog
## [3.0.x] — modularization, web, OTA, sensors, versioning