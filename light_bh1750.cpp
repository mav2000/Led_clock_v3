/*
 * File: light_bh1750.cpp | Module: LIGHT | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#include <Arduino.h>
#include "light_bh1750.h"
#include "board_config.h"
#include <Wire.h>

#define AVG_BITS 3            // 64 выборок
#define CT_BITS  5            // 2.116 ms
#define SAMPLES  3
#define POLL_MS  10000UL

static bool present=false, valid=false;
static uint16_t raw=0; static float lux=-1.0f;
static uint32_t lastPoll=0, stateT=0;
static uint8_t state=0, idx=0, retry=0;
static uint16_t smp[SAMPLES];

static bool cmd(uint8_t c){ Wire.beginTransmission(BH1750_ADDR); Wire.write(c); return Wire.endTransmission()==0; }
static bool setMtreg(uint8_t mt){
  Wire.beginTransmission(BH1750_ADDR); Wire.write((uint8_t)(0x40|(mt>>5)));
  if (Wire.endTransmission()!=0) return false;
  Wire.beginTransmission(BH1750_ADDR); Wire.write((uint8_t)(0x60|(mt&0x1F)));
  return Wire.endTransmission()==0;
}
static bool rd(uint16_t& v){ if(Wire.requestFrom(BH1750_ADDR,2)<2) return false;
  v=((uint16_t)Wire.read()<<8)|Wire.read(); return true; }
static uint16_t med(){ uint16_t t[SAMPLES]; for(int i=0;i<SAMPLES;i++)t[i]=smp[i];
  for(int i=0;i<SAMPLES;i++)for(int j=i+1;j<SAMPLES;j++)if(t[j]<t[i]){uint16_t x=t[i];t[i]=t[j];t[j]=x;}
  return t[SAMPLES/2]; }
static void start(){
  cmd(0x01); setMtreg(254);
  // one-time H-res mode2 (0x21) + averaging в битах 11:9
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write((uint8_t)(0x21 | 0));                 // режим
  Wire.endTransmission();
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write((uint8_t)(0x21));                     // повтор для надёжности
  Wire.endTransmission();
  state=1; stateT=millis();
}
void bh1750Begin(){
  Wire.beginTransmission(BH1750_ADDR);
  present = (Wire.endTransmission()==0);
  if(!present) return;
  lastPoll = millis() - POLL_MS;
}
void bh1750Update(){
  if(!present) return;
  uint32_t now=millis();
  if(state==0){ if(now-lastPoll>=POLL_MS){ idx=0; retry=0; start(); } return; }
  if(now-stateT < 700) return;                     // 64*2.116*2 ≈ 270 мс + запас
  uint16_t r; 
  if(rd(r)){ smp[idx++]=r; retry=0;
    if(idx>=SAMPLES){ raw=med(); lux=(float)raw/2.0f/1.2f/(254.0f/69.0f); valid=true; state=0; lastPoll=now; }
    else start();
  } else { if(++retry>3){ valid=false; state=0; lastPoll=now; } else start(); }
}
bool bh1750Present(){ return present; }
bool bh1750Valid(){ return valid; }
float bh1750Lux(){ return lux; }
uint16_t bh1750Raw(){ return raw; }
uint8_t bh1750Mtreg(){ return 254; }