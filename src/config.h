#pragma once

// ============================================================
// GEYLCA ONE - Configuracion global del firmware
// ============================================================

// --- Pines ESP32 ---
#define PIN_SDA      25
#define PIN_SCL      26
#define PIN_IBUTTON  14
#define PIN_RELAY    27
#define PIN_LED      13
#define PIN_BTN_PROG 4
#define PIN_DOOR     32
#define PIN_BUZZER   33

// --- EEPROM externa (24LC128, 16 KB en 0x50) ---
#define EEPROM_ADDR  0x50

// --- Capacidades ---
#define MAX_SLOTS       4000
#define MAX_LOG_ENTRIES 100

#define FIRMWARE_VERSION "2.5.0"

// --- PINs de roles ---
#define PIN_LEN 6
#define DEFAULT_ADMIN_PIN "123456"
#define DEFAULT_INSTALLER_PIN "654321"

// --- Modos de seguridad de casilla ---
#define SECMODE_SOVICA 3   // serie[0..2] + pad 0x00 por casilla (memoria SOVICA)

// --- Bitmap de ocupacion ---
#define SLOTS_BITMAP_LEN ((MAX_SLOTS + 7) / 8)

// --- Rate limit ---
#define RATE_LIMIT_MAX     10
#define RATE_LIMIT_WINDOW  60000
#define RATE_LIMIT_BLOCK   300000

// --- Ventana de emparejamiento (ms) ---
#define PAIRING_WINDOW     600000

// --- MQTT ---
#define MQTT_SERVER "broker.hivemq.com"
#define MQTT_PORT   8883
#define INTERVALO_REINTENTO_MQTT 5000

// --- NTP ---
#define NTP_SERVER     "pool.ntp.org"
#define GMT_OFFSET_SEC (-14400)
#define DST_OFFSET_SEC 0