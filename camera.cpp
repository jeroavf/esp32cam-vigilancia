#include "camera.h"
#include "config.h"
#include "boot.h"

#include <Arduino.h>

static bool s_ready = false;
static int s_framesize = (int)CAM_DEFAULT_FRAMESIZE;
static int s_quality = CAM_DEFAULT_QUALITY;

static bool cameraInitOnce(void) {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_Y2_GPIO;
    config.pin_d1 = CAM_Y3_GPIO;
    config.pin_d2 = CAM_Y4_GPIO;
    config.pin_d3 = CAM_Y5_GPIO;
    config.pin_d4 = CAM_Y6_GPIO;
    config.pin_d5 = CAM_Y7_GPIO;
    config.pin_d6 = CAM_Y8_GPIO;
    config.pin_d7 = CAM_Y9_GPIO;
    config.pin_xclk = CAM_XCLK_GPIO;
    config.pin_pclk = CAM_PCLK_GPIO;
    config.pin_vsync = CAM_VSYNC_GPIO;
    config.pin_href = CAM_HREF_GPIO;
    config.pin_sccb_sda = CAM_SIOD_GPIO;
    config.pin_sccb_scl = CAM_SIOC_GPIO;
    config.pin_pwdn = CAM_PWDN_GPIO;
    config.pin_reset = CAM_RESET_GPIO;
    config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = (framesize_t)CAM_DEFAULT_FRAMESIZE;
    config.jpeg_quality = CAM_DEFAULT_QUALITY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.fb_count = 2;
        Serial.println(F("[CAM ] PSRAM found — fb_count=2"));
    } else {
        // Sem PSRAM: frame menor e 1 buffer na DRAM
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println(F("[CAM ] PSRAM NOT found — fallback SVGA/DRAM"));
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM ] esp_camera_init failed: 0x%x\n", (unsigned)err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        // Ajustes leves para stream estável
        s->set_brightness(s, 0);
        s->set_saturation(s, 0);
        s->set_framesize(s, (framesize_t)config.frame_size);
        s->set_quality(s, config.jpeg_quality);
        s_framesize = (int)config.frame_size;
        s_quality = config.jpeg_quality;
    }

    return true;
}

bool cameraInit(void) {
    s_ready = false;

    for (int attempt = 1; attempt <= CAM_INIT_RETRIES; attempt++) {
        Serial.printf("[CAM ] init attempt %d/%d\n", attempt, CAM_INIT_RETRIES);

        if (attempt > 1) {
            // Tenta deinit se já houver estado parcial
            esp_camera_deinit();
            delay(CAM_INIT_RETRY_MS);
        }

        if (cameraInitOnce()) {
            s_ready = true;
            Serial.println(F("[CAM ] init OK"));
            bootLogHeap("CAM");
            return true;
        }

        delay(CAM_INIT_RETRY_MS);
    }

    Serial.println(F("[CAM ] init FAILED after retries"));
    return false;
}

bool cameraIsReady(void) {
    return s_ready;
}

bool cameraSetFramesize(int framesize) {
    if (!s_ready) return false;
    if (framesize < (int)FRAMESIZE_96X96 || framesize > (int)FRAMESIZE_UXGA) {
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (!s || !s->set_framesize) return false;

    // OV2640 costuma retornar 0; alguns drivers retornam outros códigos não-negativos.
    const int rc = s->set_framesize(s, (framesize_t)framesize);
    if (rc < 0) {
        Serial.printf("[CAM ] set_framesize failed rc=%d\n", rc);
        return false;
    }
    s_framesize = framesize;
    Serial.printf("[CAM ] framesize=%d (rc=%d)\n", framesize, rc);
    return true;
}

bool cameraSetQuality(int quality) {
    if (!s_ready) return false;
    if (quality < 10) quality = 10;
    if (quality > 63) quality = 63;

    sensor_t* s = esp_camera_sensor_get();
    if (!s || !s->set_quality) return false;

    const int rc = s->set_quality(s, quality);
    if (rc < 0) {
        Serial.printf("[CAM ] set_quality failed rc=%d\n", rc);
        return false;
    }
    s_quality = quality;
    Serial.printf("[CAM ] quality=%d (rc=%d)\n", quality, rc);
    return true;
}

int cameraGetFramesize(void) {
    return s_framesize;
}

int cameraGetQuality(void) {
    return s_quality;
}

camera_fb_t* cameraCapture(void) {
    if (!s_ready) return nullptr;
    return esp_camera_fb_get();
}

void cameraRelease(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}
