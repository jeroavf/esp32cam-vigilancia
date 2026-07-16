#pragma once

#include <stdbool.h>

void ledInit(void);
void ledSet(bool on);
void ledToggle(void);
bool ledIsOn(void);
