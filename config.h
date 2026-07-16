#pragma once

// ===== Projeto: ESP32-CAM Stream (MVP) =====
// Hardware: AI-Thinker ESP32-CAM + shield CAM-MB

// ----- Serial -----
#define SERIAL_BAUD              115200
// CAM-MB + USB: ao abrir serial a placa reseta; settle maior reduz "loop" de boot
#define BOOT_SETTLE_MS           500

// ----- Câmera (AI-Thinker pinout) -----
#define CAM_PWDN_GPIO            32
#define CAM_RESET_GPIO           -1
#define CAM_XCLK_GPIO            0
#define CAM_SIOD_GPIO            26
#define CAM_SIOC_GPIO            27
#define CAM_Y9_GPIO              35
#define CAM_Y8_GPIO              34
#define CAM_Y7_GPIO              39
#define CAM_Y6_GPIO              36
#define CAM_Y5_GPIO              21
#define CAM_Y4_GPIO              19
#define CAM_Y3_GPIO              18
#define CAM_Y2_GPIO              5
#define CAM_VSYNC_GPIO           25
#define CAM_HREF_GPIO            23
#define CAM_PCLK_GPIO            22

#define CAM_XCLK_FREQ_HZ         20000000
// Valor numérico do enum framesize_t (FRAMESIZE_VGA = 8) — evita dependência de esp_camera.h aqui
#define CAM_DEFAULT_FRAMESIZE    8
#define CAM_DEFAULT_QUALITY      12              // 10–63 (menor = melhor)
#define CAM_INIT_RETRIES         5
#define CAM_INIT_RETRY_MS        500

// ----- WiFi / portal -----
#define WIFI_PORTAL_NAME         "ESP32CAM-Setup"
#define WIFI_PORTAL_TIMEOUT_SEC  180
#define WIFI_CONNECT_TIMEOUT_SEC 30
#define WIFI_NETWORK_CHECK_MS    30000
#define WIFI_HOSTNAME            "esp32cam"
#define WIFI_MDNS_NAME           "esp32cam"

// Namespace Preferences (NVS)
#define NET_PREFS_NS             "netcfg"

// Defaults IP estático (só se o usuário ativar no portal)
#define NET_DEFAULT_STATIC_IP    "192.168.1.50"
#define NET_DEFAULT_GATEWAY      "192.168.1.1"
#define NET_DEFAULT_SUBNET       "255.255.255.0"
#define NET_DEFAULT_DNS          "192.168.1.1"

// ----- HTTP -----
// Porta 80 = página + /status + /control
// Porta 81 = stream MJPEG (servidor separado para não bloquear a API)
#define WEB_SERVER_PORT          80
#define STREAM_SERVER_PORT       81
#define STREAM_BOUNDARY          "frame"
