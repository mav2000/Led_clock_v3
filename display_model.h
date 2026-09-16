/*
 * File: display_model.h | Module: DMOD | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once
#include <stdint.h>

extern const uint8_t SEG_MAP[10];      // по даташиту CD4511 (6='b',9 без d)
uint16_t dmDigitSegments(uint8_t h,uint8_t m,uint8_t s);
void     dmSetColonsInstalled(bool b);
bool     dmColonsInstalled();
float    dmColonEquivAvg();            // ~2.0 при установленных точках
float    dmEffectiveSegments(uint8_t h,uint8_t m,uint8_t s);
float    dmEstimateSegmentCurrentMa(float iTotalMa,uint8_t h,uint8_t m,uint8_t s,bool instant);
float    dmSegmentLimitByTotalMa(float effSegs);