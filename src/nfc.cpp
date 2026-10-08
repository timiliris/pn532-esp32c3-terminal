#include "nfc.h"
#include <Wire.h>
#include <functional>
#include "config.h"
#include "state.h"
#include "pn532.h"
#include "ndef.h"
#include "i18n.h"
#include "api.h"

static PN532 pn;
static bool listening = false;
static uint32_t listenStart = 0, lastSeen = 0, lastHealth = 0, lastRetry = 0;
static uint8_t failures = 0;

// --- Bus and reader ------------------------------------------------------------

// Frees a slave stuck in the middle of a transaction (holding SDA low) with 9 clock pulses.
static void busRecover() {
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && !digitalRead(PIN_I2C_SDA); i++) {
    digitalWrite(PIN_I2C_SCL, LOW); delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH); delayMicroseconds(5);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
}

static void emitReader() {
  JsonDocument d;
  { Lock l; d["ok"] = S.readerOk; d["firmware"] = S.readerFw; }
  emitEvent("reader.status", d);
}

static void tryReader() {
  bool ok = pn.begin(Wire, PIN_PN532_IRQ);
  if (!ok) { busRecover(); ok = pn.begin(Wire, PIN_PN532_IRQ); }
  String fw;
  if (ok) {
    uint32_t v = pn.firmware();
    fw = String((v >> 16) & 0xFF) + "." + String((v >> 8) & 0xFF);
    Serial.printf("PN532 ready, firmware %s\n", fw.c_str());
  } else {
    Serial.println("PN532 not found (check wiring and the I2C switches)");
  }
  bool changed;
  { Lock l; changed = S.readerOk != ok; S.readerOk = ok; S.readerFw = fw; }
  listening = false;
  failures = 0;
  if (changed) emitReader();
}

static void readerLost() {
  Serial.println("PN532 lost, retrying");
  bool wasPresent;
  { Lock l; S.readerOk = false; wasPresent = S.present; S.present = false; }
  listening = false;
  lastRetry = millis();
  emitReader();
  if (wasPresent) { JsonDocument d; emitEvent("card.removed", d); }
}

void nfcBegin() { tryReader(); }

// --- Card helpers --------------------------------------------------------------

// MIFARE Classic layout (1K / 4K): 4-block sectors, then 16-block sectors past block 128
static int sectorFirst(int s) { return s < 32 ? s * 4 : 128 + (s - 32) * 16; }
static int sectorSize(int s)  { return s < 32 ? 4 : 16; }
static int blockSector(int b) { return b < 128 ? b / 4 : 32 + (b - 128) / 16; }
static bool isTrailer(int b)  { return b < 128 ? b % 4 == 3 : (b - 128) % 16 == 15; }
static int lastUserPage(const CardInfo &c) { return c.t2Cap ? 4 + c.t2Cap / 4 - 1 : 15; }

// A failed authentication or an RF error puts the card back to sleep: cycle the field
// and select it again.
static bool reselect(const CardInfo &c) {
  pn.field(false);
  delay(5);
  pn.field(true);
  delay(5);
  uint8_t uid[10], sak;
  uint16_t atqa;
  uint8_t n = pn.detect(uid, sak, atqa);
  return n == c.uidLen && memcmp(uid, c.uid, n) == 0;
}

// The RF link can be marginal (badge on the edge of the antenna): retry after reselecting.
// Page reads and writes are idempotent, so retrying is safe.
static bool t2Read(const CardInfo &c, uint8_t page, uint8_t d[16]) {
  for (int i = 0; i < 3; i++) {
    if (pn.read16(page, d)) return true;
    if (!reselect(c)) return false;
  }
  return false;
}

static bool t2Write(const CardInfo &c, uint8_t page, const uint8_t d[4]) {
  for (int i = 0; i < 3; i++) {
    if (pn.ulWrite(page, d)) return true;
    if (!reselect(c)) return false;
  }
  return false;
}

// Classic: after an error the sector must be authenticated again.
static bool mfRetry(const CardInfo &c, int block, const uint8_t key[6], std::function<bool()> op) {
  for (int i = 0; i < 3; i++) {
    if (op()) return true;
    if (!reselect(c) || !pn.mfAuth(sectorFirst(blockSector(block)), key, c.uid, c.uidLen)) return false;
  }
  return false;
}

static void identify(CardInfo &c) {
  c.sectors = 0; c.t2Pages = 0; c.t2Cap = 0;
  switch (c.sak) {
    case 0x09: c.kind = K_CLASSIC; c.sectors = 5;  c.type = "MIFARE Mini"; return;
    case 0x08: c.kind = K_CLASSIC; c.sectors = 16; c.type = "MIFARE Classic 1K"; return;
    case 0x18: c.kind = K_CLASSIC; c.sectors = 40; c.type = "MIFARE Classic 4K"; return;
    case 0x00: break;
    default:   c.kind = K_OTHER; c.type = "ISO 14443-4"; return;
  }
  c.kind = K_T2;
  uint8_t p[16];
  if (!t2Read(c, 3, p)) { c.type = "Ultralight / NTAG"; c.t2Pages = 16; return; }
  c.t2Cap = p[0] == 0xE1 ? p[2] * 8 : 0;  // capability container: magic, version, size / 8
  switch (p[2]) {
    case 0x06: c.type = "MIFARE Ultralight"; break;
    case 0x12: c.type = "NTAG213"; break;
    case 0x3E: c.type = "NTAG215"; break;
    case 0x6D: c.type = "NTAG216"; break;
    default:   c.type = "Type 2"; break;
  }
  c.t2Pages = 4 + (c.t2Cap ? c.t2Cap : 48) / 4;
}

// --- NDEF ----------------------------------------------------------------------

String buildNdef(JsonArrayConst records, std::vector<uint8_t> &out, const String &defaultLang) {
  std::vector<ndef::Record> recs;
  for (JsonObjectConst r : records) {
    ndef::Record rec;
    rec.type = (const char *)(r["type"] | "");
    rec.value = (const char *)(r["value"] | "");
    rec.lang = (const char *)(r["lang"] | defaultLang.c_str());
    recs.push_back(rec);
  }
  std::string err = ndef::encode(recs, out);
  return err.c_str();
}

// --- Jobs ----------------------------------------------------------------------

static bool fail(Job &j, const char *code, const String &msg) {
  j.code = code;
  j.message = msg;
  return false;
}

static bool done(Job &j, const String &msg) {
  j.code = "ok";
  j.message = msg;
  return true;
}

static bool readClassic(const CardInfo &c, const Job &job, JsonObject res) {
  res["kind"] = "mifare_classic";
  JsonArray sectors = res["sectors"].to<JsonArray>();
  for (int s = 0; s < c.sectors; s++) {
    JsonObject so = sectors.add<JsonObject>();
    so["sector"] = s;
    int first = sectorFirst(s);
    if (!pn.mfAuth(first, job.key, c.uid, c.uidLen)) {
      so["locked"] = true;
      if (!reselect(c)) return false;
      continue;
    }
    JsonArray blocks = so["blocks"].to<JsonArray>();
    for (int b = 0; b < sectorSize(s); b++) {
      uint8_t d[16];
      if (mfRetry(c, first + b, job.key, [&] { return pn.read16(first + b, d); })) blocks.add(hexStr(d, 16));
      else blocks.add(nullptr);
    }
  }
  return true;
}

static bool readT2(const CardInfo &c, JsonObject res) {
  res["kind"] = "type2";
  JsonArray pages = res["pages"].to<JsonArray>();
  std::vector<uint8_t> user;
  for (int p = 0; p < c.t2Pages; p += 4) {
    uint8_t d[16];
    if (!t2Read(c, p, d)) return false;
    for (int i = 0; i < 4 && p + i < c.t2Pages; i++) {
      pages.add(hexStr(d + 4 * i, 4));
      if (p + i >= 4) user.insert(user.end(), d + 4 * i, d + 4 * i + 4);
    }
  }
  JsonArray out = res["ndef"].to<JsonArray>();
  for (const ndef::Record &r : ndef::decode(user.data(), user.size())) {
    JsonObject o = out.add<JsonObject>();
    o["type"] = r.type.c_str();
    o["value"] = r.value.c_str();
    if (r.type == "text") o["lang"] = r.lang.c_str();
    if (r.type == "other") { o["tnf"] = r.tnf; o["record_type"] = r.recordType.c_str(); }
  }
  return true;
}

// Bytes in front of the NDEF message (factory Lock / Memory Control TLVs): kept as they are.
static bool t2Prefix(const CardInfo &c, std::vector<uint8_t> &prefix) {
  uint8_t head[16];
  if (!t2Read(c, 4, head)) return false;
  prefix.assign(head, head + ndef::prefixLength(head, sizeof head));
  return true;
}

// Writes the bytes from page 4, padded to a page, then zeroes up to page fillTo.
static bool t2WriteArea(const CardInfo &c, std::vector<uint8_t> bytes, int fillTo, Job &job) {
  static const uint8_t zero[4] = {0};
  while (bytes.size() % 4) bytes.push_back(0);
  int last = max(fillTo, (int)(4 + bytes.size() / 4 - 1));
  for (int p = 4; p <= last; p++) {
    size_t i = (p - 4) * 4;
    if (!t2Write(c, p, i < bytes.size() ? &bytes[i] : zero)) return fail(job, "write_failed", trf(M_WRITE_FAILED_PAGE, p));
  }
  return true;
}

static bool writeNdef(const CardInfo &c, Job &job) {
  if (c.kind != K_T2) return fail(job, "ndef_type2_only", tr(M_NDEF_T2_ONLY));
  if (!c.t2Cap) return fail(job, "not_ndef_formatted", tr(M_NOT_FORMATTED));
  std::vector<uint8_t> bytes;
  if (!t2Prefix(c, bytes)) return fail(job, "card_removed", tr(M_READ_FAILED));
  bytes.insert(bytes.end(), job.data.begin(), job.data.end());
  if ((int)bytes.size() > c.t2Cap) return fail(job, "too_long", trf(M_TOO_LONG, (int)bytes.size(), c.t2Cap));
  if (!t2WriteArea(c, bytes, 0, job)) return false;
  return done(job, trf(M_NDEF_WRITTEN, (int)job.data.size()));
}

static bool writeRaw(const CardInfo &c, Job &job) {
  int b = job.block;
  if (c.kind == K_CLASSIC) {
    if (job.data.size() > 16) return fail(job, "bad_data", tr(M_MAX_BLOCK));
    uint8_t buf[16] = {0};
    memcpy(buf, job.data.data(), job.data.size());
    int maxBlock = sectorFirst(c.sectors - 1) + sectorSize(c.sectors - 1);
    if (b <= 0 || b >= maxBlock) return fail(job, "out_of_range", trf(M_BLOCK_RANGE, maxBlock - 1));
    // Block 0 (manufacturer) and sector trailers (keys, access bits) are refused so a
    // mistake can never lock the card for good.
    if (isTrailer(b)) return fail(job, "protected_block", trf(M_TRAILER, b));
    if (!pn.mfAuth(sectorFirst(blockSector(b)), job.key, c.uid, c.uidLen)) return fail(job, "auth_failed", tr(M_AUTH_FAILED));
    if (!mfRetry(c, b, job.key, [&] { return pn.mfWrite(b, buf); })) return fail(job, "write_failed", tr(M_WRITE_FAILED));
    return done(job, trf(M_BLOCK_WRITTEN, b));
  }
  if (c.kind == K_T2) {
    if (job.data.size() > 4) return fail(job, "bad_data", tr(M_MAX_PAGE));
    uint8_t buf[4] = {0};
    memcpy(buf, job.data.data(), job.data.size());
    int last = lastUserPage(c);
    // Pages 0-3 (UID, lock bits, capability container) and configuration pages are refused.
    if (b < 4 || b > last) return fail(job, "out_of_range", trf(M_PAGE_RANGE, last));
    if (!t2Write(c, b, buf)) return fail(job, "write_failed", tr(M_WRITE_FAILED));
    return done(job, trf(M_PAGE_WRITTEN, b));
  }
  return fail(job, "unsupported_card", tr(M_UNSUPPORTED));
}

static bool erase(const CardInfo &c, Job &job) {
  if (c.kind == K_T2) {
    // Original prefix + empty NDEF message, then zeroes: the factory state.
    std::vector<uint8_t> bytes;
    if (!t2Prefix(c, bytes)) return fail(job, "card_removed", tr(M_READ_FAILED));
    bytes.insert(bytes.end(), {0x03, 0x00, 0xFE});
    int last = lastUserPage(c);
    if (!t2WriteArea(c, bytes, last, job)) return false;
    return done(job, trf(M_ERASED_T2, last - 3));
  }
  if (c.kind == K_CLASSIC) {
    static const uint8_t zero[16] = {0};
    int written = 0, locked = 0;
    for (int s = 0; s < c.sectors; s++) {
      int first = sectorFirst(s);
      if (!pn.mfAuth(first, job.key, c.uid, c.uidLen)) {
        locked++;
        if (!reselect(c)) return fail(job, "card_removed", tr(M_REMOVED));
        continue;
      }
      for (int b = first; b < first + sectorSize(s); b++) {
        if (b == 0 || isTrailer(b)) continue;
        if (!mfRetry(c, b, job.key, [&] { return pn.mfWrite(b, zero); })) return fail(job, "write_failed", trf(M_WRITE_FAILED_BLOCK, b));
        written++;
      }
    }
    String msg = trf(M_ERASED_CLASSIC, written);
    if (locked) msg += trf(M_ERASED_LOCKED, locked);
    return done(job, msg);
  }
  return fail(job, "unsupported_card", tr(M_UNSUPPORTED));
}

static bool runJob(const CardInfo &c, Job &job) {
  if (c.kind == K_OTHER) return fail(job, "unsupported_card", tr(M_UNSUPPORTED));
  switch (job.kind) {
    case J_READ: {
      JsonDocument doc;
      JsonObject res = doc.to<JsonObject>();
      bool ok = c.kind == K_CLASSIC ? readClassic(c, job, res) : readT2(c, res);
      if (!ok) return fail(job, "card_removed", tr(M_REMOVED));
      job.result = "";
      serializeJson(doc, job.result);
      return done(job, tr(M_READ_DONE));
    }
    case J_NDEF: return writeNdef(c, job);
    case J_RAW: return writeRaw(c, job);
    case J_ERASE: return erase(c, job);
  }
  return false;
}

static void emitJob(const Job &j) {
  JsonDocument d;
  jobToJson(d.to<JsonObject>(), j, false);
  emitEvent("job.updated", d);
}

static void setFlash(bool ok, const String &text) {
  Lock l;
  S.flashOk = ok;
  S.flashText = text;
  S.flashUntil = millis() + 2500;
  S.lastActivity = millis();
}

// --- Presence ------------------------------------------------------------------

static void onCard(CardInfo c, uint32_t now) {
  lastSeen = now;
  bool isNew;
  {
    Lock l;
    isNew = !S.present || c.uidLen != S.card.uidLen || memcmp(c.uid, S.card.uid, c.uidLen) != 0;
  }
  if (isNew) {
    identify(c);
    String uid = hexStr(c.uid, c.uidLen, ':');
    {
      Lock l;
      S.card = c;
      S.present = true;
      S.lastActivity = now;
      S.history.push_back({uid, c.type, now, nowEpoch()});
      if (S.history.size() > HISTORY_SIZE) S.history.erase(S.history.begin());
    }
    Serial.printf("Badge %s  %s\n", c.type.c_str(), uid.c_str());
    JsonDocument d;
    cardToJson(d.to<JsonObject>(), c);
    emitEvent("card.present", d);
  } else {
    Lock l;
    c = S.card;  // keep the identification already done
  }

  Job job;
  {
    Lock l;
    Job *j = activeJob();
    if (!j || j->state != S_WAITING) return;
    j->state = S_RUNNING;
    j->uid = hexStr(c.uid, c.uidLen, ':');
    job = *j;
  }
  emitJob(job);
  bool ok = runJob(c, job);
  job.state = ok ? S_DONE : S_ERROR;
  Serial.printf("[job %u] %s: %s\n", job.id, job.code.c_str(), job.message.c_str());
  {
    Lock l;
    if (Job *j = findJob(job.id)) *j = job;
  }
  setFlash(ok, job.message);
  emitJob(job);
}

static void onRemoved() {
  String uid;
  {
    Lock l;
    if (!S.present) return;
    S.present = false;
    uid = hexStr(S.card.uid, S.card.uidLen, ':');
  }
  Serial.println("Badge removed");
  JsonDocument d;
  d["uid"] = uid;
  emitEvent("card.removed", d);
}

static void checkJobTimeout(uint32_t now) {
  Job job;
  {
    Lock l;
    Job *j = activeJob();
    if (!j || j->state != S_WAITING || (int32_t)(now - j->deadline) < 0) return;
    j->state = S_TIMEOUT;
    j->code = "timeout";
    j->message = tr(M_TIMEOUT);
    job = *j;
  }
  setFlash(false, job.message);
  emitJob(job);
}

void nfcTick() {
  uint32_t now = millis();
  bool readerOk, present;
  { Lock l; readerOk = S.readerOk; present = S.present; }

  if (!readerOk) {
    if (now - lastRetry > 3000) { lastRetry = now; tryReader(); }
    return;
  }
  checkJobTimeout(now);

  if (listening) {
    if (pn.ready()) {
      listening = false;
      CardInfo c;
      c.uidLen = pn.listenResult(c.uid, c.sak, c.atqa);
      if (c.uidLen) { failures = 0; onCard(c, now); }
      else if (++failures > 5) readerLost();
    } else if (present && now - listenStart > 500) {
      onRemoved();  // keep listening: the next badge is seen right away
    } else if (!present && now - lastHealth > 30000) {
      // Periodic health check while idle
      lastHealth = now;
      pn.abort();
      listening = false;
      if (!pn.firmware()) readerLost();
    }
    return;
  }

  // Badge in place: detect it again every ~250 ms to notice when it leaves.
  if (present && now - lastSeen < 250) return;
  if (present) { pn.field(false); delay(2); pn.field(true); }
  if (pn.startListen()) {
    listening = true;
    listenStart = millis();
  } else if (++failures > 5) {
    readerLost();
  }
}
