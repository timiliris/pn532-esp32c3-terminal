#pragma once
#include <Arduino.h>

void uiBegin();
void uiBoot(const String &line);  // boot screen, drawn immediately
void uiTick();                    // main loop only (the bus is shared with the PN532)
