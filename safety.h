/*
 * File: safety.h | Module: SAFETY | File ver: 1.1.3 | Proj ver: 3.1.3
 */
#pragma once
#include <stdint.h>

void        safetyInit();
void        safetyTick();
bool        safetyEnabled();
bool        safetyRequestEnable();
void        safetyForceDisable(const char* reason);
void        safetyClearFault();
const char* safetyFaultText();