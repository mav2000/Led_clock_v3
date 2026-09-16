/*
 * File: light_bh1750.h | Module: LIGHT | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once
#include <stdint.h>

void     bh1750Begin();
void     bh1750Update();
bool     bh1750Present();
bool     bh1750Valid();
float    bh1750Lux();
uint16_t bh1750Raw();
uint8_t  bh1750Mtreg();