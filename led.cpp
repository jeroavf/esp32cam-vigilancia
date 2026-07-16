#include "led.h"
#include "config.h"

#include <Arduino.h>

static bool s_on = false;

void ledInit(void) {
    pinMode(LED_GPIO, OUTPUT);
    ledSet(false);
    Serial.printf("[LED ] GPIO %d init (off)\n", LED_GPIO);
}

void ledSet(bool on) {
    s_on = on;
#if LED_ACTIVE_HIGH
    digitalWrite(LED_GPIO, on ? HIGH : LOW);
#else
    digitalWrite(LED_GPIO, on ? LOW : HIGH);
#endif
}

void ledToggle(void) {
    ledSet(!s_on);
    Serial.printf("[LED ] toggle -> %s\n", s_on ? "ON" : "OFF");
}

bool ledIsOn(void) {
    return s_on;
}
