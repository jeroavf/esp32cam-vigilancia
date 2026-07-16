#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inicializa prefs + cliente (chamar após WiFi quando possível).
void mqttInit(void);

// Loop: reconnect, processar mensagens e comandos pendentes.
void mqttUpdate(void);

bool mqttIsEnabled(void);
bool mqttIsConnected(void);

// Preenche buf com "host:port" ou "—".
bool mqttFormatHost(char* buf, size_t len);
bool mqttFormatTopicIn(char* buf, size_t len);
bool mqttFormatTopicOut(char* buf, size_t len);

// JSON da config (página web / formulário).
// Ex.: {"host":"...","port":1883,"user":"...","pass":"...","topic_in":"...","topic_out":"...","enabled":true,"connected":false}
bool mqttGetConfigJson(char* buf, size_t len);

// Salva em NVS e reaplica (disconnect + reconnect se host não vazio).
// Strings podem ser vazias; port 0 → default 1883.
bool mqttApplyConfig(const char* host, uint16_t port,
                     const char* user, const char* pass,
                     const char* topicIn, const char* topicOut);
