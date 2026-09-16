/*
 * File: display_model.cpp | Module: DMOD | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#include <Arduino.h>
#include "display_model.h"
#include "board_config.h"
#include "pins_io.h"

const uint8_t SEG_MAP[10] = {6,2,5,5,4,5,5,3,7,5};
static bool colonsInstalled = true;

uint16_t dmDigitSegments(uint8_t h,uint8_t m,uint8_t s){
  uint16_t sum=0;
  if(h>=10) sum+=SEG_MAP[h/10];
  sum+=SEG_MAP[h%10]; sum+=SEG_MAP[m/10]; sum+=SEG_MAP[m%10];
  sum+=SEG_MAP[s/10]; sum+=SEG_MAP[s%10];
  return sum;
}
void dmSetColonsInstalled(bool b){ colonsInstalled=b; }
bool dmColonsInstalled(){ return colonsInstalled; }
float dmColonEquivAvg(){ return colonsInstalled ? (COLON_LED_COUNT*COLON_DUTY) : 0.0f; }
float dmEffectiveSegments(uint8_t h,uint8_t m,uint8_t s){
  return (float)dmDigitSegments(h,m,s) + dmColonEquivAvg();
}
float dmEstimateSegmentCurrentMa(float iTotalMa,uint8_t h,uint8_t m,uint8_t s,bool instant){
  float segs;
  if(instant){
    float colonNow = (colonsInstalled && pinsColonOnNow()) ? COLON_LED_COUNT : 0.0f;
    segs = (float)dmDigitSegments(h,m,s) + colonNow;
  } else segs = dmEffectiveSegments(h,m,s);
  if(segs<=0) return 0.0f;
  return iTotalMa/segs;
}
float dmSegmentLimitByTotalMa(float effSegs){
  if(effSegs<=0) return I_SEGMENT_MAX_MA;
  float lim=(I_TOTAL_HARD_MAX_MA)/effSegs;
  return lim<I_SEGMENT_MAX_MA ? lim : I_SEGMENT_MAX_MA;
}