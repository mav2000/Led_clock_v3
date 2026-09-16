/*
 * File: wifi_mgr.h | Module: WIFI | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once
#include <WString.h>

void   wifiMgrInit();
void   wifiMgrStart();          // connect or AP
void   wifiMgrTick();
bool   wifiMgrIsAp();
String wifiMgrSsid();