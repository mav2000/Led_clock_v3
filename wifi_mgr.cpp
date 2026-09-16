/*
 * File: wifi_mgr.cpp | Module: WIFI | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#include <Arduino.h>
#include "wifi_mgr.h"
#include "board_config.h"
#include "config_store.h"
#include "logging.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

static DNSServer dns;
static bool apMode=false; static String ssid="";
static uint32_t lastCheck=0, discSince=0, lastApScan=0;
static bool mdnsOn=false;

void wifiMgrInit(){ WiFi.persistent(false); }
static void startAp(){
  apMode=true; WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_TX_POWER_DEF);
  IPAddress ip(192,168,4,1); WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  if(strlen(AP_PASS_DEF)>=8) WiFi.softAP(AP_SSID_DEF,AP_PASS_DEF); else WiFi.softAP(AP_SSID_DEF);
  dns.start(53,"*",WiFi.softAPIP());
  LOG("WIFI","AP запущен %s",WiFi.softAPIP().toString().c_str());
}
static void startMdns(){ if(!mdnsOn && MDNS.begin("esp32c3-clock")){ MDNS.addService("http","tcp",80); mdnsOn=true; } }
void wifiMgrStart(){
  JsonDocument d; configLoad(d);
  JsonArray nets=d["networks"].as<JsonArray>();
  if(nets.isNull()||nets.size()==0){ LOG("WIFI","Сетей нет -> AP"); startAp(); return; }
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setTxPower(WIFI_TX_POWER_DEF);
  WiFi.setHostname("esp32c3-clock"); WiFi.disconnect(true); delay(100);
  for(JsonObject n: nets){
    String s=n["ssid"]|"", p=n["password"]|"";
    LOG("WIFI","Проба '%s'",s.c_str());
    if(p.length()) WiFi.begin(s.c_str(),p.c_str()); else WiFi.begin(s.c_str());
    for(int i=0;i<20 && WiFi.status()!=WL_CONNECTED;i++) delay(500);
    if(WiFi.status()==WL_CONNECTED){ apMode=false; ssid=s; startMdns();
      LOG("WIFI","OK IP=%s",WiFi.localIP().toString().c_str()); return; }
  }
  LOG("WIFI","Не подключились -> AP"); startAp();
}
void wifiMgrTick(){
  if(apMode){ dns.processNextRequest();
    if(millis()-lastApScan>=60000){ lastApScan=millis();
      JsonDocument d; configLoad(d); JsonArray nets=d["networks"].as<JsonArray>();
      if(nets.isNull()||nets.size()==0) return;
      WiFi.mode(WIFI_AP_STA); int n=WiFi.scanNetworks();
      for(int i=0;i<n;i++){ String s=WiFi.SSID(i);
        for(JsonObject net: nets){ if(String(net["ssid"]|"")==s){
          WiFi.mode(WIFI_STA); String p=net["password"]|"";
          if(p.length()) WiFi.begin(s.c_str(),p.c_str()); else WiFi.begin(s.c_str());
          for(int k=0;k<20&&WiFi.status()!=WL_CONNECTED;k++) delay(500);
          if(WiFi.status()==WL_CONNECTED){ apMode=false; ssid=s; startMdns();
            LOG("WIFI","AP->STA '%s'",s.c_str()); }
          else WiFi.mode(WIFI_AP);
          WiFi.scanDelete(); return;
        }}
      }
      WiFi.scanDelete(); WiFi.mode(WIFI_AP);
    }
    return;
  }
  if(millis()-lastCheck>=30000){ lastCheck=millis();
    if(WiFi.status()!=WL_CONNECTED){
      if(!discSince){ discSince=millis(); WiFi.disconnect(); WiFi.reconnect(); }
      else if(millis()-discSince>300000){ discSince=0; startAp(); }
      else WiFi.reconnect();
    } else if(discSince){ discSince=0; LOG("WIFI","Связь восстановлена"); }
  }
}
bool wifiMgrIsAp(){ return apMode; }
String wifiMgrSsid(){ return apMode?String(AP_SSID_DEF):ssid; }