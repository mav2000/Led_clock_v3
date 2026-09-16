/*
 * File: pwm_ctrl.h | Module: PWM | File ver: 1.1.2 | Proj ver: 3.1.2
 */
#pragma once
#include <stdint.h>

void     pwmInit();
void     pwmSetPercent(uint8_t p);
uint8_t  pwmGetPercent();
uint32_t pwmMsSinceChange();
bool     pwmSettled(uint32_t requiredMs);
uint8_t  pwmResolution();