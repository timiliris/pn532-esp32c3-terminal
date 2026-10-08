#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

Shared S;

static SemaphoreHandle_t mtx() {
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}
Lock::Lock() { xSemaphoreTakeRecursive(mtx(), portMAX_DELAY); }
Lock::~Lock() { xSemaphoreGiveRecursive(mtx()); }

Job *activeJob() {
  for (auto &j : S.jobs) if (j.active()) return &j;
  return nullptr;
}

Job *findJob(uint32_t id) {
  for (auto &j : S.jobs) if (j.id == id) return &j;
  return nullptr;
}

String hexStr(const uint8_t *b, size_t n, char sep) {
  static const char *H = "0123456789ABCDEF";
  String s;
  s.reserve(n * 3);
  for (size_t i = 0; i < n; i++) {
    if (sep && i) s += sep;
    s += H[b[i] >> 4];
    s += H[b[i] & 0xF];
  }
  return s;
}

int parseHex(String s, uint8_t *out, size_t maxLen) {
  s.replace(" ", ""); s.replace(":", ""); s.replace("-", "");
  if (s.length() % 2) return -1;
  size_t n = s.length() / 2;
  if (n > maxLen) return -1;
  for (size_t i = 0; i < n; i++) {
    char buf[3] = {s[2 * i], s[2 * i + 1], 0};
    char *end;
    long v = strtol(buf, &end, 16);
    if (*end) return -1;
    out[i] = (uint8_t)v;
  }
  return n;
}

time_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? t : 0;
}

const char *jobKindName(JobKind k) {
  switch (k) { case J_NDEF: return "write_ndef"; case J_RAW: return "write_raw"; case J_ERASE: return "erase"; default: return "read"; }
}

const char *jobStateName(JobState s) {
  switch (s) {
    case S_RUNNING: return "running";
    case S_DONE: return "done";
    case S_ERROR: return "error";
    case S_CANCELLED: return "cancelled";
    case S_TIMEOUT: return "timeout";
    default: return "waiting";
  }
}

const char *cardKindName(CardKind k) {
  switch (k) { case K_CLASSIC: return "mifare_classic"; case K_T2: return "type2"; case K_OTHER: return "unsupported"; default: return "none"; }
}

void cardToJson(JsonObject o, const CardInfo &c) {
  o["uid"] = hexStr(c.uid, c.uidLen, ':');
  o["type"] = c.type;
  o["kind"] = cardKindName(c.kind);
  o["sak"] = c.sak;
  o["atqa"] = c.atqa;
  if (c.kind == K_T2) o["ndef_capacity"] = c.t2Cap;
  if (c.kind == K_CLASSIC) o["sectors"] = c.sectors;
}

void jobToJson(JsonObject o, const Job &j, bool withResult) {
  o["id"] = j.id;
  o["kind"] = jobKindName(j.kind);
  o["state"] = jobStateName(j.state);
  if (j.code.length()) o["code"] = j.code;
  o["message"] = j.message;
  if (j.uid.length()) o["uid"] = j.uid;
  if (j.state == S_WAITING) o["expires_in_ms"] = (int32_t)(j.deadline - millis());
  if (withResult && j.result.length()) o["result"] = serialized(j.result);
}
