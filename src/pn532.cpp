#include "pn532.h"

static const uint8_t ADDR = 0x24;
static const uint8_t LIST_TARGET[] = {0x4A, 0x01, 0x00};  // InListPassiveTarget, 1 target, ISO14443A 106 kbps

bool PN532::begin(TwoWire &w, int irq) {
  _w = &w;
  _irq = irq;
  if (_irq >= 0) pinMode(_irq, INPUT_PULLUP);
  // The PN532 takes a moment to wake up after power-on.
  for (int i = 0; i < 3; i++) {
    if (firmware()) {
      const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01};          // SAMConfiguration: normal mode, IRQ enabled
      uint8_t r[4];
      if (command(sam, sizeof sam, r, sizeof r, 100) < 0) return false;
      // Unlimited passive activation retries: InListPassiveTarget waits for a badge until aborted.
      const uint8_t retry[] = {0x32, 0x05, 0xFF, 0x01, 0xFF};
      command(retry, sizeof retry, r, sizeof r, 100);
      return true;
    }
    delay(100);
  }
  return false;
}

uint32_t PN532::firmware() {
  const uint8_t c[] = {0x02};
  uint8_t r[4];
  if (command(c, 1, r, 4, 100) != 4) return 0;
  return (uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3];
}

// --- Asynchronous listening ----------------------------------------------------

bool PN532::startListen() {
  if (!writeFrame(LIST_TARGET, sizeof LIST_TARGET)) {
    delay(5);
    if (!writeFrame(LIST_TARGET, sizeof LIST_TARGET)) return false;
  }
  return waitReady(30) && readAck();
}

bool PN532::ready() {
  if (_irq >= 0) return digitalRead(_irq) == LOW;
  // Without IRQ: read the status byte, spaced out to leave the bus to the display.
  if (millis() - _lastStatus < 25) return false;
  _lastStatus = millis();
  return statusReady();
}

uint8_t PN532::listenResult(uint8_t *uid, uint8_t &sak, uint16_t &atqa) {
  uint8_t r[20];
  int n = readResponse(LIST_TARGET[0], r, sizeof r);
  return parseTarget(r, n, uid, sak, atqa);
}

void PN532::abort() {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  _w->beginTransmission(ADDR);
  _w->write(ack, sizeof ack);
  _w->endTransmission();
  delay(3);
}

// --- Blocking commands ----------------------------------------------------------

uint8_t PN532::parseTarget(const uint8_t *r, int n, uint8_t *uid, uint8_t &sak, uint16_t &atqa) {
  if (n < 6 || r[0] == 0) return 0;
  atqa = (uint16_t)r[2] << 8 | r[3];
  sak = r[4];
  uint8_t len = r[5];
  if (len < 4 || len > 10 || n < 6 + len) return 0;
  memcpy(uid, r + 6, len);
  return len;
}

uint8_t PN532::detect(uint8_t *uid, uint8_t &sak, uint16_t &atqa) {
  uint8_t r[20];
  int n = command(LIST_TARGET, sizeof LIST_TARGET, r, sizeof r, 60);
  return parseTarget(r, n, uid, sak, atqa);
}

bool PN532::field(bool on) {
  const uint8_t c[] = {0x32, 0x01, (uint8_t)(on ? 0x01 : 0x00)};
  uint8_t r[2];
  return command(c, sizeof c, r, sizeof r, 50) >= 0;
}

bool PN532::mfAuth(uint8_t block, const uint8_t key[6], const uint8_t *uid, uint8_t uidLen) {
  uint8_t c[12] = {0x60, block};           // key A authentication
  memcpy(c + 2, key, 6);
  memcpy(c + 8, uid + (uidLen - 4), 4);    // last 4 bytes of the UID
  uint8_t r[2];
  return exchange(c, sizeof c, r, sizeof r) >= 0;
}

bool PN532::read16(uint8_t block, uint8_t out[16]) {
  const uint8_t c[] = {0x30, block};
  uint8_t r[16];
  if (exchange(c, sizeof c, r, sizeof r) != 16) return false;
  memcpy(out, r, 16);
  return true;
}

bool PN532::mfWrite(uint8_t block, const uint8_t data[16]) {
  uint8_t c[18] = {0xA0, block};
  memcpy(c + 2, data, 16);
  uint8_t r[2];
  return exchange(c, sizeof c, r, sizeof r) >= 0;
}

bool PN532::ulWrite(uint8_t page, const uint8_t data[4]) {
  uint8_t c[6] = {0xA2, page};
  memcpy(c + 2, data, 4);
  uint8_t r[2];
  return exchange(c, sizeof c, r, sizeof r) >= 0;
}

// --- Low level -------------------------------------------------------------------

bool PN532::writeFrame(const uint8_t *d, uint8_t n) {
  uint8_t len = n + 1;
  uint8_t sum = 0xD4;
  _w->beginTransmission(ADDR);
  _w->write(0x00); _w->write(0x00); _w->write(0xFF);
  _w->write(len); _w->write((uint8_t)(~len + 1));
  _w->write(0xD4);
  for (uint8_t i = 0; i < n; i++) { _w->write(d[i]); sum += d[i]; }
  _w->write((uint8_t)(~sum + 1));
  _w->write(0x00);
  return _w->endTransmission() == 0;
}

bool PN532::statusReady() {
  return _w->requestFrom(ADDR, (size_t)1) == 1 && (_w->read() & 0x01);
}

bool PN532::waitReady(uint16_t timeoutMs) {
  uint32_t t = millis();
  do {
    if (_irq >= 0 ? digitalRead(_irq) == LOW : statusReady()) return true;
    delay(1);
  } while (millis() - t < timeoutMs);
  return false;
}

bool PN532::readAck() {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  if (_w->requestFrom(ADDR, (size_t)7) != 7) return false;
  _w->read();  // status byte
  bool ok = true;
  for (uint8_t i = 0; i < 6; i++) if (_w->read() != ack[i]) ok = false;
  return ok;
}

int PN532::readResponse(uint8_t cmd, uint8_t *out, uint8_t maxOut) {
  uint8_t b[64];
  size_t n = _w->requestFrom(ADDR, (size_t)sizeof b);
  for (size_t i = 0; i < n; i++) b[i] = _w->read();
  // b[0] = status, then the 00 00 FF preamble
  for (size_t i = 1; i + 6 < n; i++) {
    if (b[i] != 0x00 || b[i + 1] != 0xFF) continue;
    uint8_t len = b[i + 2], lcs = b[i + 3];
    if ((uint8_t)(len + lcs) != 0) return -1;
    if (len < 2 || i + 4 + len > n) return -1;
    if (b[i + 4] != 0xD5 || b[i + 5] != (uint8_t)(cmd + 1)) return -1;
    uint8_t plen = len - 2;
    if (plen > maxOut) plen = maxOut;
    memcpy(out, b + i + 6, plen);
    return plen;
  }
  return -1;
}

int PN532::command(const uint8_t *d, uint8_t n, uint8_t *out, uint8_t maxOut, uint16_t timeoutMs) {
  if (!writeFrame(d, n)) {
    delay(5);  // the first access may only wake it up, without an ACK
    if (!writeFrame(d, n)) return -1;
  }
  if (!waitReady(30) || !readAck()) return -1;
  if (!waitReady(timeoutMs)) { abort(); return -1; }
  return readResponse(d[0], out, maxOut);
}

int PN532::exchange(const uint8_t *d, uint8_t n, uint8_t *out, uint8_t maxOut) {
  uint8_t c[32] = {0x40, 0x01};  // InDataExchange, target 1
  if (n > sizeof c - 2) return -1;
  memcpy(c + 2, d, n);
  uint8_t r[24];
  int rn = command(c, n + 2, r, sizeof r, 150);
  if (rn < 1 || (r[0] & 0x3F) != 0) return -1;
  int plen = rn - 1;
  if (plen > maxOut) plen = maxOut;
  memcpy(out, r + 1, plen);
  return plen;
}
