#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_camera.h"

bool cameraInit(void);
bool cameraIsReady(void);

// framesize: enum framesize_t (0–13 típico OV2640)
// quality: 10–63 (menor = melhor qualidade JPEG)
bool cameraSetFramesize(int framesize);
bool cameraSetQuality(int quality);

int cameraGetFramesize(void);
int cameraGetQuality(void);

camera_fb_t* cameraCapture(void);
void cameraRelease(camera_fb_t* fb);
