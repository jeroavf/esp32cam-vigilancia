#include "network.h"
#include "config.h"
#include "boot.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ESPmDNS.h>

static Preferences s_prefs;
static bool s_useStatic = false;
static unsigned long s_lastCheckMs = 0;

// Buffers dos campos do portal (precisam viver até o fim do portal)
static char s_useStaticStr[2];
static char s_ipStr[16];
static char s_gwStr[16];
static char s_snStr[16];
static char s_dnsStr[16];

static WiFiManagerParameter* s_pUseStatic = nullptr;
static WiFiManagerParameter* s_pIp = nullptr;
static WiFiManagerParameter* s_pGw = nullptr;
static WiFiManagerParameter* s_pSn = nullptr;
static WiFiManagerParameter* s_pDns = nullptr;

static void loadNetPrefs(void) {
    s_prefs.begin(NET_PREFS_NS, true);
    s_useStatic = s_prefs.getBool("static", false);
    String ip = s_prefs.getString("ip", NET_DEFAULT_STATIC_IP);
    String gw = s_prefs.getString("gw", NET_DEFAULT_GATEWAY);
    String sn = s_prefs.getString("sn", NET_DEFAULT_SUBNET);
    String dns = s_prefs.getString("dns", NET_DEFAULT_DNS);
    s_prefs.end();

    snprintf(s_useStaticStr, sizeof(s_useStaticStr), "%s", s_useStatic ? "1" : "0");
    snprintf(s_ipStr, sizeof(s_ipStr), "%s", ip.c_str());
    snprintf(s_gwStr, sizeof(s_gwStr), "%s", gw.c_str());
    snprintf(s_snStr, sizeof(s_snStr), "%s", sn.c_str());
    snprintf(s_dnsStr, sizeof(s_dnsStr), "%s", dns.c_str());
}

static bool parseIp4(const char* text, IPAddress& out) {
    if (!text || !text[0]) return false;
    return out.fromString(text);
}

static bool applyStaticConfigFromBuffers(void) {
    IPAddress ip, gw, sn, dns;
    if (!parseIp4(s_ipStr, ip) || !parseIp4(s_gwStr, gw) || !parseIp4(s_snStr, sn)) {
        Serial.println(F("[NET ] IP estático inválido — usando DHCP"));
        s_useStatic = false;
        return false;
    }
    if (!parseIp4(s_dnsStr, dns)) {
        dns = gw;
    }

    if ((uint32_t)ip == 0 || (uint32_t)gw == 0) {
        Serial.println(F("[NET ] IP/gateway zerado — usando DHCP"));
        s_useStatic = false;
        return false;
    }

    if (!WiFi.config(ip, gw, sn, dns)) {
        Serial.println(F("[NET ] WiFi.config falhou — usando DHCP"));
        s_useStatic = false;
        return false;
    }

    Serial.printf("[NET ] IP estático: %s gw=%s sn=%s dns=%s\n",
                  s_ipStr, s_gwStr, s_snStr, s_dnsStr);
    return true;
}

static void saveNetPrefsFromPortal(void) {
    if (!s_pUseStatic || !s_pIp || !s_pGw || !s_pSn || !s_pDns) return;

    const char* useVal = s_pUseStatic->getValue();
    s_useStatic = (useVal && useVal[0] == '1');

    snprintf(s_ipStr, sizeof(s_ipStr), "%s", s_pIp->getValue());
    snprintf(s_gwStr, sizeof(s_gwStr), "%s", s_pGw->getValue());
    snprintf(s_snStr, sizeof(s_snStr), "%s", s_pSn->getValue());
    snprintf(s_dnsStr, sizeof(s_dnsStr), "%s", s_pDns->getValue());

    s_prefs.begin(NET_PREFS_NS, false);
    s_prefs.putBool("static", s_useStatic);
    s_prefs.putString("ip", s_ipStr);
    s_prefs.putString("gw", s_gwStr);
    s_prefs.putString("sn", s_snStr);
    s_prefs.putString("dns", s_dnsStr);
    s_prefs.end();

    Serial.printf("[NET ] prefs salvas static=%d ip=%s\n",
                  s_useStatic ? 1 : 0, s_ipStr);
}

static void startMdns(void) {
    if (MDNS.begin(WIFI_MDNS_NAME)) {
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
        Serial.printf("[NET ] mDNS: http://%s.local/\n", WIFI_MDNS_NAME);
    } else {
        Serial.println(F("[NET ] mDNS falhou"));
    }
}

static void addPortalParameters(WiFiManager& wm) {
    if (!s_pUseStatic) {
        s_pUseStatic = new WiFiManagerParameter(
            "use_static", "IP fixo? (1=sim, 0=DHCP)", s_useStaticStr, 2);
        s_pIp = new WiFiManagerParameter("ip", "IP fixo", s_ipStr, 15);
        s_pGw = new WiFiManagerParameter("gw", "Gateway", s_gwStr, 15);
        s_pSn = new WiFiManagerParameter("sn", "Mascara", s_snStr, 15);
        s_pDns = new WiFiManagerParameter("dns", "DNS", s_dnsStr, 15);
    }

    wm.addParameter(s_pUseStatic);
    wm.addParameter(s_pIp);
    wm.addParameter(s_pGw);
    wm.addParameter(s_pSn);
    wm.addParameter(s_pDns);
    wm.setSaveConfigCallback(saveNetPrefsFromPortal);
}

static void configurePortalCommon(WiFiManager& wm) {
    wm.setHostname(WIFI_HOSTNAME);
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
    wm.setCaptivePortalEnable(true);
    wm.setBreakAfterConfig(true);
    // Canal fixo 1 — melhor visibilidade em celulares
    wm.setWiFiAPChannel(1);
    // AP aberto (sem senha) — mais fácil achar e conectar
    // (WiFiManager usa senha vazia se não passarmos)
    wm.setAPCallback([](WiFiManager* w) {
        (void)w;
        Serial.println();
        Serial.println(F("========================================"));
        Serial.println(F(" PORTAL WiFi ATIVO"));
        Serial.printf(" SSID:  %s\n", WIFI_PORTAL_NAME);
        Serial.println(F(" Senha: (nenhuma — rede aberta)"));
        Serial.println(F(" URL:   http://192.168.4.1"));
        Serial.println(F(" Use Wi-Fi 2.4 GHz no celular/PC"));
        Serial.println(F("========================================"));
        Serial.println();
    });

    if (s_useStatic) {
        IPAddress ip, gw, sn, dns;
        if (parseIp4(s_ipStr, ip) && parseIp4(s_gwStr, gw) &&
            parseIp4(s_snStr, sn)) {
            if (!parseIp4(s_dnsStr, dns)) dns = gw;
            wm.setSTAStaticIPConfig(ip, gw, sn, dns);
        }
    }

    addPortalParameters(wm);
}

static bool waitConnected(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

static void reapplyStaticIfNeeded(void) {
    if (!s_useStatic) return;

    Serial.println(F("[NET ] reaplicando IP estatico apos portal/connect"));
    WiFi.disconnect(false);
    delay(100);
    applyStaticConfigFromBuffers();
    WiFi.begin();
    waitConnected(WIFI_CONNECT_TIMEOUT_SEC * 1000UL);
}

void networkInit(void) {
    Serial.println(F("[NET ] iniciando WiFi/portal (antes da camera)"));
    loadNetPrefs();

    // AP+STA desde o início — softAP fica visível no portal
    WiFi.persistent(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(WIFI_HOSTNAME);
    // Máxima potência de TX ajuda o AP a aparecer na lista
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    if (s_useStatic) {
        applyStaticConfigFromBuffers();
    } else {
        Serial.println(F("[NET ] modo DHCP (padrao)"));
    }

    WiFiManager wm;
    configurePortalCommon(wm);

    const bool hasSaved = wm.getWiFiIsSaved();
    Serial.printf("[NET ] credenciais WiFi salvas: %s\n", hasSaved ? "sim" : "nao");

    bool ok = false;

    if (!hasSaved) {
        // Primeira configuração: abre o AP imediatamente (sem tentar STA).
        // Timeout 0 = fica no portal até o usuário configurar (não some em 3 min).
        Serial.printf("[NET ] sem rede salva — abrindo AP \"%s\" agora\n", WIFI_PORTAL_NAME);
        wm.setConfigPortalTimeout(0);
        ok = wm.startConfigPortal(WIFI_PORTAL_NAME);
    } else {
        // Já tem SSID: tenta conectar; se falhar, abre portal com timeout.
        Serial.println(F("[NET ] tentando rede salva..."));
        wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
        ok = wm.autoConnect(WIFI_PORTAL_NAME);

        if (!ok) {
            Serial.println(F("[NET ] falhou — reabrindo portal (sem timeout)"));
            wm.setConfigPortalTimeout(0);
            ok = wm.startConfigPortal(WIFI_PORTAL_NAME);
        }
    }

    reapplyStaticIfNeeded();
    ok = (WiFi.status() == WL_CONNECTED);

    // Garante que o softAP do portal foi desligado após conectar
    if (ok) {
        WiFi.mode(WIFI_STA);
    }

    if (ok) {
        Serial.printf("[NET ] WiFi OK  IP=%s  RSSI=%d  mode=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI(),
                      s_useStatic ? "STATIC" : "DHCP");
        Serial.printf("[NET ] MAC=%s  GW=%s\n",
                      WiFi.macAddress().c_str(),
                      WiFi.gatewayIP().toString().c_str());
        startMdns();
    } else {
        Serial.println(F("[NET ] WiFi falhou — reinicie a placa para abrir o portal de novo"));
    }

    s_lastCheckMs = millis();
    bootLogHeap("NET");
}

void networkUpdate(void) {
    unsigned long now = millis();
    if (now - s_lastCheckMs < WIFI_NETWORK_CHECK_MS) return;
    s_lastCheckMs = now;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[NET ] reconectando..."));
        WiFi.reconnect();
    }
}

bool networkIsConnected(void) {
    return WiFi.status() == WL_CONNECTED;
}

bool networkUsesStaticIp(void) {
    return s_useStatic;
}

bool networkFormatIp(char* buf, size_t len) {
    if (!buf || len < 8) return false;
    if (!networkIsConnected()) {
        snprintf(buf, len, "---");
        return false;
    }
    snprintf(buf, len, "%s", WiFi.localIP().toString().c_str());
    return true;
}

bool networkFormatMode(char* buf, size_t len) {
    if (!buf || len < 4) return false;
    snprintf(buf, len, "%s", s_useStatic ? "STATIC" : "DHCP");
    return true;
}

const char* networkHostname(void) {
    return WIFI_HOSTNAME;
}
