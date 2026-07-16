/*
 * ESP32-CAM Stream MVP
 * Hardware: AI-Thinker ESP32-CAM + shield USB CAM-MB
 *
 * Boot: brownout off → WiFi/portal → câmera → HTTP
 * (portal primeiro: se a câmera travar, o AP ainda aparece)
 *
 * Uso:  http://IP/  ou  http://esp32cam.local/
 */

#include "config.h"
#include "boot.h"
#include "camera.h"
#include "network.h"
#include "webserver.h"

void setup() {
    // 1) Brownout off + Serial
    bootInit();

    // 2) Wi-Fi / portal ANTES da câmera
    //    Assim o AP "ESP32CAM-Setup" sobe mesmo se a OV2640 falhar.
    delay(100);
    networkInit();

    // 3) Câmera depois (evita atrasar/bloquear o portal)
    delay(200);
    if (!cameraInit()) {
        Serial.println(F("[MAIN] camera falhou — pagina sobe, stream indisponivel"));
    }

    // 4) Servidor HTTP
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
    delay(2);
}
