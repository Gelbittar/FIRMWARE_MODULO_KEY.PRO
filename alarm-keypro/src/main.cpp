/*
 * KeyPro Alarm — Módulo de alarma V1.0 (ESP-01 / ESP8266)
 *
 * Reutiliza el patrón WiFi del "código principal del programador":
 *   - WiFiManager (portal cautivo "KeyPro-XXXX", configura la red de casa
 *     sin teclear IP: Android avisa "iniciar sesión en la red" al conectarse)
 *   - LittleFS /config.json + /pairing.json
 *   - MQTT TLS (8883) con payloads cifrados AES-256-GCM end-to-end.
 *
 * Pines (ESP-01, sin expansor):
 *   GPIO1 (TX) -> Relé ARMA/DESARMA   (salida activo-LOW, PNP high-side)
 *   GPIO0      -> Relé PÁNICO          (salida activo-LOW, PNP high-side)
 *   GPIO3 (RX) -> Entrada SENSADO sirena (HIGH reposo; LOW = pulso activo)
 *   GPIO2      -> Botón RESET config   (INPUT_PULLUP; mantener 5 s)
 *
 * Protocolo (cifrado AES-256-GCM, secret del QR):
 *   App   -> keypro/{dev}/cmd   {"v":1,"t":epoch,"n":nonce_b64,"c":cipher_b64}
 *   Modul -> keypro/{dev}/state (retained) y keypro/{dev}/events
 *   plaintext: {"a":"arm"} | {"a":"disarm"} | {"a":"panic"} | {"a":"set_cfg",...}
 *   eventos: ARMED, DISARMED, TRIGGERED, ALARM, COMMAND_ARM, COMMAND_DISARM,
 *            PANIC, ONLINE, OFFLINE, CONFIG_SAVED
 *
 * Decodificación del sensado de sirena:
 *   1 pulso corto  -> ARMADO
 *   2 pulsos       -> DESARMADO
 *   1 pulso >=30 s -> ALARMA ACTIVADA (TRIGGERED)
 */

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <bearssl/bearssl.h>
#include <time.h>

#define FIRMWARE_VERSION "1.0.0"

// ------------------------------ PINES --------------------------------
#define RELAY_ARM_PIN   1   // GPIO1 / TX
#define RELAY_PANIC_PIN 0   // GPIO0
#define SENSE_PIN       3   // GPIO3 / RX
#define RESET_BTN_PIN   2   // GPIO2

// --------------------------- CONSTANTES ------------------------------
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 8883
#define MQTT_TLS_INSECURE 1

// Umbrales de sensado (configurables via set_cfg)
#define DEF_PULSE_MIN_MS    120
#define DEF_PULSE_MAX_MS    3000
#define DEF_PULSE_WINDOW_MS 2500
#define DEF_TRIGGER_MS      30000
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
    unsigned long pulseMinMs = DEF_PULSE_MIN_MS;
    unsigned long pulseMaxMs = DEF_PULSE_MAX_MS;
    unsigned long pulseWindowMs = DEF_PULSE_WINDOW_MS;
    unsigned long triggerMs = DEF_TRIGGER_MS;
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
        for (int i = 0; i < 32; i++) rnd[i] = (byte)os_random();
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
                cfg.pulseMinMs = doc["pulseMinMs"] | cfg.pulseMinMs;
                cfg.pulseMaxMs = doc["pulseMaxMs"] | cfg.pulseMaxMs;
                cfg.pulseWindowMs = doc["pulseWindowMs"] | cfg.pulseWindowMs;
                cfg.triggerMs = doc["triggerMs"] | cfg.triggerMs;
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
    doc["pulseMinMs"] = cfg.pulseMinMs;
    doc["pulseMaxMs"] = cfg.pulseMaxMs;
    doc["pulseWindowMs"] = cfg.pulseWindowMs;
    doc["triggerMs"] = cfg.triggerMs;
    doc["state"] = cfg.state;
    File f = LittleFS.open(configPath, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// ----------------------------- CRYPTO --------------------------------
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
    DynamicJsonDocument doc(128);
    doc["st"] = cfg.state;
    doc["dev"] = dev;
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
    if (d.containsKey("pulseMinMs")) cfg.pulseMinMs = d["pulseMinMs"] | DEF_PULSE_MIN_MS;
    if (d.containsKey("pulseMaxMs")) cfg.pulseMaxMs = d["pulseMaxMs"] | DEF_PULSE_MAX_MS;
    if (d.containsKey("pulseWindowMs")) cfg.pulseWindowMs = d["pulseWindowMs"] | DEF_PULSE_WINDOW_MS;
    if (d.containsKey("triggerMs")) cfg.triggerMs = d["triggerMs"] | DEF_TRIGGER_MS;
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
        String clientId = "KP_" + dev + "_" + ESP.getChipId();
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
bool rawHigh = true, stableHigh = true;
unsigned long stableSince = 0, sampleLast = 0, pulseStart = 0, windowStart = 0;
bool inPulse = false, windowOpen = false;
int pulseCount = 0;
bool triggeredFired = false;

void fireTriggered() {
    if (cfg.state != "TRIGGERED") {
        cfg.state = "TRIGGERED";
        saveConfig();
        publishEvent("ALARM");
    }
    publishCrypto(topicState().c_str(), true, "{\"st\":\"TRIGGERED\"}");
    triggeredFired = true;
    windowOpen = false; pulseCount = 0;
}

void senseTick() {
    unsigned long now = millis();
    if (now - sampleLast < SAMPLE_MS) return;
    sampleLast = now;

    bool h = digitalRead(SENSE_PIN) == HIGH;
    if (h != rawHigh) { rawHigh = h; stableSince = now; }
    if (now - stableSince < DEBOUNCE_MS) return;
    if (h == stableHigh) return;
    stableHigh = h;

    if (h) {  // flanco HIGH: fin de pulso (o fin de alarma sostenida)
        if (inPulse) {
            inPulse = false;
            unsigned long d = now - pulseStart;
            if (!triggeredFired && d >= cfg.triggerMs) {
                fireTriggered();
            } else if (!triggeredFired && d >= cfg.pulseMinMs) {
                pulseCount++;
            }
        }
    } else {  // flanco LOW: inicio de pulso
        if (!windowOpen) { windowOpen = true; windowStart = now; pulseCount = 0; }
        inPulse = true;
        pulseStart = now;
    }

    // alarma sostenida mientras sigue LOW
    if (inPulse && !triggeredFired && now - pulseStart >= cfg.triggerMs) {
        fireTriggered();
    }

    // cierre de ventana para decidir 1 (ARMADO) vs 2 (DESARMADO) pulsos
    if (windowOpen && !inPulse && !triggeredFired && now - windowStart > cfg.pulseWindowMs) {
        windowOpen = false;
        if (pulseCount >= 2) setState("DISARMED");
        else if (pulseCount == 1) setState("ARMED");
        pulseCount = 0;
    }

    if (!inPulse && triggeredFired) triggeredFired = false;
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

    LittleFS.begin();

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

    WiFi.setSleepMode(WIFI_NONE_SLEEP);
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