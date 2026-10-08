#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

void nfcBegin();
void nfcTick();   // main loop only: the only code allowed to talk to the PN532

// Validates [{type: "url"|"text", value, lang?}] and builds the NDEF message TLV.
// Returns an empty string on success, otherwise the error.
String buildNdef(JsonArrayConst records, std::vector<uint8_t> &out, const String &defaultLang);
