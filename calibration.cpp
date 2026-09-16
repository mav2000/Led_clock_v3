/*
File: calibration.cpp | Module: CAL | File ver: 1.2.3 | Proj ver: 3.5.8
Fix 1.2.3: онлайн-компактизация точного прохода: дубли (плато по току)合并
       на приходе (точка = среднее ШИМ группы), поэтому CALIB_MAX_POINTS
       ограничивает только число различных уровней тока, а не сырые шаги;
       скан 0.1% покрывает весь диапазон fineHi..fineLo без обрезания.
       Первая точка после скачка ШИМ отбрасывает 2 старые выборки.
*/
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <math.h>
#include "calibration.h"
#include "board_config.h"
#include "pins_io.h"
#include "pwm_ctrl.h"
#include "power_ina226.h"
#include "config_store.h"
#include "clock_sync.h"
#include "safety.h"
#include "logging.h"

static const float PLATEAU_EPS = 0.05f;   // порог слияния плато, мА/сегм

static CalibState state = CAL_IDLE;
static bool paused = false, abortReq = false;
static CalibParams prm;
static CalibResult res;

static float  curDuty = 0, curMaSeg = 0;
static uint16_t donePts = 0, planPts = 0;      // сырые шаги (для прогресса)
static uint8_t  progPct = 0;
static uint16_t etaSec = 0;
static uint8_t  phase = 0;
static uint32_t t0 = 0, accT0 = 0, lastSampleT = 0;
static uint16_t settleCur = 0;
static float accSum = 0, accMn = 0, accMx = 0;
static uint16_t accN = 0, accWin = 0;
static uint8_t  skipOld = 0;
static float fineLo = 0, fineHi = 0;
static bool firstFine = false;
static uint16_t plateN = 0;                    // точек в текущем плато
static char saveLabel[24] = "curve";

// ток НА СЕГМЕНТ из суммарного (без офсета)
static float iSeg(float total) { return total / (float)CAL_SEG_COUNT; }

static void beginAcc(uint16_t win, uint8_t skip) {
  accWin = win; skipOld = skip;
  accT0 = millis(); lastSampleT = 0;
  accSum = 0; accN = 0; accMn = 1e9; accMx = -1e9;
}
static bool pollAcc(float& mean, float& spread) {
  if (millis() - lastSampleT >= 20) {
    lastSampleT = millis();
    float ma;
    if (ina226SampleCurrentNow(ma)) {
      if (skipOld) { skipOld--; }
      else { accSum += ma; accN++; if (ma < accMn) accMn = ma; if (ma > accMx) accMx = ma; }
      curMaSeg = iSeg(ma);
    }
  }
  if (millis() - accT0 >= accWin) {
    if (accN == 0) { mean = 0; spread = 0; return true; }
    mean = accSum / accN; spread = accMx - accMn;
    curMaSeg = iSeg(mean);
    return true;
  }
  return false;
}

// сырая точка (грубый проход)
static void addPt(float d, float i) {
  if (res.n < CALIB_MAX_POINTS) { res.curve[res.n].duty = d; res.curve[res.n].i_ma = i; res.n++; }
}
// FIX 1.2.3: точка точного прохода с онлайн-слиянием плато
static void addFinePt(float d, float i) {
  if (res.n > 0 && fabsf(i - res.curve[res.n-1].i_ma) < PLATEAU_EPS) {
    CalibPoint& p = res.curve[res.n-1];
    plateN++;
    p.duty += (d - p.duty) / (float)plateN;   // среднее ШИМ группы
    p.i_ma += (i - p.i_ma) / (float)plateN;
  } else if (res.n < CALIB_MAX_POINTS) {
    res.curve[res.n].duty = d; res.curve[res.n].i_ma = i; res.n++; plateN = 1;
  }
}

static float dutyAtCurrent(float iWant_seg) {
  if (res.n == 0) return 0;
  for (uint16_t k = 1; k < res.n; k++) {
    float i0 = res.curve[k-1].i_ma, i1 = res.curve[k].i_ma;
    if ((i0 <= iWant_seg && i1 >= iWant_seg) || (i0 >= iWant_seg && i1 <= iWant_seg)) {
      float sp = (i1 - i0);
      if (fabsf(sp) < 1e-6) return res.curve[k].duty;
      float f = (iWant_seg - i0) / sp;
      return res.curve[k-1].duty + f * (res.curve[k].duty - res.curve[k-1].duty);
    }
  }
  return (iWant_seg > res.curve[res.n-1].i_ma) ? res.curve[res.n-1].duty : res.curve[0].duty;
}

static void updateProgress() {
  float sf = (prm.fine_settle_ms + prm.fine_accum_ms) / 1000.0f;
  switch (state) {
    case CAL_PREPARE: progPct = 2;  etaSec = 2; break;
    case CAL_COARSE:  progPct = 10; etaSec = 10; break;
    case CAL_RANGE:   progPct = 25; etaSec = 1; break;
    case CAL_FINE:    progPct = 25 + (uint8_t)(65.0f * donePts / (planPts ? planPts : 1));
                      etaSec = (uint16_t)((planPts > donePts ? planPts - donePts : 0) * sf + 3); break;
    case CAL_APPLY:   progPct = 92; etaSec = 1; break;
    case CAL_SAVE:    progPct = 96; etaSec = 1; break;
    case CAL_RESTORE: progPct = 98; etaSec = 1; break;
    case CAL_DONE:    progPct = 100; etaSec = 0; break;
    default: progPct = 0; etaSec = 0; break;
  }
}

const char* calibStateStr(CalibState s) {
  switch (s) {
    case CAL_IDLE: return "idle";
    case CAL_PREPARE: return "prepare";
    case CAL_COARSE: return "coarse";
    case CAL_RANGE: return "range";
    case CAL_FINE: return "fine";
    case CAL_APPLY: return "apply";
    case CAL_SAVE: return "save";
    case CAL_RESTORE: return "restore";
    case CAL_DONE: return "done";
    case CAL_ERROR: return "error";
  }
  return "idle";
}

void calibInit() { state = CAL_IDLE; paused = false; abortReq = false; }
bool calibIsRunning() { return (state != CAL_IDLE && state != CAL_DONE); }
bool calibBusy()      { return (state != CAL_IDLE && state != CAL_DONE); }
CalibState calibState() { return state; }
const CalibResult* calibResult() { return &res; }
void calibLive(float& d, float& ma, uint16_t& dn, uint16_t& pl, uint8_t& pc, uint16_t& et) {
  d = curDuty; ma = curMaSeg; dn = donePts; pl = planPts; pc = progPct; et = etaSec;
}

bool calibStart(const CalibParams& p) {
  if (state != CAL_IDLE && state != CAL_DONE) return false;
  if (clockSyncResyncRunning()) { LOG("CAL", "START refused: resync"); return false; }
  prm = p; res = CalibResult(); res.target_ma = p.target_ma; res.offset_ma = 0;
  donePts = 0; planPts = 0; progPct = 0; etaSec = 0; plateN = 0;
  paused = false; abortReq = false; firstFine = true;
  state = CAL_PREPARE; phase = 0; t0 = millis();
  LOG("CAL", "START target=%.2f mA/seg fine=%.2f%%", p.target_ma, p.fine_step);
  return true;
}
bool calibAbort()  { if (!calibBusy()) return false; abortReq = true; return true; }
bool calibPause()  { if (!calibBusy()) return false; paused = true;  return true; }
bool calibResume() { if (!calibBusy()) return false; paused = false; return true; }

void calibTick() {
  if (state == CAL_IDLE || state == CAL_DONE) return;
  if (abortReq) { state = CAL_RESTORE; phase = 0; t0 = millis(); }
  if (paused) { updateProgress(); return; }
  switch (state) {
    case CAL_PREPARE: {
      if (phase == 0) {
        pwmSetPercent(100);
        digitalWrite(PIN_RESET_COUNTER, HIGH);
        pinsDividerHold();
        pinsMasterAllow();
        ina226SetCalibPoll(20, 0);
        settleCur = prm.coarse_settle_ms; phase = 1; t0 = millis();
      } else if (phase == 1 && millis() - t0 >= settleCur) {
        beginAcc(prm.coarse_accum_ms, 2); phase = 2;
      } else if (phase == 2) {
        float mean, sp;
        if (pollAcc(mean, sp)) { state = CAL_COARSE; phase = 0; t0 = millis(); donePts = 0; }
      }
      break;
    }
    case CAL_COARSE: {
      if (phase == 0) {
        curDuty = 100.0f - donePts * prm.coarse_step;
        pwmSetPercent((uint8_t)curDuty);
        settleCur = prm.coarse_settle_ms; phase = 1; t0 = millis();
      } else if (phase == 1 && millis() - t0 >= settleCur) {
        beginAcc(prm.coarse_accum_ms, 2); phase = 2;
      } else if (phase == 2) {
        float mean, sp;
        if (pollAcc(mean, sp)) {
          float iseg = iSeg(mean);
          addPt(curDuty, iseg); donePts++;
          if (iseg >= prm.target_ma || curDuty <= 0.0f) { state = CAL_RANGE; phase = 0; }
          else { phase = 0; t0 = millis(); }
        }
      }
      break;
    }
    case CAL_RANGE: {
      res.i_max_ma = 0;
      for (uint16_t k = 0; k < res.n; k++) if (res.curve[k].i_ma > res.i_max_ma) res.i_max_ma = res.curve[k].i_ma;
      float iHi = (prm.target_ma < res.i_max_ma) ? prm.target_ma : res.i_max_ma;
      fineLo = dutyAtCurrent(iHi);
      fineHi = dutyAtCurrent(prm.i_min_ma);
      if (fineHi < fineLo) { float t = fineLo; fineLo = fineHi; fineHi = t; }
      res.duty_lo = fineLo; res.duty_hi = fineHi;
      planPts = (uint16_t)((fineHi - fineLo) / prm.fine_step) + 1;  // сырые шаги, без обрезания
      res.n = 0; donePts = 0; plateN = 0;
      state = CAL_FINE; phase = 0; t0 = millis();
      break;
    }
    case CAL_FINE: {
      if (phase == 0) {
        curDuty = fineHi - donePts * prm.fine_step;
        if (curDuty < fineLo) curDuty = fineLo;
        pwmSetPercent((uint8_t)(curDuty + 0.5f));
        settleCur = firstFine ? prm.fine_first_settle_ms : prm.fine_settle_ms;
        firstFine = false;
        phase = 1; t0 = millis();
      } else if (phase == 1 && millis() - t0 >= settleCur) {
        beginAcc(prm.fine_accum_ms, 2);   // 2 старые выборки отбросить
        phase = 2;
      } else if (phase == 2) {
        float mean, sp;
        if (pollAcc(mean, sp)) {
          addFinePt(curDuty, iSeg(mean));   // FIX 1.2.3: онлайн-прореживание
          donePts++;
          if (donePts >= planPts || curDuty <= fineLo) { state = CAL_APPLY; phase = 0; }
          else { phase = 0; t0 = millis(); }
        }
      }
      break;
    }
    case CAL_APPLY: {
      float want = prm.i_set_ma;
      if (want > res.i_max_ma) want = res.i_max_ma;
      if (want < prm.i_min_ma) want = prm.i_min_ma;
      res.duty_set = dutyAtCurrent(want);
      if (res.duty_set < res.duty_lo) res.duty_set = res.duty_lo;
      if (res.duty_set > res.duty_hi) res.duty_set = res.duty_hi;
      pwmSetPercent((uint8_t)(res.duty_set + 0.5f));
      res.valid = (res.n > 2);
      state = CAL_SAVE; phase = 0;
      break;
    }
    case CAL_SAVE: {
      res.saved = calibSave(saveLabel);
      state = CAL_RESTORE; phase = 0; t0 = millis();
      break;
    }
    case CAL_RESTORE: {
      pwmSetPercent(100);
      digitalWrite(PIN_RESET_COUNTER, LOW);
      pinsDividerRelease();
      ina226RestorePoll();
      clockSyncRequestResync();
      state = CAL_DONE;
      LOG("CAL", "DONE n=%u set=%.2f%% saved=%d", res.n, res.duty_set, res.saved ? 1 : 0);
      break;
    }
    default: break;
  }
  updateProgress();
}

bool calibSave(const char* label) {
  if (res.n == 0) return false;
  String lbl = String(label); lbl.trim(); String safe = "";
  for (uint16_t i = 0; i < lbl.length(); i++) { char c = lbl[i]; if (isalnum((unsigned char)c)||c=='_'||c=='-') safe += c; }
  if (!safe.length()) safe = "curve";
  LittleFS.mkdir("/calib");
  String path = "/calib/" + safe + ".json";
  File f = LittleFS.open(path, "w"); if (!f) return false;
  JsonDocument d;
  JsonObject meta = d["meta"].to<JsonObject>();
  meta["label"]=safe; meta["created"]=(uint32_t)time(nullptr);
  meta["shunt_ohm"]=SHUNT_OHM; meta["seg_count"]=CAL_SEG_COUNT;
  meta["units"]="mA_per_segment";
  meta["target_ma"]=res.target_ma; meta["i_max_ma"]=res.i_max_ma;
  meta["duty_lo"]=res.duty_lo; meta["duty_hi"]=res.duty_hi;
  meta["duty_set"]=res.duty_set; meta["offset_ma"]=0; meta["valid"]=res.valid;
  JsonArray curve = d["curve"].to<JsonArray>();
  for (uint16_t i = 0; i < res.n; i++) { JsonObject o = curve.add<JsonObject>(); o["d"]=res.curve[i].duty; o["i"]=res.curve[i].i_ma; }
  serializeJson(d, f); f.close();
  JsonDocument cfg; configLoad(cfg); cfg["calib"]["active_file"]=path; configSave(cfg);
  LOG("CAL", "Saved %s", path.c_str());
  return true;
}
bool calibApply(const char* label) {
  String lbl = String(label); lbl.trim(); String safe = "";
  for (uint16_t i = 0; i < lbl.length(); i++) { char c = lbl[i]; if (isalnum((unsigned char)c)||c=='_'||c=='-') safe += c; }
  if (!safe.length()) safe = "curve";
  String path = "/calib/" + safe + ".json";
  if (!fsMounted() || !LittleFS.exists(path)) return false;
  JsonDocument cfg; configLoad(cfg); cfg["calib"]["active_file"]=path; configSave(cfg);
  LOG("CAL", "Applied %s", path.c_str());
  return true;
}