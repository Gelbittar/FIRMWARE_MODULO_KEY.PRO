/*
 * GEYLCA — Programador Inalámbrico V1.0 (ESP-01 / ESP8266)
 * Lee iButtons (Dallas/OneWire) de forma remota y publica el código por MQTT
 * para que la interfaz GEYLCA capture la llave y la programe en un módulo.
 *
 * Protocolo sin secret (lectura rápida):
 *   Interfaz -> geylca/{id}/prog/cmd    {"action":"read_key"}
 *   Módulo   -> geylca/{id}/prog/events {"status":"WAITING_KEY"}
 *            -> geylca/{id}/prog/events {"status":"KEY_SCAN","key":"HEX16"}
 *            -> geylca/{id}/prog/events {"status":"TIMEOUT"}   (60s sin llave)
 *   Presencia -> geylca/{id}/prog/status {"status":"ONLINE",...} (retained, cada 30s)
 */

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ================= VARIABLES DE CONFIGURACIÓN DINÁMICA =================
char mqtt_server[40] = "broker.hivemq.com";
char mqtt_port_str[6] = "1883";
char programmer_id[32] = "prog01";

// ================= CONFIGURACIÓN DE PINES (ESP-01) =================
#define IBUTTON_PIN   2  // GPIO2 para el lector iButton
#define LED_PIN       3  // GPIO3 (RX) para el LED externo (Activo en HIGH)
#define BUILTIN_LED   1  // GPIO1 (TX) para el LED integrado del ESP-01 (Activo en LOW)

#define READ_TIMEOUT_MS 60000

OneWire ds(IBUTTON_PIN);
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastPingTime = 0;

// Declaración de parámetros personalizados para WiFiManager
WiFiManagerParameter custom_mqtt_server("server", "Servidor MQTT", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "Puerto MQTT", mqtt_port_str, 6);
WiFiManagerParameter custom_programmer_id("id", "ID del Programador", programmer_id, 32);

bool lecturaEsperando = false;
unsigned long lecturaInicio = 0;
String lastReadKey = "";

String getTopic(const String& subtopic) {
    return "geylca/" + String(programmer_id) + "/prog/" + subtopic;
}

void publicarState(const String& status, const String& key) {
    DynamicJsonDocument doc(256);
    doc["status"] = status;
    doc["prog_id"] = programmer_id;
    if (key.length()) doc["key"] = key;
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(getTopic("events").c_str(), buffer);
}

void reconnect() {
    while (!client.connected()) {
        String clientId = "GEYLCA_PROG_" + String(programmer_id) + "_" + String(ESP.getChipId());

        String statusTopic = getTopic("status");

        // LWT retenido para que la interfaz detecte caída del programador
        if (client.connect(clientId.c_str(), statusTopic.c_str(), 0, true, "{\"status\":\"OFFLINE\",\"type\":\"programador\"}")) {
            client.subscribe(getTopic("cmd").c_str());

            // Encender ambos LEDs fijos indicando conexión exitosa y activo
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(BUILTIN_LED, LOW); // El LED integrado enciende con LOW

            // Publicar estado ONLINE retenido en formato JSON
            DynamicJsonDocument s(256);
            s["status"] = "ONLINE";
            s["type"] = "programador";
            s["name"] = "Programador Inalambrico V1.0";
            s["id"] = programmer_id;
            char osc[256];
            serializeJson(s, osc);
            client.publish(statusTopic.c_str(), osc, true);
        } else {
            // Apagar ambos LEDs si no hay conexión
            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUILTIN_LED, HIGH); // Apagado con HIGH
            delay(5000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, message);
    if (error) return;

    String action = doc["action"] | "";

    if (action == "read_key") {
        lastReadKey = "";
        lecturaEsperando = true;
        lecturaInicio = millis();
        publicarState("WAITING_KEY", "");

        // Parpadeo indicando espera
        for (int i = 0; i < 2; i++) {
            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUILTIN_LED, HIGH);
            delay(80);
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(BUILTIN_LED, LOW);
            delay(80);
        }
    } else if (action == "restart") {
        DynamicJsonDocument res(256);
        res["message"] = "Restarting programmer device...";
        char buffer[256];
        serializeJson(res, buffer);
        client.publish(getTopic("events").c_str(), buffer);
        delay(500);
        ESP.restart();
    }
}

void checkiButton() {
    byte addr[8];
    if (!ds.search(addr)) {
        ds.reset_search();
        return;
    }

    if (OneWire::crc8(addr, 7) != addr[7]) {
        return;
    }

    String keyStr = "";
    for (int i = 0; i < 8; i++) {
        if (addr[i] < 16) keyStr += "0";
        keyStr += String(addr[i], HEX);
    }
    keyStr.toUpperCase();

    if (keyStr == lastReadKey) {
        ds.reset_search();
        return;
    }
    lastReadKey = keyStr;

    // No volver a publicar hasta una nueva orden de lectura
    lecturaEsperando = false;

    // EFECTO DE PARPADEO EN AMBOS LEDS AL LEER LA LLAVE
    for (int i = 0; i < 4; i++) {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUILTIN_LED, HIGH);
        delay(80);
        digitalWrite(LED_PIN, HIGH);
        digitalWrite(BUILTIN_LED, LOW);
        delay(80);
    }

    publicarState("KEY_SCAN", keyStr);
}

void setup() {
    // Nota: Al usar GPIO1 (TX) como salida digital para el LED integrado,
    // la consola Serial deja de enviar datos al puerto USB.
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUILTIN_LED, OUTPUT);

    // Estado inicial apagado (Externo LOW, Integrado HIGH)
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUILTIN_LED, HIGH);

    // Inicializar LittleFS para guardar parámetros personalizados de WiFiManager
    if (LittleFS.begin()) {
        if (LittleFS.exists("/config.json")) {
            File configFile = LittleFS.open("/config.json", "r");
            if (configFile) {
                size_t size = configFile.size();
                if (size > 0) {
                    std::unique_ptr<char[]> buf(new char[size + 1]);
                    memset(buf.get(), 0, size + 1);
                    configFile.readBytes(buf.get(), size);
                    DynamicJsonDocument json(512);
                    DeserializationError error = deserializeJson(json, buf.get());
                    if (!error) {
                        strcpy(mqtt_server, json["mqtt_server"] | "broker.hivemq.com");
                        strcpy(mqtt_port_str, json["mqtt_port"] | "1883");
                        strcpy(programmer_id, json["programmer_id"] | "prog01");
                    }
                }
            }
        }
    }

    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);
    wifiManager.setConfigPortalTimeout(180);

    // Agregar los campos dinámicos al portal cautivo
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_programmer_id);

    // Si no hay red guardada o se invoca el portal, levanta el AP
    if (!wifiManager.autoConnect("GEYLCA_Config")) {
        delay(3000);
        ESP.restart();
    }

    // Guardar los nuevos valores introducidos en el portal cautivo
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port_str, custom_mqtt_port.getValue());
    strcpy(programmer_id, custom_programmer_id.getValue());

    DynamicJsonDocument json(512);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port_str;
    json["programmer_id"] = programmer_id;

    File configFile = LittleFS.open("/config.json", "w");
    if (configFile) {
        serializeJson(json, configFile);
        configFile.close();
    }

    client.setServer(mqtt_server, atoi(mqtt_port_str));
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    if (millis() - lastPingTime > 30000) {
        lastPingTime = millis();
        DynamicJsonDocument s(256);
        s["status"] = "ONLINE";
        s["type"] = "programador";
        s["name"] = "Programador Inalambrico V1.0";
        s["id"] = programmer_id;
        char osc[256];
        serializeJson(s, osc);
        client.publish(getTopic("status").c_str(), osc, true);
    }

    if (lecturaEsperando) {
        if (millis() - lecturaInicio > READ_TIMEOUT_MS) {
            lecturaEsperando = false;
            lastReadKey = "";
            publicarState("TIMEOUT", "");

            // Parpadeo de timeout
            for (int i = 0; i < 3; i++) {
                digitalWrite(LED_PIN, LOW);
                digitalWrite(BUILTIN_LED, HIGH);
                delay(150);
                digitalWrite(LED_PIN, HIGH);
                digitalWrite(BUILTIN_LED, LOW);
                delay(100);
            }
        } else {
            checkiButton();
        }
    }

    yield();
}