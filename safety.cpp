/*
 * File: safety.cpp | Module: SAFETY | File ver: 1.1.4 | Proj ver: 3.4.2
 * Fix 1.1.4: самовосстановление из аварии meander_lost, когда меандр снова
 *            жив и делитель свободен (устройство больше не «залипает» до
 *            физической перезагрузки).
 */
#include <Arduino.h>
#include <string.h>
#include "safety.h"
#include "pins_io.h"
#include "pwm_ctrl.h"
#include "power_ina226.h"
#include "clock_sync.h"
#include "board_config.h"
#include "logging.h"

static bool enabled = false;
static bool firstSettle = true;
static char fault[48] = "";

void safetyInit() {
  enabled = false;
  firstSettle = true;
  fault[0] = 0;
  pinsMasterDisable();
}

bool safetyEnabled() { return enabled; }
const char* safetyFaultText() { return fault; }
void safetyClearFault() { fault[0] = 0; }

bool safetyRequestEnable() {
  if (enabled) return true;
  uint32_t need = firstSettle ? PWM_START_SETTLE_MS : PWM_SETTLE_MS;
  if (!pwmSettled(need)) return false;
  if (fault[0]) return false;
  pinsMasterAllow();               // внутри — kick сторожевого окна меандра
  enabled = true;
  firstSettle = false;
  LOG("SAFETY", "Управление разрешено (settle %u мс)", need);
  return true;
}

void safetyForceDisable(const char* reason) {
  pinsMasterDisable();
  bool was = enabled;
  enabled = false;
  snprintf(fault, sizeof(fault), "%s", reason);
  if (was) LOG("SAFETY", "ЗАПРЕТ управления: %s", reason);
}

void safetyTick() {
  // авария по току
  if (ina226Valid() && ina226CurrentMa() > I_TOTAL_HARD_MAX_MA) {
    safetyForceDisable("overcurrent");
    pwmSetPercent(PWM_SAFE_PERCENT);
    return;
  }

  // FIX: самовосстановление из meander_lost, когда меандр снова жив
  if (!enabled && fault[0] && strcmp(fault, "meander_lost") == 0) {
    if (!pinsDividerHeld() && pinsMeanderAlive()) {
      safetyClearFault();
      LOG("SAFETY", "меандр восстановлен, авария сброшена");
    }
  }

  // меандр должен идти, КОГДА делитель не удержан И ресинк не выполняется
  if (enabled &&
      !pinsDividerHeld() &&
      !clockSyncResyncRunning() &&
      !pinsMeanderAlive()) {
    safetyForceDisable("meander_lost");
    return;
  }

  // авто-разрешение после settle, если аварий нет
  if (!enabled && !fault[0]) safetyRequestEnable();
}