/*
 * File: board_config.h | Module: BOARD | File ver: 1.0.0 | Proj ver: 3.0.0
 */
#pragma once

// ---- I2C ----
#define PIN_SDA              4
#define PIN_SCL              3
#define DS3231_ADDR          0x68
#define BH1750_ADDR          0x23
#define INA226_ADDR          0x40

// ---- Управляющие выходы (актив HIGH, старт LOW) ----
#define PIN_RESET_DIVIDER    1
#define PIN_RESET_COUNTER    5
#define PIN_SET_SECOND       6
#define PIN_SET_MINUTE       7
#define PIN_SET_HOUR         10

// ---- Мастер-разрешение и меандр ----
#define PIN_MASTER_ENABLE    9    // OUTPUT LOW = разрешено; INPUT(high-Z) = запрет+safe
#define PIN_MEANDER          8    // вход: 0=точки горят, 1=нет; RISING = +1 сек
#define MEANDER_COLON_ON_LEVEL  0

// ---- ШИМ ----
#define PIN_PWM              0
#define PWM_FREQ_HZ          8000UL
#define PWM_SAFE_PERCENT     100   // безопасная скважность (мин. ток)
#define PWM_SETTLE_MS        100UL
#define PWM_START_SETTLE_MS  200UL

// ---- INA226 ----
#define SHUNT_OHM            0.165f
#define INA226_CURRENT_LSB_A 0.00005f   // 0.05 мА

// ---- Токовые лимиты ----
#define I_SEGMENT_MIN_MA     0.5f
#define I_SEGMENT_MAX_MA     10.0f
#define I_TOTAL_HARD_MAX_MA  420.0f

// ---- Точки ----
#define COLON_LED_COUNT      4
#define COLON_DUTY           0.5f

// ---- WiFi / NTP (значения по умолчанию) ----
#define AP_SSID_DEF          "ESP32C3-Clock-Setup"
#define AP_PASS_DEF          ""
#define NTP_SERVER1          "pool.ntp.org"
#define NTP_SERVER2          "time.nist.gov"
#define DEFAULT_TZ_HOURS     7.0f
#define CONFIG_FILE          "/config.json"
#define LOG_FILE             "/sys_log.txt"
#define WIFI_TX_POWER_DEF    WIFI_POWER_2dBm