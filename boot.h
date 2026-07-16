#pragma once

// Inicialização segura para ESP32-CAM + CAM-MB (brownout / picos de corrente).
void bootInit(void);
void bootLogHeap(const char* tag);
