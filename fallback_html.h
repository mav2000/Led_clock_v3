#pragma once

/*
 * ==============================================================================
 * Fallback UI для ESP32-C3 Clock
 * Аварийная страница, вкомпилированная в прошивку.
 * Используется, если в LittleFS отсутствует /index.html.
 *
 * Возможности:
 * - статус устройства;
 * - настройка WiFi;
 * - сканирование сетей;
 * - загрузка полного интерфейса в LittleFS;
 * - обновление прошивки;
 * - перезагрузка;
 * - удаление кривого index.html;
 * - просмотр лога;
 * - отображение BH1750;
 * - информативные статусы обновления прошивки;
 * - пароль WiFi отображается открыто.
 * ==============================================================================
 */

const char FALLBACK_HTML[] PROGMEM = R"FBHTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-C3 Fallback UI</title>
<style>
:root{
  --bg:#0e1319;
  --card:#17202a;
  --acc:#38bdf8;
  --txt:#e8eef4;
  --mut:#92a3b2;
  --ok:#22c55e;
  --err:#ef4444;
  --bord:#26333f;
}
*{box-sizing:border-box}
body{
  margin:0;
  background:var(--bg);
  color:var(--txt);
  font:14px/1.5 system-ui, "Segoe UI", Roboto, sans-serif;
}
header{
  padding:12px 16px;
  background:var(--card);
  border-bottom:1px solid var(--bord);
  display:flex;
  justify-content:space-between;
  gap:10px;
  align-items:center;
}
h1{
  font-size:18px;
  margin:0;
  color:var(--acc);
}
main{
  max-width:900px;
  margin:0 auto;
  padding:14px;
}
.card{
  background:var(--card);
  border:1px solid var(--bord);
  border-radius:12px;
  padding:14px;
  margin:12px 0;
}
h2{
  margin:0 0 10px;
  font-size:15px;
  color:var(--acc);
}
.row{
  display:flex;
  gap:8px;
  flex-wrap:wrap;
  align-items:center;
  margin:8px 0;
}
input,
button{
  background:#0b1118;
  color:var(--txt);
  border:1px solid var(--bord);
  border-radius:8px;
  padding:8px 10px;
  font:inherit;
}
button{
  cursor:pointer;
  background:#22303c;
}
button.primary{
  background:var(--acc);
  border-color:var(--acc);
  color:#04202c;
  font-weight:600;
}
.mut{
  color:var(--mut);
  font-size:12px;
}
.net{
  padding:7px 9px;
  border:1px solid var(--bord);
  border-radius:8px;
  margin:4px 0;
  cursor:pointer;
  display:flex;
  justify-content:space-between;
  gap:8px;
}
.net:hover{
  background:#202c38;
}
#statusMsg{
  white-space:pre-wrap;
}
.ok{
  color:var(--ok);
}
.err{
  color:var(--err);
}
a{
  color:var(--acc);
  text-decoration:none;
}
a:hover{
  text-decoration:underline;
}
</style>
</head>
<body>

<header>
  <h1>ESP32-C3 Fallback UI</h1>
  <div id="clock" class="mut">--:--:--</div>
</header>

<main>

<div class="card">
  <h2>Статус устройства</h2>
  <div id="statusMsg">Загрузка...</div>
</div>

<div class="card">
  <h2>Настройка WiFi</h2>

  <div class="row">
    <button onclick="scanWifi()">Сканировать сети</button>
    <span class="mut">Если сеть видна, нажмите на неё, чтобы подставить SSID.</span>
  </div>

  <div id="scanList"></div>

  <div class="row">
    <input id="ssid" placeholder="SSID" style="flex:1;min-width:180px">
    <input id="pass" placeholder="Пароль" type="text" autocomplete="off" style="flex:1;min-width:180px">
    <button class="primary" onclick="saveWifi()">Сохранить</button>
  </div>

  <div id="wifiMsg" class="mut"></div>
</div>

<div class="card">
  <h2>Загрузка полного интерфейса</h2>
  <div class="mut">
    Выберите index.html или другой файл для LittleFS.
    После успешной загрузки страница будет перезагружена.
  </div>

  <form id="uiForm" method="POST" action="/api/upload" enctype="multipart/form-data">
    <div class="row">
      <input type="file" name="file" id="uiFile" accept=".html,.css,.js,.png,.ico,.json" style="flex:1">
      <button type="submit" class="primary">Загрузить</button>
      <span id="uiMsg" class="mut"></span>
    </div>
  </form>
</div>

<div class="card">
  <h2>Обновление прошивки</h2>
  <div class="mut">
    Только .bin файл. После успешной прошивки устройство перезагрузится.
  </div>

  <form id="otaForm" method="POST" action="/api/ota" enctype="multipart/form-data">
    <div class="row">
      <input type="file" name="firmware" id="otaFile" accept=".bin" style="flex:1">
      <button type="submit" class="primary">Прошить</button>
      <span id="otaMsg" class="mut"></span>
    </div>
  </form>
</div>

<div class="card">
  <h2>Служебное</h2>
  <div class="row">
    <button onclick="location.href='/'">Открыть главную</button>
    <button onclick="restartEsp()">Перезагрузить</button>
    <button onclick="deleteUi()">Удалить index.html</button>
    <a href="/api/view?name=/sys_log.txt" target="_blank">Лог</a>
    <a href="/api/sysinfo" target="_blank">JSON статуса</a>
    <a href="/api/light" target="_blank">BH1750 JSON</a>
  </div>
  <div id="sysMsg" class="mut"></div>
</div>

</main>

<script>
function $(id){
  return document.getElementById(id);
}

function setMsg(id, text, type){
  var el = $(id);
  if (!el) return;
  el.textContent = text;
  el.className = (type == 1) ? "ok" : ((type == 2) ? "err" : "mut");
}

function refresh(){
  fetch("/api/sysinfo")
    .then(function(r){ return r.json(); })
    .then(function(j){
      if (!j) throw new Error("пустой ответ");

      $("clock").textContent = (j.time && j.time.length > 10) ? j.time.slice(11) : "--:--:--";

      var lines = [];
      lines.push("Режим: " + j.mode + " | IP: " + j.ip);
      lines.push("SSID: " + j.ssid + " | RSSI: " + j.rssi);
      lines.push("Время: " + j.time + " | RTC: " + j.rtc_time);
      lines.push("Прошивка: v" + j.firmware_version + " (" + j.build_date + " " + j.build_time + ")");
      lines.push("Heap: " + j.free_heap + " | PWM: " + j.pwm_duty + "% | Полный UI: " + (j.has_full_ui ? "есть" : "нет"));
      lines.push("BH1750: " + (j.light_found ? (j.light_valid ? Number(j.light_lux).toFixed(2) + " lx" : "нет данных") : "не найден"));

      $("statusMsg").textContent = lines.join("\n");
    })
    .catch(function(e){
      $("statusMsg").textContent = "Нет ответа от /api/sysinfo: " + e;
    });
}

setInterval(refresh, 5000);
refresh();

function scanWifi(){
  setMsg("wifiMsg", "Сканирование...");

  fetch("/api/scan")
    .then(function(r){ return r.json(); })
    .then(function(arr){
      var box = $("scanList");
      box.innerHTML = "";

      if (!arr || !arr.length) {
        setMsg("wifiMsg", "Сети не найдены.", 2);
        return;
      }

      arr.forEach(function(o){
        var d = document.createElement("div");
        d.className = "net";
        d.innerHTML = "<span>" + o.ssid + "</span><span class='mut'>" + o.rssi + " dBm</span>";
        d.onclick = function(){
          $("ssid").value = o.ssid;
          setMsg("wifiMsg", "SSID подставлен: " + o.ssid, 1);
        };
        box.appendChild(d);
      });

      setMsg("wifiMsg", "Найдено сетей: " + arr.length, 1);
    })
    .catch(function(e){
      setMsg("wifiMsg", "Ошибка сканирования: " + e, 2);
    });
}

function saveWifi(){
  var ssid = $("ssid").value.trim();

  if (!ssid) {
    setMsg("wifiMsg", "Введите SSID.", 2);
    return;
  }

  setMsg("wifiMsg", "Сохранение...");

  fetch("/api/save_wifi", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      ssid: ssid,
      password: $("pass").value
    })
  })
  .then(function(r){
    if (r.ok) {
      setMsg("wifiMsg", "Сохранено. Идет перезагрузка...", 1);
    } else {
      return r.text().then(function(t){ throw new Error(t); });
    }
  })
  .catch(function(e){
    setMsg("wifiMsg", "Ошибка сохранения: " + e, 2);
  });
}

function restartEsp(){
  setMsg("sysMsg", "Перезагрузка...");
  fetch("/api/restart").catch(function(){});
  setTimeout(function(){
    location.reload();
  }, 3000);
}

function deleteUi(){
  if (!confirm("Удалить /index.html?")) return;

  fetch("/api/delete_ui")
    .then(function(r){ return r.text(); })
    .then(function(t){
      setMsg("sysMsg", t, 1);
    })
    .catch(function(e){
      setMsg("sysMsg", "Ошибка: " + e, 2);
    });
}

if (window.fetch) {

  $("uiForm").addEventListener("submit", function(e){
    e.preventDefault();

    var f = $("uiFile").files[0];
    if (!f) {
      setMsg("uiMsg", "Выберите файл.", 2);
      return;
    }

    setMsg("uiMsg", "Загрузка...");

    var fd = new FormData();
    fd.append("file", f);

    fetch("/api/upload", {
      method: "POST",
      body: fd
    })
    .then(function(r){
      if (r.ok) {
        setMsg("uiMsg", "OK. Обновляем страницу...", 1);
        setTimeout(function(){
          location.href = "/";
        }, 1200);
      } else {
        return r.text().then(function(t){ throw new Error(t); });
      }
    })
    .catch(function(err){
      setMsg("uiMsg", "Ошибка: " + err.message, 2);
    });
  });

}

function waitDeviceAfterOta() {
  var attempts = 0;
  var maxAttempts = 40;

  setMsg("otaMsg", "Перезагрузка. Ожидание устройства...");

  var timer = setInterval(function() {
    attempts++;

    fetch("/api/sysinfo", { cache: "no-store" })
      .then(function(r) {
        if (!r.ok) throw new Error("bad response");
        return r.json();
      })
      .then(function(j) {
        clearInterval(timer);

        var ver = (j && j.firmware_version) ? j.firmware_version : "неизвестна";

        setMsg(
          "otaMsg",
          "Устройство снова в сети. Версия: " + ver + ". Обновление завершено.",
          1
        );

        setTimeout(function() {
          location.reload();
        }, 2500);
      })
      .catch(function() {
        if (attempts >= maxAttempts) {
          clearInterval(timer);
          setMsg(
            "otaMsg",
            "Не удалось дождаться устройства. Проверьте питание и обновите страницу вручную.",
            2
          );
        }
      });
  }, 2000);
}

$("otaForm").addEventListener("submit", function(e){
  e.preventDefault();

  var f = $("otaFile").files[0];

  if (!f) {
    setMsg("otaMsg", "Выберите .bin файл прошивки.", 2);
    return;
  }

  if (!/\.bin$/i.test(f.name)) {
    setMsg("otaMsg", "Разрешён только файл прошивки .bin", 2);
    return;
  }

  if (!confirm("Обновить прошивку устройству?")) {
    return;
  }

  var fd = new FormData();
  fd.append("firmware", f);

  var xhr = new XMLHttpRequest();
  var uploadComplete = false;

  setMsg("otaMsg", "Подготовка загрузки...");

  xhr.upload.addEventListener("progress", function(ev) {
    if (ev.lengthComputable) {
      var pc = Math.round(ev.loaded / ev.total * 100);
      setMsg("otaMsg", "Загрузка файла на устройство: " + pc + "%");
    }
  });

  xhr.upload.addEventListener("load", function() {
    uploadComplete = true;
    setMsg("otaMsg", "Файл загружен. Завершение записи и проверка...");
  });

  xhr.addEventListener("load", function() {
    try {
      var j = JSON.parse(xhr.responseText);

      if (j.status === "ok") {
        waitDeviceAfterOta();
      } else {
        setMsg("otaMsg", "Ошибка: " + (j.message || "устройство сообщило об ошибке"), 2);
      }
    } catch (err) {
      if (uploadComplete) {
        waitDeviceAfterOta();
      } else {
        setMsg("otaMsg", "Некорректный ответ устройства.", 2);
      }
    }
  });

  xhr.addEventListener("error", function() {
    if (uploadComplete) {
      waitDeviceAfterOta();
    } else {
      setMsg("otaMsg", "Ошибка сети при передаче файла.", 2);
    }
  });

  xhr.addEventListener("timeout", function() {
    setMsg("otaMsg", "Таймаут загрузки файла.", 2);
  });

  xhr.open("POST", "/api/ota");
  xhr.send(fd);
});
</script>

</body>
</html>
)FBHTML";