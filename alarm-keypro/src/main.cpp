/*
 * KeyPro Alarm — Módulo de alarma (ESP-01 / ESP8266 y ESP32 / WROOM-32U)
 *
 * Reutiliza el patrón WiFi del "código principal del programador":
 *   - WiFiManager (portal cautivo "KeyPro-XXXX", configura la red de casa
 *     sin teclear IP: Android avisa "iniciar sesión en la red" al conectarse)
 *   - LittleFS /config.json + /pairing.json
 *   - MQTT TLS (8883) con payloads cifrados AES-256-GCM end-to-end.
 *
 * Pines:
 *   ESP-01 (ESP8266):  GPIO1 (TX) -> Relé ARMA   (activo-LOW, PNP high-side)
 *                      GPIO0      -> Relé PÁNICO  (activo-LOW)
 *                      GPIO3 (RX) -> SENSADO (HIGH reposo; LOW = pulso activo)
 *                      GPIO2      -> Botón RESET config (INPUT_PULLUP; 5 s)
 *   ESP32 (WROOM-32U): GPIO27 -> Relé ARMA · GPIO26 -> Relé PÁNICO
 *                      GPIO25 -> SENSADO · GPIO16 -> Botón RESET config
 *                      (evita pines de strapping 0/2/12/15)
 *
 * Protocolo (cifrado AES-256-GCM, secret del QR):
 *   App   -> keypro/{dev}/cmd   {"v":1,"t":epoch,"n":nonce_b64,"c":cipher_b64}
 *   Modul -> keypro/{dev}/state (retained) y keypro/{dev}/events
 *   plaintext: {"a":"arm"} | {"a":"disarm"} | {"a":"panic"} | {"a":"set_cfg",...}
 *   eventos: ARMED, DISARMED, TRIGGERED, ALARM, COMMAND_ARM, COMMAND_DISARM,
 *            PANIC, ONLINE, OFFLINE, CONFIG_SAVED
 *
 * Sensado de sirena (una única ventana, senseWindowMs):
 *   1 pulso -> ARMADO · 2 pulsos -> DESARMADO
 *   señal mantenida LOW toda la ventana -> ALARMA (TRIGGERED)
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <esp_random.h>
  #include <mbedtls/gcm.h>
#else
  #include <ESP8266WiFi.h>
  #include <bearssl/bearssl.h>
#endif
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#define FIRMWARE_VERSION "1.3.0"

// ------------------------------ PINES --------------------------------
#if defined(ESP32)
#define RELAY_ARM_PIN   27  // GPIO27 -> Relé ARMA/DESARMA
#define RELAY_PANIC_PIN 26  // GPIO26 -> Relé PÁNICO
#define SENSE_PIN       25  // GPIO25 -> Entrada sensado sirena
#define RESET_BTN_PIN   16  // GPIO16 -> Botón RESET config
#else
#define RELAY_ARM_PIN   1   // GPIO1 / TX
#define RELAY_PANIC_PIN 0   // GPIO0
#define SENSE_PIN       3   // GPIO3 / RX
#define RESET_BTN_PIN   2   // GPIO2
#endif

// --------------------------- CONSTANTES ------------------------------
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 8883
#define MQTT_TLS_INSECURE 1

// Umbral de sensado (configurable via set_cfg): una única ventana general.
// Dentro de ese transcurso el módulo detecta automáticamente si recibió
// 1 pulso (ARMADO), 2 pulsos (DESARMADO) o la señal se mantuvo (ALARMA).
#define DEF_SENSE_WINDOW_MS 2000
// Comportamiento del relé ARMA: 0=mantenido, 1=pulso
#define DEF_RELAY_MODE      0
#define DEF_PULSE_ARM_MS    300
#define DEF_PANIC_MS        1500
#define DEF_RESET_HOLD_MS   5000

#define SAMPLE_MS           10
#define DEBOUNCE_MS         25
#define STATE_PUBLISH_MS    30000
#define REPLAY_EARLIEST     1567000000UL   // ~2019 (reloj sin NTP)
#define REPLAY_WINDOW_MS    15000UL

// ---------------------------- AYUDA 64/HEX ---------------------------
static const char B64T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char HEXT[] = "0123456789ABCDEF";

String base64Encode(const uint8_t* data, size_t len) {
    String out; out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out += B64T[(v >> 18) & 63]; out += B64T[(v >> 12) & 63];
        out += B64T[(v >> 6) & 63];  out += B64T[v & 63];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = data[i] << 16;
        out += B64T[(v >> 18) & 63]; out += B64T[(v >> 12) & 63]; out += "==";
    } else if (rem == 2) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8);
        out += B64T[(v >> 18) & 63]; out += B64T[(v >> 12) & 63];
        out += B64T[(v >> 6) & 63];  out += "=";
    }
    return out;
}

int b64Val(char c) {
    for (int i = 0; i < 64; i++) if (B64T[i] == c) return i;
    return -1;
}

bool base64Decode(const String& in, uint8_t* out, size_t maxLen, size_t* outLen) {
    size_t n = 0, val = 0, bits = 0;
    for (size_t i = 0; i < in.length() && in[i] != '='; i++) {
        int d = b64Val(in[i]);
        if (d < 0) continue;
        val = (val << 6) | d; bits += 6;
        if (bits >= 8) { bits -= 8; if (n < maxLen) out[n++] = (val >> bits) & 0xFF; }
    }
    *outLen = n;
    return n <= maxLen;
}

String hexEncode(const uint8_t* data, size_t len) {
    String out; out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) { out += HEXT[data[i] >> 4]; out += HEXT[data[i] & 15]; }
    return out;
}

bool hexDecode(const String& inHex, uint8_t* out, size_t maxLen) {
    for (size_t i = 0; i + 1 < inHex.length() && i / 2 < maxLen; i += 2) {
        int hi = inHex[i] >= '0' && inHex[i] <= '9' ? inHex[i] - '0'
                : (inHex[i] & ~32) - 'A' + 10;
        int lo = inHex[i+1] >= '0' && inHex[i+1] <= '9' ? inHex[i+1] - '0'
                : (inHex[i+1] & ~32) - 'A' + 10;
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return false;
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

// ------------------------ CONFIGURACIÓN (FS) -------------------------
String dev = "KP";
String secretHex = "";
uint8_t aesKey[32];

struct Cfg {
    uint8_t relayMode = DEF_RELAY_MODE;   // 0 mantenido / 1 pulso
    unsigned long pulseArmMs = DEF_PULSE_ARM_MS;
    unsigned long panicMs = DEF_PANIC_MS;
    unsigned long senseWindowMs = DEF_SENSE_WINDOW_MS;
    unsigned long resetHoldMs = DEF_RESET_HOLD_MS;
    String state = "UNKNOWN";
} cfg;

String pairingPath = "/pairing.json";
String configPath = "/config.json";

void savePairing() {
    DynamicJsonDocument doc(192);
    doc["dev"] = dev;
    doc["secret"] = secretHex;
    File f = LittleFS.open(pairingPath, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

void loadOrCreatePairing() {
    bool ok = false;
    if (LittleFS.exists(pairingPath)) {
        File f = LittleFS.open(pairingPath, "r");
        if (f) {
            DynamicJsonDocument doc(192);
            DeserializationError err = deserializeJson(doc, f);
            if (!err) {
                String d = doc["dev"] | "";
                String s = doc["secret"] | "";
                if (d.length() && s.length() == 64) { dev = d; secretHex = s; ok = true; hexDecode(secretHex, aesKey, 32); }
            }
            f.close();
        }
    }
    if (!ok) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        dev = "KP";
        for (int i = 2; i < 6; i++) { dev += HEXT[mac[i] >> 4]; dev += HEXT[mac[i] & 15]; }
        byte rnd[32];
    #if defined(ESP32)
        for (int i = 0; i < 32; i++) rnd[i] = (byte)esp_random();
    #else
        for (int i = 0; i < 32; i++) rnd[i] = (byte)os_random();
    #endif
        rnd[0] ^= mac[0]; rnd[1] ^= mac[5];
        secretHex = hexEncode(rnd, 32);
        hexDecode(secretHex, aesKey, 32);
        savePairing();
    }
}

void loadConfig() {
    if (LittleFS.exists(configPath)) {
        File f = LittleFS.open(configPath, "r");
        if (f) {
            DynamicJsonDocument doc(256);
            DeserializationError err = deserializeJson(doc, f);
            if (!err) {
                cfg.relayMode = doc["relayMode"] | cfg.relayMode;
                cfg.pulseArmMs = doc["pulseArmMs"] | cfg.pulseArmMs;
                cfg.panicMs = doc["panicMs"] | cfg.panicMs;
                cfg.senseWindowMs = doc["senseWindowMs"] | doc["pulseWindowMs"] | cfg.senseWindowMs;
                cfg.state = doc["state"] | "UNKNOWN";
            }
            f.close();
        }
    }
}

void saveConfig() {
    DynamicJsonDocument doc(256);
    doc["relayMode"] = cfg.relayMode;
    doc["pulseArmMs"] = cfg.pulseArmMs;
    doc["panicMs"] = cfg.panicMs;
    doc["senseWindowMs"] = cfg.senseWindowMs;
    doc["state"] = cfg.state;
    File f = LittleFS.open(configPath, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// ----------------------------- CRYPTO --------------------------------
#if defined(ESP32)
static mbedtls_gcm_context gcmCtx;
static bool cryptoReady = false;

bool cryptoInit() {
    mbedtls_gcm_init(&gcmCtx);
    if (mbedtls_gcm_setkey(&gcmCtx, MBEDTLS_CIPHER_ID_AES, aesKey, 256) != 0) return false;
    cryptoReady = true;
    return true;
}

// cifra plain -> base64(iv(12)+ct(n)+tag(16))
bool encryptToB64(const uint8_t* plain, size_t plainLen, const char* add, size_t addLen, String& outB64) {
    byte iv[12]; for (int i = 0; i < 12; i++) iv[i] = (byte)esp_random();
    uint8_t ct[512];
    uint8_t tag[16];
    if (mbedtls_gcm_crypt_and_tag(&gcmCtx, MBEDTLS_GCM_ENCRYPT, plainLen,
            iv, 12, (const uint8_t*)add, addLen, plain, ct, 16, tag) != 0) return false;
    uint8_t blob[12 + 512 + 16];
    memcpy(blob, iv, 12);
    memcpy(blob + 12, ct, plainLen);
    memcpy(blob + 12 + plainLen, tag, 16);
    outB64 = base64Encode(blob, 12 + plainLen + 16);
    return true;
}

bool decryptFromB64(const String& inB64, const char* add, size_t addLen, uint8_t* plain, size_t maxPlain, size_t* plainLen) {
    uint8_t blob[12 + 512 + 16];
    size_t blobLen = 0;
    if (!base64Decode(inB64, blob, sizeof(blob), &blobLen)) return false;
    if (blobLen < 12 + 16) return false;
    size_t ctLen = blobLen - 12 - 16;
    if (ctLen > maxPlain) return false;
    if (mbedtls_gcm_auth_decrypt(&gcmCtx, ctLen, blob, 12,
            (const uint8_t*)add, addLen, blob + 12 + ctLen, 16, blob + 12, plain) != 0) return false;
    *plainLen = ctLen;
    return true;
}

#else
static br_aes_ct_ctr_keys bctx;
static br_gcm_context gcmCtx;
static bool cryptoReady = false;

bool cryptoInit() {
    br_aes_ct_ctr_init(&bctx, aesKey, 32);
    br_gcm_init(&gcmCtx, &bctx.vtable, &br_ghash_ctmul);
    cryptoReady = true;
    return true;
}

// cifra plain -> base64(iv(12)+ct(n)+tag(16))
bool encryptToB64(const uint8_t* plain, size_t plainLen, const char* add, size_t addLen, String& outB64) {
    byte iv[12]; for (int i = 0; i < 12; i++) iv[i] = (byte)os_random();
    uint8_t buf[512]; memcpy(buf, plain, plainLen);
    br_gcm_reset(&gcmCtx, iv, 12);
    br_gcm_aad_inject(&gcmCtx, add, addLen);
    br_gcm_flip(&gcmCtx);
    br_gcm_run(&gcmCtx, 1, buf, plainLen);
    uint8_t tag[16];
    br_gcm_get_tag(&gcmCtx, tag);
    uint8_t blob[12 + 512 + 16];
    memcpy(blob, iv, 12);
    memcpy(blob + 12, buf, plainLen);
    memcpy(blob + 12 + plainLen, tag, 16);
    outB64 = base64Encode(blob, 12 + plainLen + 16);
    return true;
}

bool decryptFromB64(const String& inB64, const char* add, size_t addLen, uint8_t* plain, size_t maxPlain, size_t* plainLen) {
    uint8_t blob[12 + 512 + 16];
    size_t blobLen = 0;
    if (!base64Decode(inB64, blob, sizeof(blob), &blobLen)) return false;
    if (blobLen < 12 + 16) return false;
    size_t ctLen = blobLen - 12 - 16;
    if (ctLen > maxPlain) return false;
    br_gcm_reset(&gcmCtx, blob, 12);
    br_gcm_aad_inject(&gcmCtx, add, addLen);
    br_gcm_flip(&gcmCtx);
    br_gcm_run(&gcmCtx, 0, blob + 12, ctLen);
    if (br_gcm_check_tag(&gcmCtx, blob + 12 + ctLen) != 1) return false;
    memcpy(plain, blob + 12, ctLen);
    *plainLen = ctLen;
    return true;
}
#endif

// ------------------------------ MQTT --------------------------------
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

String topicCmd()   { return "keypro/" + dev + "/cmd"; }
String topicState() { return "keypro/" + dev + "/state"; }
String topicEvt()   { return "keypro/" + dev + "/events"; }

bool clockSynced() { return time(nullptr) > REPLAY_EARLIEST; }
time_t nowEpoch()  { return time(nullptr); }

void publishCrypto(const char* topic, bool retained, const char* plainJson) {
    if (!mqtt.connected() || !cryptoReady) return;
    String body = (String)plainJson;
    String addr = String(topic) + "|" + String((long)nowEpoch());
    String cipher;
    if (!encryptToB64((const uint8_t*)body.c_str(), body.length(), addr.c_str(), addr.length(), cipher)) return;
    DynamicJsonDocument doc(384);
    doc["v"] = 1;
    doc["t"] = (long)nowEpoch();
    doc["n"] = "1";
    doc["c"] = cipher;
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(topic, payload.c_str(), retained);
}

void publishEvent(const char* ev) {
    DynamicJsonDocument doc(192);
    doc["ev"] = ev;
    doc["dev"] = dev;
    String body;
    serializeJson(doc, body);
    publishCrypto(topicEvt().c_str(), false, body.c_str());
    Serial.printf("[KP] event %s\n", ev);
}

void setState(const String& st) {
    if (cfg.state != st) {
        cfg.state = st;
        saveConfig();
        if (st == "ARMED") publishEvent("ARMED");
        if (st == "DISARMED") publishEvent("DISARMED");
        if (st == "TRIGGERED") publishEvent("ALARM");
    }
    DynamicJsonDocument doc(512);
    doc["st"] = cfg.state;
    doc["dev"] = dev;
    doc["relayMode"] = cfg.relayMode;
    doc["pulseArmMs"] = cfg.pulseArmMs;
    doc["panicMs"] = cfg.panicMs;
    doc["senseWindowMs"] = cfg.senseWindowMs;
    String body;
    serializeJson(doc, body);
    publishCrypto(topicState().c_str(), true, body.c_str());
}

// relé activo-LOW: LOW enciende
void relayArm(bool on)      { digitalWrite(RELAY_ARM_PIN, on ? LOW : HIGH); }
void relayPanicOn()         { digitalWrite(RELAY_PANIC_PIN, LOW); }
void relayPanicOff()        { digitalWrite(RELAY_PANIC_PIN, HIGH); }

void pulsePanic() {
    relayPanicOn();
    delay(cfg.panicMs);
    relayPanicOff();
}

void doArm() {
    if (cfg.relayMode == 0) {
        relayArm(true);
        setState("ARMED");
        publishEvent("COMMAND_ARM");
    } else {
        relayArm(true);
        delay(cfg.pulseArmMs);
        relayArm(false);
        publishEvent("COMMAND_ARM");
    }
}

void doDisarm() {
    if (cfg.relayMode == 0) {
        relayArm(false);
        setState("DISARMED");
        publishEvent("COMMAND_DISARM");
    } else {
        if (cfg.state == "ARMED" || cfg.state == "TRIGGERED") {
            relayArm(true);
            delay(cfg.pulseArmMs);
            relayArm(false);
        }
        publishEvent("COMMAND_DISARM");
    }
}

void doPanic() {
    pulsePanic();
    publishEvent("PANIC");
}

void applyCfg(const DynamicJsonDocument& d) {
    if (d.containsKey("relayMode")) cfg.relayMode = d["relayMode"] | 0;
    if (d.containsKey("pulseArmMs")) cfg.pulseArmMs = d["pulseArmMs"] | DEF_PULSE_ARM_MS;
    if (d.containsKey("panicMs")) cfg.panicMs = d["panicMs"] | DEF_PANIC_MS;
    if (d.containsKey("senseWindowMs")) cfg.senseWindowMs = d["senseWindowMs"] | DEF_SENSE_WINDOW_MS;
    else if (d.containsKey("pulseWindowMs")) cfg.senseWindowMs = d["pulseWindowMs"] | DEF_SENSE_WINDOW_MS;
    saveConfig();
    publishEvent("CONFIG_SAVED");
}

void handleCmd(const String& plain) {
    DynamicJsonDocument d(384);
    DeserializationError err = deserializeJson(d, plain);
    if (err) return;
    String a = d["a"] | "";
    if (a == "arm") doArm();
    else if (a == "disarm") doDisarm();
    else if (a == "panic") doPanic();
    else if (a == "set_cfg") applyCfg(d);
    else if (a == "get_state") setState(cfg.state);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    DynamicJsonDocument env(384);
    DeserializationError err = deserializeJson(env, msg);
    if (err) return;
    int v = env["v"] | 0;
    long t = env["t"] | 0L;
    String cipher = env["c"] | "";
    if (v != 1 || cipher.length() == 0) return;
    time_t nowSec = nowEpoch();
    if (clockSynced()) {
        if (t < nowSec - (long)REPLAY_WINDOW_MS || t > nowSec + (long)REPLAY_WINDOW_MS) {
            publishEvent("REPLAY_REJECTED");
            return;
        }
    }
    String addr = String(topic) + "|" + String(t);
    uint8_t plain[512];
    size_t plainLen = 0;
    if (!decryptFromB64(cipher, addr.c_str(), addr.length(), plain, sizeof(plain), &plainLen)) {
        publishEvent("DECRYPT_FAILED");
        return;
    }
    String p = "";
    for (size_t i = 0; i < plainLen; i++) p += (char)plain[i];
    handleCmd(p);
}

void mqttReconnect() {
    while (!mqtt.connected()) {
        if (!clockSynced()) {
            delay(2000);
            continue;
        }
        #if defined(ESP32)
        uint32_t chipId = ESP.getEfuseMac();
        #else
        uint32_t chipId = ESP.getChipId();
        #endif
        String clientId = "KP_" + dev + "_" + String(chipId);
        String lwt = "{\"v\":1,\"t\":" + String((long)nowEpoch()) + ",\"n\":\"1\",\"c\":\"\"}";
        if (mqtt.connect(clientId.c_str(), topicState().c_str(), 0, true, lwt.c_str())) {
            mqtt.subscribe(topicCmd().c_str());
            setState(cfg.state);
            publishEvent("ONLINE");
        } else {
            delay(2500);
        }
    }
}

// --------------------------- SENSADO (sirena) -------------------------
// Una única ventana (cfg.senseWindowMs): dentro de ese transcurso el módulo
// detecta automáticamente si recibió 1 pulso (ARMADO), 2 pulsos (DESARMADO)
// o la señal se mantuvo LOW toda la ventana (ALARMA). El reporte a la app es
// el propio estado/evento publicado por setState().
bool rawHigh = true, stableHigh = true;
unsigned long stableSince = 0, sampleLast = 0, windowStart = 0;
bool inPulse = false, windowOpen = false, reported = false;
int pulseCount = 0;

void senseTick() {
    unsigned long now = millis();
    if (now - sampleLast < SAMPLE_MS) return;
    sampleLast = now;

    bool h = digitalRead(SENSE_PIN) == HIGH;
    if (h != rawHigh) { rawHigh = h; stableSince = now; }   // rebote: aún no estable

    bool changed = false;
    if (h != stableHigh && now - stableSince >= DEBOUNCE_MS) {  // flanco ya estable
        stableHigh = h;
        changed = true;
    }

    if (changed) {
        if (h) {  // flanco HIGH: finalizó un pulso
            if (inPulse) { inPulse = false; pulseCount++; }
        } else {  // flanco LOW: inicio de un pulso / señal mantenida
            if (!windowOpen) { windowOpen = true; windowStart = now; pulseCount = 0; reported = false; }
            inPulse = true;
        }
    }

    if (!windowOpen) return;

    // cierre de ventana (se evalúa cada tick, no solo en flancos)
    if (now - windowStart >= cfg.senseWindowMs) {
        windowOpen = false;
        inPulse = false;
        if (!reported) {
            reported = true;
            if (!stableHigh && pulseCount == 0) {
                setState("TRIGGERED");  // se mantuvo LOW toda la ventana -> ALARMA
            } else if (pulseCount >= 2) {
                setState("DISARMED");
            } else if (pulseCount == 1) {
                setState("ARMED");
            }
        }
        pulseCount = 0;
    }
}

// --------------------------- RESET CONFIG ----------------------------
unsigned long btnDownSince = 0;
bool btnWasDown = false;

void resetBtnTick() {
    bool down = digitalRead(RESET_BTN_PIN) == LOW;
    if (down && !btnWasDown) { btnDownSince = millis(); }
    btnWasDown = down;
    if (down && millis() - btnDownSince >= cfg.resetHoldMs) {
        WiFiManager wm;
        wm.resetSettings();
        LittleFS.remove(configPath);
        delay(500);
        ESP.restart();
    }
}

// ------------------------------ SETUP ---------------------------------
void setup() {
    Serial.begin(115200);
    delay(120);

    #ifdef ESP32
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }
#else
    LittleFS.begin();
#endif

    WiFi.mode(WIFI_STA);
    loadOrCreatePairing();
    loadConfig();
    cryptoInit();

    // Línea de provisionamiento (se imprime ANTES de habilitar TX como GPIO)
    Serial.printf("KP_PAIR {\"dev\":\"%s\",\"secret\":\"%s\"}\n", dev.c_str(), secretHex.c_str());
    Serial.printf("KeyPro Alarm %s\n", FIRMWARE_VERSION);
    Serial.flush();
    delay(200);
    Serial.end();

    // Configurar pines de salida (activo-LOW => relés OFF con HIGH)
    pinMode(RELAY_ARM_PIN, OUTPUT);
    pinMode(RELAY_PANIC_PIN, OUTPUT);
    digitalWrite(RELAY_ARM_PIN, HIGH);
    digitalWrite(RELAY_PANIC_PIN, HIGH);
    pinMode(SENSE_PIN, INPUT);
    pinMode(RESET_BTN_PIN, INPUT_PULLUP);

    #if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
    WiFi.setSleep(false);
#endif
    configTime(-3 * 3600, 0, "pool.ntp.org");

    String apName = "KeyPro-" + dev.substring(2);
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);
    wifiManager.setConfigPortalTimeout(180);
    if (!wifiManager.autoConnect(apName.c_str(), "12345678")) {
        delay(3000);
        ESP.restart();
    }

    secureClient.setInsecure();
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);
    mqtt.setBufferSize(768);
    mqtt.setKeepAlive(40);
    mqtt.setSocketTimeout(12);
}

// ------------------------------- LOOP ---------------------------------
unsigned long lastStatePub = 0;

void loop() {
    resetBtnTick();
    senseTick();

    if (!mqtt.connected()) {
        mqttReconnect();
    }
    mqtt.loop();

    if (mqtt.connected() && millis() - lastStatePub > STATE_PUBLISH_MS) {
        lastStatePub = millis();
        setState(cfg.state);
    }
    yield();
}