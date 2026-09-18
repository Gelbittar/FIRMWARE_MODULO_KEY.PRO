#pragma once

// ============================================================
// GEYLCA ONE - Estado global compartido entre modulos
// Todas las definiciones viven en main.cpp; aqui solo extern.
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include <OneWire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <PubSubClient.h>

#include "config.h"

// --- Estructuras de datos ---
struct AccessLog {
    uint32_t timestamp;
    uint8_t  action;
    uint16_t slot;
    uint8_t  status;
};

struct RateLimitEntry {
    unsigned long windowStart;
    int failCount;
    unsigned long blockedUntil;
};

// --- Objetos compartidos (definidos en main.cpp) ---
extern Preferences preferences;
extern OneWire ibutton;
extern WebServer server;
extern WiFiClientSecure secureClient;
extern WiFiClient plainClient;
extern PubSubClient mqttClient;

// --- Identidad y credenciales ---
extern String deviceId;
extern String deviceSecret;
extern String adminKey;
extern String installerKey;

// --- Modos de operacion ---
extern bool modoConfiguracion;
extern bool modoProgramacionLocal;
extern bool staWebActive;
extern unsigned long staWebStartTime;
extern unsigned long tiempoInicioPress;
extern unsigned long ultimoIntentoMQTT;

// --- Configuracion de acceso ---
extern int relayTimeDefault;
extern int securityMode;
extern int famFilter;
extern bool rewriteProbe;
extern bool doorEnabled;
extern unsigned long doorTimeoutMs;
extern bool doorClosedIsLow;
extern bool buzzerEnabled;
extern unsigned long buzzerPreMs;
extern String ntfyUrl;
extern String ntfyTopic;

// --- Aprendizaje / slots ---
extern bool learningMode;
extern unsigned long learningStartedAt;
extern String scannedKeyHex;
extern unsigned long ultimoHeartbeat;
extern int targetSlot;
extern bool slotsBatchPublishing;
extern int slotsBatchCursor;
extern bool slotsWiping;
extern unsigned int wipeBytesPos;
extern unsigned long ultimoProcesoLlave;
extern String targetApto;

// --- Monitoreo de puerta / buzzer ---
extern bool doorMonitoring;
extern unsigned long doorOpenSince;
extern unsigned long doorClosedSince;
extern int doorMonitorSlot;
extern unsigned long lastBuzzerToggle;
extern bool buzzerState;

// --- Bitmap de ocupacion ---
extern byte occupiedBitmap[SLOTS_BITMAP_LEN];
extern int occupiedCount;
extern bool ocRefreshThisSession;

// --- Relay / LED ---
extern bool relayActive;
extern unsigned long relayOffTime;
extern unsigned long lastStatusBlink;
extern bool ledState;

// --- Emparejamiento ---
extern unsigned long pairingExpiry;

// --- Rate limit ---
extern RateLimitEntry rateLimitEntries[RATE_LIMIT_MAX];
extern int rateLimitIndex;

// --- Logs ---
extern AccessLog logBuffer[MAX_LOG_ENTRIES];
extern int logWriteIndex;
extern bool logsLoaded;
extern int logCount;