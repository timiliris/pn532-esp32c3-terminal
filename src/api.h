#pragma once
#include <ArduinoJson.h>

void apiBegin();
void apiTick();
// Broadcasts {"event": type, "data": ..., "t": epoch} to the paired WebSocket clients.
void emitEvent(const char *type, JsonDocument &data);
// Erases Wi-Fi credentials, paired apps and settings, then reboots.
void factoryReset();
