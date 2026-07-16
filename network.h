#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void networkInit(void);
void networkUpdate(void);

bool networkIsConnected(void);
bool networkUsesStaticIp(void);

// Preenche buf com IP atual (ex.: "192.168.1.50"). Retorna false se offline.
bool networkFormatIp(char* buf, size_t len);
bool networkFormatMode(char* buf, size_t len);  // "DHCP" ou "STATIC"
const char* networkHostname(void);
