#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <OneWire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <time.h>
#include <Update.h>
#include <HTTPClient.h>
#include <esp_efuse.h>

#define DEBUG_MODE 0

#if DEBUG_MODE
  #define DBG(x) Serial.println(x)
#else
  #define DBG(x)
#endif

#define PIN_SDA      25
#define PIN_SCL      26
#define PIN_IBUTTON  14
#define PIN_RELAY    27
#define PIN_LED      13
#define PIN_BTN_PROG 4
#define EEPROM_ADDR  0x50

#define MAX_SLOTS    4000
int slotSize = 8;

#define FIRMWARE_VERSION "2.3.0"

#define PIN_LEN 6
#define DEFAULT_ADMIN_PIN "123456"
#define DEFAULT_INSTALLER_PIN "654321"

bool isPin6(String k);

#define MAX_LOG_ENTRIES 100

struct AccessLog {
    uint32_t timestamp;
    uint8_t  action;
    uint16_t slot;
    uint8_t  status;
};

String deviceId = "modulo01";
String deviceSecret = "";
String adminKey = "";
String installerKey = "";

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 8883;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -14400;
const int   dstOffset_sec = 0;

const char rootCA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6
UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+s
WT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qy
HB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+
UCB5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHz
Uvjg0yIhj6st7V+FmuFnSyA6TpGmm8uKl6fMmbMLA7m/aRfEXvXzKMNKJuRCkyF
GM1T2MRnlO0XRFB/raynFKVS4UCmToBdIZ8fdfEfIBFTVsO9JODnHhIoyOvu9tL
eP2V12OGShIE12MiFsSnbIq7R8VlDe6rzKdENvOjogCfbz9b8a6yNkFaL8NDi9L
54U2uMWrNcOBPV/g+VnhPPINic6LSVPKfsq0IzI0qBGQMzmfvq0GIU4Ln8aQL2eg
itN5sfU5boPlCfni6M4iFB5lPNcj0wnTcQ7EHMpZVEBraKBsN77nG4k7brcO8vr
k9OGmBhi4b+dv0vMVe33x0hizEUpJ1HcAo2gofE+QsHLSxR34p3DPHxIR4S4nIv
upE1xt5IuNzVVHx6eau5RfSuNhGBW2F7DpHxKrf1wO4kS3gOFpFSvyzZNVmlCSr
4acwY2el9Ei/jrLfGY9jGpOjMA9Q1JnInWR7Nf68g2xCrqm7JLz8OLOISatA8le
WD+9AKX16d4FVVYe1MPhlWBVmi3E9Bm1r5FiNsXMvwJr8lD1mH6smK7Mz/0fsXJ
YxJVKcFHqjz3lDTaY1eDSKMbZ37K9xN+SS8r9cFw7k/fjEnmPSXXs7jmvJJebd+
a3cy7ZfRWFgf1RAw4Qq4hSjkhIz4Y03yGKHxSZ1PBsHG0DPbxF5S4BtHNp0H4nS
/UoZzI3dMfKI2IcFzdiT7YUjHf0fXzG9a7aQxm/fbLlRf42v2mVVP3Tb6KO1G9+
UMYJad2BjY/gMgJDBeNV6JCBH6g/jE0nIh8Nf2A4M7mS4E6R2piB0l2IPVd0huy
ZSrW66X1lKXZx7y94u1y7IXfkWQNxIhLwG6F+dXUP8K4eBjaf8sDvrOD0MQdd
07M9tCmQYIJm597dCa84mFxFmCC69mZIYqzFbmClPTpHYaEFhMi8kaXNRax4LjG
hA9JbQwNvb4vHPEwvUH0bApPX4d0T9h2GE1F6kMNTwcrEU7dq858KuHgBoGeyz
Lf7yE2dJD6uUf6vBGrGCRSDDLEOs5vO3DF9BHIjJ3MCQHwCoFm14Z7dFz7u17
rU0wQDQYJKoZIhvcNAQELBQADggIBAHKbs15VL3B1KJMVXm2R8afAHKNsH0pS8R
p6IhGkLdTj0eVFv1tbKI+K+ON5R5t4K2dV1LJDqOV/HBzT+MH2wBP3MKJMnlw6A
4P2aFk0MJKVOAGZTs3NFVbcvJMj4K2x0tWRfTkHLBaB4d35I+xDUkFNJVCX9R0r
/NbLnKzJOcyH6FNxaJkqO3OeVnE1dGSHTjFfMkFP+XXHxa1YZVt1+bNZsUC/JEf
J9K+cR3e/7jznHeg82GjKYzR6kyBS2dqE/gK7BYGxMlE0nCj0gvqnF5V2Oe+xSn
b4t3sbZjFFai6MFK3X27DHB6UdxO0iGF1mXL8s/hSIXXvJWemlLKC0GHNYFfK7D
f3ZFNhMX7Wfn6g9vPOMK6yTb7jWQX0MPpDQ32dYbOE4KT75v8g8w0puqE0rzqP
PQ4TY5tJejM5B3v7aIlfj5sF7FtVftUPYH1QjAq/rvsrp7HgJ2jVJtMw6RdDP1
ix4kCfGRVJqe6ehpPJONH2t99VO0znPrHiU4CjcfMLNLO9I6P4qiebALM0S+3P
IJ8OSa1anCkazf+rEZ199CJbNKzuUePJY/dPmJ4K+g7Ibmb/uZKYEqdNJiM49H
1Xj/9GBOmVo69aG22Ef5Oo9a+pNFfJyzM2lS3/L8cHTqNK8VAWMISuCR6fDj/0
1tPrqE6dA6FPPKMSQZfChD8r3JqVPFb09J74dG1yW/DNmN+KQ6f8RNYjNg3KJZ
7s4Wv3O0ULlRnH6GNB6pHdRBGbdRX
-----END CERTIFICATE-----
)EOF";

Preferences preferences;
OneWire ibutton(PIN_IBUTTON);
WebServer server(80);
WiFiClientSecure secureClient;
WiFiClient plainClient;
PubSubClient mqttClient(secureClient);

bool modoConfiguracion = false;
bool modoProgramacionLocal = false;
bool staWebActive = false;
unsigned long staWebStartTime = 0;
unsigned long tiempoInicioPress = 0;
unsigned long ultimoIntentoMQTT = 0;
const unsigned long intervaloReintentoMQTT = 5000;
int relayTimeDefault = 1200;
int securityMode = 8;

bool learningMode = false;
unsigned long learningStartedAt = 0;
String scannedKeyHex = "";
unsigned long ultimoHeartbeat = 0;
int targetSlot = 0;
bool slotsBatchPublishing = false;
int slotsBatchCursor = 0;
bool slotsWiping = false;
unsigned int wipeBytesPos = 0;
unsigned long ultimoProcesoLlave = 0;
String targetApto = "";

#define SLOTS_BITMAP_LEN ((MAX_SLOTS + 7) / 8)
byte occupiedBitmap[SLOTS_BITMAP_LEN];
int occupiedCount = -1;
bool ocRefreshThisSession = false;

bool relayActive = false;
unsigned long relayOffTime = 0;

unsigned long lastStatusBlink = 0;
bool ledState = false;

unsigned long pairingExpiry = 0;
const unsigned long PAIRING_WINDOW = 600000;

#define RATE_LIMIT_MAX     10
#define RATE_LIMIT_WINDOW  60000
#define RATE_LIMIT_BLOCK   300000
struct RateLimitEntry {
    unsigned long windowStart;
    int failCount;
    unsigned long blockedUntil;
};
RateLimitEntry rateLimitEntries[RATE_LIMIT_MAX];
int rateLimitIndex = 0;

AccessLog logBuffer[MAX_LOG_ENTRIES];
int logWriteIndex = 0;
bool logsLoaded = false;

void saveLogToEEPROM(int index, AccessLog &log);
AccessLog readLogFromEEPROM(int index);
void loadLogsFromEEPROM();
int logCount = 0;
bool isSafeKey(String k);

String getTopic(const String& subtopic) {
    return "geylca/" + deviceId + "/" + subtopic;
}

String calcularHMAC(String payload, String secret) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload.c_str(), payload.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    String tokenHex = "";
    for (int i = 0; i < 32; i++) {
        char buf[3];
        sprintf(buf, "%02x", hmacResult[i]);
        tokenHex += buf;
    }
    return tokenHex;
}

bool checkRateLimit(String clientId) {
    unsigned long now = millis();
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        if (rateLimitEntries[i].windowStart == 0) continue;
        if (clientId.length() > 0) {
            if (now < rateLimitEntries[i].blockedUntil) return false;
        }
    }
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        if (rateLimitEntries[i].windowStart == 0 ||
            (now - rateLimitEntries[i].windowStart) > RATE_LIMIT_WINDOW) {
            rateLimitEntries[i].windowStart = now;
            rateLimitEntries[i].failCount++;
            if (rateLimitEntries[i].failCount >= RATE_LIMIT_MAX) {
                rateLimitEntries[i].blockedUntil = now + RATE_LIMIT_BLOCK;
                rateLimitEntries[i].failCount = 0;
                return false;
            }
            return true;
        }
    }
    return true;
}

void resetRateLimit() {
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        rateLimitEntries[i].failCount = 0;
        rateLimitEntries[i].blockedUntil = 0;
    }
}

int roleRank(String role) {
    if (role == "master") return 3;
    if (role == "installer") return 2;
    return 1; // admin
}

bool hasRolePermission(String requiredRole, String userRole) {
    return roleRank(userRole) >= roleRank(requiredRole);
}

void writeEEPROM(unsigned int eeaddress, byte *data, int length) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((int)(eeaddress >> 8));
    Wire.write((int)(eeaddress & 0xFF));
    for (int i = 0; i < length; i++) {
        Wire.write(data[i]);
    }
    Wire.endTransmission();
    delay(5);
}

void readEEPROM(unsigned int eeaddress, byte *buffer, int length) {
    int done = 0;
    while (done < length) {
        int n = length - done;
        if (n > 64) n = 64;
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((int)((eeaddress + done) >> 8));
        Wire.write((int)((eeaddress + done) & 0xFF));
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)n);
        for (int i = 0; i < n; i++) {
            if (Wire.available()) buffer[done + i] = Wire.read();
        }
        done += n;
        delay(2);
    }
}

static inline void ocSetBit(int slot, bool occ) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    int idx = (slot - 1) >> 3;
    int bit = (slot - 1) & 7;
    bool cur = (occupiedBitmap[idx] >> bit) & 1;
    if (occ && !cur) {
        occupiedBitmap[idx] |= (byte)(1 << bit);
        if (occupiedCount >= 0) occupiedCount++;
    } else if (!occ && cur) {
        occupiedBitmap[idx] &= (byte)~(1 << bit);
        if (occupiedCount >= 0 && occupiedCount > 0) occupiedCount--;
    }
}

static inline bool ocIsOccupied(int slot) {
    if (slot < 1 || slot > MAX_SLOTS) return false;
    return (occupiedBitmap[(slot - 1) >> 3] >> ((slot - 1) & 7)) & 1;
}

void ocRebuildFromEEPROM() {
    occupiedCount = -1;
    memset(occupiedBitmap, 0, SLOTS_BITMAP_LEN);
    int count = 0;
    int slotsPerBlock = 32;
    byte blockBuffer[256];
    for (int startSlot = 1; startSlot <= MAX_SLOTS; startSlot += slotsPerBlock) {
        int cb = slotsPerBlock;
        if (startSlot + cb - 1 > MAX_SLOTS) cb = MAX_SLOTS - startSlot + 1;
        readEEPROM(startSlot * slotSize, blockBuffer, cb * securityMode);
        for (int i = 0; i < cb; i++) {
            int off = i * securityMode;
            bool occ = false;
            for (int b = 0; b < securityMode; b++) {
                if (blockBuffer[off + b] != 0x00) { occ = true; break; }
            }
            if (occ) {
                ocSetBit(startSlot + i, true);
                count++;
            }
        }
        yield();
    }
    occupiedCount = count;
}

void ocEnsureBuilt() {
    if (occupiedCount < 0) ocRebuildFromEEPROM();
}

void ocClearAll() {
    memset(occupiedBitmap, 0, SLOTS_BITMAP_LEN);
    occupiedCount = 0;
}

void hexToBytes(String hexStr, byte *byteArray, int len) {
    for (int i = 0; i < len; i++) {
        String byteString = hexStr.substring(i * 2, i * 2 + 2);
        byteArray[i] = (byte)strtol(byteString.c_str(), NULL, 16);
    }
}

void saveLogToEEPROM(int index, AccessLog &log) {
    byte data[8];
    memcpy(data, &log.timestamp, 4);
    data[4] = log.action;
    data[5] = (uint8_t)(log.slot >> 8);
    data[6] = (uint8_t)(log.slot & 0xFF);
    data[7] = log.status;
    preferences.begin("geylca_logs", false);
    preferences.putBytes(("L" + String(index)).c_str(), data, 8);
    preferences.end();
}

AccessLog readLogFromEEPROM(int index) {
    AccessLog log;
    byte data[8];
    memset(&log, 0, sizeof(log));
    memset(data, 0, sizeof(data));
    preferences.begin("geylca_logs", true);
    size_t len = preferences.getBytes(("L" + String(index)).c_str(), data, 8);
    preferences.end();
    if (len == 8) {
        memcpy(&log.timestamp, data, 4);
        log.action = data[4];
        log.slot = (data[5] << 8) | data[6];
        log.status = data[7];
    }
    return log;
}

void loadLogsFromEEPROM() {
    if (logsLoaded) return;
    preferences.begin("geylca_logs", true);
    logWriteIndex = preferences.getUChar("idx", 0) % MAX_LOG_ENTRIES;
    preferences.end();

    for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
        AccessLog l = readLogFromEEPROM(i);
        if (l.timestamp != 0) {
            logBuffer[i] = l;
            logCount++;
        }
    }
    logsLoaded = true;
}

void writeLog(uint8_t actionType, uint16_t slotNum, uint8_t statusVal) {
    AccessLog log;
    log.timestamp = (uint32_t)time(nullptr);
    log.action = actionType;
    log.slot = slotNum;
    log.status = statusVal;

    logBuffer[logWriteIndex] = log;
    saveLogToEEPROM(logWriteIndex, log);
    logWriteIndex = (logWriteIndex + 1) % MAX_LOG_ENTRIES;

    preferences.begin("geylca_logs", false);
    preferences.putUChar("idx", logWriteIndex);
    preferences.end();

    if (logCount < MAX_LOG_ENTRIES) logCount++;
}

int contarCasillasLibres() {
    ocEnsureBuilt();
    if (occupiedCount > MAX_SLOTS) occupiedCount = MAX_SLOTS;
    return MAX_SLOTS - occupiedCount;
}

int primerCasillaLibre() {
    ocEnsureBuilt();
    for (int s = 1; s <= MAX_SLOTS; s++) {
        if (!ocIsOccupied(s)) return s;
    }
    return 0;
}

int findFreeSlot() {
    ocEnsureBuilt();
    for (int s = 1; s <= MAX_SLOTS; s++) {
        if (!ocIsOccupied(s)) return s;
    }
    return -1;
}

bool publicarLoteSlots() {
    int batchSize = 64;
    int cb = batchSize;
    if (slotsBatchCursor + cb - 1 > MAX_SLOTS) cb = MAX_SLOTS - slotsBatchCursor + 1;

    ocEnsureBuilt();
    bool bitmapEmpty = true;
    for (int i = 0; i < cb; i++) {
        if (ocIsOccupied(slotsBatchCursor + i)) { bitmapEmpty = false; break; }
    }
    if (bitmapEmpty) {
        Serial.printf("[SLOTS] batch start=%d EMPTY skip\n", slotsBatchCursor);
        slotsBatchCursor += batchSize;
        delay(5);
        if (slotsBatchCursor > MAX_SLOTS) {
            slotsBatchPublishing = false;
            String doneMsg = "{\"status\":\"SLOTS_DONE\",\"max_slots\":" + String(MAX_SLOTS) + "}";
            mqttClient.publish(getTopic("events").c_str(), doneMsg.c_str());
            Serial.println("[SLOTS] DONE");
        }
        return slotsBatchPublishing;
    }

    byte blockBuffer[512];
    readEEPROM(slotsBatchCursor * slotSize, blockBuffer, cb * securityMode);
    Serial.printf("[SLOTS] batch start=%d cb=%d\n", slotsBatchCursor, cb);

    bool batchEmpty = true;
    for (int i = 0; i < cb; i++) {
        int off = i * securityMode;
        for (int b = 0; b < securityMode; b++) {
            if (blockBuffer[off + b] != 0x00) { batchEmpty = false; break; }
        }
        if (!batchEmpty) break;
    }
    if (batchEmpty) {
        Serial.printf("[SLOTS] batch start=%d EMPTY skip\n", slotsBatchCursor);
        slotsBatchCursor += batchSize;
        delay(5);
        if (slotsBatchCursor > MAX_SLOTS) {
            slotsBatchPublishing = false;
            String doneMsg = "{\"status\":\"SLOTS_DONE\",\"max_slots\":" + String(MAX_SLOTS) + "}";
            mqttClient.publish(getTopic("events").c_str(), doneMsg.c_str());
            Serial.println("[SLOTS] DONE");
        }
        return slotsBatchPublishing;
    }

    String out = "{\"status\":\"SLOTS_BATCH\",\"start\":" + String(slotsBatchCursor) + ",\"slots\":[";
    bool first = true;
    preferences.begin("geylca_apt", true);
    for (int i = 0; i < cb; i++) {
        int slot = slotsBatchCursor + i;
        int off = i * securityMode;
        bool occ = false;
        for (int b = 0; b < securityMode; b++) {
            if (blockBuffer[off + b] != 0x00) { occ = true; break; }
        }
        if (!occ) continue;
        String keyHex = "";
        for (int b = 0; b < securityMode; b++) {
            if (blockBuffer[off + b] < 16) keyHex += "0";
            keyHex += String(blockBuffer[off + b], HEX);
        }
        keyHex.toUpperCase();
        String apto = preferences.getString(("s_" + String(slot)).c_str(), "");
        bool sus = preferences.getBool(("sus_" + String(slot)).c_str(), false);
        if (!first) out += ",";
        first = false;
        out += "{\"slot\":" + String(slot) + ",\"key\":\"" + keyHex + "\",\"apto\":\"" + apto + "\",\"sus\":" + (sus ? "true" : "false") + "}";
    }
    preferences.end();
    out += "]}";
    bool pubOk = mqttClient.publish(getTopic("events").c_str(), out.c_str());
    Serial.printf("[SLOTS] batch start=%d len=%d pub=%d\n", slotsBatchCursor, out.length(), pubOk ? 1 : 0);
    mqttClient.loop();
    slotsBatchCursor += batchSize;
    delay(5);
    if (slotsBatchCursor > MAX_SLOTS) {
        slotsBatchPublishing = false;
        String doneMsg = "{\"status\":\"SLOTS_DONE\",\"max_slots\":" + String(MAX_SLOTS) + "}";
        mqttClient.publish(getTopic("events").c_str(), doneMsg.c_str());
        Serial.println("[SLOTS] DONE");
    }
    return slotsBatchPublishing;
}

bool continuarWipe() {
    byte zeros[32] = {0x00};
    unsigned int totalBytes = (unsigned int)MAX_SLOTS * 8;
    int done = 0;
    while (done < 200 && wipeBytesPos < totalBytes) {
        writeEEPROM(wipeBytesPos, zeros, 32);
        wipeBytesPos += 32;
        done++;
    }
    if (wipeBytesPos >= totalBytes) {
        slotsWiping = false;
        preferences.begin("geylca_apt", false);
        preferences.clear();
        preferences.end();
        ocClearAll();
        mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"OK\",\"message\":\"All slots wiped\"}");
    }
    return slotsWiping;
}

const char* actionToString(uint8_t action) {
    switch (action) {
        case 1: return "open";
        case 2: return "set_key";
        case 3: return "clear_slot";
        case 4: return "suspend";
        case 5: return "learn";
        case 6: return "factory_reset";
        case 7: return "denied";
        default: return "unknown";
    }
}

const char* statusToString(uint8_t status) {
    switch (status) {
        case 0: return "DENIED";
        case 1: return "GRANTED";
        case 2: return "SUSPENDED";
        default: return "UNKNOWN";
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String mensaje = "";
    for (unsigned int i = 0; i < length; i++) {
        mensaje += (char)payload[i];
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, mensaje);
    if (error) return;

    String action = doc["action"] | "";
    String role = doc["role"] | "user";
    String clientToken = doc["token"] | "";
    unsigned long timestamp = doc["timestamp"] | 0;

    if (action != "ping") {
        if (clientToken.length() == 0) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"No token provided\"}");
            return;
        }

        if (action == "verify_role") {
            time_t nowV = time(nullptr);
            if (nowV <= 1000000000) {
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Clock not synchronized\"}");
                return;
            }
            long diffV = abs((long)nowV - (long)timestamp);
            if (diffV > 120) {
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Token expired\"}");
                return;
            }
            String rawV = action + String(timestamp);
            String foundRole = "";
            if (adminKey.length() > 0 && clientToken == calcularHMAC(rawV, adminKey)) foundRole = "admin";
            else if (installerKey.length() > 0 && clientToken == calcularHMAC(rawV, installerKey)) foundRole = "installer";
            else if (clientToken == calcularHMAC(rawV, deviceSecret)) foundRole = "master";
            if (foundRole.length() == 0) {
                if (!checkRateLimit("verify")) {
                    mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Rate limit exceeded\"}");
                    return;
                }
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Invalid security token\"}");
                return;
            }
            String verifyResp = "{\"status\":\"OK\",\"message\":\"Role verified\",\"role\":\"" + foundRole + "\"}";
            mqttClient.publish(getTopic("status_resp").c_str(), verifyResp.c_str());
            return;
        }

        String roleKey;
        if (role == "master") roleKey = deviceSecret;
        else if (role == "admin") roleKey = adminKey;
        else if (role == "installer") roleKey = installerKey;
        else {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Unknown role\"}");
            return;
        }

        if (roleKey.length() == 0) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Role key not configured\"}");
            return;
        }

        time_t now = time(nullptr);
        if (now <= 1000000000) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Clock not synchronized\"}");
            return;
        }
        long timeDiff = abs((long)now - (long)timestamp);
        if (timeDiff > 120) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Token expired\"}");
            return;
        }

        String rawData = action + String(timestamp);
        String expectedToken = calcularHMAC(rawData, roleKey);

        if (clientToken != expectedToken) {
            if (!checkRateLimit(role)) {
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Rate limit exceeded\"}");
                return;
            }
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Invalid security token\"}");
            return;
        }
        resetRateLimit();
    }

    if (action == "open") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        digitalWrite(PIN_RELAY, HIGH);
        digitalWrite(PIN_LED, HIGH);
        relayActive = true;
        relayOffTime = millis() + relayTimeDefault;
        writeLog(1, 0, 1);
        mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"GRANTED\",\"slot\":0,\"apto\":\"Apertura Remota App\"}");
        return;
    }

    if (action == "get_free_slots") {
        int libres = contarCasillasLibres();
        int usadas = MAX_SLOTS - libres;
        time_t now = time(nullptr);
        String respJSON = "{\"status\":\"OK\",\"max_slots\":" + String(MAX_SLOTS) +
                          ",\"free_slots\":" + String(libres) +
                          ",\"used_slots\":" + String(usadas) +
                          ",\"security_mode\":" + String(securityMode) +
                          ",\"timestamp\":" + String((uint32_t)now) +
                          ",\"relay_time\":" + String(relayTimeDefault / 1000) + "}";
        mqttClient.publish(getTopic("events").c_str(), respJSON.c_str());
        return;
    }

    if (action == "get_first_free_slot") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slotLibre = primerCasillaLibre();
        String respFree;
        if (slotLibre > 0) {
            respFree = "{\"status\":\"OK\",\"message\":\"Free slot\",\"slot\":" + String(slotLibre) + "}";
        } else {
            respFree = "{\"status\":\"OK\",\"message\":\"No free slot\",\"slot\":0}";
        }
        mqttClient.publish(getTopic("events").c_str(), respFree.c_str());
        return;
    }

    if (action == "get_device_info") {
        time_t now = time(nullptr);
        String respJSON = "{\"device_id\":\"" + deviceId + "\""
                         ",\"security_mode\":" + String(securityMode) +
                         ",\"relay_time\":" + String(relayTimeDefault / 1000) +
                         ",\"max_slots\":" + String(MAX_SLOTS) +
                         ",\"timestamp\":" + String((uint32_t)now) +
                         ",\"firmware\":\"" + String(FIRMWARE_VERSION) + "\"}";
        mqttClient.publish(getTopic("events").c_str(), respJSON.c_str());
        return;
    }

    if (action == "reset_wifi") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"OK\",\"message\":\"Resetting WiFi configuration\"}");
        delay(500);
        preferences.begin("geylca_wifi", false);
        preferences.clear();
        preferences.end();
        ESP.restart();
        return;
    }

    if (action == "set_key") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slot = doc["slot"];
        String keyHex = doc["key"];
        String apto = doc["apto"];
        int bytesToStore = securityMode;
        if (slot > 0 && slot <= MAX_SLOTS && keyHex.length() >= (bytesToStore * 2)) {
            byte keyBytes[8] = {0};
            hexToBytes(keyHex, keyBytes, bytesToStore);
            writeEEPROM(slot * slotSize, keyBytes, bytesToStore);
            ocSetBit(slot, true);
            preferences.begin("geylca_apt", false);
            preferences.putString(("s_" + String(slot)).c_str(), apto);
            if (preferences.isKey(("sus_" + String(slot)).c_str())) preferences.remove(("sus_" + String(slot)).c_str());
            preferences.end();
            writeLog(2, slot, 1);
            String storedHex = keyHex.substring(0, bytesToStore * 2);
            String keyUpd = "{\"status\":\"OK\",\"message\":\"Key updated\",\"slot\":" + String(slot) +
                            ",\"key\":\"" + storedHex +
                            "\",\"apto\":\"" + apto + "\"}";
            mqttClient.publish(getTopic("status_resp").c_str(), keyUpd.c_str());
        }
    }
    else if (action == "set_security_mode" && role == "master") {
        int mode = doc["mode"].as<int>();
        if (mode == 4 || mode == 8) {
            byte emptyBytes[32] = {0x00};
            unsigned int totalBytes = (unsigned int)MAX_SLOTS * 8;
            int cnt = 0;
            for (unsigned int addr = 0; addr < totalBytes; addr += 32) {
                writeEEPROM(addr, emptyBytes, 32);
                cnt++;
                if (cnt % 100 == 0) yield();
            }

            preferences.begin("geylca_apt", false);
            for (int slot = 1; slot <= MAX_SLOTS; slot++) {
                preferences.remove(("s_" + String(slot)).c_str());
                if (preferences.isKey(("sus_" + String(slot)).c_str())) preferences.remove(("sus_" + String(slot)).c_str());
            }
            securityMode = mode;
            slotSize = mode;
            preferences.putInt("secMode", securityMode);
            preferences.end();
            ocClearAll();

            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"OK\",\"message\":\"Security mode updated and EEPROM formatted\"}");
        }
    }
    else if (action == "learn_key") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slotLocal = doc["slot"] | 0;
        targetApto = doc["apto"] | "Desconocido";
        if (slotLocal > 0 && slotLocal <= MAX_SLOTS) {
            targetSlot = slotLocal;
        } else {
            targetSlot = findFreeSlot();
            if (targetSlot == -1) {
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"No free slots\"}");
                return;
            }
        }
        learningMode = true;
        learningStartedAt = millis();
        JsonDocument resp;
        resp["status"] = "WAITING_KEY";
        resp["slot"] = targetSlot;
        String output;
        serializeJson(resp, output);
        mqttClient.publish(getTopic("events").c_str(), output.c_str());
    }
    else if (action == "confirm_key") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        if (!learningMode || scannedKeyHex.length() == 0) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"No pending key scan\"}");
            return;
        }
        String confirmKey = doc["key"] | "";
        if (confirmKey.length() > 0 && confirmKey != scannedKeyHex) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Key mismatch\"}");
            return;
        }
        String savedKey = scannedKeyHex;
        int slotValido = doc["slot"] | 0;
        if (slotValido > 0 && slotValido <= MAX_SLOTS) targetSlot = slotValido;
        if (doc["apto"].is<String>()) {
            targetApto = doc["apto"].as<String>();
        }

        byte existing[8] = {0};
        readEEPROM(targetSlot * slotSize, existing, securityMode);
        bool occupied = false;
        for (int b = 0; b < securityMode; b++) {
            if (existing[b] != 0x00) { occupied = true; break; }
        }
        byte sk[8] = {0};
        hexToBytes(savedKey, sk, securityMode);
        bool mismo = true;
        for (int b = 0; b < securityMode; b++) {
            if (existing[b] != sk[b]) { mismo = false; break; }
        }
        if (occupied && !mismo) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Slot already in use\"}");
            return;
        }

        byte keyBytes[8] = {0};
        hexToBytes(savedKey, keyBytes, securityMode);
        writeEEPROM(targetSlot * slotSize, keyBytes, securityMode);
        ocSetBit(targetSlot, true);
        preferences.begin("geylca_apt", false);
        preferences.putString(("s_" + String(targetSlot)).c_str(), targetApto);
        if (preferences.isKey(("sus_" + String(targetSlot)).c_str())) preferences.remove(("sus_" + String(targetSlot)).c_str());
        preferences.end();
        writeLog(2, targetSlot, 1);
        learningMode = false;
        scannedKeyHex = "";
        JsonDocument resp;
        resp["message"] = "Key learned";
        resp["slot"] = targetSlot;
        resp["apto"] = targetApto;
        resp["key"] = savedKey;
        String output;
        serializeJson(resp, output);
        mqttClient.publish(getTopic("events").c_str(), output.c_str());
        Serial.println("[SYSTEM] Llave grabada en slot " + String(targetSlot) + ": " + savedKey);
    }
    else if (action == "cancel_learn") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        if (learningMode) {
            learningMode = false;
            scannedKeyHex = "";
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"CANCELLED\",\"message\":\"Learning cancelled\"}");
        }
    }
    else if (action == "clear_slot") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slot = doc["slot"];
        if (slot > 0 && slot <= MAX_SLOTS) {
            byte emptyBytes[8] = {0x00};
            writeEEPROM(slot * slotSize, emptyBytes, securityMode);
            ocSetBit(slot, false);
            preferences.begin("geylca_apt", false);
            preferences.remove(("s_" + String(slot)).c_str());
            if (preferences.isKey(("sus_" + String(slot)).c_str())) preferences.remove(("sus_" + String(slot)).c_str());
            preferences.end();
            writeLog(3, slot, 0);
            mqttClient.publish(getTopic("status_resp").c_str(), "{\"status\":\"OK\",\"message\":\"Slot cleared\"}");
        }
    }
    else if (action == "set_relay") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int seconds = doc["relay_time"].as<int>();
        if (seconds <= 0) seconds = 2;
        relayTimeDefault = seconds * 1000;
        preferences.begin("geylca_apt", false);
        preferences.putInt("relayTime", relayTimeDefault);
        preferences.end();
        String relayMsg = "{\"message\":\"Relay time updated\",\"relay_time\":" + String(seconds) + "}";
        mqttClient.publish(getTopic("events").c_str(), relayMsg.c_str());
    }
    else if (action == "suspend_slot") {
        if (!hasRolePermission("admin", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slot = doc["slot"];
        if (slot > 0 && slot <= MAX_SLOTS) {
            preferences.begin("geylca_apt", false);
            String susKey = "sus_" + String(slot);
            bool currentStatus = preferences.getBool(susKey.c_str(), false);
            preferences.putBool(susKey.c_str(), !currentStatus);
            preferences.end();
            writeLog(4, slot, currentStatus ? 1 : 2);
            String suspendMsg = "{\"message\":\"Slot suspension toggled\",\"slot\":" + String(slot) + ",\"suspended\":" + String(!currentStatus ? "true" : "false") + "}";
            mqttClient.publish(getTopic("events").c_str(), suspendMsg.c_str());
        }
    }
    else if (action == "factory_reset" && role == "master") {
        byte emptyBytes[32] = {0x00};
        unsigned int totalBytes = (unsigned int)MAX_SLOTS * 8;
        int cnt = 0;
        for (unsigned int addr = 0; addr < totalBytes; addr += 32) {
            writeEEPROM(addr, emptyBytes, 32);
            cnt++;
            if (cnt % 100 == 0) yield();
        }
        preferences.begin("geylca_apt", false);
        preferences.clear();
        preferences.end();
        preferences.begin("geylca_logs", true);
        preferences.clear();
        preferences.end();
        writeLog(6, 0, 1);
        mqttClient.publish(getTopic("events").c_str(), "{\"message\":\"Factory reset executed\"}");
        ocClearAll();
    }
    else if (action == "get_logs") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        loadLogsFromEEPROM();
        int count = doc["count"] | 20;
        if (count > MAX_LOG_ENTRIES) count = MAX_LOG_ENTRIES;
        if (count > logCount) count = logCount;

        JsonDocument logDoc;
        JsonArray logArray = logDoc.to<JsonArray>();

        int startIdx = (logWriteIndex - count + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
        for (int i = 0; i < count; i++) {
            int idx = (startIdx + i) % MAX_LOG_ENTRIES;
            if (logBuffer[idx].timestamp == 0) continue;
            JsonObject entry = logArray.add<JsonObject>();
            entry["ts"] = logBuffer[idx].timestamp;
            entry["action"] = actionToString(logBuffer[idx].action);
            entry["slot"] = logBuffer[idx].slot;
            entry["status"] = statusToString(logBuffer[idx].status);
        }

        String output;
        serializeJson(logDoc, output);
        mqttClient.publish(getTopic("logs").c_str(), output.c_str());
    }
    else if (action == "get_slot_info") {
        if (!hasRolePermission("admin", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slot = doc["slot"] | 0;
        if (slot > 0 && slot <= MAX_SLOTS) {
            byte keyBytes[8] = {0};
            readEEPROM(slot * slotSize, keyBytes, securityMode);

            bool isEmpty = true;
            for (int b = 0; b < securityMode; b++) {
                if (keyBytes[b] != 0x00) { isEmpty = false; break; }
            }

            String keyHex = "";
            for (int i = 0; i < securityMode; i++) {
                if (keyBytes[i] < 16) keyHex += "0";
                keyHex += String(keyBytes[i], HEX);
            }
            keyHex.toUpperCase();

            preferences.begin("geylca_apt", true);
            String apto = preferences.getString(("s_" + String(slot)).c_str(), "");
            bool isSuspended = preferences.getBool(("sus_" + String(slot)).c_str(), false);
            preferences.end();

            String statusStr = isEmpty ? "empty" : (isSuspended ? "suspended" : "active");

            String resp = "{\"action\":\"slot_info\",\"slot\":" + String(slot) +
                         ",\"key\":\"" + keyHex +
                         "\",\"apto\":\"" + apto +
                         "\",\"status\":\"" + statusStr + "\"}";
            mqttClient.publish(getTopic("events").c_str(), resp.c_str());
        }
    }
    else if (action == "set_apto") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        int slot = doc["slot"] | 0;
        String apto = doc["apto"] | "";
        if (slot < 1 || slot > MAX_SLOTS) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Invalid slot\"}");
            return;
        }
        byte keyBytes[8] = {0};
        readEEPROM(slot * slotSize, keyBytes, securityMode);
        bool isEmpty = true;
        for (int b = 0; b < securityMode; b++) {
            if (keyBytes[b] != 0x00) { isEmpty = false; break; }
        }
        if (isEmpty) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Slot is empty\"}");
            return;
        }
        if (apto.length() > 32) apto = apto.substring(0, 32);
        preferences.begin("geylca_apt", false);
        preferences.putString(("s_" + String(slot)).c_str(), apto);
        preferences.end();
        mqttClient.publish(getTopic("events").c_str(),
            ("{\"status\":\"OK\",\"message\":\"Apto updated\",\"slot\":" + String(slot) + ",\"apto\":\"" + apto + "\"}").c_str());
    }
    else if (action == "get_slots") {
        if (!hasRolePermission("admin", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        mqttClient.publish(getTopic("events").c_str(),
            ("{\"status\":\"GET_SLOTS_START\",\"max_slots\":" + String(MAX_SLOTS) + ",\"batch_size\":64}").c_str());
        if (!slotsBatchPublishing) {
            slotsBatchPublishing = true;
            slotsBatchCursor = 1;
        }
    }
    else if (action == "wipe_slots") {
        if (role != "master") {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Only master can wipe all slots\"}");
            return;
        }
        mqttClient.publish(getTopic("events").c_str(),
            ("{\"status\":\"WIPE_START\",\"max_slots\":" + String(MAX_SLOTS) + "}").c_str());
        if (!slotsWiping) {
            slotsWiping = true;
            wipeBytesPos = 0;
        }
    }
    else if (action == "ota_update") {
        if (role != "master") {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Only master can update firmware\"}");
            return;
        }
        String firmwareUrl = doc["url"] | "";
        if (firmwareUrl.length() == 0) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"No firmware URL provided\"}");
            return;
        }
        mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"OK\",\"message\":\"Starting OTA update...\"}");
        delay(100);

        String md5 = doc["md5"] | "";
        bool https = firmwareUrl.startsWith("https://");
        WiFiClientSecure tlsSec;
        WiFiClient tcpPlain;
        HTTPClient http;
        http.setTimeout(20000);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        if (https) {
            tlsSec.setInsecure();
            http.begin(tlsSec, firmwareUrl);
        } else {
            http.begin(tcpPlain, firmwareUrl);
        }
        int httpCode = http.GET();
        if (httpCode == 200) {
            int contentLength = http.getSize();
            WiFiClient * stream = http.getStreamPtr();
            if (contentLength > 0 && md5.length() == 32) {
                Update.setMD5(md5.c_str());
            }
            if (Update.begin(contentLength)) {
                size_t written = 0;
                uint8_t buf[1024];
                while (stream->available() && written < contentLength) {
                    int bytesRead = stream->readBytes(buf, sizeof(buf));
                    Update.write(buf, bytesRead);
                    written += bytesRead;
                    yield();
                }
                if (Update.end(true)) {
                    mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"OK\",\"message\":\"OTA update successful. Rebooting...\"}");
                    delay(1000);
                    ESP.restart();
                } else {
                    mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"OTA update failed (MD5 mismatch o firmware invalido)\"}");
                }
            } else {
                mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Not enough space for OTA\"}");
            }
        } else {
            String errMsg = "{\"status\":\"DENIED\",\"message\":\"HTTP error: " + String(httpCode) + "\"}";
            mqttClient.publish(getTopic("events").c_str(), errMsg.c_str());
        }
        http.end();
    }
    else if (action == "set_role_key") {
        String targetRole = doc["target_role"] | "";
        if (targetRole != "admin" && targetRole != "installer") {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Invalid target role\"}");
            return;
        }
        if (role != "master" && role != "installer") {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        String newKey = doc["key"] | "";
        if (isPin6(newKey)) {
            preferences.begin("geylca_sec", false);
            if (targetRole == "admin") {
                adminKey = newKey;
                preferences.putString("key_admin", adminKey);
            } else {
                installerKey = newKey;
                preferences.putString("key_installer", installerKey);
            }
            preferences.end();
            String keyResp = "{\"status\":\"OK\",\"message\":\"" + targetRole + " key updated\"}";
            mqttClient.publish(getTopic("status_resp").c_str(), keyResp.c_str());
        } else {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Invalid PIN (must be 6 digits)\"}");
        }
        return;
    }
    else if (action == "reset_admin_key") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        adminKey = String(DEFAULT_ADMIN_PIN);
        preferences.begin("geylca_sec", false);
        preferences.putString("key_admin", adminKey);
        preferences.end();
        String resetResp = "{\"status\":\"OK\",\"message\":\"Admin PIN reset to " + String(DEFAULT_ADMIN_PIN) + "\",\"key\":\"" + String(DEFAULT_ADMIN_PIN) + "\"}";
        mqttClient.publish(getTopic("status_resp").c_str(), resetResp.c_str());
        return;
    }
    else if (action == "reset_installer_key") {
        if (!hasRolePermission("installer", role)) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        installerKey = String(DEFAULT_INSTALLER_PIN);
        preferences.begin("geylca_sec", false);
        preferences.putString("key_installer", installerKey);
        preferences.end();
        String resetResp = "{\"status\":\"OK\",\"message\":\"Installer PIN reset to " + String(DEFAULT_INSTALLER_PIN) + "\",\"key\":\"" + String(DEFAULT_INSTALLER_PIN) + "\"}";
        mqttClient.publish(getTopic("status_resp").c_str(), resetResp.c_str());
        return;
    }
    else if (action == "get_role_keys") {
        if (role != "master" && role != "installer") {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"DENIED\",\"message\":\"Insufficient permissions\"}");
            return;
        }
        String keysResp = "{";
        if (role == "master") {
            keysResp += "\"master\":\"" + deviceSecret + "\",";
        }
        keysResp += "\"admin\":\"" + adminKey + "\",";
        keysResp += "\"installer\":\"" + installerKey + "\"}";
        mqttClient.publish(getTopic("status_resp").c_str(), keysResp.c_str());
    }
}

bool isPin6(String k) {
    if (k.length() != 6) return false;
    for (unsigned int i = 0; i < k.length(); i++) {
        if (k.charAt(i) < '0' || k.charAt(i) > '9') return false;
    }
    return true;
}

bool isSafeKey(String k) {
    if (k.length() == 0) return false;
    for (unsigned int i = 0; i < k.length(); i++) {
        char c = k.charAt(i);
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

void verificarConexionMQTT() {
    if (mqttClient.connected()) return;
    unsigned long ahora = millis();
    if (ahora - ultimoIntentoMQTT < intervaloReintentoMQTT) return;
    ultimoIntentoMQTT = ahora;

    int port = 0;
    Client* transporte = nullptr;

    if (secureClient.connect(mqtt_server, 8883)) {
        transporte = &secureClient;
        port = 8883;
        Serial.println("[MQTT] Conexion TLS exitosa (8883).");
    } else if (plainClient.connect(mqtt_server, 1883)) {
        transporte = &plainClient;
        port = 1883;
        Serial.println("[MQTT] TLS no disponible; usando MQTT plain (1883).");
    } else {
        Serial.println("[MQTT] Fallo al conectar (TLS y plain). Reintentando en 5s...");
        return;
    }

    mqttClient.setClient(*transporte);
    mqttClient.setServer(mqtt_server, port);
    String clientId = "GEYLCA_ESP32_" + deviceId;
    if (mqttClient.connect(clientId.c_str(), getTopic("status").c_str(), 0, true, "{\"status\":\"OFFLINE\"}")) {
        mqttClient.subscribe(getTopic("cmd").c_str());
        mqttClient.publish(getTopic("status").c_str(), "{\"status\":\"ONLINE\"}", true);
        Serial.println("[MQTT] Conectado y en linea.");
    } else {
        transporte->stop();
        Serial.print("[MQTT] Handshake MQTT fallo ("); Serial.print(port); Serial.println(").");
    }
}

void updateStatusLED() {
    if (relayActive) return;

    unsigned long now = millis();
    bool mqttConnected = mqttClient.connected();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);

    if (!wifiConnected) {
        if (now - lastStatusBlink >= 200) {
            lastStatusBlink = now;
            ledState = !ledState;
            digitalWrite(PIN_LED, ledState ? HIGH : LOW);
        }
    } else if (!mqttConnected) {
        if (now - lastStatusBlink >= 500) {
            lastStatusBlink = now;
            ledState = !ledState;
            digitalWrite(PIN_LED, ledState ? HIGH : LOW);
        }
    } else {
        if (now - lastStatusBlink >= 2000) {
            lastStatusBlink = now;
            ledState = !ledState;
            digitalWrite(PIN_LED, ledState ? HIGH : LOW);
        }
    }
}

void handleRoot() {
    int n = WiFi.scanNetworks();
    String options = "";
    for (int i = 0; i < n; ++i) {
        options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)</option>";
    }

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>GEYLCA Configuración AP</title><style>";
    html += "body{font-family:Arial,sans-serif;background:#0d1b2a;color:#fff;margin:0;padding:20px;display:flex;justify-content:center;align-items:center;min-height:100vh;}";
    html += ".card{background:#1b263b;padding:30px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.5);width:100%;max-width:400px;border-top:4px solid #00b4d8;}";
    html += "h2{color:#00b4d8;text-align:center;margin-bottom:5px;}";
    html += ".subtitle{text-align:center;color:#90e0ef;font-size:13px;margin-bottom:20px;}";
    html += "label{font-weight:bold;font-size:14px;display:block;margin-top:15px;color:#caf0f8;}";
    html += "input,select{width:100%;padding:10px;margin-top:5px;border:1px solid #415a77;background:#0d1b2a;color:#fff;border-radius:6px;box-sizing:border-box;}";
    html += "button{width:100%;padding:12px;background:#00b4d8;color:#0d1b2a;border:none;border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:25px;}";
    html += "button:hover{background:#0096c7;}";
    html += "</style></head><body><div class='card'>";
    html += "<h2>GEYLCA ACCESS CONTROL</h2><div class='subtitle'>Configuración de Módulo Wi-Fi v2.0</div>";
    html += "<form action='/connect' method='POST'>";
    html += "<label>Nombre / ID del Módulo:</label><input type='text' name='devid' value='" + deviceId + "' required>";
    html += "<label>Seleccione Red Wi-Fi:</label><select name='ssid'>" + options + "</select>";
    html += "<label>Contraseña Wi-Fi:</label><input type='password' name='password' placeholder='••••••••'>";
    html += "<button type='submit'>Guardar y Conectar</button>";
    html += "</form></div></body></html>";

    server.send(200, "text/html", html);
}

void handleConnect() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String newDevId = server.arg("devid");

    if (newDevId.length() > 0) {
        deviceId = newDevId;
        preferences.begin("geylca_apt", false);
        preferences.putString("devId", deviceId);
        preferences.end();
    }

    preferences.begin("geylca_wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Guardado</title><style>body{font-family:Arial;text-align:center;padding:50px;background:#0d1b2a;color:#fff;}</style></head><body>";
    html += "<div style='background:#1b263b;padding:30px;border-radius:10px;display:inline-block;box-shadow:0 4px 15px rgba(0,0,0,0.5);'>";
    html += "<h2 style='color:#00b4d8;'>¡Configuración Exitosa!</h2><p>El módulo <b>" + deviceId + "</b> se reiniciará y conectará a la red.</p></div></body></html>";

    server.send(200, "text/html", html);
    delay(3000);
    ESP.restart();
}

void handlePairingPage() {
    if (millis() > pairingExpiry && pairingExpiry != 0) {
        server.send(403, "application/json", "{\"error\":\"Pairing window expired. Restart device to re-enable.\"}");
        return;
    }

    String rawData = "{\"device_id\":\"" + deviceId + "\",\"secret\":\"" + deviceSecret + "\"}";

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>GEYLCA Emparejamiento</title><script src='https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js'></script>";
    html += "<style>body{font-family:Arial,sans-serif;background:#0d1b2a;color:#fff;text-align:center;padding:20px;margin:0;}";
    html += ".card{background:#1b263b;padding:20px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.5);display:inline-block;max-width:400px;width:100%;border-top:4px solid #00b4d8;}";
    html += "h2{color:#00b4d8;margin-top:0;}#qrcode{margin:15px auto;display:flex;justify-content:center;}";
    html += "textarea{width:100%;background:#0d1b2a;color:#00b4d8;border:1px solid #415a77;padding:10px;border-radius:6px;font-family:monospace;font-size:12px;box-sizing:border-box;resize:none;margin-top:8px;}";
    html += "p{font-size:13px;color:#caf0f8;margin:8px 0;}</style></head><body>";
    html += "<div class='card'><h2>Emparejamiento GEYLCA</h2>";
    html += "<p>Módulo: <b>" + deviceId + "</b></p>";
    html += "<div id='qrcode'></div>";
    html += "<p style='font-size:11px;color:#ffd166;'>Si el código QR no carga, copie la siguiente Data Cruda:</p>";
    html += "<textarea rows='3' readonly onclick='this.select()'>" + rawData + "</textarea>";
    html += "<p style='font-size:11px;color:#ff6b6b;margin-top:12px;'>Ventana de emparejamiento activa por 10 minutos.</p></div>";
    html += "<script>var qrcode = new QRCode(document.getElementById('qrcode'), {text: '" + rawData + "', width: 180, height: 180});</script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handlePairingInfo() {
    if (millis() > pairingExpiry && pairingExpiry != 0) {
        server.send(403, "application/json", "{\"error\":\"Pairing window expired\"}");
        return;
    }
    String jsonResp = "{\"device_id\":\"" + deviceId + "\",\"secret\":\"" + deviceSecret + "\"}";
    server.send(200, "application/json", jsonResp);
}

void iniciarModoAP() {
    modoConfiguracion = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GEYLCA-Config", "12345678");
    server.on("/", handleRoot);
    server.on("/connect", HTTP_POST, handleConnect);
    server.begin();
    DBG("[SYSTEM] Modo AP iniciado: GEYLCA-Config (IP: 192.168.4.1)");
}

String generarDeviceIdUnico() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String("mod_") + String(buf).substring(4);
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(500);

    Serial.print("\n--- GEYLCA Control de Acceso v");
    Serial.print(FIRMWARE_VERSION);
    Serial.println(" (4000 Casillas) ---");

    preferences.begin("geylca_apt", true);
    deviceId = preferences.getString("devId", "");
    if (deviceId.length() == 0) {
        deviceId = generarDeviceIdUnico();
        preferences.begin("geylca_apt", false);
        preferences.putString("devId", deviceId);
        preferences.end();
        Serial.print("[SYS] DeviceId generado por MAC: ");
        Serial.println(deviceId);
    }
    relayTimeDefault = preferences.getInt("relayTime", 1200);
    securityMode = preferences.getInt("secMode", 8);
    slotSize = securityMode;
    preferences.end();

    preferences.begin("geylca_sec", false);
    deviceSecret = preferences.getString("dev_secret", "");
    if (deviceSecret == "") {
        uint64_t chipid = ESP.getEfuseMac();
        char buf[32];
        sprintf(buf, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
        deviceSecret = String(buf) + "_" + String(millis());
        preferences.putString("dev_secret", deviceSecret);
    }
    adminKey = preferences.getString("key_admin", "");
    installerKey = preferences.getString("key_installer", "");
    if (!isPin6(adminKey)) {
        adminKey = String(DEFAULT_ADMIN_PIN);
        Serial.println("[SYS] PIN admin normalizado a 123456 (formato previo incompatible).");
    }
    if (!isPin6(installerKey)) {
        installerKey = String(DEFAULT_INSTALLER_PIN);
        Serial.println("[SYS] PIN instalador normalizado a 654321 (formato previo incompatible).");
    }
    preferences.putString("key_admin", adminKey);
    preferences.putString("key_installer", installerKey);
    preferences.end();

    pinMode(PIN_RELAY, OUTPUT);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BTN_PROG, INPUT_PULLUP);
    digitalWrite(PIN_RELAY, LOW);
    digitalWrite(PIN_LED, LOW);

    Wire.begin(PIN_SDA, PIN_SCL);

    preferences.begin("geylca_wifi", true);
    String ssidSaved = preferences.getString("ssid", "");
    String passSaved = preferences.getString("pass", "");
    preferences.end();

    if (ssidSaved == "") {
        iniciarModoAP();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssidSaved.c_str(), passSaved.c_str());
        Serial.print("[WiFi] Conectando a: ");
        Serial.println(ssidSaved);

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 12000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFi] ¡Conectado con éxito!");
            Serial.print("[IP] Dirección local: ");
            Serial.println(WiFi.localIP());

            configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
            Serial.println("[NTP] Sincronizando hora...");

            secureClient.setInsecure();
            secureClient.setTimeout(5);
            plainClient.setTimeout(5);
            mqttClient.setServer(mqtt_server, mqtt_port);
            mqttClient.setCallback(mqttCallback);
            mqttClient.setKeepAlive(60);
            mqttClient.setBufferSize(8192);

            pairingExpiry = millis() + PAIRING_WINDOW;
            server.on("/", handlePairingPage);
            server.on("/pairing", handlePairingInfo);
            server.begin();
            staWebActive = true;
            staWebStartTime = millis();
            Serial.println("[HTTP] Servidor Web QR activo en IP local por 10 minutos.");
        } else {
            Serial.println("\n[WiFi] Error de conexión. Iniciando Modo AP...");
            iniciarModoAP();
        }
    }
}

void loop() {
    if (modoConfiguracion) {
        server.handleClient();
        return;
    }

    if (staWebActive) {
        server.handleClient();
        if (millis() - staWebStartTime > 600000) {
            server.stop();
            staWebActive = false;
            Serial.println("[HTTP] Servidor Web QR apagado por ahorro de recursos.");
        }
    }

    verificarConexionMQTT();
    mqttClient.loop();
    if (mqttClient.connected() && millis() - ultimoHeartbeat >= 30000) {
        ultimoHeartbeat = millis();
        mqttClient.publish(getTopic("status").c_str(), "{\"status\":\"ONLINE\"}", true);
    }
    if (slotsWiping && mqttClient.connected()) {
        continuarWipe();
    }
    if (slotsBatchPublishing && mqttClient.connected()) {
        publicarLoteSlots();
    }

    if (relayActive && millis() >= relayOffTime) {
        digitalWrite(PIN_RELAY, LOW);
        digitalWrite(PIN_LED, LOW);
        relayActive = false;
    }

    updateStatusLED();

    if (digitalRead(PIN_BTN_PROG) == LOW) {
        if (tiempoInicioPress == 0) {
            tiempoInicioPress = millis();
        } else if (millis() - tiempoInicioPress > 5000) {
            preferences.begin("geylca_wifi", false);
            preferences.clear();
            preferences.end();
            Serial.println("[SYSTEM] Borrando credenciales Wi-Fi. Reiniciando...");
            ESP.restart();
        } else if (millis() - tiempoInicioPress > 2000 && !modoProgramacionLocal) {
            modoProgramacionLocal = true;
            Serial.println("[SYSTEM] Modo programación local activado.");
            for(int i = 0; i < 5; i++) {
                digitalWrite(PIN_LED, HIGH);
                delay(100);
                digitalWrite(PIN_LED, LOW);
                delay(100);
            }
        }
    } else {
        tiempoInicioPress = 0;
    }

    if (learningMode && !scannedKeyHex.length() && millis() - learningStartedAt > 60000) {
        learningMode = false;
        if (mqttClient.connected()) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"TIMEOUT\",\"message\":\"Learning timeout, no key presented\"}");
        }
        Serial.println("[SYSTEM] Modo aprendizaje cancelado por timeout.");
    }

    if (learningMode && scannedKeyHex.length() && millis() - learningStartedAt > 30000) {
        learningMode = false;
        scannedKeyHex = "";
        if (mqttClient.connected()) {
            mqttClient.publish(getTopic("events").c_str(), "{\"status\":\"TIMEOUT\",\"message\":\"Key confirmation timeout\"}");
        }
        Serial.println("[SYSTEM] Confirmación de llave cancelada por timeout.");
    }

    byte addr[8];
    if (ibutton.search(addr)) {
        if (OneWire::crc8(addr, 7) != addr[7]) {
            ibutton.reset_search();
            return;
        }
        if (!learningMode && millis() - ultimoProcesoLlave < 3000) {
            ibutton.reset_search();
            return;
        }
        ultimoProcesoLlave = millis();

        String llaveLeidaHex = "";
        for (uint8_t i = 0; i < 8; i++) {
            if (addr[i] < 16) llaveLeidaHex += "0";
            llaveLeidaHex += String(addr[i], HEX);
        }
        llaveLeidaHex.toUpperCase();

        if (learningMode) {
            if (scannedKeyHex == llaveLeidaHex) {
                ibutton.reset_search();
                return;
            }
            scannedKeyHex = llaveLeidaHex;
            if (mqttClient.connected()) {
                JsonDocument resp;
                resp["status"] = "KEY_SCAN";
                resp["slot"] = targetSlot;
                resp["apto"] = targetApto;
                resp["key"] = scannedKeyHex;
                String output;
                serializeJson(resp, output);
                mqttClient.publish(getTopic("events").c_str(), output.c_str());
                Serial.println("[SYSTEM] Llave escaneada slot " + String(targetSlot) + ": " + scannedKeyHex + ". Esperando confirmacion...");
            }
            learningStartedAt = millis();
            ibutton.reset_search();
            return;
        }

        if (modoProgramacionLocal) {
            int slotLibre = findFreeSlot();
            if (slotLibre != -1) {
                writeEEPROM(slotLibre * slotSize, addr, securityMode);
                ocSetBit(slotLibre, true);
                preferences.begin("geylca_apt", false);
                String aptoDefault = "Local Slot " + String(slotLibre);
                preferences.putString(("s_" + String(slotLibre)).c_str(), aptoDefault);
                if (preferences.isKey(("sus_" + String(slotLibre)).c_str())) preferences.remove(("sus_" + String(slotLibre)).c_str());
                preferences.end();

                writeLog(2, slotLibre, 1);

                if (mqttClient.connected()) {
                    String payloadJSON = "{\"action\":\"set_key\",\"slot\":" + String(slotLibre) + ",\"key\":\"" + llaveLeidaHex + "\",\"apto\":\"" + aptoDefault + "\"}";
                    mqttClient.publish(getTopic("events").c_str(), payloadJSON.c_str());
                }
            }
            modoProgramacionLocal = false;
            ibutton.reset_search();
            return;
        }

        bool accesoPermitido = false;
        int slotEncontrado = -1;

        ocEnsureBuilt();
        byte oneKey[8];
        for (int slot = 1; slot <= MAX_SLOTS && !accesoPermitido; slot++) {
            if (!ocIsOccupied(slot)) continue;
            readEEPROM(slot * slotSize, oneKey, securityMode);
            bool coincide = true;
            for (int b = 0; b < securityMode; b++) {
                if (oneKey[b] != addr[b]) { coincide = false; break; }
            }
            if (coincide) {
                preferences.begin("geylca_apt", true);
                bool isSuspended = preferences.getBool(("sus_" + String(slot)).c_str(), false);
                preferences.end();
                if (!isSuspended) {
                    accesoPermitido = true;
                    slotEncontrado = slot;
                }
            }
        }

        if (!accesoPermitido && occupiedCount > 0 && !ocRefreshThisSession) {
            ocRefreshThisSession = true;
            ocRebuildFromEEPROM();
            for (int slot = 1; slot <= MAX_SLOTS && !accesoPermitido; slot++) {
                if (!ocIsOccupied(slot)) continue;
                readEEPROM(slot * slotSize, oneKey, securityMode);
                bool coincide = true;
                for (int b = 0; b < securityMode; b++) {
                    if (oneKey[b] != addr[b]) { coincide = false; break; }
                }
                if (coincide) {
                    preferences.begin("geylca_apt", true);
                    bool isSuspended = preferences.getBool(("sus_" + String(slot)).c_str(), false);
                    preferences.end();
                    if (!isSuspended) {
                        accesoPermitido = true;
                        slotEncontrado = slot;
                    }
                }
            }
        }

        if (accesoPermitido) {
            preferences.begin("geylca_apt", true);
            String aptAsociado = preferences.getString(("s_" + String(slotEncontrado)).c_str(), "Desconocido");
            preferences.end();

            writeLog(1, slotEncontrado, 1);

            if (mqttClient.connected()) {
                String payloadJSON = "{\"slot\":" + String(slotEncontrado) + ",\"apto\":\"" + aptAsociado + "\",\"status\":\"GRANTED\"}";
                mqttClient.publish(getTopic("events").c_str(), payloadJSON.c_str());
            }

            digitalWrite(PIN_RELAY, HIGH);
            digitalWrite(PIN_LED, HIGH);
            relayActive = true;
            relayOffTime = millis() + relayTimeDefault;
        } else {
            writeLog(7, 0, 0);

            if (mqttClient.connected()) {
                String payloadJSON = "{\"key\":\"" + llaveLeidaHex + "\",\"status\":\"DENIED\"}";
                mqttClient.publish(getTopic("events").c_str(), payloadJSON.c_str());
            }
            for(int i = 0; i < 3; i++){
                digitalWrite(PIN_LED, HIGH);
                delay(150);
                digitalWrite(PIN_LED, LOW);
                delay(150);
            }
        }
        ibutton.reset_search();
    }
    delay(50);
}
