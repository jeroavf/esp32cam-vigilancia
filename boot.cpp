#include "boot.h"
#include "config.h"

#include <Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"

static const char* resetReasonText(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT (pino RST / DTR do USB)";
        case ESP_RST_SW:        return "SW (ESP.restart)";
        case ESP_RST_PANIC:     return "PANIC (crash)";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (queda de tensao!)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

void bootInit(void) {
    // Desliga o detector de brownout o mais cedo possível.
    // Comum em ESP32-CAM alimentada por USB/CAM-MB: picos de Wi-Fi/câmera
    // disparam reboot em loop mesmo com fonte "ok" para o notebook.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Pausa logo após reset: abrir o monitor serial no CAM-MB pulsa DTR/RTS
    // e reinicia a placa; sem settle a alimentação USB ainda oscila.
    delay(BOOT_SETTLE_MS);

    Serial.begin(SERIAL_BAUD);
    delay(BOOT_SETTLE_MS);

    // Evita que o USB-serial fique segurando linhas de forma estranha em alguns hosts
    Serial.setDebugOutput(false);

    esp_reset_reason_t reason = esp_reset_reason();

    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F(" ESP32-CAM Stream MVP"));
    Serial.println(F("========================================"));
    Serial.println(F("[BOOT] brownout detector disabled"));
    Serial.printf("[BOOT] reset reason: %d (%s)\n", (int)reason, resetReasonText(reason));
    if (reason == ESP_RST_EXT) {
        Serial.println(F("[BOOT] Dica: abrir a porta serial no CAM-MB REINICIA a placa (DTR/RTS)."));
        Serial.println(F("[BOOT] Use monitor com dtr=off,rts=off ou: stty -F /dev/ttyUSB0 -hupcl"));
    }
    if (reason == ESP_RST_BROWNOUT) {
        Serial.println(F("[BOOT] Alimentacao fraca — cabo USB curto, outra porta ou 5V externo >=1A."));
    }
    bootLogHeap("BOOT");
}

void bootLogHeap(const char* tag) {
    Serial.printf("[%s] heap free=%u min=%u psram=%u\n",
                  tag ? tag : "MEM",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getFreePsram());
}
