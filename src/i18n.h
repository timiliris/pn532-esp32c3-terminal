#pragma once
#include <Arduino.h>

// Text shown on the display and in job messages, in the device language (en / fr).
// API error messages stay in English: they are meant for developers.
enum Str : uint8_t {
  T_STARTING, T_WIFI, T_IDLE_1, T_IDLE_2, T_PAIRING, T_ENTER_CODE, T_PLACE_BADGE, T_HOLD_STILL,
  T_BADGE_DETECTED, T_NO_READER, T_CHECK_WIRING, T_WIFI_SETUP, T_NETWORK, T_PASSWORD,
  T_NDEF_BYTES, T_UNSUPPORTED, T_WIFI_LOST, T_PAIRED, T_RESET_HOLD, T_RESETTING, T_UPDATING,
  T_JOB_READ, T_JOB_NDEF, T_JOB_RAW, T_JOB_ERASE,
  // job messages
  M_READ_DONE, M_REMOVED, M_UNSUPPORTED, M_NDEF_T2_ONLY, M_NOT_FORMATTED, M_READ_FAILED,
  M_TOO_LONG, M_NDEF_WRITTEN, M_WRITE_FAILED_PAGE, M_WRITE_FAILED_BLOCK, M_MAX_BLOCK, M_MAX_PAGE,
  M_BLOCK_RANGE, M_PAGE_RANGE, M_TRAILER, M_AUTH_FAILED, M_WRITE_FAILED, M_BLOCK_WRITTEN,
  M_PAGE_WRITTEN, M_ERASED_T2, M_ERASED_CLASSIC, M_ERASED_LOCKED, M_TIMEOUT, M_CANCELLED,
  STR_COUNT
};

void setLang(const String &lang);  // "en" or "fr"
const char *tr(Str id);
String trf(int id, ...);          // tr() used as a printf format (int: va_start needs a promoted type)
