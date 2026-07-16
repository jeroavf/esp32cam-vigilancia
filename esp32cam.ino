/*
 * ESP32-CAM Stream + MQTT
 * Hardware: AI-Thinker ESP32-CAM + shield USB CAM-MB
 *
 * Boot: brownout off → WiFi/portal → câmera → HTTP → MQTT
 * (portal primeiro: se a câmera travar, o AP ainda aparece)
 *
 * Uso:  http://IP/  ou  http://esp32cam.local/
 * MQTT: capture / led_toggle nos tópicos configurados no portal
 */

#include "config.h"
#include "boot.h"
#include "camera.h"
#include "network.h"
#include "webserver.h"
#include "led.h"
#include "mqtt.h"

void setup() {
    // 1) Brownout off + Serial
    bootInit();

    // 2) LED onboard (flash GPIO4) cedo para feedback visual
    ledInit();

    // 3) Wi-Fi / portal ANTES da câmera
    //    Assim o AP "ESP32CAM-Setup" sobe mesmo se a OV2640 falhar.
    delay(100);
    networkInit();

    // 4) Câmera depois (evita atrasar/bloquear o portal)
    delay(200);
    if (!cameraInit()) {
        Serial.println(F("[MAIN] camera falhou — pagina sobe, stream/MQTT capture indisponivel"));
    }

    // 5) MQTT carrega prefs sempre; conecta só se houver WiFi
    mqttInit();

    // 6) Servidor HTTP (se WiFi OK)
    if (networkIsConnected()) {
        webserverStart();
    } else {
        Serial.println(F("[MAIN] offline — reinicie para abrir o portal de novo"));
    }

    Serial.println(F("[MAIN] setup concluido"));
    bootLogHeap("MAIN");
}

void loop() {
    networkUpdate();
    webserverUpdate();
    mqttUpdate();
    delay(2);
}
