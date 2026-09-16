/*
File: calibration.h | Module: CAL | File ver: 1.2.4 | Proj ver: 3.5.9
Fix 1.2.4: дефолт i_set_ma = 1.4 мА/сегмент (уставка подсветки по завершению).
*/
#pragma once
#include <stdint.h>

#define CALIB_MAX_POINTS 260
#define CAL_SEG_COUNT    30

enum CalibState {
  CAL_IDLE, CAL_PREPARE, CAL_COARSE, CAL_RANGE, CAL_FINE,
  CAL_APPLY, CAL_SAVE, CAL_RESTORE, CAL_DONE, CAL_ERROR
};

struct CalibParams {
  float    target_ma         = 10.0f;
  float    i_min_ma          = 0.5f;
  float    coarse_step       = 10.0f;
  float    fine_step         = 0.1f;
  uint16_t coarse_settle_ms  = 250;
  uint16_t coarse_accum_ms   = 1500;
  uint16_t fine_settle_ms    = 200;
  uint16_t fine_first_settle_ms = 1000;
  uint16_t fine_accum_ms     = 800;
  float    i_set_ma          = 1.4f;   // FIX 1.2.4: уставка подсветки, мА/сегмент
};

struct CalibPoint { float duty; float i_ma; };

struct CalibResult {
  bool  valid = false, saved = false;
  float offset_ma = 0, target_ma = 0, i_max_ma = 0;
  float duty_lo = 0, duty_hi = 0, duty_set = 0;
  uint16_t n = 0;
  CalibPoint curve[CALIB_MAX_POINTS];
  char note[48];
};

void calibInit();
void calibTick();
bool calibIsRunning();
bool calibBusy();
CalibState calibState();
const char* calibStateStr(CalibState s);
const CalibResult* calibResult();
bool calibStart(const CalibParams& p);
bool calibAbort();
bool calibPause();
bool calibResume();
void calibLive(float& duty, float& ma, uint16_t& done, uint16_t& planned,
               uint8_t& pct, uint16_t& eta_s);
bool calibSave(const char* label);
bool calibApply(const char* label);