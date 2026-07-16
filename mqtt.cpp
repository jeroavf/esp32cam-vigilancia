#include "mqtt.h"
#include "config.h"
#include "camera.h"
#include "led.h"
#include "network.h"
#include "boot.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ctype.h>
#include <string.h>

static Preferences s_prefs;
static WiFiClient s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);

static char s_host[64];
static char s_user[48];
static char s_pass[48];
static char s_topicIn[96];
static char s_topicOut[96];
static uint16_t s_port = MQTT_DEFAULT_PORT;

static char s_clientId[32];
static bool s_enabled = false;
static unsigned long s_lastReconnectMs = 0;

// Comando pendente (callback MQTT não deve capturar frame)
enum PendingCmd : uint8_t {
    CMD_NONE = 0,
    CMD_CAPTURE,
    CMD_LED_TOGGLE,
    CMD_LED_ON,
    CMD_LED_OFF
};
static volatile uint8_t s_pending = CMD_NONE;

static void trimInPlace(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void copyTrimmed(char* dst, size_t dstLen, const char* src) {
    if (!dst || dstLen == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dstLen, "%s", src);
    trimInPlace(dst);
}

static void loadMqttPrefs(void) {
    s_prefs.begin(MQTT_PREFS_NS, true);
    String host = s_prefs.getString("host", MQTT_DEFAULT_HOST);
    s_port = (uint16_t)s_prefs.getUShort("port", MQTT_DEFAULT_PORT);
    String user = s_prefs.getString("user", MQTT_DEFAULT_USER);
    String pass = s_prefs.getString("pass", MQTT_DEFAULT_PASS);
    String tin = s_prefs.getString("tin", MQTT_DEFAULT_TOPIC_IN);
    String tout = s_prefs.getString("tout", MQTT_DEFAULT_TOPIC_OUT);
    s_prefs.end();

    if (s_port == 0) s_port = MQTT_DEFAULT_PORT;

    copyTrimmed(s_host, sizeof(s_host), host.c_str());
    copyTrimmed(s_user, sizeof(s_user), user.c_str());
    copyTrimmed(s_pass, sizeof(s_pass), pass.c_str());
    copyTrimmed(s_topicIn, sizeof(s_topicIn), tin.c_str());
    copyTrimmed(s_topicOut, sizeof(s_topicOut), tout.c_str());

    s_enabled = (s_host[0] != '\0');
}

static void saveMqttPrefs(void) {
    s_prefs.begin(MQTT_PREFS_NS, false);
    s_prefs.putString("host", s_host);
    s_prefs.putUShort("port", s_port);
    s_prefs.putString("user", s_user);
    s_prefs.putString("pass", s_pass);
    s_prefs.putString("tin", s_topicIn);
    s_prefs.putString("tout", s_topicOut);
    s_prefs.end();
}

static void buildClientId(void) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_clientId, sizeof(s_clientId), "%s%02X%02X%02X",
             MQTT_CLIENT_ID_PREFIX, mac[3], mac[4], mac[5]);
}

static void toLowerInPlace(char* s) {
    for (; s && *s; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static uint8_t parseCommand(const char* payload, unsigned int len) {
    char buf[48];
    if (!payload || len == 0) return CMD_NONE;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, payload, len);
    buf[len] = '\0';
    trimInPlace(buf);
    toLowerInPlace(buf);

    if (strcmp(buf, MQTT_CMD_CAPTURE) == 0 ||
        strcmp(buf, "snapshot") == 0 ||
        strcmp(buf, "photo") == 0) {
        return CMD_CAPTURE;
    }
    if (strcmp(buf, MQTT_CMD_LED_TOGGLE) == 0 ||
        strcmp(buf, "led") == 0 ||
        strcmp(buf, "toggle") == 0) {
        return CMD_LED_TOGGLE;
    }
    if (strcmp(buf, MQTT_CMD_LED_ON) == 0) return CMD_LED_ON;
    if (strcmp(buf, MQTT_CMD_LED_OFF) == 0) return CMD_LED_OFF;
    return CMD_NONE;
}

static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    const uint8_t cmd = parseCommand((const char*)payload, length);
    if (cmd == CMD_NONE) {
        Serial.printf("[MQTT] cmd desconhecido (len=%u)\n", length);
        return;
    }
    s_pending = cmd;
    Serial.printf("[MQTT] cmd=%u enfileirado\n", (unsigned)cmd);
}

static void mqttDisconnectQuiet(void) {
    if (s_mqtt.connected()) {
        s_mqtt.disconnect();
    }
}

static bool mqttConnectNow(void) {
    if (!s_enabled || !networkIsConnected()) return false;

    s_mqtt.setServer(s_host, s_port);
    s_mqtt.setCallback(onMqttMessage);
    s_mqtt.setKeepAlive(MQTT_KEEPALIVE_SEC);
    s_mqtt.setSocketTimeout(5);

    if (!s_mqtt.setBufferSize(MQTT_BUFFER_SIZE)) {
        Serial.printf("[MQTT] setBufferSize(%u) falhou — tentando mesmo assim\n",
                      (unsigned)MQTT_BUFFER_SIZE);
    }

    Serial.printf("[MQTT] conectando %s:%u id=%s ...\n",
                  s_host, (unsigned)s_port, s_clientId);

    bool ok;
    if (s_user[0] != '\0') {
        ok = s_mqtt.connect(s_clientId, s_user, s_pass);
    } else {
        ok = s_mqtt.connect(s_clientId);
    }

    if (!ok) {
        Serial.printf("[MQTT] falha rc=%d\n", s_mqtt.state());
        return false;
    }

    if (s_topicIn[0] != '\0') {
        if (s_mqtt.subscribe(s_topicIn)) {
            Serial.printf("[MQTT] sub: %s\n", s_topicIn);
        } else {
            Serial.println(F("[MQTT] subscribe falhou"));
        }
    }

    Serial.printf("[MQTT] OK  out=%s\n", s_topicOut);
    bootLogHeap("MQTT");
    return true;
}

static void handleCapture(void) {
    if (!cameraIsReady()) {
        Serial.println(F("[MQTT] capture: camera nao pronta"));
        return;
    }
    if (s_topicOut[0] == '\0') {
        Serial.println(F("[MQTT] capture: topico de saida vazio"));
        return;
    }
    if (!s_mqtt.connected()) {
        Serial.println(F("[MQTT] capture: desconectado"));
        return;
    }

    camera_fb_t* discard = cameraCapture();
    if (discard) cameraRelease(discard);

    camera_fb_t* fb = cameraCapture();
    if (!fb || !fb->buf || fb->len == 0) {
        Serial.println(F("[MQTT] capture: falha ao obter frame"));
        if (fb) cameraRelease(fb);
        return;
    }

    const size_t len = fb->len;
    const uint8_t* buf = fb->buf;

    Serial.printf("[MQTT] publicando JPEG %u bytes em %s\n",
                  (unsigned)len, s_topicOut);

    if (len + 128 > MQTT_BUFFER_SIZE) {
        Serial.printf("[MQTT] JPEG %u > buffer %u — reduza resolucao/quality\n",
                      (unsigned)len, (unsigned)MQTT_BUFFER_SIZE);
        cameraRelease(fb);
        return;
    }

    const bool started = s_mqtt.beginPublish(s_topicOut, len, false);
    if (!started) {
        Serial.println(F("[MQTT] beginPublish falhou (buffer/broker?)"));
        cameraRelease(fb);
        return;
    }

    const size_t written = s_mqtt.write(buf, len);
    const bool ended = s_mqtt.endPublish();
    cameraRelease(fb);

    if (written != len || !ended) {
        Serial.printf("[MQTT] publish incompleto written=%u/%u ended=%d\n",
                      (unsigned)written, (unsigned)len, ended ? 1 : 0);
    } else {
        Serial.println(F("[MQTT] imagem publicada"));
    }
    bootLogHeap("MQTT");
}

static void processPending(void) {
    uint8_t cmd = s_pending;
    if (cmd == CMD_NONE) return;
    s_pending = CMD_NONE;

    switch (cmd) {
        case CMD_CAPTURE:
            handleCapture();
            break;
        case CMD_LED_TOGGLE:
            ledToggle();
            break;
        case CMD_LED_ON:
            ledSet(true);
            Serial.println(F("[LED ] ON (MQTT)"));
            break;
        case CMD_LED_OFF:
            ledSet(false);
            Serial.println(F("[LED ] OFF (MQTT)"));
            break;
        default:
            break;
    }
}

// Escapa aspas/backslash para JSON em strings de config.
static void jsonEscape(const char* src, char* dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    size_t j = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && j + 2 < dstLen; ++i) {
        const char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 3 >= dstLen) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if ((unsigned char)c < 0x20) {
            // ignora controles
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

void mqttInit(void) {
    loadMqttPrefs();
    buildClientId();
    s_pending = CMD_NONE;
    s_lastReconnectMs = 0;

    if (!s_enabled) {
        Serial.println(F("[MQTT] desligado (host vazio — configure na pagina web)"));
        return;
    }

    Serial.printf("[MQTT] host=%s port=%u in=%s out=%s\n",
                  s_host, (unsigned)s_port, s_topicIn, s_topicOut);

    if (networkIsConnected()) {
        mqttConnectNow();
    }
}

void mqttUpdate(void) {
    if (!s_enabled) return;

    if (!networkIsConnected()) {
        mqttDisconnectQuiet();
        return;
    }

    if (!s_mqtt.connected()) {
        const unsigned long now = millis();
        if (now - s_lastReconnectMs >= MQTT_RECONNECT_MS) {
            s_lastReconnectMs = now;
            mqttConnectNow();
        }
    } else {
        s_mqtt.loop();
    }

    processPending();
}

bool mqttIsEnabled(void) {
    return s_enabled;
}

bool mqttIsConnected(void) {
    return s_enabled && s_mqtt.connected();
}

bool mqttFormatHost(char* buf, size_t len) {
    if (!buf || len < 2) return false;
    if (!s_enabled || s_host[0] == '\0') {
        snprintf(buf, len, "—");
        return false;
    }
    snprintf(buf, len, "%s:%u", s_host, (unsigned)s_port);
    return true;
}

bool mqttFormatTopicIn(char* buf, size_t len) {
    if (!buf || len < 2) return false;
    snprintf(buf, len, "%s", s_topicIn[0] ? s_topicIn : "—");
    return s_topicIn[0] != '\0';
}

bool mqttFormatTopicOut(char* buf, size_t len) {
    if (!buf || len < 2) return false;
    snprintf(buf, len, "%s", s_topicOut[0] ? s_topicOut : "—");
    return s_topicOut[0] != '\0';
}

bool mqttGetConfigJson(char* buf, size_t len) {
    if (!buf || len < 32) return false;

    char h[128], u[96], p[96], ti[192], to[192];
    jsonEscape(s_host, h, sizeof(h));
    jsonEscape(s_user, u, sizeof(u));
    jsonEscape(s_pass, p, sizeof(p));
    jsonEscape(s_topicIn, ti, sizeof(ti));
    jsonEscape(s_topicOut, to, sizeof(to));

    const int n = snprintf(
        buf, len,
        "{\"host\":\"%s\",\"port\":%u,\"user\":\"%s\",\"pass\":\"%s\","
        "\"topic_in\":\"%s\",\"topic_out\":\"%s\","
        "\"enabled\":%s,\"connected\":%s}",
        h,
        (unsigned)s_port,
        u,
        p,
        ti,
        to,
        s_enabled ? "true" : "false",
        mqttIsConnected() ? "true" : "false");

    return n > 0 && (size_t)n < len;
}

bool mqttApplyConfig(const char* host, uint16_t port,
                     const char* user, const char* pass,
                     const char* topicIn, const char* topicOut) {
    copyTrimmed(s_host, sizeof(s_host), host);
    copyTrimmed(s_user, sizeof(s_user), user);
    copyTrimmed(s_pass, sizeof(s_pass), pass);
    copyTrimmed(s_topicIn, sizeof(s_topicIn), topicIn);
    copyTrimmed(s_topicOut, sizeof(s_topicOut), topicOut);

    if (port == 0) port = MQTT_DEFAULT_PORT;
    s_port = port;

    if (s_host[0] != '\0') {
        if (s_topicIn[0] == '\0') {
            snprintf(s_topicIn, sizeof(s_topicIn), "%s", MQTT_DEFAULT_TOPIC_IN);
        }
        if (s_topicOut[0] == '\0') {
            snprintf(s_topicOut, sizeof(s_topicOut), "%s", MQTT_DEFAULT_TOPIC_OUT);
        }
    }

    s_enabled = (s_host[0] != '\0');
    saveMqttPrefs();

    Serial.printf("[MQTT] prefs salvas host=%s port=%u in=%s out=%s\n",
                  s_host[0] ? s_host : "(off)",
                  (unsigned)s_port,
                  s_topicIn,
                  s_topicOut);

    mqttDisconnectQuiet();
    s_lastReconnectMs = 0;

    if (!s_enabled) {
        Serial.println(F("[MQTT] desligado (host vazio)"));
        return true;
    }

    if (!networkIsConnected()) {
        Serial.println(F("[MQTT] config salva — WiFi offline, conecta depois"));
        return true;
    }

    // Tentativa imediata; mqttUpdate reintenta se falhar
    mqttConnectNow();
    return true;
}
