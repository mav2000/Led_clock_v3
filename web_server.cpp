/*
File: web_server.cpp | Module: WEB | File ver: 1.3.0 | Proj ver: 3.6.1
Fix 1.3.0: подключена авто-яркость к вебу:
  GET  /api/bright          — статус (mode/lux/target_ma/duty/hw/curve/trim);
  POST /api/bright/mode     — {"mode":0|1};
  POST /api/bright/manual   — {"pct":0..100};
  POST /api/bright/params   — {lux_min,lux_max,ma_min,ma_max,gamma};
  POST /api/bright/reload   — перечитать кривую;
  brightnessReload() вызывается после calib save/apply/load.
  В sysinfo добавлены поля bright_*.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <time.h>
#include "web_server.h"
#include "version.h"
#include "board_config.h"
#include "wifi_mgr.h"
#include "config_store.h"
#include "logging.h"
#include "rtc_ds3231.h"
#include "light_bh1750.h"
#include "power_ina226.h"
#include "pins_io.h"
#include "pwm_ctrl.h"
#include "safety.h"
#include "display_model.h"
#include "clock_sync.h"
#include "brightness.h"
#include "calibration.h"
#include "fallback_html.h"

static WebServer server(80);
static File upFile;
static String upDir = "/";
static bool otaOk = false, otaDone = false;

static String nowStr() {
  struct tm t;
  if (!getLocalTime(&t)) return "no sync";
  char b[30]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
  return String(b);
}
static String normDir(const String &in) {
  String d = in; d.trim();
  if (d.length() == 0) return "/";
  if (d[0] != '/') d = "/" + d;
  while (d.length() > 1 && d.endsWith("/")) d = d.substring(0, d.length() - 1);
  return d;
}
static String normPath(const String &in) {
  String p = in; p.trim();
  if (p.length() == 0) return "";
  if (p[0] != '/') p = "/" + p;
  return p;
}
static String resolvePath(const String &in) {
  String p = normPath(in);
  if (fsMounted()) {
    if (LittleFS.exists(p)) return p;
    String base = p.substring(p.lastIndexOf('/') + 1);
    if (LittleFS.exists("/" + base)) return "/" + base;
    if (LittleFS.exists("/calib/" + base)) return "/calib/" + base;
  }
  return p;
}

static void hRoot() {
  if (fsMounted() && LittleFS.exists("/index.html")) {
    File f = LittleFS.open("/index.html", "r");
    if (f) {
      size_t size = f.size();
      if (size > 0 && size <= 500000) {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "-1");
        server.streamFile(f, "text/html; charset=utf-8");
        f.close(); return;
      }
      f.close();
    }
  }
  server.send_P(200, "text/html", FALLBACK_HTML);
}

static void hSysinfo() {
  JsonDocument doc;
  doc["mode"]  = wifiMgrIsAp() ? "AP" : "STA";
  doc["ip"]    = wifiMgrIsAp() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["ssid"]  = wifiMgrSsid();
  doc["rssi"]  = wifiMgrIsAp() ? 0 : WiFi.RSSI();
  doc["time"]  = nowStr();
  struct tm t; bool have = getLocalTime(&t);
  doc["hh"] = have ? t.tm_hour : 0;
  doc["mm"] = have ? t.tm_min  : 0;
  doc["ss"] = have ? t.tm_sec  : 0;
  doc["tz"]  = configTimezone();
  doc["rtc_found"] = rtcFound();
  doc["rtc_temp"]  = rtcFound() ? rtcReadTemp() : -999;
  doc["fw"] = projectVersion();
  doc["heap"] = ESP.getFreeHeap();
  doc["flash"] = ESP.getFlashChipSize();
  doc["sketch"] = ESP.getSketchSize();
  doc["has_full_ui"] = fsMounted() && LittleFS.exists("/index.html");
  doc["pwm_pct"] = pwmGetPercent();
  doc["pwm_res"] = pwmResolution();
  doc["pwm_settled"] = pwmSettled(PWM_SETTLE_MS);
  doc["master"] = safetyEnabled();
  doc["fault"] = safetyFaultText();
  doc["phase_ms"] = clockSyncPhaseOffsetMs();
  doc["resync"] = clockSyncResyncRunning();
  doc["divider_held"] = pinsDividerHeld();
  doc["meander_edges"] = pinsSecondEdgeCount();
  doc["colon_on"] = pinsColonOnNow();
  doc["ina_found"] = ina226Present();
  doc["ina_valid"] = ina226Valid();
  doc["ina_ma"] = ina226CurrentMa();
  doc["ina_v"] = ina226BusV();
  doc["ina_w"] = ina226PowerW();
  doc["vbus_source"] = ina226VbusSource();
  doc["rail_status"] = ina226RailStatus();
  doc["light_found"] = bh1750Present();
  doc["light_valid"] = bh1750Valid();
  doc["light_lux"] = bh1750Lux();
  doc["light_raw"] = bh1750Raw();
  const BrightStatus& bs = brightnessStatus();
  doc["bright_mode"] = bs.mode;
  doc["bright_lux"] = bs.lux;
  doc["bright_target_ma"] = bs.target_ma;
  doc["bright_duty"] = bs.duty;
  doc["bright_curve"] = bs.curve_loaded;
  if (have) {
    doc["digit_seg"] = dmDigitSegments(t.tm_hour, t.tm_min, t.tm_sec);
    doc["eff_seg"]   = dmEffectiveSegments(t.tm_hour, t.tm_min, t.tm_sec);
    doc["i_seg_est"] = dmEstimateSegmentCurrentMa(ina226CurrentMa(), t.tm_hour, t.tm_min, t.tm_sec, true);
  }
  uint32_t up = millis() / 1000;
  char ub[48];
  snprintf(ub, sizeof(ub), "%ud %uh %um %us",
           (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600),
           (unsigned)((up % 3600) / 60), (unsigned)(up % 60));
  doc["uptime"] = up; doc["uptime_str"] = ub;
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void hVersions() {
  JsonDocument doc;
  doc["project"] = projectVersion();
  JsonArray a = doc["modules"].to<JsonArray>();
  for (int i = 0; i < MODULE_VERSIONS_COUNT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = MODULE_VERSIONS[i].name;
    o["ver"]  = MODULE_VERSIONS[i].ver;
  }
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void hRestart() {
  server.send(200, "text/plain", "Restarting...");
  safetyForceDisable("restart");
  delay(300);
  ESP.restart();
}

static void hScan() {
  if (wifiMgrIsAp()) WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) { JsonObject o = arr.add<JsonObject>(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); }
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
  WiFi.scanDelete();
  if (wifiMgrIsAp()) WiFi.mode(WIFI_AP);
}

static void hSaveWifi() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  String ssid = nd["ssid"] | ""; String pass = nd["password"] | "";
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) { server.send(400, "text/plain", "bad ssid/pass"); return; }
  JsonDocument cfg; configLoad(cfg);
  JsonArray nets = cfg["networks"].as<JsonArray>();
  if (nets.isNull()) nets = cfg["networks"].to<JsonArray>();
  bool found = false;
  for (JsonObject o : nets) { if (String(o["ssid"] | "") == ssid) { o["password"] = pass; found = true; break; } }
  if (!found) { JsonObject o = nets.add<JsonObject>(); o["ssid"] = ssid; o["password"] = pass; }
  configSave(cfg);
  server.send(200, "text/plain", "OK");
  LOG("WIFI", "Сеть '%s' сохранена. Перезагрузка...", ssid.c_str());
  delay(500); ESP.restart();
}

static void hGetTz() { String res = "{\"tz\":" + String(configTimezone(), 1) + "}"; server.send(200, "application/json", res); }
static void hSetTz() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  float tz = nd["tz"] | DEFAULT_TZ_HOURS;
  configSetTimezone(tz);
  clockSyncInit();
  server.send(200, "text/plain", "OK");
  LOG("TIME", "Часовой пояс изменён на %.2f", tz);
}

static void hFiles() {
  String dir = normDir(server.hasArg("dir") ? server.arg("dir") : String("/"));
  JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
  if (fsMounted()) {
    File root = LittleFS.open(dir);
    if (root && root.isDirectory()) {
      File f = root.openNextFile();
      while (f) {
        JsonObject o = arr.add<JsonObject>();
        o["name"]   = String(f.name());
        o["size"]   = f.size();
        o["is_dir"] = f.isDirectory();
        f = root.openNextFile();
      }
    }
  }
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void hUpLoad() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (upFile) upFile.close();
    if (!fsMounted()) return;
    upDir = normDir(server.hasArg("dir") ? server.arg("dir") : String("/"));
    String fn = u.filename; fn.replace("\\", "/");
    int idx = fn.lastIndexOf('/'); if (idx >= 0) fn = fn.substring(idx + 1);
    String path = (upDir == "/" ? "/" : upDir + "/") + fn;
    upFile = LittleFS.open(path, "w");
    if (!upFile) LOG("FS", "Не удалось открыть %s", path.c_str());
    else LOG("FS", "Приём файла: %s", path.c_str());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile) upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) { upFile.close(); LOG("FS", "Файл загружен (%u байт)", (unsigned)u.totalSize); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
  }
}
static void hUpPost() { server.send(200, "text/plain", "OK"); }

static void hDownload() {
  if (!server.hasArg("name")) { server.send(404, "text/plain", "nf"); return; }
  String n = resolvePath(server.arg("name"));
  if (!fsMounted() || !LittleFS.exists(n)) { server.send(404, "text/plain", "nf"); return; }
  File f = LittleFS.open(n, "r");
  if (!f) { server.send(404, "text/plain", "nf"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + n.substring(n.lastIndexOf('/') + 1) + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void hView() {
  if (!server.hasArg("name")) { server.send(404, "text/plain", "nf"); return; }
  String n = resolvePath(server.arg("name"));
  if (!fsMounted() || !LittleFS.exists(n)) { server.send(404, "text/plain", "nf"); return; }
  File f = LittleFS.open(n, "r");
  if (!f) { server.send(404, "text/plain", "nf"); return; }
  if (f.size() > 300000) { f.close(); server.send(413, "text/plain", "big"); return; }
  server.streamFile(f, "text/plain; charset=utf-8");
  f.close();
}

static void hDelete() {
  String raw = server.hasArg("name") ? server.arg("name")
             : (server.hasArg("file") ? server.arg("file") : String(""));
  if (server.hasArg("plain") && raw.length() == 0) {
    JsonDocument nd;
    if (!deserializeJson(nd, server.arg("plain"))) raw = nd["name"] | nd["file"] | "";
  }
  if (raw.length() == 0) { server.send(404, "text/plain", "nf"); return; }
  String n = resolvePath(raw);
  if (n == String(CONFIG_FILE)) { server.send(403, "text/plain", "protected"); return; }
  if (!fsMounted() || !LittleFS.exists(n)) { server.send(404, "text/plain", "nf"); return; }
  LittleFS.remove(n);
  LOG("FS", "Файл удалён: %s", n.c_str());
  server.send(200, "text/plain", "Deleted");
}

static void hDeleteUi() {
  if (fsMounted() && LittleFS.exists("/index.html")) {
    LittleFS.remove("/index.html");
    LOG("FS", "index.html удалён принудительно");
    server.send(200, "text/plain", "index.html удалён.");
  } else server.send(200, "text/plain", "index.html не найден.");
}

static void hOtaUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    otaOk = false; otaDone = false;
    LOG("OTA", "Начало обновления: %s", u.filename.c_str());
    bool nm = u.filename.endsWith(".bin") || u.filename.endsWith(".BIN");
    if (nm) otaOk = Update.begin(UPDATE_SIZE_UNKNOWN);
    if (!otaOk) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaOk && Update.write(u.buf, u.currentSize) != u.currentSize) { otaOk = false; Update.printError(Serial); }
  } else if (u.status == UPLOAD_FILE_END) {
    otaDone = true;
    if (otaOk) otaOk = Update.end(true);
    if (!otaOk) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.end();
    otaOk = false; otaDone = true;
  }
}
static void hOtaPost() {
  server.sendHeader("Connection", "close");
  if (otaDone && otaOk) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    LOG("OTA", "Обновление успешно. Перезагрузка...");
    safetyForceDisable("ota");
    delay(1000); ESP.restart();
  } else server.send(200, "application/json", "{\"status\":\"fail\"}");
}

static void hPulse() {
  String n = server.arg("name");
  uint32_t ms = server.hasArg("ms") ? server.arg("ms").toInt() : 20;
  if (ms == 0) ms = 20; if (ms > 5000) ms = 5000;
  uint8_t pin = 255;
  if      (n == "Reset_clock_divider") pin = PIN_RESET_DIVIDER;
  else if (n == "Reset_clock_counter") pin = PIN_RESET_COUNTER;
  else if (n == "Set_Second_counter")  pin = PIN_SET_SECOND;
  else if (n == "Set_Minute_counter")  pin = PIN_SET_MINUTE;
  else if (n == "Set_Hour_counter")    pin = PIN_SET_HOUR;
  if (pin == 255) { server.send(404, "text/plain", "unknown"); return; }
  pinsPulse(pin, ms);
  server.send(200, "text/plain", "ok");
}

static void hPwm() {
  if (server.hasArg("duty")) brightnessSetHwPercent((uint8_t)constrain(server.arg("duty").toInt(), 0, 100));
  server.send(200, "text/plain", String(pwmGetPercent()));
}

static void hPower() {
  JsonDocument doc;
  doc["found"] = ina226Present(); doc["valid"] = ina226Valid();
  doc["shunt"] = SHUNT_OHM; doc["cal"] = ina226Cal();
  doc["ma"] = ina226CurrentMa(); doc["v"] = ina226BusV(); doc["w"] = ina226PowerW(); doc["uv"] = ina226ShuntUv();
  doc["vbus_source"] = ina226VbusSource(); doc["rail_status"] = ina226RailStatus(); doc["rail_mv"] = ina226RailMv();
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void hLight() {
  JsonDocument doc;
  doc["found"] = bh1750Present(); doc["valid"] = bh1750Valid();
  doc["lux"] = bh1750Lux(); doc["raw"] = bh1750Raw(); doc["mtreg"] = bh1750Mtreg();
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}

// --- BRIGHT (FIX 1.3.0) ---
static void hBright() {
  const BrightStatus& s = brightnessStatus();
  JsonDocument doc;
  doc["mode"] = s.mode;
  doc["lux"] = s.lux;
  doc["lux_valid"] = s.lux_valid;
  doc["target_ma"] = s.target_ma;
  doc["duty"] = s.duty;
  doc["hw_percent"] = s.hw_percent;
  doc["curve_loaded"] = s.curve_loaded;
  doc["curve_n"] = s.curve_n;
  doc["manual_pct"] = s.manual_pct;
  doc["lux_min"] = s.lux_min; doc["lux_max"] = s.lux_max;
  doc["ma_min"] = s.ma_min;   doc["ma_max"] = s.ma_max;
  doc["gamma"] = s.gamma;
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}
static void hBrightMode() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  uint8_t m = (uint8_t)(nd["mode"] | 0) ? BRIGHT_MANUAL : BRIGHT_AUTO;
  brightnessSetMode(m);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}
static void hBrightManual() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  uint8_t p = (uint8_t)constrain(nd["pct"] | 50, 0, 100);
  brightnessSetManualPercent(p);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}
static void hBrightParams() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  const BrightStatus& s = brightnessStatus();
  brightnessSetParams(nd["lux_min"] | s.lux_min, nd["lux_max"] | s.lux_max,
                      nd["ma_min"]  | s.ma_min,  nd["ma_max"]  | s.ma_max,
                      nd["gamma"]    | s.gamma);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}
static void hBrightReload() {
  brightnessReload();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// --- CALIB ---
static void hCalibStart() {
  if (calibIsRunning()) { server.send(409, "application/json", "{\"status\":\"busy\"}"); return; }
  CalibParams p;
  if (server.hasArg("plain")) {
    JsonDocument nd;
    if (!deserializeJson(nd, server.arg("plain"))) {
      p.target_ma        = nd["target_ma"]        | p.target_ma;
      p.i_min_ma         = nd["i_min_ma"]         | p.i_min_ma;
      p.coarse_step      = nd["coarse_step"]      | p.coarse_step;
      p.fine_step        = nd["fine_step"]        | p.fine_step;
      p.coarse_settle_ms = nd["coarse_settle_ms"] | p.coarse_settle_ms;
      p.coarse_accum_ms  = nd["coarse_accum_ms"]  | p.coarse_accum_ms;
      p.fine_settle_ms   = nd["fine_settle_ms"]   | p.fine_settle_ms;
      p.fine_first_settle_ms = nd["fine_first_settle_ms"] | p.fine_first_settle_ms;
      p.fine_accum_ms    = nd["fine_accum_ms"]    | p.fine_accum_ms;
      p.i_set_ma         = nd["i_set_ma"]         | p.i_set_ma;
    }
  }
  bool ok = calibStart(p);
  server.send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"fail\"}");
}
static void hCalibStatus() {
  const CalibResult* r = calibResult();
  float duty, ma; uint16_t done, planned, eta; uint8_t pct;
  calibLive(duty, ma, done, planned, pct, eta);
  JsonDocument doc;
  doc["state"]    = calibStateStr(calibState());
  doc["running"]  = calibIsRunning();
  doc["busy"]     = calibBusy();
  doc["progress"] = pct;
  doc["eta_s"]    = eta;
  doc["done"]     = done;
  doc["planned"]  = planned;
  doc["duty"]     = duty;
  doc["ma"]       = ma;
  doc["offset_ma"]= r->offset_ma;
  doc["n"]        = r->n;
  doc["valid"]    = r->valid;
  doc["saved"]    = r->saved;
  doc["duty_set"] = r->duty_set;
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}
static void hCalibCmd() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument nd;
  if (deserializeJson(nd, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  String cmd = nd["cmd"] | "";
  bool ok = false;
  if      (cmd == "pause")  ok = calibPause();
  else if (cmd == "resume") ok = calibResume();
  else if (cmd == "abort")  ok = calibAbort();
  else { server.send(400, "text/plain", "unknown cmd"); return; }
  server.send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"fail\"}");
}
static void hCalibResult() {
  const CalibResult* r = calibResult();
  JsonDocument doc;
  doc["valid"] = r->valid; doc["saved"] = r->saved;
  doc["offset_ma"] = r->offset_ma; doc["target_ma"] = r->target_ma;
  doc["i_max_ma"] = r->i_max_ma;
  doc["duty_lo"] = r->duty_lo; doc["duty_hi"] = r->duty_hi; doc["duty_set"] = r->duty_set;
  JsonArray c = doc["curve"].to<JsonArray>();
  for (uint16_t i = 0; i < r->n; i++) { JsonObject o = c.add<JsonObject>(); o["d"] = r->curve[i].duty; o["i"] = r->curve[i].i_ma; }
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}
static void hCalibSave() {
  String label = "curve";
  if (server.hasArg("plain")) { JsonDocument nd; if (!deserializeJson(nd, server.arg("plain"))) label = nd["label"] | "curve"; }
  bool ok = calibSave(label.c_str());
  if (ok) brightnessReload();          // FIX 1.3.0
  server.send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"fail\"}");
}
static void hCalibApply() {
  String label = "curve";
  if (server.hasArg("plain")) { JsonDocument nd; if (!deserializeJson(nd, server.arg("plain"))) label = nd["label"] | "curve"; }
  bool ok = calibApply(label.c_str());
  if (ok) brightnessReload();          // FIX 1.3.0
  server.send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"fail\"}");
}
static void hCalibList() {
  JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
  if (fsMounted()) {
    File root = LittleFS.open("/calib");
    if (root && root.isDirectory()) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) { JsonObject o = arr.add<JsonObject>(); o["name"] = String(f.name()); o["size"] = f.size(); }
        f = root.openNextFile();
      }
    }
  }
  String res; serializeJson(doc, res);
  server.send(200, "application/json", res);
}
static void hCalibLoad() {
  String raw = server.hasArg("file") ? server.arg("file") : String("");
  if (server.hasArg("plain") && raw.length() == 0) {
    JsonDocument nd;
    if (!deserializeJson(nd, server.arg("plain"))) raw = nd["file"] | "";
  }
  if (raw.length() == 0) { server.send(400, "text/plain", "no file"); return; }
  String path = resolvePath(raw);
  if (!path.startsWith("/calib/")) path = "/calib/" + path.substring(path.lastIndexOf('/') + 1);
  if (!fsMounted() || !LittleFS.exists(path)) { server.send(404, "text/plain", "nf"); return; }
  JsonDocument cfg; configLoad(cfg);
  cfg["calib"]["active_file"] = path;
  configSave(cfg);
  brightnessReload();                  // FIX 1.3.0
  LOG("CAL", "Active curve set: %s", path.c_str());
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}
static void hCalibDelete() {
  String raw = server.hasArg("file") ? server.arg("file")
             : (server.hasArg("name") ? server.arg("name") : String(""));
  if (server.hasArg("plain") && raw.length() == 0) {
    JsonDocument nd;
    if (!deserializeJson(nd, server.arg("plain"))) raw = nd["file"] | nd["name"] | "";
  }
  if (raw.length() == 0) { server.send(400, "text/plain", "no file"); return; }
  String path = resolvePath(raw);
  if (!path.startsWith("/calib/")) path = "/calib/" + path.substring(path.lastIndexOf('/') + 1);
  if (!fsMounted() || !LittleFS.exists(path)) { server.send(404, "text/plain", "nf"); return; }
  LittleFS.remove(path);
  LOG("CAL", "Профиль удалён: %s", path.c_str());
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void hNotFound() {
  if (wifiMgrIsAp()) { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); }
  else server.send(404, "text/plain", "Not found");
}

void webServerInit() {
  LOG("WEB", "Инициализация веб-сервера...");
  server.on("/",               HTTP_GET,  hRoot);
  server.on("/api/sysinfo",    HTTP_GET,  hSysinfo);
  server.on("/api/versions",   HTTP_GET,  hVersions);
  server.on("/api/restart",    HTTP_GET,  hRestart);
  server.on("/api/scan",       HTTP_GET,  hScan);
  server.on("/api/save_wifi",  HTTP_POST, hSaveWifi);
  server.on("/api/get_tz",     HTTP_GET,  hGetTz);
  server.on("/api/set_tz",     HTTP_POST, hSetTz);
  server.on("/api/files",      HTTP_GET,  hFiles);
  server.on("/api/upload",     HTTP_POST, hUpPost, hUpLoad);
  server.on("/api/download",   HTTP_GET,  hDownload);
  server.on("/api/view",       HTTP_GET,  hView);
  server.on("/api/delete",     HTTP_ANY,  hDelete);
  server.on("/api/delete_ui",  HTTP_GET,  hDeleteUi);
  server.on("/api/ota",        HTTP_POST, hOtaPost, hOtaUpload);
  server.on("/api/pulse",      HTTP_ANY,  hPulse);
  server.on("/api/pwm",        HTTP_ANY,  hPwm);
  server.on("/api/power",      HTTP_GET,  hPower);
  server.on("/api/light",      HTTP_GET,  hLight);
  server.on("/api/bright",         HTTP_GET,  hBright);         // FIX 1.3.0
  server.on("/api/bright/mode",    HTTP_POST, hBrightMode);
  server.on("/api/bright/manual",  HTTP_POST, hBrightManual);
  server.on("/api/bright/params",  HTTP_POST, hBrightParams);
  server.on("/api/bright/reload",  HTTP_POST, hBrightReload);
  server.on("/api/calib/start",  HTTP_POST, hCalibStart);
  server.on("/api/calib/status", HTTP_GET,  hCalibStatus);
  server.on("/api/calib/cmd",    HTTP_POST, hCalibCmd);
  server.on("/api/calib/result", HTTP_GET,  hCalibResult);
  server.on("/api/calib/save",   HTTP_POST, hCalibSave);
  server.on("/api/calib/apply",  HTTP_POST, hCalibApply);
  server.on("/api/calib/list",   HTTP_GET,  hCalibList);
  server.on("/api/calib/load",   HTTP_ANY,  hCalibLoad);
  server.on("/api/calib/delete", HTTP_ANY,  hCalibDelete);
  server.on("/generate_204",         HTTP_GET, hRoot);
  server.on("/gen_204",              HTTP_GET, hRoot);
  server.on("/hotspot-detect.html",  HTTP_GET, hRoot);
  server.onNotFound(hNotFound);
  server.begin();
  LOG("WEB", "Веб-сервер запущен");
}
void webServerTick() { server.handleClient(); }